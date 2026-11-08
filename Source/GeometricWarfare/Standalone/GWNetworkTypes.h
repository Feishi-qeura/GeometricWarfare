#pragma once
#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GWNetworkTypes.generated.h"

// Network layout (phase 2, sized for 8 players and a full page of enemies):
// - FGW*Net structs are the wire format: small, quantized, and laid out so an unchanged unit costs nothing
//   (one stable slot per simulation unit, ring buffers keyed by id for shots, cues and damage numbers, birth
//   times instead of countdowns, players' own data in a short separate array).
// - FGW*View structs are what the HUD, the world and the effects read. The game state decodes them from the
//   wire arrays on every machine (the host included), so host and clients always see the same thing.

USTRUCT()
struct FGWInputCommand
{
    GENERATED_BODY()
    UPROPERTY() uint32 Sequence = 0;
    UPROPERTY() FVector2D Move = FVector2D::ZeroVector;
    UPROPERTY() FVector2D Aim = FVector2D(1, 0);
    UPROPERTY() bool Fire = false;
};

namespace EGWStatus
{
// Dummy marks a training target (hub and tutorial), drawn as a paper target and never counted as an enemy.
constexpr uint8 Burning = 1, Slowed = 2, Armored = 4, Regenerating = 8, Dummy = 16;
}
// A unit's booleans travel together in one byte.
namespace EGWUnitFlag
{
constexpr uint8 Alive = 1, Human = 2, Bot = 4, Turret = 8, Elite = 16, Carried = 32, Recruitable = 64, Bait = 128;
}
// Positions travel in quarter units (0 to 16383.75 covers every page), facings in 1/256 turns, health as a
// share of the unit's maximum.
namespace GWNet
{
constexpr float PositionStep = .25f;
inline uint16 PackCoord(double V)
{
    return static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(V / PositionStep), 0, 65535));
}
inline float UnpackCoord(uint16 V)
{
    return V * PositionStep;
}
inline uint8 PackAngle(double Radians)
{
    return static_cast<uint8>(FMath::RoundToInt(Radians / (2 * PI) * 256) & 255);
}
inline int16 PackSpeed(double V)
{
    return static_cast<int16>(FMath::Clamp(FMath::RoundToInt(V), -32767, 32767));
}
} // namespace GWNet

USTRUCT()
struct FGWUnitNet
{
    GENERATED_BODY()
    UPROPERTY() int32 Id = -1;
    UPROPERTY() int32 FollowerOf = -1;
    UPROPERTY() uint32 TailOrder = 0;
    UPROPERTY() uint16 X = 0;
    UPROPERTY() uint16 Y = 0;
    UPROPERTY() uint8 Facing = 0;
    UPROPERTY() uint16 Health = 0;
    UPROPERTY() float MaxHP = 0;
    UPROPERTY() float Radius = 25;
    UPROPERTY() uint8 Armor = 0;
    UPROPERTY() uint8 Team = 1;
    UPROPERTY() uint8 Shape = 0;
    UPROPERTY() uint8 State = 0;
    UPROPERTY() uint8 Kind = 0;
    UPROPERTY() uint8 Weapon = 0;
    UPROPERTY() uint8 Status = 0;
    UPROPERTY() uint8 Sides = 3;
    UPROPERTY() uint8 Flags = 0;
    // A fighter's seat colour (255 = none).
    UPROPERTY() uint8 Seat = 255;
};

// What only players (humans and AI teammates) carry: prediction, ammo, skills, ink, throws and statistics.
USTRUCT()
struct FGWPlayerNet
{
    GENERATED_BODY()
    UPROPERTY() int32 Unit = -1;
    UPROPERTY() uint32 AckSequence = 0;
    // Authoritative knockback still decaying on this unit; the owning client folds it into prediction.
    UPROPERTY() float ImpulseX = 0;
    UPROPERTY() float ImpulseY = 0;
    UPROPERTY() float Reload = 0;
    UPROPERTY() bool Charging = false;
    UPROPERTY() float ChargeTime = 0;
    UPROPERTY() int32 Ammo = 0;
    UPROPERTY() int32 PendingUpgrades = 0;
    UPROPERTY() TArray<uint8> Skills;
    UPROPERTY() TArray<int32> Choices;
    UPROPERTY() float Ink = 0;
    UPROPERTY() float MaxInk = 0;
    UPROPERTY() float Respawn = 0;
    UPROPERTY() float Bait = 0;
    UPROPERTY() float Guard = 0;
    UPROPERTY() bool Throwable = false;
    UPROPERTY() int32 Kills = 0;
    UPROPERTY() int32 Held = 0;
    UPROPERTY() int32 Thrown = 0;
    UPROPERTY() int32 Throws = 0;
    UPROPERTY() int32 Walls = 0;
    UPROPERTY() int32 Pushes = 0;
    UPROPERTY() int32 Damage = 0;
    UPROPERTY() int32 InkGiven = 0;
    UPROPERTY() int32 BaitBonus = 0;
    // Throws that came to rest on the hub's bullseye.
    UPROPERTY() int32 Bullseyes = 0;
    // Meta progression: the speed rank that counts in this room (the owner's prediction walks at it) and the
    // single-mission items in effect (a bit per gwgame::Item).
    UPROPERTY() uint8 SpeedRank = 0;
    UPROPERTY() uint8 Items = 0;
};

