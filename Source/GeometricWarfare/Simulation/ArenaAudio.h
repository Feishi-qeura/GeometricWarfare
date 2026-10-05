#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cmath>
namespace gw {
// The first six indices match WeaponKind. Laser/Stomp variants share logical categories.
enum class AudioKind {Pistol,Shotgun,Rifle,Sniper,MachineGun,Rocket,FighterHit,FighterDeath,BossBullet,BossLaserWindup,BossLaserRage,BossLaserBeam,BossStompJump,BossStompImpact,Orb,NpcHit,NpcDeath,WeaponPickup,WeaponBreak,EvolutionPickup,EvolutionBreak,Count};
inline constexpr int AudioKindCount=static_cast<int>(AudioKind::Count);
struct AudioCandidate {int actor=-1;bool present=false;};
struct AudioSlot {AudioCandidate ordinary,important;uint64_t count=0;};
struct AudioEvents {
    std::array<AudioSlot,AudioKindCount> slots{};
    int focusId=-1,hostId=-1;
    void emit(AudioKind kind,int actor=-1){
        const int index=static_cast<int>(kind);if(index<0||index>=AudioKindCount)return;
        auto& slot=slots[index];++slot.count;
        auto& candidate=actor>=0&&(actor==hostId||actor==focusId)?slot.important:slot.ordinary;
        candidate={actor,true};
    }
    uint64_t count(AudioKind kind)const{return slots[static_cast<int>(kind)].count;}
    void clear(){slots={};}
};
inline int audioPriority(AudioKind kind,bool important){
    if(kind>=AudioKind::BossBullet&&kind<=AudioKind::BossStompImpact)return 3;
    if(important)return 2;
    if(kind==AudioKind::FighterDeath||kind>=AudioKind::NpcDeath)return 1;
    return 0;
}
struct AudioBudget {
    double tokens=8,lastTime=-1;
    std::array<double,AudioKindCount> lastOrdinary{};
    int frameStarts=0;
    AudioBudget(){lastOrdinary.fill(-1000);}
    void beginFrame(double now){if(lastTime>=0)tokens=std::min(8.0,tokens+std::max(0.0,now-lastTime)*60);lastTime=now;frameStarts=0;}
    bool allow(AudioKind kind,int priority,double now){
        const int index=static_cast<int>(kind);
        const double interval=index<6?.08:kind==AudioKind::Orb?.12:.10;
        if(frameStarts>=4||tokens<1||(priority<2&&now-lastOrdinary[index]+1e-9<interval))return false;
        tokens-=1;++frameStarts;lastOrdinary[index]=now;return true;
    }
};
enum class MusicPhase {Battle,Boss,Sprint,Results};
inline MusicPhase chooseMusic(bool results,bool sprint,bool boss){return results?MusicPhase::Results:sprint?MusicPhase::Sprint:boss?MusicPhase::Boss:MusicPhase::Battle;}
}
