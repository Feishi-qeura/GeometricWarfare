#pragma once
#include <cmath>

namespace liveinteraction {
constexpr int MaxLikeDelta=100;
inline bool IsValidLikeDelta(double Count)
{
    return std::isfinite(Count) && Count>=1.0 && Count<=MaxLikeDelta && std::floor(Count)==Count;
}
}
