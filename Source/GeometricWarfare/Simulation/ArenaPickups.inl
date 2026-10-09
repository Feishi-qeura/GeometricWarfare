#pragma once
namespace gw {
inline bool Match::damageEvolutionPack(int attackerId,int index,double amount){
    auto* f=findFighter(attackerId);
    if(!f||!f->alive||f->isHost||phase==Phase::Results||!validAmount(amount)||index<0||index>=static_cast<int>(evolutionPacks.size()))return false;
    auto& pack=evolutionPacks[index];if(!pack.active)return false;
    const double applied=std::min(pack.hp,amount*damageMultiplierFor(*f,true));
    pack.hp=std::max(0.0,pack.hp-applied);pack.hitFlash=.16;emitDamage(pack.position,applied,0,5,index);
    if(pack.hp<=1e-8)collectEvolutionPack(attackerId,index);
    return true;
}
inline bool Match::damageWeaponCrate(int attackerId,int index,double amount){
    auto* f=findFighter(attackerId);
    if(!f||!f->alive||f->isHost||phase==Phase::Results||!validAmount(amount)||index<0||index>=static_cast<int>(weaponCrates.size()))return false;
    auto& crate=weaponCrates[index];if(!crate.active)return false;
    const double applied=std::min(crate.hp,amount*damageMultiplierFor(*f,true));
    crate.hp=std::max(0.0,crate.hp-applied);crate.hitFlash=.16;emitDamage(crate.position,applied,0,6,index);
    if(crate.hp<=1e-8)collectWeaponCrate(attackerId,index);
    return true;
}
inline bool Match::collectWeaponCrate(int id,int index){
    auto* f=findFighter(id);
    if(!f||!f->alive||f->isHost||phase==Phase::Results||index<0||index>=static_cast<int>(weaponCrates.size()))return false;
    auto& crate=weaponCrates[index];const auto kind=crate.weaponKind;
    if(!crate.active||kind<WeaponKind::Sniper||kind>WeaponKind::RocketLauncher)return false;
    if(f->temporaryWeaponRemaining>0&&f->temporaryWeaponKind==kind){
        f->temporaryWeaponRemaining=TemporaryWeaponLifetime;
    }else{
        f->temporaryWeaponKind=kind;f->temporaryWeaponRemaining=TemporaryWeaponLifetime;
        f->rightWeapon={};f->rightWeapon.ammo=weaponFor(kind).magazine;
    }
    emit(EventKind::WeaponObtained,id,-1,f->team,static_cast<int>(kind));
    audio.emit(crate.hp<=1e-8?AudioKind::WeaponBreak:AudioKind::WeaponPickup,id);crate.active=false;crate.hp=0;return true;
}
inline void Match::endTemporaryWeapon(Fighter& f){
    // A crate never equips or unlocks a left weapon. Releasing its lease has
    // no left-state restoration and no permanent selection event.
    f.temporaryWeaponRemaining=0;f.temporaryWeaponKind=WeaponKind::Pistol;f.weaponBeforeTemporary=WeaponKind::Pistol;
    f.rightWeapon={};
}
inline void Match::tickWeaponCrates(double dt){
    for(auto& crate:weaponCrates)crate.hitFlash=std::max(0.0,crate.hitFlash-dt);
    for(auto& f:fighters)if(f.temporaryWeaponRemaining>0){
        if(!f.alive||f.temporaryWeaponRemaining<=dt+1e-8)endTemporaryWeapon(f);
        else f.temporaryWeaponRemaining-=dt;
    }
    const int scheduledWave=static_cast<int>(std::floor((elapsed+1e-8)/WeaponCrateInterval));
    while(weaponCrateWave<scheduledWave){
        ++weaponCrateWave;
        for(int i=0;i<10;++i){
            WeaponCrate crate;crate.position={80+random()*(World::Size-160),80+random()*(World::Size-160)};
            crate.weaponKind=static_cast<WeaponKind>(static_cast<int>(WeaponKind::Sniper)+std::min(2,static_cast<int>(random()*3)));
            crate.spawnedAt=weaponCrateWave*WeaponCrateInterval;
            auto slot=std::find_if(weaponCrates.begin(),weaponCrates.end(),[](const WeaponCrate& box){return !box.active;});
            if(slot!=weaponCrates.end())*slot=crate;
            else if(weaponCrates.size()<WeaponCrateCapacity)weaponCrates.push_back(crate);
            else *std::min_element(weaponCrates.begin(),weaponCrates.end(),[](const WeaponCrate& a,const WeaponCrate& b){return a.spawnedAt<b.spawnedAt;})=crate;
        }
    }
    if(!config.autoCollect)return;
    for(size_t i=0;i<weaponCrates.size();++i){
        const auto& crate=weaponCrates[i];if(!crate.active)continue;
        world.queryLimited(crate.position,90,candidates,128);
        double nearest=std::numeric_limits<double>::max();int collector=-1;
        for(int candidate:candidates){const auto& f=fighters[candidate];if(!f.alive||f.isHost||!pickupTouches(world.bodies[candidate],crate.position))continue;
            const auto delta=world.bodies[candidate].position-crate.position;const double distance=delta.dot(delta);
            if(distance<nearest){nearest=distance;collector=f.id;}
        }
        if(collector>=0)collectWeaponCrate(collector,static_cast<int>(i));
    }
}
}
