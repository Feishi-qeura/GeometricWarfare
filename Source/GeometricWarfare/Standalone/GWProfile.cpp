#include "GWProfile.h"
#include "Core/Defense.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

bool UGWProfileSave::Cleared(const FString& Level) const
{
    const uint8* Found = Results.Find(Level);
    return Found && (*Found & ClearedBit);
}

uint8 UGWProfileSave::BestStars(const FString& Level) const
{
    const uint8* Found = Results.Find(Level);
    return Found ? (*Found & 7) : 0;
}

int32 UGWProfileSave::StarCount() const
{
    int32 Count = 0;
    for (const auto& Pair : Results)
        Count += FMath::CountBits(Pair.Value & 7);
    return Count;
}

bool UGWProfileSave::Unlocked(int32 LevelIndex) const
{
    const auto& Levels = gwgame::defenseLevels();
    if (LevelIndex <= 0)
        return true;
    return LevelIndex < static_cast<int32>(Levels.size()) && Cleared(FString(ANSI_TO_TCHAR(Levels[LevelIndex - 1].id)));
}

int32 UGWProfileSave::UnlockedTier() const
{
    return ToCore().unlockedTier();
}

gwgame::MetaProfile UGWProfileSave::ToCore() const
{
    gwgame::MetaProfile Core;
    Core.stickers = Stickers;
    Core.stickersTotal = StickersTotal;
    Core.wavesCleared = WavesCleared;
    Core.levelsCleared = LevelsCleared;
    for (const auto& Pair : Results)
        Core.results[TCHAR_TO_UTF8(*Pair.Key)] = Pair.Value;
    for (int32 I = 0; I < gwgame::AttributeCount; ++I)
        Core.ranks[I] = Ranks.IsValidIndex(I) ? static_cast<uint8>(gwgame::sanitizeRank(Ranks[I])) : 0;
    // The first of each list is everyone's.
    Core.weapons = Weapons | 1u;
    Core.pens = Pens | 1u;
    Core.hats = Hats | 1u;
    Core.titles = Titles | 1u;
    for (int32 I = 0; I < gwgame::ItemCount; ++I)
        Core.items[I] = Items.IsValidIndex(I) ? static_cast<uint8>(FMath::Min<int32>(Items[I], gwgame::MaxItemStack)) : 0;
    for (int32 I = 0; I < gwgame::ItemSlots; ++I)
        Core.loadout[I] = Loadout.IsValidIndex(I) && Loadout[I] >= 0 && Loadout[I] < gwgame::ItemCount ? Loadout[I] : -1;
    Core.weapon = Weapon;
    Core.pen = Pen;
    Core.hat = Hat;
    Core.title = Title;
    Core.achievements = Achievements & gwgame::AchievementMask;
    gwgame::Counters& C = Core.counters;
    C.kills = Kills;
    C.damage = Damage;
    C.walls = Walls;
    C.thrown = Thrown;
    C.throws = Throws;
    C.held = Held;
    C.inkGiven = InkGiven;
    C.bullseyes = Bullseyes;
    C.missions = Missions;
    C.wins = Wins;
    C.loneWins = LoneWins;
    C.maxParty = MaxParty;
    C.maxItems = MaxItems;
    for (int32 I = 0; I < gwgame::AwardCount; ++I)
        C.awards[I] = Awards.IsValidIndex(I) ? Awards[I] : 0;
    return Core;
}

void UGWProfileSave::FromCore(const gwgame::MetaProfile& Core)
{
    Stickers = Core.stickers;
    StickersTotal = Core.stickersTotal;
    WavesCleared = Core.wavesCleared;
    LevelsCleared = Core.levelsCleared;
    Results.Reset();
    for (const auto& [Page, Bits] : Core.results)
        Results.Add(UTF8_TO_TCHAR(Page.c_str()), Bits);
    Ranks.SetNum(gwgame::AttributeCount);
    for (int32 I = 0; I < gwgame::AttributeCount; ++I)
        Ranks[I] = Core.ranks[I];
    Weapons = Core.weapons;
    Pens = Core.pens;
    Hats = Core.hats;
    Titles = Core.titles;
    Items.SetNum(gwgame::ItemCount);
    for (int32 I = 0; I < gwgame::ItemCount; ++I)
        Items[I] = Core.items[I];
    Loadout.SetNum(gwgame::ItemSlots);
    for (int32 I = 0; I < gwgame::ItemSlots; ++I)
        Loadout[I] = Core.loadout[I];
    Weapon = Core.weapon;
    Pen = Core.pen;
    Hat = Core.hat;
    Title = Core.title;
    Achievements = Core.achievements;
    const gwgame::Counters& C = Core.counters;
    Kills = C.kills;
    Damage = C.damage;
    Walls = C.walls;
    Thrown = C.thrown;
    Throws = C.throws;
    Held = C.held;
    InkGiven = C.inkGiven;
    Bullseyes = C.bullseyes;
    Missions = C.missions;
    Wins = C.wins;
    LoneWins = C.loneWins;
    MaxParty = C.maxParty;
    MaxItems = C.maxItems;
    Awards.SetNum(gwgame::AwardCount);
    for (int32 I = 0; I < gwgame::AwardCount; ++I)
        Awards[I] = C.awards[I];
    PlayedAt = FDateTime::Now();
}

FString UGWProfileSave::SlotPath(int32 Slot)
{
    return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("GWProfile%d.sav"), Slot + 1);
}

UGWProfileSave* UGWProfileSave::Read(int32 Slot)
{
    TArray<uint8> Bytes;
    if (Slot < 0 || Slot >= SlotCount || !FFileHelper::LoadFileToArray(Bytes, *SlotPath(Slot), FILEREAD_Silent))
        return nullptr;
    auto* Profile = Cast<UGWProfileSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    // Older versions load whole (new fields keep their defaults) and are written back as the current one.
    if (!Profile || Profile->Version < 1 || Profile->Version > CurrentVersion)
        return nullptr;
    Profile->Version = CurrentVersion;
    return Profile;
}

UGWProfileSave* UGWProfileSave::ReadOrCreate(int32 Slot)
{
    if (UGWProfileSave* Existing = Read(Slot))
        return Existing;
    auto* Fresh = NewObject<UGWProfileSave>();
    Fresh->CreatedAt = Fresh->PlayedAt = FDateTime::Now();
    return Fresh;
}

// Written beside the slot and moved over it, so a crash mid-write never leaves half a profile.
bool UGWProfileSave::Write(int32 Slot)
{
    if (Slot < 0 || Slot >= SlotCount)
        return false;
    TArray<uint8> Bytes;
    if (!UGameplayStatics::SaveGameToMemory(this, Bytes))
        return false;
    const FString Path = SlotPath(Slot), Temporary = Path + TEXT(".tmp");
    IFileManager& Files = IFileManager::Get();
    Files.MakeDirectory(*FPaths::GetPath(Path), true);
    if (!FFileHelper::SaveArrayToFile(Bytes, *Temporary))
        return false;
    if (!Files.Move(*Path, *Temporary, true, true))
    {
        Files.Delete(*Temporary, false, true, true);
        return false;
    }
    return true;
}

bool UGWProfileSave::Delete(int32 Slot)
{
    return Slot >= 0 && Slot < SlotCount && IFileManager::Get().Delete(*SlotPath(Slot), false, true, true);
}
