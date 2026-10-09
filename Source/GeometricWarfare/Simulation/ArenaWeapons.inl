#pragma once
// The shared swept circle/polygon helpers are defined by ArenaBoss.inl.
namespace gw {
namespace weapon_detail {
inline Body pickupBody(Vec position){Body body;body.position=position;body.shape=Shape::Square;body.scale=PickupHalfExtent/21;return body;}
inline Vec circleContact(Vec point,Vec center,double radius) {
    const Vec offset=point-center;const double distance=offset.length();return distance<=radius?point:center+offset*(radius/distance);
}
inline Vec bodyContact(Vec point,const Body& body) {
    const auto geometry=detail::geometry(body);const Vec local=point-body.position;
    if(geometry.count==0)return circleContact(point,body.position,geometry.radius);
    if(boss_detail::insidePolygon(local,geometry))return point;
    double distance2=std::numeric_limits<double>::max();Vec closest=point;
    for(int i=0;i<geometry.count;++i){const Vec a=geometry.points[i],edge=geometry.points[(i+1)%geometry.count]-a;
        const Vec candidate=a+edge*std::clamp((local-a).dot(edge)/edge.dot(edge),0.0,1.0);const Vec offset=local-candidate;
        if(offset.dot(offset)<distance2){distance2=offset.dot(offset);closest=body.position+candidate;}}
    return closest;
}
inline double bodyDistance(Vec point,const Body& body){return (point-bodyContact(point,body)).length();}
inline double wallFraction(Vec from,Vec delta) {
    double fraction=1;
    const auto axis=[&](double start,double distance){if(distance>1e-12)fraction=std::min(fraction,(World::Size-start)/distance);else if(distance< -1e-12)fraction=std::min(fraction,-start/distance);};
    axis(from.x,delta.x);axis(from.y,delta.y);return std::clamp(fraction,0.0,1.0);
}
}
inline void Match::launchProjectile(int index,Vec direction,WeaponKind kind,double damage,bool rightHand) {
    if(projectiles.size()>=WeaponProjectileCapacity)return;
    const auto& f=fighters[index];const auto& body=world.bodies[index];
    const bool rocket=kind==WeaponKind::RocketLauncher;
    const double speed=rocket?2000:(kind==WeaponKind::Sniper?10000:2200);
    WeaponProjectile projectile;projectile.position=projectile.previous=weaponMuzzle(body,direction,rightHand,!f.isHost&&f.temporaryWeaponRemaining>0);
    projectile.velocity=direction*speed;projectile.maxDistance=kind==WeaponKind::MachineGun?4400:std::numeric_limits<double>::infinity();
    projectile.radius=rocket?8:3;projectile.damage=damage;projectile.ownerId=f.id;projectile.team=f.team;projectile.kind=kind;projectile.rightHand=rightHand;
    projectiles.push_back(projectile);
}
inline void Match::explodeRocket(const WeaponProjectile& projectile) {
    const auto* owner=findFighter(projectile.ownerId);if(!owner||!owner->alive)return;
    const WeaponExplosion explosion{projectile.position,0,.45,RocketExplosionRadius,projectile.team,true};
    if(explosions.size()<WeaponExplosionCapacity)explosions.push_back(explosion);
    else{auto oldest=std::max_element(explosions.begin(),explosions.end(),[](const WeaponExplosion& a,const WeaponExplosion& b){return a.age<b.age;});*oldest=explosion;}
    // Detonation is at the real surface contact. Distance to target surfaces
    // gives large bases/Boss contact damage without expanding the blast disk.
    const auto falloff=[&](double distance){return distance<=RocketExplosionRadius+1e-8?projectile.damage*(1-.9*std::min(1.0,distance/RocketExplosionRadius)):0;};
    world.query(explosion.position,RocketExplosionRadius+90,candidates);
    for(int index:candidates)if(fighters[index].id!=owner->id&&fighters[index].alive&&hostile(*owner,fighters[index])){
        const double amount=falloff(weapon_detail::bodyDistance(explosion.position,world.bodies[index]));if(amount>0)damagePlayer(owner->id,fighters[index].id,amount);
    }
    const double range=RocketExplosionRadius+30;
    for(int y=cell(explosion.position.y-range);y<=cell(explosion.position.y+range);++y)for(int x=cell(explosion.position.x-range);x<=cell(explosion.position.x+range);++x)
        for(int index:npcGrid[y*ResourceCells+x])if(npcs[index].active){const double amount=falloff(std::max(0.0,(npcs[index].position-explosion.position).length()-30));if(amount>0)damageNpc(owner->id,index,amount);}
    for(int team=1;team<=2;++team)if(team!=owner->team&&bases[team].alive){const double amount=falloff(std::max(0.0,(bases[team].position-explosion.position).length()-100));if(amount>0)damageBase(owner->id,team,amount*2.5);}
    if(boss.active){const double amount=falloff(std::max(0.0,(boss.position-explosion.position).length()-BossDiameter*.5));if(amount>0)damageBoss(owner->id,amount);}
    if(!owner->isHost){
        for(int index=0;index<static_cast<int>(evolutionPacks.size());++index)if(evolutionPacks[index].active){const double amount=falloff(weapon_detail::bodyDistance(explosion.position,weapon_detail::pickupBody(evolutionPacks[index].position)));if(amount>0)damageEvolutionPack(owner->id,index,amount);}
        for(int index=0;index<static_cast<int>(weaponCrates.size());++index)if(weaponCrates[index].active){const double amount=falloff(weapon_detail::bodyDistance(explosion.position,weapon_detail::pickupBody(weaponCrates[index].position)));if(amount>0)damageWeaponCrate(owner->id,index,amount);}
    }
}
inline void Match::tickProjectiles(double dt) {
    for(auto& explosion:explosions){explosion.age+=dt;if(explosion.age+1e-8>=explosion.lifetime)explosion.active=false;}
    explosions.erase(std::remove_if(explosions.begin(),explosions.end(),[](const WeaponExplosion& explosion){return !explosion.active;}),explosions.end());
    for(auto& projectile:projectiles)if(projectile.active){
        const auto* owner=findFighter(projectile.ownerId);if(!owner||!owner->alive){projectile.active=false;continue;}
        const double speed=projectile.velocity.length();if(speed<=1e-8){projectile.active=false;continue;}
        const double travel=std::min(projectile.maxDistance-projectile.travelled,speed*dt);
        const Vec from=projectile.position,delta=projectile.velocity*(travel/speed);
        const double wall=weapon_detail::wallFraction(from,delta);const Vec to=from+delta*wall;
        const Vec center=(from+to)*.5;const double segmentLength=(to-from).length();
        double first=2;int kind=0,target=-1;
        // Exact spatial query and actual rotated geometry: never capped sampling for hits.
        world.query(center,segmentLength*.5+projectile.radius+90,candidates);
        for(int index:candidates)if(fighters[index].id!=owner->id&&fighters[index].alive&&hostile(*owner,fighters[index])){
            const double entry=boss_detail::bodyEntry(from,to,world.bodies[index],projectile.radius);if(entry<first){first=entry;kind=1;target=index;}
        }
        const double resourceRange=segmentLength*.5+projectile.radius+30;
        for(int y=cell(center.y-resourceRange);y<=cell(center.y+resourceRange);++y)for(int x=cell(center.x-resourceRange);x<=cell(center.x+resourceRange);++x)
            for(int index:npcGrid[y*ResourceCells+x])if(npcs[index].active){const double entry=boss_detail::circleEntry(from,to-from,npcs[index].position,30+projectile.radius);if(entry<first){first=entry;kind=2;target=index;}}
        for(int team=1;team<=2;++team)if(team!=owner->team&&bases[team].alive){const double entry=boss_detail::circleEntry(from,to-from,bases[team].position,100+projectile.radius);if(entry<first){first=entry;kind=3;target=team;}}
        if(boss.active){const double entry=boss_detail::circleEntry(from,to-from,boss.position,BossDiameter*.5+projectile.radius);if(entry<first){first=entry;kind=4;target=0;}}
        if(!owner->isHost){
            const double pad=PickupHalfExtent+projectile.radius;
            const Vec low{std::min(from.x,to.x)-pad,std::min(from.y,to.y)-pad},high{std::max(from.x,to.x)+pad,std::max(from.y,to.y)+pad};
            const auto pickupEntry=[&](Vec p){return p.x<low.x||p.x>high.x||p.y<low.y||p.y>high.y?2:boss_detail::bodyEntry(from,to,weapon_detail::pickupBody(p),projectile.radius);};
            for(int index=0;index<static_cast<int>(evolutionPacks.size());++index)if(evolutionPacks[index].active){const double entry=pickupEntry(evolutionPacks[index].position);if(entry<first){first=entry;kind=5;target=index;}}
            for(int index=0;index<static_cast<int>(weaponCrates.size());++index)if(weaponCrates[index].active){const double entry=pickupEntry(weaponCrates[index].position);if(entry<first){first=entry;kind=6;target=index;}}
        }
        projectile.previous=from;projectile.position=kind?from+(to-from)*first:to;projectile.travelled+=(projectile.position-from).length();
        if(kind){projectile.active=false;if(projectile.kind==WeaponKind::RocketLauncher){
                if(kind==1)projectile.position=weapon_detail::bodyContact(projectile.position,world.bodies[target]);
                else if(kind==2)projectile.position=weapon_detail::circleContact(projectile.position,npcs[target].position,30);
                else if(kind==3)projectile.position=weapon_detail::circleContact(projectile.position,bases[target].position,100);
                else if(kind==4)projectile.position=weapon_detail::circleContact(projectile.position,boss.position,BossDiameter*.5);
                else projectile.position=weapon_detail::bodyContact(projectile.position,weapon_detail::pickupBody(kind==5?evolutionPacks[target].position:weaponCrates[target].position));
                explodeRocket(projectile);
            }
            else if(kind==1)damagePlayer(owner->id,fighters[target].id,projectile.damage);
            else if(kind==2)damageNpc(owner->id,target,projectile.damage);
            else if(kind==3)damageBase(owner->id,target,projectile.damage);
            else if(kind==4)damageBoss(owner->id,projectile.damage);
            else if(kind==5)damageEvolutionPack(owner->id,target,projectile.damage);
            else if(kind==6)damageWeaponCrate(owner->id,target,projectile.damage);
        }else if(wall<1||projectile.position.x<=0||projectile.position.x>=World::Size||projectile.position.y<=0||projectile.position.y>=World::Size||projectile.travelled+1e-8>=projectile.maxDistance)projectile.active=false;
    }
    projectiles.erase(std::remove_if(projectiles.begin(),projectiles.end(),[](const WeaponProjectile& projectile){return !projectile.active;}),projectiles.end());
}
}