// What a player tells the room about their own profile (the design doc's 档案摘要, gwgame::Loadout on the wire):
// research ranks, the starting weapon, pen, hat, title, the two items brought and the achievement map. The host
// checks it (out-of-range values count as nothing) and never writes anyone's profile.
USTRUCT()
struct FGWProfileDigest
{
    GENERATED_BODY()
    UPROPERTY() TArray<uint8> Ranks;
    UPROPERTY() uint8 Weapon = 0;
    UPROPERTY() uint8 Pen = 0;
    UPROPERTY() uint8 Hat = 0;
    UPROPERTY() uint8 Title = 0;
    // 255 = an empty item slot.
    UPROPERTY() uint8 ItemA = 255;
    UPROPERTY() uint8 ItemB = 255;
    UPROPERTY() uint64 Achievements = 0;
};

// The host's packet for one player's own profile: a cleared wave's stickers or a page's clear with its stars, and
// that player's share of the mission since the last packet (kills, walls, throws...). Final closes a mission. The
// receiving machine drops a packet beyond the caps (design doc: 奖励包上限).
USTRUCT()
struct FGWProgressPacket
{
    GENERATED_BODY()
    UPROPERTY() FString Level;
    UPROPERTY() uint8 Tier = 1;
    UPROPERTY() int32 Stickers = 0;
    UPROPERTY() uint8 Stars = 0;
    UPROPERTY() bool Clear = false;
    UPROPERTY() int32 Kills = 0;
    UPROPERTY() int32 Damage = 0;
    UPROPERTY() int32 Walls = 0;
    UPROPERTY() int32 Thrown = 0;
    UPROPERTY() int32 Throws = 0;
    UPROPERTY() int32 Held = 0;
    UPROPERTY() int32 InkGiven = 0;
    UPROPERTY() int32 Bullseyes = 0;
    UPROPERTY() bool Final = false;
    UPROPERTY() bool Won = false;
    UPROPERTY() bool Bots = false;
    UPROPERTY() uint8 Players = 0;
    UPROPERTY() uint8 Items = 0;
    // A bit per gwgame::Award this player won at the end.
    UPROPERTY() uint8 Awards = 0;
};

// A shot is sent once when fired (and again if it bounces); clients fly it themselves. Ended marks a shot
// that stopped early (a hit or a wall).
USTRUCT()
struct FGWShotNet
{
    GENERATED_BODY()
    UPROPERTY() int32 Id = 0;
    UPROPERTY() int32 Owner = -1;
    UPROPERTY() uint16 X = 0;
    UPROPERTY() uint16 Y = 0;
    UPROPERTY() int16 VX = 0;
    UPROPERTY() int16 VY = 0;
    UPROPERTY() float Born = 0;
    UPROPERTY() float Life = 0;
    UPROPERTY() float Ended = 0;
    UPROPERTY() uint8 Team = 0;
    UPROPERTY() uint8 Weapon = 0;
};

USTRUCT()
struct FGWEffectNet
{
    GENERATED_BODY()
    UPROPERTY() uint32 Id = 0;
    UPROPERTY() uint16 FromX = 0;
    UPROPERTY() uint16 FromY = 0;
    UPROPERTY() uint16 ToX = 0;
    UPROPERTY() uint16 ToY = 0;
    UPROPERTY() float Radius = 0;
    UPROPERTY() float Born = 0;
    UPROPERTY() float Life = 0;
    UPROPERTY() uint8 Kind = 0;
    UPROPERTY() uint8 Team = 0;
    UPROPERTY() uint8 Style = 0;
    // The one unit a personal cue is meant for (a refused wall stroke); -1 for everyone.
    UPROPERTY() int32 Subject = -1;
};

USTRUCT()
struct FGWDamageNet
{
    GENERATED_BODY()
    UPROPERTY() uint32 Id = 0;
    UPROPERTY() uint16 X = 0;
    UPROPERTY() uint16 Y = 0;
    UPROPERTY() float Amount = 0;
    UPROPERTY() float Born = 0;
    UPROPERTY() int32 Victim = -1;
    UPROPERTY() uint8 Team = 0;
};

