#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LiveInteractionSubsystem.h"
#include "Simulation/ArenaMatch.h"
#include "Simulation/ScrollFeed.h"
#include "Simulation/DamageNumbers.h"
#include "ArenaProgressSave.h"
#include "ArenaGameMode.generated.h"
class FArenaLiveRoundReporter;
USTRUCT()
struct FViewerState {
    GENERATED_BODY()
    UPROPERTY() FString UserId;
    UPROPERTY() FString Name;
    UPROPERTY() TObjectPtr<UTexture2D> Avatar;
    int32 BodyId=0,Team=0;
    float HighlightUntil=0;
    bool bPresent=false,bFollowsAnchor=false;
    int64 PresenceTimestampMs=-1;
};
USTRUCT()
struct FViewerLiveState {
    GENERATED_BODY()
    UPROPERTY() FString Nickname;
    UPROPERTY() FString AvatarUrl;
    UPROPERTY() TObjectPtr<UTexture2D> Avatar;
    bool bPresent=false,bFollowsAnchor=false;
    int32 FollowStatus=0,EnterRoomScene=0;
    int64 TimestampMs=-1;
    FString InviterId;
};
struct FPendingLiveAck { FLiveSession Session;FString MessageId,MessageType;int32 Attempts=0;double RetryAfter=0; };
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
    ULiveInteractionSubsystem* GetBridge() const;
    const TMap<FString,FViewerLiveState>& GetLiveViewerStates() const { return LiveViewerStates; }
    /** Called after HUD draw submission; NullRHI never acknowledges visible fulfilment. */
    void FlushRenderedLiveEvents();
    int64 LiveRoundId=0;
    void TickLiveRound();
    void ResetLiveRound();
    const FViewerState* FindViewer(int32 BodyId) const;
    FString ViewerName(int32 BodyId) const;
    void AddMockUsers(int32 Count);
    void ResetArena();
    void TogglePaused() { if(GetBridge()->IsLocalTestMode()) bSimulationPaused=!bSimulationPaused; }
    bool IsSimulationPaused() const { return bSimulationPaused; }
    void ZoomBy(float Factor);
    void FocusViewer(int32 Id);
    void Overview();
    void MoveCamera(float X,float Y);
    void DemoAction(const FString& Action);
    void HostAssist(int32 Team);
    bool CanHostAssist() const;
    int32 GetHostTeam() const;
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
    friend class FArenaAdvancedLiveTest;
    friend class FArenaLiveRoundTest;
    friend class FArenaBackendRoundTest;
    friend class FArenaAdaptiveHostTest;
    gw::Match Match;
    UPROPERTY(Transient) TObjectPtr<class UArenaAudioSubsystem> GameAudio;
    gw::ScrollFeed<FString> Feed;
    gw::DamageNumbers DamageNumbers;
    UPROPERTY() TMap<FString,FViewerState> Viewers;
    UPROPERTY() TMap<FString,FViewerLiveState> LiveViewerStates;
    TArray<FPendingLiveAck> PendingLiveAcks;
    TMap<FString,FPendingLiveAck> InFlightLiveAcks;
    FDelegateHandle LiveAckResultHandle;
    TSharedPtr<FArenaLiveRoundReporter> LiveRoundReporter;
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
    TArray<double> StressFrames,StressSimulation,StressRender,StressAudio;
    bool bAudioStress=false,bAudioDisabled=false;
    int32 StressMinAlive=5000,StressMaxAlive=0;
    int32 StressBossFrames=0,StressMaxShots=0,StressMaxSwords=0;
    int32 DemoStage=0;
    void PumpMockUsers(int32 Budget);
    void TickRecording();
    void ConsumeEvents();
    void ConsumeDamage();
    void SaveStressReport();
    UFUNCTION() void HandleComment(const FLiveComment& Comment);
    UFUNCTION() void HandleLike(const FLiveLike& Like);
    UFUNCTION() void HandleShare(const FLiveShare& Share);
    UFUNCTION() void HandleGift(const FLiveGift& Gift);
    UFUNCTION() void HandleFollow(const FLiveFollow& Follow);
    UFUNCTION() void HandlePresence(const FLivePresence& Presence);
    UFUNCTION() void HandleTeamSelection(const FLiveTeamSelection& Selection);
    UFUNCTION() void HandleAvatar(const FString& UserId,UTexture2D* Texture);
    UFUNCTION() void HandleSessionChanged();
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
    void ApplyComment(const FLiveComment& Comment);
    void ApplyGift(const FLiveGift& Gift);
    void QueueHandledLiveEvent(const FLiveSession& Session,const FString& MessageId,const FString& MessageType);
    void SubmitRenderedLiveAcks();
    void HandleLiveAckResult(const FLiveSession& Session,const FString& RequestId,bool bSuccess,int32 ErrorCode);
    void UpdateLiveViewer(const FString& UserId);
    void ApplyFollowQualification(const FString& UserId);
};
