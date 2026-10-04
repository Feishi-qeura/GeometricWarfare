#include "../Source/GeometricWarfare/Simulation/ArenaPhysics.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

using Clock=std::chrono::steady_clock;
static double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
static void report(const char* name, std::vector<double> samples) {
    double total=0;
    for(double sample:samples) total+=sample;
    std::sort(samples.begin(),samples.end());
    std::cout << name << " mean_ms=" << total/static_cast<double>(samples.size())
        << " p95_ms=" << samples[samples.size()*95/100] << " max_ms=" << samples.back() << '\n';
}
static bool run(const char* name, bool crowded, bool coincident) {
    gw::World world;
    const auto spawnStart=Clock::now();
    for(int i=0;i<5000;++i) if(!world.add(i,static_cast<gw::Shape>(i%4))) {
        std::cerr << "Spawn failed at " << i << '\n'; return false;
    }
    std::cout << "scenario=" << name << " players=" << world.bodies.size()
        << " map=" << gw::World::Size << " spawn_ms=" << milliseconds(spawnStart) << '\n';
    if(crowded) for(int i=0;i<5000;++i) {
        const int group=i/1250, local=i%1250;
        const gw::Vec base={1500.0+(group%2)*4000,1500.0+(group/2)*4000};
        world.bodies[i].position=coincident?gw::Vec{4000,4000}:base+gw::Vec{(local%36)*32.0,(local/36)*32.0};
    }
    world.rebuildSpatial();
    std::vector<double> physics,query,limitedQuery;
    std::vector<int> nearby; nearby.reserve(5000);
    size_t queryHits=0,limitedHits=0,peakContacts=0;
    // Ordinary runs warm caches; the coincident test includes its initial surge.
    if(!coincident) for(int i=0;i<60;++i) world.step(1.0/60);
    const int frames=coincident?60:600;
    for(int frame=0;frame<frames;++frame) {
        auto begin=Clock::now(); world.step(1.0/60); physics.push_back(milliseconds(begin));
        peakContacts=std::max(peakContacts,world.contacts.size());
        begin=Clock::now();
        // 167 staggered targeting queries/frame approximates one query/player/0.5s.
        for(int k=0;k<167;++k) {
            const auto& body=world.bodies[(frame*167+k)%5000];
            world.query(body.position,k%4==0?975:650,nearby); queryHits+=nearby.size();
        }
        query.push_back(milliseconds(begin));
        begin=Clock::now();
        for(int k=0;k<167;++k) {
            const auto& body=world.bodies[(frame*167+k)%5000];
            world.queryLimited(body.position,k%4==0?975:650,nearby,128); limitedHits+=nearby.size();
        }
        limitedQuery.push_back(milliseconds(begin));
    }
    report("physics_60hz_frame",physics); report("167_radius_queries",query);
    report("167_bounded_radius_queries",limitedQuery);
    std::cout << "peak_unique_contacts=" << peakContacts << " query_hits=" << queryHits << " bounded_query_hits=" << limitedHits << '\n';
    for(const auto& body:world.bodies)
        if(!std::isfinite(body.position.x) || !std::isfinite(body.position.y)) return false;
    return true;
}
int main() {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Portable physics only; excludes Unreal rendering, combat and UI.\n";
    return run("uniform",false,false) && run("four_crowds",true,false)
        && run("all_coincident_stress",true,true)?0:1;
}
