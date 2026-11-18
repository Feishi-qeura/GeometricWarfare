#pragma once
#include "Types.h"
namespace gwgame
{
// The expedition (远征, design doc v2.1, 2026-11-16): the game's sticker run. Four stages of hand-drawn dungeon floors,
// each stage a pen biome: stage 1 is always the pencil sketchbook, stages 2-4 draw three of the other four pens. A floor
// is a handful of rooms on graph paper joined by corridors: a quiet start room, combat rooms that lock their doors until
// they are cleared, dead-end branches with a shop, a treasure room or an event, and a portal room (or, on a stage's last
// floor, the biome's boss). Every player carries a run pouch that starts empty: kills, chests and events fill it, shops
// spend it, and only a player who walks out alive keeps it (the last boss's exit door, or an early exit door in an event
// room on stages 2-4). A wipe settles nothing.

// ---- Pen biomes (五大纸笔生态群系) ----
enum class PenBiome : uint8_t
{
    Pencil,
    Watercolor,
    Fountain,
    Ballpoint,
    Brush,
    Count
};
constexpr int BiomeCount = static_cast<int>(PenBiome::Count), ExpeditionStages = 4;
// Floors in stage S: F(S) = S + 2 (3, 4, 5 and 6; 18 for a full run).
inline int stageFloors(int stage)
{
    return std::clamp(stage, 1, ExpeditionStages) + 2;
}
constexpr int ExpeditionFloors = 3 + 4 + 5 + 6;
// The rooms one floor of stage S may hold: [S + 2, S + 4].
inline int stageRoomsMin(int stage)
{
    return std::clamp(stage, 1, ExpeditionStages) + 2;
}
inline int stageRoomsMax(int stage)
{
    return std::clamp(stage, 1, ExpeditionStages) + 4;
}
// The design's table (3.1): a floor has `low` rooms, or one more with chance `higher`; early floors sit at the low end.
struct FloorRooms
{
    int low;
    double higher;
};
inline FloorRooms floorRooms(int stage, int floor)
{
    static const FloorRooms Table[ExpeditionStages][6] = {{{3, 0}, {3, .4}, {4, .35}, {4, 0}, {4, 0}, {4, 0}},
                                                          {{4, 0}, {4, .45}, {5, .3}, {5, .6}, {5, 0}, {5, 0}},
                                                          {{5, 0}, {5, .45}, {5, .7}, {6, .35}, {6, .65}, {6, 0}},
                                                          {{6, 0}, {6, .5}, {6, .75}, {7, .4}, {7, .6}, {7, .7}}};
    stage = std::clamp(stage, 1, ExpeditionStages);
    floor = std::clamp(floor, 1, stageFloors(stage));
    return Table[stage - 1][floor - 1];
}
inline int rollFloorRooms(int stage, int floor, uint64_t& rng)
{
    const FloorRooms rule = floorRooms(stage, floor);
    return rule.low + (rule.higher > 0 && random01(rng) < rule.higher ? 1 : 0);
}
// Stage 1 is the pencil; stages 2-4 take three of the other four pens in a random order, none twice.
inline std::array<PenBiome, ExpeditionStages> rollBiomes(uint64_t& rng)
{
    std::array<PenBiome, 4> others{{PenBiome::Watercolor, PenBiome::Fountain, PenBiome::Ballpoint, PenBiome::Brush}};
    for (int i = 3; i > 0; --i)
        std::swap(others[static_cast<size_t>(i)], others[static_cast<size_t>(randomStep(rng) % static_cast<uint64_t>(i + 1))]);
    return {{PenBiome::Pencil, others[0], others[1], others[2]}};
}
// Enemies grow tougher through the run: per stage and a little per floor (the tier multiplies on top).
inline double stageHealth(int stage, int floor)
{
    return 1 + .55 * (std::clamp(stage, 1, ExpeditionStages) - 1) + .07 * (std::max(1, floor) - 1);
}
inline double stageDamage(int stage, int floor)
{
    return 1 + .25 * (std::clamp(stage, 1, ExpeditionStages) - 1) + .03 * (std::max(1, floor) - 1);
}

// ---- Rooms ----
enum class RoomKind : uint8_t
{
    // Where the floor begins: no enemies, ever.
    Start,
    // Doors shut while the room's waves last.
    Combat,
    // A combat room with the portal to the next floor in its middle (it wakes when the room is cleared).
    Portal,
    // A stage's last room: the biome's boss behind a sealed door.
    Boss,
    // Dead-end branches: four goods on a shelf, a chest per player, or one of the event machines.
    Shop,
    Treasure,
    Event,
    Count
};
enum class RoomState : uint8_t
{
    // Not fought yet (or nothing to fight).
    Quiet,
    // Someone walked in: the doors shut when everyone is in, or when the grace runs out (stragglers are pulled in).
    Armed,
    // Doors shut, waves running.
    Locked,
    Cleared
};
// The thirteen event machines (design chapter 8, EV01-EV13).
enum class RoomEvent : uint8_t
{
    Scale,    // EV01 trade a weapon or a look for stickers
    Wishing,  // EV02 throw in 20 stickers for a wish
    Reforge,  // EV03 a pen nib for the gun in hand
    Vote,     // EV04 the party votes a prank on the floor
    Chained,  // EV05 hold E in the ring while an ambush comes: a double chest
    Wheel,    // EV06 spin the sticker wheel
    PenRack,  // EV07 borrow a special pen for revive drawings
    Arena,    // EV08 thirty paper monsters in thirty seconds
    Copier,   // EV09 copy a one-time item for 80 stickers
    Clinic,   // EV10 heal everyone, or trade health for ink
    Blindbox, // EV11 one blind draw per player
    Rubbing,  // EV12 the gun in hand becomes a pair
    Exit,     // EV13 the early exit door (stages 2-4)
    Count
};
constexpr int RoomEventCount = static_cast<int>(RoomEvent::Count);
// The floor's grid of room slots (5x5 at most); each slot is DungeonPitch cells and a room sits in its middle.
constexpr int DungeonPitch = 10, DungeonLattice = 5;
constexpr double DungeonCell = 300;
struct DungeonRoom
{
    RoomKind kind = RoomKind::Combat;
    RoomState state = RoomState::Quiet;
    // The lattice slot, the open interior in cells (x, y its top-left), the room it was reached from, and its
    // corridors (a bit per side: 1 east, 2 south, 4 west, 8 north).
    int slotX = 0, slotY = 0, x = 0, y = 0, w = 1, h = 1, parent = -1;
    uint8_t links = 0;
    // The corridor cells right outside the room's walls: they become solid while the room is locked.
    std::vector<int> doors;
    bool explored = false;
    // The fight: waves done and planned, and a clock (the armed grace, the pause between waves).
    int wave = 0, waves = 0;
    double clock = 0;
    // An event room's machine and its state (what each event counts: uses, spins, a timer).
    RoomEvent event = RoomEvent::Count;
    int eventState = 0;
    double eventClock = 0;
    bool contains(int cx, int cy) const
    {
        return cx >= x && cy >= y && cx < x + w && cy < y + h;
    }
    bool fights() const
    {
        return kind == RoomKind::Combat || kind == RoomKind::Portal || kind == RoomKind::Boss;
    }
};
// What one floor needs before it is laid out: its room count, whether it ends on the boss, and the dead-end rooms it
// wants in order of need (the shop first; whatever does not fit on the floor is left out from the back).
struct FloorPlan
{
    int stage = 1, floor = 1, rooms = 3;
    bool boss = false;
    std::vector<RoomKind> specials;
    // The pencil sketchbook's pillars are dashed pencil lines (drawn walls that shots and the eraser break) rather than rock.
    bool sketchPillars = false;
};
// A laid-out floor: its cells (Maze::fromGrid's codes, 0 open and 2 rock) and its rooms (room 0 is the start).
struct DungeonFloor
{
    int width = 0, height = 0;
    std::vector<int> cells;
    std::vector<DungeonRoom> rooms;
};
inline std::pair<int, int> roomSize(RoomKind kind, int stage, int floor, uint64_t& rng)
{
    switch (kind)
    {
    case RoomKind::Start:
        // The first floor's start room holds the newcomer's sticker vending machine.
        return stage == 1 && floor == 1 ? std::pair<int, int>{6, 5} : std::pair<int, int>{5, 5};
    case RoomKind::Portal:
        return {7, 7};
    case RoomKind::Boss:
        return {8, 8};
    case RoomKind::Shop:
    case RoomKind::Treasure:
        return {6, 5};
    case RoomKind::Event:
        return {6, 6};
    default:
    {
        static const std::pair<int, int> Sizes[4] = {{7, 6}, {8, 6}, {7, 7}, {8, 7}};
        return Sizes[randomStep(rng) % 4];
    }
    }
}
namespace detail
{
// Lattice steps east, south, west, north (matching DungeonRoom::links bits 1, 2, 4, 8).
constexpr int StepX[4] = {1, 0, -1, 0}, StepY[4] = {0, 1, 0, -1};
} // namespace detail
// Lays out a floor: the main line (start, combat rooms, then the portal or the boss) walks the lattice without
// crossing itself, and one or two dead-end branches hang off its combat rooms (shop, treasure, event; one branch
// may be two rooms long). 1-1 is always start, one combat room, the portal. Returns false if the walk got stuck
// (callers retry with the advanced rng).
inline bool layoutFloor(const FloorPlan& plan, uint64_t& rng, DungeonFloor& out)
{
    out = DungeonFloor{};
    const int total = std::max(3, plan.rooms);
    std::vector<RoomKind> specials = plan.specials;
    // The main line keeps at least start, one fight and the end; specials beyond that are dropped from the back.
    const int branchRooms = std::min(static_cast<int>(specials.size()), total - 3);
    while (static_cast<int>(specials.size()) > branchRooms)
        specials.pop_back();
    const int mainRooms = total - branchRooms;
    std::vector<int> grid(DungeonLattice * DungeonLattice, -1);
    std::vector<DungeonRoom> rooms;
    auto place = [&](int sx, int sy, RoomKind kind, int parent, int side)
    {
        DungeonRoom r;
        r.kind = kind;
        r.slotX = sx;
        r.slotY = sy;
        r.parent = parent;
        if (parent >= 0)
        {
            r.links = static_cast<uint8_t>(1u << ((side + 2) % 4));
            rooms[static_cast<size_t>(parent)].links = static_cast<uint8_t>(rooms[static_cast<size_t>(parent)].links | (1u << side));
        }
        grid[sy * DungeonLattice + sx] = static_cast<int>(rooms.size());
        rooms.push_back(r);
    };
    auto freeSide = [&](int sx, int sy, int side)
    {
        const int nx = sx + detail::StepX[side], ny = sy + detail::StepY[side];
        return nx >= 0 && ny >= 0 && nx < DungeonLattice && ny < DungeonLattice && grid[ny * DungeonLattice + nx] < 0;
    };
    // The main line: a random walk that never revisits a slot.
    const int startX = static_cast<int>(randomStep(rng) % DungeonLattice), startY = static_cast<int>(randomStep(rng) % DungeonLattice);
    place(startX, startY, RoomKind::Start, -1, 0);
    for (int i = 1; i < mainRooms; ++i)
    {
        const DungeonRoom& last = rooms.back();
        int options[4], count = 0;
        for (int side = 0; side < 4; ++side)
            if (freeSide(last.slotX, last.slotY, side))
            {
                // A step that leaves room for the rest of the line (at least one free neighbour after it) is preferred.
                const int nx = last.slotX + detail::StepX[side], ny = last.slotY + detail::StepY[side];
                int onward = 0;
                for (int s = 0; s < 4; ++s)
                {
                    const int ox = nx + detail::StepX[s], oy = ny + detail::StepY[s];
                    onward += ox >= 0 && oy >= 0 && ox < DungeonLattice && oy < DungeonLattice && grid[oy * DungeonLattice + ox] < 0;
                }
                if (onward > 0 || i == mainRooms - 1)
                    options[count++] = side;
            }
        if (!count)
            return false;
        const int side = options[randomStep(rng) % static_cast<uint64_t>(count)];
        const RoomKind kind = i == mainRooms - 1 ? (plan.boss ? RoomKind::Boss : RoomKind::Portal) : RoomKind::Combat;
        place(last.slotX + detail::StepX[side], last.slotY + detail::StepY[side], kind, static_cast<int>(rooms.size()) - 1, side);
    }
    // Branches hang off the fights in the middle of the line, a different one each when it can; the start room only
    // when no fight has a free side.
    const int branches = std::min(branchRooms, 2);
    std::vector<int> used;
    for (int b = 0; b < branches; ++b)
    {
        // The second branch takes the last special when there are three (a two-room branch: the shop is never deep).
        const int length = b == branches - 1 ? branchRooms - b : 1;
        std::vector<std::pair<int, int>> anchors;
        auto gather = [&](int from, int to, bool fresh)
        {
            for (int i = from; i < to; ++i)
            {
                if (fresh && std::find(used.begin(), used.end(), i) != used.end())
                    continue;
                for (int side = 0; side < 4; ++side)
                    if (freeSide(rooms[static_cast<size_t>(i)].slotX, rooms[static_cast<size_t>(i)].slotY, side))
                        anchors.push_back({i, side});
            }
        };
        gather(1, mainRooms - 1, true);
        if (anchors.empty())
            gather(1, mainRooms - 1, false);
        if (anchors.empty())
            gather(0, 1, false);
        if (anchors.empty())
            return false;
        const auto [anchor, side] = anchors[randomStep(rng) % anchors.size()];
        used.push_back(anchor);
        int parent = anchor, from = side;
        for (int k = 0; k < length; ++k)
        {
            const DungeonRoom& at = rooms[static_cast<size_t>(parent)];
            if (k > 0)
            {
                int options[4], count = 0;
                for (int s = 0; s < 4; ++s)
                    if (freeSide(at.slotX, at.slotY, s))
                        options[count++] = s;
                if (!count)
                    return false;
                from = options[randomStep(rng) % static_cast<uint64_t>(count)];
            }
            const RoomKind kind = specials[static_cast<size_t>(b + k)];
            place(at.slotX + detail::StepX[from], at.slotY + detail::StepY[from], kind, parent, from);
            parent = static_cast<int>(rooms.size()) - 1;
        }
    }
    // The grid: the slots' bounding box, every cell rock until rooms and corridors are carved out of it.
    int minX = DungeonLattice, minY = DungeonLattice, maxX = 0, maxY = 0;
    for (const auto& r : rooms)
    {
        minX = std::min(minX, r.slotX);
        minY = std::min(minY, r.slotY);
        maxX = std::max(maxX, r.slotX);
        maxY = std::max(maxY, r.slotY);
    }
    out.width = (maxX - minX + 1) * DungeonPitch + 2;
    out.height = (maxY - minY + 1) * DungeonPitch + 2;
    out.cells.assign(static_cast<size_t>(out.width * out.height), 2);
    for (auto& r : rooms)
    {
        r.slotX -= minX;
        r.slotY -= minY;
        const auto size = roomSize(r.kind, plan.stage, plan.floor, rng);
        r.w = size.first;
        r.h = size.second;
        r.x = 1 + r.slotX * DungeonPitch + (DungeonPitch - r.w) / 2;
        r.y = 1 + r.slotY * DungeonPitch + (DungeonPitch - r.h) / 2;
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x)
                out.cells[static_cast<size_t>(y * out.width + x)] = 0;
    }
    // Corridors run along the slots' middle row or column; the corridor cells next to each room are its doors.
    for (size_t i = 0; i < rooms.size(); ++i)
    {
        DungeonRoom& r = rooms[i];
        if (r.parent < 0)
            continue;
        DungeonRoom& p = rooms[static_cast<size_t>(r.parent)];
        const bool across = r.slotY == p.slotY;
        DungeonRoom& a = across ? (r.slotX < p.slotX ? r : p) : (r.slotY < p.slotY ? r : p);
        DungeonRoom& b = &a == &r ? p : r;
        if (across)
        {
            const int row = 1 + a.slotY * DungeonPitch + DungeonPitch / 2;
            for (int x = a.x + a.w; x < b.x; ++x)
                out.cells[static_cast<size_t>(row * out.width + x)] = 0;
            a.doors.push_back(row * out.width + a.x + a.w);
            b.doors.push_back(row * out.width + b.x - 1);
        }
        else
        {
            const int column = 1 + a.slotX * DungeonPitch + DungeonPitch / 2;
            for (int y = a.y + a.h; y < b.y; ++y)
                out.cells[static_cast<size_t>(y * out.width + column)] = 0;
            a.doors.push_back((a.y + a.h) * out.width + column);
            b.doors.push_back((b.y - 1) * out.width + column);
        }
    }
    // A fight gets a few pillars of rock to duck behind, never in line with a door and never in the boss's ring.
    for (auto& r : rooms)
    {
        if (!(r.kind == RoomKind::Combat || r.kind == RoomKind::Portal) || r.w < 7 || r.h < 6)
            continue;
        const int doorX = 1 + r.slotX * DungeonPitch + DungeonPitch / 2, doorY = 1 + r.slotY * DungeonPitch + DungeonPitch / 2;
        const int spots[4][2] = {{r.x + 2, r.y + 2}, {r.x + r.w - 3, r.y + 2}, {r.x + 2, r.y + r.h - 3}, {r.x + r.w - 3, r.y + r.h - 3}};
        for (const auto& s : spots)
        {
            if (s[0] == doorX || s[1] == doorY || random01(rng) < .45)
                continue;
            // The portal's middle stays clear.
            if (r.kind == RoomKind::Portal && std::abs(s[0] - (r.x + r.w / 2)) <= 1 && std::abs(s[1] - (r.y + r.h / 2)) <= 1)
                continue;
            out.cells[static_cast<size_t>(s[1] * out.width + s[0])] = plan.sketchPillars ? 1 : 2;
        }
    }
    out.rooms = std::move(rooms);
    return true;
}

