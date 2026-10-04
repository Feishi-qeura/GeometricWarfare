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
    if(f->temporaryWeaponRemaining>0&&f->temporaryWeaponKind==kind&&!(f->unlockedWeapons&weaponBit(kind))){
        f->temporaryWeaponRemaining=TemporaryWeaponLifetime;
    }else{
        const auto fallback=(f->unlockedWeapons&weaponBit(f->weaponKind))?f->weaponKind:f->weaponBeforeTemporary;
        endTemporaryWeapon(*f);
        if(!(f->unlockedWeapons&weaponBit(kind))){
            f->temporaryWeaponKind=kind;f->temporaryWeaponRemaining=TemporaryWeaponLifetime;
            f->weaponBeforeTemporary=validWeapon(fallback)&&(f->unlockedWeapons&weaponBit(fallback))?fallback:WeaponKind::Pistol;
        }
    }
    switchWeapon(id,kind);crate.active=false;crate.hp=0;return true;
}
inline void Match::endTemporaryWeapon(Fighter& f){
    if(f.temporaryWeaponRemaining<=0)return;
    const auto expired=f.temporaryWeaponKind;
    const auto fallback=validWeapon(f.weaponBeforeTemporary)&&(f.unlockedWeapons&weaponBit(f.weaponBeforeTemporary))?f.weaponBeforeTemporary:WeaponKind::Pistol;
    f.temporaryWeaponRemaining=0;f.temporaryWeaponKind=WeaponKind::Pistol;f.weaponBeforeTemporary=WeaponKind::Pistol;
    if(f.weaponKind!=expired||(f.unlockedWeapons&weaponBit(expired)))return;
    // Round transitions also release leases during Results, when the public
    // player switch command is deliberately disabled.
    f.weaponStates[static_cast<size_t>(f.weaponKind)]={f.ammo,f.reloadRemaining,f.shotRemaining};
    f.weaponKind=fallback;const auto& state=f.weaponStates[static_cast<size_t>(fallback)];
    f.ammo=state.ammo;f.reloadRemaining=state.reloadRemaining;f.shotRemaining=state.shotRemaining;
    f.targetKind=0;f.targetIndex=-1;f.aimRemaining=weaponFor(f).aimTime;f.sniperAimDuration=0;f.acquisitionRemaining=0;
    emit(EventKind::WeaponSwitched,f.id,-1,f.team,static_cast<int>(fallback));
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
