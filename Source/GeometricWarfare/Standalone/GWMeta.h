#pragma once
#include "Core/Meta.h"
#include "CoreMinimal.h"
#include "GWNetworkTypes.h"

// Meta progression glue between the wire structs (GWNetworkTypes.h) and the shared rules (Core/Meta.h).
namespace GWMeta
{
inline gwgame::Loadout ToLoadout(const FGWProfileDigest& D)
{
    gwgame::Loadout L;
    for (int32 I = 0; I < gwgame::AttributeCount; ++I)
        L.ranks[I] = D.Ranks.IsValidIndex(I) ? D.Ranks[I] : 0;
    L.weapon = D.Weapon;
    L.pen = D.Pen;
    L.hat = D.Hat;
    L.title = D.Title;
    L.items = {{D.ItemA == 255 ? -1 : D.ItemA, D.ItemB == 255 ? -1 : D.ItemB}};
    L.achievements = D.Achievements;
    return L;
}
inline FGWProfileDigest ToDigest(const gwgame::Loadout& L)
{
    FGWProfileDigest D;
    D.Ranks.Append(L.ranks.data(), L.ranks.size());
    D.Weapon = static_cast<uint8>(FMath::Clamp(L.weapon, 0, 255));
    D.Pen = static_cast<uint8>(FMath::Clamp(L.pen, 0, 255));
    D.Hat = static_cast<uint8>(FMath::Clamp(L.hat, 0, 255));
    D.Title = static_cast<uint8>(FMath::Clamp(L.title, 0, 255));
    D.ItemA = L.items[0] < 0 ? 255 : static_cast<uint8>(L.items[0]);
    D.ItemB = L.items[1] < 0 ? 255 : static_cast<uint8>(L.items[1]);
    D.Achievements = L.achievements;
    return D;
}
inline gwgame::Tally ToTally(const FGWProgressPacket& P)
{
    gwgame::Tally T;
    T.kills = P.Kills;
    T.damage = P.Damage;
    T.walls = P.Walls;
    T.thrown = P.Thrown;
    T.throws = P.Throws;
    T.held = P.Held;
    T.inkGiven = P.InkGiven;
    T.bullseyes = P.Bullseyes;
    T.final = P.Final;
    T.won = P.Won;
    T.bots = P.Bots;
    T.players = P.Players;
    T.items = P.Items;
    T.awards = P.Awards;
    return T;
}
inline FString PageId(int32 Level)
{
    const auto& Levels = gwgame::defenseLevels();
    return Levels.empty() ? FString() : FString(ANSI_TO_TCHAR(Levels[FMath::Clamp(Level, 0, static_cast<int32>(Levels.size()) - 1)].id));
}
} // namespace GWMeta
