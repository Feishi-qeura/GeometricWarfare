#pragma once
#include "CoreMinimal.h"
#include "LiveInteractionSubsystem.h"

class FJsonObject;

/** The SDK-host gift projection shared by production delivery and contract tests. */
struct DOUYINLIVEPROVIDER_API FDouyinGiftDecoder
{
    static bool Decode(const TSharedPtr<FJsonObject>& Message,const FLiveSession& Session,FLiveGift& Gift,FString& Rejection);
};
