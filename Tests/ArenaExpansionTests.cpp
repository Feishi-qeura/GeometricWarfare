#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace gw;
static int assertions=0;
static void check(bool ok,const char* message){++assertions;if(!ok)throw std::runtime_error(message);}
static bool eq(double a,double b){return std::abs(a-b)<1e-5;}
static Match quiet(){MatchConfig c;c.autoCombat=false;c.autoCollect=false;return Match(c);}
static void pinned(Match& m,double seconds){
    std::vector<Vec> positions;for(const auto& body:m.world.bodies)positions.push_back(body.position);
    while(seconds>1e-8){const double dt=std::min(.01,seconds);for(size_t i=0;i<positions.size();++i){m.world.bodies[i].position=positions[i];m.world.bodies[i].velocity={};}m.world.rebuildSpatial();m.step(dt);seconds-=dt;}
    for(size_t i=0;i<positions.size();++i){m.world.bodies[i].position=positions[i];m.world.bodies[i].velocity={};}m.world.rebuildSpatial();
}
static void armorChecks(){
    double hp=200,armor=100;
    check(eq(Match::applyArmorDamage(100,hp,armor),30)&&eq(hp,170)&&eq(armor,40.5),"armor absorbs70percent and spends85percent of absorbed damage");
    hp=200;armor=29.75;
    check(eq(Match::applyArmorDamage(100,hp,armor),65)&&eq(hp,135)&&eq(armor,0),"breaking armor redirects its uncovered damage to hp");
    check(eq(Match::applyArmorDamage(100,hp,armor),100)&&eq(hp,35)&&eq(armor,0),"depleted armor leaves all damage on hp");
    hp=200;armor=1;
    check(eq(Match::applyArmorDamage(.5,hp,armor),.15)&&eq(hp,199.85)&&eq(armor,.7025),"small fractional hits consume armor without free absorption");
    Match m=quiet();m.addHost(1,1);m.add(2,Shape::Rectangle,2);m.add(3,Shape::Circle,2);
    m.evolutionPacks.push_back({{0,0},true,0});m.collectEvolutionPack(2,0);
    m.damageEnvironment(1,100);check(eq(m.fighters[0].hp,2970)&&eq(m.fighters[0].armor,440.5),"host uses the same armor split without circle defence");
    m.damagePlayer(1,2,100);check(eq(m.fighters[1].hp,370)&&eq(m.fighters[1].armor,40.5),"evolved viewers use the same armor split for player damage");
    m.fighters[2].armor=100;m.damageEnvironment(3,100);
    check(eq(m.fighters[2].hp,285)&&eq(m.fighters[2].armor,70.25),"circle defence applies before the common armor split");
}
static void healingFeedbackChecks(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);m.fighters[0].hp=192.75;
    m.world.bodies[0].position={321,654};
    check(m.healLike(1,2)&&eq(m.fighters[0].hp,200),"likes restore only the missing health");
    check(m.damageEvents.size()==1,"a successful like heal emits floating feedback");
    const auto& event=m.damageEvents.back();
    check(eq(event.amount,7.25)&&event.kind==NumberKind::Healing&&event.team==1&&event.targetKind==1&&event.targetId==1&&eq(event.position.x,321)&&eq(event.position.y,654),"healing feedback keeps actual capped fractional restoration and recipient");
    m.healLike(1,1);check(m.damageEvents.size()==1,"full health produces no zero healing number");
    m.damageEvents.clear();
    for(int i=0;i<600;++i){m.fighters[0].hp=199.5;m.healLike(1,1);}
    check(m.damageEvents.size()==512,"healing feedback shares the bounded event queue");
}
static void buffRequiresEligibility(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);m.add(2,Shape::Rectangle,2);m.teamBuffRemaining[1]=60;
    m.damagePlayer(1,2,20);check(eq(m.fighters[1].hp,180),"team timer alone does not grant a newly joined fighter bonus damage");
    for(int i=0;i<5;++i)m.collectOrb(1,i);
    check(m.fighters[0].score==10,"team timer alone does not grant bonus resource score");
}
static void buffEndsOnDeath(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);m.add(2,Shape::Rectangle,2);m.step(BossSpawnSeconds);
    m.damageBoss(1,100000);m.damagePlayer(1,2,20);
    check(eq(m.fighters[1].hp,176),"living boss reward recipient deals twenty percent extra damage");
    m.damageEnvironment(1,100000);check(!m.fighters[0].alive&&m.teamBuffRemaining[1]>0,"death leaves the shared team reward timer active");
    m.revive(1);m.fighters[1].hp=200;m.damagePlayer(1,2,20);
    check(eq(m.fighters[1].hp,180),"revival does not restore the lost per-life boss damage buff");
    for(int i=0;i<5;++i)m.collectOrb(1,i);
    check(m.fighters[0].score==10,"revival does not restore boss bonus resource score");
}
static void buffExpiresExactly(){
    Match m=quiet();m.add(1,Shape::Rectangle,1);m.add(2,Shape::Rectangle,2);m.step(BossSpawnSeconds);
    m.damageBoss(1,100000);check(m.hasBossBuff(m.fighters[0]),"living last-hit recipient starts with the boss buff");
    m.step(60);
    check(!m.hasBossBuff(m.fighters[0])&&m.teamBuffRemaining[1]==0,"boss buff is inactive exactly sixty seconds later despite fractional simulation steps");
    m.damagePlayer(1,2,20);check(eq(m.fighters[1].hp,180),"damage immediately at the sixty second endpoint has no boss bonus");
}
static void swordAnglesRangeAndCadence(){
    MatchConfig c;c.naturalOrbs=0;c.npcCount=0;c.autoCollect=false;Match m(c);m.weapon.range=0;
    m.add(1,Shape::Rectangle,1);m.add(2,Shape::Rectangle,2);m.add(3,Shape::Rectangle,2);
    m.evolutionPacks.push_back({{0,0},true,0});m.collectEvolutionPack(1,0);
    m.world.bodies[0].position={500,700};m.world.bodies[1].position={1500,700};m.world.bodies[2].position={1500,1300};
    for(auto& b:m.world.bodies){b.velocity={};b.spin=0;}m.world.rebuildSpatial();m.fighters[0].aimAngle=0;
    pinned(m,4.999);check(m.swordWaves.empty(),"evolution waits the complete five second pulse interval");pinned(m,.001);
    check(m.swordWaves.size()==12,"five second endpoint emits both six-wave groups simultaneously");
    const Vec directions[]={{1,0},{.5,.8660254037844386},{-.5,.8660254037844386},{-1,0},{-.5,-.8660254037844386},{.5,-.8660254037844386},{.8660254037844386,.5},{0,1},{-.8660254037844386,.5},{-.8660254037844386,-.5},{0,-1},{.8660254037844386,-.5}};
    for(size_t i=0;i<12;++i)check(eq(m.swordWaves[i].direction.x,directions[i].x)&&eq(m.swordWaves[i].direction.y,directions[i].y)&&eq(m.swordWaves[i].traveled,0),"second sword group is offset thirty degrees and born at the same instant");
    pinned(m,2.499);check(m.swordWaves.size()==12&&eq(m.swordWaves[0].traveled,1099.56)&&eq(m.fighters[1].hp,140),"sword travels at 440 speed and hits a target one thousand units away");
    pinned(m,.001);check(m.swordWaves.empty()&&eq(m.fighters[1].hp,140)&&eq(m.fighters[2].hp,200),"sword expires exactly at 1100 distance without extra hits");
    Match cadence(c);cadence.weapon.range=0;cadence.add(10,Shape::Rectangle,1);cadence.evolutionPacks.push_back({{0,0},true,0});cadence.collectEvolutionPack(10,0);
    int emitted=0;
    for(int second=5;second<=40;second+=5){cadence.step(5);if(second<40){check(cadence.swordWaves.size()==12&&eq(cadence.swordWaves[0].traveled,0),"each five second endpoint before expiry creates twelve new waves");emitted+=static_cast<int>(cadence.swordWaves.size());}else check(cadence.swordWaves.empty()&&eq(cadence.fighters[0].evolutionRemaining,0),"forty second expiry wins the tied eighth pulse");}
    check(emitted==84,"one evolution emits seven twelve-wave pulses before expiry");
}
static void swordBoundedPoolAndHostSeat(){
    MatchConfig c;c.naturalOrbs=0;c.npcCount=0;c.autoCollect=false;Match pool(c);pool.weapon.range=0;
    for(int i=0;i<24;++i){pool.add(i,Shape::Rectangle,1);pool.fighters.back().evolutionRemaining=40;pool.fighters.back().swordRemaining=.01;}
    pool.step(.01);check(pool.swordWaves.size()==256,"simultaneous evolution pulses stay within the 256-wave bound");
    pool.step(2.5);check(pool.swordWaves.empty(),"all retained waves expire without growing storage");
    Match seats(c);seats.weapon.range=0;
    for(int i=0;i<500;++i){check(seats.add(i,Shape::Rectangle,i<100?0:i<300?1:2),"maximum viewer fixture joins");seats.world.bodies.back().position={100,100};}
    check(seats.addHost(6000,2),"host occupies the final physical seat");
    for(size_t i=0;i<seats.fighters.size();++i){seats.fighters[i].shotRemaining=100;seats.world.bodies[i].velocity={};}
    seats.world.find(100)->position={World::Size*.5,World::Size*.5};seats.world.find(6000)->position={World::Size*.5+100,World::Size*.5};seats.findFighter(6000)->armor=0;
    seats.world.rebuildSpatial();SwordWave wave;wave.ownerId=100;wave.team=1;wave.from=wave.position={World::Size*.5,World::Size*.5};wave.direction={1,0};seats.swordWaves.push_back(wave);
    seats.step(.25);check(eq(seats.findFighter(6000)->hp,2940)&&seats.swordWaves[0].hitPlayers.test(500),"piercing sword safely damages host in index 500 and records its one-hit bit");
}
static void gunAndSwordChecks(){
    Body envelope,ordinary;envelope.scale=2.5;envelope.position={100,100};ordinary.position={160,100};Vec normal;double depth=0;
    check(overlap(envelope,ordinary,normal,depth)&&eq(depth,17),"host circular envelope uses55radius in actual collision separation");
    MatchConfig c;c.naturalOrbs=0;c.npcCount=0;c.autoCollect=false;Match gun(c);gun.weapon.damage=0;
    gun.add(1,Shape::Rectangle,1);gun.add(2,Shape::Rectangle,2);gun.grantShotgun(1);
    gun.world.bodies[0].position={1000,1000};gun.world.bodies[1].position={1100,1000};gun.world.bodies[0].velocity=gun.world.bodies[1].velocity={165,0};gun.world.rebuildSpatial();
    gun.fighters[1].hp=gun.fighters[1].maxHp=50000;int smallest=99,largest=0,totalMisses=0;double leastDamage=99,mostDamage=0,widestAngle=0;
    for(int i=0;i<50;++i){auto& f=gun.fighters[0];f.ammo=5;f.shotRemaining=f.reloadRemaining=f.aimRemaining=0;f.acquisitionRemaining=999;f.targetKind=1;f.targetIndex=1;gun.shots.clear();gun.damageEvents.clear();const double before=gun.fighters[1].hp;gun.step(.001);
        int pellets=0,hits=0;for(const auto& shot:gun.shots)if(shot.kind==WeaponKind::Shotgun){++pellets;if(shot.hit)++hits;else ++totalMisses;check(shot.lifetime>=.12&&shot.life>0,"both hit and miss shotgun traces retain their animation lifetime");const Vec direction=shot.to-shot.from;const double spread=std::abs(std::remainder(std::atan2(direction.y,direction.x)-f.aimAngle,detail::Tau));widestAngle=std::max(widestAngle,spread);check(spread<=detail::Tau/18+1e-8,"rectangle passive narrows the new sixty degree shotgun fan to forty degrees");}
        double applied=0;for(const auto& damage:gun.damageEvents){check(damage.targetKind==1&&damage.targetId==2&&damage.amount>=3&&damage.amount<=5&&eq(damage.amount,std::floor(damage.amount)),"shotgun pellets apply independent integer damage from three through five");applied+=damage.amount;leastDamage=std::min(leastDamage,damage.amount);mostDamage=std::max(mostDamage,damage.amount);}
        check(pellets>=15&&pellets<=25&&gun.damageEvents.size()==static_cast<size_t>(hits)&&eq(before-gun.fighters[1].hp,applied),"shotgun fires fifteen through twenty five pellets and only intersecting rays apply matching hp feedback");smallest=std::min(smallest,pellets);largest=std::max(largest,pellets);
    }check(smallest==15&&largest==25&&eq(leastDamage,3)&&eq(mostDamage,5)&&totalMisses>0&&widestAngle>.28,"seeded shotgun reaches both new count and damage limits and visibly spreads misses beyond the target");
    gun.fighters[0].ammo=1;gun.fighters[0].reloadRemaining=1.7;gun.fighters[0].shotRemaining=1.2;gun.grantShotgun(1);
    check(gun.fighters[0].ammo==1&&eq(gun.fighters[0].reloadRemaining,1.7)&&eq(gun.fighters[0].shotRemaining,1.2),"repeat shares cannot refill or bypass timers");
    Match sword(c);sword.weapon.range=0;sword.add(10,Shape::Rectangle,1);sword.add(20,Shape::Rectangle,2);
    sword.evolutionPacks.push_back({{1000,1000},true,0});sword.collectEvolutionPack(10,0);sword.step(4.99);check(sword.swordWaves.empty(),"no sword wave before5seconds");
    sword.world.bodies[0].position={1000,1000};sword.world.bodies[1].position={1100,1000};sword.world.bodies[0].velocity=sword.world.bodies[1].velocity={0,0};sword.world.rebuildSpatial();sword.fighters[0].aimAngle=0;
    sword.step(.01);check(sword.swordWaves.size()==12,"first five second pulse emits twelve waves together");check(eq(sword.swordWaves[0].traveled,0)&&eq(sword.swordWaves[0].life,2.5),"sword starts at its origin with full lifetime exactly5seconds");
    sword.step(.25);check(eq(sword.fighters[1].hp,140),"traveling sword hits target once with60base damage");sword.step(.24);
    check(sword.swordWaves.size()==12&&eq(sword.swordWaves[0].traveled,215.6),"all twelve waves remain moving at5.49seconds");
    sword.step(2.01);check(eq(sword.fighters[1].hp,140)&&sword.swordWaves.empty(),"piercing wave never re-hits same target and completes1100range at7.50seconds");
    sword.config.autoCombat=false;sword.step(32.49);sword.config.autoCombat=true;sword.step(.01);check(eq(sword.fighters[0].evolutionRemaining,0)&&sword.swordWaves.empty(),"expiry at40 wins the final sword pulse while attacks enabled");
    Match score=quiet();score.add(1,Shape::Rectangle,1);score.teamBuffRemaining[1]=60;score.fighters[0].bossBuffEligible=true;for(int i=0;i<5;++i)score.collectOrb(1,i);check(score.fighters[0].score==12,"five2point balls gain twelve points with fractional20percent buff");
    Match contact=quiet();contact.add(1,Shape::Triangle,1);contact.add(2,Shape::Rectangle,2);contact.teamBuffRemaining[1]=60;contact.fighters[0].bossBuffEligible=true;contact.damagePlayer(1,2,15,true);check(eq(contact.fighters[1].hp,182),"team damage buff also applies to flat triangle contact damage");
    score.addHost(2,1);score.add(3,Shape::Rectangle,2);score.fighters[2].score=73;score.damagePlayer(2,3,10000);check(score.fighters[1].score==0&&score.fighters[2].score==59&&score.orbs.back().value==14,"host kill drops only twenty percent of victim points and retains the remainder");
    auto& host=score.fighters[1];host.hp=500;host.armor=0;score.setHostTeam(2,2);score.setHostTeam(2,2);check(eq(host.hp,500)&&eq(host.armor,0),"host team controls never refill health or armor");score.damageEnvironment(2,100);check(eq(host.hp,400),"host circular collision carries no circle damage reduction");
}
int main(){int failures=0;
    const std::pair<const char*,void(*)()> checks[]={{"armor",armorChecks},{"healing feedback",healingFeedbackChecks},{"buff eligibility",buffRequiresEligibility},{"buff lifetime",buffEndsOnDeath},{"buff exact expiry",buffExpiresExactly},{"sword cadence range angles",swordAnglesRangeAndCadence},{"sword pool and host seat",swordBoundedPoolAndHostSeat}};
    for(const auto& test:checks)try{test.second();std::cout<<"PASS "<<test.first<<'\n';}catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<test.first<<": "<<error.what()<<'\n';}
    try{
    gunAndSwordChecks();
    Match m=quiet();check(m.add(1,Shape::Rectangle,1)&&m.add(2,Shape::Circle,2),"viewers join");
    m.fighters[0].hp=50;check(m.healLike(1,3)&&eq(m.fighters[0].hp,80),"each like heals five percent maximum hp");
    check(m.healLike(1,10000)&&eq(m.fighters[0].hp,200),"batched likes clamp hp");
    check(!m.healLike(999,1)&&!m.healLike(1,0),"likes never create identities and reject zero");
    m.fighters[0].armor=m.fighters[0].maxArmor=100;check(m.damageEnvironment(1,100),"environment damage applies");
    check(eq(m.fighters[0].hp,170)&&eq(m.fighters[0].armor,40.5),"100 raw damage removes30hp and59.5armor");
    m.fighters[0].armor=29.75;m.fighters[0].hp=200;m.damageEnvironment(1,100);
    check(eq(m.fighters[0].hp,135)&&eq(m.fighters[0].armor,0),"insufficient armor absorbs proportional seventy percent budget and hp takes overflow");
    m.fighters[1].armor=100;m.damageEnvironment(2,100);check(eq(m.fighters[1].hp,285)&&eq(m.fighters[1].armor,70.25),"circle defence precedes unified armor");
    check(m.grantShotgun(1)&&m.fighters[0].weaponKind==WeaponKind::Shotgun,"share grants shotgun");
    const auto shotgun=m.weaponFor(m.fighters[0]);check(shotgun.magazine==5&&eq(shotgun.reloadTime,3)&&eq(shotgun.fireInterval,2),"shotgun has five triggers and three second reload");
    m.damageEnvironment(1,10000);check(!m.healLike(1,100),"likes cannot revive");m.revive(1);check(m.fighters[0].weaponKind==WeaponKind::Shotgun&&m.fighters[0].ammo==5,"shotgun persists through revival with its full new magazine");
    check(m.addHost(3,1),"host joins extra seat");const auto* h=m.findFighter(3);
    check(h&&h->isHost&&eq(h->maxHp,3000)&&eq(h->armor,500)&&h->weaponKind==WeaponKind::Rifle,"host has3000hp500armor and rifle");
    check(eq(m.world.find(3)->scale,2.5)&&eq(m.world.find(3)->speedScale,.75),"host movement composes half speed with rifle one point five multiplier");
    check(m.teamCounts[1]==1&&!m.grantShotgun(3)&&!m.changeShape(3,Shape::Triangle),"host excludes viewer capacity and cannot change assigned form or weapon");
    const auto rifle=m.weaponFor(*h);check(rifle.magazine==45&&eq(rifle.damage,8)&&eq(rifle.range,1800)&&eq(rifle.fireInterval,.2)&&eq(rifle.reloadTime,3),"rifle exact weapon stats");
    m.refreshStandings();check(m.leaderboard.size()==2&&!m.collectOrb(3,0),"host does not enter scores or collect resources");
    m.startNextRound();check(m.findFighter(1)->weaponKind==WeaponKind::Shotgun&&(m.findFighter(1)->unlockedWeapons&weaponBit(WeaponKind::Shotgun))&&m.findFighter(1)->ammo==5&&m.findFighter(3)->isHost&&m.findFighter(3)->weaponKind==WeaponKind::Rifle,"next round preserves shared weapon unlock and host identity while resetting ammo");
    Match e=quiet();e.add(10,Shape::Rectangle,1);e.addHost(11,1);e.step(60);
    check(e.evolutionPacks.size()==10,"minute one spawns ten evolution packs");
    for(size_t i=0;i<10;++i)for(size_t j=0;j<i;++j)check((e.evolutionPacks[i].position-e.evolutionPacks[j].position).length()>1,"evolution packs have distinct positions");
    check(!e.collectEvolutionPack(11,0)&&e.collectEvolutionPack(10,0),"host excluded while viewer can evolve");
    check(eq(e.fighters[0].maxHp,400)&&eq(e.fighters[0].hp,400)&&eq(e.fighters[0].armor,100),"evolution doubles hp and grants armor");
    e.step(10);e.fighters[0].armor=20;check(e.collectEvolutionPack(10,1)&&eq(e.fighters[0].maxHp,400)&&eq(e.fighters[0].evolutionRemaining,40)&&eq(e.fighters[0].armor,20),"repeat evolution only refreshes duration, not armor or hp multiplier");
    e.fighters[0].hp=100;e.step(40);check(eq(e.fighters[0].maxHp,200)&&eq(e.fighters[0].hp,50)&&eq(e.fighters[0].armor,0),"expiry keeps hp ratio and removes temporary armor");
    e.collectEvolutionPack(10,2);e.damageEnvironment(10,10000);check(eq(e.fighters[0].evolutionRemaining,0)&&eq(e.fighters[0].maxHp,200)&&eq(e.fighters[0].armor,0),"death cancels evolution immediately");
    e.step(300-e.elapsed);check(e.evolutionPacks.size()==50,"five minute endpoints each spawn ten packs including the new sprint boundary");
    e.step(60);check(e.evolutionPacks.size()==60,"sixth minute keeps spawning ten packs during sprint");
    e.step(60);check(e.phase==Phase::Results&&e.evolutionPacks.size()==60,"results at seven minutes prevents a new evolution spawn");
    Match cap=quiet();for(int i=0;i<500;++i)check(cap.add(i,Shape::Rectangle,i<100?0:i<300?1:2),"500 legal viewers");check(cap.addHost(6000,1)&&cap.fighters.size()==501,"host extra seat fits full viewer population");
    check(!cap.addHost(6001,2)&&!cap.add(6002,Shape::Circle,1),"only one host and no audience overflow");
    std::cout<<assertions<<" expansion assertions passed\n";
}catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<error.what()<<'\n';}return failures?1:0;}
