#pragma once
#include "Core/Battle.h"
#include "CoreMinimal.h"
#include "GWNetworkTypes.h"
#include "GameFramework/GameStateBase.h"
#include "GWGameState.generated.h"
UCLASS()
class GEOMETRICWARFARE_API AGWGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    AGWGameState();
    // Ring sizes: every live shot, recent cue and damage number has its own slot keyed by id.
    static constexpr int32 ShotSlotCount = 512;
    static constexpr int32 EffectSlotCount = 256;
    static constexpr int32 DamageSlotCount = 128;
    static constexpr int32 InkSlotCount = 192;
    UPROPERTY(Replicated) bool InMatch = false;
    UPROPERTY(Replicated) bool Finished = false;
    UPROPERTY(Replicated) bool UpgradePaused = false;
    UPROPERTY(Replicated) bool IsRoom = false;
    UPROPERTY(Replicated) int32 HostIdentity = -1;
    UPROPERTY(Replicated) int32 Winner = 0;
    UPROPERTY(Replicated) int32 SharedLevel = 1;
    UPROPERTY(Replicated) float XP = 0;
    UPROPERTY(ReplicatedUsing = OnRep_Elapsed) float Elapsed = 0;
    UPROPERTY(Replicated) float Duration = 600;
    UPROPERTY(Replicated) float PlayerSpeed = 300;
    UPROPERTY(Replicated) uint32 Seed = 1;
    UPROPERTY(Replicated) uint32 WallVersion = 0;
    UPROPERTY(Replicated) bool BossActive = false;
    UPROPERTY(Replicated) float BossHP = 0;
    // The room's trolling level (gwgame::PrankLevel), chosen by the host before deploying.
    UPROPERTY(Replicated) uint8 Prank = 1;
    UPROPERTY(Replicated) float BaitRadius = 600;
    UPROPERTY(Replicated) float BaitSeconds = 8;
    UPROPERTY(Replicated) float RollDelay = .5f;
    // A room resuming a saved run lists who belongs to it and whether they have rejoined.
    UPROPERTY(Replicated) bool Resuming = false;
    UPROPERTY(Replicated) int32 ResumeLevel = 1;
    UPROPERTY(Replicated) float ResumeElapsed = 0;
    UPROPERTY(Replicated) TArray<FGWSeatView> ResumeSeats;
    // Wire arrays (see GWNetworkTypes.h). Unit slots mirror the simulation's unit slots one to one.
    UPROPERTY(ReplicatedUsing = OnRep_Units) TArray<FGWUnitNet> UnitSlots;
    UPROPERTY(ReplicatedUsing = OnRep_Units) TArray<FGWPlayerNet> PlayerSlots;
    UPROPERTY(Replicated) TArray<FGWShotNet> ShotSlots;
    UPROPERTY(Replicated) TArray<FGWEffectNet> EffectSlots;
    UPROPERTY(Replicated) TArray<FGWDamageNet> DamageSlots;
    UPROPERTY(Replicated) TArray<FGWInkDropNet> InkSlots;
    UPROPERTY(Replicated) TArray<FGWCrateView> Crates;
    UPROPERTY(Replicated) TArray<int32> BrokenWalls;
    UPROPERTY(Replicated) TArray<FGWWallPatchView> WallPatches;
    // Rule set of the current run (gwgame::RuleSet) and the line-defense state when it is Defense.
    UPROPERTY(Replicated) uint8 Rules = 0;
    UPROPERTY(Replicated) uint8 Phase = 0;
    UPROPERTY(Replicated) int32 Level = 0;
    UPROPERTY(Replicated) int32 Wave = 0;
    UPROPERTY(Replicated) int32 WaveCount = 0;
    UPROPERTY(Replicated) int32 WavesCleared = 0;
    UPROPERTY(Replicated) float PhaseTime = 0;
    UPROPERTY(Replicated) float CoreHP = 0;
    UPROPERTY(Replicated) float CoreMaxHP = 1;
    UPROPERTY(Replicated) FVector CorePosition = FVector::ZeroVector;
    UPROPERTY(Replicated) int32 EnemiesLeft = 0;
    UPROPERTY(Replicated) int32 WallsLost = 0;
    UPROPERTY(Replicated) uint8 Stars = 0;
    UPROPERTY(Replicated) int32 StickersEarned = 0;
    // Tutorial page: the step being taught (gwgame::TutorialStep) and how far it has come.
    UPROPERTY(Replicated) bool Tutorial = false;
    UPROPERTY(Replicated) uint8 TutorialStep = 0;
    UPROPERTY(Replicated) uint8 TutorialDone = 0;
    UPROPERTY(Replicated) uint8 TutorialNeeded = 1;
    // The column of the drawing step's three suggested cells (Battle::tutorialWall).
    UPROPERTY(Replicated) uint8 TutorialWallX = 6;
    // Hub: the page the host has picked on the mission board, and the deploy countdown (server world time
    // the mission starts at; 0 when nobody is deploying).
    UPROPERTY(Replicated) uint8 BoardLevel = 0;
    // The room's difficulty tier (难度档, 1-5) the host picked on the board, and the highest tier the host's own
    // profile has opened (the board offers only those: design doc 任务板只显示房主已解锁的档位).
    UPROPERTY(Replicated) uint8 Tier = 1;
    UPROPERTY(Replicated) uint8 MaxTier = 1;
    UPROPERTY(Replicated) uint8 DeployLevel = 0;
    UPROPERTY(Replicated) float DeployAt = 0;
    UPROPERTY(Replicated) int32 HubBullseyes = 0;
    UPROPERTY(Replicated) int32 GridWidth = 0;
    UPROPERTY(Replicated) int32 GridHeight = 0;
    UPROPERTY(Replicated) float CellSize = 400;
    UPROPERTY(Replicated) TArray<uint8> Cells;
    // Decoded views, rebuilt from the wire arrays (units on arrival, the time-based ones every frame).
    TArray<FGWUnitView> Units;
    TArray<FGWProjectileView> Projectiles;
    TArray<FGWEffectView> Effects;
    TArray<FGWDamageNumberView> DamageNumbers;
    TArray<FGWInkDropView> InkDrops;
    TUniquePtr<gwgame::Battle> Simulation;
    // Copies the simulation into the wire arrays (authority only).
    void Publish();
    // Forgets every published shot, cue and number (a new run starts with empty rings).
    void ResetRings();
    // Rebuilds the time-based views for this frame (idempotent within a frame).
    void RefreshViews();
    // The simulation clock as this machine sees it: exact on the host, extrapolated briefly on clients.
    double Clock() const;
    const FGWUnitView* FindUnit(int32 Id) const;
    bool Defense() const
    {
        return Rules == static_cast<uint8>(gwgame::RuleSet::Defense);
    }
    bool Hub() const
    {
        return InMatch && Rules == static_cast<uint8>(gwgame::RuleSet::Hub);
    }
    // Pages built from replicated cells (defense levels, the tutorial and the hub).
    bool GridPage() const
    {
        return Rules == static_cast<uint8>(gwgame::RuleSet::Defense) || Rules == static_cast<uint8>(gwgame::RuleSet::Hub);
    }
    // Seconds left before the host's deploy takes everyone out of the hub (negative when none is running).
    float DeployIn() const
    {
        return DeployAt > 0 ? DeployAt - GetServerWorldTimeSeconds() : -1.f;
    }
    // Client-side copy of the defense grid as a maze (walls, rock, patches), entrances and core cell.
    bool BuildGeometry(gwgame::Maze& Out, TArray<int32>& Entrances, int32& CoreCell) const;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

private:
    UFUNCTION() void OnRep_Units();
    UFUNCTION() void OnRep_Elapsed();
    void DecodeUnits();
    double ElapsedArrival = 0;
    uint64 ViewFrame = 0;
    uint32 PublishedEffect = 0;
};
