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
    bool bDebugBot=false;
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
enum class EArenaNoticeVisual { Weapon,Revive,Base,Evolution,Boss,Hero,People,TeamSelected,WeaponSwitch,TemporaryWeapon,Gather };
struct FGiftNotice {
    FString UserId,ViewerName,GiftName,WeaponName,Detail;
    gw::WeaponKind WeaponKind=gw::WeaponKind::Pistol;
    EArenaNoticeVisual Visual=EArenaNoticeVisual::Weapon;
    int32 Team=0;
    int64 Count=1;
    float Age=0;
    bool Active=false,Elaborate=false,Success=true,CountCapped=false,bIsTestData=false;
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
    bool IsGMEnabled() const;
    void AddGMBots(int32 Team,int32 Count=1);
    bool SendGMGift(const FString& Identity,const FString& GiftName,int32 Count=1);
    bool BindGMIdentity(const FString& DouyinId,const FString& LiveUserId);
    bool PrepareGMGiftTarget(const FString& Identity,bool DestroyBase=false);
    FString GenerateRifleCode();
    FString LastRifleCode;
    const FGiftNotice& GetGiftNotice() const { for(const auto& N:GiftNotices)if(N.Active)return N;return GiftNotice; }
    const TArray<FGiftNotice>& GetGiftNotices() const { return GiftNotices; }
    const TArray<FGiftNotice>& GetRewardNotices() const { return RewardNotices; }
    const TArray<FGiftNotice>& GetJoinNotices() const { return JoinNotices; }
    void SetNoticeCompactLayout(bool Compact);
    UTexture2D* GetNoticeAvatar(const FString& UserId);
    UTexture2D* GetGiftIcon(const FString& GiftName);
    FString LastEvent=TEXT("发送“加入” → 1 红方 / 2 蓝方 → 自动战斗夺分");
    FString DemoCaption;
    float RunningTime=0,CameraZoom=1,DemoSpeed=1;
    FVector2D CameraCenter=FVector2D(gw::World::Size*.5,gw::World::Size*.5);
    int32 FocusBodyId=-1,PendingMockUsers=0;
    double SimulationMs=0,RenderMs=0,FrameMs=0;
    bool bShowControls=true,bRecording=false;
    bool IsPlatformTestSession() const {return bPlatformTestSession;}
private:
    friend class FArenaFollowQualificationTest;
    friend class FArenaDebugToolsTest;
    friend class FArenaGiftCombatTest;
    friend class FArenaDualHandIntegrationTest;
    friend class FArenaSocialCombatTest;
    friend class FArenaNotificationTest;
    friend class FArenaAdvancedLiveTest;
    friend class FArenaGatherNoticeTest;
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
    TArray<FGiftNotice> GiftNotices,RewardNotices,RewardNoticeQueue,JoinNotices,JoinNoticeQueue;
    bool bCompactNoticeLayout=false;
    TMap<FString,int64> PersonalLikes;
    TMap<int32,int32> KillStreaks;
    int32 NextBodyId=1,DemoCounter=0,RecordFrame=0;
    int32 HostBodyId=-1;
    UPROPERTY(Transient) FViewerState HostViewer;
    TMap<FString,FString> GMIdentityBindings;
    bool bApplyingGMGift=false;
    void UpdateHostProfile();
    FString CombatVisualMode;
    float VisualCaptureAt=6;
    bool bSimulationPaused=false,bVisualTest=false,bScreenshotTaken=false,bStressTest=false,bCompactOverviewPreview=false;
    bool bCombatStress=false,bCombatStressPrepared=false;
    double LastWall=0,StressStart=0,RecordingStartWall=0;
    TArray<double> StressFrames,StressSimulation,StressRender,StressAudio;
    bool bAudioStress=false,bAudioDisabled=false;
    int32 StressMinAlive=gw::Match::ViewerCapacity,StressMaxAlive=0;
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
    // SDK review gifts can influence combat in later rounds. Keep the session
    // excluded from statistics until its gameplay and temporary ownership reset.
    bool bPlatformTestSession=false;
    TMap<FString,uint8> PlatformTestWeaponUnlocks;
    bool RememberWeapon(const FString& UserId,gw::WeaponKind Kind);
    void RestoreWeapons(const FViewerState& Viewer,bool RestoreLeftSelection=false);
    void RememberSelectedLeftWeapon(const FViewerState& Viewer);
    bool HandleWeaponCommand(const FString& Command,const FViewerState& Viewer);
    void TickGiftNotice(float Dt);
    void EnqueueGiftNotice(const FString& Name,const FString& Gift,const FString& Weapon);
    void EnqueueGiftNotice(FGiftNotice Notice);
    void EnqueueRewardNotice(const FString& UserId,gw::WeaponKind Kind,const FString& Reason,EArenaNoticeVisual Visual=EArenaNoticeVisual::Weapon);
    void QueueNotice(TArray<FGiftNotice>& Active,TArray<FGiftNotice>& Queue,FGiftNotice Notice,int32 Lanes,float Lifetime);
    void TickNoticeQueue(TArray<FGiftNotice>& Active,TArray<FGiftNotice>& Queue,float Dt,float Lifetime);
    void EnqueueJoinNotice(const FViewerState& Viewer,bool TeamSelection=false);
    void EnqueueWeaponSwitchNotice(const FViewerState& Viewer,gw::WeaponKind Kind);
    void PrepareGiftVisualTest();
    void ApplyComment(const FLiveComment& Comment);
    void ApplyGift(const FLiveGift& Gift);
    void QueueHandledLiveEvent(const FLiveSession& Session,const FString& MessageId,const FString& MessageType);
    void SubmitRenderedLiveAcks();
    void HandleLiveAckResult(const FLiveSession& Session,const FString& RequestId,bool bSuccess,int32 ErrorCode);
    void UpdateLiveViewer(const FString& UserId);
    void ApplyFollowQualification(const FString& UserId);
};
