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
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(Server, Unreliable) void ServerInput(const TArray<FGWInputCommand>& Commands);
    // The dodge's cooldown as predicted here (the bar over the player's head).
    double DodgeCooldown() const
    {
        return Prediction.dashCooldown;
    }
    // The whole cooldown a dodge leaves (research shortens it).
    double DodgeRest() const
    {
        return Prediction.dashRest;
    }
    // A defense page's build ring (GWBuildRing.cpp): the floor cell it is open on (-1 while closed), and the build its
    // hovered slot would make there (gwgame::TowerKind, or -1) with the sides it would have, whose reach the world
    // sketches on the floor.
    int32 RingCell = -1, RingPreview = -1, RingPreviewSides = 4;
    // Build ring actions: a tower (or wire) of Kind on Cell, one more side or level on the tower there (the same
    // call with its own kind), or the tower or wire on Cell rubbed out for its ink.
    void BuildAt(int32 Cell, int32 Kind)
    {
        ServerAction(23, Cell | (Kind << 16));
    }
    void RemoveAt(int32 Cell)
    {
        ServerPaint(Cell, true);
    }
    // Shift was pressed: the next input command dodges.
    bool DodgeQueued = false;
    void Dodge();
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
    // The point under the mouse: on top of the box's platform where the cursor is over it, otherwise on the floor
    // (Z is the height it was found at).
    bool CursorWorld(FVector& Hit) const;
    // The rocket launcher's charge while the local player holds the trigger (0..1), or -1.
    float RocketProgress() const;
    // Test hooks (there is no mouse or keyboard under -RenderOffscreen): a rocket charge to show (0-1, -1 off) and R held
    // for an ink mend.
    float RocketOverride = -1;
    bool MendOverride = false;
    // R held for an ink mend (the world draws the circle it fills).
    bool MendHeld() const;
    // Spectating (2026-11-17): while the local player is down, the teammate the camera follows (-1 none) and how many
    // there are to step through with the mouse buttons.
    int32 SpectateId = -1, SpectateCount = 0;
    void StepSpectate(int32 Step);
    // The teammate under the cursor (ink can be poured to them) and the one being poured to now.
    int32 HoverMate = -1, PourTarget = -1;
    // True while the local player cannot steer (carried, in flight, or lying as bait): no movement prediction.
    bool Pinned = false;
    // The expedition (design v2.1 4.1): picks a hotbar slot (1-5, the wheel), drops what is in hand (G; one gun of a
    // pair), drops stickers or a look for a teammate (the kit panel: 0 twenty-five stickers, 1 all of them, 2 + a look's
    // category), and submits a revive drawing (points in the sheet's unit square, packed x << 16 | y in 1/65535ths).
    void PickSlot(int32 Slot)
    {
        ServerAction(25, Slot);
    }
    void DropHeld()
    {
        if (GameplayInputAllowed())
            ServerAction(26, 0);
    }
    void DropFromKit(int32 What)
    {
        ServerAction(27, What);
    }
    UFUNCTION(Server, Reliable) void ServerRevive(int32 Target, const TArray<uint32>& Points, uint8 Pen);
    // The hotbar slot this machine last asked for (shown at once, before the host's answer comes back), and asking for one
    // (the 1-5 keys, the wheel and a click on the HUD's hotbar).
    int32 AskedSlot = -1;
    double AskedAt = -10;
    void HotbarKey(int32 Slot);
    // Test driver for network stress runs: steers, aims, fires, recruits and throws without a keyboard,
    // a mouse or window focus (headless clients).
    bool Autopilot = false;
    // How far the autopilot has walked (a headless client proves it really played).
    double AutopilotDistance = 0;

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FGWDoorEntryCheck;
#endif
    gwgame::Vec2 AutopilotGoal{}, AutopilotLast{};
    double AutopilotTurn = 0, AutopilotAct = 0;
    uint64 AutopilotSeed = 0;
    void DriveAutopilot(const class AGWGameState* S, const FGWUnitView& U, gwgame::Input& In);
    float InputTime = 0;
    double LastInput = 0, LastAction = 0;
    bool UIConsumedMouse = false;
    bool LocalCharging = false;
    double ChargeStarted = 0;
    // When the left button went down with the rocket launcher in hand (-1 while it is up).
    double RocketStarted = -1;
    // When the launcher may charge again after its last shell (the host's interval, counted here for the preview).
    double RocketReadyAt = 0;
    gwgame::Prediction Prediction;
    uint32 SequenceCounter = 0, PredictionSeed = 0, PredictionWallVersion = 0, LastAuthorityAck = 0;
    int32 PredictedUnit = -1;
    FVector LastAuthorityPosition = FVector::ZeroVector;
    FVector LastAuthorityImpulse = FVector::ZeroVector;
    int32 LastPaintCell = -1;
    double PaintAllowance = 40, LastPaintTime = 0, NextPour = 0, PourAllowance = 8, LastPourTime = 0;
    int32 MateUnderCursor() const;
    TArray<int32> SpectateChoices() const;
    void UpdateSpectate();
    // The mouse kept inside the window in play and let go everywhere else (applied when that or the window's focus
    // changes): -1 not yet set, 0 free, 1 kept.
    void UpdateMouse(bool Play);
    int8 MouseMode = -1;
    bool MouseActive = false;
    void Click();
    void PaintPressed();
    void PaintReleased();
    void PaintTo(int32 Cell);
    // F: merges the tail's last triangle (hub, survival); in the box it opens the build ring on the cell under the
    // cursor (as a click does).
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
    // The expedition's keys: the wheel (to the next slot that holds something) and Tab (the kit panel).
    void CycleSlot(int32 Step);
    void KitPanel();
    double LastRevive = 0;
};
