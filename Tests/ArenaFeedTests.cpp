#include "../Source/GeometricWarfare/Simulation/ScrollFeed.h"
#include <string>
#include <cstdio>
#include <set>
int main() {
    gw::ScrollFeed<std::string> feed;
    int failures=0;
    auto check=[&](bool value,const char* what) { if(!value) { ++failures; std::printf("FAIL %s\n",what); } };
    const void* pool=feed.slots.data();
    for(int i=0;i<50;++i) check(feed.enqueue(std::to_string(i),i%3),"50 arrivals accepted");
    check(feed.pending()==50,"burst is queued before playback");
    feed.step(.1); check(feed.delivered()==1,"one arrival shown first, not whole burst");
    std::set<std::string> seen;
    for(int i=0;i<650;++i) { feed.step(.05); for(const auto& s:feed.slots) if(s.active) seen.insert(s.text); }
    check(seen.size()==50,"every one of 50 messages became visible");
    check(feed.delivered()==50 && feed.pending()==0,"burst drains exactly once");
    check(feed.slots.data()==pool,"fixed display pool is reused");
    bool active=false; for(const auto& s:feed.slots) active|=s.active;
    check(!active,"old messages fully fade");
    feed.reset(); check(feed.pending()==0 && feed.delivered()==0,"reset clears pending messages");
    for(int i=0;i<5000;++i) check(feed.enqueue("viewer"),"5000 arrivals fit bounded queue");
    std::printf("Feed tests: %d failures\n",failures);
    return failures?1:0;
}
