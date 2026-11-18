#include "Engine/StaticMesh.h"
#include "GWContentArt.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "Stateless/Modules/NiagaraStatelessModule_AddVelocity.h"
#include "Stateless/Modules/NiagaraStatelessModule_ApplyOwnerScaleToAttributes.h"
#include "Stateless/Modules/NiagaraStatelessModule_CurlNoiseForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_Drag.h"
#include "Stateless/Modules/NiagaraStatelessModule_DynamicMaterialParameters.h"
#include "Stateless/Modules/NiagaraStatelessModule_GravityForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitialMeshOrientation.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitializeParticle.h"
#include "Stateless/Modules/NiagaraStatelessModule_MeshIndex.h"
#include "Stateless/Modules/NiagaraStatelessModule_MeshRotationRate.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleColor.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleMeshSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleSpriteSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_ShapeLocation.h"
#include "Stateless/Modules/NiagaraStatelessModule_SpriteFacingAndAlignment.h"
#include "Stateless/Modules/NiagaraStatelessModule_SpriteRotationRate.h"
#include "Stateless/Modules/NiagaraStatelessModule_SubUVAnimation.h"
#include "Stateless/NiagaraStatelessEmitter.h"
#include "UObject/Package.h"

// Lightweight (stateless) Niagara systems for the paper diorama. Every cue is pieced together from geometry
// (design: 几何图形拼接): ruler-straight starbursts, polygon outlines a pen draws on and rubs out, card-stock
// plates that tumble out of a burst or fly together into a shape, chips, dashes, hatched scorch marks and ink.
// Colour, beam size and beam direction are user parameters set per spawn.
namespace
{
FLinearColor Hex(uint32 RGB)
{
    return FLinearColor(FColor((RGB >> 16) & 255, (RGB >> 8) & 255, RGB & 255));
}
// The HUD's sketchbook palette (GWPalette), so a cue and the card that announces it share their colours.
const FLinearColor Ink = Hex(0x1F2633), InkBlue = Hex(0x2B3A67), Paper = Hex(0xF6F0E1), PaperDust = Hex(0xC9BBA0),
                   Kraft = Hex(0xD6BC90), Graphite = Hex(0x5E6678), Coral = Hex(0xE8553F), Cobalt = Hex(0x2F6BD8),
                   Marigold = Hex(0xF2A93B), Teal = Hex(0x26A796), Mint = Hex(0x9AD8CB), Violet = Hex(0x8E5CD9),
                   Fire = Hex(0xFF6A2B), Spark = Hex(0xFFD84A), Leaf = Hex(0x4DBA5C), Frost = Hex(0x5EC8E8),
                   Ice = Hex(0x3C8DBC), Danger = Hex(0xE23A33), Plum = Hex(0x4B3F72), Eraser = Hex(0xEE9B9B);

enum class EFacing
{
    Camera,
    Ground,
    Velocity,
    Beam
};
enum class EShape
{
    Point,
    Sphere,
    Ring,
    Box
};
enum class EVelocity
{
    None,
    Radial,
    Cone,
    Linear
};
// How a 2x2 sheet's sub image is chosen: flickering at random (bolts), one picked for life, or grown through in
// order over the particle's life (triangle -> square -> pentagon -> hexagon).
enum class ESub
{
    Flicker,
    Pick,
    Morph
};
const TCHAR* const PlateTri = TEXT("SM_GW_PlateTri");
const TCHAR* const PlateSquare = TEXT("SM_GW_PlateSquare");
const TCHAR* const PlatePent = TEXT("SM_GW_PlatePent");
const TCHAR* const PlateHex = TEXT("SM_GW_PlateHex");
const TCHAR* const PlateRhomb = TEXT("SM_GW_PlateRhomb");
const TArray<const TCHAR*> Plates = {PlateTri, PlateSquare, PlatePent, PlateHex, PlateRhomb};
const TArray<const TCHAR*> Blocks = {PlateSquare, PlateRhomb, PlateSquare, TEXT("SM_GW_Shard")};

struct FLayer
{
    const TCHAR* Texture = TEXT("Star");
    const TCHAR* Mesh = nullptr;
    // Several meshes: each particle picks one (card-stock plates of mixed shapes).
    TArray<const TCHAR*> Meshes;
    int32 SubImages = 1;
    ESub Sub = ESub::Flicker;
    // Pick: the sub images to choose from (Y < 0: through the last one).
    FIntPoint Frames{0, -1};
    EFacing Facing = EFacing::Camera;
    bool Loop = false;
    float Rate = 0;
    int32 Count = 1, CountMax = 0;
    float Delay = 0;
    FVector2f Life{.3f, .5f};
    bool UserColor = true;
    FLinearColor Color = FLinearColor::White;
    FVector2f Size{20, 30};
    FVector2f Stretch{0, 0}; // fixed non-square sprite size (width, length)
    FVector2f Rotation{0, 360};
    FVector2f Spin{0, 0};
    EShape Shape = EShape::Point;
    float Radius = 0;
    FVector3f Box{0, 0, 0};
    FVector3f Offset{0, 0, 0};
    EVelocity Velocity = EVelocity::None;
    FVector2f Speed{0, 0};
    FVector3f Axis{0, 0, 1};
    float Cone = 30;
    FVector3f LinearMin{0, 0, 0}, LinearMax{0, 0, 0};
    float Drag = 0;
    FVector3f Gravity{0, 0, 0};
    float Noise = 0;
    TArray<float> SizeCurve;
    TArray<FLinearColor> ColorCurve;
    FVector2f MeshScale{.1f, .2f};
    // The pen sweep over life as (age, value) keys: Reveal draws the outline on, Keep rubs it out. Empty: whole.
    TArray<FVector2f> Reveal, Keep;
};

template <class T> T& Prop(UObject* Object, const TCHAR* Name)
{
    FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), Name);
    check(Property);
    return *Property->ContainerPtrToValuePtr<T>(Object);
}
template <class T> T* Module(UNiagaraStatelessEmitter* Emitter, bool Enabled = true)
{
    T* Found = Cast<T>(Emitter->GetModule(T::StaticClass()));
    check(Found);
    Found->SetIsModuleEnabled(Enabled);
    return Found;
}
void Range(FNiagaraDistributionRangeVector3& D, FVector3f A, FVector3f B)
{
    D.Mode = ENiagaraDistributionMode::NonUniformRange;
    D.Min = A;
    D.Max = B;
    D.ChannelConstantsAndRanges = {A.X, A.Y, A.Z, B.X, B.Y, B.Z};
}
void Uniform(FNiagaraDistributionRangeVector2& D, float A, float B)
{
    D.Mode = ENiagaraDistributionMode::UniformRange;
    D.Min = FVector2f(A, A);
    D.Max = FVector2f(B, B);
    D.ChannelConstantsAndRanges = {A, B};
}
void Uniform(FNiagaraDistributionRangeVector3& D, float A, float B)
{
    D.Mode = ENiagaraDistributionMode::UniformRange;
    D.Min = FVector3f(A);
    D.Max = FVector3f(B);
    D.ChannelConstantsAndRanges = {A, B};
}
void IntRange(FNiagaraDistributionRangeInt& D, int32 A, int32 B)
{
    if (A == B)
    {
        D.InitConstant(A);
        return;
    }
    D.Mode = ENiagaraDistributionMode::UniformRange;
    D.Min = A;
    D.Max = B;
}
void Bind(FNiagaraDistributionBase& D, const FNiagaraTypeDefinition& Type, const TCHAR* Name)
{
    D.Mode = ENiagaraDistributionMode::Binding;
    D.ParameterBinding = FNiagaraVariableBase(Type, Name);
}
FRichCurve Curve(TArrayView<const float> Keys)
{
    FRichCurve Out;
    for (int32 I = 0; I < Keys.Num(); ++I)
    {
        const FKeyHandle Key = Out.AddKey(Keys.Num() > 1 ? I / float(Keys.Num() - 1) : 0.f, Keys[I]);
        Out.SetKeyInterpMode(Key, RCIM_Linear);
    }
    return Out;
}
// Over-life multipliers sampled by Particles.NormalizedAge.
void SizeOverLife(FNiagaraDistributionBase& D, const TArray<float>& Keys)
{
    D.Mode = ENiagaraDistributionMode::UniformCurve;
    D.ChannelCurves.Reset();
    D.ChannelCurves.Add(Curve(Keys));
    D.LookupValueMode = uint8(ENiagaraDistributionLookupValueMode::ParticlesNormalizedAge);
    D.UpdateValuesFromDistribution();
}
void ColorOverLife(FNiagaraDistributionColor& D, const TArray<FLinearColor>& Keys)
{
    D.Mode = ENiagaraDistributionMode::ColorGradient;
    D.ChannelCurves.Reset();
    for (int32 Channel = 0; Channel < 4; ++Channel)
    {
        TArray<float> Values;
        for (const auto& Key : Keys)
            Values.Add(Channel == 0 ? Key.R : Channel == 1 ? Key.G : Channel == 2 ? Key.B : Key.A);
        D.ChannelCurves.Add(Curve(Values));
    }
    D.LookupValueMode = uint8(ENiagaraDistributionLookupValueMode::ParticlesNormalizedAge);
    D.UpdateValuesFromDistribution();
}
// A timed curve over life (keys at explicit ages), for the pen sweep.
void FloatOverLife(FNiagaraDistributionFloat& D, const TArray<FVector2f>& Keys, float Whole)
{
    if (Keys.IsEmpty())
    {
        D.InitConstant(Whole);
        return;
    }
    D.Mode = ENiagaraDistributionMode::UniformCurve;
    D.ChannelCurves.Reset();
    FRichCurve& Out = D.ChannelCurves.AddDefaulted_GetRef();
    for (const FVector2f& Key : Keys)
        Out.SetKeyInterpMode(Out.AddKey(Key.X, Key.Y), RCIM_Linear);
    D.LookupValueMode = uint8(ENiagaraDistributionLookupValueMode::ParticlesNormalizedAge);
    D.UpdateValuesFromDistribution();
}
TArray<FLinearColor> Fade(float Peak = 1, float Hold = .6f)
{
    return {FLinearColor(1, 1, 1, Peak), FLinearColor(1, 1, 1, Peak * Hold + (1 - Hold) * Peak * .9f), FLinearColor(1, 1, 1, 0)};
}
TArray<FLinearColor> FadeInOut(float Peak = 1)
{
    return {FLinearColor(1, 1, 1, 0), FLinearColor(1, 1, 1, Peak), FLinearColor(1, 1, 1, Peak), FLinearColor(1, 1, 1, 0)};
}
// Holds full strength for most of its life, then goes in the last stretch (outlines a sweep rubs out).
TArray<FLinearColor> Hold(float Peak = 1)
{
    return {FLinearColor(1, 1, 1, Peak), FLinearColor(1, 1, 1, Peak), FLinearColor(1, 1, 1, Peak), FLinearColor(1, 1, 1, Peak * .7f),
            FLinearColor(1, 1, 1, 0)};
}
// Pen sweeps: drawn on over the first In of a life (quick at first, easing into the last stretch like a hand
// closing a circle), rubbed out from From onward.
TArray<FVector2f> DrawOn(float In)
{
    return {{0, 0}, {In * .45f, .68f}, {In * .75f, .93f}, {In, 1}, {1, 1}};
}
TArray<FVector2f> RubOut(float From)
{
    return {{0, 1}, {From, 1}, {From + (1 - From) * .55f, .35f}, {1, 0}};
}

