#pragma once
#include "CoreMinimal.h"
#include "LiveInteractionSubsystem.h"
#include "Dom/JsonObject.h"

struct FArenaRoundReportStep {FString Operation;TSharedPtr<FJsonObject> Payload;};
struct FArenaStoredRound {
    FString Path;
    int64 RoundId=0,StartedAt=0;
    int32 MatchRound=0,Cursor=0;
    TArray<FArenaRoundReportStep> Steps;
    TSharedPtr<FJsonObject> BackendReceipt;
};
/** Frozen tokenless snapshot + append-only successful receipts. */
class FArenaLiveRoundOutbox {
public:
    static bool Save(const FLiveSession& Session,int64 RoundId,int64 StartedAt,int32 MatchRound,const TArray<FArenaRoundReportStep>& Steps,FString& Path);
    static bool AppendReceipt(const FString& Path,int32 Cursor,const FString& Operation,TSharedPtr<FJsonObject> Data=nullptr);
    /** false + empty Error = no outstanding job; nonempty Error = fail closed. */
    static bool LoadOldest(const FLiveSession& Session,FArenaStoredRound& Record,FString& Error);
    static bool Remove(const FString& Path);
};
