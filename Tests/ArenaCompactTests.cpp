#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include "../Source/GeometricWarfare/Simulation/ArenaView.h"
#include <cmath>
#include <iostream>

static int assertions=0, failures=0;
static void check(bool ok,const char* name) { ++assertions; if(!ok){++failures;std::cerr<<"FAIL: "<<name<<'\n';} }
static bool close(double a,double b) {return std::abs(a-b)<1e-8;}
static bool bounded(gw::Vec p,double margin=0) {return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=margin-1e-8&&p.y>=margin-1e-8&&p.x<=2000-margin+1e-8&&p.y<=2000-margin+1e-8;}
static gw::Match quiet() {gw::MatchConfig config;config.autoCombat=false;config.autoCollect=false;return gw::Match(config);}

int main() {
    using namespace gw;
    // These exercise admission limits, including registered players who died.
    for(int team=0;team<3;++team) {
        auto match=quiet();const int limit=team==0?100:200;int joined=0;
        for(int id=0;id<limit;++id)if(match.add(id,static_cast<Shape>(id%4),team))++joined;
        check(joined==limit,"every seat in each compact team can join");
        check(!match.add(9000,Shape::Circle,team),"next seat is rejected at gray100 or red/blue200");
        check(match.damageEnvironment(0,1000000)&&!match.findFighter(0)->alive,"full-team viewer can die");
        check(match.teamCounts[team]==limit&&!match.add(9001,Shape::Circle,team),"dead viewer keeps its registered team seat");
    }
    for(bool hostFirst:{false,true}) {
        auto match=quiet();if(hostFirst)check(match.addHost(9000,1),"host can reserve separate seat before viewers");
        int joined=0;
        for(int id=0;id<500;++id)if(match.add(id,static_cast<Shape>(id%4),id<100?0:(id<300?1:2)))++joined;
        check(joined==500&&match.teamCounts==std::array<int,3>{100,200,200},"all500 viewers fit exact per-team capacities");
        check(!match.add(501,Shape::Circle,0)&&!match.add(502,Shape::Circle,1)&&!match.add(503,Shape::Circle,2),"501st viewer is rejected for every team");
        if(!hostFirst)check(match.addHost(9000,2),"host extra seat joins after full500 viewers");
        check(match.fighters.size()==501&&match.world.bodies.size()==501&&!match.addHost(9001,1),"one host joins with full crowd and second host is rejected");
        bool inBounds=true;for(const auto& body:match.world.bodies)for(Vec vertex:vertices(body))inBounds=inBounds&&bounded(vertex);
        check(inBounds,"all500 viewers and scaled host spawn physically inside compact arena");
        const auto* host=match.world.find(9000);check(host&&close(host->scale,2.5),"existing host geometry remains scale2.5");
        match.startNextRound();
        check(match.round==2&&match.fighters.size()==501&&match.teamCounts==std::array<int,3>{100,200,200}&&match.findFighter(9000)&&match.findFighter(9000)->isHost,"round transition preserves every spectator and separate host seat");
    }

    World world;int placed=0;
    for(int id=0;id<500;++id)if(world.add(id,static_cast<Shape>(id%4)))++placed;
    check(placed==500&&!world.add(500,Shape::Circle),"physics admits500 normal bodies and rejects501st");
    check(world.add(9000,Shape::Circle,true)&&!world.add(9001,Shape::Circle,true),"physics reserves exactly one additional host body");
    bool separated=true,inBounds=true;
    for(size_t i=0;i<world.bodies.size();++i) {
        for(Vec vertex:vertices(world.bodies[i]))inBounds=inBounds&&bounded(vertex);
        for(size_t j=i+1;j<world.bodies.size();++j){Vec normal;double depth;if(overlap(world.bodies[i],world.bodies[j],normal,depth))separated=false;}
    }
    check(separated&&inBounds,"501 unscaled physical shapes can spawn without penetration inside2000 square");
    for(int i=0;i<120;++i)world.step(1.0/60);
    inBounds=true;for(const auto& body:world.bodies)for(Vec vertex:vertices(body))inBounds=inBounds&&bounded(vertex);
    check(inBounds,"dense compact crowd remains finite and within physical walls while moving");
    std::vector<int> nearby;world.query({1000,1000},1500,nearby);
    check(nearby.size()==501,"whole-map spatial query still returns every body at both grid edges");
    Body circle,square,rectangle,triangle;circle.shape=Shape::Circle;square.shape=Shape::Square;rectangle.shape=Shape::Rectangle;triangle.shape=Shape::Triangle;
    check(close(detail::geometry(circle).radius,22)&&close(detail::geometry(square).maxx,21)&&close(detail::geometry(rectangle).maxx,30)&&close(detail::geometry(rectangle).maxy,15)&&close(detail::geometry(triangle).miny,-29),"viewer physical geometry retains original radii and polygon dimensions");

    auto resources=quiet();check(resources.orbs.size()==120&&resources.npcs.size()==24,"default compact map initializes120 natural orbs and24 NPCs");
    check(close(resources.bases[1].position.x,360)&&close(resources.bases[2].position.x,1640)&&close(resources.bases[1].position.y,1000),"red and blue bases map to compact arena flanks");
    bool resourcesBounded=true;
    for(const auto& orb:resources.orbs)resourcesBounded=resourcesBounded&&bounded(orb.position,80);
    for(const auto& npc:resources.npcs)resourcesBounded=resourcesBounded&&bounded(npc.position,32);
    resources.step(150);
    check(resources.boss.active&&close(resources.boss.position.x,1000)&&close(resources.boss.position.y,1000),"boss still arrives at150seconds at compact center");
    resourcesBounded=resourcesBounded&&bounded(resources.boss.position,BossDiameter*.5)&&!resources.evolutionPacks.empty()&&!resources.weaponCrates.empty();
    for(const auto& pack:resources.evolutionPacks)resourcesBounded=resourcesBounded&&bounded(pack.position,PickupHalfExtent);
    for(const auto& crate:resources.weaponCrates)resourcesBounded=resourcesBounded&&bounded(crate.position,PickupHalfExtent);
    check(resourcesBounded,"natural resources boss evolution packs and weapon crates remain physically inbounds");
    int lateJoined=0;for(int id=0;id<500;++id)if(resources.add(id,static_cast<Shape>(id%4),id<100?0:(id<300?1:2)))++lateJoined;
    check(lateJoined==500&&resources.addHost(9000,1),"full500 viewers and host can join with active boss NPCs and pickup obstacles");
    check(close(resources.config.battleSeconds,420)&&close(resources.config.sprintSeconds,300)&&close(resources.config.resultsSeconds,30),"compact map preserves round and sprint durations");
    check(close(BossFireRadius,600)&&close(BossWaveRadius,900)&&close(BossAttackRange,4000)&&close(BossDiameter,220),"compact boss preserves combat radii range and size");
    check(close(resources.weaponFor(WeaponKind::Sniper).range,4800)&&close(resources.weaponFor(WeaponKind::RocketLauncher).range,4900)&&close(resources.weaponFor(WeaponKind::Rifle).damage,8),"weapon ranges and damage retain existing numeric values");

    ArenaView view;
    check(close(view.zoom,1)&&close(view.center.x,1000)&&close(view.center.y,1000)&&close(view.screenToWorld(0,0).x,0)&&close(view.screenToWorld(0,0).y,0)&&close(view.screenToWorld(1,1).x,2000)&&close(view.screenToWorld(1,1).y,2000),"default camera covers complete2000 square with no initial magnification");
    std::cout<<"Arena compact tests: "<<assertions<<" assertions, "<<failures<<" failures\n";
    return failures?1:0;
}
