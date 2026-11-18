#pragma once
#include "Hub.h"
#include "Foes.h"
#include "Meta.h"
#include "Snapshot.h"
namespace gwgame
{
// One simulation, several rule sets (config.rules). Survival: up to ten players hold out against
// monster waves on an open arena, share one team level and pick skills. Defense: players draw a maze
// in ink, pin polygon turrets and fight on the field while waves march on the core. Hub: the room between
// missions, with stations in its walls and a training corner (targets, a bullseye, triangles to throw).
class Battle
{
public:
    BattleConfig config;
    Maze maze;
    DefenseState defense;
    ExpeditionState expedition;
    HubState hub;
    std::vector<Unit> units;
    std::vector<Projectile> projectiles;
    std::vector<Crate> crates;
    std::vector<Effect> effects;
    std::vector<DamageNumber> damageNumbers;
    double elapsed = 0;
    bool finished = false;
    int winner = 0, sharedLevel = 1;
    double sharedXP = 0;
    explicit Battle(BattleConfig c = {}) : config(c)
    {
        units.reserve(512);
        projectiles.reserve(1024);
    }
    bool start(uint64_t seed);
    // Opens a line-defense page (index into defenseLevels()) in its first build phase.
    bool startDefense(uint64_t seed, int level);
    // A page that is not on the board (tests: rules checked on a page of their own).
    bool startDefensePage(uint64_t seed, const DefenseLevel& page);
    // Opens the hub sheet: stations, training targets and a few triangles; it never ends.
    bool startHub(uint64_t seed);
    // Opens the tutorial page (tutorialPage()) at its first step; a defense page with a script.
    bool startTutorial(uint64_t seed);
    // Opens an expedition (ExpeditionRules.inl, design v2.1): stage 1's first floor, its start room, empty pouches.
    bool startExpedition(uint64_t seed);
    bool expeditionMode() const
    {
        return config.rules == RuleSet::Expedition;
    }
    // E on the expedition: picks up the weapon, item or look underfoot, buys from a shelf, opens a chest or works an
    // event machine. Returns what it did (1 picked up, 2 bought, 3 a machine, 4 a chest, 5 the shop's buyer: the trade
    // sheet opens on that player's screen), 0 when nothing was in reach, -1 when something was but refused (a full kit, a
    // short purse: a cue tells the player why).
    int interact(int id);
    // The hotbar: 1-5 (or the wheel) pick a slot; G drops what is in hand (one gun of a pair); stickers and looks can
    // be dropped for a teammate. Drops lie on the floor for anyone to take.
    bool selectSlot(int id, int slot);
    bool dropSlot(int id);
    int dropStickers(int id, int amount);
    bool dropLook(int id, int category);
    // The backpack (2026-11-16): kit indices 0-4 are the hotbar, 5 on the bag's open slots. Tab's sheet moves a slot onto
    // another (pairs and stacks merge, the rest swap), drops one whole, and drops coins for a teammate; the shop's buyer
    // takes a slot for coins (sellSlot returns them, -1 when refused or no buyer is in reach).
    bool moveSlot(int id, int from, int to);
    bool dropKitSlot(int id, int index);
    int dropCoins(int id, int amount);
    int sellSlot(int id, int index);
    // The bag's open slots (PackBase + the backpack rank) and the whole kit's (the hotbar's five on top).
    int packSlots(const Unit& u) const;
    int kitSize(const Unit& u) const;
    HotSlot* kitSlot(Unit& u, int index);
    // What a player can spend in stickers (the pouch and every ten coins), and paying a price (coins first).
    int purse(const Unit& u) const;
    void pay(Unit& u, int price);
    // The quality gun in hand (-1 an armoury gun or none): a long gun's, or the left pistol's.
    int handVariant(const Unit& u) const;
    // The guns in hand while the hands are up (2026-11-17: a long gun, or one or two pistols); how many.
    int handGuns(const Unit& u, const HotSlot* guns[2]) const;
    // Takes up what the selected slot holds (the guns in hand or an item), keeping the hands lawful.
    void syncHand(Unit& u);
    // Throws loot onto the floor at a point (amount: stickers, ink or coins in a pile, a gun's rounds, an item's charges).
    // hop > 0 tosses it up on an arc that high (an opened chest's contents). stream: the generator its scatter draws on
    // (the run's own when null; hearts bring a side stream).
    void scatterLoot(LootKind kind, int what, int amount, Vec2 at, int owner = -1, double speed = 380, int variant = -1, double hop = 0,
                     uint64_t* stream = nullptr);
    // A generator of its own for the hearts (2026-11-17), seeded from the run, the loot dropped so far and a salt: hearts
    // never draw on the run's generator, so every other drop (the economy as it was tuned) stays exactly as it was.
    uint64_t sideStream(uint64_t salt) const;
    // A revive kit used on a fallen teammate (ghost): the drawing's points in the sheet's unit square and the pen it was
    // drawn with (PenBiome). Its closed area decides the ink and the body that stands up (design 5.2).
    bool reviveDrawn(int id, int target, const std::vector<Vec2>& points, int pen);
    // The pens a player may draw a revive with: their own pen's biome, the stage's, and any borrowed (a bit each).
    uint32_t revivePens(int id) const;
    // The room a point stands in (-1 in a corridor or outside the floor).
    int roomAt(Vec2 p) const;
    // Opens the given floor of the run in progress at once, everyone at its start (tests, scene captures).
    bool expeditionJump(int stage, int floor);
    // One more of the stage's foes in a room (an elite on a fifth of the rolls), where a wave would put it: the 8-player
    // stress run keeps a boss room full with it. Its id, or -1 (not the expedition, or the room holds MaxRoomEnemies).
    int expeditionReinforce(int room);
    bool carries(const Unit& u, Gear gear) const;
    // How fast a player walks relative to the rules' speed (research, a revive drawing's body, looks, a pair of
    // machine guns); a gun's magazine and reload for this player (pairs, the spring refill, the ballpoint).
    double moveScale(const Unit& u) const;
    int clipSize(const Unit& u) const;
    double reloadTime(const Unit& u) const;
    bool defenseMode() const
    {
        return config.rules == RuleSet::Defense;
    }
    bool hubMode() const
    {
        return config.rules == RuleSet::Hub;
    }
    bool survivalMode() const
    {
        return config.rules == RuleSet::Survival;
    }
    bool tutorialMode() const
    {
        return defenseMode() && defense.tutorial;
    }
    TutorialStep tutorialStep() const
    {
        return static_cast<TutorialStep>(std::clamp(defense.step, 0, TutorialStepCount));
    }
    TutorialProgress tutorialProgress() const;
    // The three cells the drawing step suggests (a hint: any three walls finish the step).
    std::array<int, 3> tutorialWall() const;
    // Units that make up a wave (training targets never do).
    static bool enemy(const Unit& u)
    {
        return u.alive && u.kind == UnitKind::Monster && !u.dummy;
    }
    // Draws a wall on an empty paper cell, or repairs a damaged one, paying ink. Refused when the
    // cell is rock, outside the paper, occupied, unaffordable, or would seal every route to the core.
    bool paintWall(int id, int cell);
    // Removes one of the drawn walls (and the tower on it) and refunds ink for the material that is left.
    bool eraseWall(int id, int cell);
    // Ink a paint would cost right now; negative when the paint would be refused (-2 it would seal the core off,
    // -3 someone stands there, -4 a ladder's foot is there).
    double paintCost(int id, int cell) const;
    // The box (DefenseRules.inl). A tower raised on one of the drawn blocks, or one more side on the tower already
    // there, paying ink. Refused off a sound block (-5: repair it first), past six sides or beyond the ink left.
    bool buildTower(int id, int cell, int kind = 0);
    // Ink the tower (or its next side) on this cell would cost now; negative when refused (0 with a tower kit).
    double towerCost(int id, int cell, int kind = 0) const;
    // The tower standing on a cell, or -1.
    int towerAt(int cell) const;
    // A ladder from a platform cell down the box wall to the floor beside it (-6: the floor there is not open),
    // and erasing one (its ink comes back). ladderAt: the index of the ladder on a platform cell, or -1.
    bool buildLadder(int id, int deckCell);
    bool eraseLadder(int id, int deckCell);
    double ladderCost(int id, int deckCell) const;
    int ladderAt(int deckCell) const;
    // The fallen teammate whose outline a player stands beside up on the platform (-1 when none), and bringing them
    // back at once for config.reviveInk (E does it; refused with a cue when the ink is short).
    int fallenNear(int id) const;
    bool reviveMate(int id);
    // The last triangle in a player's tail merges into the polygon in front of it (one more side).
    int merge(int id);
    // During the build phase any player may call the next wave early.
    bool callWave(int id);
    // Puts one enemy of the current wave on the page at an entrance (tests and network stress runs; real
    // waves come from their spawn queue). Returns its id, or -1.
    int spawnWaveEnemy(Shape kind, bool elite, int entrance = 0, bool raid = false);
    // Barbed wire left in a cell (0 none, 1 new).
    double wireAt(int cell) const
    {
        return cell >= 0 && cell < static_cast<int>(defense.wire.size()) ? defense.wire[cell] : 0;
    }
    int joinHuman(int owner);
    // An AI teammate that fights, levels and picks skills by itself. Returns its unit id.
    int addBot();
    std::vector<int> tailIds(int head) const
    {
        std::vector<int> result;
        for (const auto& u : units)
            if (u.alive && u.followerOf == head && u.state == UnitState::Following)
                result.push_back(u.id);
        std::sort(result.begin(), result.end(), [&](int a, int b) { return find(a)->tailOrder < find(b)->tailOrder; });
        return result;
    }
    void leaveHuman(int owner);
    Unit* find(int id)
    {
        for (auto& u : units)
            if (u.id == id)
                return &u;
        return nullptr;
    }
    const Unit* find(int id) const
    {
        for (auto& u : units)
            if (u.id == id)
                return &u;
        return nullptr;
    }
    int humanCount() const
    {
        int n = 0;
        for (auto& u : units)
            if (u.human)
                ++n;
        return n;
    }
    bool setInput(int id, Input input);
    bool joinSquad(int member, int leader);
    bool pickupWeapon(int id, int weapon);
    bool upgradePaused() const
    {
        if (finished)
            return false;
        for (const auto& u : units)
            if (u.alive && u.human && u.pendingUpgrades > 0)
                return true;
        return false;
    }
    // The nearest ally or grabbable teammate E would take (-1 when none is in reach).
    int candidate(int id) const;
    // E: recruit the candidate (a teammate is grabbed into the tail), leave a squad, wriggle out of a grip,
    // or roll out of bait once the landing has settled.
    int recruit(int id);
    // Q: throws the teammate in hand first, otherwise the ally nearest the head.
    int launch(int id);
    // Whether holder may grab target into their tail under the room's trolling level.
    bool canGrab(const Unit& holder, const Unit& target) const;
    // The teammate a player is carrying, or -1.
    int carrying(int id) const;
    // A player's own choice to be throwable in Friendly rooms (Chaos ignores it, Off forbids every throw).
    bool setThrowable(int id, bool allowed);
    bool roll(int id);
    // Defense: moves up to amount of ink from one player to a teammate in reach; returns what moved.
    double pourInk(int from, int to, double amount);
    // Meta progression (MetaRules.inl). A player's profile report, as the host accepted it: research ranks, the
    // starting weapon, the pen and the hat. Every rank counts for its own player (perk). Survival ignores it all.
    bool setLoadout(int id, const Loadout& loadout);
    // The single-mission items a player brought, applied once in a defense page's first build phase. Returns
    // the items applied, slot by slot (-1 where nothing was).
    ItemSlotsArray useItems(int id, const ItemSlotsArray& items);
    // The room's difficulty tier: caps every player's ranks and toughens the next waves.
    void setTier(int tier);
    // The bonus an attribute gives this unit in this room (1 = none): its rank, capped by the tier.
    double perk(const Unit& u, Attribute a) const;
    int effectiveRankOf(const Unit& u, Attribute a) const;
    double healthCapacity(const Unit& u) const;
    double inkCapacity(const Unit& u) const;
    // How much harder enemies hit on this tier (defense pages only; 1 elsewhere).
    double enemyPower() const;
    // Ink a player pays for a full new cell with their pen.
    double cellInk(const Unit& u, WallKind kind) const;
    bool beginCharge(int id);
    int releaseCharge(int id, Vec2 aim);
    void cancelCharge(int id);
    void emitDamage(int source, int victim, Vec2 position, double amount, int team = 0);
    // Rocket launcher (BattleCombat.inl): the shell a release fires at power 0..1 of a full charge.
    void lob(Unit& u, double power);
    // One pull of the trigger with the gun in hand (BattleCombat.inl; the step calls it, tests may too).
    void fire(Unit& u);
    // Bounded and uniquely numbered so replicated clients can trigger each cue exactly once.
    void addEffect(Effect e)
    {
        e.id = nextEffect++;
        if (effects.size() >= MaxEffects)
            effects.erase(effects.begin());
        effects.push_back(e);
    }
    void event(EffectStyle style, Vec2 from, Vec2 to, int team, double radius = 0, double lifetime = .35)
    {
        Effect e{from, to, radius, lifetime, EventEffect, team};
        e.style = style;
        addEffect(e);
    }
    static constexpr size_t MaxEffects = 256;
    bool chooseUpgrade(int id, int choice);
    void addExperience(double amount);
    // friendly: a rocket's blast is the one thing that hurts the shooter's own team (by the trolling level).
    void damageUnit(int attacker, int victim, double damage, bool friendly = false);
    // Saving copies the whole run; players who have not returned since a load stay in the copy.
    RunSnapshot snapshot() const;
    // Restored players wait as reservations until their identity joins again (see joinHuman).
    bool restore(const RunSnapshot& saved);
    bool reserved(int owner) const
    {
        return dormant.count(owner) > 0;
    }
    void step(double dt)
    {
        if (upgradePaused())
        {
            accumulator = 0;
            return;
        }
        if (!std::isfinite(dt) || dt <= 0)
            return;
        accumulator += std::min(dt, .25);
        while (accumulator + 1e-9 >= FixedStep && !finished && !upgradePaused())
        {
            tick(FixedStep);
            accumulator -= FixedStep;
        }
    }
    void advance(double seconds)
    {
        if (!std::isfinite(seconds) || seconds <= 0)
            return;
        for (double left = seconds; left > 1e-8 && !finished && !upgradePaused();)
        {
            double dt = std::min(FixedStep, left);
            tick(dt);
            left -= dt;
        }
    }

private:
    // A saved player's character and their squad, parked until that identity joins again.
    struct Dormant
    {
        Unit player;
        std::vector<Unit> squad;
    };
    std::unordered_map<int, Dormant> dormant;
    uint32_t nextTailOrder = 1;
    uint32_t nextDamageNumber = 1;
    uint32_t nextEffect = 1;
    static constexpr double FixedStep = 1. / 60;
    double accumulator = 0, nextCrate = 20, nextRecruit = 3, nextWave = 1, nextBoss = BossSchedule[0];
    int bossWave = 0;
    uint64_t rng = 1;
    int nextId = 1, nextProjectile = 1;
    uint32_t navigationVersion = 0;
    void tick(double dt);
    void equip(Unit& u, int weapon);
    void syncSquadWeapon(Unit& member, const Unit& leader);
    void moveUnits(double dt);
    void updateAI(Unit& u, double dt);
    void updateBot(Unit& u, double dt);
    void updateWeapons(double dt);
    void updateProjectiles(double dt);
    void updateEvolution(double dt);
    void updateWire(double dt);
    void fireMortar(Unit& u);
    void fireTesla(Unit& u);
    int towerTarget(const Unit& u, double range, bool needSight) const;
    void startDodge(Unit& u);
    // The expedition (ExpeditionRules.inl).
    void updateExpedition(double dt);
    void enterFloor();
    void nextFloor();
    void stockRoom(int index, uint64_t& roll);
    Vec2 roomCenter(const DungeonRoom& room) const;
    Vec2 floorSpawn(int index) const;
    Vec2 roomSpot(const DungeonRoom& room, double awayFromPlayers);
    void setDoors(DungeonRoom& room, bool shut);
    void lockRoom(int index);
    void spawnWave(int index);
    void clearRoom(int index);
    // The bestiary (FoeRules.inl): spawning and picking foes, each one's step, the bosses', and their shots and warnings.
    int spawnFoe(Foe foe, int room, Vec2 at);
    Foe pickFoe(bool elite);
    void updateFoe(Unit& u, double dt);
    void updateBoss(Unit& u, Unit& target, double dt);
    Unit* foeTarget(Unit& u);
    void foeSteer(Unit& u, Vec2 goal, double speed, double dt);
    void foeKeep(Unit& u, const Unit& target, double nearest, double farthest, double speed, double dt);
    void foeFlee(Unit& u, Vec2 from, double reach, double speed, double dt);
    const Unit* stubWard(const Unit& u) const;
    bool foeDash(Unit& u, Vec2 heading, double speed, double damage, int effect, double dt);
    void foeShot(int owner, Vec2 from, Vec2 dir, double damage, int kind, double speed, double range, int effect = 0, int pierce = 0);
    void foeFan(Unit& u, Vec2 at, int count, double spread, double damage, int kind, double speed, double range, int effect = 0);
    void foeRing(int owner, Vec2 at, double from, int count, double offset, double damage, int kind, double speed, double range, int effect);
    void foeLob(Unit& u, Vec2 at, double damage, double flight, int effect);
    void foeCue(Vec2 from, Vec2 to, double radius, double seconds, int colour);
    void foeVanish(Unit& u);
    bool nearFoe(const Unit& u, Foe foe, double reach) const;
    int addHazard(ZoneKind kind, Vec2 from, Vec2 to, double radius, double seconds, int what = 0, double dps = 0, int owner = -1);
    void updateHazards(double dt);
    void layTerrain(int room, uint64_t& roll);
    void popSacs(const Projectile& p, Vec2 from, Vec2 to);
    // Hooks: a hit on a foe, a foe's hit on a player, a player's shot meeting a foe (0 lands, 1 lost, 2 turned back), an
    // enemy shot or a nib's shot landing, a lobbed blob coming down, a foe falling.
    double foeHitTaken(Unit* attacker, Unit& victim, double damage);
    double foeHitPlayer(Unit* attacker, Unit& victim, double damage);
    int foeDeflect(Projectile& p, Unit& target, Vec2 at);
    void expeditionShotHit(const Projectile& p, Unit& target);
    void foeLobLanded(const Projectile& p);
    void foeDeath(const Unit& dead, int attacker);
    // The run's feats for every player still in it (a boss down, a floor cleared...).
    void partyFeat(Feat feat);
    void updateRooms(double dt);
    void updateEvents(double dt);
    void updateLoot(double dt);
    void updateKits(double dt);
    void updateZones(double dt);
    void expeditionKill(const Unit& dead, int attacker);
    void bossDown(const Unit& boss);
    double expeditionDamage(Unit* attacker, Unit& victim, double damage);
    bool cheatDeath(Unit& v);
    void expeditionFall(Unit& v);
    bool stow(Unit& u, LootKind kind, int what, int amount, int variant = -1);
    void stowHand(Unit& u);
    void normalizeHands(Unit& u);
    bool takeIntoHands(Unit& u, int bagIndex, int hand);
    // Ink mending (R): every player holding R pours ink into their own wounds or a teammate's (2026-11-17).
    void updateMend(double dt);
    void expeditionHands(Unit& u, double dt, bool fresh);
    void punch(Unit& u);
    void useGear(Unit& u);
    void erase(Unit& u);
    void extract(Unit& u, bool final);
    void openChest(Chest& chest, Unit& opener);
    void chestLoot(Vec2 at, int grade, int quality);
    Chest& addChest(int room, Vec2 at, ChestGrade grade, int quality);
    Vec2 clearSpot(int room, Vec2 want);
    int variantOf(int quality);
    void dropPiece(Vec2 at, int quality, double hop = 0);
    int randomWeapon();
    int randomGear(int family);
    int randomLook();
    bool service(Unit& u, Offer& offer);
    int livingParty() const;
    // How many more stickers each kill, fight and boss pays for the party's size (1 solo, up to 4 for eight).
    double shareScale() const;
    // Healing on the expedition goes through here (the fountain pen's body takes a little less).
    void heal(Unit& u, double amount);
    // Set while the eraser cuts: its damage is exact (no critical hits, stamps or shields in between).
    bool rawHit = false;
    void resetRun();
    // Builds a defense-style page (walls, entrances, core) from its rows; shared by levels and the tutorial.
    bool openPage(uint64_t seed, const DefenseLevel& page, int level);
    void updateHub(double dt);
    void updateTutorial(double dt);
    void placeTutorialWall();
    void clearTutorialWall();
    void reviveDummies(double dt);
    int spawnDummy(Vec2 at);
    // Keeps at least count idle triangles within reach of near (the hub corner, the tutorial player).
    void supplyTriangles(Vec2 near, double reach, int count, double spread);
    // A thrown unit has just come to rest (the hub's bullseye counts it).
    void landed(Unit& u);
    Vec2 hubSpawn(int index) const;
    void updateDefense(double dt);
    void beginWave();
    void clearWave();
    void rebuildRoutes();
    void collapseWalls();
    void spawnEnemy(const SpawnOrder& order);
    void updateSiege(Unit& u, double dt);
    Vec2 siegeTarget(const Unit& u, bool ram, int& wallAhead) const;
    void hurtCore(double amount, Vec2 at);
    void dropLoot(const Unit& dead, int attacker);
    void spawnTriangle(Vec2 near, double spread);
    void revive(Unit& u);
    Vec2 coreSpawn(int index) const;
    // The box (DefenseRules.inl, BoxRules.inl): where players are born on the platform, where a fallen player's
    // outline waits, a refused build's cue, towers that lost their block, ladders, climbing and the platform's AI.
    Vec2 deckSpawn(int index) const;
    Vec2 reviveSpot(Vec2 near) const;
    bool refuseCell(Unit& p, int cell, int reason);
    void dropTowers();
    bool wantsLadder(const Unit& u, int ladder) const;
    static constexpr double LadderPull = 450;
    void startClimb(Unit& u, int ladder, bool up);
    void updateClimb(Unit& u, double dt);
    bool tryLadder(Unit& u, Vec2 steer);
    void settle(Unit& u);
    Vec2 deckToward(const Unit& u, Vec2 goal) const;
    void hopDown(Unit& u);
    void updateDeckMonster(Unit& u, double dt);
    void updateDeckBot(Unit& u, double dt);
    void aimRocket(Unit& u, Vec2 target);
    // The rocket's trigger (hold to charge, release to fire) and its shell bursting where it lands.
    void rocketTrigger(Unit& u, double dt, bool fresh);
    void rocketBlast(int owner, int team, Vec2 at, double damage, double reach = RocketBlast, double burn = 0);
    void plantTurret(Unit& u);
    void fireTurret(Unit& u);
    void fireBolt(Unit& u, Vec2 target, double damage, int weapon, double speed, double range, bool high = false);
    void shatter(Unit& thrown, Vec2 at);
    void fireLaser(Unit& boss);
    double meleeDamage(const Unit& u) const;
    // How hard an expedition enemy hits (stage, floor, tier and elite on top of its base).
    double expeditionPower(const Unit& u) const;
    // New units wait here until the current update pass is over; flushing recycles dead slots first.
    std::vector<Unit> pendingUnits;
    void queueUnit(Unit u);
    void flushUnits();
    void releaseFollowers(int id);
    // Throwing teammates: the grab itself, leaving a tail, and the bait a monster chases (recounted per tick).
    void grab(Unit& holder, Unit& target);
    void leaveTail(Unit& member);
    bool throwableTail(int id) const;
    std::vector<int> baits;
    const Unit* baitFor(const Unit& monster, double reach) const;
    void chase(Unit& monster, const Unit* bait);
    // The player credited for damage dealt by a unit (itself, or the player whose tail it rides in).
    Unit* creditFor(int attacker);
    // Health and ink capacities follow ranks and tier; a full tank stays full, otherwise the share is kept.
    void refreshCapacities(Unit& u);
    int freeSeat() const;
    // A player's queued command for this step: acknowledged and aimed even while they cannot walk.
    void consumeInput(Unit& u);
    bool lineOfFireBlocked(const Unit& shooter, Vec2 target) const;
    void shove(const Projectile& p, Unit& target, Vec2 at);
    // layer: -1 every level, 0 the floor of the box only, 1 the platform only.
    void blast(int owner, int team, Vec2 position, double radius, double damage, double lifetime = .35,
               EffectStyle style = EffectStyle::None, int layer = -1);
    void spawnMonster(bool boss = false);
    void makeChoices(Unit& u);
    void botChoose(Unit& u);
    int squadHead(const Unit& u) const
    {
        return u.state == UnitState::Following && u.followerOf >= 0 ? u.followerOf : u.id;
    }
    Vec2 spawnPosition(int index = 0) const
    {
        return Vec2{maze.sizeX() * .5, maze.sizeY() * .5} + Vec2{(index % 5 - 2) * 62., (index / 5 - 2) * 62.};
    }
};
} // namespace gwgame
#include "BattleCombat.inl"
#include "BattleMovement.inl"
#include "BattleRules.inl"
#include "Evolution.inl"
#include "Squads.inl"
#include "Bots.inl"
#include "Polygons.inl"
#include "Pranks.inl"
#include "DefenseRules.inl"
#include "BoxRules.inl"
#include "HubRules.inl"
#include "MetaRules.inl"
#include "ExpeditionRules.inl"
#include "FoeRules.inl"
#include "SnapshotIO.inl"