// ---- Gear: the 24 items (design chapter 6) and the solo player's stand-in ----
enum class Gear : uint8_t
{
    // One-time use (left click with it in hand).
    Eraser,
    ReviveKit,
    StickyNote,
    InkCapsule,
    Correction,
    Crane,
    Stamp,
    TapeTrap,
    Dryer,
    Scissors,
    Bandage,
    GoldDust,
    // Tools: work while carried (copy paper, paperclips, refill), on a click with a cooldown (tape roll, sharpener),
    // or while held in hand (the ruler's shield).
    CarbonPaper,
    Paperclip,
    TapeRoll,
    Ruler,
    Refill,
    Sharpener,
    // Relics: work while they sit in any slot.
    InkBottle,
    KraftLiner,
    CuttingMat,
    Highlighter,
    Album,
    Phoenix,
    // A solo run's shop puts this where the revive kit would be: it stands its owner back up once.
    StandIn,
    Count
};
constexpr int GearCount = static_cast<int>(Gear::Count);
enum class GearUse : uint8_t
{
    Consumable,
    Passive,
    Active,
    Held,
    Relic
};
struct GearInfo
{
    int price;
    GearUse use;
    // Seconds between uses of an active tool.
    double cooldown;
};
inline const GearInfo& gearInfo(int gear)
{
    static const GearInfo Infos[GearCount] = {
        {95, GearUse::Consumable, 0},  {65, GearUse::Consumable, 0}, {30, GearUse::Consumable, 0}, {28, GearUse::Consumable, 0},
        {45, GearUse::Consumable, 0},  {35, GearUse::Consumable, 0}, {50, GearUse::Consumable, 0}, {25, GearUse::Consumable, 0},
        {40, GearUse::Consumable, 0},  {20, GearUse::Consumable, 0}, {30, GearUse::Consumable, 0}, {60, GearUse::Consumable, 0},
        {85, GearUse::Passive, 0},     {55, GearUse::Passive, 0},    {70, GearUse::Active, 18},    {75, GearUse::Held, 0},
        {110, GearUse::Passive, 0},    {80, GearUse::Active, 25},    {65, GearUse::Relic, 0},      {105, GearUse::Relic, 0},
        {85, GearUse::Relic, 0},       {70, GearUse::Relic, 0},      {160, GearUse::Relic, 0},     {180, GearUse::Relic, 0},
        {65, GearUse::Consumable, 0}};
    static const GearInfo None{0, GearUse::Consumable, 0};
    return gear >= 0 && gear < GearCount ? Infos[gear] : None;
}
inline bool gearOneTime(int gear)
{
    return gearInfo(gear).use == GearUse::Consumable;
}
// Item numbers (design chapter 6; 1 m = 100 units).
constexpr double EraserReach = 180, EraserArc = 110 * Pi / 180, NoteBlock = 60, CapsuleInk = 60, CorrectionRadius = 350,
                 CorrectionSeconds = 8, CorrectionHeal = 6, CraneRadius = 800, CraneSeconds = 6, StampRadius = 300, StampSeconds = 10,
                 StampBonus = .35, TrapLength = 500, TrapSeconds = 3.5, DryerReach = 600, DryerArc = 70 * Pi / 180, BandageShare = .5,
                 BandageSeconds = 3, GoldSeconds = 12, TapeShield = 60, RulerArc = 90 * Pi / 180, RefillAmmo = 1.35, RefillReload = .8,
                 SharpenerSeconds = 12, BottleInkCap = 180, BottleKillInk = 4, LinerCut = .18, LinerSeconds = 4, MatPush = 1.45,
                 MarkSeconds = 5, MarkBonus = .15, AlbumBonus = .25, PhoenixHealth = .3, PaperclipReach = 1000;
