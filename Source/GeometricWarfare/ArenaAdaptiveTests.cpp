#include "ArenaHUD.h"
#include "ArenaGameMode.h"
#include "LiveInteractionTestAdapter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
struct FArenaAdaptiveTestAccess {
    static void Layout(AArenaHUD& H,const gwui::ScreenLayout& L){
        H.CancelPointer();H.ScreenLayout=L;H.ViewportWidth=L.logicalSize.X;H.ViewportHeight=L.logicalSize.Y;
        H.SettingsButtonBounds=FBox2D({L.logicalSize.X-115,16},{L.logicalSize.X-32,43});
        H.HostButtonBounds=FBox2D({L.logicalSize.X-222,16},{L.logicalSize.X-124,43});
        H.ArenaX=16;H.ArenaY=184;H.ArenaSide=L.logicalSize.X-32;
        H.MinimapBounds=FBox2D({L.logicalSize.X-160,700},{L.logicalSize.X-20,840});
        H.RankRegions.Reset();H.RankBounds=FBox2D(ForceInit);
    }
    static void HostRows(AArenaHUD& H){H.SettingRegions.Reset();for(int32 I=0;I<3;++I)H.SettingRegions.Add({FBox2D({28.+I*160,400},{172.+I*160,454}),11+I});}
    static bool HostOpen(const AArenaHUD& H){return H.bHostPanelOpen;}
    static void Rank(AArenaHUD& H,int32 Id){H.RankBounds=FBox2D({16,910},{688,1050});H.RankVisibleRows=4;H.RankTotalRows=20;H.RankOffset=0;H.RankRegions.Add({FBox2D({16,910},{688,942}),Id});}
    static int32 RankOffset(const AArenaHUD& H){return H.RankOffset;}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaScreenLayoutTest,"GeometricWarfare.Arena.ScreenLayout",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaScreenLayoutTest::RunTest(const FString&) {
    const FVector2D Sizes[]={{1920,1080},{1280,960},{1080,1920},{1200,1020},{1900,900}};
    for(int32 Device=0;Device<=2;++Device)for(int32 Aspect=0;Aspect<=5;++Aspect)for(const auto Size:Sizes){
        const auto L=gwui::ResolveScreenLayout(Size.X,Size.Y,Device,Aspect);
        const auto Extent=L.logicalSize*L.scale;
        TestTrue(TEXT("All compositions fit inside viewport"),L.offset.X>=-.01 && L.offset.Y>=-.01 && Extent.X<=Size.X+.01 && Extent.Y<=Size.Y+.01);
        TestTrue(TEXT("No anisotropic stretching"),FMath::IsNearlyEqual(Extent.X/Extent.Y,L.logicalSize.X/L.logicalSize.Y,.00001));
        TestTrue(TEXT("Preset uses exact requested ratio"),FMath::IsNearlyEqual(L.logicalSize.X/L.logicalSize.Y,static_cast<double>(gwui::LayoutAspect(Aspect,Size.X/Size.Y)),.00001));
        const FVector2D Point(L.logicalSize.X*.23,L.logicalSize.Y*.71);
        TestTrue(TEXT("Click transform round trips"),L.toLogical(L.toScreen(Point)).Equals(Point,.001));
    }
    const auto Portrait=gwui::ResolveScreenLayout(1080,1920,2,3);
    TestEqual(TEXT("Mobile portrait is composed at 720 logical pixels wide"),Portrait.logicalSize.X,720.0);
    TestTrue(TEXT("Portrait arena can occupy nearly all physical width"),(Portrait.logicalSize.X-32)*Portrait.scale>=1000);
    const auto Fixed=gwui::ResolveScreenLayout(1920,1080,2,3);
    TestTrue(TEXT("Fixed Spout portrait uses centered side margins"),Fixed.offset.X>600 && Fixed.offset.Y==0);
    TestTrue(TEXT("Portrait window auto selects mobile"),gwui::ResolveScreenLayout(1080,1920,0,0).mobile);
    TestTrue(TEXT("Invalid viewport fallback remains finite"),FMath::IsFinite(gwui::ResolveScreenLayout(0,0,0,0).scale));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaAdaptiveHostTest,"GeometricWarfare.Arena.AdaptiveHostPanel",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaAdaptiveHostTest::RunTest(const FString&) {
    auto* Instance=NewObject<UGameInstance>(GEngine);Instance->InitializeStandalone(FName(TEXT("ArenaAdaptiveTestWorld")));
    auto* World=Instance->GetWorld();if(!TestNotNull(TEXT("Adaptive fixture world"),World))return false;
    ON_SCOPE_EXIT{World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);};
    auto* Bridge=Instance->GetSubsystem<ULiveInteractionSubsystem>();FLiveInteractionTestAdapter::EnableLocalTest(*Bridge);
    FURL URL;URL.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* G=World->GetAuthGameMode<AArenaGameMode>();G->ResetArena();
    Bridge->SimulateComment(TEXT("adaptive-viewer"),TEXT("适配观众"),TEXT("1"));
    const int32 ViewerId=G->GetViewers().FindChecked(TEXT("adaptive-viewer")).BodyId;
    const int32 Count=G->GetViewers().Num(),Bodies=static_cast<int32>(G->GetMatch().fighters.size());const auto TeamCounts=G->GetMatch().teamCounts;
    auto* H=World->SpawnActor<AArenaHUD>();const auto L=gwui::ResolveScreenLayout(1920,1080,2,3);FArenaAdaptiveTestAccess::Layout(*H,L);
    const auto Click=[&](FVector2D Point){const auto P=L.toScreen(Point);H->BeginPointer(P.X,P.Y);H->EndPointer(P.X,P.Y);};
    TestFalse(TEXT("Letterbox margin cannot target battlefield"),H->BeginPointer(10,500));
    Click({L.logicalSize.X-180,28});TestTrue(TEXT("Physical portrait host button opens panel"),FArenaAdaptiveTestAccess::HostOpen(*H));
    FArenaAdaptiveTestAccess::HostRows(*H);
    Click({100,425});TestEqual(TEXT("Red host button selects red"),G->GetHostTeam(),1);const int32 HostId=G->FocusBodyId;
    TestTrue(TEXT("Host role spawned"),G->GetMatch().findFighter(HostId)->isHost);
    Click({260,425});TestEqual(TEXT("Blue host button selects blue"),G->GetHostTeam(),2);
    Click({420,425});TestEqual(TEXT("Gray host button selects gray"),G->GetHostTeam(),0);
    TestEqual(TEXT("All choices keep same host identity"),G->FocusBodyId,HostId);
    TestEqual(TEXT("Only one additional body"),static_cast<int32>(G->GetMatch().fighters.size()),Bodies+1);
    TestEqual(TEXT("Host uses no viewer slots"),G->GetViewers().Num(),Count);
    for(int32 T=0;T<3;++T)TestEqual(TEXT("Host does not change viewer team capacity"),G->GetMatch().teamCounts[T],TeamCounts[T]);
    G->Match.refreshStandings();for(int32 Index:G->Match.leaderboard)TestFalse(TEXT("Host excluded from viewer rankings"),G->Match.fighters[Index].isHost);
    TestEqual(TEXT("Host personal score stays zero"),G->Match.findFighter(HostId)->score,int64_t(0));
    const float Zoom=G->CameraZoom;const auto P=L.toScreen({300,500});H->ZoomAtCursor(P.X,P.Y,2);TestEqual(TEXT("Host modal blocks zoom"),G->CameraZoom,Zoom);
    H->ToggleSettings();TestFalse(TEXT("Esc closes host overlay"),H->IsOverlayOpen());
    G->Overview();G->FocusBodyId=-1;FArenaAdaptiveTestAccess::Rank(*H,ViewerId);Click({100,925});TestEqual(TEXT("Portrait physical rank click follows viewer"),G->FocusBodyId,ViewerId);
    H->ZoomAtCursor(L.toScreen({100,1000}).X,L.toScreen({100,1000}).Y,.5);TestEqual(TEXT("Portrait rank wheel scrolls"),FArenaAdaptiveTestAccess::RankOffset(*H),1);
    G->FocusBodyId=-1;G->Overview();G->CameraZoom=4.2f;Click({L.logicalSize.X-145,715});TestTrue(TEXT("Portrait minimap inverse pointer jumps camera"),(G->CameraCenter-FVector2D(gw::World::Size*.5)).Size()>100);
    G->Match.phase=gw::Phase::Results;G->HostAssist(1);TestEqual(TEXT("Results cannot change host team"),G->GetHostTeam(),0);
    G->Match.phase=gw::Phase::Battle;Bridge->StartPlatform(TEXT("adaptive-unavailable-provider"));TestFalse(TEXT("Disconnected platform disables host"),G->CanHostAssist());
    const int32 DisconnectedTeam=G->GetHostTeam();G->HostAssist(1);TestEqual(TEXT("Disconnected cannot create or change host"),G->GetHostTeam(),DisconnectedTeam);
    return true;
}
#endif