void Configure(UNiagaraStatelessEmitter* E, const FLayer& L, float Bounds, const TMap<FName, UMaterialInterface*>& Materials,
               const TMap<FName, UStaticMesh*>& Meshes)
{
    const bool MeshLayer = L.Mesh || !L.Meshes.IsEmpty();
    auto& State = Prop<FNiagaraEmitterStateData>(E, TEXT("EmitterState"));
    State.LoopBehavior = L.Loop ? ENiagaraLoopBehavior::Infinite : ENiagaraLoopBehavior::Once;
    State.LoopDuration.InitConstant(L.Loop ? 1.f : FMath::Max(.1f, L.Delay + .05f));
    Prop<FBox>(E, TEXT("FixedBounds")) = FBox(FVector(-Bounds), FVector(Bounds));

    FNiagaraStatelessSpawnInfo& Spawn = *E->GetSpawnInfoByIndex(0);
    if (L.Loop && L.Rate > 0)
    {
        Spawn.Type = ENiagaraStatelessSpawnInfoType::Rate;
        Spawn.Rate.InitConstant(L.Rate);
    }
    else
    {
        Spawn.Type = ENiagaraStatelessSpawnInfoType::Burst;
        Spawn.SpawnTime = L.Delay;
        Spawn.Amount.InitConstant(L.Count);
        if (L.CountMax > L.Count)
        {
            Spawn.Amount.Mode = ENiagaraDistributionMode::UniformRange;
            Spawn.Amount.Min = L.Count;
            Spawn.Amount.Max = L.CountMax;
        }
    }

    auto* Init = Module<UNiagaraStatelessModule_InitializeParticle>(E);
    Init->LifetimeDistribution.InitRange(L.Life.X, L.Life.Y);
    if (L.UserColor)
        Bind(Init->ColorDistribution, FNiagaraTypeDefinition::GetColorDef(), TEXT("User.Color"));
    else
        Init->ColorDistribution.InitConstant(L.Color);
    if (L.Facing == EFacing::Beam)
        Bind(Init->SpriteSizeDistribution, FNiagaraTypeDefinition::GetVec2Def(), TEXT("User.BeamSize"));
    else if (L.Stretch.X > 0)
        Init->SpriteSizeDistribution.InitConstant(L.Stretch);
    else
        Uniform(Init->SpriteSizeDistribution, L.Size.X, L.Size.Y);
    Init->SpriteRotationDistribution.InitRange(L.Rotation.X, L.Rotation.Y);
    Uniform(Init->MeshScaleDistribution, L.MeshScale.X, L.MeshScale.Y);

    auto* Shape = Module<UNiagaraStatelessModule_ShapeLocation>(E, L.Shape != EShape::Point || !L.Offset.IsZero());
    Shape->ShapePrimitive = L.Shape == EShape::Ring ? ENSM_ShapePrimitive::Ring
                            : L.Shape == EShape::Box ? ENSM_ShapePrimitive::Box
                                                     : ENSM_ShapePrimitive::Sphere;
    Shape->SphereRadius.InitConstant(L.Shape == EShape::Point ? 0.f : L.Radius);
    Shape->RingRadius.InitConstant(L.Radius);
    Shape->DiscCoverage.InitConstant(0);
    Shape->BoxSize.InitConstant(L.Box);
    Shape->TransformOffset.InitConstant(L.Offset);

    auto* Velocity = Module<UNiagaraStatelessModule_AddVelocity>(E, L.Velocity != EVelocity::None);
    if (L.Velocity == EVelocity::Radial)
    {
        // A negative speed pulls particles in toward the middle: pieces flying together into a shape.
        Velocity->VelocityType = ENSM_VelocityType::FromPoint;
        Velocity->PointVelocityDistribution.InitRange(L.Speed.X, L.Speed.Y);
        Velocity->PointOrigin = FVector3f::ZeroVector;
    }
    else if (L.Velocity == EVelocity::Cone)
    {
        Velocity->VelocityType = ENSM_VelocityType::InCone;
        Velocity->ConeVelocityDistribution.InitRange(L.Speed.X, L.Speed.Y);
        Velocity->ConeRotationType = ENSM_ConeRotationType::Direction;
        Velocity->ConeDirection.InitConstant(L.Axis);
        Velocity->ConeAngle = L.Cone;
    }
    else if (L.Velocity == EVelocity::Linear)
    {
        Velocity->VelocityType = ENSM_VelocityType::Linear;
        Range(Velocity->LinearVelocityDistribution, L.LinearMin, L.LinearMax);
    }
    Module<UNiagaraStatelessModule_Drag>(E, L.Drag > 0)->DragDistribution.InitConstant(L.Drag);
    Module<UNiagaraStatelessModule_GravityForce>(E, !L.Gravity.IsZero())->GravityDistribution.InitConstant(L.Gravity);
    auto* Noise = Module<UNiagaraStatelessModule_CurlNoiseForce>(E, L.Noise > 0);
    Noise->NoiseStrength = L.Noise;
    Noise->NoiseFrequency = 60;
    Module<UNiagaraStatelessModule_ApplyOwnerScaleToAttributes>(E)->SystemScaleData.bScaleInitialSpriteSize = true;

    if (!L.ColorCurve.IsEmpty())
        ColorOverLife(Module<UNiagaraStatelessModule_ScaleColor>(E)->ScaleDistribution, L.ColorCurve);
    else
        Module<UNiagaraStatelessModule_ScaleColor>(E, false);
    const bool Sized = !L.SizeCurve.IsEmpty();
    auto* Index = Module<UNiagaraStatelessModule_MeshIndex>(E, L.Meshes.Num() > 1);
    IntRange(Index->MeshIndex, 0, FMath::Max(0, L.Meshes.Num() - 1));
    if (MeshLayer)
    {
        auto* Scale = Module<UNiagaraStatelessModule_ScaleMeshSize>(E, Sized);
        if (Sized)
            SizeOverLife(Scale->ScaleDistribution, L.SizeCurve);
        Module<UNiagaraStatelessModule_ScaleSpriteSize>(E, false);
        Module<UNiagaraStatelessModule_InitialMeshOrientation>(E)->MeshOrientationMode = ENSMInitialMeshOrientationMode::Random;
        auto* Spin = Module<UNiagaraStatelessModule_MeshRotationRate>(E, L.Spin.Y != 0);
        Range(Spin->RotationRateDistribution, FVector3f(L.Spin.X), FVector3f(L.Spin.Y));
    }
    else
    {
        auto* Scale = Module<UNiagaraStatelessModule_ScaleSpriteSize>(E, Sized);
        if (Sized)
            SizeOverLife(Scale->ScaleDistribution, L.SizeCurve);
        Module<UNiagaraStatelessModule_SpriteRotationRate>(E, L.Spin.Y != 0)->RotationRateDistribution.InitRange(L.Spin.X, L.Spin.Y);
    }
    // The pen sweep feeds the sprite material's dynamic parameter (x: drawn on, y: kept); 1/1 is the whole drawing.
    auto* Sweep = Module<UNiagaraStatelessModule_DynamicMaterialParameters>(E, !MeshLayer);
    Sweep->bParameter0Enabled = true;
    Sweep->bParameter1Enabled = Sweep->bParameter2Enabled = Sweep->bParameter3Enabled = false;
    Sweep->Parameter0.bXChannelEnabled = Sweep->Parameter0.bYChannelEnabled = true;
    Sweep->Parameter0.bZChannelEnabled = Sweep->Parameter0.bWChannelEnabled = false;
    FloatOverLife(Sweep->Parameter0.XChannelDistribution, L.Reveal, 1);
    FloatOverLife(Sweep->Parameter0.YChannelDistribution, L.Keep, 1);

    auto* Facing = Module<UNiagaraStatelessModule_SpriteFacingAndAlignment>(E, L.Facing == EFacing::Ground || L.Facing == EFacing::Beam);
    Facing->bSpriteFacingEnabled = L.Facing == EFacing::Ground;
    Facing->SpriteFacing.InitConstant(FVector3f(0, 0, 1));
    Facing->bSpriteAlignmentEnabled = L.Facing == EFacing::Beam || L.Facing == EFacing::Ground;
    if (L.Facing == EFacing::Beam)
        Bind(Facing->SpriteAlignment, FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.BeamDir"));
    else
        Facing->SpriteAlignment.InitConstant(FVector3f(1, 0, 0));
    auto* SubUV = Module<UNiagaraStatelessModule_SubUVAnimation>(E, L.SubImages > 1);
    SubUV->NumFrames = L.SubImages * L.SubImages;
    if (L.Sub == ESub::Pick)
    {
        SubUV->AnimationMode = ENSMSubUVAnimation_Mode::DirectSet;
        IntRange(SubUV->FrameIndex, L.Frames.X, L.Frames.Y < 0 ? SubUV->NumFrames - 1 : L.Frames.Y);
    }
    else if (L.Sub == ESub::Morph)
        SubUV->AnimationMode = ENSMSubUVAnimation_Mode::Linear;
    else
    {
        SubUV->AnimationMode = ENSMSubUVAnimation_Mode::Random;
        SubUV->RandomChangeInterval = .045f;
    }

    UNiagaraRendererProperties* Existing = E->GetRenderers().IsEmpty() ? nullptr : E->GetRenderers()[0];
    if (MeshLayer)
    {
        auto* Renderer = NewObject<UNiagaraMeshRendererProperties>(E, NAME_None, RF_Transactional);
        Renderer->Meshes.Reset();
        for (const TCHAR* Name : L.Meshes.IsEmpty() ? TArray<const TCHAR*>{L.Mesh} : L.Meshes)
        {
            Renderer->Meshes.AddDefaulted_GetRef().Mesh = Meshes.FindRef(Name);
            check(Renderer->Meshes.Last().Mesh);
        }
        Renderer->bOverrideMaterials = true;
        FNiagaraMeshMaterialOverride Override;
        Override.ExplicitMat = Materials.FindRef(TEXT("M_GW_FXMesh"));
        Renderer->OverrideMaterials.Add(Override);
        if (Existing)
            E->RemoveRenderer(Existing, FGuid());
        E->AddRenderer(Renderer, FGuid());
    }
    else if (auto* Sprite = Cast<UNiagaraSpriteRendererProperties>(Existing))
    {
        Sprite->Material = Materials.FindRef(*(FString(TEXT("MI_GW_FX_")) + L.Texture));
        check(Sprite->Material);
        Sprite->SubImageSize = FVector2D(L.SubImages, L.SubImages);
        Sprite->bSubImageBlend = false;
        Sprite->SortMode = ENiagaraSortMode::None;
        Sprite->Alignment = L.Facing == EFacing::Velocity ? ENiagaraSpriteAlignment::VelocityAligned
                            : L.Facing == EFacing::Camera ? ENiagaraSpriteAlignment::Unaligned
                                                          : ENiagaraSpriteAlignment::CustomAlignment;
        Sprite->FacingMode = L.Facing == EFacing::Ground ? ENiagaraSpriteFacingMode::CustomFacingVector
                                                         : ENiagaraSpriteFacingMode::FaceCamera;
    }
    E->PostEditChange();
}

int32 BuildSystem(const FString& Name, const TArray<FLayer>& Layers, float Bounds, const TMap<FName, UMaterialInterface*>& Materials,
                  const TMap<FName, UStaticMesh*>& Meshes)
{
    static UNiagaraSystem* Template =
        LoadObject<UNiagaraSystem>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Systems/MinimalLightweight.MinimalLightweight"));
    check(Template);
    UPackage* Package = GWContent::NewAssetPackage(TEXT("/Game/Sketch/VFX/") + Name);
    auto* System = CastChecked<UNiagaraSystem>(
        StaticDuplicateObject(Template, Package, *Name, RF_Public | RF_Standalone | RF_Transactional));
    check(System->GetEmitterHandles().Num() == 1);
    UNiagaraStatelessEmitter* Source = System->GetEmitterHandles()[0].GetStatelessEmitter();
    check(Source);
    TArray<UNiagaraStatelessEmitter*> Pristine;
    for (int32 I = 1; I < Layers.Num(); ++I)
        Pristine.Add(DuplicateObject<UNiagaraStatelessEmitter>(Source, System));
    for (int32 I = 1; I < Layers.Num(); ++I)
    {
        UNiagaraStatelessEmitter* Copy = Pristine[I - 1];
        Copy->SetUniqueEmitterName(FString::Printf(TEXT("Layer%d"), I));
        FNiagaraEmitterHandle Handle(*Copy);
        System->AddEmitterHandleDirect(Handle);
    }
    System->GetEmitterHandles()[0].SetName(TEXT("Layer0"), *System);
    auto& Store = System->GetExposedParameters();
    const FNiagaraVariable ColorVar(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.Color"));
    Store.AddParameter(ColorVar, true, false);
    Store.SetParameterValue(FLinearColor::White, ColorVar);
    for (const FLayer& L : Layers)
        if (L.Facing == EFacing::Beam)
        {
            const FNiagaraVariable SizeVar(FNiagaraTypeDefinition::GetVec2Def(), TEXT("User.BeamSize"));
            const FNiagaraVariable DirVar(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.BeamDir"));
            Store.AddParameter(SizeVar, true, false);
            Store.AddParameter(DirVar, true, false);
            Store.SetParameterValue(FVector2f(40, 300), SizeVar);
            Store.SetParameterValue(FVector3f(1, 0, 0), DirVar);
            break;
        }
    for (int32 I = 0; I < Layers.Num(); ++I)
    {
        UNiagaraStatelessEmitter* Emitter = System->GetEmitterHandles()[I].GetStatelessEmitter();
        check(Emitter);
        Configure(Emitter, Layers[I], Bounds, Materials, Meshes);
    }
    System->PostEditChange();
    System->RequestCompile(false);
    System->WaitForCompilationComplete(false, false);
    UE_LOG(LogGWContent, Display, TEXT("NIAGARA %s layers=%d handles=%d"), *Name, Layers.Num(), System->GetEmitterHandles().Num());
    return GWContent::SaveAsset(System) ? 1 : 0;
}

// Building blocks shared by the systems below.
// The ruler-straight starburst popping open: the brightest frame of a cue.
FLayer FlashLayer(FVector2f Size, FVector2f Life, float Z = 0, float Delay = 0)
{
    return FLayer{.Texture = TEXT("Burst8"), .Delay = Delay, .Life = Life, .Size = Size, .Offset{0, 0, Z}, .SizeCurve{.45f, 1.2f, 1.f},
                  .ColorCurve = Fade(1, .7f)};
}
// A polygon outline (sheet frames From..To) drawn on by the pen and rubbed out again.
FLayer OutlineLayer(int32 From, int32 To, FVector2f Size, FVector2f Life, EFacing Facing, float Z = 0)
{
    return FLayer{.Texture = TEXT("Poly"), .SubImages = 2, .Sub = ESub::Pick, .Frames{From, To}, .Facing = Facing, .Life = Life, .Size = Size,
                  .Spin{-90, 90}, .Offset{0, 0, Z}, .SizeCurve{.55f, 1.f, 1.12f}, .ColorCurve = Hold(), .Reveal = DrawOn(.3f),
                  .Keep = RubOut(.62f)};
}
// Card-stock plates of mixed shapes thrown up out of a burst.
FLayer PlateLayer(int32 Count, int32 CountMax, FVector2f Speed, float Cone, FVector2f Scale, FVector2f Life, float Z = 20)
{
    return FLayer{.Meshes = Plates, .Count = Count, .CountMax = CountMax, .Life = Life, .Spin{-560, 560}, .Shape = EShape::Sphere, .Radius = 18,
                  .Offset{0, 0, Z}, .Velocity = EVelocity::Cone, .Speed = Speed, .Cone = Cone, .Drag = .7f, .Gravity{0, 0, -1500},
                  .SizeCurve{1, 1, .15f}, .MeshScale = Scale};
}
// Solid sprite chips (triangle, square, diamond, hexagon) spraying out flat.
FLayer ChipLayer(int32 Count, int32 CountMax, FVector2f Speed, FVector2f Size, FVector2f Life, float Z = 20)
{
    return FLayer{.Texture = TEXT("Chips"), .SubImages = 2, .Sub = ESub::Pick, .Count = Count, .CountMax = CountMax, .Life = Life, .Size = Size,
                  .Spin{-720, 720}, .Shape = EShape::Sphere, .Radius = 6, .Offset{0, 0, Z}, .Velocity = EVelocity::Radial, .Speed = Speed,
                  .Drag = 3.5f, .Gravity{0, 0, -700}, .SizeCurve{1, .9f, .3f}, .ColorCurve = Fade(1, .75f)};
}
// Paper dust: soft marker puffs (fixed colour).
FLayer PuffLayer(int32 Count, FVector2f Size, float Radius, FVector2f Speed, FVector2f Life, FLinearColor Color = PaperDust, float Z = 20)
{
    return FLayer{.Texture = TEXT("Puff"), .Count = Count, .Life = Life, .UserColor = false, .Color = Color, .Size = Size, .Shape = EShape::Sphere,
                  .Radius = Radius, .Offset{0, 0, Z}, .Velocity = EVelocity::Radial, .Speed = Speed, .Drag = 2.5f, .SizeCurve{.5f, 1.2f},
                  .ColorCurve = Fade(.85f)};
}
} // namespace

int32 GWContent::BuildNiagara(const TMap<FName, UMaterialInterface*>& Materials, const TMap<FName, UStaticMesh*>& Meshes)
{
    int32 Built = 0;
    auto Build = [&](const TCHAR* Name, float Bounds, const TArray<FLayer>& Layers)
    { Built += BuildSystem(Name, Layers, Bounds, Materials, Meshes); };

    // A shot landing: a starburst, a random polygon sketched round the hit, chips and pencil sparks.
    {
        FLayer Ring = OutlineLayer(0, 3, {40, 52}, {.24f, .3f}, EFacing::Camera);
        Ring.SizeCurve = {.4f, 1.f, 1.25f};
        Ring.Reveal = DrawOn(.25f);
        Ring.Keep = RubOut(.5f);
        FLayer Sparks{.Texture = TEXT("Stroke"), .Facing = EFacing::Velocity, .Count = 3, .CountMax = 5, .Life{.18f, .28f}, .Stretch{8, 26},
                      .Rotation{0, 0}, .Shape = EShape::Sphere, .Radius = 4, .Velocity = EVelocity::Radial, .Speed{280, 500}, .Drag = 5,
                      .SizeCurve{1, .5f}, .ColorCurve = Fade()};
        Build(TEXT("NS_GW_Impact"), 220,
              {FlashLayer({38, 48}, {.09f, .12f}), Ring, ChipLayer(3, 5, {220, 420}, {10, 15}, {.26f, .4f}, 0), Sparks});
    }
    // The muzzle: a flat starburst along the shot, a tiny hexagon and a cone of chips.
    {
        FLayer Sparks{.Texture = TEXT("Stroke"), .Facing = EFacing::Velocity, .Count = 3, .Life{.1f, .16f}, .Stretch{7, 22}, .Rotation{0, 0},
                      .Shape = EShape::Sphere, .Radius = 2, .Velocity = EVelocity::Cone, .Speed{320, 520}, .Axis{1, 0, 0}, .Cone = 22,
                      .Drag = 6, .ColorCurve = Fade()};
        FLayer Chips = ChipLayer(2, 3, {0, 0}, {8, 11}, {.14f, .2f}, 0);
        Chips.Velocity = EVelocity::Cone;
        Chips.Speed = {260, 460};
        Chips.Axis = {1, 0, 0};
        Chips.Cone = 28;
        FLayer Hexagon = OutlineLayer(3, 3, {26, 30}, {.12f, .14f}, EFacing::Camera);
        Hexagon.Reveal.Reset();
        Hexagon.SizeCurve = {.5f, 1.3f};
        Hexagon.ColorCurve = Fade();
        Build(TEXT("NS_GW_Muzzle"), 160, {FlashLayer({28, 38}, {.06f, .08f}), Hexagon, Chips, Sparks});
    }
    // An explosion: a hexagon shock band and a compass construction swept round on the paper, a flash, flying
    // outlines, plates in the blast's colour and in ink, dust, dashes and a hatched scorch left behind.
    {
        FLayer Shock{.Texture = TEXT("HexRing"), .Facing = EFacing::Ground, .Life{.5f, .55f}, .Size{300, 300}, .Spin{-40, 40}, .Offset{0, 0, 4},
                     .SizeCurve{.2f, 1.f, 1.08f}, .ColorCurve = Fade(1, .85f)};
        FLayer Compass{.Texture = TEXT("Compass"), .Facing = EFacing::Ground, .Life{.75f, .8f}, .Size{390, 390}, .Spin{40, 70},
                       .Offset{0, 0, 6}, .SizeCurve{.7f, 1.f}, .ColorCurve = Hold(.9f), .Reveal = DrawOn(.32f), .Keep = RubOut(.55f)};
        FLayer Outlines = OutlineLayer(0, 3, {40, 64}, {.45f, .6f}, EFacing::Camera, 30);
        Outlines.Count = 4;
        Outlines.CountMax = 6;
        Outlines.Spin = {-300, 300};
        Outlines.Shape = EShape::Sphere;
        Outlines.Radius = 30;
        Outlines.Velocity = EVelocity::Radial;
        Outlines.Speed = {300, 520};
        Outlines.Drag = 3;
        Outlines.SizeCurve = {.6f, 1.f, .5f};
        FLayer Inked = PlateLayer(4, 6, {380, 640}, 70, {.08f, .16f}, {.55f, .8f});
        Inked.UserColor = false;
        Inked.Color = Ink;
        FLayer Dashes{.Texture = TEXT("Dash"), .Facing = EFacing::Velocity, .Count = 6, .CountMax = 8, .Life{.2f, .28f}, .UserColor = false,
                      .Color = Ink, .Stretch{12, 64}, .Rotation{0, 0}, .Shape = EShape::Sphere, .Radius = 30, .Offset{0, 0, 30},
                      .Velocity = EVelocity::Radial, .Speed{620, 920}, .Drag = 5, .ColorCurve = Fade(.85f)};
        FLayer Scorch{.Texture = TEXT("Scorch"), .Facing = EFacing::Ground, .Life{1.6f, 1.8f}, .UserColor = false, .Color = Ink, .Size{230, 260},
                      .Offset{0, 0, 2}, .SizeCurve{.85f, 1.f, 1.f}, .ColorCurve{FLinearColor(1, 1, 1, 0), FLinearColor(1, 1, 1, .75f),
                                                                                 FLinearColor(1, 1, 1, .6f), FLinearColor(1, 1, 1, 0)}};
        Build(TEXT("NS_GW_Blast"), 800,
              {Scorch, Shock, Compass, FlashLayer({160, 190}, {.14f, .18f}, 40), Outlines, PlateLayer(10, 14, {450, 780}, 70, {.12f, .26f}, {.6f, .9f}),
               Inked, PuffLayer(5, {70, 110}, 40, {60, 160}, {.55f, .8f}), Dashes});
    }
    // A defeated enemy comes apart into its plates: the outline of a shape lifting off it, plates in its colour,
    // ink chips, a pop, smoke and a splat of its colour left on the page.
    {
        FLayer Ghost = OutlineLayer(0, 3, {68, 80}, {.36f, .42f}, EFacing::Camera, 25);
        Ghost.Reveal.Reset();
        Ghost.SizeCurve = {.8f, 1.3f, 1.65f};
        Ghost.Keep = RubOut(.35f);
        Ghost.Spin = {-160, 160};
        FLayer Chips = ChipLayer(3, 4, {200, 380}, {10, 14}, {.35f, .5f});
        Chips.UserColor = false;
        Chips.Color = Ink;
        FLayer Splat{.Texture = TEXT("Splat"), .Facing = EFacing::Ground, .Life{1.1f, 1.3f}, .Size{80, 110}, .Offset{0, 0, 2},
                     .SizeCurve{.6f, 1.f, 1.f}, .ColorCurve{FLinearColor(1, 1, 1, .9f), FLinearColor(1, 1, 1, .75f), FLinearColor(1, 1, 1, .6f),
                                                          FLinearColor(1, 1, 1, 0)}};
        FLayer Stars{.Texture = TEXT("Spark"), .Count = 4, .Life{.3f, .4f}, .Size{18, 26}, .Shape = EShape::Sphere, .Radius = 6, .Offset{0, 0, 25},
                     .Velocity = EVelocity::Radial, .Speed{150, 300}, .Drag = 3, .ColorCurve = Fade()};
        Build(TEXT("NS_GW_Death"), 520,
              {Splat, Ghost, PlateLayer(7, 10, {300, 560}, 65, {.1f, .2f}, {.7f, 1.f}), Chips, FlashLayer({70, 90}, {.12f, .14f}, 25),
               PuffLayer(3, {50, 70}, 12, {40, 90}, {.45f, .6f}, Graphite), Stars});
    }
    // Something arriving: a compass circle constructed on the paper while chips fly in and snap together.
    {
        FLayer Compass{.Texture = TEXT("Compass"), .Facing = EFacing::Ground, .Life{.55f, .6f}, .Size{120, 130}, .Spin{90, 140}, .Offset{0, 0, 3},
                       .SizeCurve{.8f, 1.f}, .ColorCurve = Hold(.95f), .Reveal = DrawOn(.45f), .Keep = RubOut(.7f)};
        FLayer Gather = ChipLayer(6, 8, {-190, -150}, {12, 16}, {.36f, .42f}, 30);
        Gather.Shape = EShape::Ring;
        Gather.Radius = 72;
        Gather.Drag = 0;
        Gather.Gravity = FVector3f::ZeroVector;
        Gather.SizeCurve = {.5f, 1.f, .25f};
        Gather.ColorCurve = FadeInOut();
        FLayer Puff = PuffLayer(3, {30, 44}, 30, {40, 80}, {.35f, .45f}, PaperDust, 10);
        Puff.Shape = EShape::Ring;
        Build(TEXT("NS_GW_Spawn"), 240, {Compass, Gather, Puff, FlashLayer({56, 66}, {.1f, .12f}, 30, .33f)});
    }
    // Burning: hand-drawn flames, embers and little triangles rising off the fire.
    {
        FLayer Flames{.Texture = TEXT("Flame"), .Loop = true, .Rate = 16, .Life{.45f, .7f}, .UserColor = false, .Color = Fire,
                      .Size{24, 38}, .Rotation{-12, 12}, .Shape = EShape::Sphere, .Radius = 18, .Offset{0, 0, 14},
                      .Velocity = EVelocity::Linear, .LinearMin{-15, -15, 90}, .LinearMax{15, 15, 150}, .SizeCurve{.6f, 1.f, .3f},
                      .ColorCurve{FLinearColor(1, 1, 1, 0), FLinearColor(1, .9f, .7f, 1), FLinearColor(1, .45f, .35f, .85f),
                                  FLinearColor(.35f, .2f, .2f, 0)}};
        FLayer Embers{.Texture = TEXT("Dot"), .Loop = true, .Rate = 5, .Life{.6f, 1.f}, .UserColor = false, .Color = Marigold,
                      .Size{6, 10}, .Shape = EShape::Sphere, .Radius = 16, .Offset{0, 0, 20}, .Velocity = EVelocity::Linear,
                      .LinearMin{-10, -10, 120}, .LinearMax{10, 10, 220}, .Noise = 40, .ColorCurve = Fade()};
        FLayer Cinders{.Texture = TEXT("Chips"), .SubImages = 2, .Sub = ESub::Pick, .Frames{0, 0}, .Loop = true, .Rate = 4, .Life{.6f, .9f},
                       .UserColor = false, .Color = Fire, .Size{7, 11}, .Spin{-300, 300}, .Shape = EShape::Sphere, .Radius = 18,
                       .Offset{0, 0, 30}, .Velocity = EVelocity::Linear, .LinearMin{-20, -20, 140}, .LinearMax{20, 20, 240}, .Noise = 30,
                       .ColorCurve = Fade()};
        Build(TEXT("NS_GW_Burn"), 260, {Flames, Embers, Cinders});
    }
    // Slowed: snowflakes and diamond crystals rising off a slowly turning hexagon of frost.
    {
        FLayer Flakes{.Texture = TEXT("Snow"), .Loop = true, .Rate = 5, .Life{.9f, 1.3f}, .UserColor = false, .Color = Frost,
                      .Size{18, 26}, .Spin{-90, 90}, .Shape = EShape::Ring, .Radius = 32, .Offset{0, 0, 22},
                      .Velocity = EVelocity::Linear, .LinearMin{0, 0, 20}, .LinearMax{0, 0, 40}, .ColorCurve = FadeInOut()};
        FLayer Ground{.Texture = TEXT("HexRing"), .Facing = EFacing::Ground, .Loop = true, .Rate = 1.2f, .Life{1.f, 1.f},
                      .UserColor = false, .Color = Ice, .Size{92, 100}, .Spin{20, 30}, .Offset{0, 0, 3}, .SizeCurve{.9f, 1.05f},
                      .ColorCurve = FadeInOut(.55f)};
        FLayer Crystals{.Texture = TEXT("Chips"), .SubImages = 2, .Sub = ESub::Pick, .Frames{2, 2}, .Loop = true, .Rate = 3, .Life{.8f, 1.1f},
                        .UserColor = false, .Color = Frost, .Size{9, 13}, .Rotation{-15, 15}, .Shape = EShape::Ring, .Radius = 26,
                        .Offset{0, 0, 10}, .Velocity = EVelocity::Linear, .LinearMin{0, 0, 30}, .LinearMax{0, 0, 60},
                        .ColorCurve = FadeInOut(.9f)};
        Build(TEXT("NS_GW_Frost"), 200, {Flakes, Ground, Crystals});
    }
    // A frost nova: a hexagon band and a compass of frost, crystals standing in a ring, flakes and ice plates.
    {
        FLayer Ring{.Texture = TEXT("HexRing"), .Facing = EFacing::Ground, .Life{.48f, .52f}, .UserColor = false, .Color = Ice,
                    .Size{300, 300}, .Spin{-30, 30}, .Offset{0, 0, 4}, .SizeCurve{.2f, 1.f, 1.05f}, .ColorCurve = Fade(1, .75f)};
        FLayer Compass{.Texture = TEXT("Compass"), .Facing = EFacing::Ground, .Life{.6f, .65f}, .UserColor = false, .Color = Frost,
                       .Size{340, 340}, .Spin{-60, -40}, .Offset{0, 0, 5}, .SizeCurve{.75f, 1.f}, .ColorCurve = Hold(.9f),
                       .Reveal = DrawOn(.3f), .Keep = RubOut(.6f)};
        FLayer Flakes{.Texture = TEXT("Snow"), .Count = 10, .Life{.5f, .7f}, .UserColor = false, .Color = Frost, .Size{26, 36},
                      .Spin{-200, 200}, .Shape = EShape::Sphere, .Radius = 10, .Offset{0, 0, 30}, .Velocity = EVelocity::Radial,
                      .Speed{200, 360}, .Drag = 3, .ColorCurve = Fade()};
        FLayer Crystals{.Mesh = TEXT("SM_GW_Diamond"), .Count = 8, .Life{.55f, .65f}, .UserColor = false, .Color = Frost,
                        .Shape = EShape::Ring, .Radius = 110, .Offset{0, 0, 15}, .SizeCurve{0, 1.15f, 1, 0}, .MeshScale{.14f, .22f}};
        FLayer Shards = PlateLayer(5, 7, {240, 420}, 60, {.08f, .15f}, {.5f, .7f});
        Shards.Meshes = {PlateHex, PlateRhomb};
        Shards.UserColor = false;
        Shards.Color = Ice;
        Shards.Gravity = {0, 0, -900};
        Build(TEXT("NS_GW_FrostNova"), 620, {Ring, Compass, Flakes, Crystals, Shards});
    }
    {
        FLayer Bolt{.Texture = TEXT("Bolt"), .SubImages = 2, .Facing = EFacing::Beam, .Count = 2, .Life{.14f, .2f},
                    .Rotation{0, 0}, .ColorCurve{FLinearColor(1, 1, 1, 1), FLinearColor(1, 1, 1, 1), FLinearColor(1, 1, 1, 0)}};
        Build(TEXT("NS_GW_Arc"), 1200, {Bolt});
    }
    // A pulse of charge: a square swept round on the paper, a hexagon band after it, bolts, chips and a spark.
    {
        FLayer Square = OutlineLayer(1, 1, {380, 420}, {.36f, .4f}, EFacing::Ground, 4);
        Square.Spin = {90, 120};
        Square.SizeCurve = {.3f, 1.f, 1.08f};
        Square.Reveal = DrawOn(.2f);
        Square.Keep = RubOut(.5f);
        FLayer Band{.Texture = TEXT("HexRing"), .Facing = EFacing::Ground, .Delay = .06f, .Life{.3f, .34f}, .Size{300, 300}, .Offset{0, 0, 5},
                    .SizeCurve{.2f, 1.1f}, .ColorCurve = Fade(1, .7f)};
        FLayer Bolts{.Texture = TEXT("Stroke"), .Facing = EFacing::Velocity, .Count = 10, .Life{.18f, .24f}, .Stretch{10, 42},
                     .Rotation{0, 0}, .Shape = EShape::Sphere, .Radius = 20, .Offset{0, 0, 25}, .Velocity = EVelocity::Radial,
                     .Speed{500, 800}, .Drag = 6, .ColorCurve = Fade()};
        FLayer Glint{.Texture = TEXT("Spark"), .Life{.18f, .22f}, .Size{70, 80}, .Offset{0, 0, 30}, .SizeCurve{.5f, 1.2f},
                     .ColorCurve = Fade()};
        Build(TEXT("NS_GW_Pulse"), 620, {Square, Band, Bolts, ChipLayer(5, 7, {300, 520}, {10, 14}, {.3f, .4f}, 25), Glint});
    }
    // A level up: a gold compass constructed on the page, a hexagon band, shapes that rise and gain sides
    // (triangle to hexagon) as they go, confetti plates in three colours and stars.
    {
        FLayer Compass{.Texture = TEXT("Compass"), .Facing = EFacing::Ground, .Life{.9f, .95f}, .UserColor = false, .Color = Marigold,
                       .Size{300, 300}, .Spin{50, 80}, .Offset{0, 0, 4}, .SizeCurve{.8f, 1.f}, .ColorCurve = Hold(), .Reveal = DrawOn(.3f),
                       .Keep = RubOut(.65f)};
        FLayer Band{.Texture = TEXT("HexRing"), .Facing = EFacing::Ground, .Life{.6f, .65f}, .UserColor = false, .Color = Marigold, .Size{260, 260},
                    .Offset{0, 0, 5}, .SizeCurve{.2f, 1.f, 1.1f}, .ColorCurve = Fade(1, .8f)};
        FLayer Grow{.Texture = TEXT("Poly"), .SubImages = 2, .Sub = ESub::Morph, .Count = 3, .Life{1.f, 1.3f}, .UserColor = false, .Color = Marigold,
                    .Size{44, 60}, .Rotation{-10, 10}, .Spin{-40, 40}, .Shape = EShape::Ring, .Radius = 40, .Offset{0, 0, 30},
                    .Velocity = EVelocity::Linear, .LinearMin{-30, -30, 150}, .LinearMax{30, 30, 260}, .SizeCurve{.6f, 1.f, 1.2f},
                    .ColorCurve = FadeInOut()};
        FLayer GrowTeal = Grow;
        GrowTeal.Count = 2;
        GrowTeal.Color = Teal;
        GrowTeal.Radius = 70;
        auto Confetti = [&](FLinearColor Color, int32 Count)
        {
            FLayer L = PlateLayer(Count, Count + 2, {600, 900}, 35, {.08f, .12f}, {1.f, 1.4f}, 30);
            L.UserColor = false;
            L.Color = Color;
            L.Drag = .5f;
            return L;
        };
        FLayer Stars{.Texture = TEXT("Star"), .Count = 8, .Life{1.f, 1.2f}, .UserColor = false, .Color = Marigold, .Size{22, 30},
                     .Spin{-300, 300}, .Shape = EShape::Sphere, .Radius = 20, .Offset{0, 0, 30}, .Velocity = EVelocity::Cone,
                     .Speed{500, 800}, .Cone = 40, .Drag = .5f, .Gravity{0, 0, -1200}, .ColorCurve = Fade(1, .85f)};
        FLayer Flash = FlashLayer({120, 140}, {.16f, .2f}, 40);
        Flash.UserColor = false;
        Flash.Color = Spark;
        Build(TEXT("NS_GW_LevelUp"), 950, {Compass, Band, Grow, GrowTeal, Confetti(Coral, 7), Confetti(Teal, 6), Confetti(Marigold, 6), Stars, Flash});
    }
    // A pickup: a hexagon swept round it while chips fly in to it, then a glint.
    {
        FLayer Hexagon = OutlineLayer(3, 3, {70, 80}, {.34f, .38f}, EFacing::Camera, 30);
        Hexagon.SizeCurve = {.5f, 1.f, 1.3f};
        Hexagon.Reveal = DrawOn(.35f);
        Hexagon.Keep = RubOut(.6f);
        FLayer Gather = ChipLayer(6, 6, {-260, -210}, {10, 13}, {.24f, .28f}, 30);
        Gather.Shape = EShape::Ring;
        Gather.Radius = 64;
        Gather.Drag = 0;
        Gather.Gravity = FVector3f::ZeroVector;
        Gather.SizeCurve = {.6f, 1.f, .3f};
        FLayer Stars{.Texture = TEXT("Spark"), .Count = 6, .Delay = .2f, .Life{.4f, .55f}, .Size{14, 22}, .Shape = EShape::Ring, .Radius = 20,
                     .Offset{0, 0, 30}, .Velocity = EVelocity::Cone, .Speed{200, 360}, .Cone = 50, .Drag = 2, .ColorCurve = Fade()};
        Build(TEXT("NS_GW_Pickup"), 420, {Hexagon, Gather, Stars, FlashLayer({80, 92}, {.12f, .14f}, 30, .2f)});
    }
    {
        FLayer Squares{.Texture = TEXT("Poly"), .SubImages = 2, .Sub = ESub::Pick, .Frames{1, 1}, .Loop = true, .Rate = 4, .Life{.9f, 1.2f},
                       .Size{18, 24}, .Spin{-60, 60}, .Shape = EShape::Ring, .Radius = 46, .Offset{0, 0, 8}, .Velocity = EVelocity::Linear,
                       .LinearMin{0, 0, 50}, .LinearMax{0, 0, 90}, .ColorCurve = FadeInOut()};
        FLayer Chips{.Texture = TEXT("Chips"), .SubImages = 2, .Sub = ESub::Pick, .Loop = true, .Rate = 3, .Life{.7f, 1.f}, .Size{8, 12},
                     .Spin{-200, 200}, .Shape = EShape::Ring, .Radius = 36, .Offset{0, 0, 8}, .Velocity = EVelocity::Linear,
                     .LinearMin{0, 0, 70}, .LinearMax{0, 0, 120}, .ColorCurve = FadeInOut()};
        FLayer Sparks{.Texture = TEXT("Spark"), .Loop = true, .Rate = 6, .Life{.5f, .8f}, .Size{10, 16}, .Shape = EShape::Ring,
                      .Radius = 30, .Offset{0, 0, 10}, .Velocity = EVelocity::Linear, .LinearMin{0, 0, 120},
                      .LinearMax{0, 0, 200}, .ColorCurve = FadeInOut()};
        Build(TEXT("NS_GW_EvolveAura"), 260, {Squares, Chips, Sparks});
    }
    // Healing: plus signs and leaf diamonds rising out of a small hexagon band.
    {
        FLayer Plus{.Texture = TEXT("Plus"), .Count = 5, .Life{.7f, .9f}, .UserColor = false, .Color = Leaf, .Size{18, 26},
                    .Shape = EShape::Sphere, .Radius = 30, .Offset{0, 0, 20}, .Velocity = EVelocity::Linear, .LinearMin{0, 0, 80},
                    .LinearMax{0, 0, 140}, .ColorCurve = FadeInOut()};
        FLayer Band{.Texture = TEXT("HexRing"), .Facing = EFacing::Ground, .Life{.45f, .5f}, .UserColor = false, .Color = Leaf, .Size{110, 120},
                    .Offset{0, 0, 3}, .SizeCurve{.4f, 1.f, 1.1f}, .ColorCurve = Fade(.9f, .8f)};
        FLayer Leaves{.Texture = TEXT("Chips"), .SubImages = 2, .Sub = ESub::Pick, .Frames{2, 2}, .Count = 4, .Life{.6f, .8f}, .UserColor = false,
                      .Color = Hex(0x8FD694), .Size{10, 14}, .Rotation{-20, 20}, .Shape = EShape::Ring, .Radius = 40, .Offset{0, 0, 10},
                      .Velocity = EVelocity::Linear, .LinearMin{0, 0, 60}, .LinearMax{0, 0, 120}, .ColorCurve = FadeInOut()};
        Build(TEXT("NS_GW_Heal"), 220, {Band, Plus, Leaves});
    }
    // A shield: the hexagon flashes up and six small hexagons fly in round it like a honeycomb closing.
    {
        FLayer Hexagon{.Texture = TEXT("Hex"), .Life{.28f, .32f}, .UserColor = false, .Color = Hex(0x17A3BF), .Size{90, 100}, .Offset{0, 0, 25},
                       .SizeCurve{.85f, 1.1f}, .ColorCurve = Fade(.95f)};
        FLayer Comb{.Texture = TEXT("Poly"), .SubImages = 2, .Sub = ESub::Pick, .Frames{3, 3}, .Count = 6, .Life{.32f, .36f}, .UserColor = false,
                    .Color = Hex(0x17A3BF), .Size{24, 28}, .Rotation{0, 0}, .Shape = EShape::Ring, .Radius = 58, .Offset{0, 0, 25},
                    .Velocity = EVelocity::Radial, .Speed{-70, -50}, .SizeCurve{.2f, 1.f, 1.f, .4f}, .ColorCurve = FadeInOut()};
        FLayer Shards{.Texture = TEXT("Shard"), .Count = 4, .Life{.28f, .34f}, .UserColor = false, .Color = Hex(0x17A3BF), .Size{14, 20},
                      .Shape = EShape::Sphere, .Radius = 30, .Offset{0, 0, 25}, .Velocity = EVelocity::Radial,
                      .Speed{120, 200}, .Drag = 2, .ColorCurve = Fade()};
        Build(TEXT("NS_GW_Shield"), 220, {Hexagon, Comb, Shards});
    }
    // A boss's slam warning: the ring is drawn round over the warning's 1.5 s like a clock hand, over a compass
    // and a red fill; triangles stand round it.
    {
        FLayer Outer{.Texture = TEXT("Ring"), .Facing = EFacing::Ground, .Life{1.5f, 1.5f}, .UserColor = false, .Color = Danger,
                     .Size{640, 640}, .Rotation{0, 0}, .Offset{0, 0, 4}, .SizeCurve{1.05f, 1.f, 1.f, .98f},
                     .ColorCurve{FLinearColor(1, 1, 1, .6f), FLinearColor(1, 1, 1, .9f), FLinearColor(1, 1, 1, 1), FLinearColor(1, 1, 1, .2f)},
                     .Reveal{{0, .02f}, {.93f, 1}, {1, 1}}};
        FLayer Fill{.Texture = TEXT("Glow"), .Facing = EFacing::Ground, .Life{1.5f, 1.5f}, .UserColor = false, .Color = Danger,
                    .Size{640, 640}, .Offset{0, 0, 3}, .SizeCurve{0, 1}, .ColorCurve = FadeInOut(.45f)};
        FLayer Compass{.Texture = TEXT("Compass"), .Facing = EFacing::Ground, .Life{1.5f, 1.5f}, .UserColor = false, .Color = Danger,
                       .Size{430, 430}, .Spin{-30, -30}, .Offset{0, 0, 5}, .ColorCurve = FadeInOut(.55f)};
        FLayer Marks{.Texture = TEXT("Poly"), .SubImages = 2, .Sub = ESub::Pick, .Frames{0, 0}, .Count = 6, .Life{1.5f, 1.5f}, .UserColor = false,
                     .Color = Danger, .Size{46, 52}, .Rotation{0, 0}, .Shape = EShape::Ring, .Radius = 270, .Offset{0, 0, 40},
                     .SizeCurve{1, 1.2f, 1, 1.2f, 1}, .ColorCurve = FadeInOut()};
        Build(TEXT("NS_GW_Telegraph"), 820, {Fill, Compass, Outer, Marks});
    }
    // The slam itself: a crack, a hexagon shock band, rocks, square plates kicked up round it, dust and scorch.
    {
        FLayer Crack{.Texture = TEXT("Crack"), .Facing = EFacing::Ground, .Life{1.2f, 1.3f}, .UserColor = false, .Color = Ink,
                     .Size{520, 560}, .Offset{0, 0, 3}, .ColorCurve = Fade(1, .9f)};
        FLayer Ring{.Texture = TEXT("HexRing"), .Facing = EFacing::Ground, .Life{.42f, .48f}, .Size{640, 640}, .Offset{0, 0, 5},
                    .SizeCurve{.3f, 1.05f}, .ColorCurve = Fade()};
        FLayer Rocks{.Mesh = TEXT("SM_GW_Shard"), .Count = 14, .Life{.7f, .9f}, .UserColor = false, .Color = Hex(0x8C6A4A),
                     .Spin{-360, 360}, .Shape = EShape::Sphere, .Radius = 60, .Offset{0, 0, 10}, .Velocity = EVelocity::Cone,
                     .Speed{500, 900}, .Cone = 55, .Drag = .5f, .Gravity{0, 0, -1600}, .SizeCurve{1, 1, .2f},
                     .MeshScale{.15f, .3f}};
        FLayer Kicked = PlateLayer(10, 12, {420, 700}, 25, {.12f, .22f}, {.6f, .8f}, 10);
        Kicked.Meshes = {PlateSquare, PlateRhomb};
        Kicked.UserColor = false;
        Kicked.Color = Kraft;
        Kicked.Shape = EShape::Ring;
        Kicked.Radius = 230;
        Kicked.Gravity = {0, 0, -1600};
        FLayer Dust{.Texture = TEXT("Puff"), .Count = 10, .Life{.7f, .9f}, .UserColor = false, .Color = PaperDust, .Size{120, 180},
                    .Shape = EShape::Ring, .Radius = 200, .Offset{0, 0, 20}, .Velocity = EVelocity::Radial, .Speed{100, 200},
                    .Drag = 1.5f, .SizeCurve{.6f, 1.2f}, .ColorCurve = Fade(.85f)};
        Build(TEXT("NS_GW_Slam"), 1100, {Crack, Ring, Rocks, Kicked, Dust});
    }
    // A throw or dodge leaving the ground: a dust loop, ink speed lines, dashes and triangles trailing behind.
    {
        FLayer Dust{.Texture = TEXT("Loop"), .Facing = EFacing::Ground, .Life{.33f, .37f}, .UserColor = false, .Color = PaperDust,
                    .Size{120, 130}, .Offset{0, 0, 3}, .SizeCurve{.4f, 1.2f}, .ColorCurve = Fade(.9f)};
        FLayer Lines{.Texture = TEXT("Stroke"), .Facing = EFacing::Velocity, .Count = 5, .Life{.22f, .28f}, .UserColor = false,
                     .Color = Ink, .Stretch{8, 40}, .Rotation{0, 0}, .Shape = EShape::Sphere, .Radius = 18, .Offset{0, 0, 20},
                     .Velocity = EVelocity::Cone, .Speed{220, 420}, .Axis{-1, 0, 0}, .Cone = 30, .Drag = 4, .ColorCurve = Fade(.8f)};
        FLayer Dashes{.Texture = TEXT("Dash"), .Facing = EFacing::Velocity, .Count = 3, .Life{.24f, .3f}, .Stretch{10, 48}, .Rotation{0, 0},
                      .Shape = EShape::Sphere, .Radius = 22, .Offset{0, 0, 30}, .Velocity = EVelocity::Cone, .Speed{260, 460}, .Axis{-1, 0, 0},
                      .Cone = 18, .Drag = 4, .ColorCurve = Fade(.9f)};
        FLayer Trail{.Texture = TEXT("Chips"), .SubImages = 2, .Sub = ESub::Pick, .Frames{0, 0}, .Count = 3, .Life{.3f, .4f}, .Size{10, 14},
                     .Spin{-400, 400}, .Shape = EShape::Sphere, .Radius = 16, .Offset{0, 0, 24}, .Velocity = EVelocity::Cone,
                     .Speed{160, 300}, .Axis{-1, 0, 0}, .Cone = 40, .Drag = 3, .SizeCurve{1, .3f}, .ColorCurve = Fade()};
        Build(TEXT("NS_GW_Launch"), 320, {Dust, Lines, Dashes, Trail});
    }
    {
        FLayer Ring{.Texture = TEXT("Loop"), .Facing = EFacing::Ground, .Life{.28f, .32f}, .UserColor = false, .Color = Marigold,
                    .Size{220, 230}, .Offset{0, 0, 4}, .SizeCurve{.3f, 1.1f}, .ColorCurve = Fade()};
        FLayer Glint{.Texture = TEXT("Burst8"), .Life{.11f, .13f}, .UserColor = false, .Color = Marigold, .Size{110, 125},
                     .Offset{0, 0, 30}, .SizeCurve{.6f, 1.15f}, .ColorCurve = Fade()};
        Build(TEXT("NS_GW_SwordFlash"), 300, {Ring, Glint});
    }
    {
        // Faint geometry drifting over the page (outlines, chips, dots); it must never read as a gameplay cue.
        FLayer Bits{.Texture = TEXT("Poly"), .SubImages = 2, .Sub = ESub::Pick, .Loop = true, .Rate = 2.5f, .Life{4.f, 6.f}, .UserColor = false,
                    .Color = Hex(0x9DB5D3), .Size{14, 22}, .Spin{-30, 30}, .Shape = EShape::Box, .Box{3400, 2400, 10}, .Offset{0, 0, 40},
                    .Velocity = EVelocity::Linear, .LinearMin{-25, -25, 12}, .LinearMax{25, 25, 36}, .ColorCurve = FadeInOut(.3f)};
        FLayer Chips = Bits;
        Chips.Texture = TEXT("Chips");
        Chips.Rate = 2;
        Chips.Size = {7, 11};
        Chips.Color = Hex(0xC9BBA0);
        Chips.ColorCurve = FadeInOut(.35f);
        FLayer Dots = Bits;
        Dots.Texture = TEXT("Dot");
        Dots.SubImages = 1;
        Dots.Sub = ESub::Flicker;
        Dots.Rate = 3;
        Dots.ColorCurve = FadeInOut(.4f);
        Dots.Size = {5, 9};
        Dots.Color = Hex(0x7A93B8);
        Build(TEXT("NS_GW_Ambient"), 2500, {Bits, Chips, Dots});
    }

    // Construction (a block drawn, a tower raised, a ladder hung): a square sketched on the page while kraft
    // plates drop in and settle, a puff where they land and a pop.
    {
        FLayer Plan = OutlineLayer(1, 1, {170, 180}, {.5f, .55f}, EFacing::Ground, 3);
        Plan.Spin = {0, 0};
        Plan.Rotation = {0, 0};
        Plan.SizeCurve = {.9f, 1.f, 1.f};
        Plan.Reveal = DrawOn(.35f);
        Plan.Keep = RubOut(.7f);
        FLayer Drop{.Meshes = {PlateSquare, PlateRhomb, PlateTri}, .Count = 5, .CountMax = 6, .Life{.34f, .38f}, .Spin{-360, 360},
                    .Shape = EShape::Ring, .Radius = 70, .Offset{0, 0, 170}, .Velocity = EVelocity::Linear, .LinearMin{0, 0, -420},
                    .LinearMax{0, 0, -380}, .SizeCurve{.4f, 1.f, .7f}, .MeshScale{.14f, .2f}};
        FLayer Settle = PuffLayer(4, {36, 50}, 44, {70, 120}, {.35f, .45f}, PaperDust, 10);
        Settle.Delay = .28f;
        Settle.Shape = EShape::Ring;
        Build(TEXT("NS_GW_Build"), 420, {Plan, Drop, Settle, FlashLayer({70, 80}, {.1f, .12f}, 40, .3f), ChipLayer(4, 5, {160, 300}, {9, 12}, {.3f, .4f}, 30)});
    }
    // Rubbing out: pink eraser crumbs, graphite smudge, a scribble and a couple of dashes.
    {
        FLayer Crumbs = ChipLayer(9, 12, {140, 300}, {8, 13}, {.45f, .6f}, 10);
        Crumbs.UserColor = false;
        Crumbs.Color = Eraser;
        Crumbs.Gravity = {0, 0, -900};
        FLayer Scribble{.Texture = TEXT("Scribble"), .Count = 2, .Life{.4f, .5f}, .UserColor = false, .Color = Ink, .Size{90, 120}, .Offset{0, 0, 30},
                        .SizeCurve{.8f, 1.1f}, .ColorCurve = Fade(.55f)};
        FLayer Dashes{.Texture = TEXT("Dash"), .Facing = EFacing::Velocity, .Count = 4, .Life{.2f, .26f}, .UserColor = false, .Color = Graphite,
                      .Stretch{10, 40}, .Rotation{0, 0}, .Shape = EShape::Sphere, .Radius = 20, .Offset{0, 0, 20}, .Velocity = EVelocity::Radial,
                      .Speed{300, 480}, .Drag = 5, .ColorCurve = Fade(.8f)};
        Build(TEXT("NS_GW_Erase"), 360, {Scribble, PuffLayer(3, {50, 70}, 20, {40, 90}, {.4f, .55f}, Graphite, 15), Crumbs, Dashes});
    }
    // A block broken apart: big square plates and shards of its card, ink chips, dust and the outline lifting off.
    {
        FLayer Slabs = PlateLayer(12, 16, {380, 760}, 75, {.15f, .32f}, {.7f, 1.f}, 40);
        Slabs.Meshes = Blocks;
        FLayer Ghost = OutlineLayer(1, 1, {130, 140}, {.4f, .45f}, EFacing::Camera, 60);
        Ghost.Reveal.Reset();
        Ghost.SizeCurve = {.9f, 1.4f, 1.7f};
        Ghost.Keep = RubOut(.3f);
        FLayer Chips = ChipLayer(6, 8, {260, 480}, {11, 16}, {.4f, .6f}, 40);
        Chips.UserColor = false;
        Chips.Color = Ink;
        Build(TEXT("NS_GW_Shatter"), 760,
              {Ghost, Slabs, Chips, PuffLayer(6, {80, 120}, 50, {80, 180}, {.6f, .85f}, PaperDust, 40), FlashLayer({120, 140}, {.12f, .15f}, 50)});
    }
    // Celebration (a wave cleared, a bullseye, a lesson learned): plates and chips in every marker colour, stars.
    {
        auto Confetti = [&](FLinearColor Color, int32 Count)
        {
            FLayer L = PlateLayer(Count, Count + 3, {700, 1150}, 42, {.07f, .13f}, {1.2f, 1.6f}, 30);
            L.UserColor = false;
            L.Color = Color;
            L.Drag = .9f;
            L.Gravity = {0, 0, -1300};
            return L;
        };
        auto Bits = [&](FLinearColor Color)
        {
            FLayer L = ChipLayer(6, 8, {500, 800}, {10, 14}, {1.f, 1.3f}, 40);
            L.UserColor = false;
            L.Color = Color;
            L.Velocity = EVelocity::Cone;
            L.Cone = 50;
            L.Drag = 1.2f;
            L.Gravity = {0, 0, -1100};
            return L;
        };
        FLayer Stars{.Texture = TEXT("Star"), .Count = 6, .Life{1.f, 1.3f}, .UserColor = false, .Color = Marigold, .Size{22, 30}, .Spin{-300, 300},
                     .Shape = EShape::Sphere, .Radius = 20, .Offset{0, 0, 40}, .Velocity = EVelocity::Cone, .Speed{600, 900}, .Cone = 45, .Drag = .8f,
                     .Gravity{0, 0, -1200}, .ColorCurve = Fade(1, .85f)};
        Build(TEXT("NS_GW_Confetti"), 1600,
              {Confetti(Coral, 8), Confetti(Teal, 8), Confetti(Marigold, 7), Confetti(Cobalt, 6), Bits(Mint), Bits(Violet), Stars, FlashLayer({110, 130}, {.14f, .16f}, 40)});
    }
    // Someone put together again (a respawn, a revive, a new ally): plates fly in from all round and fold into
    // the body, a compass is drawn under it, then a pop and a hexagon band.
    {
        FLayer Fold{.Meshes = Plates, .Count = 10, .CountMax = 12, .Life{.42f, .46f}, .Spin{-480, 480}, .Shape = EShape::Sphere, .Radius = 150,
                    .Offset{0, 0, 90}, .Velocity = EVelocity::Radial, .Speed{-340, -300}, .SizeCurve{.6f, 1.f, .25f}, .MeshScale{.1f, .17f}};
        FLayer Compass{.Texture = TEXT("Compass"), .Facing = EFacing::Ground, .Life{.7f, .75f}, .Size{150, 160}, .Spin{-90, -60}, .Offset{0, 0, 3},
                       .SizeCurve{.8f, 1.f}, .ColorCurve = Hold(.95f), .Reveal = DrawOn(.4f), .Keep = RubOut(.7f)};
        FLayer Band{.Texture = TEXT("HexRing"), .Facing = EFacing::Ground, .Delay = .4f, .Life{.35f, .4f}, .Size{180, 190}, .Offset{0, 0, 5},
                    .SizeCurve{.2f, 1.1f}, .ColorCurve = Fade(1, .7f)};
        Build(TEXT("NS_GW_Assemble"), 420, {Compass, Fold, Band, FlashLayer({90, 100}, {.12f, .14f}, 50, .4f)});
    }
    // Ink: a splat on the page with droplets thrown off and a small hexagon swept round it.
    {
        FLayer Splat{.Texture = TEXT("Splat"), .Facing = EFacing::Ground, .Life{.8f, .9f}, .Size{70, 86}, .Offset{0, 0, 2}, .SizeCurve{.4f, 1.f, 1.f},
                     .ColorCurve = FadeInOut(.85f)};
        FLayer Drops{.Texture = TEXT("Dot"), .Count = 6, .Life{.3f, .42f}, .Size{8, 13}, .Shape = EShape::Sphere, .Radius = 8, .Offset{0, 0, 20},
                     .Velocity = EVelocity::Cone, .Speed{220, 380}, .Cone = 55, .Drag = 1, .Gravity{0, 0, -1300}, .ColorCurve = Fade()};
        FLayer Hexagon = OutlineLayer(3, 3, {56, 60}, {.3f, .34f}, EFacing::Ground, 4);
        Hexagon.SizeCurve = {.5f, 1.f, 1.2f};
        Build(TEXT("NS_GW_InkSplash"), 240, {Splat, Drops, Hexagon});
    }
    // A tower going up (the build ring): its footprint and a compass inked on the page, card plates flying in from
    // all round and stacking down onto the post, then dust stamped out where it lands, a hexagon band, a burst, chips
    // and speed dashes thrown off. Timed with the tower's own jelly pop (GWWorld::JellyAt).
    {
        FLayer Plan = OutlineLayer(1, 1, {230, 240}, {.62f, .66f}, EFacing::Ground, 3);
        Plan.Spin = {0, 0};
        Plan.Rotation = {0, 0};
        Plan.SizeCurve = {.85f, 1.f, 1.f};
        Plan.Reveal = DrawOn(.28f);
        Plan.Keep = RubOut(.72f);
        FLayer Compass{.Texture = TEXT("Compass"), .Facing = EFacing::Ground, .Life{.72f, .76f}, .Size{320, 340}, .Spin{-120, -90}, .Offset{0, 0, 2},
                       .SizeCurve{.7f, 1.f}, .ColorCurve = Hold(.85f), .Reveal = DrawOn(.35f), .Keep = RubOut(.65f)};
        FLayer Gather{.Meshes = Plates, .Count = 12, .CountMax = 14, .Life{.3f, .34f}, .Spin{-540, 540}, .Shape = EShape::Sphere, .Radius = 200,
                      .Offset{0, 0, 170}, .Velocity = EVelocity::Radial, .Speed{-600, -520}, .SizeCurve{.5f, 1.f, .2f}, .MeshScale{.12f, .2f}};
        FLayer Stack{.Meshes = {PlateSquare, PlateHex, PlatePent}, .Count = 4, .Delay = .1f, .Life{.2f, .24f}, .Spin{-200, 200}, .Shape = EShape::Ring,
                     .Radius = 18, .Offset{0, 0, 400}, .Velocity = EVelocity::Linear, .LinearMin{0, 0, -960}, .LinearMax{0, 0, -820},
                     .SizeCurve{.6f, 1.f, .5f}, .MeshScale{.2f, .26f}};
        FLayer Dust = PuffLayer(7, {50, 70}, 64, {170, 250}, {.42f, .56f}, PaperDust, 12);
        Dust.Delay = .2f;
        Dust.Shape = EShape::Ring;
        FLayer Band{.Texture = TEXT("HexRing"), .Facing = EFacing::Ground, .Delay = .2f, .Life{.42f, .46f}, .Size{300, 320}, .Offset{0, 0, 5},
                    .SizeCurve{.25f, 1.15f}, .ColorCurve = Fade(1, .7f)};
        FLayer Dashes{.Texture = TEXT("Dash"), .Facing = EFacing::Velocity, .Count = 8, .Delay = .2f, .Life{.22f, .28f}, .Stretch{16, 64},
                      .Rotation{0, 0}, .Shape = EShape::Ring, .Radius = 80, .Offset{0, 0, 12}, .Velocity = EVelocity::Radial, .Speed{620, 820},
                      .Drag = 4, .ColorCurve = Fade(1, .6f)};
        FLayer Chips = ChipLayer(8, 10, {320, 540}, {10, 15}, {.4f, .55f}, 60);
        Chips.Delay = .2f;
        Build(TEXT("NS_GW_TowerBuild"), 560, {Plan, Compass, Gather, Stack, Dust, Band, Dashes, Chips, FlashLayer({160, 180}, {.12f, .15f}, 90, .2f)});
    }
    // A tower growing a side or a level: hexagon bands climb it from the floor one after another, triangle chevrons
    // rise round it, a polygon outline above it grows its sides, a light column, then a burst, stars and plates
    // thrown off the top, over a compass turning on the floor.
    {
        auto Climb = [](float Delay, float Size)
        {
            return FLayer{.Texture = TEXT("HexRing"), .Facing = EFacing::Ground, .Delay = Delay, .Life{.5f, .5f}, .Size{Size, Size}, .Offset{0, 0, 8},
                          .Velocity = EVelocity::Linear, .LinearMin{0, 0, 470}, .LinearMax{0, 0, 470}, .SizeCurve{.6f, 1.f, .55f},
                          .ColorCurve = FadeInOut()};
        };
        FLayer Floor{.Texture = TEXT("Compass"), .Facing = EFacing::Ground, .Life{.8f, .85f}, .Size{300, 320}, .Spin{90, 120}, .Offset{0, 0, 2},
                     .SizeCurve{.7f, 1.f}, .ColorCurve = Hold(.9f), .Reveal = DrawOn(.3f), .Keep = RubOut(.7f)};
        FLayer Chevrons{.Texture = TEXT("Chips"), .SubImages = 2, .Sub = ESub::Pick, .Frames{0, 0}, .Count = 10, .CountMax = 12, .Life{.6f, .8f},
                        .Size{16, 22}, .Rotation{0, 0}, .Shape = EShape::Ring, .Radius = 80, .Offset{0, 0, 40}, .Velocity = EVelocity::Linear,
                        .LinearMin{-40, -40, 380}, .LinearMax{40, 40, 560}, .Drag = .6f, .SizeCurve{.6f, 1.f, .4f}, .ColorCurve = FadeInOut()};
        FLayer Morph{.Texture = TEXT("Poly"), .SubImages = 2, .Sub = ESub::Morph, .Delay = .12f, .Life{.8f, .8f}, .UserColor = false, .Color = Marigold,
                     .Size{120, 120}, .Rotation{0, 0}, .Spin{-60, -60}, .Offset{0, 0, 330}, .SizeCurve{.4f, 1.f, 1.1f}, .ColorCurve = FadeInOut()};
        FLayer Column{.Texture = TEXT("Glow"), .Facing = EFacing::Velocity, .Delay = .05f, .Life{.36f, .4f}, .Stretch{110, 520}, .Rotation{0, 0},
                      .Offset{0, 0, 250}, .Velocity = EVelocity::Linear, .LinearMin{0, 0, 1}, .LinearMax{0, 0, 1}, .ColorCurve = FadeInOut(.55f)};
        FLayer Stars{.Texture = TEXT("Star"), .Count = 7, .Delay = .3f, .Life{.6f, .8f}, .UserColor = false, .Color = Spark, .Size{20, 28},
                     .Spin{-300, 300}, .Shape = EShape::Sphere, .Radius = 20, .Offset{0, 0, 290}, .Velocity = EVelocity::Radial, .Speed{240, 420},
                     .Drag = 2, .Gravity{0, 0, -600}, .ColorCurve = Fade(1, .8f)};
        FLayer Thrown = PlateLayer(8, 10, {420, 640}, 70, {.1f, .15f}, {.6f, .8f}, 260);
        Thrown.Delay = .3f;
        Build(TEXT("NS_GW_TowerUpgrade"), 760,
              {Floor, Climb(0, 200), Climb(.1f, 180), Climb(.2f, 160), Chevrons, Morph, Column, Stars, Thrown, FlashLayer({170, 190}, {.14f, .18f}, 290, .3f)});
    }
    // A spent shot coming down on the page (a shot that ran out of range): a little paper dust, a few chips of its
    // colour skipping off and a small hexagon scuffed round where it hit.
    {
        FLayer Dust = PuffLayer(3, {18, 26}, 6, {50, 95}, {.3f, .42f}, PaperDust, 5);
        FLayer Skips = ChipLayer(3, 4, {130, 230}, {4, 7}, {.24f, .32f}, 6);
        Skips.Gravity = {0, 0, -900};
        FLayer Scuff{.Texture = TEXT("HexRing"), .Facing = EFacing::Ground, .Life{.22f, .26f}, .Size{44, 50}, .Offset{0, 0, 2}, .SizeCurve{.3f, 1.f},
                     .ColorCurve = Fade(.85f, .6f)};
        Build(TEXT("NS_GW_Thud"), 140, {Dust, Skips, Scuff});
    }
    UE_LOG(LogGWContent, Display, TEXT("NIAGARA_SYSTEMS=%d"), Built);
    return Built;
}
