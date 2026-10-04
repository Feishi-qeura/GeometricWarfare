#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "DouyinAvatarRequestTracker.h"
#include "DouyinLiveSubsystem.generated.h"

class IWebSocket;
class UTexture2D;

USTRUCT(BlueprintType)
struct DOUYINLIVEBRIDGE_API FDouyinComment
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString MessageId;
    UPROPERTY(BlueprintReadOnly) FString UserId;
    UPROPERTY(BlueprintReadOnly) FString Nickname;
    UPROPERTY(BlueprintReadOnly) FString AvatarUrl;
    UPROPERTY(BlueprintReadOnly) FString Content;
};

USTRUCT(BlueprintType)
struct DOUYINLIVEBRIDGE_API FDouyinLike
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString MessageId;
    UPROPERTY(BlueprintReadOnly) FString UserId;
    UPROPERTY(BlueprintReadOnly) FString Nickname;
    /** New likes represented by this event, never a lifetime/cumulative total. Valid range 1..100. */
    UPROPERTY(BlueprintReadOnly) int32 Count=1;
};

USTRUCT(BlueprintType)
struct DOUYINLIVEBRIDGE_API FDouyinShare
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString MessageId;
    UPROPERTY(BlueprintReadOnly) FString UserId;
    UPROPERTY(BlueprintReadOnly) FString Nickname;
};

USTRUCT(BlueprintType)
struct DOUYINLIVEBRIDGE_API FDouyinGift
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString MessageId;
    UPROPERTY(BlueprintReadOnly) FString UserId;
    UPROPERTY(BlueprintReadOnly) FString Nickname;
    /** Normalized gift name from the trusted adapter, not viewer comment text. */
    UPROPERTY(BlueprintReadOnly) FString GiftName;
    /** New units in this event, never the cumulative combo total. */
    UPROPERTY(BlueprintReadOnly) int32 Count=1;
};

/** Our relay envelope, not an official SDK wire format. */
class DOUYINLIVEBRIDGE_API FDouyinEventDecoder
{
public:
    static bool Decode(const FString& Json, FString& RoomId, TArray<FDouyinComment>& Comments, FString& Error);
    static bool DecodeEvents(const FString& Json, FString& RoomId, TArray<FDouyinComment>& Comments,
        TArray<FDouyinLike>& Likes, TArray<FDouyinShare>& Shares, FString& Error);
    static bool DecodeEvents(const FString& Json, FString& RoomId, TArray<FDouyinComment>& Comments,
        TArray<FDouyinLike>& Likes, TArray<FDouyinShare>& Shares, TArray<FDouyinGift>& Gifts, FString& Error);
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDouyinCommentEvent, const FDouyinComment&, Comment);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDouyinLikeEvent, const FDouyinLike&, Like);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDouyinShareEvent, const FDouyinShare&, Share);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDouyinGiftEvent, const FDouyinGift&, Gift);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDouyinAvatarEvent, const FString&, UserId, UTexture2D*, Texture);

UCLASS()
class DOUYINLIVEBRIDGE_API UDouyinLiveSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable) FDouyinCommentEvent OnComment;
    UPROPERTY(BlueprintAssignable) FDouyinLikeEvent OnLike;
    UPROPERTY(BlueprintAssignable) FDouyinShareEvent OnShare;
    UPROPERTY(BlueprintAssignable) FDouyinGiftEvent OnGift;
    UPROPERTY(BlueprintAssignable) FDouyinAvatarEvent OnAvatarReady;
    UPROPERTY(BlueprintReadOnly) FString ConnectionStatus = TEXT("LOCAL SIMULATION");

    UFUNCTION(BlueprintCallable) bool SimulateComment(const FString& UserId, const FString& Nickname, const FString& Content);
    UFUNCTION(BlueprintCallable) bool SimulateLike(const FString& UserId, const FString& Nickname, int32 Count=1);
    UFUNCTION(BlueprintCallable) bool SimulateShare(const FString& UserId, const FString& Nickname);
    UFUNCTION(BlueprintCallable) bool SimulateGift(const FString& UserId, const FString& Nickname, const FString& GiftName, int32 Count=1);
    /** Call on the game thread from a future authorized native SDK adapter. Does not claim SDK availability. */
    bool DeliverComment(const FDouyinComment& Comment);
    bool DeliverLike(const FDouyinLike& Like);
    bool DeliverShare(const FDouyinShare& Share);
    bool DeliverGift(const FDouyinGift& Gift);
    UFUNCTION(BlueprintCallable) bool ConnectRelay(const FString& Url, const FString& RoomId);
    static bool IsValidRelayEndpoint(const FString& Url, const FString& RoomId);
    UFUNCTION(BlueprintCallable) void DisconnectRelay();
    UFUNCTION(BlueprintCallable) void RequestAvatar(const FString& UserId, const FString& Url);
    bool IsRelayMode() const { return bRelayMode; }
    virtual void Deinitialize() override;
private:
    friend class FDouyinGiftDeliveryTest;
    TSharedPtr<IWebSocket> Socket;
    TSet<FString> SeenIds;
    TArray<FString> IdOrder;
    TArray<FHttpRequestPtr> Requests;
    FDouyinAvatarRequestTracker AvatarRequests;
    UPROPERTY(Transient) TMap<FString, TObjectPtr<UTexture2D>> AvatarCache;
    FString ExpectedRoom;
    bool bRelayMode = false;
    int32 SessionGeneration = 0;
    void Receive(const FString& Json);
    bool AcceptEventId(const FString& MessageId);
};