USTRUCT()
struct FGWInkDropNet
{
    GENERATED_BODY()
    // 0 marks an empty slot.
    UPROPERTY() int32 Id = 0;
    UPROPERTY() uint16 X = 0;
    UPROPERTY() uint16 Y = 0;
    UPROPERTY() float Amount = 0;
};

USTRUCT()
struct FGWCrateView
{
    GENERATED_BODY()
    UPROPERTY() FVector_NetQuantize Position;
    UPROPERTY() float DropTime = 0;
    UPROPERTY() uint8 Weapon = 1;
};

USTRUCT()
struct FGWWallPatchView
{
    GENERATED_BODY()
    UPROPERTY() int32 Cell = -1;
    UPROPERTY() TArray<uint8> Health;
};

// Defense grid cell flags replicated per cell (the client rebuilds walls, rock and the route from them).
namespace EGWCell
{
// Thick and Bounce mark a drawn wall's type (the marker's and the highlighter's walls).
constexpr uint8 Wall = 1, Rock = 2, Paper = 4, Entrance = 8, Core = 16, Thick = 32, Bounce = 64;
}

// A player of the saved run a room is about to resume: who they were and whether they are back.
USTRUCT()
struct FGWSeatView
{
    GENERATED_BODY()
    UPROPERTY() FString Name;
    UPROPERTY() bool Present = false;
};

// ---- Decoded views (never replicated) ----

struct FGWUnitView
{
    int32 Id = -1;
    int32 FollowerOf = -1;
    uint32 TailOrder = 0;
    uint32 AckSequence = 0;
    float MovementScale = 1;
    FVector Impulse = FVector::ZeroVector;
    float Reload = 0;
    TArray<int32> Skills;
    bool Charging = false;
    float ChargeTime = 0;
    FVector Position = FVector::ZeroVector;
    // Facing in degrees.
    float Angle = 0;
    float HP = 0;
    float MaxHP = 300;
    float Armor = 0;
    float Radius = 25;
    // Real time (FPlatformTime::Seconds) the unit last lost health, for the hit flash.
    double HitAt = -10;
    uint8 Team = 1;
    uint8 Shape = 0;
    uint8 State = 0;
    uint8 Kind = 0;
    uint8 Weapon = 0;
    int32 Ammo = 0;
    bool Human = false;
    bool Bot = false;
    bool Alive = false;
    int32 PendingUpgrades = 0;
    TArray<int32> Choices;
    // EGWStatus bits: lingering skill states clients render as looping effects.
    uint8 Status = 0;
    // Polygon tier of an ally (3-6); a turret is pinned to its cell; elites are enlarged enemies.
    uint8 Sides = 3;
    bool Turret = false;
    bool Elite = false;
    // A tail ally that can be recruited now (shards wait a moment after landing).
    bool Recruitable = true;
    // Defense: a player's own ink, and seconds until a fallen player returns.
    float Ink = 0;
    float MaxInk = 0;
    float Respawn = 0;
    // Throwing teammates: carried in someone's tail, seconds left as bait, seconds before the next grab,
    // and whether the player allows being thrown in Friendly rooms.
    bool Carried = false;
    float Bait = 0;
    float Guard = 0;
    bool Throwable = false;
    // Seat 0-7 (the player colour); -1 for units without one.
    int32 Seat = -1;
    int32 Kills = 0, Held = 0, Thrown = 0, Throws = 0, Walls = 0, Pushes = 0, Damage = 0, InkGiven = 0, BaitBonus = 0;
    int32 Bullseyes = 0;
    bool Dummy = false;
    // Meta progression: the counted speed rank and the items in effect (FGWPlayerNet).
    int32 SpeedRank = 0;
    uint8 Items = 0;
};
struct FGWProjectileView
{
    int32 Id = 0;
    int32 Owner = -1;
    // Where the shot was when last sent; Position is where it has flown to by now.
    FVector Origin = FVector::ZeroVector;
    FVector Position = FVector::ZeroVector;
    uint8 Team = 0;
    uint8 Weapon = 0;
    FVector Direction = FVector(1, 0, 0);
    FVector Velocity = FVector::ZeroVector;
    float Remaining = 0;
};
struct FGWEffectView
{
    FVector From = FVector::ZeroVector;
    FVector To = FVector::ZeroVector;
    float Radius = 0;
    uint8 Kind = 0;
    uint8 Team = 0;
    float Remaining = 0;
    uint32 Id = 0;
    uint8 Style = 0;
    int32 Subject = -1;
};
struct FGWDamageNumberView
{
    uint32 Id = 0;
    FVector Position = FVector::ZeroVector;
    float Amount = 0;
    float Remaining = 0;
    int32 Victim = -1;
    uint8 Team = 0;
};
struct FGWInkDropView
{
    int32 Id = 0;
    FVector Position = FVector::ZeroVector;
    float Amount = 0;
};
