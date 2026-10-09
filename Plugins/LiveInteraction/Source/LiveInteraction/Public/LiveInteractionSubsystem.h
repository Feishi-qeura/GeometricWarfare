#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "LiveAvatarRequestTracker.h"
#include "LiveInteractionProvider.h"
#include "LiveInteractionSubsystem.generated.h"

class IWebSocket;
class UTexture2D;

/** Opaque event provenance; never contains platform launch credentials. */
USTRUCT(BlueprintType)
struct LIVEINTERACTION_API FLiveSession
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString PlatformId;
    UPROPERTY(BlueprintReadOnly) FString AppId;
    UPROPERTY(BlueprintReadOnly) FString RoomId;
    UPROPERTY(BlueprintReadOnly) FString AnchorUserId;
    UPROPERTY(BlueprintReadOnly) int64 Generation=0;
    UPROPERTY(BlueprintReadOnly) bool bLocalTest=false;
    UPROPERTY() FGuid Nonce;
};

USTRUCT(BlueprintType)
struct LIVEINTERACTION_API FLiveComment
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FLiveSession Session;
    UPROPERTY(BlueprintReadOnly) FString MessageId;
    UPROPERTY(BlueprintReadOnly) FString UserId;
    UPROPERTY(BlueprintReadOnly) FString Nickname;
    UPROPERTY(BlueprintReadOnly) FString AvatarUrl;
    UPROPERTY(BlueprintReadOnly) FString Content;
};

USTRUCT(BlueprintType)
struct LIVEINTERACTION_API FLiveLike
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FLiveSession Session;
    UPROPERTY(BlueprintReadOnly) FString MessageId;
    UPROPERTY(BlueprintReadOnly) FString UserId;
    UPROPERTY(BlueprintReadOnly) FString Nickname;
    /** Positive new-unit delta; only the explicit local protocol is limited to 100. */
    UPROPERTY(BlueprintReadOnly) int64 Count=1;
};

USTRUCT(BlueprintType)
struct LIVEINTERACTION_API FLiveShare
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FLiveSession Session;
    UPROPERTY(BlueprintReadOnly) FString MessageId;
    UPROPERTY(BlueprintReadOnly) FString UserId;
    UPROPERTY(BlueprintReadOnly) FString Nickname;
};

USTRUCT(BlueprintType)
struct LIVEINTERACTION_API FLiveGift
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FLiveSession Session;
    UPROPERTY(BlueprintReadOnly) FString MessageId;
    UPROPERTY(BlueprintReadOnly) FString UserId;
    UPROPERTY(BlueprintReadOnly) FString Nickname;
    /** Normalized gift name from the trusted adapter, not viewer comment text. */
    UPROPERTY(BlueprintReadOnly) FString GiftName;
    /** New units in this event, never the cumulative combo total. */
    UPROPERTY(BlueprintReadOnly) int64 Count=1;
    /** Official review/self-check data: show gameplay, exclude permanent purchases and statistics. */
    UPROPERTY(BlueprintReadOnly) bool bIsTestData=false;
};

USTRUCT(BlueprintType)
struct LIVEINTERACTION_API FLiveFollow
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FLiveSession Session;
    UPROPERTY(BlueprintReadOnly) FString MessageId;
    UPROPERTY(BlueprintReadOnly) FString UserId;
    UPROPERTY(BlueprintReadOnly) FString Nickname;
    UPROPERTY(BlueprintReadOnly) FString AvatarUrl;
    UPROPERTY(BlueprintReadOnly) FString TargetUserId;
    UPROPERTY(BlueprintReadOnly) int32 Action=1;
};

