#pragma once
#include "LiveInteractionSubsystem.h"
#if WITH_DEV_AUTOMATION_TESTS
/** Explicit automation opt-in, unavailable in production builds. */
class FLiveInteractionTestAdapter
{
public:
    static void EnableLocalTest(ULiveInteractionSubsystem& Host) { Host.EnableLocalTest(); }
    template<typename T> static T Stamp(ULiveInteractionSubsystem& Host,const T& Event) { T Copy=Event; Copy.Session=Host.GetCurrentSession(); return Copy; }
    static void Receive(ULiveInteractionSubsystem& Host,const FString& Json) { Host.Receive(Json); }
    static void BeginDevRelay(ULiveInteractionSubsystem& Host,const FString& Room) {
        Host.EnableLocalTest(); Host.bDevRelayMode=true;
        Host.ChangeSession(TEXT("local"),TEXT("dev-relay"),Room,true);
        Host.ConnectionStatus=TEXT("DEV_TEST_PROTOCOL");
    }
};
#endif
