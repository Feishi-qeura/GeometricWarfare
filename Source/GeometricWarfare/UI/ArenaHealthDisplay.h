#pragma once
#include <cmath>
#include <cstdio>
#include <string>

namespace gwui {
// Do not narrow HP to an integer: official gift batches can exceed int32 HP.
inline std::string HealthDisplay(double value) {
    if(!std::isfinite(value))return "-";
    value=std::ceil(value>0?value:0);
    char text[64];
    std::snprintf(text,sizeof(text),value<=1e9?"%.0f":"%.3g",value);
    return text;
}
}
