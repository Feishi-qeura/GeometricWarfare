#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace gw;
static int assertions=0;
static void require(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
static bool close(double a,double b){return std::abs(a-b)<1e-6;}
// Compile on the previous implementation so the regression first fails on its
// missing behavior, rather than on a missing member declaration.
template<class T>static double taken(const T& f){if constexpr(requires{f.damageTaken;})return f.damageTaken;else return 0;}
template<class T>static int killsAward(const T& m){if constexpr(requires{m.mostKillsId;})return m.mostKillsId;else return -1;}
template<class T>static int damageAward(const T& m){if constexpr(requires{m.mostDamageTakenId;})return m.mostDamageTakenId;else return -1;}
static Match quiet(){MatchConfig c;c.autoCombat=false;c.autoCollect=false;c.npcCount=0;c.naturalOrbs=0;Match m(c);m.boss.spawned=true;return m;}
static void finish(Match& m){m.phase=Phase::Sprint;m.elapsed=m.config.battleSeconds-.01;m.step(.01);require(m.phase==Phase::Results,"reaches real results boundary");}
static void damageAccounting(){
    auto m=quiet();m.add(1,Shape::Rectangle,1);m.add(2,Shape::Rectangle,2);
    require(m.damagePlayer(1,2,100),"enemy damage accepted");
    require(close(taken(m.fighters[1]),100),"actual HP damage accumulates for awards");
    require(m.damageEnvironment(2,10000),"lethal environment damage accepted");
    require(close(taken(m.fighters[1]),200),"lethal overkill excluded from damage taken");
    require(m.fighters[1].deaths==1&&!m.fighters[1].alive,"victim dies once");
    require(!m.damageEnvironment(2,10)&&close(taken(m.fighters[1]),200),"dead targets do not accumulate damage");
    require(m.revive(2,true)&&close(taken(m.fighters[1]),200),"revival preserves current round stats");
    m.fighters[1].armor=85;m.fighters[1].maxArmor=85;
    require(m.damagePlayer(1,2,100),"armored hit accepted");
    require(close(m.fighters[1].hp,170)&&close(m.fighters[1].armor,25.5),"existing armor split remains 70 percent absorbed");
    require(close(taken(m.fighters[1]),300),"damage includes 70 absorbed rather than 59.5 armor cost");
    require(m.damageEnvironment(2,100),"environment path uses same accounting");
    require(close(taken(m.fighters[1]),400)&&close(m.fighters[1].armor,0),"partially depleted armor and HP counted once");
    const double before=taken(m.fighters[1]);
    require(!m.damagePlayer(1,2,-1)&&!m.damageEnvironment(2,std::numeric_limits<double>::quiet_NaN()),"invalid damage rejected");
    require(close(taken(m.fighters[1]),before),"invalid damage leaves stats unchanged");
    m.startNextRound();require(close(taken(m.fighters[0]),0)&&close(taken(m.fighters[1]),0),"next round clears damage stats");
    require(m.fighters[1].deaths==0&&m.fighters[0].kills==0,"next round clears kills and deaths");
    m.damagePlayer(1,2,20);m.healLike(2,1);require(close(taken(m.fighters[1]),20),"healing does not subtract or add damage taken");
}
static void defenceAndNeutral(){
    auto m=quiet();m.add(1,Shape::Rectangle,1);m.add(2,Shape::Circle,2);m.add(3,Shape::Rectangle,0);m.addHost(4,2);
    m.damagePlayer(1,2,100);require(close(taken(m.fighters[1]),50),"circle mitigation is excluded");
    m.damageEnvironment(2,100);require(close(taken(m.fighters[1]),100),"environment circle mitigation excluded too");
    m.damagePlayer(1,3,100);require(close(taken(m.fighters[2]),100),"gray participant damage is eligible");
    m.damagePlayer(1,4,100);require(close(taken(m.fighters[3]),100),"host combat damage accounting remains valid");
    require(!m.damagePlayer(1,1,100),"self damage rejected");
    m.add(5,Shape::Rectangle,1);require(!m.damagePlayer(1,5,100)&&close(taken(m.fighters[4]),0),"friendly damage excluded");
}
static void awardsAndReset(){
    auto m=quiet();m.add(30,Shape::Rectangle,1);m.add(20,Shape::Rectangle,2);m.add(10,Shape::Rectangle,0);m.addHost(40,1);
    m.damagePlayer(10,30,200);m.revive(30,true);m.damagePlayer(10,30,200);
    require(m.findFighter(10)->kills==2,"gray actual kills counted");
    m.damageEnvironment(10,200);require(!m.findFighter(10)->alive&&m.findFighter(10)->kills==2,"death retains kills");
    m.damagePlayer(40,20,200);m.revive(20,true);m.damagePlayer(40,20,200);m.revive(20,true);m.damagePlayer(40,20,200);
    m.damageEnvironment(40,5000);require(m.findFighter(40)->kills==3&&taken(*m.findFighter(40))>taken(*m.findFighter(20)),"host exceeds both award metrics in fixture");
    m.findFighter(30)->score=150;m.findFighter(20)->score=100;
    finish(m);
    require(killsAward(m)==10,"dead gray fighter wins kills award while host is excluded");
    require(damageAward(m)==20,"blue viewer with most actual damage wins endurance award");
    require(m.mvpId==30&&m.fmvpId==20,"new awards preserve existing team MVP and FMVP");
    const double finalDamage=taken(*m.findFighter(20));
    require(!m.damageEnvironment(20,100)&&close(taken(*m.findFighter(20)),finalDamage),"results freezes combat totals");
    m.startNextRound();require(killsAward(m)==-1&&damageAward(m)==-1,"next round clears award recipients");
    require(m.findFighter(10)->kills==0&&close(taken(*m.findFighter(20)),0),"participant stats clear on next round");
    finish(m);require(killsAward(m)==-1&&damageAward(m)==-1,"zero values do not receive empty awards");
}
static void deterministicTies(){
    auto m=quiet();m.add(30,Shape::Rectangle,1);m.add(20,Shape::Rectangle,2);m.add(10,Shape::Rectangle,0);
    for(auto& f:m.fighters){f.kills=2;f.score=40;m.damageEnvironment(f.id,30);}
    m.findFighter(30)->score=50;finish(m);
    require(killsAward(m)==30&&damageAward(m)==30,"score resolves equal primary metrics");
    m.startNextRound();
    for(auto& f:m.fighters){f.kills=2;f.score=50;m.damageEnvironment(f.id,30);}
    finish(m);require(killsAward(m)==10&&damageAward(m)==10,"lowest stable id resolves equal metric and score independently of insertion order");
}
int main(){try{damageAccounting();defenceAndNeutral();awardsAndReset();deterministicTies();std::cout<<"Awards: "<<assertions<<" assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL after "<<assertions<<": "<<e.what()<<'\n';return 1;}}
