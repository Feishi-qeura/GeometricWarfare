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
    // The expedition's floor loot: a slot per id, like the ink drops (gwgame::MaxGroundLoot lie on a floor at most).
    static constexpr int32 LootSlotCount = 192;
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
    // The expedition (design v2.1): stage 1-4 (0 when this is no expedition), floor, floors cleared, the four stages'
    // pens (3 bits each), the party the floor was scaled for, the room the party is in, the floor's rooms, loot,
    // shelves, chests, exits and item zones, the portal, the boss, and the run's tallies for the result card.
    // Twist: the party playlist's twist on this mission (0 none, 1 chaos, 2 rockets, 3 ink rain).
    UPROPERTY(Replicated) uint8 ExpStage = 0;
    UPROPERTY(Replicated) uint8 ExpFloor = 0;
    UPROPERTY(Replicated) uint8 ExpFloorsCleared = 0;
    UPROPERTY(Replicated) uint16 ExpBiomes = 0;
    UPROPERTY(Replicated) uint8 ExpParty = 1;
    UPROPERTY(Replicated) uint8 ExpCurrent = 0;
    UPROPERTY(Replicated) TArray<FGWRoomNet> Rooms;
    UPROPERTY(Replicated) TArray<FGWLootNet> LootSlots;
    UPROPERTY(Replicated) TArray<FGWOfferNet> Offers;
    UPROPERTY(Replicated) TArray<FGWChestNet> Chests;
    UPROPERTY(Replicated) TArray<FGWExitNet> Exits;
    UPROPERTY(Replicated) TArray<FGWZoneNet> Zones;
    UPROPERTY(Replicated) FVector PortalAt = FVector::ZeroVector;
    UPROPERTY(Replicated) bool PortalOpen = false;
    UPROPERTY(Replicated) float PortalWait = 0;
    UPROPERTY(Replicated) int32 ExpBoss = -1;
    UPROPERTY(Replicated) int32 ExpKills = 0;
    UPROPERTY(Replicated) int32 ExpElites = 0;
    UPROPERTY(Replicated) int32 ExpChests = 0;
    UPROPERTY(Replicated) int32 ExpFights = 0;
    // The floor's prank from an EV04 ballot (gwgame::FloorPrank, -1 none) and the seconds the open ballot has left.
    UPROPERTY(Replicated) int8 ExpPrank = -1;
    UPROPERTY(Replicated) float ExpVoteClock = 0;
    UPROPERTY(Replicated) uint8 Twist = 0;
    // Barbed wire per cell of a defense page: 0 none, 1-15 how much is left (empty off a defense page).
    UPROPERTY(Replicated) TArray<uint8> Wire;
    // What rubbing out each cell's tower or wire would pay back now, in ink (the build ring's rub-out price).
    UPROPERTY(Replicated) TArray<uint16> TowerWorth;
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
    // The box's ladders, in the simulation's order (a climber's ladder index points into it).
    UPROPERTY(Replicated) TArray<FGWLadderNet> Ladders;
    // What the box's builds and revives cost in ink (the host's rules), for the cursors and prompts.
    UPROPERTY(Replicated) float TowerInk = 40;
    UPROPERTY(Replicated) float MortarInk = 60;
    UPROPERTY(Replicated) float TeslaInk = 55;
    UPROPERTY(Replicated) float WireInk = 25;
    UPROPERTY(Replicated) float TowerFiveInk = 50;
    UPROPERTY(Replicated) float TowerSixInk = 70;
    UPROPERTY(Replicated) float LadderInk = 30;
    UPROPERTY(Replicated) float ReviveInk = 50;
    UPROPERTY(Replicated) float ReviveReach = 190;
    // Decoded views, rebuilt from the wire arrays (units on arrival, the time-based ones every frame).
    TArray<FGWUnitView> Units;
    TArray<FGWProjectileView> Projectiles;
    TArray<FGWEffectView> Effects;
    TArray<FGWDamageNumberView> DamageNumbers;
    TArray<FGWInkDropView> InkDrops;
    TArray<FGWLootView> Loot;
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
    bool Expedition() const
    {
        return Rules == static_cast<uint8>(gwgame::RuleSet::Expedition) && ExpStage > 0;
    }
    bool Defense() const
    {
        return Rules == static_cast<uint8>(gwgame::RuleSet::Defense);
    }
    // Whether the build tools are in this player's hands now: a defense page's build phase, and in the tutorial only from
    // its drawing step on (walking and shooting come first, with the reticle and no build preview).
    bool Building() const
    {
        return Defense() && Phase == static_cast<uint8>(gwgame::DefensePhase::Build) &&
               (!Tutorial || TutorialStep >= static_cast<uint8>(gwgame::TutorialStep::Draw));
    }
    bool Hub() const
    {
        return InMatch && Rules == static_cast<uint8>(gwgame::RuleSet::Hub);
    }
    // Pages built from replicated cells (defense levels, the tutorial, the hub and the expedition's floors).
    bool GridPage() const
    {
        return Rules == static_cast<uint8>(gwgame::RuleSet::Defense) || Rules == static_cast<uint8>(gwgame::RuleSet::Hub) ||
               Rules == static_cast<uint8>(gwgame::RuleSet::Expedition);
    }
    // The expedition: the pen of a stage (1-4), and the room a world point stands in (-1 in a corridor).
    gwgame::PenBiome Biome(int32 Stage = 0) const
    {
        const int32 S = FMath::Clamp(Stage > 0 ? Stage : static_cast<int32>(ExpStage), 1, 4);
        return static_cast<gwgame::PenBiome>(FMath::Clamp((ExpBiomes >> ((S - 1) * 3)) & 7, 0, gwgame::BiomeCount - 1));
    }
    int32 RoomAt(FVector World) const;
    // The gun, item or look lying nearest a point within Reach (what E would pick up there), for prompts.
    const FGWLootView* LootNear(FVector World, float Reach) const;
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
    // The expedition's floor onto the wire (rooms, loot, shelves, chests, exits, zones, portal, tallies).
    void PublishExpedition();
    double ElapsedArrival = 0;
    uint64 ViewFrame = 0;
    uint32 PublishedEffect = 0;
};
