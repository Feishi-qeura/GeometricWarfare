#include "ArenaGameMode.h"
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
    Bridge->SimulateComment(TEXT("viewer-1"),TEXT("测试观众"),TEXT("1"));
    TestEqual(TEXT("First numeric command selects team"),Game->GetViewers().FindChecked(TEXT("viewer-1")).Team,1);
    const bool WasRectangle=Game->GetArena().bodies[0].shape==gw::Shape::Rectangle;
    const auto NewShape=WasRectangle?gw::Shape::Triangle:gw::Shape::Rectangle;
    Bridge->SimulateComment(TEXT("viewer-1"),TEXT("测试观众"),WasRectangle?TEXT("4"):TEXT("3"));
    TestTrue(TEXT("Team member can select a different shape"),Game->GetArena().bodies[0].shape==NewShape);
    Bridge->SimulateComment(TEXT("viewer-1"),TEXT("测试观众"),WasRectangle?TEXT("3"):TEXT("4"));
    TestTrue(TEXT("Shape cooldown is enforced"),Game->GetArena().bodies[0].shape==NewShape);
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
    Controller->GWConnectRelay(TEXT("ws://127.0.0.1:1"),TEXT("room-one"));
    TestEqual(TEXT("Connecting relay returns to realtime speed"),Game->DemoSpeed,1.f);
    FDouyinComment Comment; Comment.MessageId=TEXT("r1-1"); Comment.UserId=TEXT("room-one-viewer"); Comment.Content=TEXT("加入");
    Bridge->DeliverComment(Comment);
    TestEqual(TEXT("First room viewer delivered"),Game->GetViewers().Num(),1);
    Controller->GWConnectRelay(TEXT("invalid"),TEXT("room-two"));
    TestEqual(TEXT("Invalid connection preserves existing viewers"),Game->GetViewers().Num(),1);
    Controller->GWConnectRelay(TEXT("ws://127.0.0.1:1"),TEXT("room-two"));
    TestEqual(TEXT("Changing room clears previous viewers"),Game->GetViewers().Num(),0);
    Controller->GWDisconnect();
    Game->ResetArena();
    for(int32 i=0;i<1000;++i) Bridge->SimulateComment(FString::Printf(TEXT("gray-cap-%d"),i),TEXT("灰色观众"),TEXT("加入"));
    const int32 LastGrayBodyId=Game->GetViewers().FindChecked(TEXT("gray-cap-999")).BodyId;
    TestEqual(TEXT("Normal joining fills exactly 1000 gray slots"),Game->GetMatch().teamCounts[0],1000);
    TestEqual(TEXT("1000 gray viewers have 1000 identities"),Game->GetViewers().Num(),1000);
    Bridge->SimulateComment(TEXT("gray-overflow"),TEXT("候补观众"),TEXT("加入"));
    TestFalse(TEXT("Gray overflow has no viewer identity"),Game->GetViewers().Contains(TEXT("gray-overflow")));
    TestEqual(TEXT("Gray overflow creates no body"),static_cast<int32>(Game->GetArena().bodies.size()),1000);
    TestTrue(TEXT("Gray-full feedback explains direct team commands"),Game->LastEvent.Contains(TEXT("1000"))&&Game->LastEvent.Contains(TEXT("1 / 2")));
    Bridge->SimulateComment(TEXT("gray-overflow"),TEXT("候补观众"),TEXT("1"));
    const auto* AcceptedRed=Game->GetViewers().Find(TEXT("gray-overflow"));
    TestTrue(TEXT("Rejected gray viewer can directly enter available red"),AcceptedRed&&AcceptedRed->Team==1);
    TestTrue(TEXT("Rejected gray request did not consume the next body id"),AcceptedRed&&AcceptedRed->BodyId==LastGrayBodyId+1);
    Bridge->SimulateComment(TEXT("blue-after-gray-full"),TEXT("蓝方观众"),TEXT("2"));
    const auto* AcceptedBlue=Game->GetViewers().Find(TEXT("blue-after-gray-full"));
    TestTrue(TEXT("Gray-full viewers can directly enter available blue"),AcceptedBlue&&AcceptedBlue->Team==2);
    for(int32 i=1;i<2000;++i) Bridge->SimulateComment(FString::Printf(TEXT("red-cap-%d"),i),TEXT("红色观众"),TEXT("1"));
    TestEqual(TEXT("Red admission stops at the configured 2000 participants"),Game->GetMatch().teamCounts[1],2000);
    const int32 IdentitiesBeforeRefusal=Game->GetViewers().Num();
    const int32 GrayId=Game->GetViewers().FindChecked(TEXT("gray-cap-0")).BodyId;
    Bridge->SimulateComment(TEXT("gray-cap-0"),TEXT("灰色观众"),TEXT("1"));
    TestEqual(TEXT("Selecting full red keeps viewer state gray"),Game->GetViewers().FindChecked(TEXT("gray-cap-0")).Team,0);
    const auto* GrayFighter=Game->GetMatch().findFighter(GrayId);
    TestTrue(TEXT("Selecting full red keeps core fighter gray"),GrayFighter&&GrayFighter->team==0);
    TestTrue(TEXT("Full team selection feedback says gray identity is retained"),Game->LastEvent.Contains(TEXT("2000"))&&Game->LastEvent.Contains(TEXT("保留灰色")));
    Bridge->SimulateComment(TEXT("red-overflow"),TEXT("红方候补"),TEXT("1"));
    TestFalse(TEXT("New viewer selecting full red is not registered"),Game->GetViewers().Contains(TEXT("red-overflow")));
    TestEqual(TEXT("Full team requests create no extra identities"),Game->GetViewers().Num(),IdentitiesBeforeRefusal);
    TestEqual(TEXT("Full team requests create no extra bodies"),static_cast<int32>(Game->GetArena().bodies.size()),IdentitiesBeforeRefusal);
    Game->ResetArena();
    Game->AddMockUsers(5001);
    TestEqual(TEXT("Queued demo capacity is 5000"),Game->PendingMockUsers,5000);
    for(int32 i=0;i<79;++i) Game->Tick(.001f);
    TestEqual(TEXT("5000 unique UE viewer records materialized"),Game->GetViewers().Num(),5000);
    TestEqual(TEXT("Core and viewer identity counts agree"),static_cast<int32>(Game->GetArena().bodies.size()),5000);
    TestEqual(TEXT("Filled demo has exactly 1000 gray participants"),Game->GetMatch().teamCounts[0],1000);
    TestEqual(TEXT("Filled demo has exactly 2000 red participants"),Game->GetMatch().teamCounts[1],2000);
    TestEqual(TEXT("Filled demo has exactly 2000 blue participants"),Game->GetMatch().teamCounts[2],2000);
    TestTrue(TEXT("Join burst remains queued for sequential display"),Game->GetFeed().pending()>4900);
    TSet<UTexture2D*> Portraits;
    bool AllTeamsMatch=true;
    FString FilledGrayUser;
    for(const auto& P:Game->GetViewers()) {
        Portraits.Add(P.Value.Avatar);
        const auto* Fighter=Game->GetMatch().findFighter(P.Value.BodyId);
        AllTeamsMatch&=Fighter&&Fighter->team==P.Value.Team;
        if(P.Value.Team==0&&FilledGrayUser.IsEmpty())FilledGrayUser=P.Key;
    }
    TestTrue(TEXT("All 5000 UE viewer teams agree with core fighters"),AllTeamsMatch);
    if(TestFalse(TEXT("Filled demo includes neutral viewers"),FilledGrayUser.IsEmpty())) {
        Bridge->SimulateComment(FilledGrayUser,TEXT("灰色观众"),TEXT("2"));
        TestEqual(TEXT("Selecting full blue preserves neutral viewer team"),Game->GetViewers().FindChecked(FilledGrayUser).Team,0);
        const auto* FilledGrayFighter=Game->GetMatch().findFighter(Game->GetViewers().FindChecked(FilledGrayUser).BodyId);
        TestTrue(TEXT("Selecting full blue preserves neutral core team"),FilledGrayFighter&&FilledGrayFighter->team==0);
        TestEqual(TEXT("Selecting full blue preserves 1000 gray participants"),Game->GetMatch().teamCounts[0],1000);
        TestEqual(TEXT("Selecting full blue preserves 2000 blue participants"),Game->GetMatch().teamCounts[2],2000);
        TestEqual(TEXT("Selecting full blue never duplicates a viewer"),Game->GetViewers().Num(),5000);
    }
    TestTrue(TEXT("Placeholder textures are shared for 5000 viewers"),Portraits.Num()<=32);
    World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); Instance->Shutdown(); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