USTRUCT(BlueprintType)
struct LIVEINTERACTION_API FLivePresence
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FLiveSession Session;
    UPROPERTY(BlueprintReadOnly) FString MessageId;
    UPROPERTY(BlueprintReadOnly) FString UserId;
    UPROPERTY(BlueprintReadOnly) FString Nickname;
    UPROPERTY(BlueprintReadOnly) FString AvatarUrl;
    UPROPERTY(BlueprintReadOnly) int64 TimestampMs=0;
    UPROPERTY(BlueprintReadOnly) int32 EnterType=1;
    UPROPERTY(BlueprintReadOnly) int32 FollowStatus=0;
    UPROPERTY(BlueprintReadOnly) FString InviterId;
    UPROPERTY(BlueprintReadOnly) int32 EnterRoomScene=0;
};

USTRUCT(BlueprintType)
struct LIVEINTERACTION_API FLiveTeamSelection
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FLiveSession Session;
    UPROPERTY(BlueprintReadOnly) FString MessageId;
    UPROPERTY(BlueprintReadOnly) FString UserId;
    UPROPERTY(BlueprintReadOnly) FString Nickname;
    UPROPERTY(BlueprintReadOnly) FString AvatarUrl;
    UPROPERTY(BlueprintReadOnly) FString GroupId;
};

/** Our relay envelope, not an official SDK wire format. */
class LIVEINTERACTION_API FLiveDevEventDecoder
{
public:
    static bool Decode(const FString& Json, FString& RoomId, TArray<FLiveComment>& Comments, FString& Error);
    static bool DecodeEvents(const FString& Json, FString& RoomId, TArray<FLiveComment>& Comments,
        TArray<FLiveLike>& Likes, TArray<FLiveShare>& Shares, FString& Error);
    static bool DecodeEvents(const FString& Json, FString& RoomId, TArray<FLiveComment>& Comments,
        TArray<FLiveLike>& Likes, TArray<FLiveShare>& Shares, TArray<FLiveGift>& Gifts, FString& Error);
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLiveCommentEvent, const FLiveComment&, Comment);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLiveLikeEvent, const FLiveLike&, Like);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLiveShareEvent, const FLiveShare&, Share);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLiveGiftEvent, const FLiveGift&, Gift);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLiveFollowEvent, const FLiveFollow&, Follow);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLivePresenceEvent, const FLivePresence&, Presence);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLiveTeamSelectionEvent, const FLiveTeamSelection&, TeamSelection);
DECLARE_MULTICAST_DELEGATE_FourParams(FLiveCommandResultEvent,const FLiveSession&,const FString&,bool,int32);
/** Native provider reply data never includes launch or backend credentials. */
struct FLiveCommandReply {
    FLiveSession Session;
    FString RequestId;
    bool bSuccess=false;
    int32 ErrorCode=0;
    TSharedPtr<FJsonObject> Data;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FLiveCommandReplyEvent,const FLiveCommandReply&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLiveAvatarEvent, const FString&, UserId, UTexture2D*, Texture);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FLiveSessionChangedEvent);

