#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DouyinLiveSubsystem.h"
#include "Simulation/ArenaMatch.h"
#include "Simulation/ScrollFeed.h"
#include "Simulation/DamageNumbers.h"
#include "ArenaProgressSave.h"
#include "ArenaGameMode.generated.h"
USTRUCT()
struct FViewerState {
    GENERATED_BODY()
    UPROPERTY() FString UserId;
    UPROPERTY() FString Name;
    UPROPERTY() TObjectPtr<UTexture2D> Avatar;
    int32 BodyId=0,Team=0;
    float HighlightUntil=0;
};
struct FGiftNotice {
    FString ViewerName,GiftName,WeaponName;
    float Age=0;
    bool Active=false;
};
UCLASS()
class GEOMETRICWARFARE_API AArenaGameMode : public AGameModeBase {
    GENERATED_BODY()
public:
    AArenaGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    const gw::World& GetArena() const { return Match.world; }
    const gw::Match& GetMatch() const { return Match; }
    const TMap<FString,FViewerState>& GetViewers() const { return Viewers; }
    const gw::ScrollFeed<FString>& GetFeed() const { return Feed; }
    const gw::DamageNumbers& GetDamageNumbers() const { return DamageNumbers; }
    UDouyinLiveSubsystem* GetBridge() const;
    const FViewerState* FindViewer(int32 BodyId) const;
    FString ViewerName(int32 BodyId) const;
    void AddMockUsers(int32 Count);
    void ResetArena();
    void TogglePaused() { bSimulationPaused=!bSimulationPaused; }
    bool IsSimulationPaused() const { return bSimulationPaused; }
    void ZoomBy(float Factor);
    void FocusViewer(int32 Id);
    void Overview();
    void MoveCamera(float X,float Y);
    void DemoAction(const FString& Action);
    void HostAssist(int32 Team);
    void SetDemoSpeed(float Speed);
    void FocusLocalViewer(const FString& Nickname);
    void SimulateGift(const FString& Nickname,const FString& GiftName);
    FString GenerateRifleCode();
    FString LastRifleCode;
    const FGiftNotice& GetGiftNotice() const { return GiftNotice; }
    UTexture2D* GetGiftIcon(const FString& GiftName);
    FString LastEvent=TEXT("发送“加入” → 1 红方 / 2 蓝方 → 自动战斗夺分");
    FString DemoCaption;
    float RunningTime=0,CameraZoom=1,DemoSpeed=1;
    FVector2D CameraCenter=FVector2D(4000,4000);
    int32 FocusBodyId=-1,PendingMockUsers=0;
    double SimulationMs=0,RenderMs=0,FrameMs=0;
    bool bShowControls=true,bRecording=false;
private:
    friend class FArenaGiftCombatTest;
    friend class FArenaSocialCombatTest;
    gw::Match Match;
    gw::ScrollFeed<FString> Feed;
    gw::DamageNumbers DamageNumbers;
    UPROPERTY() TMap<FString,FViewerState> Viewers;
    TMap<int32,FString> Identities;
    UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> PlaceholderPool;
    UPROPERTY(Transient) TMap<FString,TObjectPtr<UTexture2D>> GiftIcons;
    UPROPERTY(Transient) TObjectPtr<UArenaProgressSave> Progress;
    FString ProgressSlot=TEXT("ArenaWeaponProgress_v1");
    bool bProgressWritable=true;
    bool bProgressDirty=false;
    float ProgressRetryRemaining=5;
    FGiftNotice GiftNotice;
    TArray<FGiftNotice> GiftNoticeQueue;
    int32 NextBodyId=1,DemoCounter=0,RecordFrame=0;
    int32 HostBodyId=-1;
    FString CombatVisualMode;
    float VisualCaptureAt=6;
    bool bSimulationPaused=false,bVisualTest=false,bScreenshotTaken=false,bStressTest=false;
    bool bCombatStress=false,bCombatStressPrepared=false;
    double LastWall=0,StressStart=0,RecordingStartWall=0;
    TArray<double> StressFrames,StressSimulation,StressRender;
    int32 StressMinAlive=5000,StressMaxAlive=0;
    int32 StressBossFrames=0,StressMaxShots=0,StressMaxSwords=0;
    int32 DemoStage=0;
    void PumpMockUsers(int32 Budget);
    void TickRecording();
    void ConsumeEvents();
    void ConsumeDamage();
    void SaveStressReport();
    UFUNCTION() void HandleComment(const FDouyinComment& Comment);
    UFUNCTION() void HandleLike(const FDouyinLike& Like);
    UFUNCTION() void HandleShare(const FDouyinShare& Share);
    UFUNCTION() void HandleGift(const FDouyinGift& Gift);
    UFUNCTION() void HandleAvatar(const FString& UserId,UTexture2D* Texture);
    UTexture2D* MakePlaceholder(int32 Seed);
    void LoadProgress();
    bool SaveProgress();
    void TickProgressSave(float Dt);
    FString ProgressKey(const FString& UserId) const;
    bool RememberWeapon(const FString& UserId,gw::WeaponKind Kind);
    void RestoreWeapons(const FViewerState& Viewer);
    bool HandleWeaponCommand(const FString& Command,const FViewerState& Viewer);
    void TickGiftNotice(float Dt);
    void EnqueueGiftNotice(const FString& Name,const FString& Gift,const FString& Weapon);
    void PrepareGiftVisualTest();
};