// Weapons on a shelf (stickers), by kind 0-5.
inline int weaponShelfPrice(int weapon)
{
    static const int Prices[6] = {35, 55, 60, 70, 85, 95};
    return weapon >= 0 && weapon < 6 ? Prices[weapon] : 60;
}

// ---- Quality (品质, 2026-11-16): green, blue, purple, orange and red, on the expedition only ----
// Every item and every quality gun has one; the six armoury guns have none. The catalog holds 32 pieces: ten green,
// eight blue, six purple, five orange and three red (the 24 items, the revive kit and the solo stand-in counted as one,
// and the eight quality guns below). Quality is how rare a piece is in chests, how dear on a shelf and what the shop's
// buyer pays for it; a chest's quality is the best piece inside.
enum class Quality : uint8_t
{
    None,
    Green,
    Blue,
    Purple,
    Orange,
    Red,
    Count
};
constexpr int QualityCount = static_cast<int>(Quality::Count), CatalogSize = 32;
inline Quality gearQuality(int gear)
{
    using Q = Quality;
    static const Q Grades[GearCount] = {Q::Orange, Q::Green,  Q::Green,  Q::Green,  Q::Blue,   Q::Green,  Q::Blue,   Q::Green,  Q::Blue,
                                        Q::Green,  Q::Green,  Q::Blue,   Q::Purple, Q::Green,  Q::Purple, Q::Blue,   Q::Orange, Q::Purple,
                                        Q::Purple, Q::Orange, Q::Purple, Q::Blue,   Q::Red,    Q::Red,    Q::Green};
    return gear >= 0 && gear < GearCount ? Grades[gear] : Q::None;
}
// Quality guns (名品武器): one of the six armoury guns drawn better, each with a trick of its own. A pair needs two of
// the same variant.
enum class GunTrick : uint8_t
{
    None,
    // Staples go through: power more enemies per shot.
    Pierce,
    // A thumbtack pins: power the chance a pellet holds its enemy still for StunPin seconds (bosses shrug it off).
    Pin,
    // Ice-blue ink: a hit slows for power seconds and soaks the enemy (wet: slower still).
    Chill,
    // Rubber bands: shots ricochet off walls power times.
    Bounce,
    // Neon lightning: a hit arcs to ChainCount more enemies in ChainReach for power of its damage.
    Chain,
    // Blotting paper (2026-11-17: a shield, not health): power of the damage dealt soaks into a shield on the shooter, up
    // to AbsorbCap of their max health.
    Absorb,
    // A wax seal: the shell's burst burns what it catches for power a second over BurnSeconds and reaches farther.
    Burn,
    // Crayons: rounds take turns being red (burn), blue (chill), yellow (chain) and green (pierce).
    Prism
};
// What a round is made of (2026-11-17), on top of its gun's trick: a spike (a real staple, a real thumbtack) sets the
// enemy it hits bleeding SpikeChance of the time, SpikeDps a second for SpikeSeconds (a fresh wound restarts the clock).
enum class BulletKind : uint8_t
{
    Plain,
    Spike
};
struct GunVariant
{
    // The armoury gun it is (weaponStats kind 0-5) and its quality.
    int base;
    Quality quality;
    // Multipliers on the base gun: damage, seconds between shots, rounds in a magazine, reload seconds; extra pellets.
    double damage, interval, ammo, reload;
    int pellets;
    GunTrick trick;
    double power;
    // Stickers on a shelf; and the colour its shots are drawn in (FGWShotNet::Tint: 0 plain).
    int price, tint;
    BulletKind bullet = BulletKind::Plain;
};
constexpr int GunVariantCount = 8, StaplerVariant = 0;
constexpr double StunPin = .4, ChainReach = 320, BurnSeconds = 3, BurnReach = 1.3, PrismBurn = 8, AbsorbCap = .2, SpikeChance = .25,
                 SpikeSeconds = 10, SpikeDps = 1;
