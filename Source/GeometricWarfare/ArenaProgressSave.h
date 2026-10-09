#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ArenaProgressSave.generated.h"

// Stable platform user IDs are the persistence boundary. A future cloud
// adapter can merge this unlock bitmask without depending on transient body IDs.
UCLASS()
class UArenaProgressSave : public USaveGame {
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame) int32 Version=1;
    UPROPERTY(SaveGame) TMap<FString,uint8> WeaponUnlocks;
    // Additive version-1 field: old saves have no entry and use the pistol.
    // Only the permanent left-hand selection is saved, never a crate lease.
    UPROPERTY(SaveGame) TMap<FString,uint8> SelectedLeftWeapons;
    UPROPERTY(SaveGame) TSet<FString> UnusedRifleCodes;
};
