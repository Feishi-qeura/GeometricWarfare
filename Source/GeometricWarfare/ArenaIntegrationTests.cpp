#include "ArenaGameMode.h"
#include "LiveInteractionTestAdapter.h"
#include "ArenaPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaViewerTest,"GeometricWarfare.Arena.ViewerLifecycle",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaViewerTest::RunTest(const FString&)
{
    auto* Instance=NewObject<UGameInstance>(GEngine);
    Instance->InitializeStandalone(FName(TEXT("ArenaTestWorld")));
    FLiveInteractionTestAdapter::EnableLocalTest(*Instance->GetSubsystem<ULiveInteractionSubsystem>());
    UWorld* World=Instance->GetWorld();
    FURL Url; Url.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));
    World->SetGameMode(Url); World->InitializeActorsForPlay(Url); World->BeginPlay();
    auto* Game=World->GetAuthGameMode<AArenaGameMode>();
    if(!TestNotNull(TEXT("Project game mode started"),Game)) { World->DestroyWorld(false); Instance->Shutdown(); GEngine->DestroyWorldContext(World); return false; }
    auto* Bridge=Game->GetBridge();
    Game->ResetArena();
    Bridge->SimulateComment(TEXT("viewer-1"),TEXT("测试观众"),TEXT("加入"));
    Bridge->SimulateComment(TEXT("viewer-1"),TEXT("测试观众"),TEXT("加入"));
    TestEqual(TEXT("Repeat join creates one actor identity"),Game->GetViewers().Num(),1);
    Bridge->SimulateComment(TEXT("viewer-1"),TEXT("测试观众"),TEXT("3"));
    TestEqual(TEXT("Neutral cannot select a shape"),Game->GetViewers().FindChecked(TEXT("viewer-1")).Team,0);
    const auto NeutralShape=Game->GetArena().bodies[0].shape;
    Bridge->SimulateComment(TEXT("viewer-1"),TEXT("测试观众"),TEXT("y"));
    TestTrue(TEXT("Letter aliases do not bypass neutral team selection"),Game->GetArena().bodies[0].shape==NeutralShape);
    Bridge->SimulateComment(TEXT("viewer-1"),TEXT("测试观众"),TEXT("1"));
    TestEqual(TEXT("First numeric command selects team"),Game->GetViewers().FindChecked(TEXT("viewer-1")).Team,1);
    const bool WasRectangle=Game->GetArena().bodies[0].shape==gw::Shape::Rectangle;
    const auto NewShape=WasRectangle?gw::Shape::Triangle:gw::Shape::Rectangle;
    Bridge->SimulateComment(TEXT("viewer-1"),TEXT("测试观众"),WasRectangle?TEXT("4"):TEXT("3"));
    TestTrue(TEXT("Team member can select a different shape"),Game->GetArena().bodies[0].shape==NewShape);
    Bridge->SimulateComment(TEXT("viewer-1"),TEXT("测试观众"),WasRectangle?TEXT("3"):TEXT("4"));
    TestTrue(TEXT("Shape cooldown is enforced"),Game->GetArena().bodies[0].shape==NewShape);
    const TCHAR* AliasInputs[]={TEXT("y"),TEXT("z"),TEXT("c"),TEXT("s"),TEXT("Y"),TEXT("Z"),TEXT("C"),TEXT("S")};
    for(int32 I=0;I<8;++I){
        // Tick caps a frame at .25s; advance real simulation frames until the
        // two-second shape cooldown has elapsed, rather than one oversized tick.
        for(int32 Frame=0;Frame<9;++Frame)Game->Tick(.25f);
        Bridge->SimulateComment(TEXT("viewer-1"),TEXT("测试观众"),AliasInputs[I]);
        TestTrue(FString::Printf(TEXT("Literal shape alias %s maps to expected shape"),AliasInputs[I]),Game->GetArena().bodies[0].shape==static_cast<gw::Shape>(I%4));
        TestEqual(TEXT("Shape aliases retain original team membership"),Game->GetViewers().FindChecked(TEXT("viewer-1")).Team,1);
    }
    const int32 FocusId=Game->GetViewers().FindChecked(TEXT("viewer-1")).BodyId;
    const auto* FocusBody=Game->GetArena().find(FocusId);
    Game->CameraCenter=FVector2D::ZeroVector;
    Game->FocusViewer(FocusId);
    TestEqual(TEXT("Focus selects the requested stable player id"),Game->FocusBodyId,FocusId);
    TestTrue(TEXT("Focus moves camera immediately without waiting for Tick"),FocusBody&&Game->CameraCenter.Equals(FVector2D(FocusBody->position.x,FocusBody->position.y),.000001));
    Bridge->SimulateComment(TEXT("direct-red"),TEXT("直接红方"),TEXT("1"));
    Bridge->SimulateComment(TEXT("direct-blue"),TEXT("直接蓝方"),TEXT("2"));
    const auto* DirectRed=Game->GetViewers().Find(TEXT("direct-red"));
    const auto* DirectBlue=Game->GetViewers().Find(TEXT("direct-blue"));
    TestTrue(TEXT("Unregistered 1 directly joins red"),DirectRed&&DirectRed->Team==1);
    TestTrue(TEXT("Unregistered 2 directly joins blue"),DirectBlue&&DirectBlue->Team==2);
    auto* Controller=World->SpawnActor<AArenaPlayerController>();
    Controller->GWSpeed(4);
    FLiveInteractionTestAdapter::BeginDevRelay(*Bridge,TEXT("room-one"));
    TestEqual(TEXT("Connecting relay returns to realtime speed"),Game->DemoSpeed,1.f);
    FLiveComment Comment; Comment.MessageId=TEXT("r1-1"); Comment.UserId=TEXT("room-one-viewer"); Comment.Content=TEXT("加入"); Comment.Session=Bridge->GetCurrentSession();
    Bridge->DeliverComment(Comment);
    TestEqual(TEXT("First room viewer delivered"),Game->GetViewers().Num(),1);
    Controller->GWConnectRelay(TEXT("invalid"),TEXT("room-two"));
    TestEqual(TEXT("Invalid connection preserves existing viewers"),Game->GetViewers().Num(),1);
    FLiveInteractionTestAdapter::BeginDevRelay(*Bridge,TEXT("room-two"));
    TestEqual(TEXT("Changing room clears previous viewers"),Game->GetViewers().Num(),0);
    Controller->GWDisconnect();
    Game->ResetArena();
    const int32 GrayCapacity=gw::Match::TeamCapacity(0),RedCapacity=gw::Match::TeamCapacity(1),BlueCapacity=gw::Match::TeamCapacity(2),ViewerCapacity=gw::Match::ViewerCapacity;
    for(int32 i=0;i<GrayCapacity;++i) Bridge->SimulateComment(FString::Printf(TEXT("gray-cap-%d"),i),TEXT("灰色观众"),TEXT("加入"));
    const int32 LastGrayBodyId=Game->GetViewers().FindChecked(FString::Printf(TEXT("gray-cap-%d"),GrayCapacity-1)).BodyId;
    TestEqual(TEXT("Normal joining fills every configured gray slot"),Game->GetMatch().teamCounts[0],GrayCapacity);
    TestEqual(TEXT("Gray viewers retain unique identities"),Game->GetViewers().Num(),GrayCapacity);
    Bridge->SimulateComment(TEXT("gray-overflow"),TEXT("候补观众"),TEXT("加入"));
    TestFalse(TEXT("Gray overflow has no viewer identity"),Game->GetViewers().Contains(TEXT("gray-overflow")));
    TestEqual(TEXT("Gray overflow creates no body"),static_cast<int32>(Game->GetArena().bodies.size()),GrayCapacity);
    TestTrue(TEXT("Gray-full feedback explains direct team commands"),Game->LastEvent.Contains(LexToString(GrayCapacity))&&Game->LastEvent.Contains(TEXT("1 / 2")));
    Bridge->SimulateComment(TEXT("gray-overflow"),TEXT("候补观众"),TEXT("1"));
    const auto* AcceptedRed=Game->GetViewers().Find(TEXT("gray-overflow"));
    TestTrue(TEXT("Rejected gray viewer can directly enter available red"),AcceptedRed&&AcceptedRed->Team==1);
    TestTrue(TEXT("Rejected gray request did not consume the next body id"),AcceptedRed&&AcceptedRed->BodyId==LastGrayBodyId+1);
    Bridge->SimulateComment(TEXT("blue-after-gray-full"),TEXT("蓝方观众"),TEXT("2"));
    const auto* AcceptedBlue=Game->GetViewers().Find(TEXT("blue-after-gray-full"));
    TestTrue(TEXT("Gray-full viewers can directly enter available blue"),AcceptedBlue&&AcceptedBlue->Team==2);
    for(int32 i=1;i<RedCapacity;++i) Bridge->SimulateComment(FString::Printf(TEXT("red-cap-%d"),i),TEXT("红色观众"),TEXT("1"));
    TestEqual(TEXT("Red admission stops at configured capacity"),Game->GetMatch().teamCounts[1],RedCapacity);
    const int32 IdentitiesBeforeRefusal=Game->GetViewers().Num();
    const int32 GrayId=Game->GetViewers().FindChecked(TEXT("gray-cap-0")).BodyId;
    Bridge->SimulateComment(TEXT("gray-cap-0"),TEXT("灰色观众"),TEXT("1"));
    TestEqual(TEXT("Selecting full red keeps viewer state gray"),Game->GetViewers().FindChecked(TEXT("gray-cap-0")).Team,0);
    const auto* GrayFighter=Game->GetMatch().findFighter(GrayId);
    TestTrue(TEXT("Selecting full red keeps core fighter gray"),GrayFighter&&GrayFighter->team==0);
    TestTrue(TEXT("Full team selection feedback says gray identity is retained"),Game->LastEvent.Contains(LexToString(RedCapacity))&&Game->LastEvent.Contains(TEXT("保留灰色")));
    Bridge->SimulateComment(TEXT("red-overflow"),TEXT("红方候补"),TEXT("1"));
    TestFalse(TEXT("New viewer selecting full red is not registered"),Game->GetViewers().Contains(TEXT("red-overflow")));
    TestEqual(TEXT("Full team requests create no extra identities"),Game->GetViewers().Num(),IdentitiesBeforeRefusal);
    TestEqual(TEXT("Full team requests create no extra bodies"),static_cast<int32>(Game->GetArena().bodies.size()),IdentitiesBeforeRefusal);
    Game->ResetArena();
    Game->AddMockUsers(ViewerCapacity+1);
    TestEqual(TEXT("Queued demo respects configured viewer capacity"),Game->PendingMockUsers,ViewerCapacity);
    for(int32 i=0;i<(ViewerCapacity+63)/64;++i) Game->Tick(.001f);
    TestEqual(TEXT("Every unique UE viewer record materializes"),Game->GetViewers().Num(),ViewerCapacity);
    TestEqual(TEXT("Core and viewer identity counts agree"),static_cast<int32>(Game->GetArena().bodies.size()),ViewerCapacity);
    TestEqual(TEXT("Filled demo fills gray capacity"),Game->GetMatch().teamCounts[0],GrayCapacity);
    TestEqual(TEXT("Filled demo fills red capacity"),Game->GetMatch().teamCounts[1],RedCapacity);
    TestEqual(TEXT("Filled demo fills blue capacity"),Game->GetMatch().teamCounts[2],BlueCapacity);
    TestTrue(TEXT("Join burst remains queued for sequential display"),Game->GetFeed().pending()>ViewerCapacity-100);
    TSet<UTexture2D*> Portraits;
    bool AllTeamsMatch=true;
    FString FilledGrayUser;
    for(const auto& P:Game->GetViewers()) {
        Portraits.Add(P.Value.Avatar);
        const auto* Fighter=Game->GetMatch().findFighter(P.Value.BodyId);
        AllTeamsMatch&=Fighter&&Fighter->team==P.Value.Team;
        if(P.Value.Team==0&&FilledGrayUser.IsEmpty())FilledGrayUser=P.Key;
    }
    TestTrue(TEXT("All UE viewer teams agree with core fighters"),AllTeamsMatch);
    if(TestFalse(TEXT("Filled demo includes neutral viewers"),FilledGrayUser.IsEmpty())) {
        Bridge->SimulateComment(FilledGrayUser,TEXT("灰色观众"),TEXT("2"));
        TestEqual(TEXT("Selecting full blue preserves neutral viewer team"),Game->GetViewers().FindChecked(FilledGrayUser).Team,0);
        const auto* FilledGrayFighter=Game->GetMatch().findFighter(Game->GetViewers().FindChecked(FilledGrayUser).BodyId);
        TestTrue(TEXT("Selecting full blue preserves neutral core team"),FilledGrayFighter&&FilledGrayFighter->team==0);
        TestEqual(TEXT("Selecting full blue preserves gray participants"),Game->GetMatch().teamCounts[0],GrayCapacity);
        TestEqual(TEXT("Selecting full blue preserves blue participants"),Game->GetMatch().teamCounts[2],BlueCapacity);
        TestEqual(TEXT("Selecting full blue never duplicates a viewer"),Game->GetViewers().Num(),ViewerCapacity);
    }
    TestTrue(TEXT("Placeholder textures are shared for all viewers"),Portraits.Num()<=32);
    World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); Instance->Shutdown(); GEngine->DestroyWorldContext(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaCompactAdmissionTest,"GeometricWarfare.Arena.CompactAdmission",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaCompactAdmissionTest::RunTest(const FString&)
{
    auto* Instance=NewObject<UGameInstance>(GEngine);Instance->InitializeStandalone(FName(TEXT("CompactAdmissionWorld")));
    FLiveInteractionTestAdapter::EnableLocalTest(*Instance->GetSubsystem<ULiveInteractionSubsystem>());
    auto* World=Instance->GetWorld();FURL Url;Url.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));
    World->SetGameMode(Url);World->InitializeActorsForPlay(Url);World->BeginPlay();
    auto* Game=World->GetAuthGameMode<AArenaGameMode>();
    if(!TestNotNull(TEXT("Compact admission starts real game mode"),Game)){World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);return false;}
    auto* Bridge=Game->GetBridge();Game->ResetArena();
    for(int32 I=0;I<500;++I)Bridge->SimulateComment(FString::Printf(TEXT("compact-%03d"),I),TEXT("压测观众"),I<200?TEXT("1"):I<400?TEXT("2"):TEXT("加入"));
    TestEqual(TEXT("All 500 viewers enter through real comment admission"),Game->GetViewers().Num(),500);
    TestEqual(TEXT("Exactly 200 viewers join red"),Game->GetMatch().teamCounts[1],200);
    TestEqual(TEXT("Exactly 200 viewers join blue"),Game->GetMatch().teamCounts[2],200);
    TestEqual(TEXT("Exactly 100 viewers retain gray slots"),Game->GetMatch().teamCounts[0],100);
    for(const TCHAR* Command:{TEXT("1"),TEXT("2"),TEXT("加入")})Bridge->SimulateComment(TEXT("compact-overflow"),TEXT("候补"),Command);
    TestFalse(TEXT("501st viewer is refused through every full team path"),Game->GetViewers().Contains(TEXT("compact-overflow")));
    TestEqual(TEXT("Overflow cannot create another body"),static_cast<int32>(Game->GetArena().bodies.size()),500);
    Game->ResetArena();Game->AddMockUsers(501);
    TestEqual(TEXT("Mock queue clamps the 501 request to 500"),Game->PendingMockUsers,500);
    for(int32 Frame=0;Frame<8;++Frame)Game->Tick(.001f);
    TestEqual(TEXT("500 mocks finish materializing without duplicate identities"),Game->GetViewers().Num(),500);
    TestTrue(TEXT("500 mocks fill the exact 200/200/100 allocation"),Game->GetMatch().teamCounts[1]==200&&Game->GetMatch().teamCounts[2]==200&&Game->GetMatch().teamCounts[0]==100);
    Game->AddMockUsers(1);Game->Tick(.001f);
    TestEqual(TEXT("Adding a mock to a full game cannot exceed 500"),Game->GetViewers().Num(),500);
    TSet<UTexture2D*> Portraits;bool ValidAvatars=true;
    for(const auto& Pair:Game->GetViewers()){Portraits.Add(Pair.Value.Avatar);ValidAvatars&=Pair.Value.Avatar!=nullptr;}
    TestTrue(TEXT("Every one of the 500 mock avatars uses a valid shared placeholder"),ValidAvatars&&Portraits.Num()<=32);
    World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaProductionAdmissionTest,"GeometricWarfare.Arena.ProductionAdmission",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaProductionAdmissionTest::RunTest(const FString&)
{
    auto* Instance=NewObject<UGameInstance>(GEngine);
    Instance->InitializeStandalone(FName(TEXT("ProductionAdmissionWorld")));
    auto* Bridge=Instance->GetSubsystem<ULiveInteractionSubsystem>();
    Bridge->StartPlatform(TEXT("automation-provider-does-not-exist"));
    UWorld* World=Instance->GetWorld();
    FURL Url;Url.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));
    World->SetGameMode(Url);World->InitializeActorsForPlay(Url);World->BeginPlay();
    auto* Game=World->GetAuthGameMode<AArenaGameMode>();
    if(!TestNotNull(TEXT("Production arena starts without SDK"),Game)) {
        World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);return false;
    }
    TestTrue(TEXT("Default production arena has no mock viewers"),Game->GetViewers().IsEmpty());
    TestFalse(TEXT("Unavailable provider cannot report connected"),Bridge->IsConnected());
    TestEqual(TEXT("Missing SDK is explicit to arena"),Bridge->ConnectionStatus,FString(TEXT("SDK_UNAVAILABLE")));
    Game->AddMockUsers(96);Game->SetDemoSpeed(8);Game->Tick(2.f);
    auto* Controller=World->SpawnActor<AArenaPlayerController>();
    Controller->GWComment(TEXT("test-command"),TEXT("测试"),TEXT("1"));Controller->GWSpeed(8);
    TestTrue(TEXT("Production rejects demo and console viewer mutations"),Game->GetViewers().IsEmpty());
    TestEqual(TEXT("Production cannot queue mocks"),Game->PendingMockUsers,0);
    TestEqual(TEXT("Production ignores demo acceleration"),Game->DemoSpeed,1.f);
    TestEqual(TEXT("Missing SDK does not advance round timer"),Game->GetMatch().elapsed,0.0);
    FLiveComment Forged;Forged.UserId=TEXT("forged-viewer");Forged.Content=TEXT("1");Forged.MessageId=TEXT("forged-id");
    Bridge->OnComment.Broadcast(Forged);
    TestTrue(TEXT("Game handlers independently reject unprovenanced direct broadcasts"),Game->GetViewers().IsEmpty());
    World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);
    return true;
}

#endif
