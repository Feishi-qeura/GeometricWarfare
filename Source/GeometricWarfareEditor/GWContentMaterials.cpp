#include "Engine/Texture2D.h"
#include "GWContentArt.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameterSubUV.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/Package.h"

namespace
{
// All shading is unlit and hand-authored: fixed "paper studio" light, banded tones,
// pencil hatching that boils at six frames a second, and ink at grazing angles.
const TCHAR* ToonCode = TEXT(R"(
float3 n = normalize(N);
float3 L = normalize(float3(-0.48, -0.36, 0.80));
float ndl = dot(n, L);
float lit = smoothstep(0.16, 0.24, ndl);
float mid = smoothstep(-0.34, -0.26, ndl);
float band = 0.62 + 0.17 * mid + 0.21 * lit;
float boil = frac(sin(floor(T * 6.0) * 12.9898) * 43758.5453);
float2 px = Pix + boil * 3.0;
float h1 = abs(frac((px.x + px.y) / 7.0) - 0.5);
float h2 = abs(frac((px.x - px.y) / 9.0) - 0.5);
float hatch = (1.0 - lit) * (1.0 - smoothstep(0.10, 0.22, h1)) * 0.55 + (1.0 - mid) * (1.0 - smoothstep(0.10, 0.22, h2)) * 0.45;
float3 ink = float3(0.030, 0.034, 0.052);
float3 col = float3(CR, CG, CB) * band;
col = lerp(col, ink, hatch * 0.38);
float grain = frac(sin(dot(floor(px / 2.0), float2(12.9898, 78.233))) * 43758.5453);
col *= 0.965 + 0.035 * grain;
float facing = abs(dot(n, normalize(V)));
col = lerp(col, ink, (1.0 - smoothstep(0.10, 0.28, facing)) * 0.7);
return lerp(col, float3(1.0, 0.97, 0.88), saturate(CF));
)");

const TCHAR* PaperCode = TEXT(R"(
struct GWNoise
{
    float hash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
    float noise(float2 p)
    {
        float2 i = floor(p);
        float2 f = frac(p);
        float2 u = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(hash(i), hash(i + float2(1, 0)), u.x), lerp(hash(i + float2(0, 1)), hash(i + float2(1, 1)), u.x), u.y);
    }
};
GWNoise g;
float2 p = W.xy;
float3 paper = float3(0.86, 0.80, 0.66);
paper *= 0.95 + 0.07 * (g.noise(p * 0.0011) * 0.6 + g.noise(p * 0.004) * 0.4);
paper *= 0.985 + 0.03 * g.noise(p * 0.09);
float2 wob = float2(g.noise(p.yx * 0.0021), g.noise(p * 0.0021)) * 7.0;
float2 q = (p + wob) / 200.0;
float2 fw = max(fwidth(q), 1e-5);
float2 f = frac(q);
float2 dl = min(f, 1.0 - f);
float2 lm = 1.0 - smoothstep(fw * 0.55, fw * 1.6, dl);
float2 Q = (p + wob) / max(GridMajor, 100.0);
float2 FW = max(fwidth(Q), 1e-6);
float2 F = frac(Q);
float2 DL = min(F, 1.0 - F);
float2 LM = 1.0 - smoothstep(FW * 0.9, FW * 2.4, DL);
float3 gridInk = float3(0.20, 0.36, 0.60);
paper = lerp(paper, gridInk, max(lm.x, lm.y) * 0.20);
paper = lerp(paper, gridInk, max(LM.x, LM.y) * 0.32);
float2 inside = min(p, float2(MapSize, MapSizeY > 0.0 ? MapSizeY : MapSize) - p);
float edge = min(inside.x, inside.y);
paper *= 0.86 + 0.14 * smoothstep(0.0, 900.0, edge);
float border = 1.0 - smoothstep(5.0, 13.0, abs(edge - 22.0 + g.noise(p * 0.012) * 9.0));
paper = lerp(paper, float3(0.03, 0.035, 0.05), border * 0.85);
float hatch = abs(frac((p.x + p.y) / 70.0) - 0.5);
float3 desk = float3(0.36, 0.25, 0.15) * (0.82 + 0.18 * smoothstep(0.12, 0.2, hatch)) * (0.92 + 0.08 * g.noise(p * 0.01));
return edge < 0.0 ? desk : paper;
)");

