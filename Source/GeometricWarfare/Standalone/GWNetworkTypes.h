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
    UPROPERTY() bool Dodge = false;
    // E held (the expedition's doors and chests that are held open).
    UPROPERTY() bool Use = false;
    // R held (2026-11-17: ink mends every hurt player in a circle round this one, the player too).
    UPROPERTY() bool Mend = false;
};

namespace EGWStatus
{
// Dummy marks a training target (hub and tutorial), drawn as a paper target and never counted as an enemy. Deck: up
// on the box's platform; Climbing: on a ladder between the two levels.
constexpr uint8 Burning = 1, Slowed = 2, Armored = 4, Regenerating = 8, Dummy = 16, Deck = 32, Climbing = 64;
// An expedition foe bleeding from a staple or a thumbtack (2026-11-17).
constexpr uint8 Bleeding = 128;
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
// An expedition kit slot in one number: kind (2 bits) << 30 | count (4) << 26 | variant + 1 (8) << 18 | what (18).
inline uint32 PackSlot(uint8 Kind, int32 What, int32 Count, int32 Variant)
{
    return (static_cast<uint32>(Kind & 3) << 30) | (static_cast<uint32>(FMath::Clamp(Count, 0, 15)) << 26) |
           (static_cast<uint32>(FMath::Clamp(Variant + 1, 0, 255)) << 18) | static_cast<uint32>(FMath::Clamp(What, 0, (1 << 18) - 1));
}
inline void UnpackSlot(uint32 Packed, uint8& Kind, int32& What, int32& Count, int32& Variant)
{
    Kind = static_cast<uint8>(Packed >> 30);
    Count = static_cast<int32>((Packed >> 26) & 15);
    Variant = static_cast<int32>((Packed >> 18) & 255) - 1;
    What = Kind ? static_cast<int32>(Packed & ((1u << 18) - 1)) : -1;
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
    // The expedition: an enemy's breed (gwgame::Breed; an item's body on the field is 100 + its Gear kind, 255 nothing)
    // and the marks items leave on enemies (EGWMark).
    UPROPERTY() uint8 Breed = 255;
    UPROPERTY() uint8 Marks = 0;
    // An enemy's act (gwgame::FoeAct) and a boss's parts byte (bits 0-1 its tools, gwgame::PartsSecondForm, PartsSplit);
    // for an expedition player, Act carries what the floor has left on them (EGWAfflict).
    UPROPERTY() uint8 Act = 0;
    UPROPERTY() uint8 Parts = 0;
};
namespace EGWMark
{
constexpr uint8 Stunned = 1, Stamped = 2, Gilded = 4, Highlighted = 8, Wet = 16, Erased = 32;
}
// What the floor's foes and hazards have left on an expedition player (FGWUnitNet::Act on a player).
namespace EGWAfflict
{
constexpr uint8 Bleeding = 1, Circled = 2, Sealed = 4, Smeared = 8, Dragged = 16, Streak = 32, Fogged = 64, DryBrush = 128;
}
// An expedition player's states (FGWPlayerNet::KitFlags).
namespace EGWKit
{
constexpr uint8 Ghost = 1, Extracted = 2, FinalExit = 4, Note = 8, Dual = 16;
}

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
    // The dodge's research (2026-11-17): the cooldown's and the kick's ranks counted here (the owner predicts by them).
    UPROPERTY() uint8 DashRank = 0;
    UPROPERTY() uint8 KickRank = 0;
    // The box: how far a held rocket trigger has charged (0-255 of a full charge; everyone sees a friend winding
    // up), and the fallen teammates this player brought back with ink.
    UPROPERTY() uint8 RocketCharge = 0;
    UPROPERTY() int32 Revives = 0;
    // The expedition (everyone sees everyone's kit: design 9.1, 队友贴纸与 5 格图标): the hotbar's five slots and then the
    // bag's open ones (a slot each: GWNet::PackSlot), the slot in hand, the pouch, the looks worn (category order, 255
    // none), EGWKit states, the stickers banked on the way out, a revive drawing's trait (gwgame::PenBiome, 255 none) and
    // shape bonus, the hold on a door or chest (0-255 of its time), the walking pace on top of research, the magazine, the
    // shields left (tape and graphite) and the pens the player may draw a revive with (a bit each). The run's coins and
    // the bag's open slots (2026-11-16).
    UPROPERTY() TArray<uint32> Hotbar;
    UPROPERTY() uint8 Selected = 0;
    UPROPERTY() int32 Pouch = 0;
    UPROPERTY() int32 Coins = 0;
    UPROPERTY() uint8 PackSize = 5;
    UPROPERTY() TArray<uint8> Wear;
    UPROPERTY() uint8 KitFlags = 0;
    UPROPERTY() int32 Banked = 0;
    UPROPERTY() uint8 Trait = 255;
    UPROPERTY() uint8 Bonus = 0;
    UPROPERTY() uint8 Hold = 0;
    UPROPERTY() float Pace = 1;
    UPROPERTY() int16 Clip = 0;
    UPROPERTY() uint8 Shield = 0;
    UPROPERTY() uint8 Pens = 0;
    // Each slot's reforged nib (gwgame::PenBiome, 255 none), the prank this player voted for (255 none), the pens
    // borrowed from a rack (a bit each) and the nibs reforged this run.
    UPROPERTY() TArray<uint8> Nibs;
    UPROPERTY() uint8 Vote = 255;
    UPROPERTY() uint8 Borrowed = 0;
    UPROPERTY() uint8 Reforges = 0;
    // This player's own unit id while ink flows from them with R this moment (to everyone hurt in their circle); -1 not.
    UPROPERTY() int32 Mending = -1;
};

