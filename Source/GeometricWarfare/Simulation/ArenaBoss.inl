#pragma once
// Included after the complete Match definition.
namespace gw {
namespace boss_detail {
inline Vec direction(Vec delta,Vec fallback={1,0}) {const double length=delta.length();return length>1e-9?delta*(1/length):fallback;}
inline double cross(Vec a,Vec b) {return a.x*b.y-a.y*b.x;}
inline double pointSegmentDistance2(Vec p,Vec a,Vec b) {
    const Vec edge=b-a;const double length2=edge.dot(edge);
    const Vec delta=p-(a+edge*(length2>1e-12?std::clamp((p-a).dot(edge)/length2,0.0,1.0):0));return delta.dot(delta);
}
inline bool insidePolygon(Vec p,const detail::Geometry& g) {
    bool positive=false,negative=false;
    for(int i=0;i<g.count;++i){const double side=cross(g.points[(i+1)%g.count]-g.points[i],p-g.points[i]);positive|=side>1e-9;negative|=side< -1e-9;}
    return !(positive&&negative);
}
inline double circleEntry(Vec from,Vec delta,Vec center,double radius) {
    const Vec offset=from-center;const double c=offset.dot(offset)-radius*radius;if(c<=0)return 0;
    const double a=delta.dot(delta),b=offset.dot(delta);if(a<=1e-12||b>=0)return 2;
    const double discriminant=b*b-a*c;if(discriminant<0)return 2;
    const double t=(-b-std::sqrt(discriminant))/a;return t>=0&&t<=1?t:2;
}
// Swept disk against the actual circle/polygon, using each edge's capsule.
inline double bodyEntry(Vec from,Vec to,const Body& body,double radius) {
    const auto g=detail::geometry(body);const Vec start=from-body.position,delta=to-from;
    if(g.count==0)return circleEntry(start,delta,{},radius+g.radius);
    if(insidePolygon(start,g))return 0;
    double first=2;
    for(int i=0;i<g.count;++i){const Vec a=g.points[i],b=g.points[(i+1)%g.count],edge=b-a;
        if(pointSegmentDistance2(start,a,b)<=radius*radius)return 0;
        first=std::min(first,circleEntry(start,delta,a,radius));
        const double edgeLength=edge.length(),rate=cross(edge,delta);
        if(std::abs(rate)<=1e-12)continue;
        for(double sign:{-1.0,1.0}){const double t=(sign*radius*edgeLength-cross(edge,start-a))/rate;if(t<0||t>1)continue;
            const double along=(start+delta*t-a).dot(edge)/(edgeLength*edgeLength);if(along>=0&&along<=1)first=std::min(first,t);}
    }
    return first;
}
inline bool waveTouches(Vec center,double previous,double current,const Body& body) {
    const auto g=detail::geometry(body);const Vec relative=center-body.position;
    if(g.count==0){const double distance=relative.length();return distance-g.radius<=current&&distance+g.radius>=previous;}
    double nearest=insidePolygon(relative,g)?0:std::numeric_limits<double>::max(),furthest=0;
    for(int i=0;i<g.count;++i){nearest=std::min(nearest,pointSegmentDistance2(relative,g.points[i],g.points[(i+1)%g.count]));const Vec d=relative-g.points[i];furthest=std::max(furthest,d.dot(d));}
    return nearest<=current*current&&furthest>=previous*previous;
}
inline void laserBounds(BossState& boss) {
    double after=std::numeric_limits<double>::max();
    const auto clip=[&](double origin,double velocity){if(std::abs(velocity)<1e-12)return;const double edge=velocity>0?World::Size:0;after=std::min(after,(edge-origin)/velocity);};
    clip(boss.position.x,boss.laserDirection.x);clip(boss.position.y,boss.laserDirection.y);
    boss.laserFrom=boss.position;boss.laserTo=boss.position+boss.laserDirection*after;
}
inline uint32_t random(BossState& boss) {auto& s=boss.randomSeed;s^=s<<13;s^=s>>17;s^=s<<5;return s;}
}
inline void Match::resetBoss() {
    boss=BossState{};boss.npcWaveHits.resize(npcs.size(),0);boss.queryScratch.reserve(World::Capacity);teamBuffRemaining={};
}
inline bool Match::damageBoss(int attackerId,double amount) {
    const auto* attacker=findFighter(attackerId);
    if(phase==Phase::Results||!boss.active||!attacker||!attacker->alive||!validAmount(amount))return false;
    const bool alreadyRaging=boss.hp<=boss.maxHp*.4;
    const double applied=applyArmorDamage(amount*damageMultiplierFor(*attacker,true)*(alreadyRaging?.55:1),boss.hp,boss.armor);
    boss.hitFlash=.18;emitDamage(boss.position,applied,0,4,0);boss.rage=boss.hp<=boss.maxHp*.4;
    if(boss.hp<=1e-8){
        boss.hp=0;boss.active=false;boss.lastHitTeam=attacker->team;boss.attack=BossAttack::Idle;boss.attackRemaining=0;boss.jumpHeight=0;boss.waveRadius=0;
        for(auto& projectile:boss.projectiles)projectile.active=false;
        for(auto& fighter:fighters)fighter.movementSlow=1;
        if(attacker->team>=0&&attacker->team<=2){teamBuffRemaining[attacker->team]=60;emit(EventKind::BossReward,attackerId,-1,attacker->team,60);
            for(auto& fighter:fighters)if(fighter.alive&&fighter.team==attacker->team){fighter.bossBuffEligible=true;grantEvolution(fighter.id);}
            if(attacker->team>0){auto& base=bases[attacker->team];if(base.alive&&base.hp>0){base.hp*=2;base.maxHp=std::max(base.maxHp,base.hp);}}}
    }
    return true;
}
inline void Match::applyBossAttraction(double dt) {
    if(!boss.active)return;
    // Only time after the birth endpoint contributes to steering. Movement
    // stays in World::step, so the existing collisions and walls still resolve.
    const double interval=std::min(dt,boss.spawnAge);
    if(interval<=0)return;
    const double blend=1-std::exp(-BossAttractionSteering*interval);
    for(size_t i=0;i<fighters.size();++i)if(fighters[i].alive&&world.bodies[i].active){auto& body=world.bodies[i];
        const Vec toward=boss.position-body.position;const double distance=toward.length();if(distance<=1e-8)continue;
        const double speed=std::max(90*body.speedScale,body.velocity.length());
        body.velocity=body.velocity*(1-blend)+toward*(speed*blend/distance);
    }
}
inline void Match::tickBoss(double dt) {
    // Keep short impact visuals expiring even after the boss has died.
    for(auto& explosion:boss.explosions)if(explosion.active){explosion.age=std::min(explosion.lifetime,explosion.age+dt);if(explosion.age+1e-8>=explosion.lifetime)explosion.active=false;}
    double bodyRadius=34;
    for(auto& fighter:fighters)fighter.movementSlow=1;
    for(const auto& body:world.bodies)bodyRadius=std::max(bodyRadius,34*body.scale);
    boss.hitFlash=std::max(0.0,boss.hitFlash-dt);
    if(!boss.spawned&&elapsed+1e-8>=BossSpawnSeconds){boss.spawned=boss.active=true;dt=std::min(dt,std::max(0.0,elapsed-BossSpawnSeconds));}
    if(boss.spawned)boss.spawnAge+=dt;
    boss.position=boss.fireCenter;
    // Neutral actors and reward containers are solid scenery to players, but
    // every boss hazard passes through them without damage or displacement.
    const auto damageRay=[&](Vec from,Vec to,Vec direction,double radius,double damage){
        world.query((from+to)*.5,(to-from).length()*.5+radius+bodyRadius,boss.queryScratch);
        for(int index:boss.queryScratch)if(fighters[index].alive&&(world.bodies[index].position-from).dot(direction)>=0&&boss_detail::bodyEntry(from,to,world.bodies[index],radius)<=1)
            damageEnvironment(fighters[index].id,damage);
    };
    const auto tickScorches=[&](double interval){
        for(auto& scorch:boss.scorches)if(scorch.active){
            scorch.age=std::min(scorch.lifetime,scorch.age+interval);
            const int ticks=static_cast<int>(std::floor((scorch.age+1e-8)/.1));
            while(scorch.ticks<ticks){++scorch.ticks;if(config.autoCombat)damageRay(scorch.from,scorch.to,scorch.direction,BossScorchWidthAt(scorch.ticks*.1)*.5,10);}
            if(scorch.age+1e-8>=scorch.lifetime)scorch.active=false;
        }
    };
    if(!boss.active||!config.autoCombat){tickScorches(dt);return;}
    boss.rage=boss.hp<=boss.maxHp*.4;
    const double damageScale=boss.rage?2:1;
    const auto findTarget=[&](){
        const auto* current=findFighter(boss.targetId);
        if(current&&current->alive&&(world.bodies[indices.at(current->id)].position-boss.position).length()<=BossAttackRange)return;
        boss.targetId=-1;double best=BossAttackRange*BossAttackRange+1;
        world.queryLimited(boss.position,BossAttackRange,boss.queryScratch,128);
        for(int index:boss.queryScratch)if(fighters[index].alive){const Vec delta=world.bodies[index].position-boss.position;const double distance=delta.dot(delta);if(distance<best){best=distance;boss.targetId=fighters[index].id;}}
    };
    // Existing bullets move first, so a bullet born at this endpoint is not aged early.
    for(auto& projectile:boss.projectiles)if(projectile.active){
        const double travel=std::min(BossAttackRange-projectile.travelled,BossBulletSpeed*dt);const Vec from=projectile.position,to=from+projectile.velocity*(travel/BossBulletSpeed);
        world.query((from+to)*.5,travel*.5+projectile.radius+bodyRadius,boss.queryScratch);
        int victim=-1;double first=2;
        for(int index:boss.queryScratch)if(fighters[index].alive){const double t=boss_detail::bodyEntry(from,to,world.bodies[index],projectile.radius);if(t<first){first=t;victim=index;}}
        if(victim>=0){
            projectile.position=from+(to-from)*first;projectile.travelled+=travel*first;projectile.active=false;
            auto& explosion=boss.explosions[boss.explosionCursor];boss.explosionCursor=(boss.explosionCursor+1)%boss.explosions.size();
            explosion={projectile.position,0,BossExplosionLifetime,BossExplosionRadius,true};
            world.query(explosion.position,explosion.radius+bodyRadius,boss.queryScratch);bool moved=false;
            for(int index:boss.queryScratch)if(fighters[index].alive&&boss_detail::bodyEntry(explosion.position,explosion.position,world.bodies[index],explosion.radius)<=1){
                auto& body=world.bodies[index];const Vec outward=boss_detail::direction(body.position-explosion.position,boss_detail::direction(projectile.velocity));
                const double radius=detail::geometry(body).radius;const Vec movedTo=body.position+outward*60;
                body.position={std::clamp(movedTo.x,radius,World::Size-radius),std::clamp(movedTo.y,radius,World::Size-radius)};body.velocity+=outward*280;
                damageEnvironment(fighters[index].id,projectile.damage);moved=true;
            }
            if(moved)world.rebuildSpatial();
        }
        else {projectile.position=to;projectile.travelled+=travel;if(projectile.travelled+1e-8>=BossAttackRange)projectile.active=false;}
    }
    const auto changeAttack=[&](BossAttack attack){boss.attack=attack;boss.attackElapsed=0;boss.attackInitialized=false;boss.attackRemaining=0;};
    const auto complete=[&](){++boss.attacksCompleted;boss.targetId=-1;const uint32_t choice=boss_detail::random(boss)%3;changeAttack(choice==0?BossAttack::Bullet:(choice==1?BossAttack::LaserWindup:BossAttack::StompJump));};
    const auto initialize=[&](){
        boss.attackInitialized=true;findTarget();
        if(boss.attack==BossAttack::LaserWindup)audio.emit(boss.rage?AudioKind::BossLaserRage:AudioKind::BossLaserWindup);
        if(boss.attack==BossAttack::LaserActive)audio.emit(AudioKind::BossLaserBeam);
        if(boss.attack==BossAttack::StompJump)audio.emit(AudioKind::BossStompJump);
        if(boss.attack==BossAttack::StompWave)audio.emit(AudioKind::BossStompImpact);
        if(boss.attack==BossAttack::LaserWindup){boss.laserDirection=boss.targetId>=0?boss_detail::direction(world.bodies[indices.at(boss.targetId)].position-boss.position):Vec{1,0};boss_detail::laserBounds(boss);}
        if(boss.attack==BossAttack::LaserActive){boss.laserTicks=0;boss_detail::laserBounds(boss);
            if(boss.rage){auto& scorch=boss.scorches[boss.scorchCursor];boss.scorchCursor=(boss.scorchCursor+1)%boss.scorches.size();
                scorch={boss.laserFrom,boss.laserTo,boss.laserDirection,0,5,0,true};}}
        if(boss.attack==BossAttack::StompJump){boss.jumpFrom=boss.landingPosition=boss.fireCenter;boss.jumpHeight=0;}
        if(boss.attack==BossAttack::StompWave){boss.waveCenter=boss.position;boss.waveRadius=0;
            if(++boss.waveSerial==0){std::fill(boss.waveHits.begin(),boss.waveHits.end(),0);std::fill(boss.npcWaveHits.begin(),boss.npcWaveHits.end(),0);++boss.waveSerial;}
            if(boss.npcWaveHits.size()!=npcs.size())boss.npcWaveHits.resize(npcs.size(),0);}
    };
    const auto pulseLaser=[&](double pulseElapsed){
        const double radius=BossLaserWidthAt(pulseElapsed)*.5;
        // Reject rear centers for both hazards so their rounded origin cap
        // cannot hit bodies behind the locked ray.
        damageRay(boss.laserFrom,boss.laserTo,boss.laserDirection,radius,150*damageScale);
    };
    const auto expandWave=[&](double previous){
        world.query(boss.waveCenter,boss.waveRadius+bodyRadius,boss.queryScratch);bool moved=false;
        for(int index:boss.queryScratch)if(fighters[index].alive&&boss.waveHits[index]!=boss.waveSerial&&boss_detail::waveTouches(boss.waveCenter,previous,boss.waveRadius,world.bodies[index])){
            boss.waveHits[index]=boss.waveSerial;auto& body=world.bodies[index];const Vec outward=boss_detail::direction(body.position-boss.waveCenter);
            const double radius=detail::geometry(body).radius;const Vec movedTo=body.position+outward*60;body.position={std::clamp(movedTo.x,radius,World::Size-radius),std::clamp(movedTo.y,radius,World::Size-radius)};body.velocity+=outward*280;
            damageEnvironment(fighters[index].id,45*damageScale);moved=true;
        }
        if(moved)world.rebuildSpatial();
    };
    if(boss.attack==BossAttack::Idle){const uint32_t choice=boss_detail::random(boss)%3;changeAttack(choice==0?BossAttack::Bullet:(choice==1?BossAttack::LaserWindup:BossAttack::StompJump));}
    double remaining=dt;
    // Normal Match steps are <=1/30s; the guard also bounds malformed external state.
    for(int transitions=0;transitions<8;++transitions){
        if(!boss.attackInitialized)initialize();
        double duration=0;
        switch(boss.attack){case BossAttack::Bullet:duration=boss.rage?2.5:5;break;case BossAttack::LaserWindup:duration=boss.rage?3:5;break;
        case BossAttack::LaserActive:duration=3;break;case BossAttack::StompJump:case BossAttack::StompWave:duration=1;break;case BossAttack::StompRest:duration=boss.rage?0:2;break;default:break;}
        const double slice=std::min(remaining,std::max(0.0,duration-boss.attackElapsed));boss.attackElapsed+=slice;remaining-=slice;boss.attackRemaining=std::max(0.0,duration-boss.attackElapsed);
        tickScorches(slice);
        if(boss.attack==BossAttack::LaserWindup){findTarget();if(boss.targetId>=0)boss.laserDirection=boss_detail::direction(world.bodies[indices.at(boss.targetId)].position-boss.position,boss.laserDirection);boss_detail::laserBounds(boss);}
        if(boss.attack==BossAttack::LaserActive){const int pulses=std::min(15,static_cast<int>(std::floor((boss.attackElapsed+1e-8)/.2)));while(boss.laserTicks<pulses){++boss.laserTicks;pulseLaser(boss.laserTicks*.2);}}
        if(boss.attack==BossAttack::StompJump){const double t=std::clamp(boss.attackElapsed,0.0,1.0);boss.jumpHeight=std::sin(t*detail::Tau*.5)*120;}
        if(boss.attack==BossAttack::StompWave){const double previous=boss.waveRadius;boss.waveRadius=BossWaveRadius*std::clamp(boss.attackElapsed,0.0,1.0);expandWave(previous);}
        if(boss.attackRemaining>1e-8)break;
        switch(boss.attack){
        case BossAttack::Bullet:{findTarget();if(boss.targetId>=0){
            audio.emit(AudioKind::BossBullet);
            const auto fire=[&](Vec direction){auto& projectile=boss.projectiles[boss.projectileCursor];boss.projectileCursor=(boss.projectileCursor+1)%boss.projectiles.size();
                projectile={boss.position,direction*BossBulletSpeed,BossBulletRadius,0,100*damageScale,true};++boss.projectilesFired;};
            fire(boss_detail::direction(world.bodies[indices.at(boss.targetId)].position-boss.position));
            if(boss.rage)for(Vec direction:{Vec{1,0},Vec{0,1},Vec{-1,0},Vec{0,-1}})fire(direction);
        }complete();break;}
        case BossAttack::LaserWindup:changeAttack(BossAttack::LaserActive);break;
        case BossAttack::LaserActive:complete();break;
        case BossAttack::StompJump:boss.jumpHeight=0;changeAttack(BossAttack::StompWave);break;
        case BossAttack::StompWave:if(boss.rage)complete();else changeAttack(BossAttack::StompRest);break;
        case BossAttack::StompRest:complete();break;
        default:complete();break;}
        // Initialize the new phase at the exact endpoint for rendering and timers.
        if(remaining<=1e-10){initialize();switch(boss.attack){case BossAttack::Bullet:boss.attackRemaining=boss.rage?2.5:5;break;case BossAttack::LaserWindup:boss.attackRemaining=boss.rage?3:5;break;case BossAttack::LaserActive:boss.attackRemaining=3;break;case BossAttack::StompJump:case BossAttack::StompWave:boss.attackRemaining=1;break;case BossAttack::StompRest:boss.attackRemaining=boss.rage?0:2;break;default:break;}break;}
    }
    for(size_t i=0;i<fighters.size();++i){auto& f=fighters[i];if(!f.alive||!boss.isInFire(world.bodies[i].position)){boss.fireExposure[i]=0;continue;}
        f.movementSlow=.8;boss.fireExposure[i]+=dt;
        // Fire is an environmental hazard, so rage does not amplify its 1/s.
        while(boss.fireExposure[i]+1e-8>=1&&f.alive){boss.fireExposure[i]=std::max(0.0,boss.fireExposure[i]-1);damageEnvironment(f.id,1);}
    }
}
} // namespace gw
