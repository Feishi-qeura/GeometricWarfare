#include "ArenaHUD.h"
#include "ArenaGameMode.h"
#include "ArenaPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
// Layout fixtures belong to the test. Production exposes no testing methods.
struct FArenaHUDInputAccess {
    static void FreeOverview(AArenaGameMode& Game) { Game.FocusBodyId=-1; Game.Overview(); }
    static void RightClick(AArenaPlayerController& Controller,AArenaHUD& HUD) {
        Controller.PointerHUD=&HUD; Controller.bPointerActive=true;
        Controller.UnlockCamera();
    }
    static void KeyboardPan(AArenaPlayerController& Controller) { Controller.CameraLeft(); }
    static void Home(AArenaPlayerController& Controller) { Controller.ShowOverview(); }
    static void Layout(AArenaHUD& HUD,const AArenaGameMode& Game) {
        HUD.CancelPointer();
        HUD.ViewportWidth=1920; HUD.ViewportHeight=1080;
        HUD.ArenaX=400; HUD.ArenaY=150; HUD.ArenaSide=800;
        HUD.WorldScale=static_cast<float>(800.0/(8000.0/Game.CameraZoom));
        HUD.ViewOrigin=Game.CameraCenter-FVector2D(4000.0/Game.CameraZoom,4000.0/Game.CameraZoom);
        HUD.MinimapBounds=FBox2D(ForceInit);
        HUD.RankBounds=FBox2D(ForceInit);HUD.RankOffset=0;HUD.RankTotalRows=0;HUD.RankVisibleRows=20;
        HUD.RankRegions.Reset();
    }
    static void RankedPlayers(AArenaHUD& HUD,int32 First,int32 Second) {
        HUD.RankRegions.Reset();
        HUD.RankRegions.Add({FBox2D({1320,200},{1560,232}),First});
        HUD.RankRegions.Add({FBox2D({1320,240},{1560,272}),Second});
    }
    static void Minimap(AArenaHUD& HUD,int32 OverlappingRankId) {
        HUD.MinimapBounds=FBox2D({950,680},{1150,880});
        HUD.RankRegions.Add({HUD.MinimapBounds,OverlappingRankId});
    }
    static void InvalidMinimap(AArenaHUD& HUD) {
        // Invalid bounds can retain coordinates; the validity flag must win.
        HUD.MinimapBounds=FBox2D({0,0},{1920,1080});
        HUD.MinimapBounds.bIsValid=false;
    }
    static FVector2D Shake(AArenaHUD& HUD,AArenaGameMode& Game) {
        HUD.UpdateArenaShake(&Game);return HUD.ArenaShakeOffset;
    }
    static FVector2D Origin(const AArenaHUD& HUD) { return HUD.ViewOrigin; }
    static FBox2D Clip(const AArenaHUD& HUD,const FBox2D& Bounds) {return HUD.ClipArenaRect(Bounds);}
    static void ScrollableRanks(AArenaHUD& HUD) {
        HUD.RankBounds=FBox2D({1320,198},{1560,330});HUD.RankVisibleRows=5;HUD.RankTotalRows=20;HUD.RankOffset=0;
    }
    static int32 RankOffset(const AArenaHUD& HUD) {return HUD.RankOffset;}
};

