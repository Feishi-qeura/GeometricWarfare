#include "ArenaHUD.h"
#include "ArenaGameMode.h"
#include "ArenaUserSettings.h"
#include "LiveInteractionTestAdapter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerInput.h"
#include "Components/InputComponent.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
struct FArenaSettingsTestAccess {
    static void Layout(AArenaHUD& H) {
        H.ViewportWidth=1920;H.ViewportHeight=1080;
        H.SettingsButtonBounds=FBox2D({1805,16},{1888,43});
        H.SettingRegions.Reset();H.SettingRegions.Add({FBox2D({985,368},{1212,406}),1});
    }
    static void Row(AArenaHUD& H,int32 Action){H.SettingRegions.Reset();H.SettingRegions.Add({FBox2D({985,368},{1212,406}),Action});}
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaSettingsTest,"GeometricWarfare.Arena.Settings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaSettingsTest::RunTest(const FString&) {
    TestTrue(TEXT("Configured player input resolves without disabled plugin"),UInputSettings::GetDefaultPlayerInputClass()==UPlayerInput::StaticClass());
    TestTrue(TEXT("Configured input component resolves without disabled plugin"),UInputSettings::GetDefaultInputComponentClass()==UInputComponent::StaticClass());
    auto* S=UArenaUserSettings::Get();if(!TestNotNull(TEXT("Engine uses persistent arena settings class"),S))return false;
    const float OriginalBgm=S->BgmVolume,OriginalSfx=S->SfxVolume;
    const FString OriginalIni=GGameUserSettingsIni;
    const bool OriginalSync=S->IsVSyncEnabled();const float OriginalRate=S->GetFrameRateLimit();
    const int32 OriginalMode=S->EffectMode,OriginalLabels=S->BattlefieldLabels,OriginalNumbers=S->FloatingNumbers;
    const int32 OriginalDevice=S->LayoutDevice,OriginalAspect=S->LayoutAspect;
    GGameUserSettingsIni=FPaths::ProjectSavedDir()/TEXT("Tests/Settings-")+FGuid::NewGuid().ToString()+TEXT(".ini");
    ON_SCOPE_EXIT {
        IFileManager::Get().Delete(*GGameUserSettingsIni);GGameUserSettingsIni=OriginalIni;
        S->BgmVolume=OriginalBgm;S->SfxVolume=OriginalSfx;S->SetVSyncEnabled(OriginalSync);S->SetFrameRateLimit(OriginalRate);
        S->EffectMode=OriginalMode;S->BattlefieldLabels=OriginalLabels;S->FloatingNumbers=OriginalNumbers;
        S->LayoutDevice=OriginalDevice;S->LayoutAspect=OriginalAspect;
        S->ApplyNonResolutionSettings();
    };
    S->BgmVolume=std::numeric_limits<float>::quiet_NaN();S->SfxVolume=2;S->ValidateSettings();
    TestEqual(TEXT("NaN BGM returns default"),S->BgmVolume,.55f);TestEqual(TEXT("SFX clamps to maximum"),S->SfxVolume,1.f);
    S->BgmVolume=.23f;S->SfxVolume=.61f;S->SaveSettings();
    auto* AudioReload=NewObject<UArenaUserSettings>();AudioReload->LoadSettings();
    TestEqual(TEXT("BGM persists"),AudioReload->BgmVolume,.23f);TestEqual(TEXT("SFX persists"),AudioReload->SfxVolume,.61f);
    S->EffectMode=0;S->BattlefieldLabels=180;S->FloatingNumbers=128;
    TestFalse(TEXT("999 participants keep full cosmetics"),S->Budget(999).reduced);
    const auto Thousand=S->Budget(1000);
    TestTrue(TEXT("1000 switches to reduced"),Thousand.reduced);
    TestEqual(TEXT("1000 name/health budget"),Thousand.labels,40);
    TestEqual(TEXT("1000 floating number budget"),Thousand.damageNumbers,32);
    TestEqual(TEXT("5000 minimap refresh seconds"),S->Budget(5000).minimapInterval,.5f);
    S->EffectMode=1;TestFalse(TEXT("Full override at 5000"),S->Budget(5000).reduced);
    S->EffectMode=2;S->BattlefieldLabels=0;S->FloatingNumbers=0;
    TestTrue(TEXT("Reduced override at zero viewers"),S->Budget(0).reduced);
    TestEqual(TEXT("Zero cosmetics respected"),S->Budget(5000).labels,0);
    S->EffectMode=99;S->BattlefieldLabels=-5;S->FloatingNumbers=10000;S->SetFrameRateLimit(17);S->ValidateSettings();
    TestEqual(TEXT("Invalid frame cap recovers 60"),S->GetFrameRateLimit(),60.f);
    TestEqual(TEXT("Invalid labels clamped"),S->BattlefieldLabels,0);
    TestEqual(TEXT("Invalid number count clamped"),S->FloatingNumbers,128);
    S->SetFrameRateLimit(90);S->SetVSyncEnabled(true);S->EffectMode=1;S->BattlefieldLabels=80;S->FloatingNumbers=64;S->SaveSettings();
    auto* Reloaded=NewObject<UArenaUserSettings>();Reloaded->LoadSettings();
    TestEqual(TEXT("Saved FPS survives reload"),Reloaded->GetFrameRateLimit(),90.f);
    TestTrue(TEXT("Saved VSync survives reload"),Reloaded->IsVSyncEnabled());
    TestEqual(TEXT("Saved effect override survives reload"),Reloaded->EffectMode,1);
    TestEqual(TEXT("Saved label budget survives reload"),Reloaded->BattlefieldLabels,80);
    TestEqual(TEXT("Saved number budget survives reload"),Reloaded->FloatingNumbers,64);
    auto* Instance=NewObject<UGameInstance>(GEngine);Instance->InitializeStandalone(FName(TEXT("ArenaSettingsTestWorld")));
    auto* World=Instance->GetWorld();if(!TestNotNull(TEXT("Settings test world"),World))return false;
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);};
    FLiveInteractionTestAdapter::EnableLocalTest(*Instance->GetSubsystem<ULiveInteractionSubsystem>());
    FURL URL;URL.AddOption(TEXT("game=/Script/GeometricWarfare.ArenaGameMode"));World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* H=World->SpawnActor<AArenaHUD>();FArenaSettingsTestAccess::Layout(*H);
    H->BeginPointer(1840,28);H->EndPointer(1840,28);TestTrue(TEXT("Real UI button opens settings"),H->IsSettingsOpen());
    FArenaSettingsTestAccess::Layout(*H);
    S->SetVSyncEnabled(false);
    H->BeginPointer(1000,380);H->EndPointer(1000,380);TestTrue(TEXT("Real settings row toggles VSync"),S->IsVSyncEnabled());
    H->BeginPointer(1000,380);H->UpdatePointer(1020,380);H->EndPointer(1000,380);
    TestTrue(TEXT("Dragged settings row never toggles"),S->IsVSyncEnabled());
    FArenaSettingsTestAccess::Row(*H,2);S->SetFrameRateLimit(30);
    const float ExpectedRates[]={60,90,120,0,30};
    for(const float Expected:ExpectedRates){H->BeginPointer(1000,380);H->EndPointer(1000,380);TestEqual(TEXT("Frame rate row cycles each supported cap"),S->GetFrameRateLimit(),Expected);}
    FArenaSettingsTestAccess::Row(*H,3);S->EffectMode=0;
    H->BeginPointer(1000,380);H->EndPointer(1000,380);TestEqual(TEXT("Effects row selects full"),S->EffectMode,1);
    H->BeginPointer(1000,380);H->EndPointer(1000,380);TestEqual(TEXT("Effects row selects reduced"),S->EffectMode,2);
    FArenaSettingsTestAccess::Row(*H,7);H->BeginPointer(1000,380);H->EndPointer(1000,380);
    TestEqual(TEXT("Performance defaults restore 60"),S->GetFrameRateLimit(),60.f);
    TestEqual(TEXT("Performance defaults restore auto"),S->EffectMode,0);
    FArenaSettingsTestAccess::Row(*H,8);S->LayoutDevice=0;
    for(int32 Device:{1,2,0}){H->BeginPointer(1000,380);H->EndPointer(1000,380);TestEqual(TEXT("Device row cycles PC mobile auto"),S->LayoutDevice,Device);}
    FArenaSettingsTestAccess::Row(*H,9);S->LayoutAspect=0;
    for(int32 Aspect:{1,2,3,4,5,0}){H->BeginPointer(1000,380);H->EndPointer(1000,380);TestEqual(TEXT("Aspect row cycles all five layouts and auto"),S->LayoutAspect,Aspect);}
    S->LayoutDevice=2;S->LayoutAspect=3;S->SaveSettings();Reloaded->LoadSettings(true);
    TestEqual(TEXT("Mobile layout saved"),Reloaded->LayoutDevice,2);TestEqual(TEXT("Portrait ratio saved"),Reloaded->LayoutAspect,3);
    FArenaSettingsTestAccess::Row(*H,7);H->BeginPointer(1000,380);H->EndPointer(1000,380);
    TestEqual(TEXT("Performance reset keeps broadcast layout"),S->LayoutAspect,3);
    S->LayoutDevice=99;S->LayoutAspect=-1;S->ValidateSettings();
    TestEqual(TEXT("Invalid device clamps"),S->LayoutDevice,2);TestEqual(TEXT("Invalid aspect clamps"),S->LayoutAspect,0);
    auto* G=World->GetAuthGameMode<AArenaGameMode>();const float Zoom=G->CameraZoom;
    H->ZoomAtCursor(800,500,2);TestEqual(TEXT("Modal blocks wheel zoom"),G->CameraZoom,Zoom);
    const int32 Focus=G->FocusBodyId;
    FArenaSettingsTestAccess::Row(*H,15);G->FocusBodyId=42;
    H->BeginPointer(985,380);H->UpdatePointer(1098.5f,380);
    TestEqual(TEXT("BGM drag updates while focus locked"),S->BgmVolume,.5f);H->EndPointer(1212,380);
    TestEqual(TEXT("BGM release reaches max"),S->BgmVolume,1.f);AudioReload->LoadSettings(true);TestEqual(TEXT("Slider release saves"),AudioReload->BgmVolume,1.f);
    FArenaSettingsTestAccess::Row(*H,16);H->BeginPointer(985,380);H->EndPointer(985,380);TestEqual(TEXT("SFX slider exact mute"),S->SfxVolume,0.f);
    FArenaSettingsTestAccess::Row(*H,7);H->BeginPointer(1000,380);H->EndPointer(1000,380);
    TestEqual(TEXT("Performance reset preserves BGM"),S->BgmVolume,1.f);TestEqual(TEXT("Performance reset preserves mute"),S->SfxVolume,0.f);G->FocusBodyId=Focus;H->BeginPointer(500,400);H->EndPointer(500,400);
    TestEqual(TEXT("Modal background cannot click a fighter"),G->FocusBodyId,Focus);
    H->ToggleSettings();TestFalse(TEXT("Toggle closes panel"),H->IsSettingsOpen());
    return true;
}
#endif
