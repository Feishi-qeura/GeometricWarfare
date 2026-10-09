#include "ArenaAudioSubsystem.h"
#include "ArenaUserSettings.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Engine/World.h"
#include "AudioDevice.h"

namespace {
const TCHAR* const AudioAssetNames[]={TEXT("Battle"),TEXT("Boss"),TEXT("Sprint"),TEXT("Results"),TEXT("HostAssist"),TEXT("Pistol"),TEXT("Shotgun"),TEXT("Rifle"),TEXT("Sniper"),TEXT("MachineGun"),TEXT("Rocket"),TEXT("FighterHit"),TEXT("FighterDeath"),TEXT("BossBullet"),TEXT("BossLaserWindup"),TEXT("BossLaserRage"),TEXT("BossLaserBeam"),TEXT("BossStompJump"),TEXT("BossStompImpact"),TEXT("Orb"),TEXT("NpcHit"),TEXT("NpcDeath"),TEXT("WeaponPickup"),TEXT("WeaponBreak"),TEXT("EvolutionPickup"),TEXT("EvolutionBreak")};
// Double every playback route while preserving the slider curve, mute, fades and ducking.
float Gain(float Value,float Default){return 2.f*FMath::Square(FMath::IsFinite(Value)?FMath::Clamp(Value,0.f,1.f):Default);}
}
void UArenaAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection){Super::Initialize(Collection);Priorities.fill(-1);}
bool UArenaAudioSubsystem::HasAudioDevice()const{return GetWorld()&&GetWorld()->GetAudioDeviceRaw()!=nullptr;}
UAudioComponent* UArenaAudioSubsystem::MakeComponent(){
    auto* C=NewObject<UAudioComponent>(GetWorld());C->bAutoActivate=false;C->bAutoDestroy=false;C->bIsUISound=true;C->bAllowSpatialization=false;
    C->RegisterComponentWithWorld(GetWorld());return C;
}
void UArenaAudioSubsystem::PrepareAudio(){
    if(bPrepared)return;bPrepared=true;
    for(const auto* Name:AudioAssetNames){
        auto* Wave=LoadObject<USoundWave>(nullptr,*FString::Printf(TEXT("/Game/Audio/GeometricWarfare/%s.%s"),Name,Name));
        Waves.Add(Wave);if(!Wave){++MissingAssets;continue;}
        if(auto* Device=GetWorld()->GetAudioDeviceRaw())Device->Precache(Wave,true,false,false);
    }
    for(int i=0;i<16;++i)Pool.Add(MakeComponent());
    Music.Add(MakeComponent());Music.Add(MakeComponent());Assist=MakeComponent();
    if(MissingAssets)UE_LOG(LogTemp,Warning,TEXT("GW Audio: %d unavailable assets; gameplay continues"),MissingAssets);
}
void UArenaAudioSubsystem::ResetAudio(){
    for(UAudioComponent* C:Pool)if(C)C->Stop();for(UAudioComponent* C:Music)if(C)C->Stop();if(Assist)Assist->Stop();
    Priorities.fill(-1);Budget=gw::AudioBudget{};CurrentPhase=CurrentRound=-1;MainSlot=0;
    MainWeights[0]=MainWeights[1]=FromWeights[0]=FromWeights[1]=0;Duck=1;LastTick=-1;AssistAt=AssistEnd=-1000;
}
void UArenaAudioSubsystem::Deinitialize(){
    ResetAudio();for(UAudioComponent* C:Pool)if(C)C->DestroyComponent();for(UAudioComponent* C:Music)if(C)C->DestroyComponent();if(Assist)Assist->DestroyComponent();
    Pool.Reset();Music.Reset();Assist=nullptr;Waves.Reset();Super::Deinitialize();
}
void UArenaAudioSubsystem::HostAssist(double Now){
    PrepareAudio();if(Now<0)Now=FPlatformTime::Seconds();if(Now-AssistAt<2||!Waves[4])return;
    AssistAt=Now;AssistEnd=Now+Waves[4]->Duration;
    const auto* S=UArenaUserSettings::Get();Assist->Stop();Assist->SetSound(Waves[4]);Assist->SetVolumeMultiplier(Gain(S?S->BgmVolume:.55f,.55f)*.65f);Assist->Play();
}
void UArenaAudioSubsystem::UpdateMusic(const gw::Match& Match,bool Active,double Now,float Volume){
    if(!Active){if(CurrentPhase>=0)ResetAudio();return;}
    const int32 Phase=static_cast<int32>(gw::chooseMusic(Match.phase==gw::Phase::Results,Match.phase==gw::Phase::Sprint,Match.boss.active));
    if(CurrentRound!=Match.round){if(CurrentRound>=0)ResetAudio();CurrentRound=Match.round;}
    if(Phase==3){Assist->Stop();AssistEnd=-1000;Duck=1;}
    if(Phase!=CurrentPhase){
        FromWeights[0]=MainWeights[0];FromWeights[1]=MainWeights[1];
        int Existing=-1;for(int i=0;i<2;++i)if(MainWeights[i]>0&&Music[i]->Sound==Waves[Phase])Existing=i;
        if(Existing>=0)MainSlot=Existing; // Reverse the fade without restarting the audible theme.
        else {
            MainSlot=MainWeights[0]<=MainWeights[1]?0:1;
            Music[MainSlot]->Stop();Music[MainSlot]->SetSound(Waves[Phase]);
            FromWeights[MainSlot]=MainWeights[MainSlot]=0;
            if(Waves[Phase]){Music[MainSlot]->SetVolumeMultiplier(0);Music[MainSlot]->Play();}
        }
        TransitionAt=Now;CurrentPhase=Phase;
    }
    if(Now<AssistEnd){Duck=FMath::Lerp(1.f,.35f,static_cast<float>(FMath::Clamp((Now-AssistAt)/.15,0.0,1.0)));}
    else Duck=FMath::Lerp(.35f,1.f,static_cast<float>(FMath::Clamp((Now-AssistEnd)/.4,0.0,1.0)));
    const float Alpha=FMath::Clamp(static_cast<float>((Now-TransitionAt)/.6),0.f,1.f);
    for(int i=0;i<2;++i){MainWeights[i]=FMath::Lerp(FromWeights[i],i==MainSlot?1.f:0.f,Alpha);Music[i]->SetVolumeMultiplier(Volume*MainWeights[i]*Duck*.35f);if(i!=MainSlot&&Alpha>=1)Music[i]->Stop();}
    Assist->SetVolumeMultiplier(Volume*.65f);
}
void UArenaAudioSubsystem::StopLaser(gw::BossAttack Attack){
    for(int i=0;i<Pool.Num();++i){const auto Kind=PoolKinds[i];
        if((Kind==gw::AudioKind::BossLaserBeam&&Attack!=gw::BossAttack::LaserActive)||((Kind==gw::AudioKind::BossLaserWindup||Kind==gw::AudioKind::BossLaserRage)&&Attack!=gw::BossAttack::LaserWindup)){Pool[i]->Stop();Priorities[i]=-1;}
    }
}
bool UArenaAudioSubsystem::StartEffect(gw::AudioKind Kind,int Priority,double Now,float Offset){
    const int AssetIndex=5+static_cast<int>(Kind);if(!Waves.IsValidIndex(AssetIndex)||!Waves[AssetIndex])return false;
    int Slot=-1;for(int i=0;i<Pool.Num();++i)if(!Pool[i]->IsPlaying()){Slot=i;break;}
    if(Slot<0){for(int i=0;i<16;++i)if(Priorities[i]<Priority&&(Slot<0||Priorities[i]<Priorities[Slot]))Slot=i;}
    if(Slot<0||!Budget.allow(Kind,Priority,Now))return false;
    UAudioComponent* C=Pool[Slot];C->Stop();C->SetSound(Waves[AssetIndex]);PoolKinds[Slot]=Kind;Priorities[Slot]=Priority;
    const bool Continuous=Kind==gw::AudioKind::BossLaserBeam||Kind==gw::AudioKind::BossLaserWindup||Kind==gw::AudioKind::BossLaserRage;
    C->SetPitchMultiplier(Continuous?1.f:1.f+PitchRandom.FRandRange(-.04f,.04f));C->Play(Offset);++Starts;return true;
}
void UArenaAudioSubsystem::TickAudio(gw::Match& Match,int32 Focus,int32 Host,bool Active,double Now){
    const double Started=FPlatformTime::Seconds();PrepareAudio();if(Now<0)Now=Started;
    Match.audio.focusId=Focus;Match.audio.hostId=Host;
    const auto* S=UArenaUserSettings::Get();const float Bgm=Gain(S?S->BgmVolume:.55f,.55f),Sfx=Gain(S?S->SfxVolume:.70f,.70f);
    UpdateMusic(Match,Active,Now,Bgm);Budget.beginFrame(Now);
    StopLaser(Active&&Match.boss.active&&Match.phase!=gw::Phase::Results?Match.boss.attack:gw::BossAttack::Idle);
    for(UAudioComponent* C:Pool){C->SetVolumeMultiplier(Sfx*.08f);if(Sfx<=0||!Active||Match.phase==gw::Phase::Results)C->Stop();}
    uint64 ThisEvents=0,Before=Starts;
    for(const auto& Slot:Match.audio.slots)ThisEvents+=Slot.count;
    // 21 bounded slots, priority-first. No participant scan and no allocation.
    if(Sfx>0&&Active&&Match.phase!=gw::Phase::Results){
        for(int Priority=3;Priority>=0;--Priority)for(int i=0;i<gw::AudioKindCount;++i){
            const auto Kind=static_cast<gw::AudioKind>(i);const auto& Slot=Match.audio.slots[i];
            if(!Slot.count)continue;const bool Important=Slot.important.present;
            if(gw::audioPriority(Kind,Important)!=Priority)continue;
            if(Kind==gw::AudioKind::BossLaserBeam && (!Match.boss.active||Match.boss.attack!=gw::BossAttack::LaserActive))continue;
            if((Kind==gw::AudioKind::BossLaserWindup||Kind==gw::AudioKind::BossLaserRage)&&(!Match.boss.active||Match.boss.attack!=gw::BossAttack::LaserWindup))continue;
            StartEffect(Kind,Priority,Now);
        }
        // Restore a muted/budget-dropped sustained warning at its actual simulation progress.
        if(Match.boss.active&&Match.boss.attack==gw::BossAttack::LaserWindup){
            bool Playing=false;for(int i=0;i<16;++i)Playing|=Pool[i]->IsPlaying()&&(PoolKinds[i]==gw::AudioKind::BossLaserWindup||PoolKinds[i]==gw::AudioKind::BossLaserRage);
            if(!Playing)StartEffect(Match.boss.rage?gw::AudioKind::BossLaserRage:gw::AudioKind::BossLaserWindup,3,Now,static_cast<float>(Match.boss.attackElapsed));
        }
        // Restore a muted or budget-dropped beam while its actual attack continues.
        if(Match.boss.active&&Match.boss.attack==gw::BossAttack::LaserActive){bool Playing=false;for(int i=0;i<16;++i)Playing|=Pool[i]->IsPlaying()&&PoolKinds[i]==gw::AudioKind::BossLaserBeam;if(!Playing)StartEffect(gw::AudioKind::BossLaserBeam,3,Now);}
    }
    Events+=ThisEvents;Dropped+=ThisEvents>Starts-Before?ThisEvents-(Starts-Before):0;Match.audio.clear();
    int ActiveCount=0;for(UAudioComponent* C:Pool)ActiveCount+=C->IsPlaying()?1:0;ActivePeak=FMath::Max(ActivePeak,ActiveCount);
    LastTick=Now;SchedulingMs=(FPlatformTime::Seconds()-Started)*1000;
}
