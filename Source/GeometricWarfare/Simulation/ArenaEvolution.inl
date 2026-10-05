inline bool Match::collectEvolutionPack(int id,int index) {
    auto* f=findFighter(id);if(!f||!f->alive||f->isHost||phase==Phase::Results||index<0||index>=static_cast<int>(evolutionPacks.size())||!evolutionPacks[index].active)return false;
    audio.emit(evolutionPacks[index].hp<=1e-8?AudioKind::EvolutionBreak:AudioKind::EvolutionPickup,id);
    evolutionPacks[index].active=false;evolutionPacks[index].hp=0;
    return grantEvolution(id);
}
inline bool Match::grantEvolution(int id) {
    auto* f=findFighter(id);if(!f||!f->alive||phase==Phase::Results)return false;
    if(f->evolutionRemaining<=0){
        if(!f->heroBuff){f->hp*=2;f->maxHp*=2;}
        f->armor=std::max(f->armor,100.0);f->maxArmor=std::max(f->maxArmor,100.0);f->swordRemaining=5;
    }
    f->evolutionRemaining=40;f->evolutionStartedAt=elapsed;return true;
}
inline void Match::endEvolution(Fighter& f) {
    if(f.evolutionRemaining>0){
        const double fraction=f.maxHp>0?f.hp/f.maxHp:0;
        f.maxHp=f.heroBuff?2100:(f.isHost?3000:shapeHp(world.bodies[indices.at(f.id)].shape));f.hp=f.maxHp*fraction;
        f.maxArmor=f.heroBuff?300:(f.isHost?500:0);f.armor=std::min(f.armor,f.maxArmor);
    }
    f.evolutionRemaining=0;f.swordRemaining=5;
}
inline void Match::emitSwordPulse(int index) {
    const auto& f=fighters[index];const Vec p=world.bodies[index].position;
    for(int ring=0;ring<2;++ring)for(int i=0;i<6;++i){const double angle=f.aimAngle+i*detail::Tau/6+ring*detail::Tau/12;SwordWave wave;wave.from=wave.position=p;wave.direction={std::cos(angle),std::sin(angle)};wave.ownerId=f.id;wave.team=f.team;
        const size_t ordinary=std::count_if(swordWaves.begin(),swordWaves.end(),[](const SwordWave& w){return !w.hero;});
        if(ordinary<256)swordWaves.push_back(wave);else{auto oldest=swordWaves.end();for(auto it=swordWaves.begin();it!=swordWaves.end();++it)if(!it->hero&&(oldest==swordWaves.end()||it->life<oldest->life))oldest=it;if(oldest!=swordWaves.end())*oldest=wave;}}
}
inline void Match::emitHeroSwordPulse(int index,int ray,double angle,double birthDelay) {
    const auto& fighter=fighters[index];const auto& body=world.bodies[index];const Vec p=body.position;
    const Vec direction{std::cos(angle),std::sin(angle)},side{-direction.y,direction.x};
    const double offset=(-.2+.4*ray/5.0)*44*body.scale;
    SwordWave wave;wave.from=wave.position=p+side*offset;wave.direction=direction;
    wave.ownerId=fighter.id;wave.team=fighter.team;wave.hero=true;wave.damage=30;
    wave.speed=880;wave.life=wave.maxDistance/wave.speed;wave.stepBirthDelay=birthDelay;
    const size_t heroCount=std::count_if(swordWaves.begin(),swordWaves.end(),[](const SwordWave& w){return w.hero;});
    if(heroCount<768)swordWaves.push_back(wave);
    else {auto oldest=swordWaves.end();for(auto it=swordWaves.begin();it!=swordWaves.end();++it)if(it->hero&&(oldest==swordWaves.end()||it->life<oldest->life))oldest=it;if(oldest!=swordWaves.end())*oldest=wave;}
}
inline void Match::tickEvolution(double dt) {
    for(auto& pack:evolutionPacks)pack.hitFlash=std::max(0.0,pack.hitFlash-dt);
    for(size_t i=0;i<fighters.size();++i){auto& f=fighters[i];
        if(!f.alive||!f.heroBuff){f.heroBurstIndex=6;f.heroBurstRemaining=0;continue;}
        if(elapsed<=f.heroActivatedAt+1e-8)continue;
        double remaining=dt,offset=0;
        for(;;){
            const double next=std::min(f.heroSwordRemaining,f.heroBurstIndex<6?f.heroBurstRemaining:std::numeric_limits<double>::infinity());
            const double advance=std::min(remaining,std::max(0.0,next));
            f.heroSwordRemaining-=advance;if(f.heroBurstIndex<6)f.heroBurstRemaining-=advance;
            remaining-=advance;offset+=advance;
            if(next>advance+1e-8)break;
            // The five-second clock runs from the start of a burst. Its six
            // shots keep that first aim direction while following their owner.
            if(f.heroSwordRemaining<=1e-8){f.heroSwordRemaining+=5;f.heroBurstIndex=0;f.heroBurstRemaining=0;f.heroBurstAngle=f.aimAngle;}
            if(f.heroBurstIndex<6&&f.heroBurstRemaining<=1e-8){
                if(config.autoCombat)emitHeroSwordPulse(static_cast<int>(i),f.heroBurstIndex,f.heroBurstAngle,offset);
                ++f.heroBurstIndex;f.heroBurstRemaining=.2;
            }
            if(remaining<=1e-8)break;
        }
    }
    // New hero swords consume only their post-birth portion of this interval.
    // Ordinary evolution pulses below still originate at the step endpoint.
    tickSwordWaves(dt);
    const int minute=static_cast<int>((std::min(elapsed,config.battleSeconds-1e-7)+1e-8)/60);
    while(evolutionMinute<minute){++evolutionMinute;
        for(int i=0;i<10;++i){Vec position;bool distinct=false;for(int attempt=0;attempt<32&&!distinct;++attempt){position={120+random()*(World::Size-240),120+random()*(World::Size-240)};distinct=true;for(const auto& pack:evolutionPacks)if(pack.active&&(pack.position-position).length()<180){distinct=false;break;}}
            evolutionPacks.push_back({position,true,60.0*evolutionMinute});}}
    for(size_t i=0;i<fighters.size();++i){auto& f=fighters[i];if(!f.alive||f.evolutionRemaining<=0||elapsed<=f.evolutionStartedAt+1e-8)continue;
        // Expiry wins a tie with the eighth sword pulse at exactly40seconds.
        if(f.evolutionRemaining<=dt+1e-8){endEvolution(f);continue;}
        f.evolutionRemaining-=dt;f.swordRemaining-=dt;
        if(f.swordRemaining<=1e-8){if(config.autoCombat)emitSwordPulse(static_cast<int>(i));f.swordRemaining+=5;}
    }
    if(config.autoCollect)for(size_t pack=0;pack<evolutionPacks.size();++pack)if(evolutionPacks[pack].active){
        world.queryLimited(evolutionPacks[pack].position,90,candidates,128);int closest=-1;double best=std::numeric_limits<double>::max();
        for(int candidate:candidates)if(fighters[candidate].alive&&!fighters[candidate].isHost&&pickupTouches(world.bodies[candidate],evolutionPacks[pack].position)){const Vec d=world.bodies[candidate].position-evolutionPacks[pack].position;const double distance=d.dot(d);if(distance<best){best=distance;closest=candidate;}}
        if(closest>=0)collectEvolutionPack(fighters[closest].id,static_cast<int>(pack));}
}
inline void Match::tickSwordWaves(double dt) {
    // Separate budgets protect hero waves from the ordinary evolution pool.
    for(auto& wave:swordWaves)if(wave.active){
        auto* owner=findFighter(wave.ownerId);if(!owner||!owner->alive){wave.active=false;continue;}
        const double liveDt=std::max(0.0,dt-wave.stepBirthDelay);wave.stepBirthDelay=0;
        if(liveDt<=1e-8)continue;
        const double travel=std::min(wave.maxDistance-wave.traveled,wave.speed*liveDt);
        const Vec before=wave.position;wave.position+=wave.direction*travel;wave.traveled+=travel;wave.life=std::max(0.0,(wave.maxDistance-wave.traveled)/wave.speed);
        const Vec center=(before+wave.position)*.5;
        const auto nearSegment=[&](Vec position,double radius){const Vec d=position-before;const double along=std::clamp(d.dot(wave.direction),0.0,travel);const Vec off=d-wave.direction*along;return off.dot(off)<=radius*radius;};
        world.queryLimited(center,travel*.5+70,candidates,128);
        for(int candidate:candidates)if(!wave.hitPlayers.test(candidate)&&fighters[candidate].alive&&hostile(*owner,fighters[candidate])&&fighters[candidate].id!=wave.ownerId&&nearSegment(world.bodies[candidate].position,25*world.bodies[candidate].scale+6)){
            wave.hitPlayers.set(candidate);damagePlayer(wave.ownerId,fighters[candidate].id,wave.damage*(wave.hero&&fighters[candidate].heroBuff?4:1));
        }
        const double resourceRadius=travel*.5+40;
        for(int y=cell(center.y-resourceRadius);y<=cell(center.y+resourceRadius);++y)for(int x=cell(center.x-resourceRadius);x<=cell(center.x+resourceRadius);++x)
            for(int npc:npcGrid[y*ResourceCells+x])if(npc<128&&npcs[npc].active&&!wave.hitNpcs.test(npc)&&nearSegment(npcs[npc].position,36)){wave.hitNpcs.set(npc);damageNpc(wave.ownerId,npc,wave.damage);}
        if(!owner->isHost){
            const auto hitPickup=[&](size_t key,double spawnedAt,Vec position){
                if(!nearSegment(position,PickupHalfExtent*std::sqrt(2.0)+6))return false;
                const auto identity=std::make_pair(key,spawnedAt);
                if(std::find(wave.hitPickups.begin(),wave.hitPickups.end(),identity)!=wave.hitPickups.end())return false;
                wave.hitPickups.push_back(identity);return true;
            };
            for(size_t pack=0;pack<evolutionPacks.size();++pack)if(evolutionPacks[pack].active&&hitPickup(pack*2,evolutionPacks[pack].spawnedAt,evolutionPacks[pack].position))damageEvolutionPack(wave.ownerId,static_cast<int>(pack),wave.damage);
            for(size_t crate=0;crate<weaponCrates.size();++crate)if(weaponCrates[crate].active&&hitPickup(crate*2+1,weaponCrates[crate].spawnedAt,weaponCrates[crate].position))damageWeaponCrate(wave.ownerId,static_cast<int>(crate),wave.damage);
        }
        for(int team=1;team<=2;++team)if(team!=owner->team&&bases[team].alive&&!wave.hitBases.test(team)&&nearSegment(bases[team].position,106)){wave.hitBases.set(team);damageBase(wave.ownerId,team,wave.damage);}
        if(boss.active&&!wave.hitBoss&&nearSegment(boss.position,116)){wave.hitBoss=true;damageBoss(wave.ownerId,wave.damage);}
        if(wave.traveled>=wave.maxDistance-1e-8)wave.active=false;
    }
    swordWaves.erase(std::remove_if(swordWaves.begin(),swordWaves.end(),[](const SwordWave& wave){return !wave.active;}),swordWaves.end());
}
inline void Match::attractOrbs(double dt) {
    // Resolve each orb against the eligible magnets once, so two users cannot
    // move or collect the same orb in this interval.
    struct Magnet{int index;double range2;};std::vector<Magnet> magnets;magnets.reserve(fighters.size());
    for(size_t i=0;i<fighters.size();++i)if(fighters[i].alive&&fighters[i].evolutionRemaining>0&&fighters[i].team!=0&&!fighters[i].isHost){
        const double range=weaponFor(fighters[i]).range*(world.bodies[i].shape==Shape::Rectangle?1.5:1);
        magnets.push_back({static_cast<int>(i),range*range});}
    if(magnets.empty())return;
    for(size_t orbIndex=0;orbIndex<orbs.size();++orbIndex)if(orbs[orbIndex].active) {
        auto& orb=orbs[orbIndex];int nearest=-1;double best=std::numeric_limits<double>::max();
        for(const auto& magnet:magnets){const int i=magnet.index;const Vec d=world.bodies[i].position-orb.position;const double d2=d.dot(d);
            if(d2<=magnet.range2&&(d2<best-1e-8||(std::abs(d2-best)<=1e-8&&(nearest<0||fighters[i].id<fighters[nearest].id)))){best=d2;nearest=i;}}
        if(nearest<0)continue;
        if(best<=46*46){collectOrb(fighters[nearest].id,static_cast<int>(orbIndex));continue;}
        const int before=cellIndex(orb.position);const double distance=std::sqrt(best);
        // Distant balls accelerate visibly; contact uses the normal scoring path.
        const double travel=std::min(distance,std::max(700.0,distance*2)*dt);
        orb.position+=(world.bodies[nearest].position-orb.position)*(travel/distance);
        const int after=cellIndex(orb.position);
        if(before!=after){auto& old=orbGrid[before];old.erase(std::remove(old.begin(),old.end(),static_cast<int>(orbIndex)),old.end());orbGrid[after].push_back(static_cast<int>(orbIndex));}
        if(distance-travel<=46)collectOrb(fighters[nearest].id,static_cast<int>(orbIndex));
    }
}