constexpr int ChainCount = 3;
inline const GunVariant& gunVariant(int variant)
{
    using Q = Quality;
    using T = GunTrick;
    using B = BulletKind;
    static const GunVariant Variants[GunVariantCount] = {
        // Green: the stapler pistol (订书机手枪), the thumbtack shotgun (图钉霰弹枪); both fire spikes (real staples, real
        // tacks).
        {0, Q::Green, 1.1, .72, 1.4, .9, 0, T::Pierce, 1, 60, 1, B::Spike},
        {2, Q::Green, 1.05, .9, 1.34, .9, 2, T::Pin, .07, 65, 2, B::Spike},
        // Blue: the ice-ink rifle (冰蓝墨水步枪), the rubber-band machine gun (橡皮筋机枪).
        {1, Q::Blue, 1.2, 1, 1.25, .9, 0, T::Chill, 1.2, 90, 3},
        {4, Q::Blue, 1.15, .95, 1, .9, 0, T::Bounce, 2, 95, 4},
        // Purple: the neon lightning sniper (荧光闪电狙击枪).
        {3, Q::Purple, 1.3, .9, 1.2, .85, 0, T::Chain, .45, 130, 5},
        // Orange: the blotting-paper rifle (朱砂吸墨步枪: a tenth of its damage soaks into a shield), the wax-seal
        // launcher (火漆封缄火箭筒).
        {1, Q::Orange, 1.4, .95, 1.25, .85, 0, T::Absorb, .1, 170, 6},
        {5, Q::Orange, 1.45, .9, 1.34, .85, 0, T::Burn, 14, 180, 7},
        // Red: the rainbow crayon gatling (七彩蜡笔机枪).
        {4, Q::Red, 1.55, .9, 1.25, .8, 0, T::Prism, 1, 240, 8}};
    static const GunVariant None{0, Q::None, 1, 1, 1, 1, 0, T::None, 0, 0, 0};
    return variant >= 0 && variant < GunVariantCount ? Variants[variant] : None;
}
inline bool validVariant(int weapon, int variant)
{
    return variant < 0 || (variant < GunVariantCount && gunVariant(variant).base == weapon);
}
// A gun's numbers with its variant drawn in (the armoury gun's own for -1).
inline WeaponStats gunStats(int weapon, int variant)
{
    WeaponStats w = weaponStats(weapon);
    if (variant < 0 || variant >= GunVariantCount || gunVariant(variant).base != weapon)
        return w;
    const GunVariant& v = gunVariant(variant);
    w.damage *= v.damage;
    w.interval *= v.interval;
    w.reload *= v.reload;
    w.ammo = std::max(1, static_cast<int>(std::lround(w.ammo * v.ammo)));
    w.pellets += v.pellets;
    return w;
}
inline int gunMagazine(int weapon, int variant, bool dual)
{
    return gunStats(weapon, variant).ammo * (dual && weapon != FistWeapon ? 2 : 1);
}
inline Quality slotQuality(const HotSlot& s)
{
    if (s.kind == SlotKind::Item)
        return gearQuality(s.what);
    if (s.kind == SlotKind::Weapon && s.variant >= 0)
        return gunVariant(s.variant).quality;
    return Quality::None;
}
inline bool spikeRound(int variant)
{
    return variant >= 0 && variant < GunVariantCount && gunVariant(variant).bullet == BulletKind::Spike;
}
// Where a piece picked up goes (2026-11-17, shared by Battle::stow and the HUD's prompt): a gun into empty hands (a long
// gun needs both free; a pistol takes a free hand beside another pistol, which makes the dual wield), else the bag; a
// one-time item onto a stack of its own kind (the item slots first), else into a free item slot, else the bag.
enum class StowPlace : uint8_t
{
    None,
    Hand,
    Pair,
    Stack,
    Belt,
    Bag
};
struct StowPlan
{
    StowPlace place = StowPlace::None;
    // The kit index it lands in (0-4 the hotbar, 5 on the bag's slots).
    int slot = -1;
};
template <typename SlotAt>
inline StowPlan planStow(SlotAt slotAt, int bagOpen, bool weapon, int what, bool oneTime)
{
    const int size = HotbarSlots + std::clamp(bagOpen, 0, PackMax);
    StowPlan plan;
    auto firstFreeBag = [&]()
    {
        for (int i = HotbarSlots; i < size; ++i)
            if (slotAt(i).empty())
                return i;
        return -1;
    };
    if (weapon)
    {
        const HotSlot &left = slotAt(0), &right = slotAt(1);
        if (twoHanded(what))
        {
            if (left.empty() && right.empty())
                return {StowPlace::Hand, 0};
        }
        else
        {
            const bool longInHand = left.kind == SlotKind::Weapon && twoHanded(left.what);
            if (!longInHand && left.empty())
                return {right.kind == SlotKind::Weapon ? StowPlace::Pair : StowPlace::Hand, 0};
            if (!longInHand && right.empty())
                return {left.kind == SlotKind::Weapon ? StowPlace::Pair : StowPlace::Hand, 1};
        }
        if (const int bag = firstFreeBag(); bag >= 0)
            return {StowPlace::Bag, bag};
        return plan;
    }
    if (oneTime)
    {
        for (int i = HandSlots; i < size; ++i)
        {
            const HotSlot& s = slotAt(i);
            if (s.kind == SlotKind::Item && s.what == what && s.count < 3)
                return {StowPlace::Stack, i};
        }
    }
    for (int i = HandSlots; i < HotbarSlots; ++i)
        if (slotAt(i).empty())
            return {StowPlace::Belt, i};
    if (const int bag = firstFreeBag(); bag >= 0)
        return {StowPlace::Bag, bag};
    return plan;
}