const TCHAR* SpriteColorCode = TEXT(R"(
float3 fill = PC * (1.0 + Tex.b * 0.6);
float3 ink = lerp(InkColor, PC * 0.25, 0.18);
return lerp(fill, ink, saturate(Tex.g)) * Bright;
)");

const TCHAR* MeshCode = TEXT(R"(
float3 n = normalize(N);
float ndl = dot(n, normalize(float3(-0.48, -0.36, 0.80)));
float band = ndl > 0.2 ? 1.0 : (ndl > -0.3 ? 0.78 : 0.6);
float facing = abs(dot(n, normalize(V)));
return lerp(PC * band, float3(0.03, 0.035, 0.05), (1.0 - smoothstep(0.1, 0.32, facing)) * 0.85);
)");

const TCHAR* DecalColorCode = TEXT(R"(
float3 fill = float3(CR, CG, CB) * (1.0 + Tex.b * 0.5);
return lerp(fill, float3(0.03, 0.035, 0.05), saturate(Tex.g));
)");

const TCHAR* ShadowCode = TEXT(R"(
float r = length(UV - 0.5) * 2.0;
float disc = 1.0 - smoothstep(0.62, 1.0, r);
float h = abs(frac((Pix.x - Pix.y) / 6.0) - 0.5);
float lines = 1.0 - smoothstep(0.14, 0.26, h);
return disc * (0.35 + 0.65 * lines) * CA;
)");

const TCHAR* StreakCode = TEXT(R"(
float along = saturate(UV.x);
float across = 1.0 - saturate(abs(UV.y - 0.5) * 2.0);
return pow(along, 1.6) * smoothstep(0.0, 0.7, across) * CA;
)");

struct FGraph
{
    UMaterial* Material;
    template <class T> T* Add(int32 X = 0, int32 Y = 0)
    {
        return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(Material, T::StaticClass(), X, Y));
    }
    static int32 OutputIndex(UMaterialExpression* From, const TCHAR* Name)
    {
        const auto& Outputs = From->GetOutputs();
        for (int32 I = 0; I < Outputs.Num(); ++I)
            if (Outputs[I].OutputName.ToString().Equals(Name, ESearchCase::IgnoreCase))
                return I;
        FString Names;
        for (const auto& Output : Outputs)
            Names += TEXT("[") + Output.OutputName.ToString() + TEXT("]");
        if (*Name)
            UE_LOG(LogGWContent, Error, TEXT("%s has no output '%s'. Outputs: %s"), *From->GetClass()->GetName(), Name, *Names);
        return 0;
    }
    struct FIn
    {
        const TCHAR* Name;
        UMaterialExpression* From;
        const TCHAR* Output;
    };
    UMaterialExpressionCustom* Custom(const TCHAR* Code, ECustomMaterialOutputType Type, std::initializer_list<FIn> Inputs)
    {
        auto* Node = Add<UMaterialExpressionCustom>(-300, 0);
        Node->Code = Code;
        Node->OutputType = Type;
        Node->Description = TEXT("GW sketch shading");
        Node->Inputs.Reset();
        for (const FIn& Input : Inputs)
        {
            FCustomInput& Slot = Node->Inputs.AddDefaulted_GetRef();
            Slot.InputName = Input.Name;
            Slot.Input.Connect(OutputIndex(Input.From, Input.Output), Input.From);
        }
        Node->PostEditChange();
        return Node;
    }
    UMaterialExpressionPerInstanceCustomData* Data(int32 Index, float Default)
    {
        auto* Node = Add<UMaterialExpressionPerInstanceCustomData>(-700, Index * 80);
        Node->DataIndex = Index;
        Node->ConstDefaultValue = Default;
        return Node;
    }
};