// ---- The expedition's floor (design v2.1) ----
// A room: its interior in cells, kind and state (gwgame::RoomKind, RoomState), corridors (a bit per side), event machine
// (gwgame::RoomEvent, 255 none), whether the party has been in it, its waves, a clock (an armed room's grace; a chained
// chest's hold, 0-1) and its door cells (solid in the grid's cells while the room is shut).
USTRUCT()
struct FGWRoomNet
{
    GENERATED_BODY()
    UPROPERTY() uint8 X = 0;
    UPROPERTY() uint8 Y = 0;
    UPROPERTY() uint8 W = 1;
    UPROPERTY() uint8 H = 1;
    UPROPERTY() uint8 Kind = 0;
    UPROPERTY() uint8 State = 0;
    UPROPERTY() uint8 Links = 0;
    UPROPERTY() uint8 Event = 255;
    UPROPERTY() bool Explored = false;
    UPROPERTY() uint8 Wave = 0;
    UPROPERTY() uint8 Waves = 0;
    UPROPERTY() float Clock = 0;
    UPROPERTY() TArray<int16> Doors;
};
// A thing lying on the floor (gwgame::GroundLoot): one slot per id like the ink drops; Id 0 marks an empty slot.
USTRUCT()
struct FGWLootNet
{
    GENERATED_BODY()
    UPROPERTY() int32 Id = 0;
    UPROPERTY() uint8 Kind = 0;
    UPROPERTY() uint16 What = 0;
    UPROPERTY() int16 Amount = 0;
    UPROPERTY() int8 Owner = -1;
    UPROPERTY() uint16 X = 0;
    UPROPERTY() uint16 Y = 0;
    // A quality gun's variant (gwgame::gunVariant), -1 for anything else.
    UPROPERTY() int8 Variant = -1;
    // A toss out of an opened chest (2026-11-17): its arc's height in 4-unit steps and its flight in hundredths of a
    // second; 0 once it has landed (clients draw the arc from when they first see it).
    UPROPERTY() uint8 Hop = 0;
    UPROPERTY() uint8 Flight = 0;
};
// A shelf's ware or an event machine (Kind gwgame::LootKind; ServiceLoot is a machine, What its gwgame::Service).
USTRUCT()
struct FGWOfferNet
{
    GENERATED_BODY()
    UPROPERTY() uint8 Room = 0;
    UPROPERTY() uint8 Kind = 0;
    UPROPERTY() uint8 What = 0;
    UPROPERTY() uint16 Price = 0;
    UPROPERTY() uint16 X = 0;
    UPROPERTY() uint16 Y = 0;
    UPROPERTY() bool Sold = false;
    // A machine's choice: the prank a ballot box stands for, the pen a rack holds (-1 none).
    UPROPERTY() int8 Extra = -1;
};
USTRUCT()
struct FGWChestNet
{
    GENERATED_BODY()
    UPROPERTY() int32 Id = 0;
    UPROPERTY() uint8 Room = 0;
    UPROPERTY() uint16 X = 0;
    UPROPERTY() uint16 Y = 0;
    UPROPERTY() bool Opened = false;
    UPROPERTY() uint8 Grade = 0;
    // 1 green .. 5 red (gwgame::Quality).
    UPROPERTY() uint8 Quality = 1;
};
// An exit door (the early one of EV13, or the last boss's final door) and what lies on the floor (gwgame::ZoneKind: the
// items' zones, the foes' hazards, the biome's terrain; What its colour or variant, Life the seconds it was laid for,
// Charged a puddle with a zapper's charge in it).
USTRUCT()
struct FGWExitNet
{
    GENERATED_BODY()
    UPROPERTY() uint16 X = 0;
    UPROPERTY() uint16 Y = 0;
    UPROPERTY() uint8 Room = 0;
    UPROPERTY() bool Final = false;
};
USTRUCT()
struct FGWZoneNet
{
    GENERATED_BODY()
    UPROPERTY() uint8 Kind = 0;
    UPROPERTY() uint16 FromX = 0;
    UPROPERTY() uint16 FromY = 0;
    UPROPERTY() uint16 ToX = 0;
    UPROPERTY() uint16 ToY = 0;
    UPROPERTY() float Radius = 0;
    UPROPERTY() float Remaining = 0;
    UPROPERTY() uint8 What = 0;
    UPROPERTY() float Life = 0;
    UPROPERTY() bool Charged = false;
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
    // The weapons owned (a bit per kind): the expedition's chests and shelves favour them.
    UPROPERTY() uint8 Weapons = 1;
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
    // The expedition (gwgame::Tally): feats (a bit per gwgame::Feat), enemies erased, elites felled, stickers dropped
    // for teammates and banked on the way out, the biome bosses felled and the revive shape bonuses (a bit each).
    UPROPERTY() uint64 Feats = 0;
    UPROPERTY() int32 Erased = 0;
    UPROPERTY() int32 Elites = 0;
    UPROPERTY() int32 Given = 0;
    UPROPERTY() int32 Banked = 0;
    UPROPERTY() uint8 Bosses = 0;
    UPROPERTY() uint8 Shapes = 0;
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
    // A quality gun's colour (gwgame::GunVariant::tint; 8-11 a crayon round's red, blue, yellow, green), 0 plain.
    UPROPERTY() uint8 Tint = 0;
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
// Thick and Bounce mark a drawn wall's type (the marker's and the highlighter's walls); Deck the box's platform
// (sent with Rock: it is rock to everything down in the box).
constexpr uint8 Wall = 1, Rock = 2, Paper = 4, Entrance = 8, Core = 16, Thick = 32, Bounce = 64, Deck = 128;
}

