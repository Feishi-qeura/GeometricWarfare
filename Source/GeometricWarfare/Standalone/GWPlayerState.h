#pragma once
#include "Core/Meta.h"
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "GWPlayerState.generated.h"
UCLASS()
class GEOMETRICWARFARE_API AGWPlayerState : public APlayerState
{
    GENERATED_BODY()
public:
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Identity = -1;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 UnitId = -1;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Team = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) bool Ready = false;
    // The player's own permission to be grabbed and thrown in Friendly rooms (sent from their settings).
    UPROPERTY(Replicated, BlueprintReadOnly) bool Throwable = false;
    // Meta progression (phase 4), from the player's own report as the host checked it: the achievement map the
    // room's wall shows (design doc: 64 achievements in 8 bytes), the hat and the title everyone sees.
    UPROPERTY(Replicated) uint64 Achievements = 0;
    UPROPERTY(Replicated) uint8 Hat = 0;
    UPROPERTY(Replicated) uint8 Title = 0;
    // Server only: the account key this player's saved character is filed under.
    FString Account;
    // Server only: the player's checked report (ranks, weapon, pen, hat, title, items); applied to their
    // character in every simulation the room opens.
    gwgame::Loadout Loadout;
    bool HasLoadout = false;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
};
