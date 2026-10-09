#include "ArenaHUD.h"
#include "ArenaGameMode.h"
#include "LiveInteractionTestAdapter.h"
#include "ArenaPlayerController.h"
#include "UI/ArenaCompactPresentation.h"
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
        HUD.ArenaX=400; HUD.ArenaY=150; HUD.ArenaSide=800; HUD.ArenaWidth=800; HUD.ArenaHeight=800;
        HUD.WorldScale=static_cast<float>(800.0/(gw::World::Size/Game.CameraZoom));
        HUD.ViewOrigin=Game.CameraCenter-FVector2D(gw::World::Size*.5/Game.CameraZoom,gw::World::Size*.5/Game.CameraZoom);
        HUD.MinimapBounds=FBox2D(ForceInit);
        HUD.RankBounds=FBox2D(ForceInit);HUD.RankOffset=0;HUD.RankTotalRows=0;HUD.RankVisibleRows=20;
        HUD.RankRegions.Reset();
    }
    static void Rectangle(AArenaHUD& HUD,AArenaGameMode& Game,float Width,float Height) {
        Layout(HUD,Game);
        HUD.ViewportHeight=1440;
        HUD.ArenaWidth=Width;HUD.ArenaHeight=Height;
        HUD.SetupArenaView(&Game);
    }
    static void RefreshView(AArenaHUD& HUD,AArenaGameMode& Game) {HUD.SetupArenaView(&Game);}
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
    static FVector2D Project(const AArenaHUD& HUD,double X,double Y) {return HUD.Project(X,Y);}
    static bool DrawMinimap(AArenaHUD& HUD,AArenaGameMode& Game) {HUD.DrawMinimap(&Game);return HUD.MinimapBounds.bIsValid;}
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
    FLiveInteractionTestAdapter::EnableLocalTest(*Instance->GetSubsystem<ULiveInteractionSubsystem>());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaInitialCompactOverviewTest,"GeometricWarfare.Arena.Camera.InitialCompactOverview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaInitialCompactOverviewTest::RunTest(const FString&)
{
    FHUDInputTestWorld Fixture;auto* Game=Fixture.Game;
    if(!TestNotNull(TEXT("Compact camera fixture starts real game mode"),Game))return false;
    TestEqual(TEXT("Initial local game opens the complete map"),Game->CameraZoom,1.f);
    TestTrue(TEXT("Initial game centers the 2000-unit arena"),Game->CameraCenter.Equals({1000,1000},1e-6));
    Game->ResetArena();
    auto* Bridge=Game->GetBridge();
    Bridge->SimulateComment(TEXT("compact-first"),TEXT("首位观众"),TEXT("1"));
    Bridge->SimulateComment(TEXT("compact-first"),TEXT("首位观众"),TEXT("加入"));
    TestEqual(TEXT("First and repeat joins preserve the full-map magnification"),Game->CameraZoom,1.f);
    TestEqual(TEXT("Joining never locks the overview to a player"),Game->FocusBodyId,-1);
    TestTrue(TEXT("Joining retains the full-map center"),Game->CameraCenter.Equals({1000,1000},1e-6));
    auto* HUD=Fixture.World->SpawnActor<AArenaHUD>();
    if(!TestNotNull(TEXT("Compact camera fixture spawns real HUD"),HUD))return false;
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    TestTrue(TEXT("HUD projects the north-west wall into the full arena frame"),FArenaHUDInputAccess::Project(*HUD,0,0).Equals({400,150},1e-4));
    TestTrue(TEXT("HUD projects the south-east 2000-unit wall into the full arena frame"),FArenaHUDInputAccess::Project(*HUD,2000,2000).Equals({1200,950},1e-4));
    FArenaHUDInputAccess::Minimap(*HUD,Game->GetViewers().FindChecked(TEXT("compact-first")).BodyId);
    TestFalse(TEXT("Actual full-map HUD removes a stale minimap hit region"),FArenaHUDInputAccess::DrawMinimap(*HUD,*Game));
    Game->FocusViewer(Game->GetViewers().FindChecked(TEXT("compact-first")).BodyId);
    Game->FocusBodyId=-1;Game->Overview();
    TestTrue(TEXT("Overview restores the same entire compact arena after following"),Game->CameraZoom==1&&Game->CameraCenter.Equals({1000,1000},1e-6));
    Game->ResetArena();
    TestTrue(TEXT("Reset restores complete compact arena framing"),Game->CameraZoom==1&&Game->CameraCenter.Equals({1000,1000},1e-6)&&Game->FocusBodyId==-1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaCompactOverviewPresentationTest,"GeometricWarfare.Arena.Camera.CompactOverviewPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaCompactOverviewPresentationTest::RunTest(const FString&)
{
    // A 500px battlefield across the 2000-unit arena yields .25 physical pixels
    // per world unit: ordinary actors must retain their distinct silhouettes.
    const auto SmallOverview=gwui::ResolveCompactActorDetail(.25f,1.f,false);
    TestFalse(TEXT("Small full-map actors retain geometry instead of identical square markers"),SmallOverview.markerOnly);
    TestTrue(TEXT("Crowd names and health bars cannot cover the full-map geometry"),!SmallOverview.showLabel&&!SmallOverview.showBars);
    const auto DesktopOverview=gwui::ResolveCompactActorDetail(.4f,1.f,false);
    TestTrue(TEXT("Larger full-map views also keep ordinary names and bars quiet"),!DesktopOverview.markerOnly&&!DesktopOverview.showLabel&&!DesktopOverview.showBars);
    const auto PriorityOverview=gwui::ResolveCompactActorDetail(.25f,1.f,true);
    TestTrue(TEXT("Focused host and hero actors retain readable details in overview"),!PriorityOverview.markerOnly&&PriorityOverview.showLabel&&PriorityOverview.showBars);
    const auto Zoomed=gwui::ResolveCompactActorDetail(.4f,2.f,false);
    TestTrue(TEXT("Zooming in restores ordinary actor names and bars"),!Zoomed.markerOnly&&Zoomed.showLabel&&Zoomed.showBars);
    const auto MediumZoom=gwui::ResolveCompactActorDetail(.3f,2.f,false);
    TestTrue(TEXT("Intermediate zoom can show health without unreadable labels"),!MediumZoom.markerOnly&&!MediumZoom.showLabel&&MediumZoom.showBars);
    return true;
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

    // The overview maps the whole world to an 800px arena. Use a real body
    // location as the input, but assert its independently known viewer identity.
    const float BodyX=static_cast<float>(400+FirstBody->position.x*800/gw::World::Size);
    const float BodyY=static_cast<float>(150+FirstBody->position.y*800/gw::World::Size);
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
    TestTrue(TEXT("Locked drag cannot move the camera"),Game->CameraCenter.Equals({gw::World::Size*.5,gw::World::Size*.5},1e-6));
    Game->CameraZoom=4; Game->CameraCenter={gw::World::Size*.5,gw::World::Size*.5}; Game->FocusBodyId=SecondId;
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    HUD->BeginPointer(800,550); HUD->EndPointer(840,550);
    TestTrue(TEXT("Locked release-only drag cannot move the real camera"),Game->CameraCenter.Equals({gw::World::Size*.5,gw::World::Size*.5},1e-6));
    TestEqual(TEXT("Locked release-only drag keeps following"),Game->FocusBodyId,SecondId);
    FArenaHUDInputAccess::KeyboardPan(*Controller); FArenaHUDInputAccess::Home(*Controller);
    TestTrue(TEXT("WASD and Home cannot alter a locked view"),Game->FocusBodyId==SecondId && Game->CameraZoom==4 && Game->CameraCenter.Equals({gw::World::Size*.5,gw::World::Size*.5},1e-6));

    Game->CameraZoom=2; Game->CameraCenter={gw::World::Size*.5,gw::World::Size*.5}; Game->FocusBodyId=SecondId;
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    HUD->ZoomAtCursor(600,750,2);
    TestEqual(TEXT("HUD wheel zoom updates game zoom"),Game->CameraZoom,4.f);
    TestTrue(TEXT("Locked HUD zoom leaves the tracked center unchanged"),Game->CameraCenter.Equals({gw::World::Size*.5,gw::World::Size*.5},1e-6));
    TestEqual(TEXT("HUD wheel zoom preserves following"),Game->FocusBodyId,SecondId);

    Game->CameraZoom=4; Game->CameraCenter={gw::World::Size*.5,gw::World::Size*.5}; Game->FocusBodyId=SecondId;
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    FArenaHUDInputAccess::Minimap(*HUD,FirstId);
    HUD->ZoomAtCursor(1050,780,2);
    TestTrue(TEXT("Wheel over minimap leaves camera and following unchanged"),
        Game->CameraZoom==4 && Game->FocusBodyId==SecondId && Game->CameraCenter.Equals({gw::World::Size*.5,gw::World::Size*.5},1e-6));
    TestTrue(TEXT("Minimap wins overlapping arena and rank hit regions"),HUD->BeginPointer(1000,830));
    TestTrue(TEXT("Locked minimap press cannot reposition the real camera"),Game->CameraCenter.Equals({gw::World::Size*.5,gw::World::Size*.5},1e-6));
    TestEqual(TEXT("Minimap press preserves follow lock"),Game->FocusBodyId,SecondId);
    HUD->EndPointer(1000,830);
    TestEqual(TEXT("Locked minimap release cannot select an overlapping rank row"),Game->FocusBodyId,SecondId);
    HUD->BeginPointer(1050,780); HUD->UpdatePointer(1150,680);
    TestTrue(TEXT("Locked minimap drag keeps the tracked center"),Game->CameraCenter.Equals({gw::World::Size*.5,gw::World::Size*.5},1e-6));
    HUD->EndPointer(1170,660);
    TestEqual(TEXT("Locked minimap drag keeps the same target"),Game->FocusBodyId,SecondId);

    FArenaHUDInputAccess::RankedPlayers(*HUD,FirstId,SecondId);
    HUD->BeginPointer(1400,214);
    FArenaHUDInputAccess::RightClick(*Controller,*HUD);
    HUD->EndPointer(1400,214);
    TestEqual(TEXT("Right click unlocks and cancels a pending rank selection"),Game->FocusBodyId,-1);
    TestTrue(TEXT("Right click retains camera position and zoom"),Game->CameraZoom==4 && Game->CameraCenter.Equals({gw::World::Size*.5,gw::World::Size*.5},1e-6));
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    HUD->BeginPointer(800,550); HUD->EndPointer(840,550);
    TestTrue(TEXT("After right-click unlock, dragging moves the camera"),Game->CameraCenter.Equals({gw::World::Size*.4875,gw::World::Size*.5},1e-6));
    Game->CameraZoom=2; Game->CameraCenter={gw::World::Size*.5,gw::World::Size*.5};
    HUD->ZoomAtCursor(600,750,2);
    TestTrue(TEXT("Unlocked HUD zoom again preserves the mouse world anchor"),Game->CameraCenter.Equals({gw::World::Size*.4375,gw::World::Size*.5625},1e-6));
    FArenaHUDInputAccess::Minimap(*HUD,FirstId);
    HUD->BeginPointer(1000,830); HUD->EndPointer(1000,830);
    TestTrue(TEXT("After unlock, minimap click maps full-world coordinates"),Game->CameraCenter.Equals({gw::World::Size*.25,gw::World::Size*.75},1e-6));
    HUD->BeginPointer(1050,780); HUD->EndPointer(1170,660);
    TestTrue(TEXT("After unlock, minimap drag clamps to the world edge"),Game->CameraCenter.Equals({gw::World::Size*.875,gw::World::Size*.125},1e-6));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaHUDRectangularInputTest,"GeometricWarfare.Arena.HUDRectangularInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaHUDRectangularInputTest::RunTest(const FString&)
{
    FHUDInputTestWorld Fixture;auto* Game=Fixture.Game;
    if(!TestNotNull(TEXT("Rectangular input fixture starts the real game mode"),Game))return false;
    Game->ResetArena();auto* Bridge=Game->GetBridge();
    if(!TestNotNull(TEXT("Rectangular input has a local comment bridge"),Bridge))return false;
    TestTrue(TEXT("Rectangular input viewer joins through the real comment path"),Bridge->SimulateComment(TEXT("rectangle-pointer"),TEXT("Rectangle"),TEXT("1")));
    const auto* Viewer=Game->GetViewers().Find(TEXT("rectangle-pointer"));
    if(!TestNotNull(TEXT("Rectangular input viewer has a stable identity"),Viewer))return false;
    const int32 BodyId=Viewer->BodyId;
    auto& World=const_cast<gw::World&>(Game->GetArena());auto* Body=World.find(BodyId);
    if(!TestNotNull(TEXT("Rectangular input viewer has a physical body"),Body))return false;
    auto* HUD=Fixture.World->SpawnActor<AArenaHUD>();
    if(!TestNotNull(TEXT("Rectangular input uses the real HUD"),HUD))return false;
    const auto ResetView=[&](float Width,float Height) {
        Game->FocusBodyId=-1;Game->CameraZoom=2;Game->CameraCenter={1000,1000};
        FArenaHUDInputAccess::Rectangle(*HUD,*Game,Width,Height);
    };

    // Both viewports use 1.2 pixels per world unit. Pick beyond the shorter
    // dimension so an accidental ArenaSide hit box or normalization must fail.
    ResetView(1200,600);Body->position={1400,1000};World.rebuildSpatial();
    TestTrue(TEXT("Wide HUD projects a known world point into its far-right region"),FArenaHUDInputAccess::Project(*HUD,1400,1000).Equals({1480,450},1e-3));
    TestTrue(TEXT("Wide arena accepts a press beyond the old square right edge"),HUD->BeginPointer(1480,450));
    HUD->EndPointer(1480,450);
    TestEqual(TEXT("Far-right rectangular click selects the actual viewer"),Game->FocusBodyId,BodyId);

    ResetView(600,1200);Body->position={1000,1400};World.rebuildSpatial();
    TestTrue(TEXT("Tall HUD projects a known world point into its far-bottom region"),FArenaHUDInputAccess::Project(*HUD,1000,1400).Equals({700,1230},1e-3));
    TestTrue(TEXT("Tall arena accepts a press beyond the old square bottom edge"),HUD->BeginPointer(700,1230));
    HUD->EndPointer(700,1230);
    TestEqual(TEXT("Far-bottom rectangular click selects the actual viewer"),Game->FocusBodyId,BodyId);

    ResetView(1200,600);
    TestTrue(TEXT("Wide rectangle starts a free drag"),HUD->BeginPointer(1000,450));
    HUD->EndPointer(1060,510);
    const auto WideCenter=Game->CameraCenter;
    TestTrue(TEXT("Wide drag converts equal physical X/Y motion with one scale"),WideCenter.Equals({950,950},1e-3));
    ResetView(600,1200);
    TestTrue(TEXT("Tall rectangle starts a free drag"),HUD->BeginPointer(700,750));
    HUD->EndPointer(760,810);
    TestTrue(TEXT("Transposed rectangle preserves the same world displacement for the same pixels"),Game->CameraCenter.Equals(WideCenter,1e-3)&&Game->CameraCenter.Equals({950,950},1e-3));

    ResetView(1200,600);HUD->ZoomAtCursor(1480,600,2);
    TestTrue(TEXT("Wide cursor zoom uses independent width and height normalization"),Game->CameraZoom==4&&Game->CameraCenter.Equals({1200,1062.5},1e-3));
    FArenaHUDInputAccess::RefreshView(*HUD,*Game);
    TestTrue(TEXT("Wide cursor zoom keeps its original world anchor under the pointer"),FArenaHUDInputAccess::Project(*HUD,1400,1125).Equals({1480,600},1e-3));
    ResetView(600,1200);HUD->ZoomAtCursor(850,1230,2);
    TestTrue(TEXT("Tall cursor zoom uses independent width and height normalization"),Game->CameraZoom==4&&Game->CameraCenter.Equals({1062.5,1200},1e-3));
    FArenaHUDInputAccess::RefreshView(*HUD,*Game);
    TestTrue(TEXT("Tall cursor zoom keeps its original world anchor under the pointer"),FArenaHUDInputAccess::Project(*HUD,1125,1400).Equals({850,1230},1e-3));

    ResetView(1200,600);
    const auto WideRight=FArenaHUDInputAccess::Clip(*HUD,FBox2D({1590,300},{1630,340}));
    TestTrue(TEXT("Wide arena clips to its actual right edge beyond the square width"),WideRight.bIsValid&&WideRight.Min.Equals({1590,300})&&WideRight.Max.Equals({1600,340}));
    const auto WideBottom=FArenaHUDInputAccess::Clip(*HUD,FBox2D({800,740},{840,780}));
    TestTrue(TEXT("Wide arena clips to its shorter bottom edge"),WideBottom.bIsValid&&WideBottom.Min.Equals({800,740})&&WideBottom.Max.Equals({840,750}));
    TestFalse(TEXT("Wide arena rejects content entirely below its shorter edge"),FArenaHUDInputAccess::Clip(*HUD,FBox2D({800,760},{840,800})).bIsValid);
    ResetView(600,1200);
    const auto TallRight=FArenaHUDInputAccess::Clip(*HUD,FBox2D({990,400},{1030,440}));
    TestTrue(TEXT("Tall arena clips to its shorter right edge"),TallRight.bIsValid&&TallRight.Min.Equals({990,400})&&TallRight.Max.Equals({1000,440}));
    const auto TallBottom=FArenaHUDInputAccess::Clip(*HUD,FBox2D({600,1340},{640,1380}));
    TestTrue(TEXT("Tall arena clips to its actual bottom edge beyond the square height"),TallBottom.bIsValid&&TallBottom.Min.Equals({600,1340})&&TallBottom.Max.Equals({640,1350}));
    TestFalse(TEXT("Tall arena rejects content entirely beyond its shorter right edge"),FArenaHUDInputAccess::Clip(*HUD,FBox2D({1010,800},{1050,840})).bIsValid);
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
    Boss.laserFrom={gw::World::Size*.5,gw::World::Size*.5};Boss.laserTo={gw::World::Size,gw::World::Size*.5};
    Game->CameraZoom=8;Game->CameraCenter={gw::World::Size*.75,gw::World::Size*.5};Game->RunningTime=.31f;
    FArenaHUDInputAccess::Layout(*HUD,*Game);
    const auto Origin=FArenaHUDInputAccess::Origin(*HUD);
    const auto Near=FArenaHUDInputAccess::Shake(*HUD,*Game);
    TestTrue(TEXT("Laser crossing the view shakes even when both endpoints and boss are outside"),Near.SizeSquared()>1e-6&&Near.Size()<4.3);
    Game->RunningTime+=.017f;
    TestFalse(TEXT("Shake varies across display frames"),FArenaHUDInputAccess::Shake(*HUD,*Game).Equals(Near,1e-6));
    TestTrue(TEXT("Shake never changes logical camera or minimap origin"),Game->CameraCenter.Equals({gw::World::Size*.75,gw::World::Size*.5},1e-6)&&FArenaHUDInputAccess::Origin(*HUD).Equals(Origin,1e-6));
    Game->CameraCenter={gw::World::Size*.75,gw::World::Size*.125};FArenaHUDInputAccess::Layout(*HUD,*Game);
    TestTrue(TEXT("A distant beam leaves the camera still"),FArenaHUDInputAccess::Shake(*HUD,*Game).IsNearlyZero());
    Game->CameraCenter={gw::World::Size*.5,gw::World::Size*.5};FArenaHUDInputAccess::Layout(*HUD,*Game);Boss.attack=gw::BossAttack::LaserWindup;
    TestTrue(TEXT("Tracking warning does not apply active laser shake"),FArenaHUDInputAccess::Shake(*HUD,*Game).IsNearlyZero());
    Boss.active=false;Boss.explosions[0]={{gw::World::Size*.5,gw::World::Size*.5},0,.45,140,true};
    TestTrue(TEXT("Nearby impact shakes independently of whether boss remains alive"),FArenaHUDInputAccess::Shake(*HUD,*Game).SizeSquared()>1e-6);
    Boss.explosions[0].age=.45;
    TestTrue(TEXT("Impact shake fades completely at its lifetime"),FArenaHUDInputAccess::Shake(*HUD,*Game).IsNearlyZero());
    Boss.explosions[0].age=0;Game->TogglePaused();
    TestTrue(TEXT("Pausing suppresses impact shake"),FArenaHUDInputAccess::Shake(*HUD,*Game).IsNearlyZero());
    return true;
}
#endif