UMaterial* NewMaterial(const FString& Name, EBlendMode Blend)
{
    UPackage* Package = GWContent::NewAssetPackage(TEXT("/Game/Sketch/Materials/") + Name);
    UMaterial* Material = NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
    Material->MaterialDomain = MD_Surface;
    Material->BlendMode = Blend;
    Material->SetShadingModel(MSM_Unlit);
    Material->TwoSided = false;
    return Material;
}
void Finish(UMaterial* Material, TMap<FName, UMaterialInterface*>& Out)
{
    UMaterialEditingLibrary::RecompileMaterial(Material);
    GWContent::SaveAsset(Material);
    Out.Add(Material->GetFName(), Material);
}
UMaterialInstanceConstant* Instance(const FString& Name, UMaterialInterface* Parent, UTexture* Texture)
{
    UPackage* Package = GWContent::NewAssetPackage(TEXT("/Game/Sketch/Materials/") + Name);
    auto* MIC = NewObject<UMaterialInstanceConstant>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
    UMaterialEditingLibrary::SetMaterialInstanceParent(MIC, Parent);
    UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, TEXT("Sketch"), Texture);
    UMaterialEditingLibrary::UpdateMaterialInstance(MIC);
    MIC->PostEditChange();
    GWContent::SaveAsset(MIC);
    return MIC;
}
} // namespace

