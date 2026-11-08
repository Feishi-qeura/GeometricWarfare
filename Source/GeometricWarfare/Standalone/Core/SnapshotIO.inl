#pragma once
namespace gwgame
{
inline RunSnapshot Battle::snapshot() const
{
    RunSnapshot s;
    s.seed = maze.seed;
    s.elapsed = elapsed;
    s.sharedLevel = sharedLevel;
    s.sharedXP = sharedXP;
    s.bossWave = bossWave;
    s.mazeVersion = maze.version;
    s.walls = maze.walls;
    for (const auto& [id, patch] : maze.fragments)
        s.fragments.emplace_back(id, patch);
    std::sort(s.fragments.begin(), s.fragments.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    auto save = [&](const Unit& u, int owner)
    {
        SavedUnit r;
        r.id = u.id;
        r.team = u.team;
        r.owner = owner;
        // A carried teammate is set down when the run is saved; thrown and waiting companions settle.
        const bool follows = u.state == UnitState::Following && !u.carried;
        r.followerOf = follows ? u.followerOf : -1;
        r.burnOwner = u.burnOwner;
        r.kind = u.kind;
        r.state = follows ? UnitState::Following
                  : u.kind == UnitKind::Companion ? UnitState::Idle
                                                   : UnitState::Free;
        r.shape = u.shape;
        r.alive = u.alive;
        r.bot = u.bot;
        r.hasPickupWeapon = u.hasPickupWeapon;
        r.position = u.position;
        r.aim = u.aim;
        r.tailOrder = u.tailOrder;
        r.hp = u.hp;
        r.maxHp = u.maxHp;
        r.armor = u.armor;
        r.radius = u.radius;
        r.cooldown = u.cooldown;
        r.reload = u.reload;
        r.burnTime = u.burnTime;
        r.burnDamage = u.burnDamage;
        r.slowTime = u.slowTime;
        r.skillTime = u.skillTime;
        r.emergencyTime = u.emergencyTime;
        r.weapon = u.weapon;
        r.ammo = u.ammo;
        r.kills = u.kills;
        r.pendingUpgrades = u.pendingUpgrades;
        r.level = u.level;
        r.skills = u.skills;
        r.choices = u.choices;
        s.units.push_back(r);
    };
    for (const auto& u : units)
    {
        // Dead monsters and companions are recycled slots, not part of the run.
        if (!u.alive && u.kind != UnitKind::Fighter)
            continue;
        const bool character = u.kind == UnitKind::Fighter && !u.bot;
        save(u, character ? (u.human ? u.owner : u.previousOwner) : -1);
    }
    for (const auto& [owner, parked] : dormant)
    {
        save(parked.player, owner);
        for (const auto& member : parked.squad)
            save(member, -1);
    }
    for (const auto& c : crates)
        if (c.active)
            s.crates.push_back({c.id, c.weapon, c.position, c.dropTime, c.remaining});
    return s;
}
inline bool Battle::restore(const RunSnapshot& s)
{
    start(s.seed);
    if (s.walls.size() != maze.walls.size() || !std::isfinite(s.elapsed) || s.elapsed < 0 ||
        s.elapsed >= config.duration)
        return false;
    maze.walls = s.walls;
    for (const auto& [id, patch] : s.fragments)
        if (id >= 0 && id < static_cast<int>(maze.walls.size()))
            maze.fragments[id] = patch;
    maze.version = s.mazeVersion;
    navigationVersion = maze.version;
    elapsed = s.elapsed;
    sharedLevel = std::max(1, s.sharedLevel);
    sharedXP = std::isfinite(s.sharedXP) ? std::max(0., s.sharedXP) : 0;
    bossWave = std::clamp(s.bossWave, 0, static_cast<int>(BossSchedule.size()));
    nextBoss = bossWave < static_cast<int>(BossSchedule.size()) ? std::max(BossSchedule[bossWave], elapsed) : 1e9;
    nextWave = elapsed + 1;
    nextRecruit = elapsed + 3;
    while (nextCrate <= elapsed)
        nextCrate += config.airDropInterval;
    std::unordered_map<int, int> ownerOf;
    for (const auto& r : s.units)
        if (r.owner >= 0)
            ownerOf[r.id] = r.owner;
    int maxId = 0;
    uint32_t maxTail = 0;
    for (const auto& r : s.units)
    {
        if (!r.position.finite() || !std::isfinite(r.hp) || !std::isfinite(r.maxHp))
            continue;
        Unit u;
        u.id = r.id;
        u.team = r.team;
        u.previousOwner = r.owner;
        u.followerOf = r.followerOf;
        u.burnOwner = r.burnOwner;
        u.kind = r.kind;
        u.state = r.state;
        u.shape = r.shape;
        // An ally's outline is its tier; saves from before polygon tiers come back as triangles.
        if (u.kind == UnitKind::Companion)
        {
            u.sides = r.shape == Shape::Square ? 4 : r.shape == Shape::Pentagon ? 5 : r.shape == Shape::Hexagon ? 6 : MinSides;
            u.shape = polygonShape(u.sides);
        }
        u.alive = r.alive;
        u.bot = r.bot;
        u.hasPickupWeapon = r.hasPickupWeapon;
        u.position = r.position;
        u.aim = r.aim.finite() && r.aim.lengthSquared() > .5 ? r.aim.normalized() : Vec2{1, 0};
        u.tailOrder = r.tailOrder;
        u.hp = std::clamp(r.hp, 0., r.maxHp);
        u.maxHp = r.maxHp;
        u.armor = std::max(0., r.armor);
        u.radius = r.radius;
        u.cooldown = r.cooldown;
        u.reload = r.reload;
        u.burnTime = r.burnTime;
        u.burnDamage = r.burnDamage;
        u.slowTime = r.slowTime;
        u.skillTime = r.skillTime;
        u.emergencyTime = r.emergencyTime;
        u.weapon = std::clamp(r.weapon, 0, 5);
        u.ammo = r.ammo;
        u.kills = r.kills;
        u.pendingUpgrades = std::max(0, r.pendingUpgrades);
        u.level = r.level;
        u.skills = r.skills;
        for (int& rank : u.skills)
            rank = std::clamp(rank, 0, 5);
        u.choices = r.choices;
        u.think = (u.id % 20) * .015;
        maxId = std::max(maxId, u.id);
        maxTail = std::max(maxTail, u.tailOrder);
        if (r.owner >= 0)
        {
            dormant[r.owner].player = u;
            continue;
        }
        if (auto head = ownerOf.find(r.followerOf); r.followerOf >= 0 && head != ownerOf.end())
        {
            dormant[head->second].squad.push_back(u);
            continue;
        }
        if (u.state == UnitState::Following)
        {
            u.state = UnitState::Idle;
            u.followerOf = -1;
        }
        units.push_back(u);
    }
    for (auto it = dormant.begin(); it != dormant.end();)
        if (it->second.player.id < 0)
            it = dormant.erase(it);
        else
            ++it;
    for (const auto& c : s.crates)
    {
        crates.push_back({c.id, c.weapon, c.position, c.dropTime, c.remaining, true});
        maxId = std::max(maxId, c.id);
    }
    nextId = maxId + 1;
    nextTailOrder = maxTail + 1;
    return true;
}
} // namespace gwgame