// ---- Looks (design 4.3): six categories, one of each worn at a time; a found look lasts the run ----
enum class Wear : uint8_t
{
    Hat,
    Mask,
    Cape,
    Charm,
    Trail,
    Title,
    Count
};
static_assert(static_cast<int>(Wear::Count) == WearSlots, "a kit wears one look per category");
// How many looks each category has (index 0 is "none"): hats and titles are the wardrobe's (8 hats with the expedition's
// paper boat as 8, 11 titles); masks, capes, charms and trails exist only on the expedition.
inline int wearCount(int category)
{
    static const int Counts[WearSlots] = {9, 4, 4, 4, 4, 11};
    return category >= 0 && category < WearSlots ? Counts[category] : 0;
}
// A look on the ground is one number: category * 32 + index.
inline int lookId(int category, int index)
{
    return category * 32 + index;
}
inline int lookCategory(int look)
{
    return look >= 0 ? look / 32 : -1;
}
inline int lookIndex(int look)
{
    return look >= 0 ? look % 32 : -1;
}
// Each look's small perk (design: 微弱词条, kept small): hats +4% health, masks +3% damage, capes +3% speed, charms +10%
// pickup reach, trails +3% speed; titles are for show.
struct LookPerks
{
    double health = 1, damage = 1, speed = 1, reach = 1;
};
inline LookPerks lookPerks(const Kit& kit)
{
    LookPerks p;
    if (kit.wear[0] > 0)
        p.health *= 1.04;
    if (kit.wear[1] > 0)
        p.damage *= 1.03;
    if (kit.wear[2] > 0)
        p.speed *= 1.03;
    if (kit.wear[3] > 0)
        p.reach *= 1.1;
    if (kit.wear[4] > 0)
        p.speed *= 1.03;
    return p;
}

// ---- Enemies (the bestiary itself is Foes.h) ----
constexpr double BoltDamage = 12, BossBoltDamage = 16;
// Waves a fight runs, and how many come in each (before the party's crowd scale).
inline int roomWaves(RoomKind kind, int stage, int floor, uint64_t& rng)
{
    if (kind == RoomKind::Boss)
        return 1;
    if (kind == RoomKind::Portal)
        return 2;
    if (stage == 1 && floor == 1)
        return 1;
    return random01(rng) < .3 + .1 * stage ? 2 : 1;
}
inline int waveSize(int stage, int floor, int wave)
{
    return 3 + std::clamp(stage, 1, ExpeditionStages) + std::max(1, floor) / 2 + std::max(1, wave);
}
constexpr int MaxRoomEnemies = 45;

