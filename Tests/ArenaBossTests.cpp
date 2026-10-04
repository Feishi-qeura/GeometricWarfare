#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace gw;
static int assertions=0;
static void require(bool value,const char* message) { ++assertions;if(!value)throw std::runtime_error(message); }
static bool close(double a,double b) { return std::abs(a-b)<1e-6; }
static Match quiet() { MatchConfig c;c.autoCombat=false;c.autoCollect=false;c.naturalOrbs=0;c.npcCount=0;return Match(c); }
static void place(Match& m,int id,Vec p) {auto* b=m.world.find(id);b->position=p;b->velocity={};m.world.rebuildSpatial();}
static void pinned(Match& m,double seconds) {
    std::vector<Vec> positions;for(const auto& b:m.world.bodies)positions.push_back(b.position);
    while(seconds>1e-8){const double dt=std::min(.01,seconds);for(size_t i=0;i<positions.size();++i){m.world.bodies[i].position=positions[i];m.world.bodies[i].velocity={};}m.world.rebuildSpatial();m.step(dt);seconds-=dt;}
    for(size_t i=0;i<positions.size();++i){m.world.bodies[i].position=positions[i];m.world.bodies[i].velocity={};}m.world.rebuildSpatial();
}
static Match combat() {auto m=quiet();m.step(BossSpawnSeconds);m.config.autoCombat=true;m.weapon.damage=0;return m;}
static void target(Match& m,int id,Vec p,Shape shape=Shape::Rectangle,int team=1) {require(m.add(id,shape,team),"target joins");auto* f=m.findFighter(id);f->hp=f->maxHp=10000;place(m,id,p);}
static void forceAttack(Match& m,BossAttack attack) {m.boss.attack=attack;m.boss.attackElapsed=0;m.boss.attackInitialized=false;m.boss.targetId=-1;}
static int nearestNpc(const Match& m) {int nearest=-1;double distance=450;for(size_t i=0;i<m.npcs.size();++i){const double next=(m.npcs[i].position-m.boss.position).length();if(next<distance){distance=next;nearest=static_cast<int>(i);}}require(nearest>=0,"deterministic NPC layout includes a target near stationary boss");return nearest;}
static void spawnBoundary() {
    auto m=quiet();m.step(149.999);require(!m.boss.active&&!m.boss.spawned,"boss absent before 150 seconds");
    m.step(.001);require(m.boss.active&&m.boss.spawned&&close(m.boss.hp,32000),"boss spawns exactly 150 seconds with 32000 hp");
    require(close(m.boss.position.x,4000)&&close(m.boss.position.y,4000),"boss spawns at arena center");
    require(m.add(1,Shape::Rectangle,1),"boss attacker joins");
    require(m.damageBoss(1,100000)&&!m.boss.active,"boss can be killed");
    m.step(50);require(!m.boss.active&&m.boss.spawned,"boss never respawns within same round");
    m.reset();require(!m.boss.active&&!m.boss.spawned,"round reset clears boss spawn state");
    m.step(BossSpawnSeconds);require(m.boss.active,"boss spawns in reset round");
}
static void bulletTiming() {
    auto m=combat();target(m,1,{4700,4000});forceAttack(m,BossAttack::Bullet);
    pinned(m,4.999);require(m.boss.projectilesFired==0,"bullet waits full five seconds");pinned(m,.001);
    require(m.boss.projectilesFired==1&&m.boss.attacksCompleted==1,"bullet fires exactly at five seconds and chooses next event");
    const auto& p=m.boss.projectiles[0];require(p.active&&close(p.radius,17.6)&&close(p.velocity.length(),1000),"bullet has required radius and bounded speed");
    forceAttack(m,BossAttack::StompRest);m.boss.attackInitialized=true;m.boss.attackElapsed=-100;
    pinned(m,.8);require(close(m.findFighter(1)->hp,9900),"bullet hits once for one hundred damage");
    require(!m.boss.projectiles[0].active,"bullet stops at first hit");
    require(close(m.bases[1].hp,2500)&&close(m.bases[2].hp,2500),"boss bullet cannot damage bases");
    auto range=combat();target(range,2,{100,100});forceAttack(range,BossAttack::StompRest);range.boss.attackInitialized=true;range.boss.attackElapsed=-100;
    range.boss.projectiles[0]={{100,4000},{1000,0},BossBulletRadius,3990,100,true};pinned(range,.02);
    require(!range.boss.projectiles[0].active&&close(range.boss.projectiles[0].travelled,4000),"bullet expires exactly at maximum range");
}
static void laserTiming() {
    auto m=combat();target(m,1,{5000,4000});target(m,2,{1000,4000});target(m,3,{6500,4000});forceAttack(m,BossAttack::LaserWindup);m.boss.targetId=1;
    pinned(m,2);place(m,1,{5000,4500});pinned(m,1);
    require(close(m.boss.laserFrom.x,4000)&&close(m.boss.laserFrom.y,4000),"warning ray begins at boss, never behind boss");
    require(m.boss.laserTo.y>4000,"windup laser follows moving target");place(m,1,{5000,4000});pinned(m,1.999);
    require(m.boss.attack==BossAttack::LaserWindup&&close(m.findFighter(1)->hp,10000),"five second laser windup deals no damage");pinned(m,.001);
    require(m.boss.attack==BossAttack::LaserActive,"laser locks at exactly five seconds");const Vec from=m.boss.laserFrom,to=m.boss.laserTo;
    place(m,1,{5000,5000});pinned(m,.199);require(close(m.findFighter(3)->hp,10000),"laser waits until its first point two second tick");pinned(m,.001);
    require(close(m.findFighter(3)->hp,9850)&&close(m.findFighter(1)->hp,10000),"first pulse hits forward player but not target that left locked ray");
    require(close(m.findFighter(2)->hp,10000),"single direction laser never hits player behind boss");
    require(close(m.boss.laserFrom.x,from.x)&&close(m.boss.laserTo.y,to.y),"active laser stays locked after target moves");
    pinned(m,2.8);require(close(m.findFighter(3)->hp,7750)&&close(m.findFighter(2)->hp,10000),"laser applies exactly fifteen forward-only one hundred fifty damage pulses");
    require(m.boss.attacksCompleted==1&&m.boss.attack!=BossAttack::LaserActive,"laser ends after three seconds");
}
static void stompTiming() {
    auto m=combat();target(m,1,{4700,4000});forceAttack(m,BossAttack::StompJump);m.boss.targetId=1;
    pinned(m,.5);require(m.boss.jumpHeight>100,"jump visibly leaves ground");
    require(close(m.boss.position.x,4000)&&close(m.boss.position.y,4000),"jump changes height without moving boss toward target");pinned(m,.5);
    require(m.boss.attack==BossAttack::StompWave&&close(m.boss.jumpHeight,0),"jump lands after one second");
    require(close(m.boss.landingPosition.x,4000)&&close(m.boss.landingPosition.y,4000)&&close(m.boss.waveCenter.x,4000)&&close(m.boss.waveCenter.y,4000),"landing and shockwave stay at original spawn center");
    place(m,1,m.boss.position+Vec{660,0});double waveTime=0,beforeHit=0;
    while(m.findFighter(1)->hp>9955&&waveTime<.9){beforeHit=m.world.find(1)->position.x;m.step(.01);waveTime+=.01;}
    require(close(m.findFighter(1)->hp,9955),"wave deals forty five damage");
    require(m.world.find(1)->position.x>beforeHit+50&&m.world.find(1)->velocity.x>0,"wave knocks target outward while attraction continues steering");pinned(m,.9-waveTime);
    pinned(m,.1);require(close(m.findFighter(1)->hp,9955),"one target receives at most one hit per wave");
    require(m.boss.attack==BossAttack::StompRest&&close(m.boss.waveRadius,900),"wave reaches fire radius times one point five then rests");
    pinned(m,1.999);require(m.boss.attacksCompleted==0,"stomp rest lasts full two seconds");pinned(m,.001);
    require(m.boss.attacksCompleted==1,"stomp completes after two second rest");
}
static void rageAndFire() {
    auto m=combat();target(m,1,{4000,4000});m.boss.hp=m.boss.maxHp*.4+1;
    require(m.damageBoss(1,1)&&m.boss.rage&&close(m.boss.hp,m.boss.maxHp*.4),"rage enters exactly at forty percent health");
    require(close(m.boss.hitFlash,.18),"boss hit flashes on valid damage");forceAttack(m,BossAttack::Bullet);
    pinned(m,.999);require(close(m.findFighter(1)->hp,10000),"fire waits a full second of exposure");pinned(m,.001);
    require(close(m.findFighter(1)->hp,9999)&&close(m.findFighter(1)->movementSlow,.8),"rage fire remains one damage per second and slows twenty percent");
    require(close(m.boss.hitFlash,0),"boss hit flash expires");place(m,1,{4700,4000});pinned(m,1.499);
    require(m.boss.projectilesFired==0&&close(m.findFighter(1)->movementSlow,1),"rage bullet still waits until two point five seconds and leaving fire restores speed");pinned(m,.001);
    require(m.boss.projectilesFired==5&&close(m.boss.projectiles[0].damage,200),"rage bullet fires five missiles at two point five seconds for double damage");
    auto laser=combat();target(laser,2,{5000,4000});target(laser,4,{1000,4000});laser.boss.hp=1600;laser.boss.rage=true;forceAttack(laser,BossAttack::LaserWindup);laser.boss.targetId=2;
    pinned(laser,2.999);require(laser.boss.attack==BossAttack::LaserWindup,"rage windup waits three seconds");pinned(laser,.001);
    require(laser.boss.attack==BossAttack::LaserActive,"rage windup finishes at three seconds");pinned(laser,1);require(close(laser.findFighter(2)->hp,8400),"rage beam deals five double damage pulses plus ten scorch ticks per second");
    require(close(laser.findFighter(4)->hp,10000)&&close(laser.boss.laserFrom.x,4000)&&close(laser.boss.laserTo.x,8000),"rage laser remains single direction from boss to forward edge");
    auto stomp=combat();target(stomp,3,{4700,4000});stomp.boss.rage=true;stomp.boss.hp=1600;forceAttack(stomp,BossAttack::StompWave);
    pinned(stomp,1);require(stomp.boss.attacksCompleted==1&&stomp.boss.attack!=BossAttack::StompRest,"rage stomp skips rest");
    require(close(stomp.findFighter(3)->hp,9910),"rage wave doubles damage");
}
static void environmentAndReward() {
    auto m=combat();target(m,1,{4000,4000},Shape::Circle);m.findFighter(1)->hp=1;forceAttack(m,BossAttack::StompRest);m.boss.attackInitialized=true;m.boss.attackElapsed=-100;
    pinned(m,1);require(close(m.findFighter(1)->hp,.5),"fire uses circle shape defence");m.findFighter(1)->score=37;pinned(m,1);
    require(!m.findFighter(1)->alive&&m.findFighter(1)->deaths==1&&m.findFighter(1)->score==30,"fire death retains the unaffected eighty percent of carried score");
    int64_t drops=0;for(const auto& o:m.orbs)if(o.active&&!o.natural)drops+=o.value;require(drops==7,"environment kill drops the floored twenty percent score loss");
    auto armor=combat();target(armor,2,{4500,4000});armor.findFighter(2)->armor=100;armor.boss.projectiles[0]={{4400,4000},{1000,0},BossBulletRadius,0,100,true};forceAttack(armor,BossAttack::StompRest);armor.boss.attackInitialized=true;armor.boss.attackElapsed=-100;
    pinned(armor,.1);require(close(armor.findFighter(2)->hp,9970)&&close(armor.findFighter(2)->armor,40.5),"boss damage uses shared armor split");
    auto reward=quiet();reward.step(BossSpawnSeconds);reward.add(3,Shape::Rectangle,1);reward.bases[1].hp=2000;
    require(reward.damageBoss(3,100000)&&close(reward.teamBuffRemaining[1],60)&&close(reward.teamBuffRemaining[2],0),"last hit grants exactly one team sixty second buff");
    require(close(reward.bases[1].hp,4000)&&close(reward.bases[1].maxHp,4000)&&reward.findFighter(3)->score==0,"base current hp doubles beyond original cap with no personal reward");
    reward.step(59.999);require(reward.teamBuffRemaining[1]>0,"buff persists until full sixty seconds");reward.step(.001);require(close(reward.teamBuffRemaining[1],0),"buff expires exactly at sixty seconds");
    auto destroyed=quiet();destroyed.step(BossSpawnSeconds);destroyed.add(4,Shape::Rectangle,2);destroyed.bases[2].alive=false;destroyed.bases[2].hp=0;destroyed.damageBoss(4,100000);
    require(!destroyed.bases[2].alive&&close(destroyed.bases[2].hp,0)&&close(destroyed.teamBuffRemaining[2],60),"boss reward does not revive destroyed base");
    auto gray=quiet();gray.step(BossSpawnSeconds);gray.add(5,Shape::Rectangle,0);gray.damageBoss(5,100000);require(close(gray.teamBuffRemaining[0],60)&&gray.hasBossBuff(*gray.findFighter(5)),"gray last hit grants the gray team its sixty second buff");
    auto host=quiet();host.step(BossSpawnSeconds);require(host.addHost(6,1),"host joins");host.damageBoss(6,100000);require(close(host.teamBuffRemaining[1],60)&&host.findFighter(6)->score==0,"host last hit grants own team buff without personal score");
}
static void npcWaveAndSafety() {
    MatchConfig c;c.autoCombat=false;c.autoCollect=false;c.naturalOrbs=0;c.npcCount=512;Match m(c);m.step(BossSpawnSeconds);m.config.autoCombat=true;m.weapon.damage=0;
    const int index=nearestNpc(m);auto& npc=m.npcs[index];const Vec original=npc.position;forceAttack(m,BossAttack::StompWave);m.step(.5);
    require(close(npc.hp,200)&&(npc.position-original).length()<1e-8,"neutral NPC is immune to boss shockwave damage and knockback");m.step(.5);require(close(npc.hp,200),"entire shockwave leaves neutral NPC health unchanged");
    npc.hp=40;forceAttack(m,BossAttack::StompWave);m.step(1);require(npc.active&&close(npc.hp,40)&&close(npc.respawnRemaining,0),"low health NPC remains alive through repeated boss waves");
    auto safety=quiet();safety.step(BossSpawnSeconds);safety.add(9,Shape::Rectangle,1);require(!safety.damageBoss(9,-1)&&!safety.damageBoss(9,std::numeric_limits<double>::infinity()),"boss rejects invalid damage");
    safety.phase=Phase::Results;require(!safety.damageBoss(9,100),"results phase rejects boss damage");safety.reset();require(safety.boss.projectiles.size()==32&&safety.boss.waveHits.size()<=World::Capacity+1,"boss pools remain fixed and bounded after reset");
}
static void collisionEdges() {
    Body rectangle;rectangle.shape=Shape::Rectangle;rectangle.position={4000,4000};
    require(boss_detail::bodyEntry({3900,4035},{4100,4035},rectangle,17.6)>1,"bullet misses outside flat rectangle despite circumcircle overlap");
    require(boss_detail::bodyEntry({3900,4032.5},{4100,4032.5},rectangle,17.6)<=1,"bullet catches rectangle edge within radius");
    rectangle.angle=detail::Tau/8;
    require(boss_detail::bodyEntry({3950,4000},{4050,4000},rectangle,17.6)<=1,"swept bullet hits rotated polygon");
    Body circle;circle.position={4000,4000};circle.scale=2.5;
    require(boss_detail::bodyEntry({3900,4060},{4100,4060},circle,17.6)<=1,"host sized circular envelope receives edge hits");
    rectangle.angle=0;
    require(!boss_detail::waveTouches({4000,4100},0,84,rectangle),"wave broadphase false positive does not damage polygon");
    require(boss_detail::waveTouches({4000,4100},0,85,rectangle),"wave reaches exact polygon edge");
    require(!boss_detail::waveTouches({4000,4100},120,140,rectangle),"wave does not hit a unit wholly behind passed front");
    auto m=combat();target(m,1,{4300,4000});target(m,2,{4600,4000});forceAttack(m,BossAttack::StompRest);m.boss.attackInitialized=true;m.boss.attackElapsed=-100;
    m.boss.projectiles[0]={{4000,4000},{1000,0},BossBulletRadius,0,100,true};pinned(m,.8);
    require(close(m.findFighter(1)->hp,9900)&&close(m.findFighter(2)->hp,10000),"projectile chooses first intersecting target and does not pierce");
}
static void exposureAndRageTransitions() {
    auto m=combat();target(m,1,{4000,4000});forceAttack(m,BossAttack::StompRest);m.boss.attackInitialized=true;m.boss.attackElapsed=-100;
    pinned(m,.6);place(m,1,{4700,4000});pinned(m,.01);place(m,1,{4000,4000});pinned(m,.6);
    require(close(m.findFighter(1)->hp,10000),"leaving fire resets fractional exposure");pinned(m,.4);require(close(m.findFighter(1)->hp,9999),"fresh full exposure damages exactly once");
    m.damageBoss(1,100000);require(close(m.findFighter(1)->movementSlow,1),"boss death immediately clears fire slow");pinned(m,2);require(close(m.findFighter(1)->hp,19998),"dead boss fire is inactive after its evolution reward doubles current hp");
    auto bullet=combat();target(bullet,2,{5000,4000});forceAttack(bullet,BossAttack::Bullet);pinned(bullet,3);bullet.damageBoss(2,bullet.boss.maxHp*.6);pinned(bullet,.01);
    require(bullet.boss.projectilesFired==5&&close(bullet.boss.projectiles[0].damage,200),"entering rage shortens in-progress bullet cooldown");
    auto laser=combat();target(laser,3,{5000,4000});forceAttack(laser,BossAttack::LaserWindup);pinned(laser,3.5);laser.damageBoss(3,laser.boss.maxHp*.6);pinned(laser,.01);
    require(laser.boss.attack==BossAttack::LaserActive,"entering rage shortens in-progress laser windup");
    auto rest=combat();target(rest,4,{5000,4000});forceAttack(rest,BossAttack::StompRest);pinned(rest,1);rest.damageBoss(4,rest.boss.maxHp*.6);pinned(rest,.01);
    require(rest.boss.attacksCompleted==1,"entering rage cancels existing stomp rest");
}
static void rewardIntegration() {
    MatchConfig c;c.autoCombat=false;c.autoCollect=false;c.naturalOrbs=10;c.npcCount=1;Match m(c);m.step(BossSpawnSeconds);m.add(1,Shape::Rectangle,1);m.add(2,Shape::Rectangle,2);m.add(3,Shape::Rectangle,1);
    m.damageBoss(1,100000);for(int i=0;i<5;++i)require(m.collectOrb(1,i),"buffed natural orb can be collected");
    require(m.findFighter(1)->score==12,"twenty percent score buff accumulates fractions over five two point pickups");
    m.damagePlayer(1,2,100);require(close(m.findFighter(2)->hp,80),"boss team buff amplifies outgoing player damage");
    m.findFighter(2)->score=71;m.damagePlayer(1,2,100);require(m.findFighter(1)->score==26&&m.findFighter(2)->score==57,"twenty percent player score transfer is not multiplied by boss score buff");
    m.damageNpc(3,0,1000);require(m.findFighter(3)->score==30,"boss score buff amplifies newly earned NPC reward");
    m.step(60);m.collectOrb(1,5);require(m.findFighter(1)->score==28,"expired boss buff restores ordinary orb value");
    auto shapes=quiet();shapes.step(BossSpawnSeconds);shapes.add(11,Shape::Square,1);shapes.add(12,Shape::Triangle,1);shapes.addHost(13,1);
    shapes.damageBoss(11,20);require(close(shapes.boss.hp,shapes.boss.maxHp-50),"square neutral damage bonus applies against boss");shapes.damageBoss(12,20);require(close(shapes.boss.hp,shapes.boss.maxHp-80),"triangle damage bonus applies against boss");
    shapes.damageBoss(13,20);require(close(shapes.boss.hp,shapes.boss.maxHp-100),"host does not inherit circular spectator passives");
    shapes.boss.armor=100;shapes.damageBoss(13,100);require(close(shapes.boss.hp,shapes.boss.maxHp-130)&&close(shapes.boss.armor,40.5),"boss damage path honors armor if configured");
}
static void npcGridAndReset() {
    MatchConfig c;c.autoCombat=false;c.autoCollect=false;c.naturalOrbs=0;c.npcCount=512;Match m(c);m.step(BossSpawnSeconds);m.config.autoCombat=true;m.weapon.damage=0;
    // Boss hazards cannot displace a neutral NPC or invalidate its resource cell.
    const int index=nearestNpc(m);auto& npc=m.npcs[index];const Vec start=npc.position;npc.hp=10000;
    const auto changedCell=[&](){return std::floor(npc.position.x/250)!=std::floor(start.x/250)||std::floor(npc.position.y/250)!=std::floor(start.y/250);};
    for(int i=0;i<4&&!changedCell();++i){forceAttack(m,BossAttack::StompWave);m.step(1);}
    require(!changedCell()&&(npc.position-start).length()<1e-8,"repeated boss waves leave the static NPC and resource grid unchanged");
    m.boss.active=false;m.weapon.range=5;target(m,1,npc.position);m.findFighter(1)->acquisitionRemaining=0;m.step(.001);
    require(m.findFighter(1)->targetKind==2&&m.findFighter(1)->targetIndex==index,"static NPC remains discoverable through the resource grid");
    m.boss.projectiles[0].active=true;m.teamBuffRemaining[1]=42;m.reset();
    require(!m.boss.spawned&&!m.boss.active&&!m.boss.projectiles[0].active&&close(m.teamBuffRemaining[1],0),"reset clears boss transient attacks and team buff");
    require(std::all_of(m.boss.fireExposure.begin(),m.boss.fireExposure.end(),[](double v){return v==0;}),"reset clears per-player fire exposure");
    auto next=quiet();next.step(BossSpawnSeconds);next.add(2,Shape::Rectangle,1);next.damageBoss(2,100000);next.step(next.config.battleSeconds+next.config.resultsSeconds-BossSpawnSeconds);
    require(next.round==2&&!next.boss.spawned&&close(next.elapsed,0),"next round resets once-only boss state");next.step(BossSpawnSeconds);require(next.boss.active&&close(next.boss.hp,32000),"new round receives a fresh boss at 150 seconds");
}
static void stationaryEveryStage() {
    for(bool rage:{false,true})for(BossAttack attack:{BossAttack::Bullet,BossAttack::LaserWindup,BossAttack::LaserActive,BossAttack::StompJump,BossAttack::StompWave,BossAttack::StompRest}){
        auto m=combat();target(m,1,{5000,4500});m.boss.hp=m.boss.maxHp*(rage?.1:1);m.boss.rage=rage;forceAttack(m,attack);m.boss.targetId=1;
        const double duration=attack==BossAttack::Bullet?(rage?2.5:5):(attack==BossAttack::LaserWindup?(rage?3:5):(attack==BossAttack::LaserActive?3:(attack==BossAttack::StompRest?(rage?.1:2):1)));
        for(double time=0;time<duration-1e-8;time+=.1){pinned(m,std::min(.1,duration-time));require(close(m.boss.position.x,4000)&&close(m.boss.position.y,4000),"boss remains at center through every ordinary and rage attack stage");}
    }
    for(const Vec aim:{Vec{1,0},Vec{-1,0},Vec{0,1},Vec{0,-1},Vec{1,1},Vec{-1,1},Vec{1,-1},Vec{-1,-1}}){
        BossState boss;boss.laserDirection=boss_detail::direction(aim);boss_detail::laserBounds(boss);
        require(close(boss.laserFrom.x,4000)&&close(boss.laserFrom.y,4000),"all warning orientations originate at stationary boss");
        const Vec ray=boss.laserTo-boss.position;require(ray.dot(boss.laserDirection)>0,"laser endpoint is strictly forward of boss");
        require(close(boss.laserTo.x,0)||close(boss.laserTo.x,8000)||close(boss.laserTo.y,0)||close(boss.laserTo.y,8000),"single direction laser stops at forward world boundary");
    }
}
static void closeBehindLaser() {
    for(bool rage:{false,true}){
        auto m=combat();target(m,1,{5000,4000});target(m,2,{3990,4000},Shape::Rectangle);target(m,3,{3990,4010},Shape::Circle);
        m.boss.hp=m.boss.maxHp*(rage?.1:1);m.boss.rage=rage;forceAttack(m,BossAttack::LaserActive);m.boss.laserDirection={1,0};pinned(m,1);
        require(close(m.findFighter(1)->hp,rage?8400:9250),"single direction beam still damages a forward target");
        require(close(m.findFighter(2)->hp,9999)&&close(m.findFighter(3)->hp,9999.5),"targets immediately behind origin receive fire only, never beam cap damage");
    }
}
static void expandedHazards() {
    auto fire=combat();target(fire,1,{4600,4000});target(fire,2,{4600.01,4100});
    forceAttack(fire,BossAttack::StompRest);fire.boss.attackInitialized=true;fire.boss.attackElapsed=-100;
    pinned(fire,1);require(close(fire.findFighter(1)->hp,9999)&&close(fire.findFighter(1)->movementSlow,.8),"fire reaches the six hundred unit boundary");
    require(close(fire.findFighter(2)->hp,10000)&&close(fire.findFighter(2)->movementSlow,1),"fire excludes bodies whose centers are outside six hundred units");
    auto wave=combat();target(wave,3,{4915,4000});target(wave,4,{4931,4100});forceAttack(wave,BossAttack::StompWave);
    pinned(wave,.98);require(close(wave.findFighter(3)->hp,10000),"stomp has not reached the outer target before its actual edge");
    pinned(wave,.02);require(close(wave.findFighter(3)->hp,9955)&&close(wave.findFighter(4)->hp,10000),"nine hundred unit wave reaches overlapping shapes but not outside shapes");
}
static void missileExplosion() {
    auto m=combat();target(m,1,{5000,4000});target(m,2,{4872.4,4090},Shape::Rectangle,2);target(m,3,{4952.4,4156},Shape::Rectangle,0);
    require(m.addHost(4,1),"explosion host joins");m.findFighter(4)->armor=0;m.findFighter(4)->shotRemaining=100;place(m,4,{4952.4,3810});
    // These exact edge expectations use stationary, axis-aligned fixtures.
    for(auto& body:m.world.bodies)body.spin=0;
    forceAttack(m,BossAttack::StompRest);m.boss.attackInitialized=true;m.boss.attackElapsed=-100;
    m.boss.projectiles[0]={{4930,4000},{1000,0},BossBulletRadius,0,100,true};
    m.step(.03);
    require(close(m.findFighter(1)->hp,9900)&&close(m.findFighter(2)->hp,9900),"missile explodes once and damages every overlapping live team");
    require(close(m.findFighter(3)->hp,10000),"explosion checks rectangle edges rather than circumcircle bounds");
    require(close(m.findFighter(4)->hp,2900),"explosion includes the enlarged host body at its actual edge");
    require(m.world.find(1)->position.x>5050&&m.world.find(2)->position.x<4835&&m.world.find(2)->position.y>4130&&m.world.find(4)->position.y<3760,"explosion knocks each hit body away from the impact point");
    const Vec p=m.world.find(2)->position;std::vector<int> found;m.world.query(p,1,found);
    require(std::find(found.begin(),found.end(),1)!=found.end(),"explosion knockback updates the player spatial index");
    pinned(m,.2);require(close(m.findFighter(1)->hp,9900)&&close(m.findFighter(2)->hp,9900),"consumed missile cannot repeat explosion damage");
    const auto& explosion=m.boss.explosions[0];
    require(explosion.active&&close(explosion.position.x,4952.4)&&close(explosion.position.y,4000)&&close(explosion.radius,140),"explosion visual remains centered at the first swept hit with the damage radius");
    require(m.damageBoss(1,100000)&&explosion.active,"boss death preserves an existing short impact visual");
    m.config.autoCombat=false;m.step(.45);
    require(!explosion.active&&close(explosion.age,.45),"impact visuals expire even after boss death and disabled combat");
    m.reset();require(m.boss.explosions.size()==16&&m.boss.explosionCursor==0&&!m.boss.explosions[0].active,"round reset clears bounded explosion storage");
}
static void missileEnvironmentDeath() {
    auto m=combat();target(m,1,{5000,4000});target(m,2,{4952.4,4090},Shape::Circle,2);target(m,3,{4900,3910},Shape::Rectangle,0);
    m.findFighter(1)->hp=80;m.findFighter(1)->score=37;m.findFighter(2)->hp=100;
    m.findFighter(3)->alive=false;m.world.find(3)->active=false;m.findFighter(3)->respawnRemaining=100;m.world.rebuildSpatial();
    forceAttack(m,BossAttack::StompRest);m.boss.attackInitialized=true;m.boss.attackElapsed=-100;
    m.boss.projectiles[0]={{4900,4000},{1000,0},BossBulletRadius,0,100,true};pinned(m,.06);
    require(!m.findFighter(1)->alive&&m.findFighter(1)->deaths==1&&m.findFighter(1)->score==30,"missile lethality retains the unaffected eighty percent of carried score");
    require(close(m.findFighter(2)->hp,50)&&m.findFighter(2)->kills==0&&m.findFighter(2)->score==0,"splash keeps circle defence and grants no nearby player kill reward");
    require(close(m.findFighter(3)->hp,10000),"missile splash excludes dead fighters");
    int64_t drops=0;for(const auto& orb:m.orbs)if(orb.active&&!orb.natural)drops+=orb.value;
    require(drops==7,"missile environment death drops only its floored twenty percent score loss");
}
static void laserGrowth() {
    require(close(BossLaserWarningWidth,17.6),"warning width is forty percent of an ordinary player's diameter");
    require(close(BossLaserWidthAt(-1),17.6)&&close(BossLaserWidthAt(0),17.6),"active beam begins with the narrow warning width");
    require(close(BossLaserWidthAt(.1625),23.1)&&close(BossLaserWidthAt(.325),35.2)&&close(BossLaserWidthAt(.4875),47.3),"beam thickness follows smooth growth through the visible charge");
    require(close(BossLaserWidthAt(.65),52.8)&&close(BossLaserWidthAt(3),52.8),"beam reaches its maximum after sixty five hundredths of a second");
    auto m=combat();target(m,1,{5000,4041.4});target(m,2,{5300,4041.41});forceAttack(m,BossAttack::LaserActive);m.boss.laserDirection={1,0};
    for(auto& body:m.world.bodies)body.spin=0;
    pinned(m,.799);require(close(m.findFighter(1)->hp,10000),"first three narrow pulses do not reach a body at the final beam edge");
    pinned(m,.001);require(close(m.findFighter(1)->hp,9850)&&close(m.findFighter(2)->hp,10000),"fourth laser pulse uses maximum visual width for exact polygon collision");
    pinned(m,2.2);require(close(m.findFighter(1)->hp,8200)&&close(m.findFighter(2)->hp,10000),"only the twelve full-width pulses reach the maximum beam edge");
}
static void rewardRecipients() {
    auto m=quiet();m.step(150);m.add(1,Shape::Rectangle,1);m.add(2,Shape::Rectangle,1);m.add(3,Shape::Rectangle,2);m.addHost(4,1);
    m.damageEnvironment(2,10000);m.damageBoss(1,100000);
    require(m.hasBossBuff(*m.findFighter(1))&&m.hasBossBuff(*m.findFighter(4)),"boss kill grants living same-team viewers and host their reward");
    require(!m.hasBossBuff(*m.findFighter(2))&&!m.findFighter(2)->bossBuffEligible&&!m.hasBossBuff(*m.findFighter(3)),"dead teammates and opposing players are ineligible for the kill reward");
    m.add(5,Shape::Rectangle,1);m.step(15);
    require(m.findFighter(2)->alive&&!m.hasBossBuff(*m.findFighter(2))&&!m.hasBossBuff(*m.findFighter(5)),"reviving or joining after the boss kill cannot acquire its team reward");
}
static void boundedExplosionPool() {
    auto m=combat();target(m,1,{5000,4000});m.world.find(1)->spin=0;forceAttack(m,BossAttack::StompRest);m.boss.attackInitialized=true;m.boss.attackElapsed=-100;
    for(int i=0;i<18;++i){place(m,1,{5000,4000});m.boss.projectiles[0]={{4980,4000},{1000,0},BossBulletRadius,0,100,true};m.step(.001);}
    require(m.boss.explosions.size()==16&&m.boss.explosionCursor==2,"successive impacts recycle the sixteen-slot explosion ring");
    require(std::all_of(m.boss.explosions.begin(),m.boss.explosions.end(),[](const BossExplosion& e){return e.active;}),"each recent missile impact keeps a bounded visual");
    require(close(m.findFighter(1)->hp,8200),"eighteen missiles each apply exactly one explosion hit");
    m.config.autoCombat=false;m.step(.449);require(m.boss.explosions[1].active,"newest explosion remains visible just before its lifetime boundary");
    m.step(.001);require(std::none_of(m.boss.explosions.begin(),m.boss.explosions.end(),[](const BossExplosion& e){return e.active;}),"all explosion slots expire at their lifetime boundaries");
}
static void scorchLifetimeAndOverlap() {
    auto m=combat();target(m,1,{5000,4000});target(m,2,{5500,4500});target(m,3,{1000,1000},Shape::Rectangle,2);m.boss.hp=1600;
    m.bases[1].position={6000,4000};m.bases[2].position={7000,4000};
    forceAttack(m,BossAttack::LaserActive);m.boss.laserDirection={1,0};
    pinned(m,.099);require(close(m.findFighter(1)->hp,10000),"scorch waits until the first tenth second boundary");
    pinned(m,.001);require(close(m.findFighter(1)->hp,9990),"rage laser lays damaging scorch at firing onset");
    pinned(m,2.9);require(close(m.findFighter(1)->hp,5200),"three second rage beam includes fifteen laser and thirty scorch ticks");
    m.damageBoss(3,100000);require(!m.boss.active,"boss can die while the scorch remains");
    place(m,1,{5000,4500});place(m,2,{5500,4000});
    pinned(m,1.999);require(close(m.findFighter(2)->hp,9810),"late entrant receives only nineteen overlapping scorch ticks before expiry");
    pinned(m,.001);require(close(m.findFighter(2)->hp,9800),"scorch survives boss death and its final tick is exactly five seconds from laser start");
    pinned(m,.5);require(close(m.findFighter(2)->hp,9800)&&close(m.findFighter(1)->hp,5200),"expired scorch and absent overlap cause no extra damage");
    require(close(m.bases[1].hp,2500)&&close(m.bases[2].hp,5000),"boss beam and scorch never damage either base");
}
static void scorchMitigationAndNpc() {
    auto m=combat();target(m,1,{5000,4000},Shape::Circle);target(m,2,{5500,4000});target(m,3,{6000,4000});
    require(m.addHost(4,0),"gray host joins scorch fixture");place(m,4,{6500,4000});m.findFighter(4)->armor=0;m.findFighter(4)->shotRemaining=100;
    m.findFighter(1)->armor=100;m.findFighter(2)->armor=100;m.findFighter(3)->alive=false;m.world.find(3)->active=false;m.findFighter(3)->respawnRemaining=100;m.world.rebuildSpatial();
    m.boss.hp=1600;forceAttack(m,BossAttack::LaserActive);m.boss.laserDirection={1,0};pinned(m,.1);
    require(close(m.findFighter(1)->hp,9998.5)&&close(m.findFighter(1)->armor,97.025),"scorch applies circle reduction before shared armor");
    require(close(m.findFighter(2)->hp,9997)&&close(m.findFighter(2)->armor,94.05),"scorch applies the ordinary armor split");
    require(close(m.findFighter(3)->hp,10000)&&close(m.findFighter(4)->hp,2990),"scorch excludes dead fighters and includes gray host without circle defence");
    pinned(m,.1);require(close(m.findFighter(4)->hp,2680),"rage laser damages gray host without inheriting a spectator shape passive");
    MatchConfig c;c.autoCombat=false;c.autoCollect=false;c.naturalOrbs=0;c.npcCount=128;Match npc(c);npc.step(150);npc.config.autoCombat=true;npc.weapon.damage=0;
    const int index=0;auto& n=npc.npcs[index];n.hp=n.maxHp=10000;npc.boss.hp=1600;forceAttack(npc,BossAttack::LaserActive);npc.boss.laserDirection=boss_detail::direction(n.position-npc.boss.position);
    const Vec original=n.position;npc.step(.1);require(close(n.hp,10000),"rage scorch cannot damage a neutral NPC on the ray");
    npc.step(.1);require(close(n.hp,10000),"neutral NPC is also immune to the first laser damage tick");
    npc.add(9,Shape::Rectangle,1);npc.damageBoss(9,100000);n.hp=5;npc.step(.1);
    require(n.active&&close(n.hp,5)&&close(n.respawnRemaining,0)&&(n.position-original).length()<1e-8&&npc.findFighter(9)->score==0,"post-boss scorch leaves even a low health neutral NPC alive and stationary");
}
static void scorchPresentationAndPool() {
    require(close(BossScorchWidthAt(0),17.6)&&close(BossScorchWidthAt(.1625),23.1)&&close(BossScorchWidthAt(.325),35.2)&&close(BossScorchWidthAt(.4875),47.3)&&close(BossScorchWidthAt(.65),52.8),"scorch width uses the same gradual smooth growth as the beam");
    require(close(BossScorchOpacityAt(0),1)&&close(BossScorchOpacityAt(4),1)&&close(BossScorchOpacityAt(4.25),.84375)&&close(BossScorchOpacityAt(4.5),.5)&&close(BossScorchOpacityAt(4.75),.15625)&&close(BossScorchOpacityAt(5),0),"scorch remains opaque for four seconds then smoothly fades through expiry");
    auto ordinary=combat();target(ordinary,1,{5000,4000});forceAttack(ordinary,BossAttack::LaserActive);ordinary.step(.1);
    require(std::none_of(ordinary.boss.scorches.begin(),ordinary.boss.scorches.end(),[](const BossScorch& scorch){return scorch.active;}),"ordinary laser does not leave rage scorch");
    auto m=combat();target(m,1,{5000,4000});m.boss.hp=1600;forceAttack(m,BossAttack::LaserWindup);m.boss.targetId=1;
    pinned(m,3);const auto& scorch=m.boss.scorches[0];
    require(scorch.active&&close(scorch.age,0)&&scorch.ticks==0&&close(scorch.lifetime,5),"scorch starts at the exact windup endpoint without aging before birth");
    const Vec from=scorch.from,to=scorch.to,direction=scorch.direction;place(m,1,{5000,5000});pinned(m,.4);
    require(close(scorch.from.x,from.x)&&close(scorch.from.y,from.y)&&close(scorch.to.x,to.x)&&close(scorch.to.y,to.y)&&close(scorch.direction.x,direction.x)&&close(scorch.direction.y,direction.y),"scorch retains the locked ray after its target moves");
    for(int i=0;i<8;++i){forceAttack(m,BossAttack::LaserActive);m.step(.001);}
    require(m.boss.scorches.size()==8&&m.boss.scorchCursor==1&&std::all_of(m.boss.scorches.begin(),m.boss.scorches.end(),[](const BossScorch& s){return s.active;}),"repeated rage laser starts recycle a bounded eight-slot scorch pool");
    m.damageBoss(1,100000);m.config.autoCombat=false;m.step(5);
    require(std::none_of(m.boss.scorches.begin(),m.boss.scorches.end(),[](const BossScorch& s){return s.active;}),"scorch visuals expire after boss death even if combat is disabled");
    m.reset();require(m.boss.scorchCursor==0&&std::all_of(m.boss.scorches.begin(),m.boss.scorches.end(),[](const BossScorch& s){return !s.active&&s.ticks==0&&s.age==0;}),"round reset clears all scorch state");
}
static void grayRewardRecipients() {
    auto m=quiet();m.step(150);m.add(1,Shape::Rectangle,0);m.add(2,Shape::Rectangle,0);m.add(3,Shape::Rectangle,1);m.addHost(4,0);m.add(5,Shape::Rectangle,0);
    m.damageEnvironment(5,10000);m.damageBoss(1,100000);
    require(m.hasBossBuff(*m.findFighter(1))&&m.hasBossBuff(*m.findFighter(2))&&m.hasBossBuff(*m.findFighter(4)),"gray boss reward includes every living gray teammate and gray host");
    require(!m.hasBossBuff(*m.findFighter(3))&&!m.hasBossBuff(*m.findFighter(5)),"gray boss reward excludes other factions and dead gray teammates");
    m.damagePlayer(2,3,20);require(close(m.findFighter(3)->hp,176),"gray reward grants outgoing damage bonus");
    m.damageEnvironment(2,10000);m.revive(2);require(!m.hasBossBuff(*m.findFighter(2)),"gray reward is lost immediately on death and remains lost after revival");
    m.step(60);require(!m.hasBossBuff(*m.findFighter(1))&&close(m.teamBuffRemaining[0],0),"gray reward expires exactly at sixty seconds");
    require(close(m.bases[1].hp,2500)&&close(m.bases[2].hp,2500),"gray reward never doubles a colored faction base");
}
static void rageDamageReduction() {
    auto m=quiet();m.step(BossSpawnSeconds);m.add(1,Shape::Rectangle,1);m.add(2,Shape::Square,1);m.boss.hp=m.boss.maxHp=16000;
    m.damageBoss(1,9400);require(close(m.boss.hp,6600)&&!m.boss.rage,"boss remains ordinary above forty percent health");
    m.damageBoss(1,400);require(close(m.boss.hp,6200)&&m.boss.rage,"threshold-crossing hit takes full damage before rage begins");
    m.damageBoss(1,200);require(close(m.boss.hp,6090),"an already-raging boss receives fifty five percent of later damage");
    m.damageBoss(2,20);require(close(m.boss.hp,6062.5),"rage reduction composes with square neutral damage bonus");
    m.boss.armor=100;m.damageBoss(1,200);
    require(close(m.boss.hp,6029.5)&&close(m.boss.armor,34.55),"rage reduction precedes the common armor split");
    auto exact=quiet();exact.step(BossSpawnSeconds);exact.add(3,Shape::Rectangle,1);exact.boss.hp=6401;exact.boss.maxHp=16000;
    exact.damageBoss(3,1);require(close(exact.boss.hp,6400)&&exact.boss.rage,"exact forty percent endpoint enters rage without reducing that hit");
    exact.damageBoss(3,100);require(close(exact.boss.hp,6345),"very next hit after threshold receives rage reduction");
}
static void rageCardinalMissiles() {
    auto m=combat();target(m,1,{4700,4700});m.boss.hp=1600;forceAttack(m,BossAttack::Bullet);pinned(m,2.5);
    require(m.boss.projectilesFired==5&&m.boss.attacksCompleted==1,"rage attack creates aimed missile plus four cardinal missiles in one firing event");
    const Vec expected[]={{.7071067811865475,.7071067811865475},{1,0},{0,1},{-1,0},{0,-1}};
    for(size_t i=0;i<5;++i){const auto& p=m.boss.projectiles[i];
        require(p.active&&close(p.position.x,4000)&&close(p.position.y,4000)&&close(p.travelled,0)&&close(p.damage,200),"every rage missile is born at the boss with double damage and no pre-birth travel");
        require(close(p.velocity.x,expected[i].x*1000)&&close(p.velocity.y,expected[i].y*1000),"rage extras use fixed world cardinal directions rather than target-relative spread");}
    require(close(m.bases[1].hp,2500)&&close(m.bases[2].hp,2500),"rage burst leaves bases immune");
    auto pool=combat();target(pool,2,{4700,4700});pool.boss.hp=1600;
    for(int i=0;i<7;++i){forceAttack(pool,BossAttack::Bullet);pool.boss.attackElapsed=2.499;pinned(pool,.001);}
    require(pool.boss.projectiles.size()==32&&pool.boss.projectilesFired==35&&pool.boss.projectileCursor==3,"repeated rage volleys recycle a bounded thirty two missile pool");
    require(std::all_of(pool.boss.projectiles.begin(),pool.boss.projectiles.end(),[](const BossProjectile& p){return p.active;}),"bounded pool retains thirty two recent moving rage missiles");
    pool.damageBoss(2,100000);require(std::none_of(pool.boss.projectiles.begin(),pool.boss.projectiles.end(),[](const BossProjectile& p){return p.active;}),"boss death clears cardinal missiles along with aimed missiles");
}
static void spawnWaveAndAttraction() {
    auto m=quiet();m.step(149.999);require(close(m.boss.spawnAge,0),"unspawned boss has no birth-wave age");
    m.step(.001);require(close(m.boss.spawnAge,0),"birth wave starts exactly at 150 seconds without pre-birth aging");
    m.step(.5);require(close(m.boss.spawnAge,.5),"birth wave advances independently of enabled combat");
    require(close(BossSpawnWaveRadiusAt(0),0)&&close(BossSpawnWaveOpacityAt(0),1),"birth wave begins at the boss fully visible");
    require(close(BossSpawnWaveRadiusAt(1),2828.42712474619)&&close(BossSpawnWaveOpacityAt(1),.5),"birth wave radius and opacity use smooth halfway progress");
    require(close(BossSpawnWaveRadiusAt(2),5656.85424949238)&&close(BossSpawnWaveOpacityAt(2),0),"birth wave reaches all four map corners and fades out at two seconds");
    const Vec starts[]={{7000,4000},{1000,4000},{4000,1000},{4000,7000}};
    for(int i=0;i<3;++i){require(m.add(i+1,static_cast<Shape>(i),i),"each faction joins attraction fixture");place(m,i+1,starts[i]);m.world.find(i+1)->velocity={165,0};}
    require(m.addHost(4,0),"host joins attraction fixture");place(m,4,starts[3]);m.world.find(4)->velocity={82.5,0};
    require(m.add(5,Shape::Rectangle,1),"dead fixture joins");m.findFighter(5)->alive=false;m.findFighter(5)->respawnRemaining=100;m.world.find(5)->active=false;
    const Vec deadPosition=m.world.find(5)->position;m.world.rebuildSpatial();m.step(1);
    for(int i=0;i<4;++i){const auto* body=m.world.find(i+1);const Vec toward=m.boss.position-body->position;
        require(toward.dot(body->velocity)>0&&(body->position-m.boss.position).length()<3000,"boss gravity turns every living faction and host inward without teleporting");}
    require(close(m.world.find(5)->position.x,deadPosition.x)&&close(m.world.find(5)->position.y,deadPosition.y),"boss gravity ignores inactive dead geometry");
    require(m.world.find(4)->velocity.length()<m.world.find(2)->velocity.length(),"attraction preserves the host movement-speed distinction");
    m.damageBoss(2,100000);place(m,1,{7000,4000});m.world.find(1)->velocity={0,165};m.step(.1);
    require(close(m.world.find(1)->velocity.x,0)&&m.world.find(1)->position.y>4000,"boss death immediately stops steering surviving bodies");
    require(close(m.boss.spawnAge,1.6),"existing birth wave keeps aging after an early boss death");
    m.reset();require(close(m.boss.spawnAge,0)&&!m.boss.spawned,"round reset clears the birth wave");
}
static void rewardAlsoGrantsEvolution() {
    auto m=quiet();m.step(BossSpawnSeconds);m.add(1,Shape::Rectangle,0);m.add(2,Shape::Circle,0);m.add(3,Shape::Rectangle,1);m.addHost(4,0);m.add(5,Shape::Rectangle,0);
    m.findFighter(4)->hp=500;m.damageEnvironment(5,100000);m.damageBoss(1,100000);
    for(int id:{1,2,4})require(m.hasBossBuff(*m.findFighter(id))&&close(m.findFighter(id)->evolutionRemaining,40),"living last-hit team receives boss damage reward and evolution together including gray host");
    require(close(m.findFighter(1)->hp,400)&&close(m.findFighter(2)->hp,600)&&close(m.findFighter(4)->hp,1000)&&close(m.findFighter(4)->maxHp,6000),"boss-granted evolution doubles current and maximum life including host");
    require(close(m.findFighter(3)->evolutionRemaining,0)&&close(m.findFighter(5)->evolutionRemaining,0),"other factions and dead teammates receive neither evolution reward");
    m.add(6,Shape::Rectangle,0);require(close(m.findFighter(6)->evolutionRemaining,0),"joining after a boss kill cannot acquire its evolution reward");
    m.damageEnvironment(2,100000);require(!m.hasBossBuff(*m.findFighter(2))&&close(m.findFighter(2)->evolutionRemaining,0),"death immediately removes both boss-granted buffs");
    m.step(40);require(close(m.findFighter(1)->evolutionRemaining,0)&&close(m.findFighter(1)->maxHp,200)&&close(m.findFighter(4)->maxHp,3000)&&close(m.findFighter(4)->hp,500),"evolution reward expires at forty seconds and restores ordinary host life limits");
    require(m.hasBossBuff(*m.findFighter(1))&&m.hasBossBuff(*m.findFighter(4)),"sixty second boss reward outlasts its forty second evolution");
    for(int team:{1,2}){auto colored=quiet();colored.step(BossSpawnSeconds);colored.add(10,Shape::Rectangle,team);colored.add(11,Shape::Square,team);colored.add(12,Shape::Rectangle,3-team);colored.damageBoss(10,100000);
        require(colored.hasBossBuff(*colored.findFighter(10))&&colored.hasBossBuff(*colored.findFighter(11))&&close(colored.findFighter(10)->evolutionRemaining,40)&&close(colored.findFighter(11)->evolutionRemaining,40)&&close(colored.findFighter(12)->evolutionRemaining,0),"red and blue last hits also grant both rewards only to living teammates");}
}
int main(){int failures=0;const auto run=[&](const char* name,void(*test)()){try{test();}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    run("spawn",spawnBoundary);run("bullet",bulletTiming);run("laser",laserTiming);run("stomp",stompTiming);run("rage",rageAndFire);run("environment",environmentAndReward);run("npc",npcWaveAndSafety);run("geometry",collisionEdges);run("transitions",exposureAndRageTransitions);run("reward",rewardIntegration);run("grid/reset",npcGridAndReset);run("stationary/single direction",stationaryEveryStage);run("close behind laser",closeBehindLaser);
    run("expanded hazards",expandedHazards);run("missile explosion",missileExplosion);run("missile environment",missileEnvironmentDeath);run("laser growth",laserGrowth);run("reward recipients",rewardRecipients);run("bounded explosions",boundedExplosionPool);
    run("scorch lifetime",scorchLifetimeAndOverlap);run("scorch mitigation NPC",scorchMitigationAndNpc);run("gray reward recipients",grayRewardRecipients);
    run("scorch presentation pool",scorchPresentationAndPool);
    run("rage received damage",rageDamageReduction);run("rage cardinal missiles",rageCardinalMissiles);
    run("spawn wave attraction",spawnWaveAndAttraction);run("boss evolution reward",rewardAlsoGrantsEvolution);
    std::cout<<"Boss tests: "<<assertions<<" assertions, "<<failures<<" failures\n";return failures?1:0;}
