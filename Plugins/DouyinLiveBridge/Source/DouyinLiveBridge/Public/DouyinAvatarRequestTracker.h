#pragma once
#include "CoreMinimal.h"
/** Tracks the latest requested avatar per viewer, including immediate cache hits. */
class FDouyinAvatarRequestTracker
{
public:
    uint64 Begin(const FString& UserId,const FString& Url) {
        const uint64 Ticket=++Sequence;
        Pending.Add(UserId,FRequest{Url,Ticket});
        return Ticket;
    }
    bool Complete(const FString& UserId,uint64 Ticket) {
        const FRequest* Request=Pending.Find(UserId);
        if(!Request || Request->Ticket!=Ticket) return false;
        Pending.Remove(UserId); return true;
    }
    bool IsPending(const FString& UserId,const FString& Url) const {
        const FRequest* Request=Pending.Find(UserId);
        return Request && Request->Url==Url;
    }
    // Keep sequence monotonic across rooms, even when a viewer and URL are reused.
    void Reset() { Pending.Empty(); }
private:
    struct FRequest { FString Url; uint64 Ticket; };
    TMap<FString,FRequest> Pending;
    uint64 Sequence=0;
};