// ---- Loot on the floor ----
enum class LootKind : uint8_t
{
    // A sticker pile (amount stickers) and an ink blot (amount ink): both are picked up by walking over them.
    Stickers,
    Ink,
    // A weapon (what = kind, amount = rounds in it, variant = a quality gun), an item (what = Gear, amount = charges) and
    // a look (what = lookId; owner = the seat it belongs to): E picks these up.
    Weapon,
    Item,
    Look,
    // A pile of expedition coins (amount coins): walked over like stickers.
    Coins,
    // A heart drop (2026-11-17; amount: the percent of max health it gives back): walked over by a hurt player.
    Heart,
    Count
};
struct GroundLoot
{
    int id = 0;
    LootKind kind = LootKind::Stickers;
    int what = 0, amount = 0, owner = -1;
    // A quality gun's variant (-1 an armoury gun; only weapons).
    int variant = -1;
    Vec2 position{}, velocity{};
    double age = 0;
    // A toss out of an opened chest (2026-11-17): it flies straight for flight seconds on an arc hop units high (drawn by
    // clients), then lands, skids and settles; 0 for a drop that only slides.
    double hop = 0, flight = 0;
    bool airborne() const
    {
        return hop > 0 && age < flight;
    }
};
// Stickers, ink and coins are swept up by walking over them; the rest wait for E.
inline bool walkOverLoot(LootKind kind)
{
    return kind == LootKind::Stickers || kind == LootKind::Ink || kind == LootKind::Coins || kind == LootKind::Heart;
}
// More ways back to health and back to life (2026-11-17, the second playtest found the expedition short of both; Claude's
// numbers): a heart drop gives back HeartHeal percent of max health (HeartChance of ordinary foes drop one, every elite,
// HeartsPerBoss from a boss, every chest but half the room-clear ones); clearing a fight gives everyone in the room
// RoomClearHeal of their max health; a revive drawn without the revive kit costs KitlessReviveInk times the ink; and the
// fallen come back by themselves on the next floor with GhostReturnHealth of their health and the starting gun.
constexpr double HeartHeal = 10, HeartChance = .05, RoomClearHeal = .2, KitlessReviveInk = 2, GhostReturnHealth = .5;
constexpr int HeartsPerBoss = 3;
// A shelf's ware (shops, the vending machine, event offers): what it is, its price, whether it is gone.
struct Offer
{
    int room = -1;
    LootKind kind = LootKind::Item;
    int what = 0, price = 0;
    Vec2 position{};
    bool sold = false;
    // A machine's choice (the prank a ballot box stands for, the pen a rack holds), -1 none; a quality gun's variant on
    // a shelf (-1 an armoury gun).
    int extra = -1;
};
// An event room's machines stand on the shelf list too (kind ServiceLoot, what = Service): E at one works it, for its
// price in stickers.
constexpr LootKind ServiceLoot = LootKind::Count;
enum class Service : uint8_t
{
    Heal,     // EV10: everyone back to full health
    BloodInk, // EV10: 40% of your health for 80 ink
    Spin,     // EV06: the wheel for 30 or 60
    BigSpin,
    Wish,     // EV02: 20 stickers into the well (once each)
    Copy,     // EV09: copy the one-time item lying on the copier
    Rub,      // EV12: the gun in hand becomes a pair
    Sell,     // EV01: the gun in hand (or a look lying on the scale) for stickers
    Arena,    // EV08: start the thirty-second challenge
    Draw,     // EV11: one blind draw
    Reforge,  // EV03: the stage's nib for the gun in hand (35)
    Vote,     // EV04: a ballot for one of three pranks (the offer's extra: which)
    Borrow,   // EV07: borrow a pen for the stage's revive drawings (the offer's extra: which PenBiome)
    Trade,    // the shop's buyer (收购商): E opens the trade sheet; what is sold turns into coins (Battle::sellSlot)
    Count
};
// The floor pranks a ballot can choose (EV04): everyone doubles in health and girth, every shot bounces off the walls
// twice, or everyone hits and is hit half as hard again.
enum class FloorPrank : uint8_t
{
    Chubby,
    Bouncy,
    Glass,
    Count
};
constexpr int FloorPrankCount = static_cast<int>(FloorPrank::Count), VoteStickers = 35, ReforgePrice = 35, MaxReforges = 2;
constexpr double VoteSeconds = 12;
// What lies on the floor besides loot (ExpeditionState::Zone kinds). Below 10, the items' zones; 10-20 the foes'
// hazards (correction fluid and a second eraser on a skill boss wipe these); 21 and up the biomes' terrain, laid with
// the floor and kept until it ends.
enum class ZoneKind : uint8_t
{
    Correction = 0, // correction fluid: friends heal in it
    Tape = 1,       // double-sided tape: enemies stick (owner counts them)
    Fog = 10,       // graphite dust: whoever stands in it is hard to aim at
    Puddle = 11,    // water: slows players; a zapper's charge makes it bite (charge seconds)
    Paint = 12,     // a paint pool (what: its colour, 0 magenta or 1 cyan): slows; two colours together blind
    Ribbon = 13,    // a striker's neon ribbon (a band): slows
    Ink = 14,       // wet ink (a disc or a band; dps its bite): slows (what 1: a written wall, slows hard)
    Crumbs = 15,    // eraser crumbs: rub shields away
    Track = 16,     // a roller's track (a band): +20% pace along it
    Disk = 17,      // the compass's closing disk: bites hard inside (it shrinks over its life)
    Seal = 18,      // the judge's red seal: no items inside (the black seal follows)
    Spring = 19,    // the palette's spring: heals players
    Whirl = 20,     // the rainbow's whirlpool: pulls everyone toward its middle
    Clean = 21,     // a clean-water pool: washes off bleeding, smears and red circles
    Sac = 22,       // an ink sac: a shot bursts it (enemies around are hit, the party gets ink)
    Carbon = 23,    // carbon paper: an enemy shot fired on it comes again half a second later
    Cinnabar = 24,  // a cinnabar seal ring: stand in it to charge a critical streak (enemies come for you)
    DryBrush = 25,  // a dry-brush stroke (a band): +15% damage for players standing on it
    Thorns = 26,    // cross-hatched thorns round a pillar: bleeding for whoever rubs against it
    Dust = 27       // a strip of graphite dust (a band): hard to aim at whoever stands in it
};
inline bool hazardZone(int kind)
{
    return kind >= 10 && kind <= 20;
}
struct Chest
{
    int id = 0, room = -1;
    Vec2 position{};
    bool opened = false;
    // Where it came from (ChestGrade): a treasure room's, a boss's, the chained double, a cleared fight's, an elite's.
    int grade = 0;
    // Its quality (1 green .. 5 red): the best piece inside; rolled by the stage, the floor and where it came from.
    int quality = 1;
};
enum class ChestGrade : uint8_t
{
    Treasure,
    Boss,
    Chained,
    Clear,
    Elite
};
// A chest's quality: the stage's odds of green, blue, purple, orange and red, leaning toward the next stage's over the
// floors, then raised by where the chest came from (bump: an elite's and the chained double's +1, a boss's +2).
inline int rollQuality(int stage, int floor, int bump, uint64_t& rng)
{
    static const double Odds[ExpeditionStages + 1][5] = {
        {70, 24, 6, 0, 0}, {48, 34, 14, 4, 0}, {30, 36, 22, 10, 2}, {18, 32, 28, 16, 6}, {18, 32, 28, 16, 6}};
    stage = std::clamp(stage, 1, ExpeditionStages);
    const int floors = stageFloors(stage);
    const double lean = floors > 1 ? .5 * (std::clamp(floor, 1, floors) - 1) / (floors - 1) : 0;
    double odds[5], total = 0;
    for (int q = 0; q < 5; ++q)
    {
        odds[q] = Odds[stage - 1][q] + (Odds[stage][q] - Odds[stage - 1][q]) * lean;
        total += odds[q];
    }
    double pick = random01(rng) * total;
    int quality = 1;
    for (int q = 0; q < 5; ++q)
        if ((pick -= odds[q]) < 0)
        {
            quality = q + 1;
            break;
        }
    return std::clamp(quality + std::max(0, bump), 1, 5);
}
// A shelf's stand and an event machine's cabinet are solid (Maze::placeFixture); E reaches past them (ShelfReach).
constexpr double StandRadius = 38, MachineHalfWidth = 50, MachineHalfDepth = 37;
constexpr double LootReach = 70, PickupReach = 120, ShelfReach = 130, ChestReach = 140, PortalRadius = 170, ExitRadius = 150,
                 ExitHoldSeconds = 3, ChainedHoldSeconds = 6, ArmGrace = 5, BossGrace = 10, PortalWait = 8;
