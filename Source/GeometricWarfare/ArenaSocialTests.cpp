#include "ArenaGameMode.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaSocialCombatTest,"GeometricWarfare.Arena.SocialCombat",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaSocialCombatTest::RunTest(const FString&)
{
    auto* Instance=NewObject<UGameInstance>(GEngine);
    Instance->InitializeStandalone(FName(TEXT("SocialCombatWorld")));
    UWorld* World=Instance->GetWorld();
    FURL Url;Url.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));
    World->SetGameMode(Url);World->InitializeActorsForPlay(Url);World->BeginPlay();
    auto* Game=World->GetAuthGameMode<AArenaGameMode>();
    if(!TestNotNull(TEXT("Arena created"),Game))return false;
    Game->ResetArena();auto* Bridge=Game->GetBridge();
    Game->ProgressSlot=TEXT("Automation-Social-")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Game->Progress=NewObject<UArenaProgressSave>(Game);Game->bProgressWritable=true;
    Bridge->SimulateLike(TEXT("absent"),TEXT("未入场"),1);
    Bridge->SimulateShare(TEXT("absent"),TEXT("未入场"));
    TestEqual(TEXT("Social events do not implicitly register viewers"),Game->GetViewers().Num(),0);
    Bridge->SimulateComment(TEXT("social-1"),TEXT("互动观众"),TEXT("1"));
    const int32 Id=Game->GetViewers().FindChecked(TEXT("social-1")).BodyId;
    auto& Match=const_cast<gw::Match&>(Game->GetMatch());
    auto* Fighter=Match.findFighter(Id);Fighter->hp=100;
    const double MaxHp=Fighter->maxHp;
    FDouyinLike Like;Like.MessageId=TEXT("single-like");Like.UserId=TEXT("social-1");Like.Count=1;
    Bridge->DeliverLike(Like);
    TestTrue(TEXT("Like reaches actual GameMode and restores max HP times five percent"),FMath::IsNearlyEqual(Fighter->hp,100+MaxHp*.05,.00001));
    const auto& Numbers=Game->GetDamageNumbers();
    TestEqual(TEXT("Like immediately reaches the visible number pool before a simulation tick"),static_cast<int32>(Numbers.activeCount()),1);
    if(Numbers.activeCount()==1) {
        for(const auto& Slot:Numbers.slots)if(Slot.active) {
            TestTrue(TEXT("Like is labeled as healing"),Slot.kind==gw::NumberKind::Healing);
            TestEqual(TEXT("Healing number is attached to the actual viewer"),Slot.targetId,Id);
            TestTrue(TEXT("Healing number reports the actual restored amount"),FMath::IsNearlyEqual(Slot.amount,MaxHp*.05,.00001));
        }
    }
    Bridge->DeliverLike(Like);
    TestTrue(TEXT("Redelivering one like cannot heal twice"),FMath::IsNearlyEqual(Fighter->hp,100+MaxHp*.05,.00001));
    double ShownHealing=0;for(const auto& Slot:Numbers.slots)if(Slot.active)ShownHealing+=Slot.amount;
    TestTrue(TEXT("Duplicate like does not duplicate the visible healing amount"),FMath::IsNearlyEqual(ShownHealing,MaxHp*.05,.00001));
    Bridge->SimulateLike(TEXT("social-1"),TEXT("互动观众"),100);
    TestEqual(TEXT("Like batches clamp at maximum HP"),Fighter->hp,MaxHp);
    Bridge->SimulateShare(TEXT("social-1"),TEXT("互动观众"));
    TestTrue(TEXT("Share grants shotgun through real delegate"),Fighter->weaponKind==gw::WeaponKind::Shotgun);
    TestEqual(TEXT("Shotgun initially holds five volleys"),Fighter->ammo,5);
    Fighter->ammo=0;Fighter->reloadRemaining=4;
    Bridge->SimulateShare(TEXT("social-1"),TEXT("互动观众"));
    TestEqual(TEXT("Repeated share cannot refill ammunition"),Fighter->ammo,0);
    TestEqual(TEXT("Repeated share cannot shorten reload"),Fighter->reloadRemaining,4.0);
    Game->DemoAction(TEXT("host-red"));
    int HostId=-1;for(const auto& F:Match.fighters)if(F.isHost)HostId=F.id;
    if(TestTrue(TEXT("Local host control enters one special fighter"),HostId>=0)) {
        const auto* Host=Match.findFighter(HostId);
        TestEqual(TEXT("Host starts with 3000 life"),Host->maxHp,3000.0);
        TestEqual(TEXT("Host starts with 500 defense"),Host->armor,500.0);
        TestEqual(TEXT("Host does not consume a red viewer slot"),Match.teamCounts[1],1);
        TestEqual(TEXT("Host is not a viewer identity"),Game->GetViewers().Num(),1);
        TestTrue(TEXT("Host has rifle"),Host->weaponKind==gw::WeaponKind::Rifle);
        Game->DemoAction(TEXT("host-blue"));
        TestEqual(TEXT("Host control changes team without another host"),static_cast<int>(Match.fighters.size()),2);
        TestEqual(TEXT("Host now assists blue"),Match.findFighter(HostId)->team,2);
        Match.refreshStandings();
        TestEqual(TEXT("Host absent from ranking"),static_cast<int>(Match.leaderboard.size()),1);
    }
    Bridge->SimulateComment(TEXT("social-1"),TEXT("互动观众"),TEXT("host-blue"));
    TestFalse(TEXT("Viewer comment cannot transform a viewer into host"),Match.findFighter(Id)->isHost);
    Match.startNextRound();
    TestTrue(TEXT("Sharing weapon remains unlocked at next round"),(Match.findFighter(Id)->unlockedWeapons&gw::weaponBit(gw::WeaponKind::Shotgun))!=0);
    TestTrue(TEXT("Equipped weapon survives next round"),Match.findFighter(Id)->weaponKind==gw::WeaponKind::Shotgun);
    Game->ResetArena();
    TestTrue(TEXT("Reset clears host and viewers"),Match.fighters.empty());
    UGameplayStatics::DeleteGameInSlot(Game->ProgressSlot,0);
    World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);
    return true;
}
#endif
