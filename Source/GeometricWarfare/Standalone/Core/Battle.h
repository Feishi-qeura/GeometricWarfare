#pragma once
#include "Hub.h"
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
    // Opens the hub sheet: stations, training targets and a few triangles; it never ends.
    bool startHub(uint64_t seed);
    // Opens the tutorial page (tutorialPage()) at its first step; a defense page with a script.
    bool startTutorial(uint64_t seed);
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
    // Removes one of the drawn walls and refunds ink for the material that is left.
    bool eraseWall(int id, int cell);
    // Ink a paint would cost right now; negative when the paint would be refused.
    double paintCost(int id, int cell) const;
    // The last triangle in a player's tail merges into the polygon in front of it (one more side).
    int merge(int id);
    // During the build phase any player may call the next wave early.
    bool callWave(int id);
    // Puts one enemy of the current wave on the page at an entrance (tests and network stress runs; real
    // waves come from their spawn queue). Returns its id, or -1.
    int spawnWaveEnemy(Shape kind, bool elite, int entrance = 0);
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
    // starting weapon, the pen and the hat. Ranks count up to the room's tier (perk). Survival ignores it all.
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
    void damageUnit(int attacker, int victim, double damage);
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
    void dropLoot(const Unit& dead);
    void spawnTriangle(Vec2 near, double spread);
    void revive(Unit& u);
    Vec2 coreSpawn(int index) const;
    void plantTurret(Unit& u);
    void fireTurret(Unit& u);
    void fireBolt(Unit& u, Vec2 target, double damage, int weapon, double speed, double range);
    void shatter(Unit& thrown, Vec2 at);
    void fireLaser(Unit& boss);
    double meleeDamage(const Unit& u) const;
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
    void fire(Unit& u);
    bool lineOfFireBlocked(const Unit& shooter, Vec2 target) const;
    void shove(const Projectile& p, Unit& target, Vec2 at);
    void blast(int owner, int team, Vec2 position, double radius, double damage, double lifetime = .35,
               EffectStyle style = EffectStyle::None);
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
#include "HubRules.inl"
#include "MetaRules.inl"
#include "SnapshotIO.inl"
