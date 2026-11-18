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
// the room everyone waits in between missions (stations, training targets, no enemies, never ends), Expedition
// is the dungeon run of four pen biomes (Expedition.h: rooms that lock, a sticker pouch, extraction).
enum class RuleSet : uint8_t
{
    Survival,
    Defense,
    Hub,
    Expedition
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
// the starting weapon, the pen, the hat, and the single-mission items in effect. Every rank counts, in any room
// (Battle::perk); the seventh is the expedition's backpack.
struct Perks
{
    // One rank per research attribute (Meta.h Attribute: nine since 2026-11-17).
    std::array<uint8_t, 9> ranks{};
    int weapon = 0, pen = 0, hat = 0, title = 0;
    // The weapons the player has unlocked (a bit per kind): the expedition's chests and shelves favour these.
    uint32_t weapons = 1;
    bool bottle = false, magnet = false, carbon = false, plaster = false;
    // The tower kit (the pouch's slot in the box): the player's next tower costs no ink.
    bool towerKit = false;
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
    // launches counts every throw (allies too); bullseyes the throws that came to rest on the hub's target; revives
    // the fallen teammates this player brought back with ink.
    int launches = 0, bullseyes = 0, revives = 0;
    double damage = 0, inkGiven = 0;
    // The expedition's feats (a bit each, Meta.h Feat) and counts: enemies rubbed out with an eraser, elites felled,
    // stickers dropped that a teammate picked up, the biome bosses felled and the revive shape bonuses drawn (a bit each).
    uint64_t feats = 0;
    int erased = 0, elites = 0, given = 0;
    uint8_t bosses = 0, shapes = 0;
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
    // Shift was pressed for this command (the dodge toward the cursor).
    bool dodge = false;
    // E is held (the expedition's hold-to-use doors and chests count it).
    bool use = false;
    // R is held (2026-11-17: ink mends the hurt players around the mender, the mender too).
    bool mend = false;
};
// The expedition's five-slot hotbar (design v2.1 4.1) and the backpack behind it (背包, 2026-11-16). Since 2026-11-17
// slots 1 and 2 are the two hands and hold guns only: a long gun (rifle, shotgun, sniper, machine gun, rocket launcher)
// takes both hands and sits in slot 1 with slot 2 left empty, a pistol takes one hand, and a pistol in each hand is the
// dual wield. Slots 3-5 hold items. The bag holds anything. A slot holds a weapon (what = weapon kind; variant = a
// quality gun, Expedition.h gunVariant, -1 for one of the six armoury guns), an item (what = Gear kind, count = charges
// left), or nothing.
enum class SlotKind : uint8_t
{
    Empty,
    Weapon,
    Item
};
struct HotSlot
{
    SlotKind kind = SlotKind::Empty;
    int what = -1, count = 0, ammo = 0;
    // An item's cooldown (tools that recharge), or a weapon's reload carried while another slot is in hand.
    double cooldown = 0;
    // A gun's reforged nib (EV03: the PenBiome whose trick its shots carry), -1 none.
    int nib = -1;
    // A quality gun's variant (Expedition.h gunVariant), -1 for a plain armoury gun (and for items).
    int variant = -1;
    bool empty() const
    {
        return kind == SlotKind::Empty;
    }
};
// The bag holds PackBase slots of its own behind the hotbar (ten in all by default), one more per research rank of the
// backpack (Meta.h Attribute::Pack), PackMax at most.
constexpr int HotbarSlots = 5, WearSlots = 6, PackBase = 5, PackMax = 10, KitSlots = HotbarSlots + PackMax;
// The hotbar's first two slots are the hands (guns only); the rest hold items.
constexpr int HandSlots = 2;
inline bool handSlot(int index)
{
    return index >= 0 && index < HandSlots;
}
inline bool beltSlot(int index)
{
    return index >= HandSlots && index < HotbarSlots;
}
// A long gun needs both hands: everything but the pistol (and bare hands).
inline bool twoHanded(int weapon)
{
    return weapon >= 1 && weapon <= 5;
}
// What a player carries through an expedition: the hotbar and the backpack, the run pouch of unsettled stickers, the
// run's coins, what they wear (one cosmetic per category), and the states the run's items and revive drawings leave on
// them.
struct Kit
{
    std::array<HotSlot, HotbarSlots> slots{};
    // The backpack's own slots (the first packSlots of them are open; Battle::packSlots).
    std::array<HotSlot, PackMax> pack{};
    int selected = 0;
    // Stickers picked up this run and not yet settled (the design's 随身贴纸袋), and the lucky album's pending bonus.
    int pouch = 0;
    double album = 0;
    // Expedition coins (远征币, 2026-11-16): the run's own money from kills, chests and selling to the shop's buyer. Ten
    // buy what one sticker buys (shops take coins first); they never leave the run, and a fall scatters them.
    int coins = 0;
    // One cosmetic per category (Cosmetic.h order: hat, mask, cape, charm, trail, title), -1 for none; wearOwner is the
    // seat of the player it belongs to (-1: the wearer's own), so a picked-up look goes home when the run ends.
    std::array<int, WearSlots> wear{{-1, -1, -1, -1, -1, -1}};
    std::array<int, WearSlots> wearOwner{{-1, -1, -1, -1, -1, -1}};
    // A ghost has fallen and dropped everything; extracted players have left with their pouch (banked stickers).
    bool ghost = false, extracted = false, finalExit = false;
    int banked = 0;
    // A revive drawing's results: the pen trait (PenBiome, -1 none), body scale, health and speed factors, and the
    // shape bonus (0 none, 1 circle, 2 triangle, 3 rectangle).
    int trait = -1, shape = 0;
    double scale = 1, hpScale = 1, speedScale = 1;
    // Seconds E has been held at the thing being held open (an exit door, a chained chest), and which.
    double hold = 0;
    int holdTarget = -1;
    // Item and trait states: a sticky note waiting to eat a hit, the tape's shield, a bandage's healing left and
    // its seconds, shots fired (copy paper, the ballpoint), the trait's clock, the pencil's save this floor and its
    // graphite shield, and whether the trigger was down last step (items fire once per press).
    bool note = false, pencilSaved = false, triggerHeld = false;
    double tape = 0, heal = 0, healTime = 0, traitClock = 0, graphite = 0, roomGuard = 0;
    // The blotting-paper rifle's shield (2026-11-17): soaked up from the damage it deals, up to a fifth of max health.
    double blot = 0;
    int shots = 0;
    // What the floor's foes and hazards leave on a player (seconds left): bleeding (and its bite a second), a red pen's
    // circle (+20% damage taken), the judge's red seal (no items), an ink smear over the eyes; and, worked out afresh each
    // step, the floor's pull on their feet (drag, boost), graphite fog hiding them, a dry-brush stroke under them; the
    // seconds stood in a cinnabar ring and the critical streak it charged.
    double bleed = 0, bleedDamage = 0, marked = 0, sealed = 0, smear = 0, drag = 1, boost = 1, cinnabar = 0, streak = 0;
    bool fogged = false, dryBrush = false;
    // Bleeding and the floor's bites pile up here and land in small bites (a damage number every half second, not sixty).
    double hurt = 0, hurtClock = 0;
    // A chilling shot's slow and a jolt's near-stop (seconds left; they work through drag, which the pace replicates).
    double chill = 0, jolt = 0;
    // Pens borrowed from a pen rack this stage (EV07, a bit per PenBiome), nibs reforged this run (EV03, two at most),
    // the prank voted for on this floor's ballot (EV04, -1 none), and the last borrowed pen a revive was drawn with.
    uint32_t borrowed = 0;
    int reforges = 0, vote = -1;
    // The pouch dropped at the last fall (a rich wipe is a feat of its own).
    int lost = 0;
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
    // The expedition's burning foes lose health in bites: the seconds since the last one.
    double burnTick = 0;
    // A spiked round's bleeding (staples, thumbtacks; 2026-11-17): seconds left, the seconds since the last point lost,
    // and who it is credited to.
    double bleedTime = 0, bleedTick = 0;
    int bleedOwner = -1;
    // Mending with ink (R, 2026-11-17): seconds R has been held with ink flowing (the flow speeds up with it), the
    // mender's own id while it flows this step (-1 not mending), and the slow it leaves on every body it touches.
    double mendTime = 0, mendSlow = 0;
    int mendOn = -1;
    // The dodge: untouchable while dashTime runs, then dashCooldown. batter: an elite's swing at a tower.
    double dashTime = 0, dashCooldown = 0, batter = 0;
    // A tower's kind (TowerKind: bolt, mortar or tesla).
    int towerKind = 0;
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
    // Where a plaster stands a fallen player up again (Perks::standUp), and on which level.
    Vec2 standAt{};
    bool standDeck = false;
    // The box (defense pages): deck is true for a unit up on the platform around the box. A unit on a ladder
    // climbs it (climb = the ladder's index, climbTime its progress 0..1, up or down). A runner that decided to
    // take a ladder walks to its foot first (ladderGoal); a monster up on the platform with nobody to chase hops
    // back down after deckIdle seconds.
    bool deck = false, climbUp = true;
    int climb = -1, ladderGoal = -1;
    double climbTime = 0, deckIdle = 0;
    // A tower stands on a drawn block; builder is the player it was built by (kills and ink go to them).
    int builder = -1;
    // The rocket launcher (weapon 5): seconds the trigger has been held while ready, and whether it is held.
    double rocketCharge = 0;
    bool rocketHeld = false;
    // Boss laser: aimed along laserDir, fires when laserTime runs out, then rests for laserCooldown.
    double laserTime = 0, laserCooldown = 0;
    Vec2 laserDir{1, 0};
    Vec2 castTarget{};
    int weapon = 0, ammo = 10, kills = 0, pendingUpgrades = 0, level = 1;
    // A pistol in each hand (the expedition's dual wield: twice the shots, a wider spread, a longer reload).
    bool dual = false;
    // The expedition: what a player carries; for an enemy, the room it belongs to, its breed (Expedition.h), a boss's
    // erasable parts (a bit each), the eraser's cuts it has taken, and the seconds left on what items did to it: stun
    // (no moving or attacking), a red stamp (+35% damage taken), gold dust (hits shake stickers loose), a highlighter
    // mark (+15% critical damage) and wet ink.
    Kit kit;
    // An item's own body on the field (Gear kind: the paper crane decoy, the sharpener's turret), -1 for everything else.
    int gadget = -1;
    int room = -1, breed = -1, erased = 0;
    uint8_t parts = 0;
    double stun = 0, stamp = 0, gold = 0, mark = 0, wet = 0;
    // An expedition foe's act (Foes.h FoeAct: what it is doing, which clients draw), the act's seconds left, a counter
    // (shots left in a volley, a dash's hits, a split's spawn), hits taken (thorns, parries, rain), a remembered point
    // or heading, and what it has soaked up or keeps (a sponge's puddles, a thief's stickers, a copy's mark).
    int act = 0, acts = 0, hits = 0;
    double actClock = 0, store = 0, hitClock = 0;
    Vec2 actAt{};
    // The seats a dash or a sweep has already struck (a bit each: once per player per dash).
    int struck = 0;
    std::array<int, 20> skills{};
    std::array<int, 3> choices{{0, 4, 8}};
    Input input{};
    // Whether this step's command is a real one from the player (a gap in the network stream is not a release
    // of the rocket's trigger).
    bool freshInput = true;
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
    // high: fired from above (a tower on its block) and never stopped by walls. A lobbed rocket shell (weapon
    // LobWeapon) flies its ground track over everything and bursts where it lands, when remaining runs out.
    bool high = false;
    // Fired from the platform and still over it: the platform never stops it there; once it drops into the box,
    // the platform's wall is as solid as rock to it.
    bool deck = false;
    // Fired from up on the platform (where it is drawn from, even when its muzzle is already over the box).
    bool fromDeck = false;
    // The expedition: what an enemy shot leaves on whoever it hits (Foes.h ShotEffect), and the reforged nib a player's
    // shot carries (PenBiome, -1 none).
    int effect = 0, nib = -1;
    // A quality gun's shot (Expedition.h): its variant (-1 none), trick (GunTrick) and the trick's colour this round (a
    // crayon round's 0-3 red, blue, yellow, green), ricochets it may still make, and the colour clients draw it in.
    int variant = -1, trick = 0, prism = -1, ricochet = 0, tint = 0;
    std::vector<int> hit;
};
// The rocket launcher lobs a shell like a thrown stone: holding the trigger charges it, releasing fires it, and the
// charge alone decides how far it flies (design: 火箭筒, 蓄力抛物线). The shell travels as its own weapon kind.
constexpr int RocketWeapon = 5, LobWeapon = 10;
// A mortar tower's shell: lobbed like the rocket's, but it bursts on enemies only.
constexpr int MortarWeapon = 11;
constexpr double MortarMinRange = 450, MortarBlast = 170;
// The dodge (Shift): a kick toward the cursor, untouchable for DashSeconds, then a cooldown.
constexpr double DashImpulse = 1500, DashSeconds = .22, DashCooldownSeconds = 2.4;
constexpr double RocketChargeSeconds = 1.1, RocketMinRange = 300, RocketMaxRange = 2000, RocketBlast = 210;
inline double rocketRange(double power)
{
    return RocketMinRange + std::clamp(power, 0., 1.) * (RocketMaxRange - RocketMinRange);
}
inline double rocketFlight(double power)
{
    return .55 + .45 * std::clamp(power, 0., 1.);
}
// Height of a lobbed shell above the straight line between its ends, t = 0..1 of the flight.
inline double rocketArc(double distance, double t)
{
    const double apex = 120 + .35 * distance;
    t = std::clamp(t, 0., 1.);
    return 4 * apex * t * (1 - t);
}
// Where a lobbed shell comes down: along the aim, as far as the charge says, kept on the page (the sizes are the
// maze's). The launcher's aiming preview draws the same spot while the trigger is held (2026-11-17).
inline Vec2 rocketLanding(Vec2 from, Vec2 aim, double power, double sizeX, double sizeY)
{
    Vec2 dir = aim.normalized();
    if (dir.lengthSquared() < .5)
        dir = {1, 0};
    const Vec2 target = from + dir * rocketRange(power);
    return {std::clamp(target.x, 1., std::max(1., sizeX - 1)), std::clamp(target.y, 1., std::max(1., sizeY - 1))};
}
// How hard a rocket hits the shooter's own team: nothing when trolling is off, some when friendly, a lot in chaos.
inline double rocketFriendlyShare(PrankLevel level)
{
    return level == PrankLevel::Off ? 0 : level == PrankLevel::Chaos ? .7 : .35;
}
// The platform stands this high above the floor of the box (presentation and aiming share it).
constexpr double DeckHeight = 240;
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
    StepDone,
    // The box: a ladder drawn or erased, a tower raised or grown (radius carries its sides), a fallen teammate
    // revived with ink, someone starting up or down a ladder, a monster hopping off the platform, a rocket fired.
    LadderDraw,
    LadderErase,
    TowerBuild,
    Revive,
    Climb,
    HopDown,
    RocketLob,
    Zap,
    Dash,
    // The expedition: a room's doors slam shut or swing open, a room is cleared, stickers picked up (radius carries the
    // count), a chest opened, something bought, the portal waking, a new floor, a player leaving through an exit
    // (radius: stickers banked), a fall that scatters a kit, a drawn revive (radius: the body scale), the eraser's
    // swipe, an item used (radius: its Gear kind), a weapon doubled into both hands, a boss part rubbed out, a loot
    // pickup (radius: what kind), and an event room's machine at work (radius: the event).
    DoorShut,
    DoorOpen,
    RoomClear,
    StickerPickup,
    ChestOpen,
    Purchase,
    PortalOpen,
    FloorStart,
    Extract,
    Scatter,
    DrawRevive,
    EraseSwipe,
    ItemUse,
    DualWield,
    PartErased,
    LootPickup,
    EventFire,
    // An expedition foe's warning (a circle at from when to is from, otherwise a band from from to to; radius its size,
    // pulse a colour code), one of its moments (a burst, a heal, a copy; radius its size, pulse what), and a boss
    // changing (the rainbow splitting and pulling itself back together, the sage turning dragon; radius the boss's foe).
    FoeCue,
    FoeBurst,
    BossPhase,
    // Coins picked up (radius: how many) and something sold to the shop's buyer (radius: the coins paid).
    CoinPickup,
    // A heart drop swept up (radius: the health it gave; 2026-11-17).
    HealPickup,
    Sale,
    // A spiked round set an enemy bleeding (2026-11-17), and a chest's toss landing on the floor (radius: its quality).
    Bleed,
    LootLand
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
    // A fallen player gets back up on the platform on their own after respawnSeconds; a teammate standing by their
    // outline can bring them back at once for reviveInk (design: 平台上消耗墨水复活).
    double respawnSeconds = 20, reviveInk = 50, reviveReach = 190;
    // The box: a tower on a block, its growth to five and six sides, a ladder down the box wall, and how long a
    // climb takes.
    double towerInk = 40, towerFiveInk = 50, towerSixInk = 70, ladderInk = 30, climbSeconds = .7;
    double mortarInk = 60, teslaInk = 55, wireInk = 25;
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
        // The rocket launcher: lobbed shells, 300-2000 by charge (rocketRange); speed is unused.
        return {140, 1.2, 850, RocketMaxRange, 2, 3, 1};
    case 12:
        // Bare hands (FistWeapon): a slap at arm's length that shoves more than it hurts; never reloads.
        return {5, .45, 0, 70, 0, 1, 1};
    default:
        return {24, .38, 1300, 900, 1.2, 10, 1};
    }
}
// Bare hands: what an expedition player holds with an empty slot (or straight after a naked revive).
constexpr int FistWeapon = 12;
constexpr double FistReach = 70, FistShove = 520;
// Dual wield (the expedition, design v2.1 4.2; since 2026-11-17 a pistol in each hand): both magazines, both guns fire
// on every pull (twice the damage per second), 3.5 degrees more spread and a reload only a quarter longer.
constexpr double DualSpread = 3.5 * Pi / 180, DualReload = 1.25;
// Mending with ink (R, 2026-11-17; second playtest the same day: a better ratio, and a circle shared by everyone in it):
// the flow starts at MendBaseRate ink a second and grows by MendRamp of that every second held, up to MendMaxRate, each
// ink a point of health (MendInkPerHealth ink a point). Every hurt player within MendReach of the mender (the mender
// too) takes an even share; the mender and everyone mended walk at MendSlow while it flows.
constexpr double MendInkPerHealth = 1, MendBaseRate = 15, MendRamp = 1, MendMaxRate = 60, MendReach = 380, MendSlow = .6,
                 MendLinger = .2;
inline int magazine(int weapon, bool dual)
{
    return weaponStats(weapon).ammo * (dual && weapon != FistWeapon ? 2 : 1);
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
