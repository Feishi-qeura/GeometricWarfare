#pragma once
#include "LiveInteractionSubsystem.h"
#include "LiveInteractionTestConsumer.generated.h"

/** Internal regression fixture. No instance is created by runtime gameplay. */
UCLASS(Transient)
class ULiveInteractionTestConsumer : public UObject
{
    GENERATED_BODY()
public:
    int32 Comments=0,Likes=0,Shares=0,Gifts=0,Follows=0,PresenceEvents=0,TeamSelections=0;
    UFUNCTION() void ConsumeComment(const FLiveComment& Event) { ++Comments; }
    UFUNCTION() void ConsumeLike(const FLiveLike& Event) { ++Likes; }
    UFUNCTION() void ConsumeShare(const FLiveShare& Event) { ++Shares; }
    UFUNCTION() void ConsumeGift(const FLiveGift& Event) { ++Gifts; }
    UFUNCTION() void ConsumeFollow(const FLiveFollow& Event) { ++Follows; }
    UFUNCTION() void ConsumePresence(const FLivePresence& Event) { ++PresenceEvents; }
    UFUNCTION() void ConsumeTeamSelection(const FLiveTeamSelection& Event) { ++TeamSelections; }
    void Bind(ULiveInteractionSubsystem& Host) {
        Host.OnComment.AddDynamic(this,&ULiveInteractionTestConsumer::ConsumeComment);
        Host.OnLike.AddDynamic(this,&ULiveInteractionTestConsumer::ConsumeLike);
        Host.OnShare.AddDynamic(this,&ULiveInteractionTestConsumer::ConsumeShare);
        Host.OnGift.AddDynamic(this,&ULiveInteractionTestConsumer::ConsumeGift);
        Host.OnFollow.AddDynamic(this,&ULiveInteractionTestConsumer::ConsumeFollow);
        Host.OnPresence.AddDynamic(this,&ULiveInteractionTestConsumer::ConsumePresence);
        Host.OnTeamSelection.AddDynamic(this,&ULiveInteractionTestConsumer::ConsumeTeamSelection);
    }
};