void GWContent::BuildMaterials(const TMap<FName, UTexture2D*>& Textures, TMap<FName, UMaterialInterface*>& Out)
{
    {
        UMaterial* M = NewMaterial(TEXT("M_GW_Toon"), BLEND_Opaque);
        M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
        FGraph G{M};
        auto* Normal = G.Add<UMaterialExpressionVertexNormalWS>();
        auto* Camera = G.Add<UMaterialExpressionCameraVectorWS>();
        auto* Screen = G.Add<UMaterialExpressionScreenPosition>();
        auto* Time = G.Add<UMaterialExpressionTime>();
        auto* Shade = G.Custom(ToonCode, CMOT_Float3,
                               {{TEXT("CR"), G.Data(0, 1), TEXT("")},
                                {TEXT("CG"), G.Data(1, 1), TEXT("")},
                                {TEXT("CB"), G.Data(2, 1), TEXT("")},
                                {TEXT("CF"), G.Data(3, 0), TEXT("")},
                                {TEXT("N"), Normal, TEXT("")},
                                {TEXT("V"), Camera, TEXT("")},
                                {TEXT("Pix"), Screen, TEXT("PixelPosition")},
                                {TEXT("T"), Time, TEXT("")}});
        M->GetEditorOnlyData()->EmissiveColor.Connect(0, Shade);
        Finish(M, Out);
    }
    {
        UMaterial* M = NewMaterial(TEXT("M_GW_Ink"), BLEND_Opaque);
        M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
        FGraph G{M};
        auto* Ink = G.Custom(TEXT("return float3(CR, CG, CB);"), CMOT_Float3,
                             {{TEXT("CR"), G.Data(0, .03f), TEXT("")},
                              {TEXT("CG"), G.Data(1, .035f), TEXT("")},
                              {TEXT("CB"), G.Data(2, .05f), TEXT("")}});
        M->GetEditorOnlyData()->EmissiveColor.Connect(0, Ink);
        Finish(M, Out);
    }
    {
        UMaterial* M = NewMaterial(TEXT("M_GW_Paper"), BLEND_Opaque);
        FGraph G{M};
        auto* World = G.Add<UMaterialExpressionWorldPosition>();
        auto* Size = G.Add<UMaterialExpressionScalarParameter>();
        Size->ParameterName = TEXT("MapSize");
        Size->DefaultValue = 12400;
        // A defense page is not square, and its bold grid lines fall on the 400-unit cells.
        auto* SizeY = G.Add<UMaterialExpressionScalarParameter>();
        SizeY->ParameterName = TEXT("MapSizeY");
        SizeY->DefaultValue = 0;
        auto* Major = G.Add<UMaterialExpressionScalarParameter>();
        Major->ParameterName = TEXT("GridMajor");
        Major->DefaultValue = 1000;
        auto* Shade = G.Custom(PaperCode, CMOT_Float3,
                               {{TEXT("W"), World, TEXT("")},
                                {TEXT("MapSize"), Size, TEXT("")},
                                {TEXT("MapSizeY"), SizeY, TEXT("")},
                                {TEXT("GridMajor"), Major, TEXT("")}});
        M->GetEditorOnlyData()->EmissiveColor.Connect(0, Shade);
        Finish(M, Out);
    }
    {
        UMaterial* M = NewMaterial(TEXT("M_GW_FXSprite"), BLEND_Translucent);
        M->SetUsageByFlag(MATUSAGE_NiagaraSprites, true);
        M->SetUsageByFlag(MATUSAGE_NiagaraRibbons, true);
        M->SetUsageByFlag(MATUSAGE_NiagaraMeshParticles, true);
        FGraph G{M};
        auto* Tex = G.Add<UMaterialExpressionTextureSampleParameterSubUV>(-900, 0);
        Tex->ParameterName = TEXT("Sketch");
        Tex->Texture = Textures.FindRef(TEXT("T_GW_Star"));
        Tex->SamplerType = SAMPLERTYPE_LinearColor;
        auto* Particle = G.Add<UMaterialExpressionParticleColor>(-900, 300);
        auto* Ink = G.Add<UMaterialExpressionVectorParameter>(-900, 450);
        Ink->ParameterName = TEXT("InkColor");
        Ink->DefaultValue = FLinearColor(.03f, .035f, .05f);
        auto* Bright = G.Add<UMaterialExpressionScalarParameter>(-900, 600);
        Bright->ParameterName = TEXT("Bright");
        Bright->DefaultValue = 1;
        auto* Color = G.Custom(SpriteColorCode, CMOT_Float3,
                               {{TEXT("Tex"), Tex, TEXT("RGB")},
                                {TEXT("PC"), Particle, TEXT("")},
                                {TEXT("InkColor"), Ink, TEXT("")},
                                {TEXT("Bright"), Bright, TEXT("")}});
        auto* Alpha = G.Custom(TEXT("return saturate(TA * PA);"), CMOT_Float1,
                               {{TEXT("TA"), Tex, TEXT("A")}, {TEXT("PA"), Particle, TEXT("A")}});
        M->GetEditorOnlyData()->EmissiveColor.Connect(0, Color);
        M->GetEditorOnlyData()->Opacity.Connect(0, Alpha);
        Finish(M, Out);
    }
    {
        UMaterial* M = NewMaterial(TEXT("M_GW_FXMesh"), BLEND_Opaque);
        M->SetUsageByFlag(MATUSAGE_NiagaraMeshParticles, true);
        FGraph G{M};
        auto* Particle = G.Add<UMaterialExpressionParticleColor>();
        auto* Normal = G.Add<UMaterialExpressionVertexNormalWS>();
        auto* Camera = G.Add<UMaterialExpressionCameraVectorWS>();
        auto* Shade = G.Custom(MeshCode, CMOT_Float3,
                               {{TEXT("PC"), Particle, TEXT("")}, {TEXT("N"), Normal, TEXT("")}, {TEXT("V"), Camera, TEXT("")}});
        M->GetEditorOnlyData()->EmissiveColor.Connect(0, Shade);
        Finish(M, Out);
    }
    {
        UMaterial* M = NewMaterial(TEXT("M_GW_Decal"), BLEND_Translucent);
        M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
        FGraph G{M};
        auto* Tex = G.Add<UMaterialExpressionTextureSampleParameter2D>(-900, 0);
        Tex->ParameterName = TEXT("Sketch");
        Tex->Texture = Textures.FindRef(TEXT("T_GW_Ring"));
        Tex->SamplerType = SAMPLERTYPE_LinearColor;
        auto* Color = G.Custom(DecalColorCode, CMOT_Float3,
                               {{TEXT("Tex"), Tex, TEXT("RGB")},
                                {TEXT("CR"), G.Data(0, 1), TEXT("")},
                                {TEXT("CG"), G.Data(1, 1), TEXT("")},
                                {TEXT("CB"), G.Data(2, 1), TEXT("")}});
        auto* Alpha = G.Custom(TEXT("return saturate(TA * CA);"), CMOT_Float1,
                               {{TEXT("TA"), Tex, TEXT("A")}, {TEXT("CA"), G.Data(3, 1), TEXT("")}});
        M->GetEditorOnlyData()->EmissiveColor.Connect(0, Color);
        M->GetEditorOnlyData()->Opacity.Connect(0, Alpha);
        Finish(M, Out);
    }
    {
        UMaterial* M = NewMaterial(TEXT("M_GW_Shadow"), BLEND_Translucent);
        M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
        FGraph G{M};
        auto* UV = G.Add<UMaterialExpressionTextureCoordinate>();
        auto* Screen = G.Add<UMaterialExpressionScreenPosition>();
        auto* Ink = G.Add<UMaterialExpressionConstant3Vector>();
        Ink->Constant = FLinearColor(.03f, .035f, .05f);
        auto* Alpha = G.Custom(ShadowCode, CMOT_Float1,
                               {{TEXT("UV"), UV, TEXT("")},
                                {TEXT("Pix"), Screen, TEXT("PixelPosition")},
                                {TEXT("CA"), G.Data(3, 1), TEXT("")}});
        M->GetEditorOnlyData()->EmissiveColor.Connect(0, Ink);
        M->GetEditorOnlyData()->Opacity.Connect(0, Alpha);
        Finish(M, Out);
    }
    {
        UMaterial* M = NewMaterial(TEXT("M_GW_Streak"), BLEND_Translucent);
        M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
        FGraph G{M};
        auto* UV = G.Add<UMaterialExpressionTextureCoordinate>();
        auto* Color = G.Custom(TEXT("return float3(CR, CG, CB);"), CMOT_Float3,
                               {{TEXT("CR"), G.Data(0, 1), TEXT("")},
                                {TEXT("CG"), G.Data(1, 1), TEXT("")},
                                {TEXT("CB"), G.Data(2, 1), TEXT("")}});
        auto* Alpha = G.Custom(StreakCode, CMOT_Float1, {{TEXT("UV"), UV, TEXT("")}, {TEXT("CA"), G.Data(3, 1), TEXT("")}});
        M->GetEditorOnlyData()->EmissiveColor.Connect(0, Color);
        M->GetEditorOnlyData()->Opacity.Connect(0, Alpha);
        Finish(M, Out);
    }
    UMaterialInterface* Sprite = Out.FindRef(TEXT("M_GW_FXSprite"));
    UMaterialInterface* Decal = Out.FindRef(TEXT("M_GW_Decal"));
    for (const auto& Texture : Textures)
    {
        const FString Short = Texture.Key.ToString().RightChop(5); // T_GW_
        if (Short == TEXT("PaperGrain"))
            continue;
        Out.Add(*(TEXT("MI_GW_FX_") + Short), Instance(TEXT("MI_GW_FX_") + Short, Sprite, Texture.Value));
        if (Short == TEXT("Ring") || Short == TEXT("Loop") || Short == TEXT("Crack") || Short == TEXT("Dot") ||
            Short == TEXT("Glow") || Short == TEXT("Hex") || Short == TEXT("Star") || Short == TEXT("Diamond") ||
            Short == TEXT("Square") || Short == TEXT("Hatch") || Short == TEXT("Plus") || Short == TEXT("Triangle"))
            Out.Add(*(TEXT("MI_GW_Decal_") + Short), Instance(TEXT("MI_GW_Decal_") + Short, Decal, Texture.Value));
    }
    UE_LOG(LogGWContent, Display, TEXT("MATERIALS=%d"), Out.Num());
}