namespace {
struct FHUDInputTestWorld {
    UGameInstance* Instance=nullptr;
    UWorld* World=nullptr;
    AArenaGameMode* Game=nullptr;
    FHUDInputTestWorld() {
        Instance=NewObject<UGameInstance>(GEngine);
        Instance->InitializeStandalone(FName(TEXT("ArenaHUDInputTestWorld")));
        World=Instance->GetWorld();
        if(!World) return;
        FURL URL;
        URL.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        Game=World->GetAuthGameMode<AArenaGameMode>();
    }
    ~FHUDInputTestWorld() {
        if(World) { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); }
        if(Instance) Instance->Shutdown();
        if(World) GEngine->DestroyWorldContext(World);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaHUDInputTest,"GeometricWarfare.Arena.HUDInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaHUDInputTest::RunTest(const FString&)
{
    FHUDInputTestWorld Fixture;
    auto* Game=Fixture.Game;
    if(!TestNotNull(TEXT("HUD interaction game mode started"),Game)) return false;
    Game->ResetArena();
    auto* Bridge=Game->GetBridge();
    if(!TestNotNull(TEXT("Local comment bridge exists"),Bridge)) return false;
    TestTrue(TEXT("First player joins through real comment delivery"),Bridge->SimulateComment(TEXT("pointer-first"),TEXT("First"),TEXT("1")));
    TestTrue(TEXT("Second player joins through real comment delivery"),Bridge->SimulateComment(TEXT("pointer-second"),TEXT("Second"),TEXT("2")));
    const auto* FirstViewer=Game->GetViewers().Find(TEXT("pointer-first"));
    const auto* SecondViewer=Game->GetViewers().Find(TEXT("pointer-second"));
    if(!TestNotNull(TEXT("First viewer identity exists"),FirstViewer)
        || !TestNotNull(TEXT("Second viewer identity exists"),SecondViewer)) return false;
    const int32 FirstId=FirstViewer->BodyId,SecondId=SecondViewer->BodyId;
    const auto* FirstBody=Game->GetArena().find(FirstId);
    if(!TestNotNull(TEXT("First viewer has a physical body"),FirstBody)) return false;
    auto* HUD=Fixture.World->SpawnActor<AArenaHUD>();
    if(!TestNotNull(TEXT("Real project HUD spawned"),HUD)) return false;
    auto* Controller=Fixture.World->SpawnActor<AArenaPlayerController>();
    if(!TestNotNull(TEXT("Real project pointer controller spawned"),Controller)) return false;

    // The overview maps 8000 world units to an 800px arena. Use a real body
    // location as the input, but assert its independently known viewer identity.
    const float BodyX=static_cast<float>(400+FirstBody->position.x*.1);
    const float BodyY=static_cast<float>(150+FirstBody->position.y*.1);
    FArenaHUDInputAccess::FreeOverview(*Game);
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    FArenaHUDInputAccess::InvalidMinimap(*HUD);
    TestTrue(TEXT("Invalid minimap bounds leave arena press available"),HUD->BeginPointer(BodyX,BodyY));
    TestEqual(TEXT("Arena press alone does not select"),Game->FocusBodyId,-1);
    HUD->EndPointer(BodyX,BodyY);
    TestEqual(TEXT("Arena click selects the nearest real active player"),Game->FocusBodyId,FirstId);

    FArenaHUDInputAccess::FreeOverview(*Game);
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    FArenaHUDInputAccess::RankedPlayers(*HUD,FirstId,SecondId);
    TestTrue(TEXT("Leaderboard press begins a gesture"),HUD->BeginPointer(1400,214));
    TestEqual(TEXT("Leaderboard press waits for release"),Game->FocusBodyId,-1);
    // Another player's score changes between press and release.
    FArenaHUDInputAccess::RankedPlayers(*HUD,SecondId,FirstId);
    HUD->EndPointer(1400,214);
    TestEqual(TEXT("Reordered row selects the ID captured at press"),Game->FocusBodyId,FirstId);
    TestTrue(TEXT("Following a rank row immediately centers its player"),
        Game->CameraCenter.Equals(FVector2D(FirstBody->position.x,FirstBody->position.y),1e-6));
    HUD->BeginPointer(1400,214); HUD->EndPointer(1400,214);
    TestEqual(TEXT("Clicking another rank row can change an existing follow target"),Game->FocusBodyId,SecondId);

    FArenaHUDInputAccess::FreeOverview(*Game);
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    FArenaHUDInputAccess::RankedPlayers(*HUD,FirstId,SecondId);
    HUD->BeginPointer(1400,214); HUD->UpdatePointer(1412,214); HUD->EndPointer(1400,214);
    TestEqual(TEXT("Dragging a rank row and returning to it cannot click"),Game->FocusBodyId,-1);
    HUD->BeginPointer(1559,214); HUD->EndPointer(1562,214);
    TestEqual(TEXT("Release outside the original row cancels even below drag threshold"),Game->FocusBodyId,-1);

    FArenaHUDInputAccess::FreeOverview(*Game);
    Game->FocusBodyId=SecondId;
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    HUD->BeginPointer(BodyX,BodyY);
    HUD->UpdatePointer(BodyX+20,BodyY);
    HUD->EndPointer(BodyX,BodyY);
    TestEqual(TEXT("Locked arena drag preserves the original follow target"),Game->FocusBodyId,SecondId);
    TestTrue(TEXT("Locked drag cannot move the camera"),Game->CameraCenter.Equals({4000,4000},1e-6));
    Game->CameraZoom=4; Game->CameraCenter={4000,4000}; Game->FocusBodyId=SecondId;
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    HUD->BeginPointer(800,550); HUD->EndPointer(840,550);
    TestTrue(TEXT("Locked release-only drag cannot move the real camera"),Game->CameraCenter.Equals({4000,4000},1e-6));
    TestEqual(TEXT("Locked release-only drag keeps following"),Game->FocusBodyId,SecondId);
    FArenaHUDInputAccess::KeyboardPan(*Controller); FArenaHUDInputAccess::Home(*Controller);
    TestTrue(TEXT("WASD and Home cannot alter a locked view"),Game->FocusBodyId==SecondId && Game->CameraZoom==4 && Game->CameraCenter.Equals({4000,4000},1e-6));

    Game->CameraZoom=2; Game->CameraCenter={4000,4000}; Game->FocusBodyId=SecondId;
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    HUD->ZoomAtCursor(600,750,2);
    TestEqual(TEXT("HUD wheel zoom updates game zoom"),Game->CameraZoom,4.f);
    TestTrue(TEXT("Locked HUD zoom leaves the tracked center unchanged"),Game->CameraCenter.Equals({4000,4000},1e-6));
    TestEqual(TEXT("HUD wheel zoom preserves following"),Game->FocusBodyId,SecondId);

    Game->CameraZoom=4; Game->CameraCenter={4000,4000}; Game->FocusBodyId=SecondId;
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    FArenaHUDInputAccess::Minimap(*HUD,FirstId);
    HUD->ZoomAtCursor(1050,780,2);
    TestTrue(TEXT("Wheel over minimap leaves camera and following unchanged"),
        Game->CameraZoom==4 && Game->FocusBodyId==SecondId && Game->CameraCenter.Equals({4000,4000},1e-6));
    TestTrue(TEXT("Minimap wins overlapping arena and rank hit regions"),HUD->BeginPointer(1000,830));
    TestTrue(TEXT("Locked minimap press cannot reposition the real camera"),Game->CameraCenter.Equals({4000,4000},1e-6));
    TestEqual(TEXT("Minimap press preserves follow lock"),Game->FocusBodyId,SecondId);
    HUD->EndPointer(1000,830);
    TestEqual(TEXT("Locked minimap release cannot select an overlapping rank row"),Game->FocusBodyId,SecondId);
    HUD->BeginPointer(1050,780); HUD->UpdatePointer(1150,680);
    TestTrue(TEXT("Locked minimap drag keeps the tracked center"),Game->CameraCenter.Equals({4000,4000},1e-6));
    HUD->EndPointer(1170,660);
    TestEqual(TEXT("Locked minimap drag keeps the same target"),Game->FocusBodyId,SecondId);

    FArenaHUDInputAccess::RankedPlayers(*HUD,FirstId,SecondId);
    HUD->BeginPointer(1400,214);
    FArenaHUDInputAccess::RightClick(*Controller,*HUD);
    HUD->EndPointer(1400,214);
    TestEqual(TEXT("Right click unlocks and cancels a pending rank selection"),Game->FocusBodyId,-1);
    TestTrue(TEXT("Right click retains camera position and zoom"),Game->CameraZoom==4 && Game->CameraCenter.Equals({4000,4000},1e-6));
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    HUD->BeginPointer(800,550); HUD->EndPointer(840,550);
    TestTrue(TEXT("After right-click unlock, dragging moves the camera"),Game->CameraCenter.Equals({3900,4000},1e-6));
    Game->CameraZoom=2; Game->CameraCenter={4000,4000};
    HUD->ZoomAtCursor(600,750,2);
    TestTrue(TEXT("Unlocked HUD zoom again preserves the mouse world anchor"),Game->CameraCenter.Equals({3500,4500},1e-6));
    FArenaHUDInputAccess::Minimap(*HUD,FirstId);
    HUD->BeginPointer(1000,830); HUD->EndPointer(1000,830);
    TestTrue(TEXT("After unlock, minimap click maps full-world coordinates"),Game->CameraCenter.Equals({2000,6000},1e-6));
    HUD->BeginPointer(1050,780); HUD->EndPointer(1170,660);
    TestTrue(TEXT("After unlock, minimap drag clamps to the world edge"),Game->CameraCenter.Equals({7000,1000},1e-6));

    Game->FocusViewer(SecondId);
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    FArenaHUDInputAccess::RankedPlayers(*HUD,FirstId,SecondId);
    HUD->BeginPointer(1400,214); HUD->CancelPointer(); HUD->EndPointer(1400,214);
    TestEqual(TEXT("Explicit cancellation never selects the pending player"),Game->FocusBodyId,SecondId);
    HUD->BeginPointer(1400,214); HUD->UpdatePointer(1921,214); HUD->EndPointer(1400,214);
    TestEqual(TEXT("Leaving viewport cancels a gesture before returning for release"),Game->FocusBodyId,SecondId);

    FArenaHUDInputAccess::FreeOverview(*Game);FArenaHUDInputAccess::Layout(*HUD,*Game);
    FArenaHUDInputAccess::RankedPlayers(*HUD,FirstId,SecondId);FArenaHUDInputAccess::ScrollableRanks(*HUD);
    HUD->BeginPointer(1400,214);HUD->ZoomAtCursor(1400,214,.8f);HUD->EndPointer(1400,214);
    TestEqual(TEXT("Wheel in compact leaderboard reveals the next rank"),FArenaHUDInputAccess::RankOffset(*HUD),1);
    TestEqual(TEXT("Scrolling cancels the previously pressed row"),Game->FocusBodyId,-1);
    TestEqual(TEXT("Leaderboard wheel does not zoom the arena"),Game->CameraZoom,1.f);
    for(int32 i=0;i<30;++i)HUD->ZoomAtCursor(1400,214,.8f);
    TestEqual(TEXT("Compact leaderboard can reach rank twenty without scrolling past it"),FArenaHUDInputAccess::RankOffset(*HUD),15);
    for(int32 i=0;i<30;++i)HUD->ZoomAtCursor(1400,214,1.2f);
    TestEqual(TEXT("Leaderboard scroll clamps back to first rank"),FArenaHUDInputAccess::RankOffset(*HUD),0);

    const auto Left=FArenaHUDInputAccess::Clip(*HUD,FBox2D({380,300},{410,340}));
    TestTrue(TEXT("A body centered outside the left edge retains its visible portion"),Left.bIsValid&&Left.Min.Equals({400,300})&&Left.Max.Equals({410,340}));
    const auto TopRight=FArenaHUDInputAccess::Clip(*HUD,FBox2D({1190,130},{1230,180}));
    TestTrue(TEXT("Corner portrait clipping preserves only the true rectangle intersection"),TopRight.bIsValid&&TopRight.Min.Equals({1190,150})&&TopRight.Max.Equals({1200,180}));
    const auto Bottom=FArenaHUDInputAccess::Clip(*HUD,FBox2D({700,940},{740,980}));
    TestTrue(TEXT("A body at the bottom wall remains partially visible"),Bottom.bIsValid&&Bottom.Min.Equals({700,940})&&Bottom.Max.Equals({740,950}));
    TestFalse(TEXT("An entirely offscreen body has no visible intersection"),FArenaHUDInputAccess::Clip(*HUD,FBox2D({350,300},{390,340})).bIsValid);
    TestFalse(TEXT("A zero-area boundary touch is not drawable"),FArenaHUDInputAccess::Clip(*HUD,FBox2D({350,300},{400,340})).bIsValid);
    TestFalse(TEXT("Invalid rectangles never become valid clipping regions"),FArenaHUDInputAccess::Clip(*HUD,FBox2D(ForceInit)).bIsValid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaHUDCombatShakeTest,"GeometricWarfare.Arena.CombatShake",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaHUDCombatShakeTest::RunTest(const FString&)
{
    FHUDInputTestWorld Fixture;auto* Game=Fixture.Game;
    if(!TestNotNull(TEXT("Combat camera game mode started"),Game))return false;
    Game->ResetArena();auto* HUD=Fixture.World->SpawnActor<AArenaHUD>();
    if(!TestNotNull(TEXT("Combat camera HUD spawned"),HUD))return false;
    auto& Match=const_cast<gw::Match&>(Game->GetMatch());auto& Boss=Match.boss;
    Boss.active=true;Boss.attack=gw::BossAttack::LaserActive;Boss.attackElapsed=1;Boss.attackRemaining=2;
    Boss.laserFrom={4000,4000};Boss.laserTo={8000,4000};
    Game->CameraZoom=8;Game->CameraCenter={6000,4000};Game->RunningTime=.31f;
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    const auto Origin=FArenaHUDInputAccess::Origin(*HUD);
    const auto Near=FArenaHUDInputAccess::Shake(*HUD,*Game);
    TestTrue(TEXT("Laser crossing the view shakes even when both endpoints and boss are outside"),Near.SizeSquared()>1e-6&&Near.Size()<4.3);
    Game->RunningTime+=.017f;
    TestFalse(TEXT("Shake varies across display frames"),FArenaHUDInputAccess::Shake(*HUD,*Game).Equals(Near,1e-6));
    TestTrue(TEXT("Shake never changes logical camera or minimap origin"),Game->CameraCenter.Equals({6000,4000},1e-6)&&FArenaHUDInputAccess::Origin(*HUD).Equals(Origin,1e-6));
    Game->CameraCenter={6000,1000};FArenaHUDInputAccess::Layout(*HUD,*Game);
    TestTrue(TEXT("A distant beam leaves the camera still"),FArenaHUDInputAccess::Shake(*HUD,*Game).IsNearlyZero());
    Game->CameraCenter={4000,4000};FArenaHUDInputAccess::Layout(*HUD,*Game);Boss.attack=gw::BossAttack::LaserWindup;
    TestTrue(TEXT("Tracking warning does not apply active laser shake"),FArenaHUDInputAccess::Shake(*HUD,*Game).IsNearlyZero());
    Boss.active=false;Boss.explosions[0]={{4000,4000},0,.45,140,true};
    TestTrue(TEXT("Nearby impact shakes independently of whether boss remains alive"),FArenaHUDInputAccess::Shake(*HUD,*Game).SizeSquared()>1e-6);
    Boss.explosions[0].age=.45;
    TestTrue(TEXT("Impact shake fades completely at its lifetime"),FArenaHUDInputAccess::Shake(*HUD,*Game).IsNearlyZero());
    Boss.explosions[0].age=0;Game->TogglePaused();
    TestTrue(TEXT("Pausing suppresses impact shake"),FArenaHUDInputAccess::Shake(*HUD,*Game).IsNearlyZero());
    return true;
}
#endif
