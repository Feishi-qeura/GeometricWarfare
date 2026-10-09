#pragma once
#include <algorithm>

namespace gwui {
// Presentation only. Never changes simulation, combat, events or platform ACKs.
struct PresentationBudget {
    bool reduced=false;
    int labels=180,damageNumbers=128;
    float minimapInterval=.2f;
};
inline PresentationBudget ResolvePresentationBudget(int mode,int participants,int labels,int numbers) {
    const bool reduced=mode==2 || (mode==0 && participants>=1000);
    return {reduced,std::clamp(labels,0,reduced?40:180),std::clamp(numbers,0,reduced?32:128),reduced?.5f:.2f};
}
}
