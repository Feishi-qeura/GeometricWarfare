#pragma once
namespace gwgame
{
inline void Battle::equip(Unit& u, int weapon)
{
    u.weapon = weapon;
    u.hasPickupWeapon = true;
    u.ammo = weaponStats(weapon).ammo;
    u.reload = 0;
    u.cooldown = 0;
}
inline bool Battle::pickupWeapon(int id, int weapon)
{
    auto* p = find(id);
    if (!p || !p->alive || !p->player() || weapon < 1 || weapon > 5 || upgradePaused())
        return false;
    // Any human in a voluntary squad may collect for the whole squad (a carried teammate only for themselves).
    if (p->human && p->followerOf >= 0 && !p->carried)
        if (auto* head = find(p->followerOf); head && head->alive)
            p = head;
    equip(*p, weapon);
    for (int member : tailIds(p->id))
        equip(*find(member), weapon);
    return true;
}
inline void Battle::syncSquadWeapon(Unit& member, const Unit& leader)
{
    if (leader.hasPickupWeapon)
        equip(member, leader.weapon);
}
inline bool Battle::joinSquad(int member, int leader)
{
    auto* m = find(member);
    auto* h = find(leader);
    if (upgradePaused() || !m || !h || member == leader || !m->human || !h->human || !m->alive || !h->alive ||
        m->team != h->team || h->followerOf >= 0 || m->followerOf >= 0 || !tailIds(member).empty() ||
        (m->position - h->position).length() > config.recruitRange ||
        !maze.clear(m->position, h->position, std::min(m->radius, h->radius)))
        return false;
    m->followerOf = leader;
    m->tailOrder = nextTailOrder++;
    m->state = UnitState::Following;
    syncSquadWeapon(*m, *h);
    event(EffectStyle::Recruit, h->position, m->position, h->team);
    return true;
}
} // namespace gwgame
