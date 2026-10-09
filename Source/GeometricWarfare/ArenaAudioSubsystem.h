#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Simulation/ArenaMatch.h"
#include "ArenaAudioSubsystem.generated.h"
class UAudioComponent;
class USoundWave;
UCLASS()
class GEOMETRICWARFARE_API UArenaAudioSubsystem : public UWorldSubsystem {
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void PrepareAudio();
    void TickAudio(gw::Match& Match,int32 Focus,int32 Host,bool Active,double Now=-1);
    void HostAssist(double Now=-1);
    void ResetAudio();
    double SchedulingMs=0;
    uint64 Events=0,Starts=0,Dropped=0;
    int32 ActivePeak=0,MissingAssets=0;
    bool HasAudioDevice() const;
private:
    friend struct FArenaAudioTestAccess;
    friend struct FArenaAudioGainTestAccess;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundWave>> Waves;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> Pool;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> Music;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> Assist;
    std::array<int,16> Priorities{};
    std::array<gw::AudioKind,16> PoolKinds{};
    gw::AudioBudget Budget;
    FRandomStream PitchRandom{1052026};
    int32 CurrentPhase=-1,CurrentRound=-1,MainSlot=0;
    bool bPrepared=false;
    double TransitionAt=0,LastTick=-1,AssistAt=-1000,AssistEnd=-1000;
    float MainWeights[2]={0,0},FromWeights[2]={0,0};
    float Duck=1;
    UAudioComponent* MakeComponent();
    bool StartEffect(gw::AudioKind Kind,int Priority,double Now,float Offset=0);
    void StopLaser(gw::BossAttack Attack);
    void UpdateMusic(const gw::Match& Match,bool Active,double Now,float Gain);
};
