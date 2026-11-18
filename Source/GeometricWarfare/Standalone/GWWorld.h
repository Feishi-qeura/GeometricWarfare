#pragma once
#include "Core/Maze.h"
#include "Core/ProjectilePresentation.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/PimplPtr.h"
#include "GWWorld.generated.h"
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UCameraComponent;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UGWFxDirector;
class AGWGameState;
class AGWController;
struct FGWUnitView;
struct FGWShowBattle;
// A level-up on the title page's battle: three skill cards over a player's head, one picked and broken into shards that
// fly into it (the HUD draws it from this, AGWHUD::ShowcaseCards).
struct FGWShowcasePick
{
    // Where the cards stand above (world space) and where the shards go.
    FVector Head = FVector::ZeroVector, Body = FVector::ZeroVector;
    // Seconds since the cards came out; the shards arrive at 2.25 and it is over at 2.7.
    float Age = 0;
    // Skill ids (family * 4 + skill) and which one is picked.
    int32 Skills[3] = {0, 0, 0};
    int32 Chosen = 0;
    FLinearColor Color = FLinearColor::White;
};
// The diorama's card-stock solids and paper decals, by index into the world's instance batches (shared by the
// match renderer in GWWorld.cpp and the title page's brawl in GWShowcase.cpp).
enum EGWMesh : int32
{
    MeshCoin,
    MeshBlock,
    MeshTri,
    MeshHex,
    MeshDiamond,
    MeshSphere,
    MeshSword,
    MeshStar,
    MeshPent,
    MeshTrap,
    MeshCount
};
enum EGWDecal : int32
{
    DecalShadow,
    DecalGlow,
    DecalLoop,
    DecalRing,
    DecalHex,
    DecalStreak,
    DecalDot,
    DecalDiamond,
    DecalSquare,
    DecalHatch,
    DecalPlus,
    DecalTriangle,
    // Geometric markers: a compass rose with ticks (gates, targets, wind-ups), a hexagon band (the core, the local
    // player) and a triangle band (a throw winding up).
    DecalCompass,
    DecalHexRing,
    DecalTriRing,
    DecalCount
};
UCLASS()
class GEOMETRICWARFARE_API AGWWorld : public AActor
{
    GENERATED_BODY()
public:
    AGWWorld();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Delta) override;
    int32 SelectedCandidate = -1;
    // True when E would grab the selected candidate (a teammate) rather than recruit or join them.
    bool SelectedGrab = false;
    gwgame::Maze Geometry;
    // Whether Local may grab Target under the room's trolling level (mirrors Battle::canGrab).
    static bool CanGrab(const AGWGameState* State, const FGWUnitView& Local, const FGWUnitView& Target);
    bool VisualPosition(int32 Id, FVector& Position) const;
    bool VisualProjectile(int32 Id, FVector& Position) const;
    UGWFxDirector* Effects() const
    {
        return Fx;
    }
    // The title page's looping brawl (GWShowcase.cpp) is ShowcaseLoop seconds long; tests jump its clock to a
    // moment of it (seconds into the loop; -1 plays on).
    static constexpr float ShowcaseLoop = 60;
    float ShowcaseSeek = -1;
    bool ShowcaseOn() const
    {
        return ShowActive;
    }
    // Shots that ran out of range and are still falling, skipping or lying on the floor.
    int32 SpentShotCount() const
    {
        return SpentShots.Num();
    }
    // The title page's level-ups in progress, for the HUD's cards.
    TArray<FGWShowcasePick> ShowcasePicks;
    // A line on how the title page's battle stands (tests log it).
    FString ShowcaseStats() const;

