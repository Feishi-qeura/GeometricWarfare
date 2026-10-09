#pragma once
#include "CoreMinimal.h"
#include "LiveInteractionSubsystem.h"
#include "Dom/JsonObject.h"
#include "ArenaLiveRoundOutbox.h"
class AArenaGameMode;

/** Ordered, acknowledged platform round reporting; gameplay scores are snapshots. */
class FArenaLiveRoundReporter
{
public:
    explicit FArenaLiveRoundReporter(AArenaGameMode& InGame);
    ~FArenaLiveRoundReporter();
    void Tick();
    bool IsCurrentRoundActive() const;
private:
    friend class FArenaLiveRoundTest;
    friend class FArenaBackendRoundTest;
    friend class FArenaAdvancedLiveTest;
    using FStep=FArenaRoundReportStep;
    TWeakObjectPtr<AArenaGameMode> Game;
    TWeakObjectPtr<ULiveInteractionSubsystem> Bridge;
    FLiveSession Session;
    FDelegateHandle ResultHandle;
    TArray<FStep> Steps;
    TMap<FString,FString> ReportedGroups;
    FString RequestId,RedGroup,BlueGroup,GrayGroup;
    int32 MatchRound=0,Attempts=0;
    int64 RoundId=0,StartedAt=0;
    bool bStarted=false,bEndQueued=false;
    bool bBackendRequired=true,bReplay=false,bRecoveryBlocked=false;
    FString OutboxPath;
    int32 ConfirmedSteps=0;
    double RetryAfter=0,SentAt=0;
    void Result(const FLiveCommandReply& Reply);
    bool ApplyBackendReceipt(const TSharedPtr<FJsonObject>& Data);
    bool EnsurePersisted();
    bool Recover();
    bool StartRound();
    void EndRound();
    void QueueGroup();
    void SendNext();
};
