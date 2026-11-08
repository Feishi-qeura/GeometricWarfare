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
#include "Stateless/Modules/NiagaraStatelessModule_GravityForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitialMeshOrientation.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitializeParticle.h"
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

// Lightweight (stateless) Niagara systems for the 3D Evolution arena. Each system is a stack of
// data-only emitter layers; colour, beam size and beam direction are user parameters set per spawn.
namespace
{
const FLinearColor Ink(.03f, .035f, .05f), PaperDust(.52f, .47f, .4f), Gold(.9f, .4f, .035f), Fire(1.f, .2f, .03f),
    Ice(.16f, .62f, .9f), Frost(.62f, .86f, 1.f), Heal(.11f, .6f, .12f), Cyan(.05f, .55f, .62f), Coral(.85f, .12f, .07f),
    Teal(.02f, .45f, .37f), Danger(.86f, .05f, .04f);

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
struct FLayer
{
    const TCHAR* Texture = TEXT("Star");
    const TCHAR* Mesh = nullptr;
    int32 SubImages = 1;
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
void SizeOverLife(FNiagaraDistributionBase& D, const TArray<float>& Keys, int32 Channels)
{
    D.Mode = ENiagaraDistributionMode::UniformCurve;
    D.ChannelCurves.Reset();
    D.ChannelCurves.Add(Curve(Keys));
    D.LookupValueMode = uint8(ENiagaraDistributionLookupValueMode::ParticlesNormalizedAge);
    D.UpdateValuesFromDistribution();
    (void)Channels;
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
TArray<FLinearColor> Fade(float Peak = 1, float Hold = .6f)
{
    return {FLinearColor(1, 1, 1, Peak), FLinearColor(1, 1, 1, Peak * Hold + (1 - Hold) * Peak * .9f), FLinearColor(1, 1, 1, 0)};
}
TArray<FLinearColor> FadeInOut(float Peak = 1)
{
    return {FLinearColor(1, 1, 1, 0), FLinearColor(1, 1, 1, Peak), FLinearColor(1, 1, 1, Peak), FLinearColor(1, 1, 1, 0)};
}

void Configure(UNiagaraStatelessEmitter* E, const FLayer& L, float Bounds, const TMap<FName, UMaterialInterface*>& Materials,
               const TMap<FName, UStaticMesh*>& Meshes)
{
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
    if (L.Mesh)
    {
        auto* Scale = Module<UNiagaraStatelessModule_ScaleMeshSize>(E, Sized);
        if (Sized)
            SizeOverLife(Scale->ScaleDistribution, L.SizeCurve, 3);
        Module<UNiagaraStatelessModule_ScaleSpriteSize>(E, false);
        Module<UNiagaraStatelessModule_InitialMeshOrientation>(E)->MeshOrientationMode = ENSMInitialMeshOrientationMode::Random;
        auto* Spin = Module<UNiagaraStatelessModule_MeshRotationRate>(E, L.Spin.Y != 0);
        Range(Spin->RotationRateDistribution, FVector3f(L.Spin.X), FVector3f(L.Spin.Y));
    }
    else
    {
        auto* Scale = Module<UNiagaraStatelessModule_ScaleSpriteSize>(E, Sized);
        if (Sized)
            SizeOverLife(Scale->ScaleDistribution, L.SizeCurve, 2);
        Module<UNiagaraStatelessModule_SpriteRotationRate>(E, L.Spin.Y != 0)->RotationRateDistribution.InitRange(L.Spin.X, L.Spin.Y);
    }
    auto* Facing = Module<UNiagaraStatelessModule_SpriteFacingAndAlignment>(E, L.Facing == EFacing::Ground || L.Facing == EFacing::Beam);
    Facing->bSpriteFacingEnabled = L.Facing == EFacing::Ground;
    Facing->SpriteFacing.InitConstant(FVector3f(0, 0, 1));
    Facing->bSpriteAlignmentEnabled = L.Facing == EFacing::Beam || L.Facing == EFacing::Ground;
    if (L.Facing == EFacing::Beam)
        Bind(Facing->SpriteAlignment, FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.BeamDir"));
    else
        Facing->SpriteAlignment.InitConstant(FVector3f(1, 0, 0));
    auto* SubUV = Module<UNiagaraStatelessModule_SubUVAnimation>(E, L.SubImages > 1);
    SubUV->AnimationMode = ENSMSubUVAnimation_Mode::Random;
    SubUV->RandomChangeInterval = .045f;

    UNiagaraRendererProperties* Existing = E->GetRenderers().IsEmpty() ? nullptr : E->GetRenderers()[0];
    if (L.Mesh)
    {
        auto* Renderer = NewObject<UNiagaraMeshRendererProperties>(E, NAME_None, RF_Transactional);
        if (Renderer->Meshes.IsEmpty())
            Renderer->Meshes.AddDefaulted();
        Renderer->Meshes[0].Mesh = Meshes.FindRef(L.Mesh);
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
} // namespace

int32 GWContent::BuildNiagara(const TMap<FName, UMaterialInterface*>& Materials, const TMap<FName, UStaticMesh*>& Meshes)
{
    int32 Built = 0;
    auto Build = [&](const TCHAR* Name, float Bounds, const TArray<FLayer>& Layers)
    { Built += BuildSystem(Name, Layers, Bounds, Materials, Meshes); };

    {
        FLayer Pop{.Texture = TEXT("Star"), .Life{.16f, .2f}, .Size{46, 58}, .SizeCurve{.45f, 1.15f, 1.f}, .ColorCurve = Fade(1, .8f)};
        FLayer Sparks{.Texture = TEXT("Stroke"), .Facing = EFacing::Velocity, .Count = 5, .CountMax = 7, .Life{.2f, .32f},
                      .Stretch{9, 28}, .Rotation{0, 0}, .Shape = EShape::Sphere, .Radius = 4, .Velocity = EVelocity::Radial,
                      .Speed{260, 480}, .Drag = 5, .SizeCurve{1, .5f}, .ColorCurve = Fade()};
        FLayer Dust{.Texture = TEXT("Puff"), .Count = 2, .Life{.35f, .5f}, .UserColor = false, .Color = PaperDust, .Size{26, 38},
                    .Shape = EShape::Sphere, .Radius = 8, .Velocity = EVelocity::Radial, .Speed{30, 70}, .SizeCurve{.6f, 1.4f},
                    .ColorCurve = Fade(.85f)};
        Build(TEXT("NS_GW_Impact"), 200, {Pop, Sparks, Dust});
    }
    {
        FLayer Flash{.Texture = TEXT("Star"), .Life{.07f, .09f}, .Size{30, 40}, .SizeCurve{.7f, 1.1f}, .ColorCurve = Fade()};
        FLayer Sparks{.Texture = TEXT("Stroke"), .Facing = EFacing::Velocity, .Count = 3, .Life{.1f, .16f}, .Stretch{7, 22},
                      .Rotation{0, 0}, .Shape = EShape::Sphere, .Radius = 2, .Velocity = EVelocity::Cone, .Speed{320, 520},
                      .Axis{1, 0, 0}, .Cone = 22, .Drag = 6, .ColorCurve = Fade()};
        Build(TEXT("NS_GW_Muzzle"), 150, {Flash, Sparks});
    }
    {
        FLayer Ring{.Texture = TEXT("Ring"), .Facing = EFacing::Ground, .Life{.5f, .58f}, .Size{300, 300}, .Rotation{0, 360},
                    .Offset{0, 0, 4}, .SizeCurve{.22f, 1.f, 1.08f}, .ColorCurve = Fade(1, .85f)};
        FLayer Flash{.Texture = TEXT("Burst"), .Life{.16f, .2f}, .Size{150, 185}, .Offset{0, 0, 40}, .SizeCurve{.6f, 1.2f},
                     .ColorCurve = Fade()};
        FLayer Shards{.Mesh = TEXT("SM_GW_Shard"), .Count = 9, .CountMax = 12, .Life{.55f, .85f}, .Spin{-420, 420},
                      .Shape = EShape::Sphere, .Radius = 20, .Offset{0, 0, 20}, .Velocity = EVelocity::Cone, .Speed{420, 720},
                      .Cone = 70, .Drag = .8f, .Gravity{0, 0, -1400}, .SizeCurve{1, 1, .2f}, .MeshScale{.1f, .22f}};
        FLayer Puffs{.Texture = TEXT("Puff"), .Count = 6, .Life{.55f, .8f}, .UserColor = false, .Color = PaperDust, .Size{70, 110},
                     .Shape = EShape::Sphere, .Radius = 40, .Offset{0, 0, 20}, .Velocity = EVelocity::Radial, .Speed{60, 160},
                     .Drag = 2.5f, .SizeCurve{.5f, 1.25f}, .ColorCurve = Fade(.9f)};
        FLayer Scribble{.Texture = TEXT("Scribble"), .Count = 2, .Life{.45f, .55f}, .UserColor = false, .Color = Ink,
                        .Size{110, 150}, .Offset{0, 0, 30}, .SizeCurve{.7f, 1.1f}, .ColorCurve = Fade(.65f)};
        Build(TEXT("NS_GW_Blast"), 700, {Ring, Flash, Shards, Puffs, Scribble});
    }
    {
        FLayer Shards{.Mesh = TEXT("SM_GW_Shard"), .Count = 8, .Life{.7f, 1.f}, .Spin{-480, 480}, .Shape = EShape::Sphere,
                      .Radius = 15, .Offset{0, 0, 20}, .Velocity = EVelocity::Cone, .Speed{300, 560}, .Cone = 65,
                      .Drag = .6f, .Gravity{0, 0, -1500}, .SizeCurve{1, 1, .15f}, .MeshScale{.12f, .2f}};
        FLayer Pop{.Texture = TEXT("Loop"), .Life{.28f, .32f}, .Size{70, 90}, .Offset{0, 0, 25}, .SizeCurve{.6f, 1.4f},
                   .ColorCurve = Fade()};
        FLayer Puff{.Texture = TEXT("Puff"), .Count = 3, .Life{.45f, .6f}, .UserColor = false, .Color = FLinearColor(.25f, .24f, .26f),
                    .Size{50, 70}, .Shape = EShape::Sphere, .Radius = 12, .Offset{0, 0, 20}, .Velocity = EVelocity::Radial,
                    .Speed{40, 90}, .SizeCurve{.6f, 1.3f}, .ColorCurve = Fade(.7f)};
        FLayer Stars{.Texture = TEXT("Spark"), .Count = 4, .Life{.3f, .4f}, .Size{18, 26}, .Shape = EShape::Sphere, .Radius = 6,
                     .Offset{0, 0, 25}, .Velocity = EVelocity::Radial, .Speed{150, 300}, .Drag = 3, .ColorCurve = Fade()};
        Build(TEXT("NS_GW_Death"), 500, {Shards, Pop, Puff, Stars});
    }
    {
        FLayer Spiral{.Texture = TEXT("Spiral"), .Facing = EFacing::Ground, .Life{.45f, .5f}, .Size{90, 100}, .Spin{360, 480},
                      .Offset{0, 0, 3}, .SizeCurve{.3f, 1.f, .8f}, .ColorCurve = FadeInOut(.9f)};
        FLayer Puff{.Texture = TEXT("Puff"), .Count = 3, .Life{.35f, .45f}, .UserColor = false, .Color = PaperDust, .Size{30, 44},
                    .Shape = EShape::Ring, .Radius = 30, .Offset{0, 0, 10}, .Velocity = EVelocity::Radial, .Speed{40, 80},
                    .SizeCurve{.5f, 1.2f}, .ColorCurve = Fade(.8f)};
        Build(TEXT("NS_GW_Spawn"), 200, {Spiral, Puff});
    }
    {
        FLayer Flames{.Texture = TEXT("Flame"), .Loop = true, .Rate = 16, .Life{.45f, .7f}, .UserColor = false, .Color = Fire,
                      .Size{24, 38}, .Rotation{-12, 12}, .Shape = EShape::Sphere, .Radius = 18, .Offset{0, 0, 14},
                      .Velocity = EVelocity::Linear, .LinearMin{-15, -15, 90}, .LinearMax{15, 15, 150}, .SizeCurve{.6f, 1.f, .3f},
                      .ColorCurve{FLinearColor(1, 1, 1, 0), FLinearColor(1, .9f, .7f, 1), FLinearColor(1, .45f, .35f, .85f),
                                  FLinearColor(.35f, .2f, .2f, 0)}};
        FLayer Embers{.Texture = TEXT("Dot"), .Loop = true, .Rate = 6, .Life{.6f, 1.f}, .UserColor = false, .Color = Gold,
                      .Size{6, 10}, .Shape = EShape::Sphere, .Radius = 16, .Offset{0, 0, 20}, .Velocity = EVelocity::Linear,
                      .LinearMin{-10, -10, 120}, .LinearMax{10, 10, 220}, .Noise = 40, .ColorCurve = Fade()};
        Build(TEXT("NS_GW_Burn"), 250, {Flames, Embers});
    }
    {
        FLayer Flakes{.Texture = TEXT("Snow"), .Loop = true, .Rate = 5, .Life{.9f, 1.3f}, .UserColor = false, .Color = Frost,
                      .Size{18, 26}, .Spin{-90, 90}, .Shape = EShape::Ring, .Radius = 32, .Offset{0, 0, 22},
                      .Velocity = EVelocity::Linear, .LinearMin{0, 0, 20}, .LinearMax{0, 0, 40}, .ColorCurve = FadeInOut()};
        FLayer Ground{.Texture = TEXT("Loop"), .Facing = EFacing::Ground, .Loop = true, .Rate = 1.2f, .Life{1.f, 1.f},
                      .UserColor = false, .Color = Ice, .Size{86, 96}, .Offset{0, 0, 3}, .SizeCurve{.9f, 1.05f},
                      .ColorCurve = FadeInOut(.55f)};
        Build(TEXT("NS_GW_Frost"), 200, {Flakes, Ground});
    }
    {
        FLayer Ring{.Texture = TEXT("Ring"), .Facing = EFacing::Ground, .Life{.48f, .52f}, .UserColor = false, .Color = Ice,
                    .Size{300, 300}, .Offset{0, 0, 4}, .SizeCurve{.2f, 1.f, 1.05f}, .ColorCurve = Fade(1, .75f)};
        FLayer Flakes{.Texture = TEXT("Snow"), .Count = 10, .Life{.5f, .7f}, .UserColor = false, .Color = Frost, .Size{26, 36},
                      .Spin{-200, 200}, .Shape = EShape::Sphere, .Radius = 10, .Offset{0, 0, 30}, .Velocity = EVelocity::Radial,
                      .Speed{200, 360}, .Drag = 3, .ColorCurve = Fade()};
        FLayer Crystals{.Mesh = TEXT("SM_GW_Diamond"), .Count = 6, .Life{.55f, .65f}, .UserColor = false, .Color = Frost,
                        .Shape = EShape::Ring, .Radius = 110, .Offset{0, 0, 15}, .SizeCurve{0, 1, 1, 0}, .MeshScale{.14f, .22f}};
        Build(TEXT("NS_GW_FrostNova"), 600, {Ring, Flakes, Crystals});
    }
    {
        FLayer Bolt{.Texture = TEXT("Bolt"), .SubImages = 2, .Facing = EFacing::Beam, .Count = 2, .Life{.14f, .2f},
                    .Rotation{0, 0}, .ColorCurve{FLinearColor(1, 1, 1, 1), FLinearColor(1, 1, 1, 1), FLinearColor(1, 1, 1, 0)}};
        Build(TEXT("NS_GW_Arc"), 1200, {Bolt});
    }
    {
        FLayer Ring{.Texture = TEXT("Loop"), .Facing = EFacing::Ground, .Life{.32f, .38f}, .Size{420, 420}, .Offset{0, 0, 4},
                    .SizeCurve{.3f, 1.05f}, .ColorCurve = Fade(1, .7f)};
        FLayer Bolts{.Texture = TEXT("Stroke"), .Facing = EFacing::Velocity, .Count = 10, .Life{.18f, .24f}, .Stretch{10, 42},
                     .Rotation{0, 0}, .Shape = EShape::Sphere, .Radius = 20, .Offset{0, 0, 25}, .Velocity = EVelocity::Radial,
                     .Speed{500, 800}, .Drag = 6, .ColorCurve = Fade()};
        FLayer Spark{.Texture = TEXT("Spark"), .Life{.18f, .22f}, .Size{70, 80}, .Offset{0, 0, 30}, .SizeCurve{.5f, 1.2f},
                     .ColorCurve = Fade()};
        Build(TEXT("NS_GW_Pulse"), 600, {Ring, Bolts, Spark});
    }
    {
        FLayer Ring{.Texture = TEXT("Ring"), .Facing = EFacing::Ground, .Life{.65f, .7f}, .UserColor = false, .Color = Gold,
                    .Size{260, 260}, .Offset{0, 0, 4}, .SizeCurve{.2f, 1.f, 1.1f}, .ColorCurve = Fade(1, .8f)};
        FLayer Tris{.Mesh = TEXT("SM_GW_Tri"), .Count = 10, .Life{1.f, 1.4f}, .UserColor = false, .Color = Coral, .Spin{-520, 520},
                    .Shape = EShape::Sphere, .Radius = 20, .Offset{0, 0, 30}, .Velocity = EVelocity::Cone, .Speed{600, 900},
                    .Cone = 35, .Drag = .5f, .Gravity{0, 0, -1500}, .SizeCurve{1, 1, .3f}, .MeshScale{.08f, .12f}};
        FLayer Blocks = Tris;
        Blocks.Mesh = TEXT("SM_GW_Block");
        Blocks.Color = Teal;
        FLayer Stars{.Texture = TEXT("Star"), .Count = 8, .Life{1.f, 1.2f}, .UserColor = false, .Color = Gold, .Size{22, 30},
                     .Spin{-300, 300}, .Shape = EShape::Sphere, .Radius = 20, .Offset{0, 0, 30}, .Velocity = EVelocity::Cone,
                     .Speed{500, 800}, .Cone = 40, .Drag = .5f, .Gravity{0, 0, -1200}, .ColorCurve = Fade(1, .85f)};
        FLayer Rise{.Texture = TEXT("Diamond"), .Count = 5, .Life{1.1f, 1.3f}, .UserColor = false, .Color = Gold, .Size{24, 30},
                    .Shape = EShape::Ring, .Radius = 40, .Offset{0, 0, 20}, .Velocity = EVelocity::Linear, .LinearMin{0, 0, 120},
                    .LinearMax{0, 0, 200}, .ColorCurve = FadeInOut()};
        Build(TEXT("NS_GW_LevelUp"), 900, {Ring, Tris, Blocks, Stars, Rise});
    }
    {
        FLayer Ring{.Texture = TEXT("Loop"), .Facing = EFacing::Ground, .Life{.38f, .42f}, .Size{140, 150}, .Offset{0, 0, 3},
                    .SizeCurve{.3f, 1.1f}, .ColorCurve = Fade()};
        FLayer Stars{.Texture = TEXT("Spark"), .Count = 8, .Life{.5f, .7f}, .Size{16, 24}, .Shape = EShape::Ring, .Radius = 30,
                     .Offset{0, 0, 15}, .Velocity = EVelocity::Cone, .Speed{200, 380}, .Cone = 50, .Drag = 2,
                     .ColorCurve = Fade()};
        FLayer Glint{.Texture = TEXT("Burst"), .Life{.14f, .16f}, .Size{90, 100}, .Offset{0, 0, 30}, .SizeCurve{.6f, 1.15f},
                     .ColorCurve = Fade()};
        Build(TEXT("NS_GW_Pickup"), 400, {Ring, Stars, Glint});
    }
    {
        FLayer Squares{.Texture = TEXT("Square"), .Loop = true, .Rate = 5, .Life{.9f, 1.2f}, .Size{18, 24}, .Spin{-60, 60},
                       .Shape = EShape::Ring, .Radius = 46, .Offset{0, 0, 8}, .Velocity = EVelocity::Linear,
                       .LinearMin{0, 0, 50}, .LinearMax{0, 0, 90}, .ColorCurve = FadeInOut()};
        FLayer Sparks{.Texture = TEXT("Spark"), .Loop = true, .Rate = 6, .Life{.5f, .8f}, .Size{10, 16}, .Shape = EShape::Ring,
                      .Radius = 30, .Offset{0, 0, 10}, .Velocity = EVelocity::Linear, .LinearMin{0, 0, 120},
                      .LinearMax{0, 0, 200}, .ColorCurve = FadeInOut()};
        Build(TEXT("NS_GW_EvolveAura"), 250, {Squares, Sparks});
    }
    {
        FLayer Plus{.Texture = TEXT("Plus"), .Count = 5, .Life{.7f, .9f}, .UserColor = false, .Color = Heal, .Size{18, 26},
                    .Shape = EShape::Sphere, .Radius = 30, .Offset{0, 0, 20}, .Velocity = EVelocity::Linear, .LinearMin{0, 0, 80},
                    .LinearMax{0, 0, 140}, .ColorCurve = FadeInOut()};
        Build(TEXT("NS_GW_Heal"), 200, {Plus});
    }
    {
        FLayer Hex{.Texture = TEXT("Hex"), .Life{.28f, .32f}, .UserColor = false, .Color = Cyan, .Size{90, 100}, .Offset{0, 0, 25},
                   .SizeCurve{.85f, 1.1f}, .ColorCurve = Fade(.95f)};
        FLayer Shards{.Texture = TEXT("Shard"), .Count = 4, .Life{.28f, .34f}, .UserColor = false, .Color = Cyan, .Size{14, 20},
                      .Shape = EShape::Sphere, .Radius = 30, .Offset{0, 0, 25}, .Velocity = EVelocity::Radial,
                      .Speed{120, 200}, .Drag = 2, .ColorCurve = Fade()};
        Build(TEXT("NS_GW_Shield"), 200, {Hex, Shards});
    }
    {
        FLayer Outer{.Texture = TEXT("Ring"), .Facing = EFacing::Ground, .Life{1.5f, 1.5f}, .UserColor = false, .Color = Danger,
                     .Size{640, 640}, .Offset{0, 0, 4}, .SizeCurve{1.05f, 1.f, 1.f, .98f},
                     .ColorCurve{FLinearColor(1, 1, 1, 0), FLinearColor(1, 1, 1, .9f), FLinearColor(1, 1, 1, 1), FLinearColor(1, 1, 1, .2f)}};
        FLayer Fill{.Texture = TEXT("Glow"), .Facing = EFacing::Ground, .Life{1.5f, 1.5f}, .UserColor = false, .Color = Danger,
                    .Size{640, 640}, .Offset{0, 0, 3}, .SizeCurve{0, 1}, .ColorCurve = FadeInOut(.45f)};
        FLayer Marks{.Texture = TEXT("Triangle"), .Count = 3, .Life{1.5f, 1.5f}, .UserColor = false, .Color = Danger,
                     .Size{46, 52}, .Shape = EShape::Ring, .Radius = 250, .Offset{0, 0, 40}, .ColorCurve = FadeInOut()};
        Build(TEXT("NS_GW_Telegraph"), 800, {Outer, Fill, Marks});
    }
    {
        FLayer Crack{.Texture = TEXT("Crack"), .Facing = EFacing::Ground, .Life{1.2f, 1.3f}, .UserColor = false, .Color = Ink,
                     .Size{520, 560}, .Offset{0, 0, 3}, .ColorCurve = Fade(1, .9f)};
        FLayer Ring{.Texture = TEXT("Ring"), .Facing = EFacing::Ground, .Life{.42f, .48f}, .Size{640, 640}, .Offset{0, 0, 5},
                    .SizeCurve{.3f, 1.05f}, .ColorCurve = Fade()};
        FLayer Rocks{.Mesh = TEXT("SM_GW_Shard"), .Count = 14, .Life{.7f, .9f}, .UserColor = false, .Color = FLinearColor(.5f, .38f, .26f),
                     .Spin{-360, 360}, .Shape = EShape::Sphere, .Radius = 60, .Offset{0, 0, 10}, .Velocity = EVelocity::Cone,
                     .Speed{500, 900}, .Cone = 55, .Drag = .5f, .Gravity{0, 0, -1600}, .SizeCurve{1, 1, .2f},
                     .MeshScale{.15f, .3f}};
        FLayer Dust{.Texture = TEXT("Puff"), .Count = 10, .Life{.7f, .9f}, .UserColor = false, .Color = PaperDust, .Size{120, 180},
                    .Shape = EShape::Ring, .Radius = 200, .Offset{0, 0, 20}, .Velocity = EVelocity::Radial, .Speed{100, 200},
                    .Drag = 1.5f, .SizeCurve{.6f, 1.2f}, .ColorCurve = Fade(.85f)};
        Build(TEXT("NS_GW_Slam"), 1100, {Crack, Ring, Rocks, Dust});
    }
    {
        FLayer Dust{.Texture = TEXT("Loop"), .Facing = EFacing::Ground, .Life{.33f, .37f}, .UserColor = false, .Color = PaperDust,
                    .Size{120, 130}, .Offset{0, 0, 3}, .SizeCurve{.4f, 1.2f}, .ColorCurve = Fade(.9f)};
        FLayer Lines{.Texture = TEXT("Stroke"), .Facing = EFacing::Velocity, .Count = 6, .Life{.22f, .28f}, .UserColor = false,
                     .Color = Ink, .Stretch{8, 40}, .Rotation{0, 0}, .Shape = EShape::Sphere, .Radius = 18, .Offset{0, 0, 20},
                     .Velocity = EVelocity::Cone, .Speed{220, 420}, .Axis{-1, 0, 0}, .Cone = 30, .Drag = 4, .ColorCurve = Fade(.8f)};
        Build(TEXT("NS_GW_Launch"), 300, {Dust, Lines});
    }
    {
        FLayer Ring{.Texture = TEXT("Loop"), .Facing = EFacing::Ground, .Life{.28f, .32f}, .UserColor = false, .Color = Gold,
                    .Size{220, 230}, .Offset{0, 0, 4}, .SizeCurve{.3f, 1.1f}, .ColorCurve = Fade()};
        FLayer Glint{.Texture = TEXT("Burst"), .Life{.11f, .13f}, .UserColor = false, .Color = Gold, .Size{110, 125},
                     .Offset{0, 0, 30}, .SizeCurve{.6f, 1.15f}, .ColorCurve = Fade()};
        Build(TEXT("NS_GW_SwordFlash"), 300, {Ring, Glint});
    }
    {
        // Faint paper confetti drifting over the page; it must never read as dirt or as a gameplay cue.
        FLayer Bits{.Texture = TEXT("Triangle"), .Loop = true, .Rate = 3, .Life{4.f, 6.f}, .UserColor = false,
                    .Color = FLinearColor(.55f, .6f, .72f), .Size{10, 16}, .Spin{-40, 40}, .Shape = EShape::Box,
                    .Box{3400, 2400, 10}, .Offset{0, 0, 40}, .Velocity = EVelocity::Linear, .LinearMin{-25, -25, 12},
                    .LinearMax{25, 25, 36}, .ColorCurve = FadeInOut(.3f)};
        FLayer Dots = Bits;
        Dots.Texture = TEXT("Dot");
        Dots.Rate = 3;
        Dots.ColorCurve = FadeInOut(.4f);
        Dots.Size = {5, 9};
        Dots.Color = FLinearColor(.3f, .42f, .6f);
        Build(TEXT("NS_GW_Ambient"), 2500, {Bits, Dots});
    }
    UE_LOG(LogGWContent, Display, TEXT("NIAGARA_SYSTEMS=%d"), Built);
    return Built;
}
