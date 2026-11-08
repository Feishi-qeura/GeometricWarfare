#pragma once
#include "Core/Snapshot.h"
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GWSaveGame.generated.h"

// How a saved run was played, so continuing it reopens the same kind of game.
UENUM()
enum class EGWSaveOrigin : uint8
{
    Offline,
    Steam,
    Epic
};

USTRUCT()
struct FGWSavedPlayer
{
    GENERATED_BODY()
    // Stable account key (platform prefix + platform account id), never a per-session slot or join order,
    // so every friend gets their own character back however the room fills up again.
    UPROPERTY() FString Account;
    UPROPERTY() FString Name;
    // The identity this player had inside the snapshot; it is remapped to a live identity on load.
    UPROPERTY() int32 Identity = -1;
};

// One saved run. Only the authority (single player or the room host) ever writes one, because only it owns
// the simulation; clients keep nothing, so two machines can never hold diverging copies of a co-op run.
UCLASS()
class GEOMETRICWARFARE_API UGWRunSave : public USaveGame
{
    GENERATED_BODY()
public:
    static constexpr int32 SlotCount = 3;
    static constexpr int32 CurrentVersion = 1;
    UPROPERTY() int32 Version = CurrentVersion;
    UPROPERTY() FDateTime SavedAt;
    UPROPERTY() EGWSaveOrigin Origin = EGWSaveOrigin::Offline;
    UPROPERTY() FString RoomName;
    UPROPERTY() FString HostAccount;
    UPROPERTY() int32 TeamLevel = 1;
    UPROPERTY() float Elapsed = 0;
    UPROPERTY() int32 Bots = 0;
    UPROPERTY() TArray<FGWSavedPlayer> Players;
    UPROPERTY() TArray<uint8> Snapshot;

    void Encode(const gwgame::RunSnapshot& Run);
    bool Decode(gwgame::RunSnapshot& Run) const;
    // Written beside the slot and moved over it, so a crash mid-write never leaves half a save.
    bool WriteToSlot(int32 Slot);
    static UGWRunSave* ReadSlot(int32 Slot);
    static bool DeleteSlot(int32 Slot);
    static FString SlotPath(int32 Slot);
};