constexpr int MaxGroundLoot = 160;
// How high an opened chest tosses what is inside (2026-11-17): its pieces, and its piles of stickers, coins and ink.
constexpr double ChestHopPiece = 150, ChestHopPile = 95;
// The shop's buyer stands on a round counter (solid like a stand) and trades from a little farther than a shelf.
constexpr double TraderRadius = 40, TradeReach = 200;
// Coins: ten buy what one sticker does; the buyer pays 40% of a piece's shelf price for it (30% for an armoury gun), in
// coins (so a 50-sticker item fetches 200).
constexpr int CoinsPerSticker = 10;
constexpr double SellShare = .4, PlainGunShare = .3;
// What a piece costs on a shelf, in stickers: an armoury gun's own price, a quality gun's, an item's.
inline int piecePrice(LootKind kind, int what, int variant)
{
    if (kind == LootKind::Weapon)
        return variant >= 0 && validVariant(what, variant) ? gunVariant(variant).price : weaponShelfPrice(what);
    if (kind == LootKind::Item)
        return gearInfo(what).price;
    return 0;
}
inline Quality pieceQuality(LootKind kind, int what, int variant)
{
    if (kind == LootKind::Item)
        return gearQuality(what);
    if (kind == LootKind::Weapon && variant >= 0 && validVariant(what, variant))
        return gunVariant(variant).quality;
    return Quality::None;
}
// What the buyer pays for a slot (a pair is two guns, a stack every charge).
inline int sellCoins(const HotSlot& s)
{
    if (s.kind == SlotKind::Weapon)
    {
        const bool plain = s.variant < 0;
        const int each =
            static_cast<int>(std::lround(piecePrice(LootKind::Weapon, s.what, s.variant) * CoinsPerSticker * (plain ? PlainGunShare : SellShare)));
        return each * std::max(1, s.count);
    }
    if (s.kind == SlotKind::Item)
        return static_cast<int>(std::lround(piecePrice(LootKind::Item, s.what, -1) * CoinsPerSticker * SellShare)) *
               (gearOneTime(s.what) ? std::max(1, s.count) : 1);
    return 0;
}

// ---- Settlement ----
// Stickers scale with the room's tier (Meta.h tierInfo reward); a fractional part is rolled so small piles still add up.
inline int tierStickers(double base, double reward, uint64_t& rng)
{
    const double v = std::max(0., base * reward);
    const double whole = std::floor(v);
    return static_cast<int>(whole) + (random01(rng) < v - whole ? 1 : 0);
}
// The last boss's exit pays a clear bonus on top of each player's pouch (before the tier multiplier).
constexpr double ClearBonusBase = 150;

