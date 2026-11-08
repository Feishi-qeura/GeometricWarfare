#pragma once
#include "CoreMinimal.h"

class UMaterialInterface;
class UPackage;
class UStaticMesh;
class UTexture2D;

DECLARE_LOG_CATEGORY_EXTERN(LogGWContent, Log, All);

// Every generated presentation asset lives under /Game/Sketch and is rebuilt from code,
// so the hand-drawn look stays reviewable and reproducible without editor-only edits.
namespace GWContent
{
UPackage* NewAssetPackage(const FString& LongPackageName);
bool SaveAsset(UObject* Asset);

void BuildTextures(TMap<FName, UTexture2D*>& Out);
void BuildMeshes(TMap<FName, UStaticMesh*>& Out);
void BuildMaterials(const TMap<FName, UTexture2D*>& Textures, TMap<FName, UMaterialInterface*>& Out);
int32 BuildNiagara(const TMap<FName, UMaterialInterface*>& Materials, const TMap<FName, UStaticMesh*>& Meshes);
} // namespace GWContent
