#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace gw;
static int assertions=0;
static void check(bool ok,const char* message){++assertions;if(!ok)throw std::runtime_error(message);}
static bool eq(double a,double b){return std::abs(a-b)<1e-5;}
static Match quiet(){MatchConfig c;c.autoCombat=false;c.autoCollect=false;c.npcCount=0;return Match(c);}
static void timingAndScore(){
    auto m=quiet();check(eq(m.config.battleSeconds,420)&&eq(m.config.sprintSeconds,300),"seven minutes with final two minute Sprint");
    m.step(299.999);check(m.phase==Phase::Battle,"Battle still active before300");m.step(.001);check(m.phase==Phase::Sprint,"Sprint starts exactly300");
    m.step(119.999);check(m.phase==Phase::Sprint,"Sprint spans full120seconds");m.step(.001);check(m.phase==Phase::Results,"Results begins exactly420");m.step(30);check(m.round==2&&eq(m.elapsed,0),"round cycles exactly450");
    auto score=quiet();score.add(1,Shape::Rectangle,1);score.add(2,Shape::Rectangle,2);score.add(3,Shape::Rectangle,0);
    score.fighters[0].score=3;score.fighters[1].score=73;score.damagePlayer(1,2,10000);
    check(score.fighters[0].score==17&&score.fighters[1].score==59,"death retains80percent and transfers samefloor20percent");
    score.refreshStandings();check(score.teamScores[1]+score.teamScores[2]==76,"twentypercent transfer conserves total score");
    score.step(14.999);check(!score.fighters[1].alive,"redblue waits full15seconds");score.step(.001);check(score.fighters[1].alive&&score.fighters[1].score==59,"respawn retains remainder exactly15seconds");
    score.fighters[1].score=91;score.damagePlayer(3,2,10000);check(score.fighters[1].score==73&&score.orbs.back().value==18,"gray kill drops only20percent for scoring viewers");
    score.damageEnvironment(3,10000);score.step(19.999);check(!score.fighters[2].alive,"gray waits full20seconds");score.step(.001);check(score.fighters[2].alive,"gray revives exactly20seconds");
    const Vec spawn=score.world.bodies[2].position;check(std::abs(spawn.x-4000)<400&&std::abs(spawn.y-1920)<400,"gray revival is upper center");
}
static void heroes(){
    MatchConfig c;c.autoCombat=false;c.autoCollect=false;c.naturalOrbs=0;c.npcCount=0;c.sprintSeconds=2;c.battleSeconds=20;Match m(c);
    for(int id=1;id<=12;++id){m.add(id,Shape::Rectangle,id%2+1);m.findFighter(id)->score=1000-id;}
    m.findFighter(1)->score=10000;m.damageEnvironment(1,100000);m.addHost(100,1);m.findFighter(100)->score=1000000;
    m.step(1.999);check(!m.findFighter(1)->alive,"deadleader not revived early");m.step(.001);
    int count=0;for(const auto& f:m.fighters)if(f.heroBuff){++count;check(f.alive&&eq(f.hp,2100)&&eq(f.maxHp,2100)&&eq(f.armor,300),"topten receivesexactHPandarmor");check(eq(f.heroSwordRemaining,5),"herowaveclockstarts atSprintboundary");}
    check(count==10&&m.findFighter(1)->heroBuff&&m.findFighter(1)->alive,"deadtopten revived beforegrant");check(!m.findFighter(11)->heroBuff&&!m.findFighter(12)->heroBuff&&!m.findFighter(100)->heroBuff,"noherotoeleventh/host");
    const auto* first=m.findFighter(1);check(eq(first->hp,2100)&&first->score==8000,"automatichero revive retainsdeathscore");
    m.grantEvolution(1);check(eq(m.findFighter(1)->hp,2100)&&eq(m.findFighter(1)->armor,300),"evolution cannot overridefixedhero life andarmor");
    m.findFighter(1)->evolutionRemaining=.001;m.step(.001);check(eq(m.findFighter(1)->maxHp,2100)&&eq(m.findFighter(1)->maxArmor,300),"evolution expiry preserveshero stats");
    m.changeShape(1,Shape::Circle);check(eq(m.findFighter(1)->maxHp,2100),"shapechange preserveshero life");
    m.damageEnvironment(1,100000);m.step(15);check(!m.findFighter(1)->alive&&!m.revive(1),"herodeathdoesnotgrantunlimitedSprintrevives");
    check(m.revive(1,true)&&m.findFighter(1)->heroBuff&&eq(m.findFighter(1)->hp,2100)&&eq(m.findFighter(1)->armor,300),"fairywandrestoresroundheroqualification");
    m.startNextRound();check(!m.findFighter(1)->heroBuff&&eq(m.findFighter(1)->maxHp,300),"newround resetshero status");
}
static void heroSwords(){
    MatchConfig c;c.naturalOrbs=0;c.npcCount=0;c.autoCollect=false;Match m(c);m.weapon.range=0;
    m.add(1,Shape::Rectangle,1);m.add(2,Shape::Rectangle,2);m.add(3,Shape::Rectangle,2);
    for(size_t i=0;i<m.fighters.size();++i){m.world.bodies[i].position={1000+1500.0*i,1000};m.world.bodies[i].velocity={90,0};}
    m.world.rebuildSpatial();auto& owner=m.fighters[0];owner.heroBuff=true;owner.heroSwordRemaining=5;owner.aimAngle=.4;
    m.step(4.999);check(m.swordWaves.empty(),"hero waits5seconds");m.step(.001);check(m.swordWaves.size()==1,"hero starts its burst with only one sword at five seconds");
    check(eq(m.swordWaves[0].speed,880)&&eq(m.swordWaves[0].life,1.25)&&eq(m.swordWaves[0].traveled,0),"first hero sword has double speed and no age before birth");
    owner.aimAngle=1.7;
    for(size_t i=1;i<6;++i){m.step(.199);check(m.swordWaves.size()==i,"next hero sword never fires before point two seconds");const Vec birthPosition=m.world.bodies[0].position;m.step(.001);check(m.swordWaves.size()==i+1,"next sword fires at each point two second boundary");
        const auto& wave=m.swordWaves.back();const double offset=(-.2+.4*i/5.0)*44;const Vec side{-std::sin(.4),std::cos(.4)};
        check(wave.hero&&eq(wave.damage,30)&&eq(wave.maxDistance,1100)&&eq(wave.speed,880)&&eq(wave.life,1.25)&&eq(wave.traveled,0)&&eq(wave.direction.x,std::cos(.4))&&eq(wave.direction.y,std::sin(.4)),"hero burst locks one direction and gives each newborn a full range");
        const Vec separation=wave.from-birthPosition;check(eq(separation.x,side.x*offset)&&eq(separation.y,side.y*offset),"each sword starts from current position with its narrow strip offset");}
    m.swordWaves.clear();owner.heroSwordRemaining=100;m.config.autoCombat=false;
    m.world.bodies[0].position={1000,1000};m.world.bodies[1].position={1050,1000};m.world.bodies[2].position={1050,1100};
    m.fighters[1].hp=500;m.fighters[2].hp=500;m.fighters[2].heroBuff=true;m.fighters[2].heroSwordRemaining=100;
    for(auto& b:m.world.bodies)b.velocity={90,0};m.world.rebuildSpatial();
    SwordWave ordinary;ordinary.hero=true;ordinary.from=ordinary.position={1000,1000};ordinary.direction={1,0};ordinary.ownerId=1;ordinary.team=1;ordinary.damage=30;
    auto versusHero=ordinary;versusHero.from=versusHero.position={1000,1100};m.swordWaves={ordinary,versusHero};m.step(.1);
    check(eq(m.fighters[1].hp,470)&&eq(m.fighters[2].hp,380),"30normal/120hero base damage, penetratingtargethandling");
    m.step(.1);check(eq(m.fighters[1].hp,470)&&eq(m.fighters[2].hp,380),"eachwave hitsagivenunitonlyonce");
}
static void heroScheduling(){
    const auto fixture=[](){auto m=quiet();m.config.autoCombat=true;m.weapon.range=0;m.add(1,Shape::Rectangle,1);m.world.bodies[0].position={1000,1000};m.world.bodies[0].velocity={90,0};m.world.rebuildSpatial();m.fighters[0].heroBuff=true;m.fighters[0].heroSwordRemaining=.007;return m;};
    auto coarse=fixture(),fine=fixture();coarse.step(.027);for(int i=0;i<27;++i)fine.step(.001);
    check(coarse.swordWaves.size()==1&&fine.swordWaves.size()==1&&eq(coarse.swordWaves[0].traveled,17.6)&&eq(fine.swordWaves[0].traveled,17.6),"birth inside a frame consumes only the post-birth twenty milliseconds");
    coarse.step(.99);for(int i=0;i<990;++i)fine.step(.001);
    check(coarse.swordWaves.size()==6&&fine.swordWaves.size()==6,"large and finely divided steps both emit all six scheduled swords");
    for(size_t i=0;i<6;++i)check(eq(coarse.swordWaves[i].traveled,fine.swordWaves[i].traveled),"hero sword ages are independent of caller step partition");
    coarse.step(3.989);check(coarse.swordWaves.empty(),"hero burst does not restart before five seconds from its first sword");coarse.step(.001);
    check(coarse.swordWaves.size()==1&&eq(coarse.swordWaves[0].traveled,0),"next burst begins five seconds after previous first sword");
    auto death=fixture();death.step(.007);death.damageEnvironment(1,100000);death.step(.5);check(death.swordWaves.empty(),"death cancels already flying swords and pending burst");
    check(death.revive(1,true),"hero can revive after an interrupted burst");death.step(.5);check(death.swordWaves.empty(),"revival cannot resume the old burst");death.step(4.5);check(death.swordWaves.size()==1,"revived hero starts a fresh burst after five seconds");
    auto reset=fixture();reset.step(.007);reset.startNextRound();reset.step(1);check(reset.swordWaves.empty()&&!reset.fighters[0].heroBuff,"new round clears pending hero burst");
    auto ordinary=quiet();ordinary.config.autoCombat=true;ordinary.weapon.range=0;ordinary.add(2,Shape::Rectangle,1);ordinary.grantEvolution(2);ordinary.fighters[0].swordRemaining=.001;ordinary.step(.001);
    check(ordinary.swordWaves.size()==12,"ordinary evolution still fires twelve simultaneous swords");for(const auto& wave:ordinary.swordWaves)check(!wave.hero&&eq(wave.speed,440)&&eq(wave.maxDistance,1100)&&eq(wave.life,2.5),"ordinary evolution flight stays unchanged");
}
static void evolutionMagnet(){
    auto packs=quiet();packs.step(59.999);check(packs.evolutionPacks.empty(),"nopackbefore60");packs.step(.001);check(packs.evolutionPacks.size()==10,"tenpacksperminute");packs.step(300);check(packs.evolutionPacks.size()==60,"sixtybeforeroundends");
    MatchConfig c;c.autoCombat=false;c.naturalOrbs=2;c.npcCount=0;Match m(c);m.add(1,Shape::Square,1);
    m.orbs[0].position={1000,1000};m.orbs[1].position={7500,7500};m.world.bodies[0].position={1500,1000};m.world.bodies[0].velocity={0,90};m.world.rebuildSpatial();
    check(m.grantEvolution(1),"grantmagnet");const auto old=m.orbs[0].position;m.step(.1);
    check(m.orbs[0].position.x>old.x&&m.orbs[0].active,"orb visiblytravels towardevolvedunit");check(eq(m.orbs[1].position.x,7500)&&eq(m.orbs[1].position.y,7500),"outsideweaponrange remainsstill");
    m.step(1);check(!m.orbs[0].active&&m.fighters[0].score==4,"magnetusesnormalcollectionandshapeaward");
    auto host=quiet();host.addHost(1,0);check(eq(host.fighters[0].maxHp,3000),"hostbase3000");host.grantEvolution(1);check(eq(host.fighters[0].maxHp,6000)&&eq(host.fighters[0].armor,500),"Bossgrant canevolvehost withoutreducingarmor");host.fighters[0].evolutionRemaining=.001;host.step(.001);check(eq(host.fighters[0].maxHp,3000)&&eq(host.fighters[0].maxArmor,500),"hostevolutionendsbackat3000");
}
int main(){int fails=0;const auto run=[&](auto fn,const char* name){try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++fails;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    run(timingAndScore,"timing score respawn");run(heroes,"topten hero lifecycle");run(heroSwords,"sequential parallel hero swords and damage");run(heroScheduling,"hero birth scheduling and cancellation");run(evolutionMagnet,"evolution spawn magnet and host");
    std::cout<<assertions<<" assertions, "<<fails<<" failed groups\n";return fails?1:0;}