private:
    // Instance batches are rebuilt every frame and uploaded once per component.
    struct FBatch
    {
        TArray<FTransform> Transforms;
        TArray<float> Data;
        void Add(const FTransform& Transform, const FLinearColor& Color, float Extra)
        {
            Transforms.Add(Transform);
            Data.Append({Color.R, Color.G, Color.B, Extra});
        }
    };
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<UAudioComponent> Music;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Bodies;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Hulls;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Decals;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WallBodies;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WallHulls;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Floor;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FloorMaterial;
    UPROPERTY() TArray<TObjectPtr<UStaticMesh>> Meshes;
    UPROPERTY() TObjectPtr<UGWFxDirector> Fx;
    TArray<FVector> MeshSizes;
    TArray<FBatch> BodyBatches, HullBatches, DecalBatches;
    // Defense page: entrances, core cell and the shortest route from each entrance, as dots along the road.
    TArray<int32> Entrances;
    int32 CoreCell = -1;
    TArray<TArray<FVector>> Routes;
    float ViewPitch = 55, ViewDistance = 1650;
    uint32 LastSeed = 0, LastVersion = MAX_uint32;
    int32 LastProjectile = 0;
    bool WasInMatch = false;
    FString LastMusicTrack;
    float AudioTime = 0;
    FVector CameraFocus = FVector::ZeroVector;
    TMap<int32, FVector> SmoothedPositions;
    TMap<int32, FVector> ProjectilePositions;
    // Chest tosses in the air (2026-11-17): when this machine first saw each, and its recent points for the trail.
    TMap<int32, float> TossBorn;
    TMap<int32, TArray<FVector>> TossTrail;
    void Solid(int32 Mesh, FVector Center, FVector Size, float Yaw, const FLinearColor& Color, float Flash = 0,
               float Ink = 3.5f, const FLinearColor& Outline = FLinearColor(.012f, .014f, .022f));
    void Decal(int32 Kind, FVector Center, FVector2D Size, float Yaw, const FLinearColor& Color);
    // A solid with a full rotation (hats stand some shapes on end), always inked in the line colour.
    void Rotated(int32 Mesh, FVector Center, FVector Size, FRotator Rotation, const FLinearColor& Color, float Ink);
    // A player's hat (gwgame hat ids, 1-7) on the middle of their body's top face.
    void Hat(int32 Kind, FVector Top, float R, float Yaw, float Time);
    void Upload(UInstancedStaticMeshComponent* Component, FBatch& Batch);
    void Rebuild(const AGWGameState* State);
    // The page's arrival (FGWTransition): once the wipe opens, the blocks rise off the paper in a wave from the
    // middle like a pop-up book, the players drop onto the page and the camera settles from high above.
    // IntroAge is seconds into that (negative while the page is still covered, large once it is over).
    float IntroAge();
    void BuildWalls(const AGWGameState* State, float Intro);
    double MatchSeenAt = 0, IntroStart = 0;
    bool IntroPending = false, IntroCovered = false, WallsRising = false;
    TSet<int32> Landed, UnitsLanded;
    TArray<TPair<FVector, FLinearColor>> Landings, Arrivals;
    void UpdateAudio(const AGWGameState* State, FVector Focus, float Delta);
    void RebuildRoutes();
    void DrawDefense(const AGWGameState* State, const AGWController* Controller, const FGWUnitView* Local, float Time);
    // The hub's stations as paper props with their keepers, the training mat, the bullseye and the spawn mark.
    void DrawHub(const AGWGameState* State, const FGWUnitView* Local, float Time);
    // The tutorial's marks on the page: where to walk, where to throw, which cells to draw.
    void DrawTutorial(const AGWGameState* State, float Time);
    double LastBullseye = -10;
    uint32 SeenBullseye = 0;
    // The local player's last rocket: where its arc ran (drawn as a dotted guide for a few seconds) and when.
    int32 GhostShot = 0;
    FVector GhostFrom = FVector::ZeroVector, GhostTo = FVector::ZeroVector;
    float GhostAt = -100;
    // The box: its platform's ruler ticks, the tunnel mouths under it, ladders, towers and fallen teammates' outlines.
    void DrawBox(const AGWGameState* State, const AGWController* Controller, const FGWUnitView* Local, float Time);
    // The expedition's floor (design v2.1): doorways and the doors of an armed room, the portal, the exit doors, chests,
    // the shelves' wares and the event machines, loot on the floor, the items' zones and the ghosts of the fallen.
    void DrawExpedition(const AGWGameState* State, const FGWUnitView* Local, float Time);
    // A thing as it lies on the floor or a shelf (a gun, an item in its colour, a look), floating at At.
    void LootModel(uint8 Kind, int32 What, FVector At, float Time, int32 Seed, int32 Variant = -1);
    // The looks an expedition player wears besides the hat: a mask, a cape, a charm and a trail.
    void Looks(const FGWUnitView& Unit, FVector Base, float R, float Height, float Lift, float Yaw, float Time);
    // The bestiary (GWFoeArt.cpp): each foe drawn from its own parts in its pen's colours with its act on show (a boss's
    // tools ringed while they can be rubbed out); an enemy shot by kind (false: not a foe's shot); the floor's hazards,
    // terrain and the foes' warnings. An eel's beads and the dragon's body follow the wake each leaves (FoeTrails).
    void FoeBody(const FGWUnitView& Unit, FVector Base, float R, float Lift, float Flash, float Time);
    bool FoeShotModel(uint8 Weapon, FVector Position, FVector Dir, float Yaw, float Time, int32 Id);
    void DrawHazards(const AGWGameState* State, float Time);
    TMap<int32, TArray<FVector>> FoeTrails;

