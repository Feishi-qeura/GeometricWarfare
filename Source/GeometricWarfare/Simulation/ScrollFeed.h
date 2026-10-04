#pragma once
#include <array>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <vector>
namespace gw {
template<class T> class ScrollFeed {
public:
    struct Slot { T text{}; int team=0; double age=0; bool active=false; };
    std::array<Slot,8> slots{};
    static constexpr double Interval=.45;
    static constexpr double Lifetime=3.6;
    bool enqueue(const T& text,int team=0) {
        if(count==queue.size()) return false;
        auto& item=queue[(head+count)%queue.size()]; item.text=text; item.team=team;
        ++count; return true;
    }
    void step(double dt) {
        if(!std::isfinite(dt) || dt<=0) return;
        dt=std::min(dt,.25);
        for(auto& slot:slots) if(slot.active) { slot.age+=dt; if(slot.age>=Lifetime) slot.active=false; }
        untilNext-=dt;
        if(count && untilNext<=0) {
            auto& slot=slots[nextSlot];
            slot.text=queue[head].text; slot.team=queue[head].team; slot.age=0; slot.active=true;
            nextSlot=(nextSlot+1)%slots.size(); head=(head+1)%queue.size(); --count; ++shown;
            untilNext=Interval;
        }
        if(!count) untilNext=std::max(0.0,untilNext);
    }
    size_t pending() const { return count; }
    size_t delivered() const { return shown; }
    void reset() { head=count=nextSlot=shown=0; untilNext=0; for(auto& s:slots) s.active=false; }
private:
    struct Pending { T text{}; int team=0; };
    std::vector<Pending> queue=std::vector<Pending>(8192);
    size_t head=0,count=0,nextSlot=0,shown=0;
    double untilNext=0;
};
}
