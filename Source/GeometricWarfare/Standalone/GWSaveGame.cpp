#include "GWSaveGame.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
constexpr uint32 SnapshotMagic = 0x31525747; // "GWR1"
template <class T> void Raw(FArchive& Ar, T& Value)
{
    uint8 Byte = static_cast<uint8>(Value);
    Ar << Byte;
    if (Ar.IsLoading())
        Value = static_cast<T>(Byte);
}
void Point(FArchive& Ar, gwgame::Vec2& V)
{
    Ar << V.x << V.y;
}
// Reads a container size and rejects anything a real run could never produce.
bool Count(FArchive& Ar, int32& Value, int32 Limit)
{
    Ar << Value;
    if (Value < 0 || Value > Limit)
        Ar.SetError();
    return !Ar.IsError();
}
void Unit(FArchive& Ar, gwgame::SavedUnit& U)
{
    Ar << U.id << U.team << U.owner << U.followerOf << U.burnOwner;
    Raw(Ar, U.kind);
    Raw(Ar, U.state);
    Raw(Ar, U.shape);
    Ar << U.alive << U.bot << U.hasPickupWeapon;
    Point(Ar, U.position);
    Point(Ar, U.aim);
    Ar << U.tailOrder << U.hp << U.maxHp << U.armor << U.radius << U.cooldown << U.reload << U.burnTime << U.burnDamage
       << U.slowTime << U.skillTime << U.emergencyTime << U.weapon << U.ammo << U.kills << U.pendingUpgrades << U.level;
    for (int& Rank : U.skills)
        Ar << Rank;
    for (int& Choice : U.choices)
        Ar << Choice;
}
void Run(FArchive& Ar, gwgame::RunSnapshot& S)
{
    uint32 Magic = SnapshotMagic;
    Ar << Magic;
    if (Magic != SnapshotMagic)
    {
        Ar.SetError();
        return;
    }
    Ar << S.seed << S.elapsed << S.sharedXP << S.sharedLevel << S.bossWave << S.mazeVersion;
    int32 Walls = static_cast<int32>(S.walls.size());
    if (!Count(Ar, Walls, 4096))
        return;
    S.walls.resize(Walls);
    for (double& Wall : S.walls)
        Ar << Wall;
    int32 Fragments = static_cast<int32>(S.fragments.size());
    if (!Count(Ar, Fragments, 4096))
        return;
    S.fragments.resize(Fragments);
    for (auto& [Cell, Patch] : S.fragments)
    {
        Ar << Cell;
        Ar.Serialize(Patch.data(), Patch.size());
    }
    int32 Units = static_cast<int32>(S.units.size());
    if (!Count(Ar, Units, 2048))
        return;
    S.units.resize(Units);
    for (auto& U : S.units)
        Unit(Ar, U);
    int32 Crates = static_cast<int32>(S.crates.size());
    if (!Count(Ar, Crates, 512))
        return;
    S.crates.resize(Crates);
    for (auto& C : S.crates)
    {
        Ar << C.id << C.weapon;
        Point(Ar, C.position);
        Ar << C.dropTime << C.remaining;
    }
}
} // namespace

void UGWRunSave::Encode(const gwgame::RunSnapshot& In)
{
    gwgame::RunSnapshot Copy = In;
    Snapshot.Reset();
    FMemoryWriter Writer(Snapshot);
    Run(Writer, Copy);
}

bool UGWRunSave::Decode(gwgame::RunSnapshot& Out) const
{
    FMemoryReader Reader(Snapshot);
    gwgame::RunSnapshot Loaded;
    Run(Reader, Loaded);
    if (Reader.IsError())
        return false;
    Out = MoveTemp(Loaded);
    return true;
}

FString UGWRunSave::SlotPath(int32 Slot)
{
    return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("GWRun%d.sav"), Slot + 1);
}

bool UGWRunSave::WriteToSlot(int32 Slot)
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

UGWRunSave* UGWRunSave::ReadSlot(int32 Slot)
{
    TArray<uint8> Bytes;
    if (Slot < 0 || Slot >= SlotCount || !FFileHelper::LoadFileToArray(Bytes, *SlotPath(Slot), FILEREAD_Silent))
        return nullptr;
    auto* Save = Cast<UGWRunSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    return Save && Save->Version == CurrentVersion ? Save : nullptr;
}

bool UGWRunSave::DeleteSlot(int32 Slot)
{
    return Slot >= 0 && Slot < SlotCount && IFileManager::Get().Delete(*SlotPath(Slot), false, true, true);
}
