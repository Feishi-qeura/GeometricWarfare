#pragma once
namespace gwgame
{
static_assert(AttributeCount == static_cast<int>(std::tuple_size<decltype(Perks::ranks)>::value), "Perks hold one rank per attribute");
// Meta progression inside a room: research ranks (capped by the room's tier), the starting weapon, the pen that
// decides the wall type, and single-mission items. Defense pages and the hub play by them; Survival does not.
inline int Battle::effectiveRankOf(const Unit& u, Attribute a) const
{
    const int i = static_cast<int>(a);
    if (survivalMode() || !u.player() || i < 0 || i >= AttributeCount)
        return 0;
    return effectiveRank(u.perks.ranks[i], config.tier);
}
inline double Battle::perk(const Unit& u, Attribute a) const
{
    return attributeScale(a, effectiveRankOf(u, a));
}
inline double Battle::healthCapacity(const Unit& u) const
{
    return 300 * perk(u, Attribute::Health);
}
inline double Battle::inkCapacity(const Unit& u) const
{
    return config.inkMax * perk(u, Attribute::Ink) * (u.perks.bottle ? BottleInk : 1);
}
inline double Battle::enemyPower() const
{
    return defenseMode() && !defense.tutorial ? tierInfo(config.tier).enemyDamage : 1;
}
inline double Battle::cellInk(const Unit& u, WallKind kind) const
{
    return config.inkPerCell * penInk(static_cast<int>(kind)) * (u.perks.carbon ? CarbonInk : 1);
}
inline void Battle::refreshCapacities(Unit& u)
{
    if (survivalMode() || u.kind != UnitKind::Fighter || !u.player())
        return;
    const double health = healthCapacity(u);
    if (std::abs(health - u.maxHp) > 1e-9)
    {
        const bool full = u.hp >= u.maxHp - 1e-9;
        const double share = u.maxHp > 0 ? u.hp / u.maxHp : 1;
        u.maxHp = health;
        if (u.alive)
            u.hp = full ? health : std::clamp(share * health, 1., health);
    }
    if (defenseMode())
    {
        const double ink = inkCapacity(u);
        if (std::abs(ink - u.maxInk) > 1e-9)
        {
            const bool brimming = u.ink >= u.maxInk - 1e-9;
            u.maxInk = ink;
            u.ink = brimming ? ink : std::min(u.ink, ink);
        }
    }
}
inline bool Battle::setLoadout(int id, const Loadout& loadout)
{
    Unit* u = find(id);
    if (!u || !u->human || u->kind != UnitKind::Fighter)
        return false;
    const Loadout clean = sanitize(loadout);
    u->perks.ranks = clean.ranks;
    u->perks.pen = clean.pen;
    u->perks.hat = clean.hat;
    u->perks.weapon = clean.weapon;
    // The starting weapon is in hand whenever no crate weapon is.
    if (!survivalMode() && !u->hasPickupWeapon && u->weapon != clean.weapon)
    {
        u->weapon = clean.weapon;
        u->ammo = weaponStats(u->weapon).ammo;
        u->reload = u->cooldown = 0;
    }
    refreshCapacities(*u);
    return true;
}
inline void Battle::setTier(int tier)
{
    config.tier = clampTier(tier);
    for (auto& u : units)
        if (u.human)
            refreshCapacities(u);
}
inline ItemSlotsArray Battle::useItems(int id, const ItemSlotsArray& items)
{
    ItemSlotsArray used{{-1, -1}};
    Unit* u = find(id);
    // Items belong to a real page, before its first wave (the tutorial and the hub use none).
    if (!u || !u->human || !defenseMode() || tutorialMode() || finished || defense.phase != DefensePhase::Build || defense.wave > 0)
        return used;
    for (int slot = 0; slot < ItemSlots; ++slot)
    {
        const int item = items[slot];
        if (item < 0 || item >= ItemCount)
            continue;
        // A second copy of an item that does not add up stays in the bag (it is not used up either).
        switch (static_cast<Item>(item))
        {
        case Item::InkBottle:
            if (u->perks.bottle)
                continue;
            u->perks.bottle = true;
            refreshCapacities(*u);
            u->ink = u->maxInk;
            break;
        case Item::Tape:
            defense.coreHealth += TapeCore;
            defense.coreMaxHealth += TapeCore;
            break;
        case Item::Pouch:
        {
            int room = config.tailLimit;
            for (int member : tailIds(id))
                room -= !find(member)->human;
            if (room < 3)
                continue;
            for (int sides : {4, 3, 3})
            {
                Unit a;
                a.id = nextId++;
                a.kind = UnitKind::Companion;
                a.team = u->team;
                a.sides = sides;
                a.shape = polygonShape(sides);
                a.maxHp = a.hp = CompanionHealth * sideHealth(sides);
                a.position = u->position;
                a.followerOf = id;
                a.state = UnitState::Following;
                a.tailOrder = nextTailOrder++;
                syncSquadWeapon(a, *u);
                event(EffectStyle::SpawnAlly, a.position, a.position, u->team, a.radius, .5);
                queueUnit(a);
            }
            flushUnits();
            break;
        }
        case Item::Plaster:
            if (u->perks.plaster)
                continue;
            u->perks.plaster = true;
            break;
        case Item::Magnet:
            if (u->perks.magnet)
                continue;
            u->perks.magnet = true;
            break;
        case Item::Carbon:
            if (u->perks.carbon)
                continue;
            u->perks.carbon = true;
            break;
        default:
            continue;
        }
        used[slot] = item;
        u->perks.itemsUsed = static_cast<uint8_t>(u->perks.itemsUsed | (1u << item));
    }
    return used;
}
} // namespace gwgame
