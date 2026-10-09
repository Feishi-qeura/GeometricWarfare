#include "LiveSpoutViewportClient.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiveSpoutLaunchPolicyTest, "GeometricWarfare.LiveSpout.LaunchPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLiveSpoutLaunchPolicyTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Production local launch enables output"), ULiveSpoutViewportClient::IsLaunchEligible(TEXT("-token=opaque")));
    TestFalse(TEXT("Cloud space flag retains platform viewport"), ULiveSpoutViewportClient::IsLaunchEligible(TEXT("-cloud-game 1 -Spout -screen-width 1080 -screen-height 1920")));
    TestFalse(TEXT("Cloud equals flag retains platform viewport"), ULiveSpoutViewportClient::IsLaunchEligible(TEXT("-cloud-game=1 -Spout")));
    TestFalse(TEXT("NullRHI skips output"), ULiveSpoutViewportClient::IsLaunchEligible(TEXT("-nullrhi -Spout")));
    TestFalse(TEXT("Explicit disable wins"), ULiveSpoutViewportClient::IsLaunchEligible(TEXT("-NoSpout -Spout")));
    TestFalse(TEXT("Local testing needs explicit Spout"), ULiveSpoutViewportClient::IsLaunchEligible(TEXT("-GWLocalTest")));
    TestTrue(TEXT("Explicit local Spout testing enables output"), ULiveSpoutViewportClient::IsLaunchEligible(TEXT("-GWLocalTest -Spout")));
    TestTrue(TEXT("Platform token overrides test mode"), ULiveSpoutViewportClient::IsLaunchEligible(TEXT("-GWLocalTest -token=")));
    TestTrue(TEXT("Inherited credential launch overrides test mode"), ULiveSpoutViewportClient::IsLaunchEligible(TEXT("-GWLocalTest -GWCredentialStdin")));
    const auto Portrait = ULiveSpoutViewportClient::ParseCloudViewportLaunch(TEXT("-cloud-game 1 -screen-width 1080 -screen-height=1920 -screen-fullscreen 1"));
    TestTrue(TEXT("Cloud space launch recognized"), Portrait.bCloud);
    TestEqual(TEXT("Platform portrait width preserved"), Portrait.Width, 1080);
    TestEqual(TEXT("Platform portrait height preserved"), Portrait.Height, 1920);
    TestEqual(TEXT("Platform fullscreen preserved"), Portrait.Fullscreen, 1);
    const auto Invalid = ULiveSpoutViewportClient::ParseCloudViewportLaunch(TEXT("-cloud-game=1 -screen-width 999999999999999999999 -screen-height -1 -screen-fullscreen=7"));
    TestEqual(TEXT("Overflowing width rejected"), Invalid.Width, 0);
    TestEqual(TEXT("Negative height rejected"), Invalid.Height, 0);
    TestEqual(TEXT("Invalid fullscreen rejected"), Invalid.Fullscreen, -1);
    const auto Local = ULiveSpoutViewportClient::ParseCloudViewportLaunch(TEXT("-screen-width=1080 -screen-height=1920"));
    TestEqual(TEXT("Platform screen flags inert outside cloud"), Local.Width, 0);
    return true;
}
#endif
