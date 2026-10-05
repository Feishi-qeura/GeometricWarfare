#include "ArenaAudioSubsystem.h"
#include "ArenaUserSettings.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Misc/ScopeExit.h"
#if WITH_DEV_AUTOMATION_TESTS
struct FArenaAudioTestAccess {
    static int Phase(UArenaAudioSubsystem& S){return S.CurrentPhase;}
    static float Duck(UArenaAudioSubsystem& S){return S.Duck;}
    static double AssistAt(UArenaAudioSubsystem& S){return S.AssistAt;}
    static int PoolSize(UArenaAudioSubsystem& S){return S.Pool.Num();}
    static void Exhaust(UArenaAudioSubsystem& S,double At){S.Budget.tokens=0;S.Budget.lastTime=At;}
    static float BattleWeight(UArenaAudioSubsystem& S){for(int i=0;i<2;++i)if(S.Music[i]->Sound==S.Waves[0])return S.MainWeights[i];return 0;}
    static bool SilentAssist(UArenaAudioSubsystem& S){return S.Waves[4]->VirtualizationMode==EVirtualizationMode::PlayWhenSilent;}
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaAudioPlaybackTest,"GeometricWarfare.Arena.Audio.Playback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FArenaAudioPlaybackTest::RunTest(const FString&){
    auto* Instance=NewObject<UGameInstance>(GEngine);Instance->InitializeStandalone(FName(TEXT("AudioTestWorld")));
    auto* World=Instance->GetWorld();ON_SCOPE_EXIT{World->DestroyWorld(false);Instance->Shutdown();GEngine->DestroyWorldContext(World);};
    auto* S=World->GetSubsystem<UArenaAudioSubsystem>();if(!TestNotNull(TEXT("world audio"),S))return false;
    S->PrepareAudio();TestEqual(TEXT("all cooked assets present"),S->MissingAssets,0);TestEqual(TEXT("fixed SFX pool"),FArenaAudioTestAccess::PoolSize(*S),16);
    auto* Settings=UArenaUserSettings::Get();const float B=Settings->BgmVolume,F=Settings->SfxVolume;
    ON_SCOPE_EXIT{Settings->BgmVolume=B;Settings->SfxVolume=F;};
    gw::Match Match;Settings->SfxVolume=0;Match.audio.emit(gw::AudioKind::Pistol);S->TickAudio(Match,-1,-1,true,0);
    TestEqual(TEXT("muted starts zero"),S->Starts,uint64(0));TestEqual(TEXT("muted events drain"),Match.audio.count(gw::AudioKind::Pistol),uint64(0));
    TestEqual(TEXT("battle music"),FArenaAudioTestAccess::Phase(*S),0);
    Match.boss.active=true;S->TickAudio(Match,-1,-1,true,1);TestEqual(TEXT("boss music"),FArenaAudioTestAccess::Phase(*S),1);
    Match.phase=gw::Phase::Sprint;S->TickAudio(Match,-1,-1,true,2);TestEqual(TEXT("sprint overrides boss"),FArenaAudioTestAccess::Phase(*S),2);
    S->HostAssist(2);S->TickAudio(Match,-1,-1,true,2.2);TestTrue(TEXT("assist ducks"),FArenaAudioTestAccess::Duck(*S)<.5f);
    S->HostAssist(3);TestEqual(TEXT("assist cooldown prevents replay"),FArenaAudioTestAccess::AssistAt(*S),2.0);
    S->TickAudio(Match,-1,-1,true,7);TestTrue(TEXT("assist restores theme"),FArenaAudioTestAccess::Duck(*S)>.99f);
    Match.phase=gw::Phase::Results;S->TickAudio(Match,-1,-1,true,8);TestEqual(TEXT("results highest"),FArenaAudioTestAccess::Phase(*S),3);
    S->ResetAudio();TestEqual(TEXT("session reset stops music state"),FArenaAudioTestAccess::Phase(*S),-1);
    Settings->SfxVolume=.7f;Match.phase=gw::Phase::Battle;Match.boss.active=false;
    for(int frame=0;frame<60;++frame){for(int i=0;i<10000;++i)Match.audio.emit(static_cast<gw::AudioKind>(i%6),i);S->TickAudio(Match,7,9,true,10+frame/60.0);}
    TestTrue(TEXT("pool never expands with events"),S->ActivePeak<=16);TestTrue(TEXT("bounded starts under flood"),S->Starts<=67);
    S->ResetAudio();
    TestTrue(TEXT("Silent nonloop assist preserves playback clock"),FArenaAudioTestAccess::SilentAssist(*S));
    Match.boss.active=false;Match.phase=gw::Phase::Battle;S->TickAudio(Match,-1,-1,true,20);S->TickAudio(Match,-1,-1,true,20.7);
    Match.boss.active=true;S->TickAudio(Match,-1,-1,true,20.7);S->TickAudio(Match,-1,-1,true,20.8);
    const float BattleWeight=FArenaAudioTestAccess::BattleWeight(*S);Match.boss.active=false;S->TickAudio(Match,-1,-1,true,20.8);
    TestEqual(TEXT("Rapid return keeps audible original music"),FArenaAudioTestAccess::BattleWeight(*S),BattleWeight);
    S->ResetAudio();Match.boss.active=true;Match.boss.attack=gw::BossAttack::LaserWindup;Match.boss.attackElapsed=1.5;
    const uint64 BeforeResume=S->Starts;FArenaAudioTestAccess::Exhaust(*S,30);Match.audio.emit(gw::AudioKind::BossLaserWindup);S->TickAudio(Match,-1,-1,true,30);
    S->TickAudio(Match,-1,-1,true,30.02);TestEqual(TEXT("Dropped windup resumes without new event"),S->Starts,BeforeResume+1);
    S->ResetAudio();
    auto* Second=NewObject<UGameInstance>(GEngine);Second->InitializeStandalone(FName(TEXT("AudioReuseWorld")));
    auto* SecondWorld=Second->GetWorld();SecondWorld->GetSubsystem<UArenaAudioSubsystem>()->PrepareAudio();
    TestEqual(TEXT("cached waves reusable across worlds"),SecondWorld->GetSubsystem<UArenaAudioSubsystem>()->MissingAssets,0);
    SecondWorld->DestroyWorld(false);Second->Shutdown();GEngine->DestroyWorldContext(SecondWorld);
    return true;
}
#endif
