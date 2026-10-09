#pragma once
#include "CoreMinimal.h"

namespace liveinteraction {
// Names are normalized by the trusted platform adapter; quantities are event
// deltas, never a cumulative combo counter.
inline bool IsSupportedGift(const FString& Name) {
    return Name==TEXT("仙女棒") || Name==TEXT("能力药丸") || Name==TEXT("魔法镜") ||
        Name==TEXT("甜甜圈") || Name==TEXT("能量电池");
}
inline bool IsValidGiftCount(double Count) {
    return FMath::IsFinite(Count) && Count>=1 && Count<=100 && FMath::FloorToDouble(Count)==Count;
}
}
