#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GWNetworkTypes.h"
#include "Core/Prediction.h"
#include "GWController.generated.h"
class AGWWorld;
UCLASS()
class GEOMETRICWARFARE_API AGWController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(Server, Unreliable) void ServerInput(const TArray<FGWInputCommand>& Commands);
    UFUNCTION(Server, Reliable) void ServerAction(uint8 Action, int32 Value);
    UFUNCTION(Server, Reliable) void ServerCharge(uint8 Action, FVector2D Aim);
    // Defense: draw (or repair) a wall on a cell, or erase one of the drawn walls.
    UFUNCTION(Server, Reliable) void ServerPaint(int32 Cell, bool Erase);
    // Defense: pour a measure of this player's ink to a teammate (right button held on them).
    UFUNCTION(Server, Reliable) void ServerPour(int32 Target);
    // The local player's own permission to be thrown (from settings); sent on joining and when it changes.
    void SendThrowable(bool Allowed);
    UFUNCTION(Client, Reliable) void ClientNotice(const FString& Key);
    // A kicked guest goes back to their own hub with a notice (the engine's default does nothing).
    virtual void ClientWasKicked_Implementation(const FText& KickReason) override;
    // The host's packet for this player's own profile (per cleared wave, the clear, a bullseye in the hub):
    // stickers, stars and this player's share of the run. This machine caps it and writes the profile.
    UFUNCTION(Client, Reliable) void ClientProgress(const FGWProgressPacket& Packet);
    // The host has applied these items to the mission that just opened (255 = none): out of the stock they go.
    UFUNCTION(Client, Reliable) void ClientConsume(uint8 ItemA, uint8 ItemB);
    // This player's report of their own profile (on joining and after every change in the hub).
    UFUNCTION(Server, Reliable) void ServerProfile(const FGWProfileDigest& Digest);
    void SendProfile();
    // Development builds: adds stickers to the active profile (console: GWStickers 2000), for playtests of the shop
    // and the research desk (does nothing in shipping builds).
    UFUNCTION(Exec) void GWStickers(int32 Count);
    FString NoticeKey;
    UPROPERTY() TObjectPtr<AGWWorld> WorldView;
    bool GameplayInputAllowed() const;
    bool PredictedPosition(int32 UnitId, FVector& Position) const;
    FVector2D MouseAim = FVector2D(1, 0);
    float ChargeProgress() const;
    // The defense grid cell under the mouse (-1 when off the page) and the stroke being drawn.
    int32 HoverCell = -1;
    bool Painting = false, PaintErase = false;
    bool CursorCell(int32& Cell) const;
    // The teammate under the cursor (ink can be poured to them) and the one being poured to now.
    int32 HoverMate = -1, PourTarget = -1;
    // True while the local player cannot steer (carried, in flight, or lying as bait): no movement prediction.
    bool Pinned = false;
    // Test driver for network stress runs: steers, aims, fires, recruits and throws without a keyboard,
    // a mouse or window focus (headless clients).
    bool Autopilot = false;
    // How far the autopilot has walked (a headless client proves it really played).
    double AutopilotDistance = 0;

private:
    gwgame::Vec2 AutopilotGoal{}, AutopilotLast{};
    double AutopilotTurn = 0, AutopilotAct = 0;
    uint64 AutopilotSeed = 0;
    void DriveAutopilot(const class AGWGameState* S, const FGWUnitView& U, gwgame::Input& In);
    float InputTime = 0;
    double LastInput = 0, LastAction = 0;
    bool UIConsumedMouse = false;
    bool LocalCharging = false;
    double ChargeStarted = 0;
    gwgame::Prediction Prediction;
    uint32 SequenceCounter = 0, PredictionSeed = 0, PredictionWallVersion = 0, LastAuthorityAck = 0;
    int32 PredictedUnit = -1;
    FVector LastAuthorityPosition = FVector::ZeroVector;
    FVector LastAuthorityImpulse = FVector::ZeroVector;
    int32 LastPaintCell = -1;
    double PaintAllowance = 40, LastPaintTime = 0, NextPour = 0, PourAllowance = 8, LastPourTime = 0;
    int32 MateUnderCursor() const;
    void Click();
    void PaintPressed();
    void PaintReleased();
    void PaintTo(int32 Cell);
    void Merge();
    void CallWave();
    void Recruit();
    void Launch();
    void ReleaseLaunch();
    void CancelLaunch();
    FVector2D ReadMouseAim() const;
    void Pause();
    void ZoomIn();
    void ZoomOut();
};
