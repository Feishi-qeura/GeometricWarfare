#pragma once
namespace gwgame
{
// The expedition's rules (design v2.1; data and the floor generator in Expedition.h). The run walks four stages of
// floors; a floor's combat rooms lock until their waves are down, its dead ends hold a shop, chests or an event, and
// its portal (or its boss) leads on. Every player keeps a pouch of stickers that only an exit door turns into real ones.
namespace detail
{
// Refusal reasons the HUD reads off a Denied cue (radius): the hotbar and the bag are full, stickers (and coins) are
// short, ink is short, the drawing does not close, it has been used, it is not for now, no buyer is in reach.
constexpr int DenyFull = 20, DenyStickers = 21, DenyInk = 22, DenyDrawing = 24, DenyUsed = 25, DenyNotNow = 26, DenyNoBuyer = 27;
// The hands hold guns only, the belt (slots 3-5) items only, and a long gun taken into the hands needs a free bag slot
// for the second pistol it pushes out (2026-11-17).
constexpr int DenyHandsGuns = 28, DenyBeltItems = 29, DenyNoSpare = 30;
// Hold targets: the early exit door (chests use their own ids).
constexpr int HoldExit = -100;
} // namespace detail

inline bool Battle::startExpedition(uint64_t seed)
{
    config.rules = RuleSet::Expedition;
    rng = seed ? seed : 1;
    resetRun();
    defense = DefenseState{};
    hub = HubState{};
    expedition = ExpeditionState{};
    auto& x = expedition;
    x.active = true;
    x.seed = rng;
    uint64_t roll = rng ^ 0xB10E5ull;
    if (!roll)
        roll = 1;
    x.biomes = rollBiomes(roll);
    // No clock ends the run and nothing falls from the sky: the dungeon is the whole mission.
    config.duration = 1e12;
    nextBoss = nextWave = nextRecruit = 1e12;
    nextCrate = std::numeric_limits<double>::infinity();
    enterFloor();
    return true;
}
inline double Battle::shareScale() const
{
    const int party = std::clamp(expedition.party, 1, 8);
    return std::clamp(party / crowdScale(party).count, 1., 4.);
}
inline int Battle::livingParty() const
{
    int n = 0;
    for (const auto& u : units)
        n += u.human && u.alive && !u.kit.extracted;
    return n;
}
inline Vec2 Battle::roomCenter(const DungeonRoom& r) const
{
    return {(r.x + r.w * .5) * maze.cellSize, (r.y + r.h * .5) * maze.cellSize};
}
inline int Battle::roomAt(Vec2 p) const
{
    if (!expeditionMode())
        return -1;
    const int cell = maze.cell(p);
    if (cell < 0)
        return -1;
    const int cx = cell % maze.width, cy = cell / maze.width;
    for (size_t i = 0; i < expedition.rooms.size(); ++i)
        if (expedition.rooms[i].contains(cx, cy))
            return static_cast<int>(i);
    return -1;
}
inline Vec2 Battle::floorSpawn(int index) const
{
    const DungeonRoom* start = expedition.room(0);
    const Vec2 mid = start ? roomCenter(*start) : Vec2{maze.sizeX() * .5, maze.sizeY() * .5};
    for (int tries = 0; tries < 24; ++tries)
    {
        const int slot = std::max(0, index) + tries;
        const double angle = (slot % 8) * Pi / 4 + (slot / 8) * .39;
        const Vec2 p = mid + Vec2{std::cos(angle), std::sin(angle)} * (100. + 95 * ((slot / 8) % 3));
        if (maze.walkable(p, 34))
            return p;
    }
    return mid;
}
inline Vec2 Battle::roomSpot(const DungeonRoom& r, double away)
{
    Vec2 best = roomCenter(r);
    double bestDistance = -1;
    for (int tries = 0; tries < 28; ++tries)
    {
        const Vec2 p{(r.x + .6 + random01(rng) * (r.w - 1.2)) * maze.cellSize, (r.y + .6 + random01(rng) * (r.h - 1.2)) * maze.cellSize};
        if (!maze.walkable(p, 42))
            continue;
        double nearest = 1e18;
        for (const auto& u : units)
            if (u.alive && u.team == 1)
                nearest = std::min(nearest, (u.position - p).length());
        if (nearest >= away)
            return p;
        if (nearest > bestDistance)
        {
            bestDistance = nearest;
            best = p;
        }
    }
    return best;
}
// An item works only from the hotbar (2026-11-17, the user's call: passives have to be equipped); the bag only stores.
inline bool Battle::carries(const Unit& u, Gear gear) const
{
    for (const auto& s : u.kit.slots)
        if (s.kind == SlotKind::Item && s.what == static_cast<int>(gear))
            return true;
    return false;
}
inline int Battle::packSlots(const Unit& u) const
{
    return std::clamp(PackBase + effectiveRankOf(u, Attribute::Pack), PackBase, PackMax);
}
inline int Battle::kitSize(const Unit& u) const
{
    return HotbarSlots + packSlots(u);
}
inline HotSlot* Battle::kitSlot(Unit& u, int index)
{
    if (index >= 0 && index < HotbarSlots)
        return &u.kit.slots[static_cast<size_t>(index)];
    if (index >= HotbarSlots && index < kitSize(u))
        return &u.kit.pack[static_cast<size_t>(index - HotbarSlots)];
    return nullptr;
}
// The guns in hand while the hands are selected (2026-11-17): a long gun in slot 1, or a pistol in either hand or both.
inline int Battle::handGuns(const Unit& u, const HotSlot* guns[2]) const
{
    guns[0] = guns[1] = nullptr;
    if (!handSlot(u.kit.selected))
        return 0;
    int count = 0;
    for (int h = 0; h < HandSlots; ++h)
        if (const HotSlot& s = u.kit.slots[static_cast<size_t>(h)]; s.kind == SlotKind::Weapon)
            guns[count++] = &s;
    return count;
}
// The first gun in hand's variant (a long gun's own, or the left pistol's).
inline int Battle::handVariant(const Unit& u) const
{
    const HotSlot* guns[2];
    if (handGuns(u, guns) == 0)
        return -1;
    return guns[0]->variant >= 0 && validVariant(u.weapon, guns[0]->variant) ? guns[0]->variant : -1;
}
// What a player can spend: the pouch, and every ten coins as one sticker.
inline int Battle::purse(const Unit& u) const
{
    return u.kit.pouch + u.kit.coins / CoinsPerSticker;
}
// Pays a sticker price, coins first (they are worth nothing outside the run), the pouch for the rest.
inline void Battle::pay(Unit& u, int price)
{
    price = std::max(0, price);
    const int fromCoins = std::min(price, u.kit.coins / CoinsPerSticker);
    u.kit.coins -= fromCoins * CoinsPerSticker;
    u.kit.pouch = std::max(0, u.kit.pouch - (price - fromCoins));
}
inline double Battle::moveScale(const Unit& u) const
{
    double s = perk(u, Attribute::Speed);
    if (expeditionMode() && u.kind == UnitKind::Fighter)
    {
        s *= u.kit.speedScale * lookPerks(u.kit).speed * (u.kit.shape == 1 ? 1.05 : 1) * u.kit.drag * u.kit.boost;
        if (expedition.floorPrank == static_cast<int>(FloorPrank::Chubby))
            s *= .9;
    }
    // Ink flowing into a wound (R): the mender and the mended both slow down.
    if (u.mendSlow > 0)
        s *= MendSlow;
    return s;
}
// A magazine for each gun in hand (both pistols' together), the spring refill's extra on top.
inline int Battle::clipSize(const Unit& u) const
{
    if (!expeditionMode() || u.kind != UnitKind::Fighter)
        return weaponStats(u.weapon).ammo;
    const HotSlot* guns[2];
    const int count = handGuns(u, guns);
    if (count == 0 || u.weapon == FistWeapon)
        return gunMagazine(u.weapon, -1, false);
    int base = 0;
    for (int g = 0; g < count; ++g)
        base += gunMagazine(guns[g]->what, guns[g]->variant, false);
    return static_cast<int>(std::lround(base * (carries(u, Gear::Refill) ? RefillAmmo : 1)));
}
inline double Battle::reloadTime(const Unit& u) const
{
    double t = weaponStats(u.weapon).reload;
    if (expeditionMode() && u.kind == UnitKind::Fighter)
    {
        const HotSlot* guns[2];
        const int count = handGuns(u, guns);
        t = gunStats(u.weapon, -1).reload;
        for (int g = 0; g < count; ++g)
            t = g == 0 ? gunStats(guns[g]->what, guns[g]->variant).reload : std::max(t, gunStats(guns[g]->what, guns[g]->variant).reload);
        t *= (u.dual ? DualReload : 1) * (carries(u, Gear::Refill) ? RefillReload : 1);
        if (u.kit.trait == static_cast<int>(PenBiome::Ballpoint))
            t /= 1.15;
    }
    return t;
}
inline uint32_t Battle::revivePens(int id) const
{
    const Unit* u = find(id);
    if (!u || !expeditionMode())
        return 0;
    // The player's own pen (the wardrobe's fountain pen, marker or highlighter) brings its biome's nib; the stage's own
    // pen is on hand in its biome; pens borrowed from a pen rack last the stage.
    static const PenBiome Own[3] = {PenBiome::Fountain, PenBiome::Brush, PenBiome::Watercolor};
    uint32_t pens = 1u << static_cast<int>(Own[std::clamp(u->perks.pen, 0, 2)]);
    pens |= 1u << static_cast<int>(expedition.biome());
    pens |= u->kit.borrowed & ((1u << BiomeCount) - 1);
    return pens;
}

// ---- Floors ----
inline void Battle::enterFloor()
{
    auto& x = expedition;
    uint64_t roll = x.seed ^ (static_cast<uint64_t>(x.stage) * 0x9E3779B97F4A7C15ull) ^ (static_cast<uint64_t>(x.floor) * 0xC2B2AE3D27D4EB4Full);
    if (!roll)
        roll = 1;
    for (int i = 0; i < 4; ++i)
        randomStep(roll);
    FloorPlan plan;
    plan.stage = x.stage;
    plan.floor = x.floor;
    plan.boss = x.bossFloor();
    plan.rooms = rollFloorRooms(x.stage, x.floor, roll);
    plan.sketchPillars = x.biome() == PenBiome::Pencil;
    // The shop: every boss floor, and any floor after two without one; otherwise a little over half the time.
    const bool shop = plan.boss || x.shopless >= 2 || random01(roll) < .55;
    // A treasure room about every other floor; the floor before the boss makes sure the stage has had one.
    const bool treasureForced = x.floor == stageFloors(x.stage) - 1 && !x.treasureThisStage;
    const bool treasure = treasureForced || random01(roll) < .5;
    const bool eventRoom = random01(roll) < (x.stage >= 2 ? .6 : .45);
    if (shop)
        plan.specials.push_back(RoomKind::Shop);
    std::vector<RoomKind> rest;
    if (treasure)
        rest.push_back(RoomKind::Treasure);
    if (eventRoom)
        rest.push_back(RoomKind::Event);
    if (rest.size() == 2 && !treasureForced && random01(roll) < .5)
        std::swap(rest[0], rest[1]);
    plan.specials.insert(plan.specials.end(), rest.begin(), rest.end());
    // Four rooms or more always leave a branch to explore.
    if (plan.rooms >= 4 && plan.specials.empty())
        plan.specials.push_back(random01(roll) < .5 ? RoomKind::Treasure : RoomKind::Event);
    DungeonFloor f;
    bool laid = false;
    for (int tries = 0; tries < 64 && !laid; ++tries)
        laid = layoutFloor(plan, roll, f);
    if (!laid)
    {
        plan.rooms = 3;
        plan.specials.clear();
        for (int tries = 0; tries < 64 && !laid; ++tries)
            laid = layoutFloor(plan, roll, f);
    }
    // A new floor is a new page: its own seed and a version past every one before it (clients rebuild on either).
    const uint32_t version = maze.version;
    maze.fromGrid(f.width, f.height, DungeonCell, f.cells);
    maze.version = version + 1;
    maze.seed = (x.seed ^ (static_cast<uint64_t>(x.floorsCleared + 1) * 0xD6E8FEB86659FD93ull)) | 1;
    navigationVersion = maze.version;
    x.rooms = std::move(f.rooms);
    x.current = 0;
    x.loot.clear();
    x.offers.clear();
    x.chests.clear();
    x.zones.clear();
    x.exits.clear();
    x.standing.clear();
    x.echoes.clear();
    x.portalOpen = false;
    x.portalRoom = x.bossId = -1;
    x.portalClock = 0;
    x.floorPrank = -1;
    x.voteClock = 0;
    bool hasShop = false, hasTreasure = false;
    for (const auto& r : x.rooms)
    {
        hasShop |= r.kind == RoomKind::Shop;
        hasTreasure |= r.kind == RoomKind::Treasure;
    }
    x.shopless = hasShop ? 0 : x.shopless + 1;
    x.treasureThisStage |= hasTreasure;
    // The floor starts empty: last floor's enemies, gadgets and shots stay behind.
    units.erase(std::remove_if(units.begin(), units.end(), [](const Unit& u) { return u.kind != UnitKind::Fighter; }), units.end());
    pendingUnits.clear();
    projectiles.clear();
    x.party = 0;
    int index = 0;
    for (auto& u : units)
    {
        if (u.kind != UnitKind::Fighter || u.kit.extracted)
            continue;
        x.party += u.human;
        u.position = floorSpawn(index++);
        u.velocity = u.impulse = {};
        u.path.clear();
        u.trail.clear();
        u.followerOf = -1;
        u.tailOrder = 0;
        u.carried = false;
        u.bait = 0;
        u.climb = u.ladderGoal = -1;
        if (u.state == UnitState::Following || u.state == UnitState::Launched)
            u.state = UnitState::Free;
        u.kit.pencilSaved = false;
        u.kit.hold = 0;
        u.kit.holdTarget = -1;
        u.kit.vote = -1;
        u.kit.bleed = u.kit.marked = u.kit.sealed = u.kit.smear = u.kit.hurt = 0;
        // A ghost walks onto the new floor alive again (2026-11-17): a plain body, half its health and the starting gun.
        if (!u.alive && u.human && u.kit.ghost)
        {
            Kit& k = u.kit;
            k.ghost = false;
            k.trait = -1;
            k.shape = 0;
            k.scale = k.hpScale = k.speedScale = 1;
            k.selected = 0;
            k.slots[0] = {SlotKind::Weapon, u.perks.weapon >= 0 && u.perks.weapon <= 5 ? u.perks.weapon : 0, 1, 0, 0};
            k.slots[0].ammo = gunMagazine(k.slots[0].what, -1, false);
            u.alive = true;
            u.radius = 25;
            u.maxHp = healthCapacity(u);
            u.hp = u.maxHp * GhostReturnHealth;
            u.armor = 0;
            u.input = {};
            u.moveQueue.clear();
            u.dashTime = 1.5;
            syncHand(u);
            event(EffectStyle::Revive, u.position, u.position, u.team, u.seat, 1);
        }
        refreshCapacities(u);
        if (u.alive && u.kit.trait == static_cast<int>(PenBiome::Pencil))
            u.kit.graphite = u.maxHp * .25;
    }
    x.party = std::clamp(x.party, 1, 8);
    for (int i = 0; i < static_cast<int>(x.rooms.size()); ++i)
    {
        stockRoom(i, roll);
        layTerrain(i, roll);
    }
    if (!x.rooms.empty())
        x.rooms[0].explored = true;
    const Vec2 start = floorSpawn(0);
    event(EffectStyle::FloorStart, start, start, 1, x.stage * 10 + x.floor, 1.2);
}
inline bool Battle::expeditionJump(int stage, int floor)
{
    if (!expeditionMode() || finished || stage < 1 || stage > ExpeditionStages || floor < 1 || floor > stageFloors(stage))
        return false;
    expedition.stage = stage;
    expedition.floor = floor;
    enterFloor();
    return true;
}
inline int Battle::expeditionReinforce(int index)
{
    auto& x = expedition;
    if (!expeditionMode() || finished || index < 0 || index >= static_cast<int>(x.rooms.size()))
        return -1;
    int alive = 0;
    for (const auto& u : units)
        alive += enemy(u) && u.room == index;
    if (alive >= MaxRoomEnemies)
        return -1;
    const DungeonRoom& r = x.rooms[static_cast<size_t>(index)];
    const int id = spawnFoe(pickFoe(random01(rng) < .2), index, roomSpot(r, 450));
    flushUnits();
    return id;
}
inline void Battle::nextFloor()
{
    auto& x = expedition;
    ++x.floorsCleared;
    partyFeat(Feat::FloorCleared);
    if (x.floor < stageFloors(x.stage))
        ++x.floor;
    else
    {
        x.stage = std::min(ExpeditionStages, x.stage + 1);
        x.floor = 1;
        x.treasureThisStage = false;
        // The phoenix charm takes one charge again each stage; borrowed pens go back on their racks.
        for (auto& u : units)
            u.kit.borrowed = 0;
        for (auto& u : units)
        {
            for (auto& s : u.kit.slots)
                if (s.kind == SlotKind::Item && s.what == static_cast<int>(Gear::Phoenix))
                    s.count = 1;
            for (auto& s : u.kit.pack)
                if (s.kind == SlotKind::Item && s.what == static_cast<int>(Gear::Phoenix))
                    s.count = 1;
        }
    }
    enterFloor();
}
inline void Battle::stockRoom(int index, uint64_t& roll)
{
    auto& x = expedition;
    DungeonRoom& r = x.rooms[static_cast<size_t>(index)];
    const Vec2 mid = roomCenter(r);
    const double cell = maze.cellSize;
    auto ware = [&](LootKind kind, int what, int price, Vec2 at)
    {
        Offer o;
        o.room = index;
        o.kind = kind;
        o.what = what;
        o.price = price;
        o.position = at;
        x.offers.push_back(o);
        // The stand (or an event machine's cabinet) is solid: bodies walk round it, E still reaches it (ShelfReach).
        if (kind == ServiceLoot)
            maze.placeFixture(at, {MachineHalfWidth, MachineHalfDepth}, false);
        else
            maze.placeFixture(at, {StandRadius, StandRadius}, true);
    };
    auto machine = [&](Service s, int price, Vec2 at) { ware(ServiceLoot, static_cast<int>(s), price, at); };
    switch (r.kind)
    {
    case RoomKind::Start:
        r.state = RoomState::Cleared;
        // The newcomer's sticker vending machine on the very first floor (design 3.1: 1-1's start room).
        if (x.stage == 1 && x.floor == 1)
        {
            const int what = static_cast<int>(random01(roll) < .5 ? Gear::Bandage : Gear::InkCapsule);
            ware(LootKind::Item, what, gearInfo(what).price, mid + Vec2{0, -cell * 1.5});
        }
        break;
    case RoomKind::Combat:
    case RoomKind::Portal:
    case RoomKind::Boss:
        r.waves = roomWaves(r.kind, x.stage, x.floor, roll);
        break;
    case RoomKind::Shop:
    {
        r.state = RoomState::Cleared;
        // Four wares on the back wall: a revive kit almost always (the stand-in for a solo run) or a supply, a gun (a
        // quality gun more often the deeper the run goes), a one-time item (the eraser now and then), and a tool or a
        // relic.
        const bool solo = x.party <= 1;
        const int supply = random01(roll) < .85 ? static_cast<int>(solo ? Gear::StandIn : Gear::ReviveKit)
                                                : static_cast<int>(random01(roll) < .5 ? Gear::Bandage : Gear::InkCapsule);
        int gun = randomWeapon(), gunVariantId = -1;
        if (random01(roll) < .25 + .1 * x.stage)
            if (const int v = variantOf(rollQuality(x.stage, x.floor, 0, roll)); v >= 0)
            {
                gunVariantId = v;
                gun = gunVariant(v).base;
            }
        const int oneTime = random01(roll) < .12 ? static_cast<int>(Gear::Eraser) : randomGear(0);
        const int keeper = randomGear(random01(roll) < .5 ? 1 : 2);
        const int wares[4][2] = {{static_cast<int>(LootKind::Item), supply},
                                 {static_cast<int>(LootKind::Weapon), gun},
                                 {static_cast<int>(LootKind::Item), oneTime},
                                 {static_cast<int>(LootKind::Item), keeper}};
        for (int k = 0; k < 4; ++k)
        {
            const LootKind kind = static_cast<LootKind>(wares[k][0]);
            const int what = wares[k][1], variant = kind == LootKind::Weapon ? gunVariantId : -1;
            ware(kind, what, piecePrice(kind, what, variant), {(r.x + r.w * (k + 1) / 5.) * cell, (r.y + 1.2) * cell});
            x.offers.back().extra = variant;
        }
        // The buyer (收购商, 2026-11-16): a paper-bag shopkeeper at a round counter in the corner farthest from the door,
        // who turns guns and items into coins (E opens the trade sheet).
        Vec2 counter = mid;
        double farthest = -1;
        for (const Vec2 spot : {Vec2{(r.x + 1.1) * cell, (r.y + r.h - 1.1) * cell}, Vec2{(r.x + r.w - 1.1) * cell, (r.y + r.h - 1.1) * cell},
                                Vec2{(r.x + r.w * .5) * cell, (r.y + r.h - 1.05) * cell}})
        {
            double nearest = 1e18;
            for (int door : r.doors)
                nearest = std::min(nearest, (maze.center(door) - spot).length());
            if (nearest > farthest)
            {
                farthest = nearest;
                counter = spot;
            }
        }
        Offer trader;
        trader.room = index;
        trader.kind = ServiceLoot;
        trader.what = static_cast<int>(Service::Trade);
        trader.position = counter;
        x.offers.push_back(trader);
        maze.placeFixture(counter, {TraderRadius, TraderRadius}, true);
        break;
    }
    case RoomKind::Treasure:
    {
        r.state = RoomState::Cleared;
        // A chest for every player in the party (design 3.2: 宝箱数 = 联机人数), two rows past four.
        const int count = std::clamp(x.party, 1, 8), rows = count > 4 ? 2 : 1;
        for (int k = 0; k < count; ++k)
        {
            const int row = rows == 2 ? k / ((count + 1) / 2) : 0, inRow = rows == 2 ? (row ? count - (count + 1) / 2 : (count + 1) / 2) : count;
            const int col = rows == 2 && row ? k - (count + 1) / 2 : k;
            addChest(index, {(r.x + r.w * (col + 1.) / (inRow + 1)) * cell, (r.y + r.h * (rows == 2 ? (row ? .68 : .36) : .5)) * cell}, ChestGrade::Treasure,
                     rollQuality(x.stage, x.floor, 0, roll));
        }
        break;
    }
    case RoomKind::Event:
    {
        r.state = RoomState::Cleared;
        static const RoomEvent Ready[] = {RoomEvent::Clinic,  RoomEvent::Arena, RoomEvent::Chained, RoomEvent::Wheel,   RoomEvent::Blindbox,
                                          RoomEvent::Wishing, RoomEvent::Scale, RoomEvent::Copier,  RoomEvent::Rubbing, RoomEvent::Reforge,
                                          RoomEvent::Vote,    RoomEvent::PenRack};
        constexpr int ReadyCount = static_cast<int>(sizeof(Ready) / sizeof(Ready[0]));
        // The early exit door turns up in stages 2-4 (design EV13).
        r.event = x.stage >= 2 && random01(roll) < .4 ? RoomEvent::Exit : Ready[randomStep(roll) % ReadyCount];
        switch (r.event)
        {
        case RoomEvent::Exit:
            x.exits.push_back({mid, index, false});
            break;
        case RoomEvent::Clinic:
            machine(Service::Heal, 25, mid + Vec2{-cell * 1.1, 0});
            machine(Service::BloodInk, 0, mid + Vec2{cell * 1.1, 0});
            break;
        case RoomEvent::Arena:
            machine(Service::Arena, 0, mid);
            break;
        case RoomEvent::Chained:
            addChest(index, mid, ChestGrade::Chained, rollQuality(x.stage, x.floor, 1, roll));
            break;
        case RoomEvent::Wheel:
            machine(Service::Spin, 30, mid + Vec2{-cell * .9, 0});
            machine(Service::BigSpin, 60, mid + Vec2{cell * .9, 0});
            break;
        case RoomEvent::Blindbox:
            machine(Service::Draw, 0, mid);
            break;
        case RoomEvent::Wishing:
            machine(Service::Wish, 20, mid);
            break;
        case RoomEvent::Scale:
            machine(Service::Sell, 0, mid);
            break;
        case RoomEvent::Copier:
            machine(Service::Copy, 80, mid);
            break;
        case RoomEvent::Rubbing:
            machine(Service::Rub, 60, mid);
            break;
        case RoomEvent::Reforge:
            machine(Service::Reforge, ReforgePrice, mid);
            break;
        case RoomEvent::Vote:
            // EV04: a ballot box for each prank; with one player it is simply their pick.
            for (int k = 0; k < FloorPrankCount; ++k)
            {
                machine(Service::Vote, 0, mid + Vec2{(k - 1) * cell * 1.4, 0});
                x.offers.back().extra = k;
            }
            break;
        case RoomEvent::PenRack:
        {
            // EV07: two pens the stage does not give, free to borrow for the stage's revive drawings.
            std::vector<int> pens;
            for (int b = 0; b < BiomeCount; ++b)
                if (b != static_cast<int>(x.biome()))
                    pens.push_back(b);
            for (int i = static_cast<int>(pens.size()) - 1; i > 0; --i)
                std::swap(pens[static_cast<size_t>(i)], pens[static_cast<size_t>(randomStep(roll) % static_cast<uint64_t>(i + 1))]);
            for (int k = 0; k < 2; ++k)
            {
                machine(Service::Borrow, 0, mid + Vec2{(k ? 1 : -1) * cell * .9, 0});
                x.offers.back().extra = pens[static_cast<size_t>(k)];
            }
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

// ---- Rooms: arm, lock, waves, clear ----
inline void Battle::setDoors(DungeonRoom& r, bool shut)
{
    for (int door : r.doors)
        maze.setPost(door, shut);
}
inline void Battle::lockRoom(int index)
{
    auto& x = expedition;
    DungeonRoom& r = x.rooms[static_cast<size_t>(index)];
    r.state = RoomState::Locked;
    r.wave = 0;
    r.clock = .8;
    const Vec2 mid = roomCenter(r);
    // The party fights together: anyone still outside is pulled in through the nearest door (design 3.2).
    for (auto& u : units)
    {
        if (!u.human || !u.alive || u.kit.extracted || roomAt(u.position) == index || u.carried)
            continue;
        Vec2 spot = mid;
        double best = 1e18;
        for (int door : r.doors)
        {
            const Vec2 c = maze.center(door), inward = (mid - c).normalized();
            const Vec2 inside = c + inward * maze.cellSize;
            const double d = (u.position - c).lengthSquared();
            if (d < best)
            {
                best = d;
                spot = inside;
            }
        }
        for (int tries = 0; tries < 12 && !maze.walkable(spot, u.radius + 2); ++tries)
            spot = spot + (mid - spot).normalized() * 40;
        releaseFollowers(u.id);
        leaveTail(u);
        u.position = spot;
        u.velocity = u.impulse = {};
        u.state = UnitState::Free;
        u.path.clear();
        u.trail.clear();
        event(EffectStyle::Respawn, u.position, u.position, u.team, u.radius, .5);
    }
    // Loot lying in a doorway is swept into the room before the door comes down on it.
    for (auto& l : x.loot)
        for (int door : r.doors)
            if (maze.cell(l.position) == door)
                l.position = l.position + (mid - l.position).normalized() * maze.cellSize;
    setDoors(r, true);
    // Anyone the doors came down on (a body still over the threshold when the clock ran out) steps back into the room.
    for (auto& u : units)
        if (u.alive && !u.deck && u.climb < 0)
            u.position = maze.unstick(u.position, u.radius);
    // The kraft liner keeps its wearer clear of the floor's hazards for a moment after the doors shut.
    for (auto& u : units)
        if (u.human && u.alive && carries(u, Gear::KraftLiner))
            u.kit.roomGuard = LinerSeconds;
    event(EffectStyle::DoorShut, mid, mid, 2, r.kind == RoomKind::Boss ? 2 : 1, .8);
}
inline void Battle::spawnWave(int index)
{
    auto& x = expedition;
    DungeonRoom& r = x.rooms[static_cast<size_t>(index)];
    ++r.wave;
    r.clock = 1;
    if (r.kind == RoomKind::Boss)
    {
        x.bossId = spawnFoe(biomeBoss(x.biome()), index, roomCenter(r) + Vec2{0, -maze.cellSize});
        x.fallsAtBoss = x.falls;
        // Later stages bring the boss an escort.
        for (int k = 0; k < x.stage - 1; ++k)
            spawnFoe(biomeFoe(x.biome(), 0), index, roomSpot(r, 500));
        flushUnits();
        return;
    }
    const CrowdScale crowd = crowdScale(std::max(1, livingParty()));
    int alive = 0;
    for (const auto& u : units)
        alive += enemy(u) && u.room == index;
    const int count = std::clamp(static_cast<int>(std::lround(waveSize(x.stage, x.floor, r.wave) * crowd.count)), 1, std::max(1, MaxRoomEnemies - alive));
    const double eliteChance = crowd.elite * .6 + tierInfo(config.tier).elite + .01 * (x.stage - 1);
    for (int k = 0; k < count; ++k)
    {
        const bool elite = !(x.stage == 1 && x.floor == 1) && random01(rng) < eliteChance;
        const Foe foe = pickFoe(elite);
        const Vec2 at = roomSpot(r, 450);
        spawnFoe(foe, index, at);
        // Pencil stubs come in threes, huddled in a corner.
        if (foe == Foe::Stub)
            for (int s = 0; s < 2; ++s)
                spawnFoe(Foe::Stub, index, maze.move(at, Vec2{s ? 50. : -50., 40}, 20));
    }
    flushUnits();
}
inline void Battle::clearRoom(int index)
{
    auto& x = expedition;
    DungeonRoom& r = x.rooms[static_cast<size_t>(index)];
    r.state = RoomState::Cleared;
    ++x.fightsCleared;
    setDoors(r, false);
    const Vec2 mid = roomCenter(r);
    if (r.kind == RoomKind::Portal || (r.kind == RoomKind::Boss && x.stage < ExpeditionStages))
    {
        x.portal = mid;
        x.portalRoom = index;
        x.portalOpen = true;
        x.portalClock = 0;
        event(EffectStyle::PortalOpen, mid, mid, 1, PortalRadius, 1);
    }
    else if (r.kind == RoomKind::Boss)
    {
        // The last boss is down: the final exit door opens where it stood (design: 终局撤离草稿门).
        x.exits.push_back({mid + Vec2{0, -maze.cellSize * 1.2}, index, true});
        event(EffectStyle::PortalOpen, mid, mid, 1, ExitRadius, 1);
    }
    // The fight's reward (2026-11-16): a chest for each player of the party, stickers, coins, ink and a piece of gear in
    // each. A boss's chests come from bossDown. They stand in a ring well clear of the portal, and any chest or loot
    // already lying in the portal's way is moved out of it, so nobody walks through before they have opened theirs.
    if (r.kind != RoomKind::Boss)
    {
        const int count = std::clamp(x.party, 1, 8);
        const bool portalHere = x.portalOpen && x.portalRoom == index;
        const double ring = portalHere ? PortalRadius + 380 : count > 1 ? 150 + 26 * count : 0;
        for (int k = 0; k < count; ++k)
        {
            const double angle = -Pi / 2 + 2 * Pi * k / count + (portalHere ? Pi / 4 : 0);
            addChest(index, clearSpot(index, mid + Vec2{std::cos(angle), std::sin(angle)} * ring), ChestGrade::Clear,
                     rollQuality(x.stage, x.floor, 0, rng));
        }
    }
    if (x.portalOpen && x.portalRoom == index)
        for (auto& c : x.chests)
            if (!c.opened && (c.position - x.portal).length() < PortalRadius + 120)
                c.position = clearSpot(index, x.portal + (c.position - x.portal).normalized() * (PortalRadius + 260));
    // Whoever stands on the new portal or door has to step off and back on to use it.
    x.standing.clear();
    for (const auto& u : units)
    {
        if (!u.human || !u.alive || u.kit.extracted)
            continue;
        bool on = x.portalOpen && (u.position - x.portal).length() < PortalRadius;
        for (const auto& door : x.exits)
            on |= door.final && (u.position - door.at).length() < ExitRadius;
        if (on)
            x.standing.push_back(u.id);
    }
    // A breather (2026-11-17): everyone standing in the cleared room gets a share of their health back.
    for (auto& u : units)
        if (u.human && u.alive && !u.kit.extracted && roomAt(u.position) == index && u.hp < u.maxHp)
        {
            const double before = u.hp;
            heal(u, u.maxHp * RoomClearHeal);
            event(EffectStyle::HealPickup, u.position, u.position, u.team, u.hp - before, .6);
        }
    event(EffectStyle::RoomClear, mid, mid, 1, r.kind == RoomKind::Boss ? 2 : 1, 1);
    event(EffectStyle::DoorOpen, mid, mid, 1, 0, .6);
}
// A spot in the room near want where a chest can stand: walkable, off the portal and the exit doors, a chest's width
// from the others and from the doorways.
inline Vec2 Battle::clearSpot(int index, Vec2 want)
{
    const auto& x = expedition;
    const DungeonRoom* r = x.room(index);
    if (!r)
        return want;
    const double cell = maze.cellSize;
    auto fits = [&](Vec2 p)
    {
        if (!maze.walkable(p, 62) || roomAt(p) != index)
            return false;
        if (x.portalOpen && (p - x.portal).length() < PortalRadius + 160)
            return false;
        for (const auto& door : x.exits)
            if ((p - door.at).length() < ExitRadius + 140)
                return false;
        for (const auto& c : x.chests)
            if (!c.opened && (c.position - p).length() < 120)
                return false;
        for (int door : r->doors)
            if ((maze.center(door) - p).length() < cell * .95)
                return false;
        return true;
    };
    if (fits(want))
        return want;
    // Spiral out from the wish in small steps; failing that, anywhere in the room.
    for (int ringStep = 1; ringStep <= 14; ++ringStep)
        for (int k = 0; k < 12; ++k)
        {
            const double a = 2 * Pi * k / 12 + ringStep * .37;
            const Vec2 p = want + Vec2{std::cos(a), std::sin(a)} * (45. * ringStep);
            if (fits(p))
                return p;
        }
    for (int y = r->y; y < r->y + r->h; ++y)
        for (int xx = r->x; xx < r->x + r->w; ++xx)
        {
            const Vec2 p{(xx + .5) * cell, (y + .5) * cell};
            if (fits(p))
                return p;
        }
    return maze.unstick(want, 62);
}
inline Chest& Battle::addChest(int room, Vec2 at, ChestGrade grade, int quality)
{
    auto& x = expedition;
    Chest c;
    c.id = x.nextChest++;
    c.room = room;
    c.grade = static_cast<int>(grade);
    c.quality = std::clamp(quality, 1, 5);
    c.position = at;
    x.chests.push_back(c);
    return x.chests.back();
}
inline void Battle::updateRooms(double dt)
{
    auto& x = expedition;
    std::vector<int> inside(x.rooms.size(), 0), settled(x.rooms.size(), 0);
    int living = 0;
    for (const auto& u : units)
    {
        if (!u.human || !u.alive || u.kit.extracted)
            continue;
        ++living;
        const int room = roomAt(u.position);
        if (room >= 0)
        {
            DungeonRoom& r = x.rooms[static_cast<size_t>(room)];
            ++inside[static_cast<size_t>(room)];
            r.explored = true;
            x.current = room;
            // Over the threshold is not in yet: the doors wait until no body stands in a doorway (design 3.2's lock
            // when everyone is in), or for the clock.
            bool doorway = false;
            for (int door : r.doors)
            {
                const Vec2 lo{(door % maze.width) * maze.cellSize, (door / maze.width) * maze.cellSize}, hi = lo + Vec2{maze.cellSize, maze.cellSize};
                const Vec2 nearest{std::clamp(u.position.x, lo.x, hi.x), std::clamp(u.position.y, lo.y, hi.y)};
                doorway |= (u.position - nearest).lengthSquared() < (u.radius + 2) * (u.radius + 2);
            }
            settled[static_cast<size_t>(room)] += !doorway;
        }
    }
    for (int i = 0; i < static_cast<int>(x.rooms.size()); ++i)
    {
        DungeonRoom& r = x.rooms[static_cast<size_t>(i)];
        if (!r.fights() || r.state == RoomState::Cleared)
            continue;
        const int here = inside[static_cast<size_t>(i)];
        if (r.state == RoomState::Quiet && here > 0)
        {
            r.state = RoomState::Armed;
            r.clock = r.kind == RoomKind::Boss ? BossGrace : ArmGrace;
        }
        if (r.state == RoomState::Armed)
        {
            r.clock -= dt;
            if (here == 0)
                r.state = RoomState::Quiet;
            else if (settled[static_cast<size_t>(i)] >= living || r.clock <= 0)
                lockRoom(i);
            continue;
        }
        if (r.state != RoomState::Locked)
            continue;
        int alive = 0;
        for (const auto& u : units)
            alive += enemy(u) && u.room == i;
        for (const auto& u : pendingUnits)
            alive += u.kind == UnitKind::Monster && u.room == i;
        if (alive > 0)
            continue;
        if (r.wave < r.waves)
        {
            r.clock -= dt;
            if (r.clock <= 0)
                spawnWave(i);
        }
        else
            clearRoom(i);
    }
    // Anyone who was standing on the portal or the final door when it opened counts again once they have stepped off.
    x.standing.erase(std::remove_if(x.standing.begin(), x.standing.end(),
                                    [&](int id)
                                    {
                                        const Unit* u = find(id);
                                        if (!u || !u->alive)
                                            return true;
                                        if (x.portalOpen && (u->position - x.portal).length() < PortalRadius)
                                            return false;
                                        for (const auto& door : x.exits)
                                            if (door.final && (u->position - door.at).length() < ExitRadius)
                                                return false;
                                        return true;
                                    }),
                     x.standing.end());
    auto stale = [&](int id) { return std::find(x.standing.begin(), x.standing.end(), id) != x.standing.end(); };
    // The portal takes the party on when everyone alive stands in it, or once the first has waited long enough.
    if (x.portalOpen)
    {
        int standing = 0;
        for (const auto& u : units)
            standing += u.human && u.alive && !u.kit.extracted && !stale(u.id) && (u.position - x.portal).length() < PortalRadius;
        if (standing > 0 && standing >= living)
            return nextFloor();
        if (standing > 0 && (x.portalClock += dt) >= PortalWait)
            return nextFloor();
        if (standing == 0)
            x.portalClock = 0;
    }
    // Exits: the final door takes whoever walks into it; an early door wants E held for three seconds.
    for (const auto& door : x.exits)
        for (auto& u : units)
        {
            if (!u.human || !u.alive || u.kit.extracted)
                continue;
            const bool at = (u.position - door.at).length() < ExitRadius;
            if (door.final)
            {
                if (at && !stale(u.id))
                    extract(u, true);
                continue;
            }
            if (at && u.input.use)
            {
                if (u.kit.holdTarget != detail::HoldExit)
                {
                    u.kit.holdTarget = detail::HoldExit;
                    u.kit.hold = 0;
                }
                if ((u.kit.hold += dt) >= ExitHoldSeconds)
                    extract(u, false);
            }
            else if (u.kit.holdTarget == detail::HoldExit)
            {
                u.kit.holdTarget = -1;
                u.kit.hold = 0;
            }
        }
}
inline void Battle::extract(Unit& u, bool final)
{
    auto& x = expedition;
    stowHand(u);
    // The pouch, the lucky album's quarter on top, and the last boss's clear bonus through the final door.
    u.kit.banked = u.kit.pouch + static_cast<int>(std::lround(u.kit.album)) + (final ? expeditionClearBonus(config.tier) : 0);
    u.kit.pouch = 0;
    u.kit.album = 0;
    // Coins never leave the run.
    u.kit.coins = 0;
    u.kit.extracted = true;
    u.kit.finalExit = final;
    if (final)
    {
        u.stats.feats |= featBit(Feat::FinalExit);
        if (x.party >= 4)
            u.stats.feats |= featBit(Feat::TeamExit);
        if (config.tier >= 3)
            u.stats.feats |= featBit(Feat::Tier3Exit);
    }
    else if (u.kit.banked >= 500)
        u.stats.feats |= featBit(Feat::EarlyRich);
    if (u.kit.banked >= 1000)
        u.stats.feats |= featBit(Feat::BigBank);
    u.kit.hold = 0;
    u.kit.holdTarget = -1;
    ++x.extracted;
    releaseFollowers(u.id);
    leaveTail(u);
    cancelCharge(u.id);
    event(EffectStyle::Extract, u.position, u.position, u.team, u.kit.banked, 1.4);
    u.alive = false;
    u.hp = 0;
    u.input = {};
    u.moveQueue.clear();
    u.velocity = u.impulse = {};
}

// ---- Enemies ----
inline double Battle::expeditionPower(const Unit& u) const
{
    // Elites are drawn heavier already (Foes.h); an ink inspector nearby doubles what its crew deals.
    double power = stageDamage(expedition.stage, expedition.floor) * tierInfo(config.tier).enemyDamage * (u.elite ? 1.25 : 1);
    if (u.kind == UnitKind::Monster && u.breed != static_cast<int>(Foe::Inspector) && nearFoe(u, Foe::Inspector, 400))
        power *= 2;
    return power;
}
inline void Battle::expeditionKill(const Unit& dead, int attacker)
{
    if (!expeditionMode() || dead.kind != UnitKind::Monster || dead.gadget >= 0)
        return;
    auto& x = expedition;
    ++x.kills;
    x.eliteKills += dead.elite;
    foeDeath(dead, attacker);
    if (bossFoe(dead.breed))
        return bossDown(dead);
    // A boss's summons drop nothing.
    if (foeInfo(dead.breed).rank == FoeRank::Summon)
        return;
    const double reward = tierInfo(config.tier).reward, share = shareScale();
    // Ink (design 5.2: +5-10, an elite 35) and now and then a sticker (an elite pays a purse). A bigger party finds more
    // enemies and more stickers on each, so everyone's share stays about a solo run's (design 2.1: 350-700 on tier 1).
    scatterLoot(LootKind::Ink, 0, dead.elite ? 35 : 5 + static_cast<int>(randomStep(rng) % 6), dead.position, -1, 260);
    // An elite's purse now lies in its chest.
    const int stickers = !dead.elite && random01(rng) < .18 ? tierStickers(share, reward, rng) : 0;
    if (stickers > 0)
        scatterLoot(LootKind::Stickers, 0, stickers, dead.position, -1, 300);
    // Coins (2026-11-16): most enemies drop a few, an elite a handful, more each stage.
    const double coins = dead.elite ? 20 + 10 * x.stage + random01(rng) * 12 : random01(rng) < .7 ? 2 + 2 * x.stage + random01(rng) * 4 : 0;
    if (coins > 0)
        scatterLoot(LootKind::Coins, 0, tierStickers(coins * share, reward, rng), dead.position, -1, 280);
    // An elite leaves a chest of its own (one grade up on the room's odds) where it fell.
    if (dead.elite)
        addChest(dead.room, clearSpot(dead.room, dead.position), ChestGrade::Elite, rollQuality(x.stage, x.floor, 1, rng));
    // A heart now and then (2026-11-17): from every elite, from a few ordinary foes (on the hearts' own stream).
    if (uint64_t s = sideStream(static_cast<uint64_t>(dead.id)); dead.elite || random01(s) < HeartChance)
        scatterLoot(LootKind::Heart, 0, static_cast<int>(HeartHeal), dead.position, -1, 240, -1, 0, &s);
    // The glass ink bottle drinks a little from every kill its owner makes.
    if (Unit* k = creditFor(attacker); k && k->alive && k->human && carries(*k, Gear::InkBottle))
        k->ink = std::min(k->maxInk, k->ink + BottleKillInk);
}
inline void Battle::bossDown(const Unit& boss)
{
    auto& x = expedition;
    ++x.bossesDown;
    x.bossId = -1;
    // Everyone still in the run shares the boss (a bit per biome); a fight with no falls is flawless; the sage's dragon.
    const int biome = foeInfo(boss.breed).biome;
    for (auto& u : units)
        if (u.human && u.alive && !u.kit.extracted)
        {
            if (biome >= 0)
                u.stats.bosses = static_cast<uint8_t>(u.stats.bosses | (1u << biome));
            if (x.falls == x.fallsAtBoss)
                u.stats.feats |= featBit(Feat::Flawless);
            if (boss.breed == static_cast<int>(Foe::Sage) && (boss.parts & PartsSecondForm))
                u.stats.feats |= featBit(Feat::Dragon);
        }
    // The boss's summons crumble with it.
    for (auto& m : units)
        if (m.alive && m.kind == UnitKind::Monster && foeInfo(m.breed).rank == FoeRank::Summon)
            foeVanish(m);
    const double reward = tierInfo(config.tier).reward;
    // A heap of stickers, plenty of ink, an item, and a chest for each of the party (design 3.2).
    const int stickers = tierStickers((15 + 8 * x.stage) * shareScale(), reward, rng);
    for (int k = 0; k < 5; ++k)
        scatterLoot(LootKind::Stickers, 0, stickers / 5 + (k < stickers % 5 ? 1 : 0), boss.position, -1, 560);
    for (int k = 0; k < 4; ++k)
        scatterLoot(LootKind::Ink, 0, 20, boss.position, -1, 460);
    const int coins = tierStickers((120 + 60 * x.stage) * shareScale(), reward, rng);
    for (int k = 0; k < 4; ++k)
        scatterLoot(LootKind::Coins, 0, coins / 4 + (k < coins % 4 ? 1 : 0), boss.position, -1, 520);
    dropPiece(boss.position, rollQuality(x.stage, x.floor, 1, rng));
    for (int k = 0; k < HeartsPerBoss; ++k)
    {
        uint64_t s = sideStream(static_cast<uint64_t>(boss.id) * 8 + static_cast<uint64_t>(k));
        scatterLoot(LootKind::Heart, 0, static_cast<int>(HeartHeal), boss.position, -1, 480, -1, 0, &s);
    }
    const DungeonRoom* r = x.room(boss.room);
    const Vec2 mid = r ? roomCenter(*r) : boss.position;
    const int count = std::clamp(x.party, 1, 8);
    for (int k = 0; k < count; ++k)
    {
        const double t = count > 1 ? static_cast<double>(k) / (count - 1) : .5;
        Vec2 at = mid + Vec2{(t - .5) * maze.cellSize * 4.6, maze.cellSize * 2.1};
        if (!maze.walkable(at, 50))
            at = mid + Vec2{(t - .5) * maze.cellSize * 3, maze.cellSize};
        // A boss's chests are two grades up on the floor's odds.
        addChest(boss.room, clearSpot(boss.room, at), ChestGrade::Boss, rollQuality(x.stage, x.floor, 2, rng));
    }
}

// ---- Loot ----
inline uint64_t Battle::sideStream(uint64_t salt) const
{
    uint64_t s = (expedition.seed + 0x9E3779B97F4A7C15ull * (static_cast<uint64_t>(expedition.nextLoot) + 1)) ^ (salt * 0xD1B54A32D192ED03ull) ^ 0x4EA27ull;
    if (!s)
        s = 1;
    randomStep(s);
    randomStep(s);
    return s;
}
inline void Battle::scatterLoot(LootKind kind, int what, int amount, Vec2 at, int owner, double speed, int variant, double hop, uint64_t* stream)
{
    auto& x = expedition;
    uint64_t& r = stream ? *stream : rng;
    if (walkOverLoot(kind) && amount <= 0)
        return;
    // A crowded floor folds new stickers, ink and coins into the nearest pile of the same kind (a heart stays a heart).
    if (walkOverLoot(kind) && kind != LootKind::Heart && hop <= 0 && x.loot.size() >= static_cast<size_t>(MaxGroundLoot) - 20)
    {
        GroundLoot* best = nullptr;
        double nearest = 1e18;
        for (auto& l : x.loot)
            if (l.kind == kind && (l.position - at).lengthSquared() < nearest)
            {
                nearest = (l.position - at).lengthSquared();
                best = &l;
            }
        if (best)
        {
            best->amount += amount;
            return;
        }
    }
    GroundLoot l;
    l.id = x.nextLoot++;
    l.kind = kind;
    l.what = what;
    l.amount = amount;
    l.owner = owner;
    l.variant = kind == LootKind::Weapon && validVariant(what, variant) ? variant : -1;
    l.position = at;
    const double angle = random01(r) * 2 * Pi;
    l.velocity = Vec2{std::cos(angle), std::sin(angle)} * (speed * (.45 + .55 * random01(r)));
    if (hop > 0)
    {
        // A toss (an opened chest, 2026-11-17): up on an arc and down again 0.7-2 m away.
        l.hop = hop * (.85 + .3 * random01(r));
        l.flight = .5 + .25 * random01(r);
        l.velocity = Vec2{std::cos(angle), std::sin(angle)} * ((70 + 130 * random01(r)) / l.flight);
    }
    x.loot.push_back(l);
    if (x.loot.size() > static_cast<size_t>(MaxGroundLoot))
    {
        // Never stickers or coins: the oldest ink blot gives way first, then the oldest anything else.
        auto victim = std::find_if(x.loot.begin(), x.loot.end(), [](const GroundLoot& g) { return g.kind == LootKind::Ink; });
        if (victim == x.loot.end())
            victim = std::find_if(x.loot.begin(), x.loot.end(),
                                  [](const GroundLoot& g) { return g.kind != LootKind::Stickers && g.kind != LootKind::Coins; });
        if (victim != x.loot.end())
            x.loot.erase(victim);
    }
}
inline void Battle::updateLoot(double dt)
{
    auto& x = expedition;
    std::vector<const Unit*> magnets;
    bool brush = false;
    for (const auto& u : units)
        if (u.human && u.alive && !u.kit.extracted)
        {
            if (carries(u, Gear::Paperclip))
                magnets.push_back(&u);
            brush |= u.kit.trait == static_cast<int>(PenBiome::Brush);
        }
    // Nothing comes to rest on an open portal or the final door: whatever lies there slides to the ring's edge, so it
    // is never left behind by someone stepping in to fetch it.
    auto clearOf = [&](GroundLoot& l, Vec2 centre, double radius)
    {
        const Vec2 d = l.position - centre;
        if (d.length() >= radius)
            return;
        const Vec2 out = d.lengthSquared() > 1 ? d.normalized() : Vec2{0, 1};
        const Vec2 spot = centre + out * (radius + 30);
        l.position = maze.walkable(spot, 14) ? spot : maze.move(l.position, out * (radius + 30 - d.length()), 12);
        l.velocity = {};
    };
    for (auto& l : x.loot)
    {
        l.age += dt;
        // Tossed out of a chest: straight over the floor while it is up in the air (clients draw the arc), out of
        // anyone's reach; then it lands with a short skid.
        if (l.hop > 0)
        {
            if (l.age < l.flight)
            {
                l.position = maze.move(l.position, l.velocity * dt, 12);
                continue;
            }
            l.hop = 0;
            l.velocity = l.velocity * .3;
            event(EffectStyle::LootLand, l.position, l.position, 1, static_cast<double>(l.kind), .3);
        }
        if (l.velocity.lengthSquared() > 1)
        {
            l.position = maze.move(l.position, l.velocity * dt, 12);
            l.velocity = l.velocity * std::exp(-6 * dt);
        }
        if (x.portalOpen)
            clearOf(l, x.portal, PortalRadius + 40);
        for (const auto& door : x.exits)
            if (door.final)
                clearOf(l, door.at, ExitRadius + 40);
        if (!walkOverLoot(l.kind))
            continue;
        // A fresh drop settles before it can be swept up; stickers a player dropped wait for someone else a moment.
        if (l.age < .35)
            continue;
        for (const Unit* m : magnets)
            if ((m->position - l.position).length() < PaperclipReach)
            {
                l.velocity = (m->position - l.position).normalized() * 900;
                break;
            }
        Unit* taker = nullptr;
        double best = 1e18;
        for (auto& u : units)
        {
            if (!u.human || !u.alive || u.kit.extracted || (l.kind == LootKind::Ink && u.ink >= u.maxInk - 1e-6) ||
                (l.kind == LootKind::Heart && u.hp >= u.maxHp - 1e-6) ||
                (l.owner == u.id && l.age < 2))
                continue;
            const double reach = u.radius + LootReach * perk(u, Attribute::Reach) * lookPerks(u.kit).reach;
            const double d = (u.position - l.position).length();
            if (d < reach && d < best)
            {
                best = d;
                taker = &u;
            }
        }
        if (!taker)
            continue;
        if (l.kind == LootKind::Stickers)
        {
            if (l.owner >= 0 && l.owner != taker->id)
                if (Unit* giver = find(l.owner); giver && giver->human)
                    giver->stats.given += l.amount;
            taker->kit.pouch += l.amount;
            if (carries(*taker, Gear::Album))
                taker->kit.album += l.amount * AlbumBonus;
            event(EffectStyle::StickerPickup, l.position, taker->position, taker->team, l.amount, .45);
        }
        else if (l.kind == LootKind::Coins)
        {
            taker->kit.coins += l.amount;
            event(EffectStyle::CoinPickup, l.position, taker->position, taker->team, l.amount, .45);
        }
        else if (l.kind == LootKind::Heart)
        {
            const double before = taker->hp;
            heal(*taker, taker->maxHp * l.amount / 100.);
            event(EffectStyle::HealPickup, l.position, taker->position, taker->team, taker->hp - before, .45);
        }
        else
        {
            const double taken = std::min(l.amount * (brush ? 1.15 : 1), taker->maxInk - taker->ink);
            taker->ink += taken;
            event(EffectStyle::InkPickup, l.position, taker->position, taker->team, taken, .35);
        }
        l.amount = 0;
    }
    x.loot.erase(std::remove_if(x.loot.begin(), x.loot.end(), [](const GroundLoot& g) { return walkOverLoot(g.kind) ? g.amount <= 0 : g.amount < 0; }),
                 x.loot.end());
}
inline int Battle::randomWeapon()
{
    // The party's unlocked guns come up three times as often (design 2.3: a wider armoury, more pairs).
    uint32_t owned = 0;
    for (const auto& u : units)
        if (u.human)
            owned |= u.perks.weapons;
    double weights[6], total = 0;
    for (int w = 0; w < 6; ++w)
    {
        weights[w] = w == 0 ? 1 : ((owned >> w) & 1u) ? 3 : 1;
        total += weights[w];
    }
    double pick = random01(rng) * total;
    for (int w = 0; w < 6; ++w)
        if ((pick -= weights[w]) < 0)
            return w;
    return 1;
}
inline int Battle::randomGear(int family)
{
    if (family == 1)
        return static_cast<int>(Gear::CarbonPaper) + static_cast<int>(randomStep(rng) % 6);
    if (family == 2)
        return static_cast<int>(Gear::InkBottle) + static_cast<int>(randomStep(rng) % 6);
    // One-time items: the eraser and the revive kit are the rare ones (a solo run finds the stand-in instead of a kit).
    const double pick = random01(rng);
    if (pick < .06)
        return static_cast<int>(Gear::Eraser);
    if (pick < .18)
        return static_cast<int>(expedition.party <= 1 ? Gear::StandIn : Gear::ReviveKit);
    return static_cast<int>(Gear::StickyNote) + static_cast<int>(randomStep(rng) % 10);
}
// A quality gun of this quality (the nearest lower one when there is none), -1 when there is none at all.
inline int Battle::variantOf(int quality)
{
    for (int q = std::clamp(quality, 1, 5); q >= 1; --q)
    {
        int options[GunVariantCount], count = 0;
        for (int v = 0; v < GunVariantCount; ++v)
            if (static_cast<int>(gunVariant(v).quality) == q)
                options[count++] = v;
        if (count)
            return options[randomStep(rng) % static_cast<uint64_t>(count)];
    }
    return -1;
}
// One piece of the catalog at this quality: any of its items or quality guns (a gun a little more often). A solo run
// finds the stand-in where the revive kit would be.
inline void Battle::dropPiece(Vec2 at, int quality, double hop)
{
    const int q = std::clamp(quality, 1, 5);
    struct Choice
    {
        LootKind kind;
        int what, variant;
        double weight;
    };
    std::vector<Choice> pool;
    for (int g = 0; g < GearCount; ++g)
        if (g != static_cast<int>(Gear::StandIn) && static_cast<int>(gearQuality(g)) == q)
            pool.push_back({LootKind::Item, g == static_cast<int>(Gear::ReviveKit) && expedition.party <= 1 ? static_cast<int>(Gear::StandIn) : g, -1, 1});
    for (int v = 0; v < GunVariantCount; ++v)
        if (static_cast<int>(gunVariant(v).quality) == q)
            pool.push_back({LootKind::Weapon, gunVariant(v).base, v, 1.5});
    double total = 0;
    for (const auto& c : pool)
        total += c.weight;
    double pick = random01(rng) * total;
    for (const auto& c : pool)
        if ((pick -= c.weight) < 0 || &c == &pool.back())
        {
            scatterLoot(c.kind, c.what, c.kind == LootKind::Weapon ? gunMagazine(c.what, c.variant, false) : 1, at, -1, 320, c.variant, hop);
            return;
        }
}
inline int Battle::randomLook()
{
    const int category = static_cast<int>(randomStep(rng) % 5);
    return lookId(category, 1 + static_cast<int>(randomStep(rng) % static_cast<uint64_t>(wearCount(category) - 1)));
}
inline void Battle::chestLoot(Vec2 at, int grade, int quality)
{
    const double reward = tierInfo(config.tier).reward;
    const int stage = expedition.stage, q = std::clamp(quality, 1, 5);
    const ChestGrade kind = static_cast<ChestGrade>(grade);
    // How rich the chest is for where it came from: a fight's 1, an elite's a little more, a treasure room's, a boss's
    // and the chained double's most. Stickers stay about what fights paid before chests (a solo tier-1 run still banks
    // some 900); the chest's worth is in its coins and its gear, which only matter inside the run.
    const double rich = kind == ChestGrade::Boss ? 3 : kind == ChestGrade::Chained ? 3.5 : kind == ChestGrade::Treasure ? 2.5
                        : kind == ChestGrade::Elite                                  ? 1.2
                                                                                     : 1;
    const double purse = kind == ChestGrade::Boss ? 2 : kind == ChestGrade::Chained ? 2.2 : kind == ChestGrade::Treasure ? 1.8
                         : kind == ChestGrade::Elite                                  ? 1.2
                                                                                      : 1;
    // Everything is tossed out of the box on an arc (2026-11-17): the pieces highest, the piles a little lower.
    scatterLoot(LootKind::Stickers, 0, tierStickers((.7 + .25 * stage + .5 * q) * rich, reward, rng), at, -1, 300, -1, ChestHopPile);
    scatterLoot(LootKind::Coins, 0, tierStickers((20 + 22 * q) * (1 + .35 * (stage - 1)) * purse * (.8 + .4 * random01(rng)), reward, rng), at, -1, 300, -1,
                ChestHopPile);
    scatterLoot(LootKind::Ink, 0, 10 + 5 * q, at, -1, 260, -1, ChestHopPile);
    if (uint64_t s = sideStream(0xC4E57ull + static_cast<uint64_t>(std::llround(at.x)) * 131 + static_cast<uint64_t>(std::llround(at.y)));
        kind != ChestGrade::Clear || random01(s) < .5)
        scatterLoot(LootKind::Heart, 0, static_cast<int>(HeartHeal), at, -1, 260, -1, ChestHopPile, &s);
    // The piece the chest is graded by; a boss's and the chained double's hold a second one grade down, a treasure
    // room's half the time; and any chest may hold an armoury gun or a look on top.
    dropPiece(at, q, ChestHopPiece);
    if (kind == ChestGrade::Boss || kind == ChestGrade::Chained || (kind == ChestGrade::Treasure && random01(rng) < .5))
        dropPiece(at, std::max(1, q - 1), ChestHopPiece);
    const double extra = random01(rng);
    if (extra < .12)
    {
        const int w = randomWeapon();
        scatterLoot(LootKind::Weapon, w, magazine(w, false), at, -1, 320, -1, ChestHopPiece);
    }
    else if (extra < .24)
        scatterLoot(LootKind::Look, randomLook(), 1, at, -1, 320, -1, ChestHopPiece);
}
inline void Battle::openChest(Chest& chest, Unit& opener)
{
    chest.opened = true;
    ++expedition.chestsOpened;
    if (chest.grade == 2)
        opener.stats.feats |= featBit(Feat::Chained);
    chestLoot(chest.position, chest.grade, chest.quality);
    event(EffectStyle::ChestOpen, chest.position, opener.position, opener.team, chest.grade, .9);
}

// ---- The hotbar ----
// Puts the live magazine and reload back on the guns in hand (two pistols split what is left, the left one first).
inline void Battle::stowHand(Unit& u)
{
    if (!handSlot(u.kit.selected) || u.weapon == FistWeapon)
        return;
    HotSlot* guns[2] = {nullptr, nullptr};
    int count = 0;
    for (int h = 0; h < HandSlots; ++h)
        if (HotSlot& s = u.kit.slots[static_cast<size_t>(h)]; s.kind == SlotKind::Weapon && s.what == u.weapon)
            guns[count++] = &s;
    if (count == 0)
        return;
    const int ammo = std::max(0, u.ammo);
    guns[0]->ammo = count == 2 ? (ammo + 1) / 2 : ammo;
    if (count == 2)
        guns[1]->ammo = ammo - guns[0]->ammo;
    for (int g = 0; g < count; ++g)
        guns[g]->cooldown = u.reload;
}
// Keeps the hands lawful (2026-11-17): guns only in the hands and items only on the belt (a stray goes to the bag, or the
// floor when the bag is full), a long gun in slot 1 with slot 2 empty.
inline void Battle::normalizeHands(Unit& u)
{
    auto& s = u.kit.slots;
    auto evict = [&](HotSlot& stray)
    {
        if (stray.empty())
            return;
        const HotSlot piece = stray;
        stray = HotSlot{};
        for (int i = HotbarSlots; i < kitSize(u); ++i)
            if (HotSlot* bag = kitSlot(u, i); bag && bag->empty())
            {
                *bag = piece;
                return;
            }
        if (piece.kind == SlotKind::Weapon)
            scatterLoot(LootKind::Weapon, piece.what, piece.ammo, u.position, -1, 160, piece.variant);
        else
            scatterLoot(LootKind::Item, piece.what, std::max(1, piece.count), u.position, -1, 160);
    };
    for (int h = 0; h < HandSlots; ++h)
        if (s[static_cast<size_t>(h)].kind == SlotKind::Item)
            evict(s[static_cast<size_t>(h)]);
    for (int b = HandSlots; b < HotbarSlots; ++b)
        if (s[static_cast<size_t>(b)].kind == SlotKind::Weapon)
            evict(s[static_cast<size_t>(b)]);
    if (s[1].kind == SlotKind::Weapon && twoHanded(s[1].what))
    {
        evict(s[0]);
        std::swap(s[0], s[1]);
    }
    if (s[0].kind == SlotKind::Weapon && twoHanded(s[0].what))
        evict(s[1]);
}
inline void Battle::syncHand(Unit& u)
{
    normalizeHands(u);
    u.kit.selected = std::clamp(u.kit.selected, 0, HotbarSlots - 1);
    // Both hand keys hold the guns; an item slot that has run empty gives the hands back.
    if (handSlot(u.kit.selected) || u.kit.slots[static_cast<size_t>(u.kit.selected)].empty())
        u.kit.selected = 0;
    const int was = u.weapon;
    const bool wasDual = u.dual;
    const HotSlot* guns[2];
    const int count = handGuns(u, guns);
    if (count > 0)
    {
        u.weapon = guns[0]->what;
        u.dual = count == 2;
        u.ammo = guns[0]->ammo + (count == 2 ? guns[1]->ammo : 0);
        u.reload = count == 2 ? std::max(guns[0]->cooldown, guns[1]->cooldown) : guns[0]->cooldown;
        if (u.dual)
        {
            u.stats.feats |= featBit(Feat::Pair);
            if (guns[0]->variant == StaplerVariant && guns[1]->variant == StaplerVariant)
                u.stats.feats |= featBit(Feat::StaplerPair);
        }
    }
    else
    {
        u.weapon = FistWeapon;
        u.dual = false;
        u.ammo = 1;
        u.reload = 0;
    }
    if (was != u.weapon || wasDual != u.dual)
    {
        u.cooldown = std::max(u.cooldown, .2);
        u.rocketHeld = false;
        u.rocketCharge = 0;
    }
}
// Picks a piece up (2026-11-17): a gun into free hands (a pistol beside a pistol makes the dual wield) or else the bag,
// an item onto its stack, a free belt slot (3-5) or the bag (planStow).
inline bool Battle::stow(Unit& u, LootKind kind, int what, int amount, int variant)
{
    stowHand(u);
    if (kind == LootKind::Weapon)
    {
        if (what < 0 || what > 5)
            return false;
        if (!validVariant(what, variant))
            variant = -1;
    }
    else if (kind != LootKind::Item || what < 0 || what >= GearCount)
        return false;
    const bool gun = kind == LootKind::Weapon;
    const StowPlan plan = planStow([&](int i) -> const HotSlot& { return *kitSlot(u, i); }, packSlots(u), gun, what, !gun && gearOneTime(what));
    HotSlot* s = plan.place == StowPlace::None ? nullptr : kitSlot(u, plan.slot);
    if (!s)
        return false;
    if (gun)
    {
        *s = {SlotKind::Weapon, what, 1, std::clamp(amount, 0, gunMagazine(what, variant, false)), 0};
        s->variant = variant;
        if (plan.place == StowPlace::Pair)
            event(EffectStyle::DualWield, u.position, u.position, u.team, what, .8);
    }
    else if (plan.place == StowPlace::Stack)
        s->count = std::min(3, s->count + std::max(1, amount));
    else
        *s = {SlotKind::Item, what, gearOneTime(what) ? std::max(1, amount) : std::max(0, amount), 0, 0};
    syncHand(u);
    refreshCapacities(u);
    return true;
}
inline bool Battle::selectSlot(int id, int slot)
{
    Unit* u = find(id);
    if (!expeditionMode() || !u || !u->human || !u->alive || u->kit.extracted || slot < 0 || slot >= HotbarSlots)
        return false;
    // Keys 1 and 2 both take up the guns (2026-11-17: the two hands); an empty item slot has nothing to hold.
    if (handSlot(slot))
        slot = 0;
    else if (u->kit.slots[static_cast<size_t>(slot)].empty())
        return false;
    stowHand(*u);
    u->kit.selected = slot;
    syncHand(*u);
    return true;
}
inline bool Battle::dropSlot(int id)
{
    Unit* u = find(id);
    if (!expeditionMode() || !u || !u->human || !u->alive || u->kit.extracted)
        return false;
    stowHand(*u);
    // With the guns up, G drops the right hand's pistol first, then the gun in the left hand.
    const int index = handSlot(u->kit.selected) ? (u->kit.slots[1].kind == SlotKind::Weapon ? 1 : 0) : u->kit.selected;
    HotSlot& s = u->kit.slots[static_cast<size_t>(index)];
    if (s.empty())
        return false;
    const Vec2 at = maze.move(u->position, u->aim.normalized() * (u->radius + 45), 12);
    if (s.kind == SlotKind::Weapon)
    {
        scatterLoot(LootKind::Weapon, s.what, s.ammo, at, -1, 120, s.variant);
        s = HotSlot{};
    }
    else if (gearOneTime(s.what) && s.count > 1)
    {
        scatterLoot(LootKind::Item, s.what, 1, at, -1, 120);
        --s.count;
    }
    else
    {
        scatterLoot(LootKind::Item, s.what, s.count, at, -1, 120);
        s = HotSlot{};
    }
    syncHand(*u);
    refreshCapacities(*u);
    return true;
}
inline int Battle::dropStickers(int id, int amount)
{
    Unit* u = find(id);
    if (!expeditionMode() || !u || !u->human || !u->alive || u->kit.extracted || u->kit.pouch <= 0)
        return 0;
    const int n = amount <= 0 ? u->kit.pouch : std::min(amount, u->kit.pouch);
    u->kit.pouch -= n;
    // The lucky album's bonus never rides on stickers handed back and forth.
    u->kit.album = std::max(0., u->kit.album - n * AlbumBonus);
    scatterLoot(LootKind::Stickers, 0, n, maze.move(u->position, u->aim.normalized() * (u->radius + 50), 12), u->id, 120);
    return n;
}
inline int Battle::dropCoins(int id, int amount)
{
    Unit* u = find(id);
    if (!expeditionMode() || !u || !u->human || !u->alive || u->kit.extracted || u->kit.coins <= 0)
        return 0;
    const int n = amount <= 0 ? u->kit.coins : std::min(amount, u->kit.coins);
    u->kit.coins -= n;
    scatterLoot(LootKind::Coins, 0, n, maze.move(u->position, u->aim.normalized() * (u->radius + 50), 12), u->id, 120);
    return n;
}
// Tab's sheet: a slot of the hotbar or the bag dropped whole (a pair as two guns, a stack every charge).
inline bool Battle::dropKitSlot(int id, int index)
{
    Unit* u = find(id);
    if (!expeditionMode() || !u || !u->human || !u->alive || u->kit.extracted)
        return false;
    stowHand(*u);
    HotSlot* s = kitSlot(*u, index);
    if (!s || s->empty())
        return false;
    const Vec2 at = maze.move(u->position, u->aim.normalized() * (u->radius + 45), 12);
    if (s->kind == SlotKind::Weapon)
    {
        const int guns = std::max(1, s->count);
        for (int g = 0; g < guns; ++g)
            scatterLoot(LootKind::Weapon, s->what, std::min(gunMagazine(s->what, s->variant, false), s->ammo / guns), at, -1, 140, s->variant);
    }
    else
        scatterLoot(LootKind::Item, s->what, s->count, at, -1, 140);
    *s = HotSlot{};
    syncHand(*u);
    refreshCapacities(*u);
    return true;
}
// The gun at a bag slot taken into the hands (hand: the hand slot it was put on). A long gun clears both hands: what they
// held goes back into the bag, the first piece where the long gun was and a second pistol into another free slot (none:
// refused). A pistol pushes a long gun out to where it came from, or trades places with that hand's pistol.
inline bool Battle::takeIntoHands(Unit& u, int bagIndex, int hand)
{
    HotSlot* gun = kitSlot(u, bagIndex);
    if (!gun || gun->kind != SlotKind::Weapon || !handSlot(hand))
        return false;
    HotSlot &left = u.kit.slots[0], &right = u.kit.slots[1];
    if (twoHanded(gun->what))
    {
        HotSlot out[2];
        int count = 0;
        for (HotSlot* h : {&left, &right})
            if (!h->empty())
                out[count++] = *h;
        HotSlot* spare = nullptr;
        if (count == 2)
        {
            for (int i = HotbarSlots; i < kitSize(u) && !spare; ++i)
                if (i != bagIndex && kitSlot(u, i)->empty())
                    spare = kitSlot(u, i);
            if (!spare)
                return false;
        }
        const HotSlot taken = *gun;
        *gun = count > 0 ? out[0] : HotSlot{};
        if (spare)
            *spare = out[1];
        left = taken;
        right = HotSlot{};
        return true;
    }
    if (left.kind == SlotKind::Weapon && twoHanded(left.what))
    {
        const HotSlot taken = *gun;
        *gun = left;
        left = right = HotSlot{};
        u.kit.slots[static_cast<size_t>(hand)] = taken;
        return true;
    }
    std::swap(u.kit.slots[static_cast<size_t>(hand)], *gun);
    return true;
}
// Tab's sheet: a slot moved onto another (2026-11-17). The same one-time item stacks (three a slot); guns go only into the
// hands or the bag and items only onto the belt (slots 3-5) or into the bag; a gun brought into the hands from the bag
// follows takeIntoHands; anything else swaps places.
inline bool Battle::moveSlot(int id, int from, int to)
{
    Unit* u = find(id);
    if (!expeditionMode() || !u || !u->human || !u->alive || u->kit.extracted || from == to)
        return false;
    stowHand(*u);
    HotSlot* a = kitSlot(*u, from);
    HotSlot* b = kitSlot(*u, to);
    if (!a || !b || a->empty())
        return false;
    auto deny = [&](int reason)
    {
        Effect e{u->position, u->position, static_cast<double>(reason), .6, EventEffect, u->team};
        e.style = EffectStyle::Denied;
        e.pulse = u->id;
        addEffect(e);
        return false;
    };
    auto pistols = [&]() { return u->kit.slots[0].kind == SlotKind::Weapon && u->kit.slots[1].kind == SlotKind::Weapon; };
    const bool pairBefore = pistols();
    if (a->kind == SlotKind::Item && b->kind == SlotKind::Item && a->what == b->what && gearOneTime(a->what) && b->count < 3)
    {
        const int moved = std::min(3 - b->count, a->count);
        b->count += moved;
        if ((a->count -= moved) <= 0)
            *a = HotSlot{};
    }
    else
    {
        auto fits = [](const HotSlot& s, int index)
        { return s.empty() || (handSlot(index) ? s.kind == SlotKind::Weapon : beltSlot(index) ? s.kind == SlotKind::Item : true); };
        if (!fits(*a, to) || !fits(*b, from))
            return deny(handSlot(to) || handSlot(from) ? detail::DenyHandsGuns : detail::DenyBeltItems);
        if (handSlot(to) && !handSlot(from))
        {
            if (!takeIntoHands(*u, from, to))
                return deny(detail::DenyNoSpare);
        }
        else if (handSlot(from) && !handSlot(to) && b->kind == SlotKind::Weapon)
        {
            if (!takeIntoHands(*u, to, from))
                return deny(detail::DenyNoSpare);
        }
        else
            std::swap(*a, *b);
    }
    syncHand(*u);
    if (!pairBefore && pistols())
        event(EffectStyle::DualWield, u->position, u->position, u->team, u->kit.slots[0].what, .8);
    refreshCapacities(*u);
    return true;
}
// The shop's buyer takes a slot of the hotbar or the bag for coins (sellCoins). Returns the coins paid, or -1.
inline int Battle::sellSlot(int id, int index)
{
    Unit* u = find(id);
    if (!expeditionMode() || finished || !u || !u->human || !u->alive || u->kit.extracted)
        return -1;
    const Offer* buyer = nullptr;
    for (const auto& o : expedition.offers)
        if (o.kind == ServiceLoot && o.what == static_cast<int>(Service::Trade) && (o.position - u->position).length() < TradeReach + u->radius)
            buyer = &o;
    auto deny = [&](int reason)
    {
        Effect e{u->position, u->position, static_cast<double>(reason), .6, EventEffect, u->team};
        e.style = EffectStyle::Denied;
        e.pulse = u->id;
        addEffect(e);
        return -1;
    };
    if (!buyer)
        return deny(detail::DenyNoBuyer);
    stowHand(*u);
    HotSlot* s = kitSlot(*u, index);
    if (!s || s->empty())
        return -1;
    const int coins = sellCoins(*s);
    const int quality = static_cast<int>(slotQuality(*s));
    u->kit.coins += coins;
    *s = HotSlot{};
    syncHand(*u);
    refreshCapacities(*u);
    event(EffectStyle::Sale, buyer->position, u->position, u->team, coins, .8);
    (void)quality;
    return coins;
}
inline bool Battle::dropLook(int id, int category)
{
    Unit* u = find(id);
    if (!expeditionMode() || !u || !u->human || !u->alive || category < 0 || category >= WearSlots || u->kit.wear[static_cast<size_t>(category)] <= 0)
        return false;
    const size_t c = static_cast<size_t>(category);
    scatterLoot(LootKind::Look, lookId(category, u->kit.wear[c]), 1, u->position + u->aim.normalized() * (u->radius + 40),
                u->kit.wearOwner[c] >= 0 ? u->kit.wearOwner[c] : u->seat, 120);
    u->kit.wear[c] = -1;
    u->kit.wearOwner[c] = -1;
    refreshCapacities(*u);
    return true;
}

// ---- E ----
inline int Battle::interact(int id)
{
    Unit* u = find(id);
    if (!expeditionMode() || finished || !u || !u->human || !u->alive || u->kit.extracted)
        return 0;
    auto& x = expedition;
    // Something was in reach but refused (a full hotbar, a short pouch): -1, so E never falls through to a grab.
    auto deny = [&](int reason)
    {
        Effect e{u->position, u->position, static_cast<double>(reason), .6, EventEffect, u->team};
        e.style = EffectStyle::Denied;
        e.pulse = u->id;
        addEffect(e);
        return -1;
    };
    // 1. A gun, an item or a look underfoot (nearest first).
    GroundLoot* best = nullptr;
    double nearest = PickupReach + u->radius;
    for (auto& l : x.loot)
        if ((l.kind == LootKind::Weapon || l.kind == LootKind::Item || l.kind == LootKind::Look) && l.age > .25 && l.amount >= 0 && !l.airborne())
        {
            const double d = (l.position - u->position).length();
            if (d < nearest)
            {
                nearest = d;
                best = &l;
            }
        }
    if (best)
    {
        if (best->kind == LootKind::Look)
        {
            const int c = lookCategory(best->what), index = lookIndex(best->what);
            if (c < 0 || c >= WearSlots || index <= 0)
                return deny(detail::DenyNotNow);
            const size_t slot = static_cast<size_t>(c);
            const int oldWear = u->kit.wear[slot], oldOwner = u->kit.wearOwner[slot];
            const Vec2 at = best->position;
            // One look per category: a second one is swapped for the one worn, which stays on the floor (design 4.3).
            u->kit.wear[slot] = index;
            u->kit.wearOwner[slot] = best->owner == u->seat ? -1 : best->owner;
            best->amount = -1;
            if (oldWear > 0)
                scatterLoot(LootKind::Look, lookId(c, oldWear), 1, at, oldOwner >= 0 ? oldOwner : u->seat, 80);
            refreshCapacities(*u);
            bool all = true;
            for (int w : u->kit.wear)
                all &= w > 0;
            if (all)
                u->stats.feats |= featBit(Feat::AllLooks);
            event(EffectStyle::LootPickup, at, u->position, u->team, static_cast<double>(LootKind::Look), .5);
            x.loot.erase(std::remove_if(x.loot.begin(), x.loot.end(), [](const GroundLoot& g) { return g.amount < 0; }), x.loot.end());
            return 1;
        }
        if (!stow(*u, best->kind, best->what, best->amount, best->variant))
            return deny(detail::DenyFull);
        event(EffectStyle::LootPickup, best->position, u->position, u->team, static_cast<double>(best->kind), .5);
        best->amount = -1;
        x.loot.erase(std::remove_if(x.loot.begin(), x.loot.end(), [](const GroundLoot& g) { return g.amount < 0; }), x.loot.end());
        return 1;
    }
    // 2. A shelf's ware or an event machine.
    Offer* offer = nullptr;
    nearest = 1e18;
    for (auto& o : x.offers)
    {
        const bool trader = o.kind == ServiceLoot && o.what == static_cast<int>(Service::Trade);
        const double d = (o.position - u->position).length();
        if (!o.sold && d < (trader ? TradeReach : ShelfReach) + u->radius && d < nearest)
        {
            nearest = d;
            offer = &o;
        }
    }
    if (offer)
    {
        // The buyer opens the trade sheet on the player's screen; selling is its own request (sellSlot).
        if (offer->kind == ServiceLoot && offer->what == static_cast<int>(Service::Trade))
            return 5;
        if (offer->kind == ServiceLoot)
            return service(*u, *offer) ? 3 : -1;
        if (purse(*u) < offer->price)
            return deny(detail::DenyStickers);
        const int variant = offer->kind == LootKind::Weapon ? offer->extra : -1;
        if (!stow(*u, offer->kind, offer->what, offer->kind == LootKind::Weapon ? gunMagazine(offer->what, variant, false) : 1, variant))
            return deny(detail::DenyFull);
        pay(*u, offer->price);
        offer->sold = true;
        event(EffectStyle::Purchase, offer->position, u->position, u->team, offer->price, .7);
        return 2;
    }
    // 3. A chest (the chained one is held open instead).
    Chest* chest = nullptr;
    nearest = ChestReach + u->radius;
    for (auto& c : x.chests)
        if (!c.opened && c.grade != 2 && (c.position - u->position).length() < nearest)
        {
            nearest = (c.position - u->position).length();
            chest = &c;
        }
    if (chest)
    {
        openChest(*chest, *u);
        return 4;
    }
    return 0;
}
inline bool Battle::service(Unit& u, Offer& o)
{
    auto& x = expedition;
    if (o.room < 0 || o.room >= static_cast<int>(x.rooms.size()))
        return false;
    DungeonRoom& room = x.rooms[static_cast<size_t>(o.room)];
    const int bit = 1 << std::clamp(u.seat, 0, 15);
    auto deny = [&](int reason)
    {
        Effect e{u.position, u.position, static_cast<double>(reason), .6, EventEffect, u.team};
        e.style = EffectStyle::Denied;
        e.pulse = u.id;
        addEffect(e);
        return false;
    };
    if (purse(u) < o.price)
        return deny(detail::DenyStickers);
    const Service kind = static_cast<Service>(o.what);
    switch (kind)
    {
    case Service::Heal:
        // EV10: everyone standing gets back to full health, once.
        for (auto& v : units)
            if (v.human && v.alive)
                heal(v, v.maxHp);
        o.sold = true;
        break;
    case Service::BloodInk:
        // EV10: 40% of your health for 80 ink, once each.
        if ((room.eventState & bit) || u.hp <= u.maxHp * .4 + 1)
            return deny(detail::DenyUsed);
        u.hp -= u.maxHp * .4;
        u.ink = std::min(u.maxInk, u.ink + 80);
        room.eventState |= bit;
        break;
    case Service::Spin:
    case Service::BigSpin:
    {
        // EV06: the wheel stops on double stickers, a free eraser, half your health gone, or a purse for everyone.
        if (room.eventState >= 8)
            return deny(detail::DenyUsed);
        ++room.eventState;
        if (kind == Service::BigSpin)
            u.stats.feats |= featBit(Feat::BigSpin);
        const double pick = random01(rng);
        if (pick < .3)
            scatterLoot(LootKind::Stickers, 0, o.price * 2, o.position, -1, 220);
        else if (pick < .5)
            scatterLoot(LootKind::Item, static_cast<int>(Gear::Eraser), 1, o.position, -1, 220);
        else if (pick < .75)
            u.hp = std::max(1., u.hp * .5);
        else
            for (auto& v : units)
                if (v.human && v.alive && !v.kit.extracted)
                    v.kit.pouch += o.price / 2;
        break;
    }
    case Service::Wish:
    {
        // EV02: 20 stickers into the well, once each: a keepsake, ink and a little health for all, or an angry elite.
        if (room.eventState & bit)
            return deny(detail::DenyUsed);
        room.eventState |= bit;
        const double pick = random01(rng);
        if (pick < .55)
        {
            if (random01(rng) < .6)
                scatterLoot(LootKind::Item, randomGear(2), 1, o.position, -1, 240);
            else
            {
                const int w = randomWeapon();
                scatterLoot(LootKind::Weapon, w, magazine(w, false), o.position, -1, 240);
            }
        }
        else if (pick < .85)
        {
            for (auto& v : units)
                if (v.human && v.alive && !v.kit.extracted)
                {
                    v.ink = std::min(v.maxInk, v.ink + 40);
                    heal(v, v.maxHp * .2);
                }
        }
        else
        {
            spawnFoe(biomeElite(x.biome(), random01(rng) < .5 ? 0 : 1), o.room, roomSpot(room, 350));
            flushUnits();
        }
        break;
    }
    case Service::Copy:
    {
        // EV09: the one-time item lying on the copier comes out twice (70%) or jams into scrap.
        if (room.eventState >= 2)
            return deny(detail::DenyUsed);
        GroundLoot* item = nullptr;
        for (auto& l : x.loot)
            if (l.kind == LootKind::Item && gearOneTime(l.what) && (l.position - o.position).length() < 230)
                item = &l;
        if (!item)
            return deny(detail::DenyNotNow);
        ++room.eventState;
        if (random01(rng) < .7)
        {
            scatterLoot(LootKind::Item, item->what, 1, o.position, -1, 200);
            if (item->what == static_cast<int>(Gear::Eraser))
                u.stats.feats |= featBit(Feat::CopyEraser);
        }
        else
        {
            item->amount = -1;
            scatterLoot(LootKind::Stickers, 0, 10, o.position, -1, 200);
            x.loot.erase(std::remove_if(x.loot.begin(), x.loot.end(), [](const GroundLoot& g) { return g.amount < 0; }), x.loot.end());
        }
        break;
    }
    case Service::Rub:
    {
        // EV12: the gun in hand is rubbed into a twin, once each (2026-11-17): a pistol's twin goes into the free hand
        // (the dual wield), any other twin into the bag, or onto the floor by the table when the bag is full.
        stowHand(u);
        const HotSlot* guns[2];
        const int count = handGuns(u, guns);
        if ((room.eventState & bit) || count == 0)
            return deny(detail::DenyNotNow);
        room.eventState |= bit;
        const HotSlot twin = *guns[count - 1];
        const int rounds = gunMagazine(twin.what, twin.variant, false);
        if (!stow(u, LootKind::Weapon, twin.what, rounds, twin.variant))
            scatterLoot(LootKind::Weapon, twin.what, rounds, o.position, -1, 200, twin.variant);
        syncHand(u);
        break;
    }
    case Service::Sell:
    {
        // EV01: the gun in hand (a single one) goes on the scale, or a look or gun left lying on it. The scale weighs in
        // coins (2026-11-16: whatever is sold becomes coins, never stickers): the buyer's price, doubled, give or take.
        stowHand(u);
        // With the guns up, the right hand's pistol goes on first (2026-11-17), as G drops it.
        const int index = handSlot(u.kit.selected) ? (u.kit.slots[1].kind == SlotKind::Weapon ? 1 : 0) : u.kit.selected;
        HotSlot& s = u.kit.slots[static_cast<size_t>(index)];
        int value = 0;
        if (s.kind == SlotKind::Weapon)
        {
            HotSlot one = s;
            one.count = 1;
            value = sellCoins(one);
            s = HotSlot{};
            syncHand(u);
        }
        else
        {
            GroundLoot* look = nullptr;
            for (auto& l : x.loot)
                if ((l.kind == LootKind::Look || l.kind == LootKind::Weapon) && (l.position - o.position).length() < 230)
                    look = &l;
            if (!look)
                return deny(detail::DenyNotNow);
            if (look->kind == LootKind::Weapon)
            {
                HotSlot one{SlotKind::Weapon, look->what, 1, 0, 0};
                one.variant = look->variant;
                value = sellCoins(one);
            }
            else
                value = 150;
            look->amount = -1;
            x.loot.erase(std::remove_if(x.loot.begin(), x.loot.end(), [](const GroundLoot& g) { return g.amount < 0; }), x.loot.end());
        }
        u.kit.coins += static_cast<int>(std::lround(std::max(100, value) * (1.4 + random01(rng) * 1.2)));
        break;
    }
    case Service::Arena:
    {
        // EV08: thirty paper monsters at once; clear them in thirty seconds for a purse each and full ink.
        if (room.eventState != 0)
            return deny(detail::DenyUsed);
        room.eventState = 1;
        room.eventClock = 30;
        for (int k = 0; k < 30; ++k)
        {
            const int idm = spawnFoe(biomeFoe(x.biome(), 0), o.room, roomSpot(room, 300));
            for (auto& m : pendingUnits)
                if (m.id == idm)
                    m.hp = m.maxHp = m.maxHp * .35;
        }
        flushUnits();
        o.sold = true;
        break;
    }
    case Service::Draw:
    {
        // EV11: as many blind draws as the party has players.
        if (room.eventState >= x.party)
            return deny(detail::DenyUsed);
        ++room.eventState;
        const double pick = random01(rng);
        if (pick < .35)
        {
            const int w = randomWeapon();
            scatterLoot(LootKind::Weapon, w, magazine(w, false), o.position, -1, 240);
        }
        else if (pick < .6)
            scatterLoot(LootKind::Look, randomLook(), 1, o.position, -1, 240);
        else if (pick < .85)
            scatterLoot(LootKind::Item, randomGear(random01(rng) < .7 ? 0 : 1), 1, o.position, -1, 240);
        else
        {
            // An angry fountain-pen bug jumps out of the box.
            spawnFoe(Foe::InkBug, o.room, o.position + Vec2{0, 120});
            flushUnits();
        }
        if (room.eventState >= x.party)
            o.sold = true;
        break;
    }
    case Service::Reforge:
    {
        // EV03: the gun in hand takes the stage's nib (two reforges a run); with a pistol in each hand, the first one
        // without this nib (the left one first).
        stowHand(u);
        const int nib = static_cast<int>(x.biome());
        HotSlot* s = nullptr;
        bool armed = false;
        if (handSlot(u.kit.selected))
            for (int h = 0; h < HandSlots; ++h)
                if (HotSlot& g = u.kit.slots[static_cast<size_t>(h)]; g.kind == SlotKind::Weapon)
                {
                    armed = true;
                    if (!s && g.nib != nib)
                        s = &g;
                }
        if (!armed || u.kit.reforges >= MaxReforges || !s)
            return deny(!armed ? detail::DenyNotNow : detail::DenyUsed);
        s->nib = nib;
        if (++u.kit.reforges >= MaxReforges)
            u.stats.feats |= featBit(Feat::Reforge2);
        syncHand(u);
        break;
    }
    case Service::Vote:
    {
        // EV04: one vote each; the ballot closes when everyone has voted or twelve seconds after the first.
        if (x.floorPrank >= 0 || u.kit.vote >= 0 || o.extra < 0)
            return deny(detail::DenyUsed);
        u.kit.vote = o.extra;
        if (x.voteClock <= 0)
            x.voteClock = VoteSeconds;
        break;
    }
    case Service::Borrow:
        // EV07: a pen for the stage's revive drawings, free.
        if (o.extra < 0 || o.extra >= BiomeCount || ((u.kit.borrowed >> o.extra) & 1u))
            return deny(detail::DenyUsed);
        u.kit.borrowed |= 1u << o.extra;
        break;
    default:
        return false;
    }
    pay(u, o.price);
    event(EffectStyle::EventFire, o.position, u.position, u.team, static_cast<double>(room.event), .8);
    return true;
}
inline void Battle::updateEvents(double dt)
{
    auto& x = expedition;
    // EV04's ballot: everyone standing has voted, or the clock ran out after the first vote.
    if (x.floorPrank < 0 && x.voteClock > 0)
    {
        int voters = 0, living = 0, tally[FloorPrankCount] = {};
        for (const auto& u : units)
            if (u.human && u.alive && !u.kit.extracted)
            {
                ++living;
                if (u.kit.vote >= 0 && u.kit.vote < FloorPrankCount)
                {
                    ++voters;
                    ++tally[u.kit.vote];
                }
            }
        x.voteClock -= dt;
        if (voters > 0 && (voters >= living || x.voteClock <= 0))
        {
            int best = 0, ties = 0;
            for (int k = 0; k < FloorPrankCount; ++k)
                if (tally[k] > tally[best])
                    best = k;
            int pick = static_cast<int>(randomStep(rng) % 64);
            for (int k = 0; k < FloorPrankCount; ++k)
                ties += tally[k] == tally[best];
            pick %= std::max(1, ties);
            for (int k = 0; k < FloorPrankCount; ++k)
                if (tally[k] == tally[best] && pick-- == 0)
                {
                    best = k;
                    break;
                }
            x.floorPrank = best;
            x.voteClock = 0;
            const double reward = tierInfo(config.tier).reward;
            for (auto& u : units)
                if (u.human && u.alive && !u.kit.extracted)
                {
                    u.kit.pouch += tierStickers(VoteStickers, reward, rng);
                    if (voters >= 4 && u.kit.vote >= 0)
                        u.stats.feats |= featBit(Feat::Vote4);
                    refreshCapacities(u);
                }
            for (auto& o : x.offers)
                if (o.kind == ServiceLoot && o.what == static_cast<int>(Service::Vote))
                    o.sold = true;
            for (const auto& o : x.offers)
                if (o.kind == ServiceLoot && o.what == static_cast<int>(Service::Vote) && o.extra == best)
                    event(EffectStyle::EventFire, o.position, o.position, 1, static_cast<double>(RoomEvent::Vote), 1);
        }
    }
    for (int i = 0; i < static_cast<int>(x.rooms.size()); ++i)
    {
        DungeonRoom& r = x.rooms[static_cast<size_t>(i)];
        if (r.kind != RoomKind::Event)
            continue;
        if (r.event == RoomEvent::Arena && r.eventState == 1)
        {
            r.eventClock -= dt;
            int alive = 0;
            for (const auto& u : units)
                alive += enemy(u) && u.room == i;
            if (alive == 0)
            {
                // Done in time: a purse per player in the middle of the room and every ink tank full.
                r.eventState = 2;
                partyFeat(Feat::Arena);
                const Vec2 mid = roomCenter(r);
                for (int k = 0; k < x.party; ++k)
                    scatterLoot(LootKind::Stickers, 0, tierStickers(12, tierInfo(config.tier).reward, rng), mid, -1, 380);
                for (auto& u : units)
                    if (u.human && u.alive)
                        u.ink = u.maxInk;
                event(EffectStyle::RoomClear, mid, mid, 1, 1, 1);
            }
            else if (r.eventClock <= 0)
                r.eventState = 3;
        }
        if (r.event == RoomEvent::Chained)
        {
            Chest* chest = nullptr;
            for (auto& c : x.chests)
                if (c.room == i && c.grade == 2 && !c.opened)
                    chest = &c;
            if (!chest)
                continue;
            // EV05: someone holds E in the ring; an ambush keeps coming while they do.
            Unit* holder = nullptr;
            for (auto& u : units)
                if (u.human && u.alive && !u.kit.extracted && u.input.use && (u.position - chest->position).length() < 210)
                    holder = &u;
            if (!holder)
                continue;
            r.eventClock += dt;
            holder->kit.holdTarget = chest->id;
            holder->kit.hold = r.eventClock;
            r.clock -= dt;
            if (r.clock <= 0)
            {
                r.clock = 1.6;
                const int count = 1 + static_cast<int>(randomStep(rng) % 2) + (x.stage >= 3);
                for (int k = 0; k < count; ++k)
                    spawnFoe(biomeFoe(x.biome(), random01(rng) < .7 ? 0 : 2), i, roomSpot(r, 380));
                flushUnits();
            }
            if (r.eventClock >= ChainedHoldSeconds)
            {
                openChest(*chest, *holder);
                holder->kit.hold = 0;
                holder->kit.holdTarget = -1;
            }
        }
    }
}

// ---- Items, traits and their zones ----
inline void Battle::heal(Unit& u, double amount)
{
    if (!u.alive || amount <= 0)
        return;
    // The fountain pen's sharp body takes healing a little worse (design 5.2: -15%).
    if (expeditionMode() && u.kit.trait == static_cast<int>(PenBiome::Fountain))
        amount *= .85;
    u.hp = std::min(u.maxHp, u.hp + amount);
}
// Ink mending (R, 2026-11-17; the expedition and the box). The flow speeds up the longer R is held (MendBaseRate ink a
// second, MendRamp more of that each second, up to MendMaxRate), MendInkPerHealth ink a point of health. Since the second
// playtest the same day it fills a circle: every hurt player within MendReach of the mender (the mender too, on the same
// level) takes an even share, and the mender and everyone mended walk at MendSlow while it flows.
inline void Battle::updateMend(double dt)
{
    if (!expeditionMode() && !defenseMode())
        return;
    for (auto& u : units)
    {
        u.mendSlow = std::max(0., u.mendSlow - dt);
        u.mendOn = -1;
    }
    std::vector<Unit*> circle;
    for (auto& u : units)
    {
        if (!u.human || !u.alive || !u.input.mend || u.carried || u.kit.ghost || u.kit.extracted || u.ink <= 1e-9)
        {
            u.mendTime = 0;
            continue;
        }
        circle.clear();
        double missing = 0;
        for (auto& v : units)
            if (v.human && v.alive && v.team == u.team && v.deck == u.deck && !v.kit.ghost && !v.kit.extracted && v.hp < v.maxHp - 1e-6 &&
                (v.position - u.position).length() <= MendReach + v.radius)
            {
                circle.push_back(&v);
                missing += v.maxHp - v.hp;
            }
        if (circle.empty())
        {
            u.mendTime = 0;
            continue;
        }
        u.mendTime += dt;
        const double rate = std::min(MendMaxRate, MendBaseRate * (1 + MendRamp * u.mendTime));
        const double ink = std::min({u.ink, rate * dt, missing * MendInkPerHealth}), share = ink / static_cast<double>(circle.size());
        double spent = 0;
        for (Unit* v : circle)
        {
            const double given = std::min(share, (v->maxHp - v->hp) * MendInkPerHealth);
            heal(*v, given / MendInkPerHealth);
            spent += given;
            v->mendSlow = MendLinger;
        }
        u.ink = std::max(0., u.ink - spent);
        u.mendOn = u.id;
        u.mendSlow = MendLinger;
    }
}
inline void Battle::updateKits(double dt)
{
    for (auto& u : units)
    {
        if (u.kind == UnitKind::Fighter && u.player())
        {
            Kit& k = u.kit;
            for (auto& s : k.slots)
                if (s.kind == SlotKind::Item && s.cooldown > 0)
                    s.cooldown = std::max(0., s.cooldown - dt);
            for (auto& s : k.pack)
                if (s.kind == SlotKind::Item && s.cooldown > 0)
                    s.cooldown = std::max(0., s.cooldown - dt);
            if (!u.alive)
                continue;
            refreshCapacities(u);
            k.roomGuard = std::max(0., k.roomGuard - dt);
            if (k.healTime > 0)
            {
                const double step = std::min(dt, k.healTime), part = k.heal * step / k.healTime;
                heal(u, part);
                k.heal -= part;
                k.healTime -= step;
            }
            k.traitClock += dt;
            switch (static_cast<PenBiome>(k.trait))
            {
            case PenBiome::Pencil:
                // A graphite shield of a quarter of health that slowly fills back.
                k.graphite = std::min(u.maxHp * .25, k.graphite + u.maxHp * .02 * dt);
                break;
            case PenBiome::Watercolor:
                // Health seeps back; enemies brushing past are slowed and wet.
                heal(u, u.maxHp * .015 * dt);
                for (auto& m : units)
                    if (m.alive && m.team != u.team && (m.position - u.position).length() < u.radius + m.radius + 40)
                    {
                        m.slowTime = std::max(m.slowTime, .5);
                        m.wet = std::max(m.wet, 2.);
                    }
                break;
            case PenBiome::Brush:
                // Every four seconds an ink wave throws the nearest enemies back.
                if (k.traitClock >= 4)
                {
                    k.traitClock = 0;
                    for (auto& m : units)
                        if (m.alive && m.team != u.team && !bossFoe(m.breed) && (m.position - u.position).length() < 230 + m.radius)
                            m.impulse += (m.position - u.position).normalized() * 620;
                    event(EffectStyle::Voltage, u.position, u.position, u.team, 230, .45);
                }
                break;
            default:
                break;
            }
        }
        else if (u.gadget == static_cast<int>(Gear::Crane) && u.alive && u.bait <= 0)
        {
            // The paper crane bursts when its taunt runs out, its paper flying into whoever came for it.
            u.alive = false;
            u.hp = 0;
            blast(u.builder, u.team, u.position, 220, 40, .45, EffectStyle::Shatter);
        }
        else if (u.gadget == static_cast<int>(Gear::Sharpener) && u.alive && (u.respawn -= dt) <= 0)
        {
            u.alive = false;
            u.hp = 0;
            u.turret = false;
            event(EffectStyle::Shatter, u.position, u.position, u.team, u.radius, .4);
        }
    }
}
inline void Battle::updateZones(double dt)
{
    auto& x = expedition;
    for (auto& z : x.zones)
    {
        z.remaining -= dt;
        if (z.kind == 0)
        {
            // Correction fluid: friends standing in it heal.
            for (auto& u : units)
                if (u.alive && u.team == 1 && u.kind == UnitKind::Fighter && (u.position - z.from).length() < z.radius + u.radius)
                    heal(u, CorrectionHeal * dt);
        }
        else if (z.kind == 1)
        {
            // Double-sided tape: whatever walks over it sticks fast (bosses only slow down).
            for (auto& m : units)
                if (enemy(m) && m.stun <= 0 && distanceToSegment(m.position, z.from, z.to) < z.radius + m.radius)
                {
                    if (bossFoe(m.breed))
                        m.slowTime = std::max(m.slowTime, 1.);
                    else
                    {
                        m.stun = TrapSeconds;
                        if (++z.owner >= 8)
                            z.remaining = 0;
                    }
                }
        }
    }
    x.zones.erase(std::remove_if(x.zones.begin(), x.zones.end(), [](const ExpeditionState::Zone& z) { return z.remaining <= 0; }), x.zones.end());
}
inline void Battle::punch(Unit& u)
{
    // Bare hands (a naked revive): a slap that shoves more than it hurts (design 4.4: 5 points).
    u.cooldown = weaponStats(FistWeapon).interval;
    const Vec2 dir = u.aim.normalized();
    for (auto& v : units)
    {
        if (!v.alive || v.team == u.team)
            continue;
        const Vec2 d = v.position - u.position;
        const double reach = u.radius + v.radius + FistReach;
        if (d.length() < reach && dir.dot(d.normalized()) > .25)
        {
            damageUnit(u.id, v.id, 5 * perk(u, Attribute::Damage));
            if (!bossFoe(v.breed))
                v.impulse += dir * FistShove;
        }
    }
    event(EffectStyle::Shove, u.position + dir * (u.radius + 20), u.position + dir * (u.radius + 80), u.team, 60, .25);
}
inline void Battle::erase(Unit& u)
{
    // The eraser (design 5.1): a lunge that rubs out the enemy nearest the aim within 1.8 m and a 110 degree arc.
    const Vec2 dir = u.aim.normalized();
    Unit* target = nullptr;
    double best = -2;
    for (auto& v : units)
    {
        if (!enemy(v) || v.team == u.team)
            continue;
        const Vec2 d = v.position - u.position;
        const double length = d.length();
        if (length > EraserReach + v.radius + u.radius)
            continue;
        const double facing = length > 1e-6 ? dir.dot(d * (1 / length)) : 1;
        if (facing < std::cos(EraserArc * .5) && length > v.radius + u.radius)
            continue;
        if (facing > best)
        {
            best = facing;
            target = &v;
        }
    }
    u.impulse += dir * 600;
    event(EffectStyle::EraseSwipe, u.position, u.position + dir * EraserReach, u.team, target ? 1 : 0, .4);
    if (!target)
    {
        // Nothing to rub out: a dashed pencil wall in reach is rubbed away instead, and the eraser is kept.
        const Vec2 tip = u.position + dir * (EraserReach * .7);
        if (const int cell = maze.cell(tip); cell >= 0 && !maze.isFixed(cell) && maze.walls[static_cast<size_t>(cell)] > 0)
        {
            maze.setWall(cell, false);
            event(EffectStyle::WallErase, maze.center(cell), maze.center(cell), u.team, 0, .5);
        }
        return;
    }
    rawHit = true;
    const uint8_t tools = static_cast<uint8_t>(target->parts & 3);
    if (!bossFoe(target->breed))
    {
        damageUnit(u.id, target->id, target->hp + target->armor + 1e6);
        ++u.stats.erased;
    }
    else if (tools)
    {
        // A tool boss loses the part nearest the eraser (its attacks with it), a fifth of its health, and staggers for
        // three seconds (design 5.1: rub its left side for the left arm's tool).
        int bit = -1;
        double nearest = 1e18;
        for (int b = 0; b < 2; ++b)
            if ((tools >> b) & 1u)
            {
                const double d = (foePartAt(target->breed, b, target->position, target->aim, target->radius) - u.position).length();
                if (d < nearest)
                {
                    nearest = d;
                    bit = b;
                }
            }
        target->parts = static_cast<uint8_t>(target->parts & ~(1u << bit));
        event(EffectStyle::PartErased, foePartAt(target->breed, bit, target->position, target->aim, target->radius), u.position, target->team, bit, 1);
        target->stun = 3;
        target->act = static_cast<int>(FoeAct::Idle);
        u.stats.feats |= featBit(Feat::Disarm);
        if (target->breed == static_cast<int>(Foe::Editor) && (target->parts & 3) == 0)
            u.stats.feats |= featBit(Feat::Toolless);
        damageUnit(u.id, target->id, target->maxHp * .2);
    }
    else if (target->erased == 0)
    {
        // A skill boss has half its health rubbed away, once per fight.
        target->erased = 1;
        u.stats.feats |= featBit(Feat::HalfErase);
        damageUnit(u.id, target->id, target->maxHp * .5);
    }
    else
    {
        // A second eraser takes a quarter of what is left and wipes the floor's hazards.
        ++target->erased;
        damageUnit(u.id, target->id, target->hp * .25);
        expedition.zones.erase(std::remove_if(expedition.zones.begin(), expedition.zones.end(),
                                              [](const ExpeditionState::Zone& z) { return z.kind >= 10; }),
                               expedition.zones.end());
    }
    rawHit = false;
    // The eraser is used up (a whiff keeps it).
    HotSlot& s = u.kit.slots[static_cast<size_t>(u.kit.selected)];
    if (s.kind == SlotKind::Item && s.what == static_cast<int>(Gear::Eraser) && --s.count <= 0)
        s = HotSlot{};
    syncHand(u);
}
inline void Battle::useGear(Unit& u)
{
    auto& x = expedition;
    HotSlot& s = u.kit.slots[static_cast<size_t>(u.kit.selected)];
    if (s.kind != SlotKind::Item)
        return;
    if (u.kit.sealed > 0)
    {
        Effect e{u.position, u.position, static_cast<double>(detail::DenyNotNow), .6, EventEffect, u.team};
        e.style = EffectStyle::Denied;
        e.pulse = u.id;
        addEffect(e);
        return;
    }
    const Gear gear = static_cast<Gear>(s.what);
    const Vec2 dir = u.aim.normalized();
    const Vec2 ahead = maze.move(u.position, dir * 350, 10);
    bool spent = gearOneTime(s.what);
    switch (gear)
    {
    case Gear::Eraser:
        return erase(u);
    case Gear::ReviveKit:
    case Gear::StandIn:
        // The kit is drawn with (the client's sheet sends reviveDrawn); the stand-in waits for its owner's fall.
        return;
    case Gear::StickyNote:
        if (u.kit.note)
            return;
        u.kit.note = true;
        break;
    case Gear::InkCapsule:
        for (auto& v : units)
            if (v.human && v.alive && !v.kit.extracted)
                v.ink = std::min(v.maxInk, v.ink + CapsuleInk);
        for (auto& m : units)
            if (enemy(m) && (m.position - u.position).length() < 260 + m.radius)
                m.impulse += (m.position - u.position).normalized() * 500;
        break;
    case Gear::Correction:
    {
        ExpeditionState::Zone z;
        z.kind = 0;
        z.from = z.to = ahead;
        z.radius = CorrectionRadius;
        z.remaining = CorrectionSeconds;
        x.zones.push_back(z);
        break;
    }
    case Gear::Crane:
    {
        Unit c;
        c.id = nextId++;
        c.team = u.team;
        c.kind = UnitKind::Companion;
        c.state = UnitState::Idle;
        c.gadget = static_cast<int>(Gear::Crane);
        c.shape = Shape::Triangle;
        c.radius = 46;
        c.hp = c.maxHp = 260;
        c.bait = CraneSeconds;
        c.builder = u.id;
        c.position = maze.move(u.position, dir * 260, c.radius);
        queueUnit(c);
        break;
    }
    case Gear::Stamp:
        for (auto& m : units)
            if (enemy(m) && (m.position - ahead).length() < StampRadius + m.radius)
                m.stamp = StampSeconds;
        break;
    case Gear::TapeTrap:
    {
        ExpeditionState::Zone z;
        z.kind = 1;
        z.owner = 0;
        const Vec2 across{-dir.y, dir.x}, at = u.position + dir * 110;
        z.from = at - across * (TrapLength * .5);
        z.to = at + across * (TrapLength * .5);
        z.radius = 30;
        z.remaining = 20;
        x.zones.push_back(z);
        break;
    }
    case Gear::Dryer:
        for (auto& p : projectiles)
        {
            const Vec2 d = p.position - u.position;
            if (p.team != u.team && d.length() < DryerReach && dir.dot(d.normalized()) > std::cos(DryerArc * .5))
                p.remaining = 0;
        }
        for (auto& m : units)
        {
            const Vec2 d = m.position - u.position;
            if (enemy(m) && d.length() < DryerReach + m.radius && dir.dot(d.normalized()) > std::cos(DryerArc * .5))
            {
                m.impulse += d.normalized() * (bossFoe(m.breed) ? 200 : 950);
                m.stun = std::max(m.stun, bossFoe(m.breed) ? .3 : 1.);
            }
        }
        break;
    case Gear::Scissors:
    {
        GroundLoot* scrap = nullptr;
        double nearest = 240;
        for (auto& l : x.loot)
            if ((l.kind == LootKind::Weapon || l.kind == LootKind::Look || l.kind == LootKind::Item) && (l.position - u.position).length() < nearest)
            {
                nearest = (l.position - u.position).length();
                scrap = &l;
            }
        if (!scrap)
            return;
        // What it would fetch from the buyer, and a little more for the trouble (a look: 150-250), in coins.
        HotSlot piece{scrap->kind == LootKind::Weapon ? SlotKind::Weapon : SlotKind::Item, scrap->what, std::max(1, scrap->amount), 0, 0};
        piece.variant = scrap->variant;
        if (scrap->kind == LootKind::Weapon)
            piece.count = 1;
        const int value = scrap->kind == LootKind::Look ? 150 + static_cast<int>(randomStep(rng) % 101) : static_cast<int>(std::lround(sellCoins(piece) * 1.2));
        scrap->amount = -1;
        x.loot.erase(std::remove_if(x.loot.begin(), x.loot.end(), [](const GroundLoot& g) { return g.amount < 0; }), x.loot.end());
        u.kit.coins += value;
        u.ink = std::min(u.maxInk, u.ink + 15);
        break;
    }
    case Gear::Bandage:
        u.kit.heal = u.maxHp * BandageShare;
        u.kit.healTime = BandageSeconds;
        u.slowTime = u.burnTime = 0;
        break;
    case Gear::GoldDust:
    {
        Unit* best = nullptr;
        double score = -1e18;
        for (auto& m : units)
        {
            const Vec2 d = m.position - u.position;
            if (!enemy(m) || d.length() > 750)
                continue;
            const double value = (m.elite || bossFoe(m.breed) ? 1000 : 0) + dir.dot(d.normalized()) * 100 - d.length() * .1;
            if (value > score)
            {
                score = value;
                best = &m;
            }
        }
        if (!best)
            return;
        best->gold = GoldSeconds;
        break;
    }
    case Gear::TapeRoll:
    {
        if (s.cooldown > 0)
            return;
        Unit* who = &u;
        for (auto& v : units)
            if (v.human && v.alive && v.id != u.id && (v.position - u.position).length() < 320 && v.hp / v.maxHp < who->hp / who->maxHp)
                who = &v;
        who->kit.tape = TapeShield;
        s.cooldown = gearInfo(s.what).cooldown;
        break;
    }
    case Gear::Sharpener:
    {
        if (s.cooldown > 0)
            return;
        Unit t;
        t.id = nextId++;
        t.team = u.team;
        t.kind = UnitKind::Companion;
        t.state = UnitState::Idle;
        t.gadget = static_cast<int>(Gear::Sharpener);
        t.turret = true;
        t.builder = u.id;
        t.sides = 4;
        t.shape = Shape::Square;
        t.radius = 30;
        t.hp = t.maxHp = 300;
        t.respawn = SharpenerSeconds;
        t.position = maze.move(u.position, dir * 90, t.radius);
        queueUnit(t);
        s.cooldown = gearInfo(s.what).cooldown;
        break;
    }
    default:
        // Tools that work while carried and relics: nothing to click.
        return;
    }
    event(EffectStyle::ItemUse, u.position, gear == Gear::Correction || gear == Gear::Stamp ? ahead : u.position + dir * 120, u.team,
          static_cast<double>(gear), .6);
    if (spent && --s.count <= 0)
        s = HotSlot{};
    syncHand(u);
}
inline void Battle::expeditionHands(Unit& u, double dt, bool fresh)
{
    Kit& k = u.kit;
    const HotSlot& s = k.slots[static_cast<size_t>(std::clamp(k.selected, 0, HotbarSlots - 1))];
    const bool pressed = fresh && u.input.fire && !k.triggerHeld;
    if (fresh)
        k.triggerHeld = u.input.fire;
    if (u.state == UnitState::Launched || u.climb >= 0 || u.carried)
    {
        u.rocketHeld = false;
        u.rocketCharge = 0;
        return;
    }
    if (!handSlot(k.selected) && s.kind == SlotKind::Item)
    {
        if (pressed)
            useGear(u);
        return;
    }
    // Empty hands slap.
    if (u.weapon == FistWeapon)
    {
        if (u.input.fire && u.cooldown <= 0)
            punch(u);
        return;
    }
    if (u.weapon == RocketWeapon)
        return rocketTrigger(u, dt, fresh);
    if (u.input.fire && u.cooldown <= 0 && u.reload <= 0 && u.ammo > 0)
        fire(u);
}

// ---- Hits, saves and falls ----
inline double Battle::expeditionDamage(Unit* attacker, Unit& v, double damage)
{
    if (rawHit)
        return damage;
    Unit* credited = attacker ? creditFor(attacker->id) : nullptr;
    if (v.team == 2 && credited && credited->player())
    {
        Kit& k = credited->kit;
        // Critical hits: 5%, the fountain pen's sharp body +20%; a highlighter mark adds to the critical damage.
        const double crit = .05 + (k.trait == static_cast<int>(PenBiome::Fountain) ? .2 : 0);
        if (random01(rng) < crit)
            damage *= 1.5 + (v.mark > 0 ? MarkBonus : 0);
        if (k.shape == 2)
            damage *= 1.08;
        if (v.stamp > 0)
            damage *= 1 + StampBonus * (bossFoe(v.breed) ? .5 : 1);
        if (v.gold > 0 && random01(rng) < .35)
            scatterLoot(LootKind::Stickers, 0, 1 + static_cast<int>(randomStep(rng) % 2), v.position, -1, 280);
        if (carries(*credited, Gear::Highlighter))
            v.mark = MarkSeconds;
        if (k.trait == static_cast<int>(PenBiome::Watercolor))
            v.wet = std::max(v.wet, 3.);
        if (carries(*credited, Gear::CuttingMat) && !bossFoe(v.breed) && attacker)
            v.impulse += (v.position - attacker->position).normalized() * 110 * (MatPush - 1) * 4;
        // A cinnabar streak crits a third of the time; a dry-brush stroke underfoot adds 15%; a glass floor half again.
        if (k.streak > 0 && random01(rng) < .3)
            damage *= 1.5;
        if (k.dryBrush)
            damage *= 1.15;
        if (expedition.floorPrank == static_cast<int>(FloorPrank::Glass))
            damage *= 1.5;
        return v.kind == UnitKind::Monster ? foeHitTaken(attacker, v, damage) : damage;
    }
    if (v.team == 2 && v.kind == UnitKind::Monster)
        return foeHitTaken(attacker, v, damage);
    if (v.kind == UnitKind::Fighter && v.player())
    {
        Kit& k = v.kit;
        damage = foeHitPlayer(attacker, v, damage);
        if (expedition.floorPrank == static_cast<int>(FloorPrank::Glass))
            damage *= 1.5;
        if (carries(v, Gear::KraftLiner))
            damage *= 1 - LinerCut;
        if (k.shape == 3)
            damage *= .9;
        // The sticky note eats the next lethal or heavy hit and bursts, throwing the nearest enemies off.
        if (k.note && (damage >= v.hp + v.armor || damage >= NoteBlock))
        {
            k.note = false;
            for (auto& m : units)
                if (enemy(m) && (m.position - v.position).length() < 260 + m.radius && !bossFoe(m.breed))
                    m.impulse += (m.position - v.position).normalized() * 700;
            event(EffectStyle::ItemUse, v.position, v.position, v.team, static_cast<double>(Gear::StickyNote), .6);
            return 0;
        }
        if (k.graphite > 0)
        {
            const double soak = std::min(k.graphite, damage);
            k.graphite -= soak;
            damage -= soak;
        }
        if (k.tape > 0)
        {
            const double soak = std::min(k.tape, damage);
            k.tape -= soak;
            damage -= soak;
        }
        // The blotting-paper rifle's shield last.
        if (k.blot > 0)
        {
            const double soak = std::min(k.blot, damage);
            k.blot -= soak;
            damage -= soak;
        }
    }
    return damage;
}
inline bool Battle::cheatDeath(Unit& v)
{
    Kit& k = v.kit;
    // The pencil's eraser-smudge dodge, once a floor: 1 health and a second untouchable.
    if (k.trait == static_cast<int>(PenBiome::Pencil) && !k.pencilSaved)
    {
        k.pencilSaved = true;
        v.hp = 1;
        v.dashTime = 1;
        event(EffectStyle::Dash, v.position, v.position, v.team, v.seat, .5);
        return true;
    }
    // Only what is on the hotbar works (2026-11-17): a charm or a stand-in left in the bag does nothing.
    const int size = HotbarSlots;
    for (int i = 0; i < size; ++i)
        if (HotSlot& s = *kitSlot(v, i); s.kind == SlotKind::Item && s.what == static_cast<int>(Gear::Phoenix) && s.count > 0)
        {
            // The phoenix charm burns in its owner's place (a heap of scrap paper) once a stage.
            s.count = 0;
            v.hp = v.maxHp * PhoenixHealth;
            v.dashTime = 2;
            event(EffectStyle::ItemUse, v.position, v.position, v.team, static_cast<double>(Gear::Phoenix), 1);
            return true;
        }
    for (int i = 0; i < size; ++i)
        if (HotSlot& s = *kitSlot(v, i); s.kind == SlotKind::Item && s.what == static_cast<int>(Gear::StandIn) && s.count > 0)
        {
            if (--s.count <= 0)
                s = HotSlot{};
            v.hp = v.maxHp * .5;
            v.dashTime = 1.5;
            syncHand(v);
            event(EffectStyle::ItemUse, v.position, v.position, v.team, static_cast<double>(Gear::StandIn), 1);
            return true;
        }
    return false;
}
inline void Battle::expeditionFall(Unit& v)
{
    Kit& k = v.kit;
    stowHand(v);
    ++expedition.falls;
    k.lost = k.pouch;
    // Everything goes to the floor where they fell (design 4.4): the pouch in a few piles, every gun (a pair as two),
    // every item, every look (each remembers whose it is).
    if (k.pouch > 0)
    {
        const int piles = std::clamp(k.pouch / 20, 1, 3);
        for (int p = 0; p < piles; ++p)
            scatterLoot(LootKind::Stickers, 0, k.pouch / piles + (p < k.pouch % piles ? 1 : 0), v.position, -1, 420);
    }
    k.pouch = 0;
    k.album = 0;
    if (k.coins > 0)
    {
        const int piles = std::clamp(k.coins / 100, 1, 3);
        for (int p = 0; p < piles; ++p)
            scatterLoot(LootKind::Coins, 0, k.coins / piles + (p < k.coins % piles ? 1 : 0), v.position, -1, 420);
    }
    k.coins = 0;
    auto spill = [&](HotSlot& s)
    {
        if (s.kind == SlotKind::Weapon)
        {
            const int guns = std::max(1, s.count);
            for (int g = 0; g < guns; ++g)
                scatterLoot(LootKind::Weapon, s.what, std::min(gunMagazine(s.what, s.variant, false), s.ammo / guns), v.position, -1, 380, s.variant);
        }
        else if (s.kind == SlotKind::Item)
            scatterLoot(LootKind::Item, s.what, s.count, v.position, -1, 380);
        s = HotSlot{};
    };
    for (auto& s : k.slots)
        spill(s);
    for (auto& s : k.pack)
        spill(s);
    for (int c = 0; c < WearSlots; ++c)
        if (k.wear[static_cast<size_t>(c)] > 0)
            scatterLoot(LootKind::Look, lookId(c, k.wear[static_cast<size_t>(c)]), 1, v.position,
                        k.wearOwner[static_cast<size_t>(c)] >= 0 ? k.wearOwner[static_cast<size_t>(c)] : v.seat, 340);
    k.wear.fill(-1);
    k.wearOwner.fill(-1);
    k.ghost = true;
    k.trait = -1;
    k.shape = 0;
    k.scale = k.hpScale = k.speedScale = 1;
    k.note = false;
    k.tape = k.graphite = k.heal = k.healTime = k.blot = 0;
    k.bleed = k.marked = k.sealed = k.smear = k.hurt = 0;
    k.selected = 0;
    k.hold = 0;
    k.holdTarget = -1;
    v.weapon = FistWeapon;
    v.dual = false;
    v.ammo = 1;
    v.radius = 25;
    event(EffectStyle::Scatter, v.position, v.position, v.team, v.seat, 1);
}
inline bool Battle::reviveDrawn(int id, int target, const std::vector<Vec2>& points, int pen)
{
    Unit* p = find(id);
    Unit* t = find(target);
    if (!expeditionMode() || finished || !p || !t || !p->human || !p->alive || p->kit.extracted || !t->human || t->alive || !t->kit.ghost ||
        t->kit.extracted || t->id == p->id)
        return false;
    if (pen < 0 || pen >= BiomeCount || !((revivePens(id) >> pen) & 1u))
        return false;
    // The revive kit draws from the hotbar only (the bag only stores, 2026-11-17). Without one a teammate can still draw
    // the fallen back (2026-11-17, second playtest: too few ways back), for KitlessReviveInk times the ink.
    int kitIndex = -1;
    for (int i = HotbarSlots - 1; i >= 0; --i)
        if (const HotSlot* s = kitSlot(*p, i); s && s->kind == SlotKind::Item && s->what == static_cast<int>(Gear::ReviveKit))
            kitIndex = i;
    auto deny = [&](int reason)
    {
        Effect e{p->position, p->position, static_cast<double>(reason), .6, EventEffect, p->team};
        e.style = EffectStyle::Denied;
        e.pulse = p->id;
        addEffect(e);
        return false;
    };
    const ReviveShape shape = measureDrawing(points);
    if (!shape.closed || shape.share < ReviveMinShare)
        return deny(detail::DenyDrawing);
    const int ink = static_cast<int>(std::lround(reviveInk(shape.share) * (kitIndex >= 0 ? 1. : KitlessReviveInk)));
    if (p->ink + 1e-9 < ink)
        return deny(detail::DenyInk);
    p->ink -= ink;
    if (kitIndex >= 0)
    {
        HotSlot& kit = *kitSlot(*p, kitIndex);
        if (--kit.count <= 0)
            kit = HotSlot{};
    }
    syncHand(*p);
    // The drawing is the body (design 5.2): its area sets the size, health and speed, its pen the trait.
    Kit& k = t->kit;
    k.ghost = false;
    k.trait = pen;
    k.shape = shape.bonus;
    k.scale = reviveScale(shape.share);
    k.hpScale = reviveHealth(shape.share);
    k.speedScale = reviveSpeed(shape.share);
    k.selected = 0;
    k.pencilSaved = false;
    k.traitClock = 0;
    t->alive = true;
    t->radius = 25 * k.scale;
    t->maxHp = healthCapacity(*t);
    t->hp = t->maxHp;
    k.graphite = pen == static_cast<int>(PenBiome::Pencil) ? t->maxHp * .25 : 0;
    t->armor = 0;
    syncHand(*t);
    t->reload = t->cooldown = 0;
    const Vec2 side{-p->aim.y, p->aim.x};
    t->position = maze.move(p->position, side * (p->radius + t->radius + 24), t->radius);
    t->velocity = t->impulse = {};
    t->state = UnitState::Free;
    t->followerOf = -1;
    t->carried = false;
    t->bait = 0;
    t->input = {};
    t->moveQueue.clear();
    t->trail.clear();
    t->path.clear();
    t->dashTime = 1;
    ++p->stats.revives;
    p->stats.feats |= featBit(Feat::Revive);
    if (shape.share < .1)
        p->stats.feats |= featBit(Feat::TinyRevive);
    if (shape.bonus >= 1 && shape.bonus <= 3)
        p->stats.shapes = static_cast<uint8_t>(p->stats.shapes | (1u << (shape.bonus - 1)));
    if ((p->kit.borrowed >> pen) & 1u)
    {
        static const PenBiome Own[3] = {PenBiome::Fountain, PenBiome::Brush, PenBiome::Watercolor};
        if (pen != static_cast<int>(expedition.biome()) && pen != static_cast<int>(Own[std::clamp(p->perks.pen, 0, 2)]))
            p->stats.feats |= featBit(Feat::BorrowedPen);
    }
    event(EffectStyle::DrawRevive, t->position, p->position, t->team, k.scale, 1.2);
    return true;
}

// ---- The step ----
inline void Battle::updateExpedition(double dt)
{
    if (!expeditionMode() || finished)
        return;
    for (auto& u : units)
    {
        u.stun = std::max(0., u.stun - dt);
        u.stamp = std::max(0., u.stamp - dt);
        u.gold = std::max(0., u.gold - dt);
        u.mark = std::max(0., u.mark - dt);
        u.wet = std::max(0., u.wet - dt);
    }
    // Burning enemies (a wax seal's burst, a red crayon round) lose health in bites, twice a second.
    for (size_t i = 0; i < units.size(); ++i)
    {
        Unit& u = units[i];
        if (!u.alive || u.kind != UnitKind::Monster || u.burnTime <= 0)
            continue;
        u.burnTime = std::max(0., u.burnTime - dt);
        if ((u.burnTick += dt) >= .5 || u.burnTime <= 0)
        {
            damageUnit(u.burnOwner, u.id, u.burnDamage * std::min(.5, u.burnTick));
            u.burnTick = 0;
        }
    }
    // Bleeding enemies (a staple or a thumbtack stuck in them) lose a point of health a second.
    for (size_t i = 0; i < units.size(); ++i)
    {
        Unit& u = units[i];
        if (!u.alive || u.kind != UnitKind::Monster || u.bleedTime <= 0)
            continue;
        const double step = std::min(dt, u.bleedTime);
        u.bleedTime -= step;
        if ((u.bleedTick += step) >= 1 - 1e-9 || u.bleedTime <= 1e-9)
        {
            const double bite = SpikeDps * u.bleedTick;
            u.bleedTick = 0;
            // A flat point a second (the user's numbers): no armour, no critical hits, nothing a foe's body shrugs off.
            rawHit = true;
            damageUnit(u.bleedOwner, u.id, bite);
            rawHit = false;
        }
    }
    updateKits(dt);
    updateLoot(dt);
    updateHazards(dt);
    updateZones(dt);
    updateEvents(dt);
    const int floorsBefore = expedition.floorsCleared;
    updateRooms(dt);
    if (expedition.floorsCleared != floorsBefore)
        return;
    // The run ends when nobody is left standing in the dungeon: those who got out keep their pouch, a wipe keeps nothing.
    bool standing = false, anyone = false;
    for (const auto& u : units)
        if (u.human)
        {
            anyone = true;
            standing |= u.alive && !u.kit.extracted;
        }
    if (anyone && !standing)
    {
        finished = true;
        winner = expedition.extracted > 0 ? 1 : 2;
        // A wipe keeps nothing: whoever lost 300 or more when they fell has earned their sorrow.
        if (winner == 2)
            for (auto& u : units)
                if (u.human && u.kit.lost >= 300)
                    u.stats.feats |= featBit(Feat::WipedRich);
    }
}
inline void Battle::partyFeat(Feat feat)
{
    for (auto& u : units)
        if (u.human && u.alive && !u.kit.extracted)
            u.stats.feats |= featBit(feat);
}
} // namespace gwgame