// ---- Revive drawings (design 5.2): a closed shape drawn on the emergency sheet ----
// Points are in the sheet's own unit square; the shape closes when the pen comes back within ReviveClose of where it
// started (or crosses its own line). r is the share of the sheet it encloses.
constexpr double ReviveClose = .053, ReviveMinShare = .06;
constexpr int ReviveMaxPoints = 256;
struct ReviveShape
{
    bool closed = false;
    double share = 0;
    // Shape bonus (0 none, 1 circle, 2 triangle, 3 rectangle) and how well it scored.
    int bonus = 0;
    double score = 0;
};
inline int reviveInk(double share)
{
    return static_cast<int>(std::lround(10 + 90 * std::clamp(share, ReviveMinShare, 1.)));
}
inline double reviveScale(double share)
{
    return .55 + 1.15 * std::clamp(share, ReviveMinShare, 1.);
}
inline double reviveHealth(double share)
{
    return .35 + 1.6 * std::clamp(share, ReviveMinShare, 1.);
}
inline double reviveSpeed(double share)
{
    return 1.42 - .72 * std::clamp(share, ReviveMinShare, 1.);
}
namespace detail
{
inline double cross(Vec2 o, Vec2 a, Vec2 b)
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}
inline double shoelace(const std::vector<Vec2>& p)
{
    double sum = 0;
    for (size_t i = 0; i < p.size(); ++i)
    {
        const Vec2 a = p[i], b = p[(i + 1) % p.size()];
        sum += a.x * b.y - b.x * a.y;
    }
    return std::abs(sum) * .5;
}
inline std::vector<Vec2> hull(std::vector<Vec2> p)
{
    std::sort(p.begin(), p.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    if (p.size() < 3)
        return p;
    std::vector<Vec2> h(p.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < p.size(); ++i)
    {
        while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0)
            --k;
        h[k++] = p[i];
    }
    for (size_t i = p.size() - 1, t = k + 1; i > 0; --i)
    {
        while (k >= t && cross(h[k - 2], h[k - 1], p[i - 1]) <= 0)
            --k;
        h[k++] = p[i - 1];
    }
    h.resize(k - 1);
    return h;
}
inline bool segmentsCross(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec2* at)
{
    const Vec2 r = b - a, s = d - c;
    const double den = r.x * s.y - r.y * s.x;
    if (std::abs(den) < 1e-12)
        return false;
    const double t = ((c.x - a.x) * s.y - (c.y - a.y) * s.x) / den, u = ((c.x - a.x) * r.y - (c.y - a.y) * r.x) / den;
    if (t <= 0 || t >= 1 || u <= 0 || u >= 1)
        return false;
    if (at)
        *at = a + r * t;
    return true;
}
} // namespace detail
// Measures a drawing: closes it (the ends near each other, or the first loop where the line crosses itself), its share of
// the sheet by the shoelace formula, and its likeness to a circle (4 pi A / P^2 over its hull), a triangle (how much of
// it the largest triangle on its hull covers) or a rectangle (the area over its smallest bounding rectangle); 82% or
// better earns that shape's bonus.
inline ReviveShape measureDrawing(std::vector<Vec2> points)
{
    ReviveShape out;
    points.erase(std::remove_if(points.begin(), points.end(), [](Vec2 p) { return !p.finite(); }), points.end());
    if (points.size() > static_cast<size_t>(ReviveMaxPoints))
        points.resize(ReviveMaxPoints);
    if (points.size() < 3)
        return out;
    std::vector<Vec2> loop;
    if ((points.back() - points.front()).length() <= ReviveClose)
        loop = points;
    else
    {
        // The first place the stroke crosses an earlier part of itself closes the loop between them.
        for (size_t j = 2; j + 1 < points.size() && loop.empty(); ++j)
            for (size_t i = 0; i + 1 < j && loop.empty(); ++i)
            {
                Vec2 at{};
                if (detail::segmentsCross(points[i], points[i + 1], points[j], points[j + 1], &at))
                {
                    loop.push_back(at);
                    for (size_t k = i + 1; k <= j; ++k)
                        loop.push_back(points[k]);
                }
            }
    }
    if (loop.size() < 3)
        return out;
    out.closed = true;
    const double area = detail::shoelace(loop);
    out.share = std::clamp(area, 0., 1.);
    const std::vector<Vec2> h = detail::hull(loop);
    if (h.size() < 3 || area <= 1e-9)
        return out;
    double perimeter = 0;
    for (size_t i = 0; i < h.size(); ++i)
        perimeter += (h[(i + 1) % h.size()] - h[i]).length();
    const double hullArea = detail::shoelace(h), solid = area / hullArea;
    const double circle = 4 * Pi * hullArea / std::max(1e-12, perimeter * perimeter) * std::min(1., solid / .9);
    double triangle = 0;
    for (size_t a = 0; a < h.size(); ++a)
        for (size_t b = a + 1; b < h.size(); ++b)
            for (size_t c = b + 1; c < h.size(); ++c)
                triangle = std::max(triangle, std::abs(detail::cross(h[a], h[b], h[c])) * .5);
    // The best triangle must cover the shape and the shape fill it.
    triangle = std::min(triangle / area, area / std::max(1e-12, triangle));
    double rectangle = 0;
    for (size_t i = 0; i < h.size(); ++i)
    {
        const Vec2 axis = (h[(i + 1) % h.size()] - h[i]).normalized(), normal{-axis.y, axis.x};
        double lo = 1e18, hi = -1e18, lo2 = 1e18, hi2 = -1e18;
        for (const Vec2 p : h)
        {
            lo = std::min(lo, p.dot(axis));
            hi = std::max(hi, p.dot(axis));
            lo2 = std::min(lo2, p.dot(normal));
            hi2 = std::max(hi2, p.dot(normal));
        }
        const double box = (hi - lo) * (hi2 - lo2);
        if (box > 1e-12)
            rectangle = std::max(rectangle, area / box);
    }
    // A triangle is never mistaken for anything else; a square scores a rectangle before a circle; a hollow shape (a star,
    // a crescent) is none of them.
    const double gate = solid >= .85 ? 1 : 0;
    const double scores[3] = {circle, std::min(1., triangle) * gate, std::min(1., rectangle) * gate};
    const int order[3] = {1, 2, 0};
    for (int k : order)
        if (scores[k] >= .82 && (out.bonus == 0))
        {
            out.bonus = k + 1;
            out.score = scores[k];
        }
    return out;
}

// ---- The run ----
struct ExpeditionState
{
    bool active = false;
    uint64_t seed = 1;
    // Where the party is: stage 1-4, floor 1..stageFloors(stage); floors cleared so far; the four stages' biomes.
    int stage = 1, floor = 1, floorsCleared = 0;
    std::array<PenBiome, ExpeditionStages> biomes{};
    // The floor's rooms (room 0 the start), the room the party is in, and the floors in a row without a shop.
    std::vector<DungeonRoom> rooms;
    int current = 0, shopless = 0;
    bool treasureThisStage = false;
    // Ground loot, shelves, chests.
    std::vector<GroundLoot> loot;
    std::vector<Offer> offers;
    std::vector<Chest> chests;
    int nextLoot = 1, nextChest = 1;
    // The portal (where, which room, awake) and the seconds the first player has waited in it. Whoever stands on the
    // portal (or the final door) the moment it opens has to step off and walk in again: nobody is whisked away before
    // they have picked up the room's loot.
    Vec2 portal{};
    int portalRoom = -1, bossId = -1, bossesDown = 0;
    bool portalOpen = false;
    double portalClock = 0;
    std::vector<int> standing;
    // Exit doors on this floor: an early one in an event room (EV13, hold E) and the last boss's final door.
    struct ExitDoor
    {
        Vec2 at{};
        int room = -1;
        bool final = false;
    };
    std::vector<ExitDoor> exits;
    // What lies on the floor (ZoneKind): the items' zones, the foes' hazards and the biome's terrain. A zone is a disc at
    // from (to == from) or a band from from to to; life is the seconds it was laid for (a closing disk shrinks over it),
    // what its colour or variant, dps its bite a second, charge a puddle's electric seconds left.
    struct Zone
    {
        int kind = 0, owner = -1, what = 0;
        Vec2 from{}, to{};
        double radius = 0, remaining = 0, life = 0, dps = 0, charge = 0;
    };
    std::vector<Zone> zones;
    static constexpr int MaxHazards = 48;
    // The floor's prank (EV04, FloorPrank; -1 none), and a ballot's clock once its first vote is in.
    int floorPrank = -1;
    double voteClock = 0;
    // Enemy shots fired on carbon paper come again: the copies wait here (seconds left, the shot).
    struct Echo
    {
        double delay = 0;
        Projectile shot;
    };
    std::vector<Echo> echoes;
    // Party size the floor was scaled for (living humans when the floor began), and the run's settled results.
    int party = 1;
    int extracted = 0;
    // The run so far (the result card): enemies and elites felled, chests opened, fights cleared; players fallen, and the
    // falls counted when the floor's boss came out (a boss felled with no fall since is flawless).
    int kills = 0, eliteKills = 0, chestsOpened = 0, fightsCleared = 0, falls = 0, fallsAtBoss = 0;
    PenBiome biome() const
    {
        return biomes[static_cast<size_t>(std::clamp(stage, 1, ExpeditionStages) - 1)];
    }
    bool bossFloor() const
    {
        return floor >= stageFloors(stage);
    }
    const DungeonRoom* room(int index) const
    {
        return index >= 0 && index < static_cast<int>(rooms.size()) ? &rooms[static_cast<size_t>(index)] : nullptr;
    }
};
} // namespace gwgame
