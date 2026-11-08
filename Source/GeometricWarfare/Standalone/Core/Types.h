#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <unordered_map>
#include <vector>

namespace gwgame
{
constexpr double Pi = 3.14159265358979323846;
struct Vec2
{
    double x = 0, y = 0;
    Vec2 operator+(Vec2 b) const
    {
        return {x + b.x, y + b.y};
    }
    Vec2 operator-(Vec2 b) const
    {
        return {x - b.x, y - b.y};
    }
    Vec2 operator*(double s) const
    {
        return {x * s, y * s};
    }
    Vec2& operator+=(Vec2 b)
    {
        x += b.x;
        y += b.y;
        return *this;
    }
    double dot(Vec2 b) const
    {
        return x * b.x + y * b.y;
    }
    double lengthSquared() const
    {
        return dot(*this);
    }
    double length() const
    {
        return std::sqrt(lengthSquared());
    }
    Vec2 normalized() const
    {
        double n = length();
        return n > 1e-9 ? (*this) * (1 / n) : Vec2{};
    }
    bool finite() const
    {
        return std::isfinite(x) && std::isfinite(y);
    }
};
inline double distanceToSegment(Vec2 p, Vec2 a, Vec2 b)
{
    Vec2 d = b - a;
    double n = d.lengthSquared();
    return (p - (a + d * (n > 1e-9 ? std::clamp((p - a).dot(d) / n, 0., 1.) : 0))).length();
}
inline bool segmentBox(Vec2 a, Vec2 b, Vec2 lo, Vec2 hi, double* fraction = nullptr)
{
    Vec2 d = b - a;
    double enter = 0, leave = 1;
    for (int axis = 0; axis < 2; ++axis)
    {
        double s = axis ? a.y : a.x, v = axis ? d.y : d.x, l = axis ? lo.y : lo.x, h = axis ? hi.y : hi.x;
        if (std::abs(v) < 1e-9)
        {
            if (s < l || s > h)
                return false;
        }
        else
        {
            double x = (l - s) / v, y = (h - s) / v;
            if (x > y)
                std::swap(x, y);
            enter = std::max(enter, x);
            leave = std::min(leave, y);
            if (enter > leave)
                return false;
        }
    }
    if (fraction)
        *fraction = enter;
    return true;
}
enum class UnitKind : uint8_t
{
    Fighter,
    Companion,
    Monster
};
enum class UnitState : uint8_t
{
    Free,
    Idle,
    Waiting,
    Following,
    Launched
};
enum class Shape : uint8_t
{
    Circle,
    Square,
    Rectangle,
    Triangle,
    Pentagon,
    Hexagon,
    Trapezoid
};
// Every mode is a rule set over the same units, weapons and maze: Survival is the original endless
// arena, Defense is the graph-paper line defense (walls drawn in ink, enemies march on the core), Hub is
// the room everyone waits in between missions (stations, training targets, no enemies, never ends).
enum class RuleSet : uint8_t
{
    Survival,
    Defense,
    Hub
};
// The room's trolling level (the design doc's 整活强度), set by the host. Off: nobody can be grabbed or
// thrown and friendly fire never shoves. Friendly: only players who opted in can be grabbed, weak shoves.
// Chaos: anyone can be grabbed, strong shoves.
enum class PrankLevel : uint8_t
{
    Off,
    Friendly,
    Chaos
};
inline double prankPush(PrankLevel level)
{
    return level == PrankLevel::Off ? 0 : level == PrankLevel::Chaos ? 1.5 : .6;
}
// Wall types (the design doc's 墙型, each drawn by its own pen; Meta.h): plain, thick (every bite takes half as
// much) and bounce (shots ricochet off it).
enum class WallKind : uint8_t
{
    Plain,
    Thick,
    Bounce
};
constexpr double ThickBite = .5;
constexpr int BounceRicochets = 2;
// What a player brought from their profile (meta progression, Meta.h): research ranks as the host accepted them,
// the starting weapon, the pen, the hat, and the single-mission items in effect. Ranks only count up to the
// room's difficulty tier (Battle::perk).
struct Perks
{
    std::array<uint8_t, 6> ranks{};
    int weapon = 0, pen = 0, hat = 0;
    bool bottle = false, magnet = false, carbon = false, plaster = false;
    // A bit per item kind applied this mission (what the player card shows).
    uint8_t itemsUsed = 0;
    // A plaster spent on a fall: the player gets up where they fell instead of at the core.
    bool standUp = false;
};
// What a player did for (and to) the team during a run: the awards and the bait stickers read these.
struct PlayerStats
{
    // held: enemies pulled while lying as bait; waveHeld counts the current wave, baitBonus the stickers earned.
    int held = 0, waveHeld = 0, baitBonus = 0, thrown = 0, throws = 0, walls = 0, pushes = 0;
    // launches counts every throw (allies too); bullseyes the throws that came to rest on the hub's target.
    int launches = 0, bullseyes = 0;
    double damage = 0, inkGiven = 0;
};
// Sides are the tier: allies start as triangles and grow one side per merged triangle, up to six.
constexpr int MinSides = 3, MaxSides = 6;
inline int clampSides(int sides)
{
    return std::clamp(sides, MinSides, MaxSides);
}
inline double sideHealth(int sides)
{
    return 1 + .35 * (clampSides(sides) - MinSides);
}
inline double sideDamage(int sides)
{
    return 1 + .25 * (clampSides(sides) - MinSides);
}
inline Shape polygonShape(int sides)
{
    switch (clampSides(sides))
    {
    case 4:
        return Shape::Square;
    case 5:
        return Shape::Pentagon;
    case 6:
        return Shape::Hexagon;
    default:
        return Shape::Triangle;
    }
}
struct Input
{
    Vec2 move{}, aim{1, 0};
    bool fire = false;
    uint32_t sequence = 0;
};
inline bool newerSequence(uint32_t a, uint32_t b)
{
    return static_cast<int32_t>(a - b) > 0;
}
struct Unit
{
    int id = -1, team = 1, owner = -1, previousOwner = -1, followerOf = -1, launchOwner = -1;
    UnitKind kind = UnitKind::Fighter;
    UnitState state = UnitState::Free;
    Shape shape = Shape::Circle;
    Vec2 position{}, velocity{}, impulse{}, aim{1, 0};
    uint32_t tailOrder = 0, ackSequence = 0, lastQueuedSequence = 0;
    // A bot is an AI teammate that plays like a human: it levels up, picks skills and collects crates.
    bool alive = true, human = false, bot = false;
    bool hasPickupWeapon = false;
    double hp = 300, maxHp = 300, armor = 0, radius = 25, cooldown = 0, reload = 0, launchTime = 0, think = 0,
           hitFlash = 0;
    double burnTime = 0, burnDamage = 0, slowTime = 0, skillTime = 0, castTime = 0, emergencyTime = 0;
    bool charging = false;
    double chargeTime = 0, launchPower = 0, launchInitialSpeed = 1, fireExposure = 0;
    // A shard flies with a fraction of its parent's throw damage; it becomes recruitable after shardDelay.
    double impactScale = 1, shardDelay = 0;
    int burnOwner = -1;
    // Allies are polygons; a turret is an ally of four or more sides pinned to its grid cell.
    // target: a shooter's unit id (-2 = the core) or the wall cell a ram is chewing through.
    int sides = 3, target = -1;
    bool turret = false, elite = false;
    // Defense: each player carries their own ink; fallen players come back after respawn seconds.
    double ink = 0, maxInk = 0, respawn = 0;
    // Throwing teammates: a player may opt in to being thrown (throwable); a grabbed player rides in the
    // holder's tail (carried) until thrown or wriggling free; a thrown player lands as bait for bait seconds
    // (pinned, pulling nearby enemies, taking half damage) and cannot be grabbed again while guard runs.
    bool throwable = false, carried = false;
    double bait = 0, guard = 0;
    // A training target (hub and tutorial): an enemy that never moves or attacks and stands up again at home
    // a few seconds after it breaks. It never counts as part of a wave.
    bool dummy = false;
    Vec2 home{};
    // A fighter's seat (0-7) is their colour on the page; lure is the bait a monster is chasing.
    int seat = -1, lure = -1;
    PlayerStats stats{};
    Perks perks{};
    // Where a plaster stands a fallen player up again (Perks::standUp).
    Vec2 standAt{};
    // Boss laser: aimed along laserDir, fires when laserTime runs out, then rests for laserCooldown.
    double laserTime = 0, laserCooldown = 0;
    Vec2 laserDir{1, 0};
    Vec2 castTarget{};
    int weapon = 0, ammo = 10, kills = 0, pendingUpgrades = 0, level = 1;
    std::array<int, 20> skills{};
    std::array<int, 3> choices{{0, 4, 8}};
    Input input{};
    std::deque<Input> moveQueue;
    std::vector<Vec2> path, trail;
    size_t waypoint = 0;
    bool player() const
    {
        return human || bot;
    }
};
struct Projectile
{
    // squad is the shooter's squad head when fired: a bullet never shoves its own formation.
    int id = 0, owner = -1, squad = -1, team = 0, weapon = 0, pierce = 0, bounces = 0;
    Vec2 position{}, velocity{};
    double damage = 10, remaining = 2, radius = 5;
    std::vector<int> hit;
};
struct Crate
{
    int id = 0, weapon = 1;
    Vec2 position;
    double dropTime = 2, remaining = 30;
    bool active = true;
};
// A bullet that crosses a teammate deals no damage; it shoves them along its flight instead.
// The impulse grows with the shot's weight and decays like every other knockback (e^-5t).
inline double friendlyPush(double damage)
{
    return std::clamp(40 + damage * 3.5, 70., 420.);
}
constexpr double MaxFriendlyImpulse = 650;
// Presentation cue carried with an effect so clients can tell which skill or event produced it.
enum class EffectStyle : uint8_t
{
    None,
    Fragment,
    Combustion,
    KillBlast,
    Rocket,
    BossSlam,
    FrostNova,
    Chain,
    Voltage,
    Spread,
    Emergency,
    Shield,
    Launch,
    Recruit,
    SpawnAlly,
    SpawnEnemy,
    Telegraph,
    PickupWeapon,
    Upgrade,
    Shove,
    WallDraw,
    WallErase,
    WallBreak,
    Shatter,
    Merge,
    LaserTelegraph,
    Laser,
    CoreHit,
    InkPickup,
    WaveStart,
    WaveClear,
    Turret,
    Respawn,
    Denied,
    // Throwing teammates: a grab, a thrown player landing as bait, rolling out of it, wriggling out of a
    // grip, and ink poured from one player to another.
    Grab,
    Bait,
    Roll,
    Escape,
    InkPour,
    // The hub's target was hit by a throw; a tutorial step was completed (radius carries the step).
    Bullseye,
    StepDone
};
// Effect kind 8 is a point/direction event cue; it never deals damage.
constexpr int EventEffect = 8;
struct Effect
{
    Vec2 from, to;
    double radius = 0, remaining = .2;
    int kind = 0, team = 0;
    double pulse = 0;
    uint32_t id = 0;
    EffectStyle style = EffectStyle::None;
};
// Support skills only matter once their core skill is learned: never offer a dead pick.
inline int skillPrerequisite(int skill)
{
    switch (skill)
    {
    case 5:
    case 7:
        return 4;
    case 9:
    case 10:
    case 11:
        return 8;
    case 13:
        return 12;
    default:
        return -1;
    }
}
struct DamageNumber
{
    uint32_t id = 0;
    int victim = -1, source = -1, team = 0;
    Vec2 position{};
    double amount = 0, remaining = .9;
};
struct BattleConfig
{
    RuleSet rules = RuleSet::Survival;
    bool aiEnabled = true, enemiesEnabled = true, bossEnabled = true;
    double duration = 600, playerSpeed = 300, aiSpeed = 215;
    double recruitRange = 165, companionSpacing = 64, launchSpeed = 1550, launchDamage = 100;
    double airDropInterval = 40, recruitInterval = 5;
    int maxMonsters = 160, maxIdleCompanions = 35, tailLimit = 8;
    // Defense (initial values from the design doc; tuned in playtests).
    double coreHealth = 1000, inkMax = 300, inkPerCell = 10, buildSeconds = 30, firstBuildSeconds = 45;
    double respawnSeconds = 10;
    // Throwing teammates (design doc: 投掷真人 and 防止变成恶意; initial values, tuned in playtests).
    PrankLevel prank = PrankLevel::Friendly;
    double baitSeconds = 8, baitRadius = 600, baitDamage = .5, rollDelay = .5, throwGuard = 10, escapeGuard = 3;
    // Ink handover: how far a player can pour ink to a teammate, and the stickers bait can earn per wave.
    double pourRange = 2400;
    int baitStickerCap = 30;
    // The room's difficulty tier (难度档, 1-5; Meta.h): how far research ranks count, how tough enemies are.
    int tier = 1;
};
// Elite encounters arrive on a fixed schedule; the index survives a save so a reload never repeats one.
constexpr std::array<double, 3> BossSchedule{{120, 300, 540}};
struct WeaponStats
{
    double damage, interval, speed, range, reload;
    int ammo, pellets;
};
inline WeaponStats weaponStats(int kind)
{
    switch (kind)
    {
    case 1:
        return {18, .14, 1600, 1150, 1.6, 24, 1};
    case 2:
        return {13, .7, 1200, 650, 1.8, 6, 6};
    case 3:
        return {95, 1.25, 2400, 1900, 2.2, 5, 1};
    case 4:
        return {12, .07, 1550, 1000, 2.8, 60, 1};
    case 5:
        return {80, 1.2, 850, 1450, 2, 3, 1};
    default:
        return {24, .38, 1300, 900, 1.2, 10, 1};
    }
}
inline uint64_t randomStep(uint64_t& s)
{
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}
inline double random01(uint64_t& s)
{
    return (randomStep(s) >> 11) * (1.0 / 9007199254740992.0);
}
} // namespace gwgame
