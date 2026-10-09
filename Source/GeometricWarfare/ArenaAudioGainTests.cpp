#include "ArenaAudioSubsystem.h"
#include "ArenaUserSettings.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
struct FArenaAudioGainTestAccess {
    static float MusicVolume(const UArenaAudioSubsystem& S){return S.Music[S.MainSlot]->VolumeMultiplier;}
    static float AssistVolume(const UArenaAudioSubsystem& S){return S.Assist->VolumeMultiplier;}
    static float EffectVolume(const UArenaAudioSubsystem& S){return S.Pool[0]->VolumeMultiplier;}
    static double AssistEnd(const UArenaAudioSubsystem& S){return S.AssistEnd;}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaAudioGainBoostTest,"GeometricWarfare.Arena.Audio.GainBoost",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FArenaAudioGainBoostTest::RunTest(const FString&){
    auto* Instance=NewObject<UGameInstance>(GEngine);
    Instance->InitializeStandalone(FName(TEXT("AudioGainTestWorld")));
    auto* World=Instance->GetWorld();
    ON_SCOPE_EXIT{World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);};
    auto* Audio=World->GetSubsystem<UArenaAudioSubsystem>();
    if(!TestNotNull(TEXT("world audio"),Audio))return false;
    Audio->PrepareAudio();
    if(!TestEqual(TEXT("gain test assets present"),Audio->MissingAssets,0))return false;
    auto* Settings=UArenaUserSettings::Get();
    if(!TestNotNull(TEXT("audio settings"),Settings))return false;
    const float SavedBgm=Settings->BgmVolume,SavedSfx=Settings->SfxVolume;
    ON_SCOPE_EXIT{Settings->BgmVolume=SavedBgm;Settings->SfxVolume=SavedSfx;};
    auto Near=[this](const TCHAR* Label,float Actual,float Expected){TestTrue(Label,FMath::IsNearlyEqual(Actual,Expected,1.e-5f));};
    gw::Match Match;
    Settings->BgmVolume=Settings->SfxVolume=1.f;
    Audio->TickAudio(Match,-1,-1,true,10);
    Match.audio.emit(gw::AudioKind::Pistol);
    Audio->TickAudio(Match,-1,-1,true,11);
    Near(TEXT("100 BGM doubles previous .35"),FArenaAudioGainTestAccess::MusicVolume(*Audio),.70f);
    Near(TEXT("100 SFX doubles previous .08"),FArenaAudioGainTestAccess::EffectVolume(*Audio),.16f);
    TestTrue(TEXT("effect reaches playback route"),Audio->Starts>0);
    Audio->HostAssist(12);
    Near(TEXT("assist initial playback doubles previous .65"),FArenaAudioGainTestAccess::AssistVolume(*Audio),1.30f);
    Audio->TickAudio(Match,-1,-1,true,12.2);
    Near(TEXT("assist tick retains boosted volume"),FArenaAudioGainTestAccess::AssistVolume(*Audio),1.30f);
    Near(TEXT("boost retains .35 duck ratio"),FArenaAudioGainTestAccess::MusicVolume(*Audio),.70f*.35f);
    Audio->TickAudio(Match,-1,-1,true,FArenaAudioGainTestAccess::AssistEnd(*Audio)+1);
    Near(TEXT("music recovers boosted volume after assist"),FArenaAudioGainTestAccess::MusicVolume(*Audio),.70f);
    Audio->ResetAudio();
    Settings->BgmVolume=Settings->SfxVolume=.5f;
    Audio->TickAudio(Match,-1,-1,true,20);
    Audio->TickAudio(Match,-1,-1,true,21);
    Near(TEXT("half BGM preserves quadratic curve"),FArenaAudioGainTestAccess::MusicVolume(*Audio),.175f);
    Near(TEXT("half SFX preserves quadratic curve"),FArenaAudioGainTestAccess::EffectVolume(*Audio),.04f);
    Settings->BgmVolume=Settings->SfxVolume=0.f;
    const uint64 StartsBeforeMute=Audio->Starts;
    Match.audio.emit(gw::AudioKind::Pistol);
    Audio->TickAudio(Match,-1,-1,true,22);
    Near(TEXT("BGM mute remains zero"),FArenaAudioGainTestAccess::MusicVolume(*Audio),0.f);
    Near(TEXT("SFX mute remains zero"),FArenaAudioGainTestAccess::EffectVolume(*Audio),0.f);
    TestEqual(TEXT("mute prevents effect starts"),Audio->Starts,StartsBeforeMute);
    Audio->HostAssist(23);
    Near(TEXT("assist mute remains zero at playback"),FArenaAudioGainTestAccess::AssistVolume(*Audio),0.f);
    return true;
}
#endif