UCLASS()
class LIVEINTERACTION_API ULiveInteractionSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable) FLiveCommentEvent OnComment;
    UPROPERTY(BlueprintAssignable) FLiveLikeEvent OnLike;
    UPROPERTY(BlueprintAssignable) FLiveShareEvent OnShare;
    UPROPERTY(BlueprintAssignable) FLiveGiftEvent OnGift;
    UPROPERTY(BlueprintAssignable) FLiveFollowEvent OnFollow;
    UPROPERTY(BlueprintAssignable) FLivePresenceEvent OnPresence;
    UPROPERTY(BlueprintAssignable) FLiveTeamSelectionEvent OnTeamSelection;
    FLiveCommandResultEvent OnCommandResult;
    FLiveCommandReplyEvent OnCommandReply;
    UPROPERTY(BlueprintAssignable) FLiveAvatarEvent OnAvatarReady;
    UPROPERTY(BlueprintAssignable) FLiveSessionChangedEvent OnSessionChanged;
    UPROPERTY(BlueprintReadOnly) FString ConnectionStatus=TEXT("SDK_UNAVAILABLE");

    UFUNCTION(BlueprintCallable) bool SimulateComment(const FString& UserId,const FString& Nickname,const FString& Content);
    UFUNCTION(BlueprintCallable) bool SimulateLike(const FString& UserId,const FString& Nickname,int32 Count=1);
    UFUNCTION(BlueprintCallable) bool SimulateShare(const FString& UserId,const FString& Nickname);
    UFUNCTION(BlueprintCallable) bool SimulateGift(const FString& UserId,const FString& Nickname,const FString& GiftName,int32 Count=1);
    /** Provider events require the current session returned to the registered provider. */
    bool DeliverComment(const FLiveComment& Comment);
    bool DeliverLike(const FLiveLike& Like);
    bool DeliverShare(const FLiveShare& Share);
    bool DeliverGift(const FLiveGift& Gift);
    bool DeliverFollow(const FLiveFollow& Follow);
    bool DeliverPresence(const FLivePresence& Presence);
    bool DeliverTeamSelection(const FLiveTeamSelection& TeamSelection);
    FString SubmitCommand(const FString& Operation,const TSharedRef<FJsonObject>& Payload);
    void ReportCommandResult(ILiveInteractionProvider& Source,const FString& RequestId,bool bSuccess,int32 ErrorCode,TSharedPtr<FJsonObject> Data=nullptr);
    /** True means ACK submitted, never platform confirmation. Only official message types are accepted. */
    bool NotifyEventHandled(const FLiveSession& Session,const FString& MessageId,const FString& MessageType);
    bool StartPlatform(const FString& PlatformId);
    FLiveSession BeginProviderSession(ILiveInteractionProvider& Source,const FString& AppId,const FString& RoomId,const FString& AnchorUserId=FString());
    void EndProviderSession(ILiveInteractionProvider& Source,const FString& Status);
    bool IsEventFromCurrentSession(const FLiveSession& Session) const;
    const FLiveSession& GetCurrentSession() const { return CurrentSession; }
    void SetAnchorProfile(const FLiveSession& Session,const FString& Nickname,const FString& AvatarUrl);
    const FString& GetAnchorNickname() const { return AnchorNickname; }
    FString GetScopedUserId(const FString& UserId) const;
    bool IsLocalTestMode() const { return bLocalTestMode; }
    bool IsLiveMode() const { return !bLocalTestMode; }
    bool IsConnected() const { return !bLocalTestMode && Provider.IsValid() && CurrentSession.Nonce.IsValid(); }
    static bool ShouldEnableLocalTest(const FString& CommandLine,bool bShipping);
    /** Development custom protocol only. Never a LiveOpenSDK connection. */
    UFUNCTION(BlueprintCallable) bool ConnectRelay(const FString& Url,const FString& RoomId);
    static bool IsValidRelayEndpoint(const FString& Url,const FString& RoomId);
    UFUNCTION(BlueprintCallable) void DisconnectRelay();
    UFUNCTION(BlueprintCallable) void RequestAvatar(const FString& UserId,const FString& Url);
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
private:
    FString AnchorNickname;
    friend class FLiveInteractionTestAdapter;
    TSharedPtr<ILiveInteractionProvider> Provider;
    TSharedPtr<IWebSocket> Socket;
    TSet<FString> SeenIds;
    TArray<FString> IdOrder;
    TArray<FHttpRequestPtr> Requests;
    FLiveAvatarRequestTracker AvatarRequests;
    UPROPERTY(Transient) TMap<FString,TObjectPtr<UTexture2D>> AvatarCache;
    FLiveSession CurrentSession;
    TMap<FString,FLiveSession> PendingCommands;
    FString SelectedPlatform=TEXT("douyin");
    bool bLocalTestMode=false;
    bool bDevRelayMode=false;
    int64 SessionGeneration=0;
    void EnableLocalTest();
    void ChangeSession(const FString& PlatformId,const FString& AppId,const FString& RoomId,bool bLocalTest,const FString& AnchorUserId=FString());
    void InvalidateSession(bool bNotify=true);
    void CloseSocket();
    void Receive(const FString& Json);
    bool AcceptEventId(const FString& MessageId);
};
