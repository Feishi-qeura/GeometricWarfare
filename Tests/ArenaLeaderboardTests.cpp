#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace gw;
static int checks=0;
static void require(bool v,const char* m){++checks;if(!v)throw std::runtime_error(m);}
static Match quiet(){MatchConfig c;c.autoCombat=c.autoCollect=false;c.npcCount=c.naturalOrbs=0;Match m(c);m.boss.spawned=true;return m;}
int main(){try{
    auto m=quiet();m.add(30,Shape::Rectangle,1);m.add(20,Shape::Rectangle,2);m.add(10,Shape::Rectangle,1);m.add(5,Shape::Rectangle,0);m.addHost(1,1);
    m.findFighter(5)->score=m.findFighter(1)->score=999999;
    for(int id:{30,20,10})m.findFighter(id)->score=100;
    m.findFighter(30)->kills=1;m.findFighter(20)->kills=2;m.findFighter(10)->kills=2;m.refreshStandings();
    require(m.leaderboard.size()==3,"only red/blue viewers rank");
    require(m.fighters[m.leaderboard[0]].id==10&&m.fighters[m.leaderboard[1]].id==20&&m.fighters[m.leaderboard[2]].id==30,"ties use kills then stable smaller ID");
    require(m.teamScores[1]==200&&m.teamScores[2]==100,"team sum excludes gray/host");
    m.grantWeapon(20,WeaponKind::RocketLauncher);m.rebuildOrFortifyBase(2,100);m.refreshStandings();
    require(m.fighters[m.leaderboard[0]].id==10&&m.findFighter(20)->score==100,"paid items do not directly add ranking score");
    m.orbs.push_back({{World::Size*.5,World::Size*.5},80,false,true,0});require(m.collectOrb(30,0),"nonpaying viewer collects score");
    m.step(.25);require(m.fighters[m.leaderboard[0]].id==30,"live standings update after scoring within250ms");
    require(m.damagePlayer(20,30,10000),"ranked viewer can die");m.step(.25);
    require(m.findFighter(30)->score==144&&m.findFighter(20)->score==136,"death transfers floor20percent once");
    require(m.leaderboard.size()==3&&!m.findFighter(30)->alive,"dead viewer retains rank and remaining score");
    m.phase=Phase::Sprint;m.elapsed=m.config.battleSeconds-.001;m.step(.001);
    const auto frozen=m.leaderboard;const auto totals=m.teamScores;
    require(m.phase==Phase::Results&&m.winnerTeam==1&&m.mvpId==30&&m.fmvpId==20,"settles winner MVP FMVP from round combat scores");
    require(!m.collectOrb(20,0)&&!m.damagePlayer(20,10,20),"results rejects late score sources");m.refreshStandings();
    require(m.leaderboard==frozen&&m.teamScores==totals,"results freezes displayed rankings");
    m.startNextRound();require(m.round==2&&m.teamScores[1]==0&&m.teamScores[2]==0,"next round resets both team scores");
    for(const auto& f:m.fighters)require(f.score==0&&f.kills==0,"next round resets every personal score and kills");
    require(m.findFighter(20)->unlockedWeapons&weaponBit(WeaponKind::RocketLauncher),"next round retains paid weapon entitlement independently of score");
    auto big=quiet();
    for(int i=0;i<500;++i){const int team=i<200?1:i<400?2:0;require(big.add(500-i,Shape::Rectangle,team),"500 participants fit exact capacities");auto* f=big.findFighter(500-i);f->score=(i*17)%313;f->kills=i%7;}
    big.addHost(6000,1);big.findFighter(6000)->score=999999;big.refreshStandings();
    std::vector<int> expected;int64_t red=0,blue=0;
    for(size_t i=0;i<big.fighters.size();++i){const auto& f=big.fighters[i];if(!f.isHost&&f.team>0){expected.push_back(static_cast<int>(i));(f.team==1?red:blue)+=f.score;}}
    std::sort(expected.begin(),expected.end(),[&](int a,int b){const auto& x=big.fighters[a];const auto& y=big.fighters[b];if(x.score!=y.score)return x.score>y.score;if(x.kills!=y.kills)return x.kills>y.kills;return x.id<y.id;});expected.resize(20);
    require(big.leaderboard==expected,"500-player top20 equals independently fully sorted400 eligible viewers");
    require(big.teamScores[1]==red&&big.teamScores[2]==blue,"team totals include all eligible viewers beyond top20");
    std::cout<<"PASS leaderboard: "<<checks<<" assertions, 0 failures\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL leaderboard: "<<e.what()<<"\n";return 1;}}
