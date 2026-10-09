#pragma once
#include "CoreMinimal.h"

class ULiveInteractionSubsystem;
struct FLiveSession;
class FJsonObject;

/** Trusted native provider boundary. Implement only against a verified platform SDK. */
class LIVEINTERACTION_API ILiveInteractionProvider
{
public:
    virtual ~ILiveInteractionProvider() = default;
    virtual FString GetPlatformId() const = 0;
    /** Start may remain pending. Only BeginProviderSession establishes an authenticated room. */
    virtual bool Start(ULiveInteractionSubsystem& Host) = 0;
    virtual void Stop() = 0;
    /** True means transport submission only; completion is reported separately. */
    virtual bool SendCommand(const FString& RequestId,const FString& Operation,const TSharedRef<FJsonObject>& Payload) { return false; }
};

class LIVEINTERACTION_API FLiveInteractionProviderRegistry
{
public:
    using FFactory = TFunction<TSharedPtr<ILiveInteractionProvider>()>;
    static bool Register(const FString& PlatformId, FFactory Factory);
    static void Unregister(const FString& PlatformId);
    static TSharedPtr<ILiveInteractionProvider> Create(const FString& PlatformId);
};
