#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace gw;
static int assertions=0;
static void require(bool value,const char* message) { ++assertions; if(!value) throw std::runtime_error(message); }
static bool close(double a,double b) { return std::abs(a-b)<1e-6; }
static Match quiet() { MatchConfig c; c.autoCombat=false; c.autoCollect=false; return Match(c); }
static void phaseBoundaries() {
    Match m=quiet(); m.step(299.9); require(m.phase==Phase::Battle,"battle lasts until 300");
    m.step(.1); require(m.phase==Phase::Sprint,"sprint begins exactly at 300");
    require(!m.bases[1].alive&&!m.bases[2].alive,"300 destroys both bases");
    require(!m.rebuildBase(1),"sprint forbids base reconstruction");
    m.step(120); require(m.phase==Phase::Results,"results begins exactly at 420");
    require(close(m.intermissionRemaining,30),"results lasts 30 seconds");
    m.step(29.9); require(m.round==1,"no early next round");
    m.step(.1); require(m.round==2&&m.phase==Phase::Battle&&close(m.elapsed,0),"450 begins next round exactly");
}
static void sprintWinsSimultaneousRespawn() {
    Match m=quiet(); require(m.add(1,Shape::Rectangle,1)&&m.add(2,Shape::Triangle,2)&&m.add(3,Shape::Rectangle,0),"boundary participants join");
    for(int id=10;id<20;++id){require(m.add(id,Shape::Rectangle,1),"higher-ranked hero candidate joins");m.findFighter(id)->score=1000;}
    m.step(280); require(m.damagePlayer(2,3,10000),"gray dies twenty seconds before sprint");
    m.step(5); require(m.damagePlayer(2,1,10000),"ordinary red dies fifteen seconds before sprint");
    m.step(14.999); require(!m.fighters[0].alive&&!m.fighters[2].alive,"ordinary red and gray remain dead immediately before300");
    m.step(.001);
    require(m.phase==Phase::Sprint&&!m.bases[1].alive,"sprint destroys base at300");
    require(!m.fighters[0].alive&&!m.world.bodies[0].active&&!m.fighters[0].heroBuff,"base destruction wins ordinary red respawn due exactly300");
    require(!m.fighters[2].alive&&!m.world.bodies[2].active,"sprint blocks gray respawn due exactly300");
    m.step(1);require(!m.fighters[0].alive,"red remains dead after simultaneous sprint boundary");
}
static void rankedHeroRevivesAtSprint() {
    Match m=quiet();
    for(int id=1;id<=11;++id){require(m.add(id,Shape::Rectangle,id%2?1:2),"hero ranking participant joins");m.findFighter(id)->score=id==1?10000:110-id;}
    require(m.add(20,Shape::Rectangle,0)&&m.addHost(21,1),"excluded gray and host candidates join");m.findFighter(20)->score=m.findFighter(21)->score=100000;
    m.step(299.999);m.damageEnvironment(1,100000);m.damageEnvironment(11,100000);
    require(!m.findFighter(1)->alive&&!m.findFighter(11)->alive,"ranked and ordinary players are dead before sprint entry");
    m.step(.001);
    const auto* hero=m.findFighter(1);
    require(hero->alive&&hero->heroBuff&&close(hero->hp,2100)&&close(hero->maxHp,2100)&&close(hero->armor,300),"top ten snapshot immediately revives its dead leader at full hero health and armor");
    require(close(hero->heroSwordRemaining,5),"new hero does not consume sword time before its sprint-entry birth");
    require(!m.findFighter(11)->alive&&!m.findFighter(11)->heroBuff,"rank eleven does not receive hero revival");
    require(!m.findFighter(20)->heroBuff&&!m.findFighter(21)->heroBuff,"gray and host scores never displace ranked viewer heroes");
}
static void resultsWinsSimultaneousRespawn() {
    Match m=quiet();require(m.add(1,Shape::Triangle,1)&&m.addHost(2,2),"result boundary participants join");
    m.step(405);require(m.damagePlayer(1,2,10000),"host dies fifteen seconds before results");
    m.step(14.99);require(!m.fighters[1].alive,"host still waiting at419.99");
    m.step(.01);require(m.phase==Phase::Results&&!m.fighters[1].alive&&!m.world.bodies[1].active,"results wins a host respawn otherwise permitted during sprint");
}
static void resultsWinsSimultaneousShot() {
    Match m=quiet();require(m.add(1,Shape::Square,1)&&m.add(2,Shape::Rectangle,2),"last shot participants join");
    m.step(419.99);
    m.world.bodies[0].position={World::Size*.25,World::Size*.5};m.world.bodies[1].position={World::Size*.25+200,World::Size*.5};
    for(auto& b:m.world.bodies)b.velocity={165,0};m.world.rebuildSpatial();
    m.fighters[0].targetKind=1;m.fighters[0].targetIndex=1;m.fighters[0].acquisitionRemaining=1;m.fighters[0].shotRemaining=.01;
    m.fighters[1].hp=10;m.fighters[1].score=37;m.weapon.spreadRadians=0;m.config.autoCombat=true;
    m.step(.01);
    require(m.phase==Phase::Results&&m.fighters[1].alive&&close(m.fighters[1].hp,10),"shot due exactly420 cannot kill");
    require(m.fighters[0].score==0&&m.teamScores[2]==37&&m.winnerTeam==2,"420 boundary shot cannot change result score or winner");
    require(m.fighters[0].ammo==10,"420 boundary shot cannot consume ammo");
}
static void resultsWinsSimultaneousPickup() {
    Match m=quiet();require(m.add(1,Shape::Square,1),"last pickup participant joins");m.step(419.99);
    m.world.bodies[0].position=m.orbs[0].position;m.world.bodies[0].velocity={90,0};m.world.rebuildSpatial();m.config.autoCollect=true;
    m.step(.01);require(m.phase==Phase::Results&&m.fighters[0].score==0&&m.orbs[0].active,"pickup at420 cannot mint points after result cutoff");
}
static void joiningAndShapes() {
    Match m=quiet();
    require(m.add(10,Shape::Circle),"join gray"); require(m.add(11,Shape::Triangle,1),"join red");
    require(!m.add(10,Shape::Square,2),"reject duplicate identity"); require(!m.add(12,Shape::Circle,3),"reject invalid team");
    require(m.fighters.size()==m.world.bodies.size(),"fighter and body indices agree");
    require(m.chooseTeam(10,2)&&m.fighters[0].team==2,"gray selects blue");
    require(!m.chooseTeam(10,1),"cannot steal carried score by switching team");
    m.fighters[0].hp=150; m.fighters[0].reloadRemaining=1.25;
    require(m.changeShape(10,Shape::Square),"switch shape");
    require(close(m.fighters[0].maxHp,250)&&close(m.fighters[0].hp,125),"shape keeps hp fraction");
    require(close(m.fighters[0].reloadRemaining,1.25),"shape keeps weapon timer");
    require(!m.changeShape(10,Shape::Triangle),"shape cooldown rejects spam");
    m.step(2); require(m.changeShape(10,Shape::Rectangle),"shape cooldown expires");
    require(close(m.fighters[0].hp,100),"rectangle preserves half hp");
}
static void teamCapacityRejection() {
    Match m=quiet();
    for(int i=0;i<100;++i) require(m.add(i,Shape::Circle,0),"gray joins up to100");
    require(!m.add(100,Shape::Circle,0),"gray101 is rejected");
    require(m.fighters.size()==100&&m.world.bodies.size()==100&&!m.findFighter(100),"rejected admission consumes no body or identity");
    require(m.add(100,Shape::Circle,1),"rejected identity can directly join available red");
    for(int i=101;i<300;++i) require(m.add(i,Shape::Circle,1),"red joins up to200");
    require(!m.add(300,Shape::Circle,1),"red201 is rejected");
    require(!m.chooseTeam(0,1)&&m.findFighter(0)->team==0,"full team leaves gray choice unchanged");
    require(m.chooseTeam(0,2)&&m.findFighter(0)->team==2,"available blue accepts gray");
    require(m.add(300,Shape::Circle,0),"successful selection releases gray slot");
    for(int i=301;i<500;++i) require(m.add(i,Shape::Circle,2),"blue joins up to200");
    require(!m.add(500,Shape::Circle,2),"blue201 is rejected");
    require(m.teamCounts==std::array<int,3>{100,200,200},"counts include all registered participants");
    require(Match::TeamCapacity(0)==100&&Match::TeamCapacity(1)==200&&Match::TeamCapacity(2)==200&&Match::TeamCapacity(-1)==0&&Match::TeamCapacity(3)==0,"capacity lookup validates teams");
    require(m.damagePlayer(100,0,10000),"capacity participant can die");
    require(m.teamCounts[2]==200&&!m.add(500,Shape::Circle,2),"death reserves existing participant slot");
    require(m.revive(0)&&m.teamCounts[2]==200,"revival does not increment participant counts");
    m.startNextRound();require(m.teamCounts==std::array<int,3>{100,200,200}&&m.fighters.size()==500,"next round preserves full distribution");
    m.reset();require(m.teamCounts==std::array<int,3>{0,0,0},"reset releases all team slots");
}
static void damageEvents() {
    Match m=quiet();require(m.add(11,Shape::Triangle,1)&&m.add(22,Shape::Circle,2)&&m.add(33,Shape::Rectangle,0),"damage feedback participants join");
    m.world.bodies[1].position={123,456};
    require(m.damagePlayer(11,22,20)&&m.damageEvents.size()==1,"player damage generates feedback");
    const auto first=m.damageEvents.back();
    require(close(first.amount,15)&&first.team==2&&first.targetKind==1&&first.targetId==22&&close(first.position.x,123)&&close(first.position.y,456),"feedback captures actual armored hp reduction, victim color and stable identity");
    require(!m.damagePlayer(11,11,20)&&m.damageEvents.size()==1,"invalid damage produces no feedback");
    m.damagePlayer(11,22,10000);require(close(m.damageEvents.back().amount,285),"overkill feedback clamps to remaining player hp");
    m.damagePlayer(11,33,15,true);require(close(m.damageEvents.back().amount,15)&&m.damageEvents.back().targetId==33,"triangle contact creates feedback");
    m.damageNpc(11,0,10000);require(close(m.damageEvents.back().amount,200)&&m.damageEvents.back().team==0&&m.damageEvents.back().targetKind==2&&m.damageEvents.back().targetId==0,"NPC feedback clamps actual hp with neutral color and NPC identity");
    m.damageBase(11,2,100000);require(close(m.damageEvents.back().amount,2500)&&m.damageEvents.back().team==2&&m.damageEvents.back().targetKind==3&&m.damageEvents.back().targetId==2,"base feedback clamps actual hp and uses base identity");
    m.damageEvents.clear();m.fighters[2].hp=100000;
    for(int i=0;i<5000;++i) m.damagePlayer(11,33,1);
    require(m.damageEvents.size()==512,"damage event storage stays bounded under bursts");
    m.damageEvents.clear();m.damagePlayer(11,33,2);require(m.damageEvents.size()==1&&close(m.damageEvents.back().amount,3),"consumer clear starts a fresh batch");
    m.reset();require(m.damageEvents.empty(),"reset clears damage feedback");
}
static void damageRules() {
    Match m=quiet(); require(m.add(1,Shape::Triangle,1),"triangle joins"); require(m.add(2,Shape::Circle,2),"circle joins");
    require(m.add(3,Shape::Square,2),"friendly joins"); require(m.add(4,Shape::Square,1),"square joins"); require(m.add(5,Shape::Rectangle,0),"gray joins");
    require(m.damagePlayer(1,2,20),"enemy receives bullet"); require(close(m.fighters[1].hp,285),"triangle1.5 times circle0.5");
    require(!m.damagePlayer(2,3,20)&&close(m.fighters[2].hp,250),"friendly fire disabled");
    require(m.damagePlayer(4,5,20)&&close(m.fighters[4].hp,150),"square neutral damage times2.5");
    require(m.damagePlayer(4,2,20)&&close(m.fighters[1].hp,275),"square does not multiply team damage");
    require(m.damagePlayer(1,5,15,true)&&close(m.fighters[4].hp,135),"triangle contact exactly15");
    require(!m.damagePlayer(1,5,15,true),"contact cooldown rejects repeat");
    m.step(.75); require(m.damagePlayer(1,5,15,true)&&close(m.fighters[4].hp,120),"contact cooldown expires");
    require(!m.damagePlayer(4,5,-2),"negative damage rejected");
    require(m.damageNpc(4,0,20)&&close(m.npcs[0].hp,150),"square multiplies npc damage");
    require(!m.damageBase(4,1,20),"cannot damage own base"); require(m.damageBase(4,2,20)&&close(m.bases[2].hp,2480),"square base damage normal");
    require(m.damageBase(1,2,20)&&close(m.bases[2].hp,2450),"triangle multiplies base damage");
}
static void scoreRules() {
    Match m=quiet(); require(m.add(1,Shape::Square,1),"score square"); require(m.add(2,Shape::Rectangle,2),"score victim"); require(m.add(3,Shape::Triangle,0),"score gray");
    require(m.collectOrb(1,0)&&m.fighters[0].score==4,"square doubles natural ball reward");
    require(!m.collectOrb(1,0),"consumed ball cannot pay twice");
    require(!m.collectOrb(3,1)&&m.orbs[1].active,"gray cannot collect");
    require(m.damageNpc(1,0,1000)&&m.fighters[0].score==54,"square doubles npc reward25");
    m.fighters[1].score=73; require(m.damagePlayer(1,2,10000),"kill blue");
    require(m.fighters[0].score==68&&m.fighters[1].score==59,"kill transfers floored twenty percent without square multiplier");
    require(m.fighters[0].kills==1&&m.fighters[1].deaths==1&&!m.fighters[1].alive,"kill bookkeeping");
    m.step(15); require(m.fighters[1].alive,"blue automatic respawn after fifteen seconds");
    m.fighters[1].score=91; const auto before=m.orbs.size(); require(m.damagePlayer(3,2,10000),"gray kill redblue");
    require(m.fighters[1].score==73&&m.fighters[2].score==0,"gray kill leaves eighty percent with victim and earns no personal score");
    require(m.orbs.size()==before+1&&m.orbs.back().value==18&&!m.orbs.back().natural,"eighteen dropped and seventy three retained conserve ninety one points");
    require(m.collectOrb(1,static_cast<int>(before))&&m.fighters[0].score==86,"square cannot double dropped score");
    m.refreshStandings(); require(m.teamScores[1]==86&&m.teamScores[2]==73,"team totals include retained score on dead participants");
    require(m.leaderboard.size()==2,"gray excluded from leaderboard");
}
static void respawnAndGifts() {
    Match m=quiet(); require(m.add(1,Shape::Triangle,1),"red joins"); require(m.add(2,Shape::Rectangle,2),"blue joins"); require(m.add(3,Shape::Rectangle,0),"gray joins");
    m.damagePlayer(1,2,10000); m.step(14.99); require(!m.fighters[1].alive,"team cannot respawn before fifteen seconds");
    m.step(.01); require(m.fighters[1].alive&&close(m.fighters[1].hp,200),"team respawns exactly fifteen seconds");
    m.damageBase(1,2,10000); m.damagePlayer(1,2,10000); m.step(16); require(!m.fighters[1].alive,"destroyed base prevents automatic respawn even after the full timer");
    require(m.changeShape(2,Shape::Square)&&!m.fighters[1].alive,"dead shape change cannot revive");
    require(m.rebuildBase(2),"local reconstruction before sprint"); m.step(.01); require(m.fighters[1].alive,"rebuilt base releases waiting fighter");
    m.damagePlayer(1,3,10000); m.step(19.99); require(!m.fighters[2].alive,"gray waits twenty seconds"); m.step(.01); require(m.fighters[2].alive,"gray respawns at twenty seconds");
    m.damagePlayer(1,2,10000); require(m.revive(2)&&m.fighters[1].alive,"local revival effect");
    require(!m.revive(2),"live fighter cannot revive");
    for(int id=10;id<20;++id){require(m.add(id,Shape::Rectangle,1),"higher-ranked hero candidate joins revive fixture");m.findFighter(id)->score=1000;}
    m.step(300-m.elapsed);m.damagePlayer(1,2,10000);m.step(15);require(!m.fighters[1].alive&&!m.fighters[1].heroBuff,"sprint prevents ordinary team respawn after its full timer");
    m.damagePlayer(1,3,10000);m.step(20);require(!m.fighters[2].alive&&!m.revive(3),"sprint blocks automatic and ordinary manual gray respawn");
}
static void resultsAndReset() {
    Match m=quiet(); require(m.add(10,Shape::Square,1),"result red"); require(m.add(20,Shape::Circle,2),"result blue"); require(m.add(30,Shape::Triangle,1),"result contributor");
    m.fighters[0].score=100; m.fighters[1].score=120; m.fighters[2].score=30; m.fighters[2].kills=10; m.fighters[2].baseDamage=200;
    m.step(420); require(m.winnerTeam==1,"winning team uses summed carried scores");
    require(m.mvpId==10,"mvp is highest scoring player from winning red team"); require(m.fmvpId==20,"fmvp is highest scoring player from losing blue team even if global score leader");
    require(m.teamScores[1]==130&&m.teamScores[2]==120,"results retain scores");
    require(m.add(40,Shape::Rectangle,2),"join during results");
    require(!m.damagePlayer(10,20,10000)&&!m.collectOrb(10,0)&&!m.rebuildBase(1),"results freezes combat and effects");
    m.refreshStandings(); require(m.mvpId==10&&m.fmvpId==20&&m.teamScores[1]==130&&m.leaderboard.size()==3,"results snapshots frozen despite join");
    m.step(30); require(m.round==2&&m.fighters.size()==4,"restart keeps participants");
    require(m.world.bodies[0].shape==Shape::Square&&m.fighters[0].team==1&&m.fighters[0].id==10,"restart preserves identity team shape");
    for(const auto& f:m.fighters) require(f.alive&&f.score==0&&f.kills==0&&f.deaths==0&&f.ammo==10&&close(f.hp,f.maxHp),"restart clears round state");
    require(m.bases[1].alive&&m.bases[2].alive&&m.mvpId==-1,"restart restores bases and clears awards");
    m.reset(); require(m.fighters.empty()&&m.world.bodies.empty()&&m.round==1,"full reset clears identities");
    Match tie=quiet(); tie.step(420); require(tie.winnerTeam==1||tie.winnerTeam==2,"existing equal-score outcome keeps its red-blue tie breaker");
    require(tie.mvpId==-1&&tie.fmvpId==-1,"empty match invents no award recipients");
}
static void teamAwards() {
    Match m=quiet();
    require(m.add(11,Shape::Rectangle,1)&&m.add(10,Shape::Rectangle,1)&&m.add(12,Shape::Rectangle,1),"losing award candidates join");
    require(m.add(21,Shape::Rectangle,2)&&m.add(20,Shape::Rectangle,2)&&m.add(22,Shape::Rectangle,2),"winning award candidates join");
    require(m.add(1,Shape::Rectangle,0)&&m.addHost(2,2),"excluded award candidates join");
    m.findFighter(11)->score=m.findFighter(10)->score=m.findFighter(12)->score=50;
    m.findFighter(11)->kills=m.findFighter(10)->kills=3;m.findFighter(12)->kills=1;m.findFighter(12)->baseDamage=100000;
    m.findFighter(21)->score=m.findFighter(20)->score=m.findFighter(22)->score=100;
    m.findFighter(21)->kills=m.findFighter(20)->kills=4;m.findFighter(22)->kills=2;m.findFighter(22)->baseDamage=100000;
    m.findFighter(1)->score=m.findFighter(2)->score=99999;m.findFighter(1)->kills=m.findFighter(2)->kills=99999;
    m.step(420);
    require(m.winnerTeam==2,"blue wins independently of excluded gray-host scores");
    require(m.mvpId==20&&m.fmvpId==10,"each team award sorts by score then kills then smallest stable id");
    require(m.mvpId!=m.fmvpId,"winning and losing award recipients belong to distinct teams");
    const auto& event=m.events.back();require(event.kind==EventKind::RoundEnded&&event.actorId==20&&event.targetId==10,"round ended event reports winning MVP and losing FMVP");
    Match absent=quiet();absent.add(9,Shape::Rectangle,1);absent.findFighter(9)->score=1;absent.step(420);
    require(absent.winnerTeam==1&&absent.mvpId==9&&absent.fmvpId==-1,"missing losing team has no fabricated FMVP");
}
static Match duel(Shape shooter,double distance) {
    MatchConfig config; config.naturalOrbs=0; config.npcCount=0; config.autoCollect=false;
    Match m(config); m.weapon.spreadRadians=0; m.weapon.magazine=1; m.weapon.fireInterval=100; m.weapon.aimTime=0;
    require(m.add(1,shooter,1)&&m.add(2,Shape::Square,2),"duel joins");
    m.world.bodies[0].position={World::Size*.25,World::Size*.5}; m.world.bodies[1].position={World::Size*.25+distance,World::Size*.5};
    for(auto& body:m.world.bodies) body.velocity={165,0};
    m.world.rebuildSpatial(); return m;
}
static void automaticWeapons() {
    auto circle=duel(Shape::Circle,300); circle.step(.04);
    require(circle.fighters[0].ammo==0&&circle.fighters[0].reloadRemaining>.9,"automatic pistol fires and starts circle reload");
    require(close(circle.fighters[1].hp,240),"automatic hitscan applies configured damage");
    require(!circle.shots.empty()&&circle.shots.front().life>0,"automatic fire creates trace");
    circle.step(1); require(circle.fighters[0].ammo==1&&close(circle.fighters[0].reloadRemaining,0),"circle reloads in half of two seconds");
    auto square=duel(Shape::Square,300); square.step(1.09);
    require(square.fighters[0].ammo==0&&square.fighters[0].reloadRemaining>0.9,"square reload still pending at1.09");
    square.step(.95); require(square.fighters[0].ammo==1,"ordinary reload completes after two seconds");
    auto shortRange=duel(Shape::Square,900); shortRange.step(.3); require(shortRange.fighters[0].ammo==1,"ordinary pistol cannot target900range");
    auto longRange=duel(Shape::Rectangle,900); longRange.step(.3); require(longRange.fighters[0].ammo==0,"rectangle range includes900");
    auto fastAim=duel(Shape::Rectangle,300); fastAim.weapon.aimTime=.6; fastAim.step(.34);
    require(fastAim.fighters[0].ammo==0,"rectangle halves aim time");
    auto slowAim=duel(Shape::Square,300); slowAim.weapon.aimTime=.6; slowAim.step(.34);
    require(slowAim.fighters[0].ammo==1,"ordinary shape still aims at.34");
    auto deterministicA=duel(Shape::Triangle,400),deterministicB=duel(Shape::Triangle,400);
    deterministicA.weapon.spreadRadians=deterministicB.weapon.spreadRadians=.2;
    deterministicA.step(.1); deterministicB.step(.1);
    require(close(deterministicA.shots[0].to.x,deterministicB.shots[0].to.x)&&close(deterministicA.shots[0].to.y,deterministicB.shots[0].to.y),"seeded spread is deterministic");
    auto normalAccuracy=duel(Shape::Circle,300),rectangleAccuracy=duel(Shape::Rectangle,300);
    normalAccuracy.weapon.spreadRadians=rectangleAccuracy.weapon.spreadRadians=.2;
    normalAccuracy.step(.04);rectangleAccuracy.step(.04);
    const Vec normalTrace=normalAccuracy.shots[0].to-normalAccuracy.shots[0].from,rectangleTrace=rectangleAccuracy.shots[0].to-rectangleAccuracy.shots[0].from;
    require(close(std::atan2(normalTrace.y,normalTrace.x),std::atan2(rectangleTrace.y,rectangleTrace.x)*1.5),"rectangle accuracy narrows the same seeded spread by1.5");
}
static void automaticResourcesAndFrozenWorld() {
    MatchConfig config; config.autoCombat=false; config.orbRespawnSeconds=.2; config.npcRespawnSeconds=.3;
    Match m(config); require(m.add(1,Shape::Square,1),"collector joins");
    m.world.bodies[0].position=m.orbs[0].position; m.world.bodies[0].velocity={90,0};m.world.rebuildSpatial();
    m.step(.01); require(m.fighters[0].score==4&&!m.orbs[0].active,"spatial automatic pickup");
    m.config.autoCollect=false; m.step(.2); require(m.orbs[0].active,"natural resource renews");
    m.damageNpc(1,0,1000); m.step(.3); require(m.npcs[0].active&&close(m.npcs[0].hp,200),"npc renews at configured delay");
    m.step(420-m.elapsed); const Vec position=m.world.bodies[0].position; const auto score=m.fighters[0].score;
    m.step(10); require(close(m.world.bodies[0].position.x,position.x)&&close(m.world.bodies[0].position.y,position.y)&&m.fighters[0].score==score,"results freezes simulation and score");
    m.step(25); require(m.round==2&&close(m.elapsed,5),"large delta carries through result into next round");
    const double previous=m.elapsed; m.step(-1);m.step(NAN);require(close(m.elapsed,previous),"invalid delta is ignored");
}
static void scale500() {
    Match m; const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<500;++i) require(m.add(i,static_cast<Shape>(i%4),i%5==0?0:1+i%2),"500 participants join");
    require(!m.add(501,Shape::Circle,1),"501st participant rejected");
    const auto spawned=std::chrono::steady_clock::now(); std::vector<double> milliseconds;
    for(int frame=0;frame<300;++frame) {
        m.events.clear(); const auto before=std::chrono::steady_clock::now(); m.step(1.0/30);
        milliseconds.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count());
    }
    double total=0; for(double ms:milliseconds) total+=ms; std::sort(milliseconds.begin(),milliseconds.end());
    std::cout<<"BENCH 500: spawn_ms="<<std::chrono::duration<double,std::milli>(spawned-start).count()<<" match30Hz_mean_ms="<<total/300<<" p95_ms="<<milliseconds[285]<<" shots="<<m.shots.size()<<"\n";
    require(m.shots.size()<=512&&m.events.size()<=512&&m.damageEvents.size()<=512,"visuals and event queues are bounded");
    require(m.leaderboard.size()==20,"500 match exposes top20");
    int kills=0; for(size_t i=0;i<m.fighters.size();++i) {
        const auto& f=m.fighters[i]; kills+=f.kills;
        require(f.id==m.world.bodies[i].id&&f.alive==m.world.bodies[i].active,"500 indices and active state remain aligned");
        require(std::isfinite(f.hp)&&std::isfinite(m.world.bodies[i].position.x),"large match remains finite");
        require(f.team!=0||f.score==0,"gray never earns score at scale");
    }
    require(kills>0,"500 benchmark exercised real kills");
    for(int i=0;i<500;++i) {
        const int cluster=i%4; m.world.bodies[i].position={World::Size*.25+World::Size*.5*(cluster%2)+(i%17),World::Size*.25+World::Size*.5*(cluster/2)+(i%13)};
    }
    m.world.rebuildSpatial(); const auto crowdedStart=std::chrono::steady_clock::now();
    for(int frame=0;frame<30;++frame) {m.events.clear();m.step(1.0/30);}
    std::cout<<"BENCH four dense crowds500: match30Hz_mean_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-crowdedStart).count()/30<<'\n';
    for(const auto& body:m.world.bodies) require(std::isfinite(body.position.x)&&std::isfinite(body.position.y),"dense combat stays finite");
}
int main() {
    int failures=0;
    try {teamAwards();std::cout<<"PASS team award semantics\n";} catch(const std::exception& e){++failures;std::cerr<<"FAIL team award semantics: "<<e.what()<<'\n';}
    try {teamCapacityRejection();std::cout<<"PASS team capacities\n";} catch(const std::exception& e){++failures;std::cerr<<"FAIL team capacities: "<<e.what()<<'\n';}
    try {damageEvents();std::cout<<"PASS damage events\n";} catch(const std::exception& e){++failures;std::cerr<<"FAIL damage events: "<<e.what()<<'\n';}
    const std::pair<const char*,void(*)()> tests[]={{"phase boundaries",phaseBoundaries},{"sprint simultaneous respawn",sprintWinsSimultaneousRespawn},{"ranked hero sprint revival",rankedHeroRevivesAtSprint},{"results simultaneous respawn",resultsWinsSimultaneousRespawn},{"results simultaneous shot",resultsWinsSimultaneousShot},{"results simultaneous pickup",resultsWinsSimultaneousPickup},{"joining and shapes",joiningAndShapes},{"damage",damageRules},{"score",scoreRules},{"respawn",respawnAndGifts},{"results",resultsAndReset},{"automatic weapons",automaticWeapons},{"automatic resources",automaticResourcesAndFrozenWorld},{"500 scale",scale500}};
    for(const auto& test:tests) try { test.second(); std::cout<<"PASS "<<test.first<<'\n'; } catch(const std::exception& error) { ++failures; std::cerr<<"FAIL "<<test.first<<": "<<error.what()<<'\n'; }
    std::cout<<assertions<<" assertions, "<<failures<<" failed groups\n"; return failures?1:0;
}