// A ladder down the box wall: the platform cell it hangs from and the floor cell its foot stands in.
USTRUCT()
struct FGWLadderNet
{
    GENERATED_BODY()
    UPROPERTY() int16 Deck = -1;
    UPROPERTY() int16 Pit = -1;
};

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
    // Real time (GWClock::Seconds) the unit last lost health, for the hit flash.
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
    // Meta progression: the counted speed and dodge ranks and the items in effect (FGWPlayerNet).
    int32 SpeedRank = 0, DashRank = 0, KickRank = 0;
    uint8 Items = 0;
    // The box: up on the platform, on a ladder, a held rocket trigger's charge (0..1), revives paid for.
    bool Deck = false;
    bool Climbing = false;
    float RocketCharge = 0;
    int32 Revives = 0;
    // The expedition (FGWPlayerNet): the hotbar, the slot in hand, the pouch, looks worn (category order, -1 none),
    // states, stickers banked, a revive drawing's trait (-1 none) and shape bonus, the hold (0-1), magazine, shields
    // and pens; an enemy's breed (-1 none; 100 + Gear for an item's body) and marks (EGWMark).
    struct FSlot
    {
        uint8 Kind = 0;
        int32 What = -1, Count = 0;
        // A quality gun's variant (-1 an armoury gun or an item).
        int32 Variant = -1;
    };
    FSlot Hotbar[5];
    // The bag's slots (the first PackSize are open) and the run's coins.
    FSlot Pack[10];
    int32 PackSize = 5, Coins = 0;
    // A kit slot by index: 0-4 the hotbar, 5 on the bag.
    const FSlot& KitSlot(int32 Index) const
    {
        return Index < 5 ? Hotbar[FMath::Clamp(Index, 0, 4)] : Pack[FMath::Clamp(Index - 5, 0, 9)];
    }
    int32 Selected = 0, Pouch = 0, Banked = 0, Trait = -1, Bonus = 0, Clip = 0, Breed = -1;
    int32 Wear[6] = {-1, -1, -1, -1, -1, -1};
    bool Ghost = false, Extracted = false, FinalExit = false, Note = false, Dual = false;
    float Hold = 0, Shield = 0;
    uint8 Pens = 0, Marks = 0;
    // An enemy's act and a boss's parts (FGWUnitNet); on a player, Act is EGWAfflict bits. Each slot's nib (-1 none),
    // the vote cast (-1 none), the pens borrowed and nibs reforged.
    uint8 Act = 0, Parts = 0;
    int32 Nibs[5] = {-1, -1, -1, -1, -1};
    int32 Vote = -1;
    uint8 Borrowed = 0, Reforges = 0;
    // This player's id while ink flows from them with R (everyone hurt in MendReach takes a share); -1 not.
    int32 Mending = -1;
};
struct FGWLootView
{
    int32 Id = 0;
    uint8 Kind = 0;
    int32 What = 0, Amount = 0, Owner = -1, Variant = -1;
    FVector Position = FVector::ZeroVector;
    // A chest's toss still in the air: its arc's height and flight seconds (0: on the floor).
    float Hop = 0, Flight = 0;
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
    // Fired from up on the platform: drawn from its muzzle up there, over the platform, then dropping into the box.
    bool Deck = false;
    // Seconds flown, the whole flight its range allows, and whether the host stopped it early (a hit).
    float Age = 0, Life = 0;
    bool Stopped = false;
    // A quality gun's colour (FGWShotNet::Tint).
    uint8 Tint = 0;
    // Whether the shot runs out of range (rather than hitting something) within Ahead seconds: such a shot does
    // not vanish, it drops to the floor (AGWWorld's spent shots).
    bool RanOut(float Ahead) const
    {
        return !Stopped && Life > 0 && Age + Ahead >= Life - .03f;
    }
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