public:
    // Whether the local player could draw on this cell right now (the cursor colour; the host decides).
    bool CanDraw(const AGWGameState* State, const FGWUnitView* Local, int32 Cell) const;
    // Whether a ladder could hang from this platform cell now (-1 no, 0 yes, 1 one hangs there already).
    int32 LadderState(const AGWGameState* State, const FGWUnitView* Local, int32 Cell) const;
    // Whether the enemies would still have a road to the core with a post on this cell (a tower; the host decides).
    bool RoadStaysOpen(int32 Cell) const;

private:
    // Towers wobble like jelly when they go up (from nothing to a little too big and back, stretching tall on the
    // way) and when they grow (squashed down, then popped up past their size): each tower's sides as last drawn and
    // when its wobble began. While JellyOn, every solid drawn is squashed about JellyPivot by JellyScale.
    struct FJelly
    {
        int32 Sides = 0;
        double Start = -100;
        bool Grew = false;
    };
    TMap<int32, FJelly> Jellies;
    uint32 JellySeed = 0;
    bool JellyOn = false;
    FVector JellyPivot = FVector::ZeroVector, JellyScale = FVector::OneVector;
    static FVector JellyAt(float Seconds, bool Grew);

    // Spent shots (match and title page alike): a shot that runs out of range does not vanish, it drops to the
    // floor in an arc, skips once with a puff of dust and a scuff mark, lies still a moment and is gone.
    struct FSpentShot
    {
        FVector Position = FVector::ZeroVector, Velocity = FVector::ZeroVector;
        float Age = 0, Rest = -1, Spin = 0;
        uint8 Weapon = 0, Team = 1;
        bool Bounced = false;
    };
    struct FScuff
    {
        FVector At = FVector::ZeroVector;
        float Yaw = 0, Age = 0, Length = 30;
        FLinearColor Color = FLinearColor::Black;
    };
    TArray<FSpentShot> SpentShots;
    TArray<FScuff> Scuffs;
    // What each shot drawn last frame looked like, to catch the ones that ran out of range this frame.
    struct FShotTrace
    {
        FVector Position = FVector::ZeroVector, Velocity = FVector::ZeroVector;
        uint8 Weapon = 0, Team = 1;
        bool RanOut = false;
    };
    TMap<int32, FShotTrace> ShotTraces;
    void AddSpentShot(FVector Position, FVector Velocity, uint8 Weapon, uint8 Team);
    // Flies, bounces, draws and forgets the spent shots and their scuffs; OnMaze: the floor is the page's (the
    // platform's top, a block's top), otherwise the flat paper.
    void StepSpentShots(float Delta, bool OnMaze);

    // The title page's battle (GWShowcase.cpp) and whether it is on (it takes the floor, the camera and the cue director
    // while it is, and gives them back when it stops).
    TPimplPtr<FGWShowBattle> Battle;
    bool ShowActive = false;
    void TickShowcase(AGWGameState* State, float Delta);
    void StopShowcase();
    // A tower in the diorama's look, standing on Foot: its post, its body (a bolt tower's polygon of Sides, a
    // mortar's round body and barrel along Facing, a tesla coil's triangle with coils and spark ball), its paper disc
    // and its turning gold pin.
    void TowerParts(FVector Foot, int32 Kind, int32 Sides, float Facing, float Time, float Flash = 0);
};
