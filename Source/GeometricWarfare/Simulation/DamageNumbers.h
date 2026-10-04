#pragma once
#include "ArenaPhysics.h"

namespace gw {
enum class NumberKind { Damage, Healing };
// team is the recipient color; targetKind: 1 player id, 2 NPC index, 3 base team.
struct DamageEvent { Vec position; double amount=0; int team=0, targetKind=0, targetId=-1; NumberKind kind=NumberKind::Damage; };

class DamageNumbers {
public:
    static constexpr size_t Capacity=128;
    static constexpr double Lifetime=1.05, MergeWindow=.18;
    struct Slot {
        Vec position;
        double amount=0,age=0;
        int team=0,targetKind=0,targetId=-1;
        bool active=false;
        NumberKind kind=NumberKind::Damage;
        double alpha() const {return active?std::clamp(1.0-age/Lifetime,0.0,1.0):0.0;}
        // Screen pixels, independent of the world camera zoom.
        double rise() const {return std::min(age,Lifetime)*44;}
    };
    // Heap-backed stable slots keep the containing UObject well below UHT's offset limit.
    std::vector<Slot> slots=std::vector<Slot>(Capacity);
    void add(const DamageEvent& event) {
        if(!std::isfinite(event.amount)||event.amount<=0||!std::isfinite(event.position.x)||!std::isfinite(event.position.y))return;
        for(auto& slot:slots)if(slot.active&&slot.age<=MergeWindow&&slot.targetKind==event.targetKind&&slot.targetId==event.targetId&&slot.kind==event.kind){
            slot.amount+=event.amount;slot.position=event.position;slot.team=event.team;return;
        }
        size_t index=nextSlot;
        bool found=false;
        for(size_t offset=0;offset<Capacity;++offset){const size_t candidate=(nextSlot+offset)%Capacity;if(!slots[candidate].active){index=candidate;found=true;break;}}
        if(!found)for(size_t i=0;i<Capacity;++i)if(slots[i].age>slots[index].age)index=i;
        auto& slot=slots[index];slot={event.position,event.amount,0,event.team,event.targetKind,event.targetId,true,event.kind};
        nextSlot=(index+1)%Capacity;
    }
    void step(double displayDt) {
        if(!std::isfinite(displayDt)||displayDt<=0)return;
        for(auto& slot:slots)if(slot.active){slot.age+=displayDt;if(slot.age>=Lifetime)slot.active=false;}
    }
    size_t activeCount() const {size_t count=0;for(const auto& slot:slots)count+=slot.active?1:0;return count;}
    void reset(){for(auto& slot:slots)slot.active=false;nextSlot=0;}
private:
    size_t nextSlot=0;
};
}
