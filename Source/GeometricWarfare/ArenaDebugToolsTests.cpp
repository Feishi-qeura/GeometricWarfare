#include "ArenaGameMode.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace {
class FDebugToolsProvider final : public ILiveInteractionProvider {
public:
    FLiveSession Session;
    FString GetPlatformId() const override {return TEXT("debug-tools-test");}
    bool Start(ULiveInteractionSubsystem& Host) override {Session=Host.BeginProviderSession(*this,TEXT("app"),TEXT("room"),TEXT("anchor"));return Session.Nonce.IsValid();}
    void Stop() override {}
    bool SendCommand(const FString&,const FString&,const TSharedRef<FJsonObject>&) override {return true;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaDebugToolsTest,"GeometricWarfare.Arena.DebugTools",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaDebugToolsTest::RunTest(const FString&) {
    bool Old=false;const bool Had=GConfig->GetBool(TEXT("ArenaDebug"),TEXT("EnableGM"),Old,GGameIni);
    GConfig->SetBool(TEXT("ArenaDebug"),TEXT("EnableGM"),true,GGameIni);
    auto Provider=MakeShared<FDebugToolsProvider>();
    FLiveInteractionProviderRegistry::Register(Provider->GetPlatformId(),[Provider]() -> TSharedPtr<ILiveInteractionProvider> {return Provider;});
    auto* Instance=NewObject<UGameInstance>(GEngine);Instance->InitializeStandalone(FName(TEXT("DebugToolsWorld")));
    auto* Bridge=Instance->GetSubsystem<ULiveInteractionSubsystem>();Bridge->StartPlatform(Provider->GetPlatformId());
    UWorld* World=Instance->GetWorld();FURL Url;Url.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));
    World->SetGameMode(Url);World->InitializeActorsForPlay(Url);World->BeginPlay();
    auto* Game=World->GetAuthGameMode<AArenaGameMode>();
    if(!TestNotNull(TEXT("GM fixture created"),Game))return false;
    const FString Slot=TEXT("DebugTools_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Game->ProgressSlot=Slot;Game->Progress=NewObject<UArenaProgressSave>(Game);
    ON_SCOPE_EXIT {UGameplayStatics::DeleteGameInSlot(Slot,0);World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);FLiveInteractionProviderRegistry::Unregister(Provider->GetPlatformId());if(Had)GConfig->SetBool(TEXT("ArenaDebug"),TEXT("EnableGM"),Old,GGameIni);else GConfig->RemoveKey(TEXT("ArenaDebug"),TEXT("EnableGM"),GGameIni);};
    Game->DemoAction(TEXT("gm-add-red"));Game->DemoAction(TEXT("gm-add-blue"));Game->DemoAction(TEXT("gm-add-gray"));
    TestEqual(TEXT("G panel actions add one bot in each requested camp"),static_cast<int32>(Game->GetViewers().Num()),3);
    for(int Team=0;Team<3;++Team)TestEqual(FString::Printf(TEXT("Bot camp %d correct"),Team),Game->GetMatch().teamCounts[Team],1);
    FLiveComment Join;Join.Session=Provider->Session;Join.MessageId=FGuid::NewGuid().ToString();Join.UserId=TEXT("trusted-open-id");Join.Nickname=TEXT("同名观众");Join.Content=TEXT("1");Bridge->DeliverComment(Join);
    const auto* Viewer=Game->GetViewers().Find(Join.UserId);
    Game->SimulateGift(Join.UserId,TEXT("魔法镜"));
    TestTrue(TEXT("GM exact live identity executes ordinary sniper gift gameplay"),Viewer&&(Game->GetMatch().findFighter(Viewer->BodyId)->unlockedWeapons&gw::weaponBit(gw::WeaponKind::Sniper)));
    TestEqual(TEXT("GM gift never generates platform fulfilment ACK"),Game->PendingLiveAcks.Num(),1); // only real join
    Game->SimulateGift(TEXT("同名观众"),TEXT("甜甜圈"));
    TestTrue(TEXT("Nickname cannot target a real account"),Viewer&&!(Game->GetMatch().findFighter(Viewer->BodyId)->unlockedWeapons&gw::weaponBit(gw::WeaponKind::MachineGun)));
    TestFalse(TEXT("GM weapon reward remains session-only"),(Game->Progress->WeaponUnlocks.FindRef(Game->ProgressKey(Join.UserId))&gw::weaponBit(gw::WeaponKind::Sniper))!=0);
    TestTrue(TEXT("Public Douyin account explicitly binds observed SDK identity"),Game->BindGMIdentity(TEXT("real-douyin-id"),Join.UserId));
    TestTrue(TEXT("Bound public account receives exact gift effect"),Game->SendGMGift(TEXT("real-douyin-id"),TEXT("甜甜圈"),2));
    TestTrue(TEXT("Bound account equips machine gun"),Game->Match.findFighter(Viewer->BodyId)->weaponKind==gw::WeaponKind::MachineGun);
    TestTrue(TEXT("Battery gift equips rocket"),Game->SendGMGift(Join.UserId,TEXT("能量电池"))&&Game->Match.findFighter(Viewer->BodyId)->weaponKind==gw::WeaponKind::RocketLauncher);
    TestTrue(TEXT("GM can prepare death for wand test"),Game->PrepareGMGiftTarget(Join.UserId));
    TestFalse(TEXT("Wand fixture really dead"),Game->Match.findFighter(Viewer->BodyId)->alive);
    TestTrue(TEXT("Wand uses shared revive gameplay"),Game->SendGMGift(Join.UserId,TEXT("仙女棒"))&&Game->Match.findFighter(Viewer->BodyId)->alive);
    TestTrue(TEXT("GM can prepare destroyed base"),Game->PrepareGMGiftTarget(Join.UserId,true));
    TestTrue(TEXT("Pill rebuilds base via shared gameplay"),Game->SendGMGift(Join.UserId,TEXT("能力药丸"))&&Game->Match.bases[1].alive);
    const int Host=Game->NextBodyId++;Game->Match.addHost(Host,1);Game->HostBodyId=Host;
    Bridge->SetAnchorProfile(Provider->Session,TEXT("真实主播"),TEXT(""));
    auto* Portrait=Game->MakePlaceholder(83);Game->HandleAvatar(TEXT("anchor"),Portrait);Game->UpdateHostProfile();
    const auto* HostView=Game->FindViewer(Host);
    TestTrue(TEXT("Host fighter uses actual anchor portrait and nickname"),HostView&&HostView->Avatar==Portrait&&HostView->Name==TEXT("真实主播"));
    TestFalse(TEXT("Host profile never becomes a spectator identity"),Game->Viewers.Contains(TEXT("anchor")));
    const int Before=Game->Viewers.Num();GConfig->SetBool(TEXT("ArenaDebug"),TEXT("EnableGM"),false,GGameIni);
    Game->DemoAction(TEXT("gm-add-red"));TestEqual(TEXT("Release config disables GM"),Game->Viewers.Num(),Before);
    Game->HandleSessionChanged();TestTrue(TEXT("Session switch clears debug account bindings and host portrait"),Game->GMIdentityBindings.IsEmpty()&&!Game->HostViewer.Avatar);
    return true;
}
#endif
