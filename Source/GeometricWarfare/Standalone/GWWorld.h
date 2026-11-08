#pragma once
#include "Core/Maze.h"
#include "Core/ProjectilePresentation.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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
    void Solid(int32 Mesh, FVector Center, FVector Size, float Yaw, const FLinearColor& Color, float Flash = 0,
               float Ink = 3.5f, const FLinearColor& Outline = FLinearColor(.012f, .014f, .022f));
    void Decal(int32 Kind, FVector Center, FVector2D Size, float Yaw, const FLinearColor& Color);
    // A solid with a full rotation (hats stand some shapes on end), always inked in the line colour.
    void Rotated(int32 Mesh, FVector Center, FVector Size, FRotator Rotation, const FLinearColor& Color, float Ink);
    // A player's hat (gwgame hat ids, 1-7) on the middle of their body's top face.
    void Hat(int32 Kind, FVector Top, float R, float Yaw, float Time);
    void Upload(UInstancedStaticMeshComponent* Component, FBatch& Batch);
    void Rebuild(const AGWGameState* State);
    void UpdateAudio(const AGWGameState* State, FVector Focus, float Delta);
    void RebuildRoutes();
    void DrawDefense(const AGWGameState* State, const AGWController* Controller, const FGWUnitView* Local, float Time);
    // The hub's stations as paper props with their keepers, the training mat, the bullseye and the spawn mark.
    void DrawHub(const AGWGameState* State, const FGWUnitView* Local, float Time);
    // The tutorial's marks on the page: where to walk, where to throw, which cells to draw.
    void DrawTutorial(const AGWGameState* State, float Time);
    double LastBullseye = -10;
    uint32 SeenBullseye = 0;

public:
    // Whether the local player could draw on this cell right now (the cursor colour; the host decides).
    bool CanDraw(const AGWGameState* State, const FGWUnitView* Local, int32 Cell) const;
};
