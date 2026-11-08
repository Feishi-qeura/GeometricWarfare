#pragma once
#include "Core/Meta.h"
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GWProfile.generated.h"

// One player's own progress (the design doc's 玩家档案): it travels with the player into any room and is
// written only by that player's machine. The host computes rewards and sends them; this side caps each
// packet, so a hostile host can never flood a profile. Separate from UGWRunSave (the host's run snapshot).
// The fields mirror gwgame::MetaProfile (Core/Meta.h), where the rules live: research, the shop, the wardrobe,
// rewards and achievements all run there through ToCore/FromCore.
UCLASS()
class GEOMETRICWARFARE_API UGWProfileSave : public USaveGame
{
    GENERATED_BODY()
public:
    static constexpr int32 SlotCount = 3;
    // Version 2 (phase 4) added meta progression; a version-1 profile loads with its pages, stickers and tutorial
    // flag intact and everything new at its defaults.
    static constexpr int32 CurrentVersion = 2;
    // Best result per page id ("1-1"): star bits 0-2, the highest tier cleared in bits 3-5, bit 7 = cleared.
    static constexpr uint8 ClearedBit = 0x80;
    UPROPERTY() int32 Version = CurrentVersion;
    UPROPERTY() FDateTime CreatedAt;
    UPROPERTY() FDateTime PlayedAt;
    UPROPERTY() int32 Stickers = 0;
    UPROPERTY() int32 StickersTotal = 0;
    UPROPERTY() int32 WavesCleared = 0;
    UPROPERTY() int32 LevelsCleared = 0;
    UPROPERTY() TMap<FString, uint8> Results;
    // The tutorial runs once per save (design doc: 新存档 → 新手引导); finishing or skipping it sets this.
    // Older profiles load without it and are offered the tutorial once, which can be skipped.
    UPROPERTY() bool TutorialDone = false;
    // Hub stations this save has opened, one bit per gwgame::HubStation: the hub's edge tags point the way to
    // the others until each has been visited once.
    UPROPERTY() int32 StationsSeen = 0;
    // ---- Meta progression (phase 4) ----
    // Research ranks, one per gwgame::Attribute.
    UPROPERTY() TArray<uint8> Ranks;
    // A bit per weapon, pen, hat and title owned (each list's first entry is always owned).
    UPROPERTY() uint32 Weapons = 1;
    UPROPERTY() uint32 Pens = 1;
    UPROPERTY() uint32 Hats = 1;
    UPROPERTY() uint32 Titles = 1;
    // Item stock, one count per gwgame::Item, and the two slots brought into the next mission (-1 = empty).
    UPROPERTY() TArray<uint8> Items;
    UPROPERTY() TArray<int32> Loadout;
    UPROPERTY() int32 Weapon = 0;
    UPROPERTY() int32 Pen = 0;
    UPROPERTY() int32 Hat = 0;
    UPROPERTY() int32 Title = 0;
    UPROPERTY() uint64 Achievements = 0;
    // Lifetime numbers the achievements read (gwgame::Counters).
    UPROPERTY() int64 Kills = 0;
    UPROPERTY() int64 Damage = 0;
    UPROPERTY() int64 Walls = 0;
    UPROPERTY() int64 Thrown = 0;
    UPROPERTY() int64 Throws = 0;
    UPROPERTY() int64 Held = 0;
    UPROPERTY() int64 InkGiven = 0;
    UPROPERTY() int64 Bullseyes = 0;
    UPROPERTY() int64 Missions = 0;
    UPROPERTY() int64 Wins = 0;
    UPROPERTY() int64 LoneWins = 0;
    UPROPERTY() int32 MaxParty = 0;
    UPROPERTY() int32 MaxItems = 0;
    UPROPERTY() TArray<int32> Awards;

    bool Cleared(const FString& Level) const;
    // The page's star bits.
    uint8 BestStars(const FString& Level) const;
    int32 StarCount() const;
    // The first page is always open; every other page opens once the page before it is cleared.
    bool Unlocked(int32 LevelIndex) const;
    // The highest difficulty tier this profile has opened (gwgame::MetaProfile::unlockedTier).
    int32 UnlockedTier() const;

    gwgame::MetaProfile ToCore() const;
    void FromCore(const gwgame::MetaProfile& Core);

    static UGWProfileSave* Read(int32 Slot);
    static UGWProfileSave* ReadOrCreate(int32 Slot);
    bool Write(int32 Slot);
    static bool Delete(int32 Slot);
    static FString SlotPath(int32 Slot);
};
