#pragma once
namespace gwgame
{
static_assert(AttributeCount == static_cast<int>(std::tuple_size<decltype(Perks::ranks)>::value), "Perks hold one rank per attribute");
// Meta progression inside a room: research ranks (each player's own, whole in every room), the starting weapon, the
// pen that decides the wall type, and single-mission items. Defense pages and the hub play by them; Survival does not.
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
    double health = 300 * perk(u, Attribute::Health);
    // On the expedition a revive drawing's body and the hat worn change it too (and a chubby floor doubles it, EV04).
    if (expeditionMode() && u.kind == UnitKind::Fighter)
        health *= u.kit.hpScale * lookPerks(u.kit).health * (expedition.floorPrank == static_cast<int>(FloorPrank::Chubby) ? 2 : 1);
    return health;
}
inline double Battle::inkCapacity(const Unit& u) const
{
    // The expedition's ink tank (design 5.2): 100, 15% more per research rank, 180 with the glass ink bottle.
    if (expeditionMode())
        return (carries(u, Gear::InkBottle) ? BottleInkCap : 100.) * (1 + .15 * effectiveRankOf(u, Attribute::Ink));
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
    if (defenseMode() || expeditionMode())
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
    const int previous = u->perks.weapon;
    u->perks.ranks = clean.ranks;
    u->perks.pen = clean.pen;
    u->perks.hat = clean.hat;
    u->perks.title = clean.title;
    u->perks.weapons = clean.weapons;
    u->perks.weapon = clean.weapon;
    if (expeditionMode())
    {
        // The expedition: the armoury's gun goes into slot 1 in place of the starting pistol (only while slot 1 still
        // holds the gun the room last knew), and the wardrobe's hat and title are worn into the dungeon.
        HotSlot& first = u->kit.slots[0];
        if (first.kind == SlotKind::Weapon && first.count == 1 && first.what == previous && first.what != clean.weapon)
        {
            stowHand(*u);
            first = {SlotKind::Weapon, clean.weapon, 1, magazine(clean.weapon, false), 0};
            syncHand(*u);
        }
        if (!u->kit.ghost)
        {
            if (u->kit.wear[0] < 0 || u->kit.wearOwner[0] < 0)
                u->kit.wear[0] = clean.hat;
            if (u->kit.wear[5] < 0 || u->kit.wearOwner[5] < 0)
                u->kit.wear[5] = clean.title;
        }
        refreshCapacities(*u);
        return true;
    }
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
            // The tower kit: the player's first tower in the box costs no ink.
            if (u->perks.towerKit)
                continue;
            u->perks.towerKit = true;
            break;
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
