inline WeaponConfig Match::weaponFor(WeaponKind kind) const {
    switch(kind){case WeaponKind::Shotgun:return {4,700,2,.28,3,detail::Tau/12,5};case WeaponKind::Rifle:return {8,1800,.2,.2,3,.08,45};
    case WeaponKind::Sniper:return {200,4800,3,1,5,0,5};case WeaponKind::MachineGun:return {3,1750,.1,.28,8,detail::Tau/24,150};
    case WeaponKind::RocketLauncher:return {1000,4900,.42,.28,5,0,1};default:return weapon;}
}
inline WeaponConfig Match::weaponFor(const Fighter& f) const {
    return weaponFor(f,false);
}
inline WeaponConfig Match::weaponFor(const Fighter& f,bool rightHand) const {
    const WeaponKind kind=rightHand?f.temporaryWeaponKind:f.weaponKind;
    const int targetKind=rightHand?f.rightWeapon.targetKind:f.targetKind,targetIndex=rightHand?f.rightWeapon.targetIndex:f.targetIndex;
    auto gun=weaponFor(kind);if(kind!=WeaponKind::Sniper)return gun;
    const auto owner=indices.find(f.id);if(owner==indices.end())return gun;
    Vec target;bool found=false;
    if(targetKind==1&&targetIndex>=0&&targetIndex<static_cast<int>(fighters.size())){target=world.bodies[targetIndex].position;found=true;}
    else if(targetKind==2&&targetIndex>=0&&targetIndex<static_cast<int>(npcs.size())){target=npcs[targetIndex].position;found=true;}
    else if(targetKind==3&&targetIndex>=1&&targetIndex<=2){target=bases[targetIndex].position;found=true;}
    else if(targetKind==4){target=boss.position;found=true;}
    else if(targetKind==5&&targetIndex>=0&&targetIndex<static_cast<int>(evolutionPacks.size())){target=evolutionPacks[targetIndex].position;found=true;}
    else if(targetKind==6&&targetIndex>=0&&targetIndex<static_cast<int>(weaponCrates.size())){target=weaponCrates[targetIndex].position;found=true;}
    if(found){const double distance=(target-world.bodies[owner->second].position).length();gun.aimTime=1+std::clamp((distance-2400)/2400,0.0,1.0);if(distance>=2400)gun.damage=300;}
    return gun;
}
inline double Match::weaponMovementMultiplier(WeaponKind kind){
    switch(kind){case WeaponKind::Pistol:return 1.2;case WeaponKind::Rifle:return 1.5;case WeaponKind::Sniper:return .4;case WeaponKind::MachineGun:return .8;default:return 1;}
}
inline bool Match::addHost(int id,int team) {
    if(team<0||team>2||indices.count(id))return false;
    for(const auto& f:fighters)if(f.isHost)return false;
    if(!world.add(id,Shape::Circle,true))return false;
    Fighter f;f.id=id;f.team=team;f.isHost=true;f.hp=f.maxHp=3000;f.armor=f.maxArmor=500;f.weaponKind=WeaponKind::Rifle;f.unlockedWeapons=weaponBit(WeaponKind::Rifle);resetWeaponAmmo(f);
    indices.emplace(id,static_cast<int>(fighters.size()));fighters.push_back(f);
    auto& b=world.bodies.back();b.scale=2.5;b.speedScale=.5*weaponMovementMultiplier(f.weaponKind);b.velocity=b.velocity*b.speedScale;
    world.rebuildSpatial();emit(EventKind::Joined,id,-1,team);return true;
}
inline bool Match::setHostTeam(int id,int team) {
    auto* f=findFighter(id);if(!f||!f->isHost||team<0||team>2||phase==Phase::Results)return false;
    if(f->team!=team)f->bossBuffEligible=false;
    f->team=team;f->targetKind=0;f->acquisitionRemaining=0;return true;
}
inline bool Match::healLike(int id,int64_t count) {
    auto* f=findFighter(id);if(!f||!f->alive||count<=0||phase==Phase::Results)return false;
    const double restored=std::max(0.0,std::min(f->maxHp-f->hp,f->maxHp*.05*std::min<int64_t>(count,20)));
    f->hp+=restored;
    if(restored>0)f->healFlash=.7;
    emitDamage(world.bodies[indices.at(id)].position,restored,f->team,1,id,NumberKind::Healing);return true;
}
inline bool Match::grantShotgun(int id) {
    return grantWeapon(id,WeaponKind::Shotgun);
}
inline void Match::resetWeaponAmmo(Fighter& f) {
    for(int i=0;i<WeaponCount;++i)f.weaponStates[i]={weaponFor(static_cast<WeaponKind>(i)).magazine,0,0};
    f.ammo=weaponFor(f).magazine;f.reloadRemaining=f.shotRemaining=f.aimRemaining=f.sniperAimDuration=0;
}
inline bool Match::grantWeapon(int id,WeaponKind kind) {
    auto* f=findFighter(id);if(!f||f->isHost||!validWeapon(kind)||phase==Phase::Results)return false;
    f->unlockedWeapons|=weaponBit(kind);
    return switchWeapon(id,kind);
}
inline bool Match::switchWeapon(int id,WeaponKind kind) {
    auto* f=findFighter(id);if(!f||!validWeapon(kind)||phase==Phase::Results||(f->isHost&&kind!=WeaponKind::Rifle))return false;
    if(!(f->unlockedWeapons&weaponBit(kind)))return false;
    if(f->weaponKind==kind)return true;
    f->weaponStates[static_cast<size_t>(f->weaponKind)]={f->ammo,f->reloadRemaining,f->shotRemaining};
    f->weaponKind=kind;const auto& state=f->weaponStates[static_cast<size_t>(kind)];
    f->ammo=state.ammo;f->reloadRemaining=state.reloadRemaining;f->shotRemaining=state.shotRemaining;
    f->targetKind=0;f->targetIndex=-1;f->aimRemaining=weaponFor(*f).aimTime;f->sniperAimDuration=0;f->acquisitionRemaining=0;
    emit(EventKind::WeaponSwitched,id,-1,f->team,static_cast<int>(kind));return true;
}
inline bool Match::damageEnvironment(int id,double amount) {
    auto* v=findFighter(id);if(!v||!v->alive||phase==Phase::Results||!validAmount(amount))return false;
    const int index=indices.at(id);
    if(!v->isHost&&world.bodies[index].shape==Shape::Circle)amount*=.5;
    const double applied=applyFighterDamage(*v,amount);v->hitFlash=.16;audio.emit(AudioKind::FighterHit,id);
    emitDamage(world.bodies[index].position,applied,v->team,1,id);
    if(v->hp<=1e-8)kill(-1,index);return true;
}
inline bool Match::targetPosition(int index,Vec& position,bool rightHand) const {
    const auto& f=fighters[index];const int target=rightHand?f.rightWeapon.targetIndex:f.targetIndex;const int targetKind=rightHand?f.rightWeapon.targetKind:f.targetKind;
    if(targetKind==1&&target>=0&&target<static_cast<int>(fighters.size())&&fighters[target].alive&&hostile(f,fighters[target]))position=world.bodies[target].position;
    else if(targetKind==2&&target>=0&&target<static_cast<int>(npcs.size())&&npcs[target].active)position=npcs[target].position;
    else if(targetKind==3&&target>=1&&target<=2&&bases[target].alive&&f.team!=target)position=bases[target].position;
    else if(targetKind==4&&boss.active)position=boss.position;
    else if(targetKind==5&&!f.isHost&&target>=0&&target<static_cast<int>(evolutionPacks.size())&&evolutionPacks[target].active)position=evolutionPacks[target].position;
    else if(targetKind==6&&!f.isHost&&target>=0&&target<static_cast<int>(weaponCrates.size())&&weaponCrates[target].active)position=weaponCrates[target].position;
    else return false;
    const double range=weaponFor(f,rightHand).range*(!f.isHost&&world.bodies[index].shape==Shape::Rectangle?1.5:1);
    const Vec delta=position-world.bodies[index].position;return delta.dot(delta)<=range*range;
}
inline void Match::acquireTarget(int index,bool rightHand) {
    auto& f=fighters[index];auto state=weaponRuntime(f,rightHand);const auto& body=world.bodies[index];const auto gun=weaponFor(f,rightHand);
    const double range=gun.range*(!f.isHost&&body.shape==Shape::Rectangle?1.5:1),range2=range*range;
    double best=std::numeric_limits<double>::max();int kind=0,target=-1;
    const auto consider=[&](Vec position,int candidateKind,int candidateIndex){const Vec delta=position-body.position;const double d=delta.dot(delta);if(d<=range2&&d<best){best=d;kind=candidateKind;target=candidateIndex;}};
    // Explicit tiers: distance only selects within one tier. A nearby neutral
    // cannot distract a weapon from an in-range Boss or hostile participant.
    if(boss.active)consider(boss.position,4,0);
    if(!kind){
        world.queryLimitedFiltered(body.position,range,candidates,[&](int other){return other!=index&&fighters[other].alive&&hostile(f,fighters[other]);},128);
        for(int other:candidates)consider(world.bodies[other].position,1,other);
    }
    if(!kind)for(int team=1;team<=2;++team)if(team!=f.team&&bases[team].alive)consider(bases[team].position,3,team);
    if(!kind&&!f.isHost){
        for(int other=0;other<static_cast<int>(evolutionPacks.size());++other)if(evolutionPacks[other].active)consider(evolutionPacks[other].position,5,other);
        for(int other=0;other<static_cast<int>(weaponCrates.size());++other)if(weaponCrates[other].active)consider(weaponCrates[other].position,6,other);
    }
    if(!kind)for(int y=cell(body.position.y-range);y<=cell(body.position.y+range);++y)for(int x=cell(body.position.x-range);x<=cell(body.position.x+range);++x)
        for(int other:npcGrid[y*ResourceCells+x])if(npcs[other].active)consider(npcs[other].position,2,other);
    const bool changed=kind!=state.targetKind||target!=state.targetIndex;
    state.targetKind=kind;state.targetIndex=target;state.acquisitionRemaining=.45+static_cast<double>(index%11)*.023;
    if(changed){state.aimRemaining=weaponFor(f,rightHand).aimTime*(!f.isHost&&body.shape==Shape::Rectangle?.5:1);state.sniperAimDuration=state.kind==WeaponKind::Sniper?state.aimRemaining:0;}
}
inline WeaponRuntimeView Match::weaponRuntime(Fighter& f,bool rightHand) {
    if(rightHand){auto& r=f.rightWeapon;return {f.temporaryWeaponKind,r.ammo,r.reloadRemaining,r.shotRemaining,r.aimRemaining,r.sniperAimDuration,r.acquisitionRemaining,r.aimAngle,r.targetKind,r.targetIndex};}
    return {f.weaponKind,f.ammo,f.reloadRemaining,f.shotRemaining,f.aimRemaining,f.sniperAimDuration,f.acquisitionRemaining,f.aimAngle,f.targetKind,f.targetIndex};
}
inline void Match::tickWeapon(int index,double dt) {
    tickWeaponHand(index,dt,false);
    const auto& f=fighters[index];
    if(f.alive&&!f.isHost&&f.temporaryWeaponRemaining>0)tickWeaponHand(index,dt,true);
}
inline void Match::tickWeaponHand(int index,double dt,bool rightHand) {
    auto& f=fighters[index];auto state=weaponRuntime(f,rightHand);const auto& body=world.bodies[index];auto gun=weaponFor(f,rightHand);
    state.shotRemaining=std::max(0.0,state.shotRemaining-dt);
    double aimDt=dt;
    if(state.reloadRemaining>0){
        // Sniper telegraphing starts after reload. Only the portion of this
        // interval after completion can advance its next visible aim.
        if(state.kind==WeaponKind::Sniper)aimDt=std::max(0.0,dt-state.reloadRemaining);
        state.reloadRemaining=std::max(0.0,state.reloadRemaining-dt);if(state.reloadRemaining<=1e-8){state.reloadRemaining=0;state.ammo=gun.magazine;}
    }
    state.acquisitionRemaining-=dt;Vec target;
    const double range=gun.range*(!f.isHost&&body.shape==Shape::Rectangle?1.5:1);
    const Vec bossOffset=boss.position-body.position;
    const bool bossPreempts=boss.active&&state.targetKind!=4&&bossOffset.dot(bossOffset)<=range*range;
    if(state.acquisitionRemaining<=0||bossPreempts||!targetPosition(index,target,rightHand))acquireTarget(index,rightHand);
    if(!targetPosition(index,target,rightHand)){state.targetKind=0;return;}
    const Vec delta=target-body.position;const double distance=delta.length();state.aimAngle=std::atan2(delta.y,delta.x);
    gun=weaponFor(f,rightHand);
    if(state.kind==WeaponKind::Sniper){
        const double duration=gun.aimTime*(!f.isHost&&body.shape==Shape::Rectangle?.5:1);
        // Changing distance bands preserves time already spent aiming. A target
        // crossing into the far band still requires the full longer telegraph.
        const double elapsedAim=state.sniperAimDuration>0?std::max(0.0,state.sniperAimDuration-state.aimRemaining):0;
        state.aimRemaining=std::max(0.0,duration-elapsedAim);state.sniperAimDuration=duration;
    }
    state.aimRemaining=std::max(0.0,state.aimRemaining-aimDt);
    if(state.aimRemaining>1e-8||state.shotRemaining>1e-8||state.reloadRemaining>1e-8)return;
    const double reload=gun.reloadTime*(!f.isHost&&body.shape==Shape::Circle?.5:1);
    if(state.ammo<=0){state.reloadRemaining=reload;if(state.kind==WeaponKind::Sniper)state.aimRemaining=state.sniperAimDuration;return;}
    if(state.targetKind!=4&&state.targetKind!=1){
        // Keep routine steering staggered, but never spend a shot on a lower
        // tier after a hostile participant has entered range during cooldown.
        // A changed target must complete its own full aim on subsequent ticks.
        const int previousKind=state.targetKind,previousIndex=state.targetIndex;
        acquireTarget(index,rightHand);
        if(state.targetKind!=previousKind||state.targetIndex!=previousIndex)return;
    }
    const WeaponKind firedKind=state.kind;const int firedTargetKind=state.targetKind,firedTargetIndex=state.targetIndex;
    const double firedAngle=state.aimAngle;const bool hadRightWeapon=!f.isHost&&f.temporaryWeaponRemaining>0;
    --state.ammo;state.shotRemaining=gun.fireInterval;
    audio.emit(static_cast<AudioKind>(firedKind),f.id);
    if(firedKind==WeaponKind::Sniper)state.aimRemaining=state.sniperAimDuration;
    if(state.ammo==0)state.reloadRemaining=reload;
    // Finish runtime writes before hit callbacks: breaking a crate may replace
    // the right slot. Captured firing data cannot overwrite that fresh weapon.
    const int pellets=firedKind==WeaponKind::Shotgun?15+std::min(10,static_cast<int>(random()*11)):(firedKind==WeaponKind::MachineGun?7:1);
    for(int pellet=0;pellet<pellets;++pellet){
        const double damage=firedKind==WeaponKind::Shotgun?3+std::min(2,static_cast<int>(random()*3)):gun.damage;
        const double spread=(random()*2-1)*gun.spreadRadians/(!f.isHost&&body.shape==Shape::Rectangle?1.5:1);
        const double angle=firedAngle+spread;const Vec direction{std::cos(angle),std::sin(angle)};
        if(firedKind>=WeaponKind::Sniper){launchProjectile(index,direction,firedKind,damage,rightHand);continue;}
        const double radius=firedTargetKind>=5?PickupHalfExtent:(firedTargetKind==4?110:(firedTargetKind==3?140:(firedTargetKind==2?30:25*world.bodies[firedTargetIndex].scale)));
        bool hit=false;
        bool intersects=std::abs(std::sin(spread))*distance<=radius;
        if(firedTargetKind>=5){
            // Segment/slab intersection against the same square used by body
            // collisions, so square corners do not behave like a circle.
            double enter=0,leave=distance;const Vec relative=body.position-target;
            const auto slab=[&](double start,double rate){if(std::abs(rate)<1e-12)return std::abs(start)<=PickupHalfExtent;double a=(-PickupHalfExtent-start)/rate,b=(PickupHalfExtent-start)/rate;if(a>b)std::swap(a,b);enter=std::max(enter,a);leave=std::min(leave,b);return enter<=leave;};
            intersects=slab(relative.x,direction.x)&&slab(relative.y,direction.y);
        }
        if(intersects){
            if(firedTargetKind==1)hit=damagePlayer(f.id,fighters[firedTargetIndex].id,damage);
            else if(firedTargetKind==2)hit=damageNpc(f.id,firedTargetIndex,damage);
            else if(firedTargetKind==3)hit=damageBase(f.id,firedTargetIndex,damage);
            else if(firedTargetKind==4)hit=damageBoss(f.id,damage);
            else if(firedTargetKind==5)hit=damageEvolutionPack(f.id,firedTargetIndex,damage);
            else if(firedTargetKind==6)hit=damageWeaponCrate(f.id,firedTargetIndex,damage);
        }
        const double life=std::clamp(distance/2400.0,.12,.36);
        const Shot shot{weaponMuzzle(body,direction,rightHand,hadRightWeapon),body.position+direction*distance,f.team,life,firedKind,life,hit,rightHand};
        if(shots.size()<512)shots.push_back(shot);else{shots[shotCursor%512]=shot;++shotCursor;}
    }
}
