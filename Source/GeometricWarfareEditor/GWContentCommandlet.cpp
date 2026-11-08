#include "GWContentCommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GWContentArt.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/NiagaraStatelessModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY(LogGWContent);

UGWContentCommandlet::UGWContentCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

UPackage* GWContent::NewAssetPackage(const FString& LongPackageName)
{
    UPackage* Package = CreatePackage(*LongPackageName);
    // A package created over an existing .uasset counts as "partially loaded" and refuses to save;
    // every asset here is rebuilt from scratch, so the new in-memory package is the whole package.
    Package->MarkAsFullyLoaded();
    return Package;
}

bool GWContent::SaveAsset(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    Package->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(Asset);
    const FString File =
        FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    const bool Saved = UPackage::SavePackage(Package, Asset, *File, Args);
    UE_LOG(LogGWContent, Display, TEXT("%s %s"), Saved ? TEXT("SAVED") : TEXT("SAVE_FAILED"), *Package->GetName());
    return Saved;
}

namespace
{
void DumpSystem(const FString& Path, bool Properties)
{
    auto* System = LoadObject<UNiagaraSystem>(nullptr, *Path);
    if (!System)
    {
        UE_LOG(LogGWContent, Error, TEXT("Missing %s"), *Path);
        return;
    }
    UE_LOG(LogGWContent, Display, TEXT("SYSTEM %s handles=%d user=%d"), *Path, System->GetEmitterHandles().Num(),
           System->GetExposedParameters().Num());
    for (const auto& Handle : System->GetEmitterHandles())
    {
        const UNiagaraStatelessEmitter* Emitter = Handle.GetStatelessEmitter();
        UE_LOG(LogGWContent, Display, TEXT("  HANDLE %s mode=%d enabled=%d stateless=%s"), *Handle.GetName().ToString(),
               static_cast<int32>(Handle.GetEmitterMode()), Handle.GetIsEnabled() ? 1 : 0,
               Emitter ? *Emitter->GetUniqueEmitterName() : TEXT("none"));
        if (!Emitter)
            continue;
        for (const auto& Module : Emitter->GetModules())
            if (Module && Module->IsModuleEnabled())
            {
                FString Line = Module->GetClass()->GetName();
                if (Properties)
                    for (TFieldIterator<FProperty> It(Module->GetClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
                    {
                        FString Value;
                        It->ExportTextItem_Direct(Value, It->ContainerPtrToValuePtr<void>(Module), nullptr, nullptr, PPF_None);
                        Line += FString::Printf(TEXT(" | %s=%s"), *It->GetName(), *Value.Left(260));
                    }
                UE_LOG(LogGWContent, Display, TEXT("    MODULE %s"), *Line);
            }
        for (const auto* Renderer : Emitter->GetRenderers())
            UE_LOG(LogGWContent, Display, TEXT("    RENDERER %s"), Renderer ? *Renderer->GetClass()->GetName() : TEXT("null"));
    }
}
} // namespace

int32 UGWContentCommandlet::Main(const FString& Params)
{
    if (Params.Contains(TEXT("-dump")))
    {
        DumpSystem(TEXT("/Niagara/DefaultAssets/Templates/Systems/MinimalLightweight.MinimalLightweight"), true);
        return 0;
    }
    if (Params.Contains(TEXT("-verify")))
    {
        for (const TCHAR* Name : {TEXT("NS_GW_Impact"), TEXT("NS_GW_Blast"), TEXT("NS_GW_Arc"), TEXT("NS_GW_Burn")})
            DumpSystem(FString::Printf(TEXT("/Game/Sketch/VFX/%s.%s"), Name, Name), true);
        return 0;
    }
    TMap<FName, UTexture2D*> Textures;
    TMap<FName, UStaticMesh*> Meshes;
    TMap<FName, UMaterialInterface*> Materials;
    GWContent::BuildTextures(Textures);
    GWContent::BuildMeshes(Meshes);
    GWContent::BuildMaterials(Textures, Materials);
    const int32 Systems = GWContent::BuildNiagara(Materials, Meshes);
    UE_LOG(LogGWContent, Display, TEXT("GW_CONTENT_DONE textures=%d meshes=%d materials=%d systems=%d"), Textures.Num(),
           Meshes.Num(), Materials.Num(), Systems);
    return Systems > 0 ? 0 : 1;
}
