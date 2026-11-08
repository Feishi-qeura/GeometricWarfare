#include "GWWorld.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GWCinematic.h"
#include "GWController.h"
#include "GWFx.h"
#include "GWGameState.h"
#include "GWPalette.h"
#include "GWPlayerState.h"
#include "GWSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include <cstring>

// The arena is a paper diorama: faceted card-stock solids with ink hulls, hatched blob shadows,
// marker decals on graph paper, and Niagara cues from UGWFxDirector.
namespace
{
enum EMesh : int32
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
enum EDecal : int32
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
    DecalCount
};
const TCHAR* MeshPaths[MeshCount] = {
    TEXT("/Game/Sketch/Meshes/SM_GW_Coin.SM_GW_Coin"),       TEXT("/Game/Sketch/Meshes/SM_GW_Block.SM_GW_Block"),
    TEXT("/Game/Sketch/Meshes/SM_GW_Tri.SM_GW_Tri"),         TEXT("/Game/Sketch/Meshes/SM_GW_Hex.SM_GW_Hex"),
    TEXT("/Game/Sketch/Meshes/SM_GW_Diamond.SM_GW_Diamond"), TEXT("/Engine/BasicShapes/Sphere.Sphere"),
    TEXT("/Game/Sketch/Meshes/SM_GW_Diamond.SM_GW_Diamond"), TEXT("/Game/Sketch/Meshes/SM_GW_Star.SM_GW_Star"),
    TEXT("/Game/Sketch/Meshes/SM_GW_Pent.SM_GW_Pent"),       TEXT("/Game/Sketch/Meshes/SM_GW_Trap.SM_GW_Trap")};
const TCHAR* DecalPaths[DecalCount] = {
    TEXT("/Game/Sketch/Materials/M_GW_Shadow.M_GW_Shadow"),         TEXT("/Game/Sketch/Materials/MI_GW_Decal_Glow.MI_GW_Decal_Glow"),
    TEXT("/Game/Sketch/Materials/MI_GW_Decal_Loop.MI_GW_Decal_Loop"), TEXT("/Game/Sketch/Materials/MI_GW_Decal_Ring.MI_GW_Decal_Ring"),
    TEXT("/Game/Sketch/Materials/MI_GW_Decal_Hex.MI_GW_Decal_Hex"),   TEXT("/Game/Sketch/Materials/M_GW_Streak.M_GW_Streak"),
    TEXT("/Game/Sketch/Materials/MI_GW_Decal_Dot.MI_GW_Decal_Dot"),   TEXT("/Game/Sketch/Materials/MI_GW_Decal_Diamond.MI_GW_Decal_Diamond"),
    TEXT("/Game/Sketch/Materials/MI_GW_Decal_Square.MI_GW_Decal_Square"), TEXT("/Game/Sketch/Materials/MI_GW_Decal_Hatch.MI_GW_Decal_Hatch"),
    TEXT("/Game/Sketch/Materials/MI_GW_Decal_Plus.MI_GW_Decal_Plus"), TEXT("/Game/Sketch/Materials/MI_GW_Decal_Triangle.MI_GW_Decal_Triangle")};
const FLinearColor InkLine(.012f, .014f, .022f);
// Level rock reads as graphite next to the kraft card of drawn walls; the marker's thick walls are dark card and
// the highlighter's bounce walls fluorescent lime.
const FLinearColor Graphite = GWPalette::Hex(0x5E6678);
const FLinearColor ThickCard = GWPalette::Hex(0x8A6A43);
const FLinearColor Highlighter = GWPalette::Hex(0xB5E655);
// Cells of the hub's wall ring that hold a station (drawn as props instead of plain wall).
bool HubStationCell(int32 Cell)
{
    const auto& Rows = gwgame::hubRows();
    const int32 W = static_cast<int32>(std::strlen(Rows[0]));
    const int32 X = Cell % W, Y = Cell / W;
    return Y >= 0 && Y < static_cast<int32>(Rows.size()) && gwgame::hubStationOf(Rows[Y][X]) >= 0;
}
float Hash(int32 X)
{
    uint32 H = static_cast<uint32>(X) * 2654435761u;
    H ^= H >> 15;
    return (H & 0xffff) / 65535.f;
}
} // namespace

AGWWorld::AGWWorld()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("WorldRoot"));
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("PlayerCamera"));
    Camera->SetupAttachment(RootComponent);
    Camera->SetFieldOfView(50);
    // Everything in the diorama is unlit and authored in sRGB, so the camera shows it as painted:
    // fixed exposure, no filmic curve, no local-exposure compression and no lens vignette.
    FPostProcessSettings& Post = Camera->PostProcessSettings;
    Post.bOverride_AutoExposureMethod = Post.bOverride_AutoExposureBias = true;
    Post.AutoExposureMethod = AEM_Manual;
    Post.AutoExposureBias = 0;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    Post.AutoExposureApplyPhysicalCameraExposure = false;
    Post.bOverride_ToneCurveAmount = true;
    Post.ToneCurveAmount = 0;
    Post.bOverride_LocalExposureHighlightContrastScale = Post.bOverride_LocalExposureShadowContrastScale = true;
    Post.LocalExposureHighlightContrastScale = Post.LocalExposureShadowContrastScale = 1;
    Post.bOverride_VignetteIntensity = true;
    Post.VignetteIntensity = 0;
    Camera->PostProcessBlendWeight = 1;
    Music = CreateDefaultSubobject<UAudioComponent>(TEXT("Music"));
    Music->SetupAttachment(RootComponent);
    Music->bAutoActivate = false;
    Music->bIsUISound = true;
    Music->bAllowSpatialization = false;
}

void AGWWorld::BeginPlay()
{
    Super::BeginPlay();
    auto* Toon = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sketch/Materials/M_GW_Toon.M_GW_Toon"));
    auto* Ink = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sketch/Materials/M_GW_Ink.M_GW_Ink"));
    auto* Card = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Sketch/Meshes/SM_GW_Card.SM_GW_Card"));
    auto Make = [&](UStaticMesh* Mesh, UMaterialInterface* Material, bool Hull, int32 Sort)
    {
        auto* C = NewObject<UInstancedStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent);
        C->SetMobility(EComponentMobility::Movable);
        C->SetStaticMesh(Mesh);
        C->SetMaterial(0, Material ? Material : UMaterial::GetDefaultMaterial(MD_Surface));
        C->SetNumCustomDataFloats(4);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCanEverAffectNavigation(false);
        C->SetCastShadow(false);
        C->SetReverseCulling(Hull);
        C->TranslucencySortPriority = Sort;
        C->RegisterComponent();
        return C;
    };
    for (int32 I = 0; I < MeshCount; ++I)
    {
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPaths[I]);
        if (!Mesh)
            Mesh = LoadObject<UStaticMesh>(nullptr, MeshPaths[MeshDiamond]);
        Meshes.Add(Mesh);
        MeshSizes.Add(Mesh ? Mesh->GetBoundingBox().GetSize().ComponentMax(FVector(1)) : FVector(100));
        Bodies.Add(Make(Mesh, Toon, false, 0));
        Hulls.Add(Make(Mesh, Ink, true, 0));
    }
    for (int32 I = 0; I < DecalCount; ++I)
        Decals.Add(Make(Card, LoadObject<UMaterialInterface>(nullptr, DecalPaths[I]), false, I));
    BodyBatches.SetNum(MeshCount);
    HullBatches.SetNum(MeshCount);
    DecalBatches.SetNum(DecalCount);
    WallBodies = Make(Meshes[MeshBlock], Toon, false, 0);
    WallHulls = Make(Meshes[MeshBlock], Ink, true, 0);
    Floor = NewObject<UStaticMeshComponent>(this);
    Floor->SetupAttachment(RootComponent);
    Floor->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Floor->SetCastShadow(false);
    if (auto* Paper = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sketch/Materials/M_GW_Paper.M_GW_Paper")))
    {
        FloorMaterial = UMaterialInstanceDynamic::Create(Paper, this);
        Floor->SetMaterial(0, FloorMaterial);
    }
    Floor->RegisterComponent();
    Fx = NewObject<UGWFxDirector>(this);
    Fx->Initialize(this);
    Music->SetSound(LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/GeometricWarfare/Battle.Battle")));
}

void AGWWorld::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Fx)
        Fx->Stop();
    Super::EndPlay(Reason);
}

void AGWWorld::Solid(int32 Mesh, FVector Center, FVector Size, float Yaw, const FLinearColor& Color, float Flash, float Ink,
                     const FLinearColor& Outline)
{
    if (!BodyBatches.IsValidIndex(Mesh))
        return;
    const FQuat Rotation = FRotator(0, Yaw, 0).Quaternion();
    BodyBatches[Mesh].Add(FTransform(Rotation, Center, Size / MeshSizes[Mesh]), Color, Flash);
    if (Ink > 0)
        HullBatches[Mesh].Add(FTransform(Rotation, Center, (Size + FVector(Ink * 2)) / MeshSizes[Mesh]), Outline, 0);
}

void AGWWorld::Rotated(int32 Mesh, FVector Center, FVector Size, FRotator Rotation, const FLinearColor& Color, float Ink)
{
    if (!BodyBatches.IsValidIndex(Mesh))
        return;
    const FQuat Turn = Rotation.Quaternion();
    BodyBatches[Mesh].Add(FTransform(Turn, Center, Size / MeshSizes[Mesh]), Color, 0);
    if (Ink > 0)
        HullBatches[Mesh].Add(FTransform(Turn, Center, (Size + FVector(Ink * 2)) / MeshSizes[Mesh]), InkLine, 0);
}

// Hats (the wardrobe's, phase 4), built from the same card-stock solids as everything else and sized to the body's
// radius R; Top is the middle of the body's top face.
void AGWWorld::Hat(int32 Kind, FVector Top, float R, float Yaw, float Time)
{
    const FVector Front = FRotator(0, Yaw, 0).Vector();
    switch (Kind)
    {
    case 1:
        // A folded newspaper hat: a paper triangle standing across the head.
        Rotated(MeshTri, Top + FVector(0, 0, R * .62f), FVector(R * 1.25f, R * 1.9f, R * .34f), FRotator(90, Yaw + 90, 0), GWPalette::Hex(0xE9E2D0), 2.5f);
        break;
    case 2:
        // A knitted beanie with a pom-pom.
        Solid(MeshSphere, Top + FVector(0, 0, R * .05f), FVector(R * 1.55f, R * 1.55f, R * 1.0f), 0, GWPalette::Hex(0xD9534F), 0, 2.5f);
        Solid(MeshSphere, Top + FVector(0, 0, R * .62f), FVector(R * .46f), 0, GWPalette::Paper, 0, 2);
        break;
    case 3:
        // A cap with its brim along the aim.
        Solid(MeshCoin, Top + FVector(0, 0, R * .22f), FVector(R * 1.5f, R * 1.5f, R * .44f), Yaw, GWPalette::Teal, 0, 2.5f);
        Solid(MeshBlock, Top + Front * R * .95f + FVector(0, 0, R * .08f), FVector(R * .9f, R * 1.2f, R * .12f), Yaw, GWPalette::Teal * .8f, 0, 2);
        break;
    case 4:
        // A top hat: brim, a tall crown and a coral band.
        Solid(MeshCoin, Top + FVector(0, 0, R * .06f), FVector(R * 1.9f, R * 1.9f, R * .12f), 0, GWPalette::Ink, 0, 2);
        Solid(MeshCoin, Top + FVector(0, 0, R * .78f), FVector(R * 1.2f, R * 1.2f, R * 1.4f), 0, GWPalette::Ink, 0, 2.5f);
        Solid(MeshCoin, Top + FVector(0, 0, R * .3f), FVector(R * 1.26f, R * 1.26f, R * .26f), 0, GWPalette::Coral, 0, 0);
        break;
    case 5:
        // An antenna with a bobbing ball.
        Solid(MeshBlock, Top + FVector(0, 0, R * .8f), FVector(R * .14f, R * .14f, R * 1.6f), 0, GWPalette::InkSoft, 0, 1.5f);
        Solid(MeshSphere, Top + FVector(0, 0, R * 1.7f + 3 * FMath::Sin(Time * 6)), FVector(R * .5f), 0, GWPalette::Marigold, 0, 2);
        break;
    case 6:
        // A pencil standing on end: yellow hex body, kraft cone, graphite tip.
        Solid(MeshHex, Top + FVector(0, 0, R * .8f), FVector(R * .62f, R * .62f, R * 1.6f), 30, GWPalette::Hex(0xF2C230), 0, 2);
        Solid(MeshDiamond, Top + FVector(0, 0, R * 1.78f), FVector(R * .62f, R * .62f, R * .7f), 0, GWPalette::Kraft, 0, 1.5f);
        Solid(MeshDiamond, Top + FVector(0, 0, R * 2.05f), FVector(R * .22f, R * .22f, R * .28f), 0, GWPalette::Ink, 0, 0);
        break;
    case 7:
    {
        // A gold crown: a band with five points and a coral gem.
        Solid(MeshCoin, Top + FVector(0, 0, R * .22f), FVector(R * 1.4f, R * 1.4f, R * .44f), 0, GWPalette::Marigold, 0, 2.5f);
        for (int32 I = 0; I < 5; ++I)
        {
            const float A = I * 2 * PI / 5 + FMath::DegreesToRadians(Yaw);
            Solid(MeshDiamond, Top + FVector(FMath::Cos(A), FMath::Sin(A), 0) * R * .58f + FVector(0, 0, R * .62f), FVector(R * .3f, R * .3f, R * .5f), 0,
                  GWPalette::Marigold, 0, 1.5f);
        }
        Solid(MeshSphere, Top + Front * R * .7f + FVector(0, 0, R * .25f), FVector(R * .3f), 0, GWPalette::Coral, 0, 1.5f);
        break;
    }
    default:
        break;
    }
}

void AGWWorld::Decal(int32 Kind, FVector Center, FVector2D Size, float Yaw, const FLinearColor& Color)
{
    if (DecalBatches.IsValidIndex(Kind))
        DecalBatches[Kind].Add(FTransform(FRotator(0, Yaw, 0), Center, FVector(Size.X / 100, Size.Y / 100, 1)), Color, Color.A);
}

void AGWWorld::Upload(UInstancedStaticMeshComponent* Component, FBatch& Batch)
{
    if (!Component)
        return;
    const int32 Count = Batch.Transforms.Num();
    if (Component->GetInstanceCount() != Count)
    {
        Component->ClearInstances();
        if (Count)
            Component->AddInstances(Batch.Transforms, false, true);
    }
    else if (Count)
        Component->BatchUpdateInstancesTransforms(0, Batch.Transforms, true, false, true);
    for (int32 I = 0; I < Count; ++I)
        Component->SetCustomData(I, MakeArrayView(Batch.Data.GetData() + I * 4, 4), false);
    Component->MarkRenderStateDirty();
    Batch.Transforms.Reset();
    Batch.Data.Reset();
}

void AGWWorld::Rebuild(const AGWGameState* S)
{
    const bool NewMatch = LastSeed != S->Seed || WasInMatch != S->InMatch;
    const bool Hub = S->Rules == static_cast<uint8>(gwgame::RuleSet::Hub);
    if (S->GridPage())
    {
        // A grid page (defense, tutorial or hub) arrives as cells; until they do there is nothing to build.
        if (!S->BuildGeometry(Geometry, Entrances, CoreCell))
            return;
        RebuildRoutes();
    }
    else
    {
        Entrances.Reset();
        CoreCell = -1;
        Routes.Reset();
        if (NewMatch || Geometry.walls.empty())
            Geometry.generate(S->Seed, true);
        for (int32 Id : S->BrokenWalls)
            if (Id >= 0 && Id < static_cast<int32>(Geometry.walls.size()))
                Geometry.walls[Id] = 0;
        for (const auto& Patch : S->WallPatches)
            if (Patch.Health.Num() == gwgame::Maze::FragmentCount)
            {
                gwgame::Maze::WallPatch Values;
                FMemory::Memcpy(Values.data(), Patch.Health.GetData(), Values.size());
                Geometry.applyPatch(Patch.Cell, Values);
            }
        Geometry.version = S->WallVersion;
    }
    FBatch WallBatch, WallHullBatch;
    // Card-stock blocks: low enough to read the arena over them, hatched on the shaded faces.
    // Level rock stands taller in graphite; the walls the players draw are kraft card.
    Geometry.solids({0, 0}, {Geometry.sizeX(), Geometry.sizeY()},
                        [&](int Cell, gwgame::Vec2 Lo, gwgame::Vec2 Hi, int HP)
                        {
                            // A hub station stands in its wall cell as a prop (DrawHub) rather than a block.
                            if (Hub && HubStationCell(Cell))
                                return;
                            const bool Rock = Geometry.isFixed(Cell);
                            // The marker's thick wall stands taller in dark card, the highlighter's bounce wall glows
                            // fluorescent lime; plain walls are kraft.
                            const gwgame::WallKind Kind = Rock ? gwgame::WallKind::Plain : Geometry.kind(Cell);
                            const float Full = Kind == gwgame::WallKind::Thick ? 165 : 140;
                            const float Height = Rock ? 180 : HP >= 200 ? Full : 70 + HP * (Full - 70) / 200;
                            const FVector Size(Hi.x - Lo.x - 2, Hi.y - Lo.y - 2, Height);
                            const FVector Center((Lo.x + Hi.x) * .5, (Lo.y + Hi.y) * .5, Height * .5f);
                            const FLinearColor Card = Rock                                ? Graphite
                                                      : Kind == gwgame::WallKind::Thick  ? ThickCard
                                                      : Kind == gwgame::WallKind::Bounce ? Highlighter
                                                                                         : GWPalette::Kraft;
                            const FLinearColor Tint = Card * (.9f + .1f * Hash(Cell));
                            WallBatch.Add(FTransform(FQuat::Identity, Center, Size / MeshSizes[MeshBlock]), Tint, 0);
                            WallHullBatch.Add(FTransform(FQuat::Identity, Center, (Size + FVector(10)) / MeshSizes[MeshBlock]),
                                              InkLine, 0);
                        });
    Upload(WallBodies, WallBatch);
    Upload(WallHulls, WallHullBatch);
    const float Map = Geometry.sizeX();
    Floor->SetWorldLocation(FVector(Map * .5f, Geometry.sizeY() * .5f, -20));
    Floor->SetWorldScale3D(FVector(Map * 3 / 100, Geometry.sizeY() * 3 / 100, .4f));
    if (FloorMaterial)
    {
        FloorMaterial->SetScalarParameterValue(TEXT("MapSize"), Map);
        FloorMaterial->SetScalarParameterValue(TEXT("MapSizeY"), S->GridPage() ? Geometry.sizeY() : 0.f);
        FloorMaterial->SetScalarParameterValue(TEXT("GridMajor"), S->GridPage() ? Geometry.cellSize : 1000.f);
    }
    LastSeed = S->Seed;
    LastVersion = S->WallVersion;
    if (NewMatch)
    {
        SmoothedPositions.Reset();
        ProjectilePositions.Reset();
        CameraFocus = FVector(Geometry.sizeX() * .5, Geometry.sizeY() * .5, 0);
        LastProjectile = 0;
        AudioTime = 0;
    }
}

void AGWWorld::Tick(float Delta)
{
    Super::Tick(Delta);
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    auto* C = Cast<AGWController>(GetWorld()->GetFirstPlayerController());
    auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    if (!S || !C || Bodies.IsEmpty())
        return;
    S->RefreshViews();
    const bool Show3D = S->InMatch;
    const auto* Prefs = UGWUserSettings::Get();
    const double RealNow = FPlatformTime::Seconds();
    if (LastSeed != S->Seed || LastVersion != S->WallVersion || WasInMatch != S->InMatch)
        Rebuild(S);
    SelectedCandidate = -1;
    SelectedGrab = false;
    const FGWUnitView* Local = P ? S->FindUnit(P->UnitId) : nullptr;
    FVector Focus = Local ? FVector(Local->Position) : FVector(Geometry.sizeX() * .5f, Geometry.sizeY() * .5f, 0);
    if (Local && Local->Alive)
        C->PredictedPosition(Local->Id, Focus);
    if (Local && !Local->Alive)
    {
        // Spectate a living human first, then any surviving ally.
        const FGWUnitView* Watch = nullptr;
        for (const auto& U : S->Units)
            if (U.Alive && U.Team == Local->Team && U.Kind != 2 && (!Watch || (U.Human && !Watch->Human)))
                Watch = &U;
        if (Watch)
            Focus = Watch->Position;
    }
    double Closest = GetDefault<UGWRuntimeSettings>()->RecruitDistance;
    // E takes the closest ally or grabbable teammate (the same rule as the host's Battle::candidate).
    if (Local && Local->Alive && Local->FollowerOf < 0 && Local->Bait <= 0 && Local->State != 4)
        for (const auto& U : S->Units)
        {
            const bool Ally = U.Kind == 1 && U.FollowerOf < 0 && U.State != 4 && U.Recruitable;
            if (!U.Alive || U.Team != Local->Team || !(Ally || CanGrab(S, *Local, U)))
                continue;
            double Distance = (FVector(U.Position) - FVector(Local->Position)).Size2D();
            if (Distance < Closest &&
                Geometry.clear({Local->Position.X, Local->Position.Y}, {U.Position.X, U.Position.Y}, 25))
            {
                Closest = Distance;
                SelectedCandidate = U.Id;
                SelectedGrab = U.Human;
            }
        }
    if (Local && Local->Alive && Local->FollowerOf < 0 && Local->Bait <= 0 && Local->State != 4 && SelectedCandidate < 0)
        for (const auto& U : S->Units)
            if (U.Id != Local->Id && U.Alive && U.Human && U.FollowerOf < 0 && U.Team == Local->Team)
            {
                double D = (FVector(U.Position) - FVector(Local->Position)).Size2D();
                if (D < Closest && Geometry.clear({Local->Position.X, Local->Position.Y}, {U.Position.X, U.Position.Y}, 25))
                {
                    Closest = D;
                    SelectedCandidate = U.Id;
                }
            }
    const float Time = GetWorld()->GetTimeSeconds();
    // A tail's allies (and a carried teammate) wear their holder's seat colour as their outline.
    auto HolderSeat = [&](const FGWUnitView& U) -> int32
    {
        const FGWUnitView* Head = U.FollowerOf >= 0 ? S->FindUnit(U.FollowerOf) : nullptr;
        return Head && Head->Kind == 0 ? Head->Seat : -1;
    };
    // The hat each player wears (from their own profile report, through the room).
    TMap<int32, int32> HatOf;
    for (const auto& Item : S->PlayerArray)
        if (const auto* PS = Cast<AGWPlayerState>(Item); PS && PS->UnitId >= 0 && PS->Hat > 0)
            HatOf.Add(PS->UnitId, PS->Hat);
    // Smoothing runs every frame so menus can still ask for a unit's visual position.
    for (const auto& U : S->Units)
    {
        if (!U.Alive)
            continue;
        FVector Target = U.Position;
        const bool Predicted = Local && U.Id == Local->Id && C->PredictedPosition(U.Id, Target);
        FVector& Smoothed = SmoothedPositions.FindOrAdd(U.Id, Target);
        Smoothed = Predicted ? Target : FMath::VInterpTo(Smoothed, Target, Delta, 18);
        if (!Show3D)
            continue;
        const float R = U.Radius, Height = R * (U.Kind == 2 ? 1.25f : 1.1f);
        const bool Idle = U.Kind == 1 && U.FollowerOf < 0 && U.State != 4 && !U.Turret;
        const bool Launched = U.State == 4;
        const float Lift = (Idle ? 4 + 4 * FMath::Sin(Time * 3 + U.Id) : 0) + (Launched ? 18 : 0) + (U.Carried ? 14 : 0);
        const float Yaw = U.Angle + (Launched ? Time * 900 : 0);
        const FVector Base(Smoothed.X, Smoothed.Y, 0);
        const FLinearColor Color = UGWFxDirector::UnitColor(U);
        const float Flash = Prefs && !Prefs->HitFlash ? 0.f : FMath::Clamp(1.f - static_cast<float>(RealNow - U.HitAt) / .15f, 0.f, 1.f);
        const bool Candidate = U.Id == SelectedCandidate;
        const int32 Holder = U.Kind != 2 ? HolderSeat(U) : -1;
        const FLinearColor Outline = Candidate ? GWPalette::Marigold : Holder >= 0 ? GWPalette::Seat(Holder) * .8f : InkLine;
        const float InkWidth = Candidate ? 7 : R >= 100 ? 7 : Holder >= 0 ? 5.5f : 3.5f;
        const FVector Center = Base + FVector(0, 0, Height * .5f + Lift);
        if (U.Dummy)
        {
            // A training target: a paper drum on a kraft stake with a bullseye on its lid.
            const float Top = 118;
            Solid(MeshBlock, Base + FVector(0, 0, 30), FVector(16, 16, 60), 0, GWPalette::Kraft, 0, 2.5f);
            Solid(MeshCoin, Base + FVector(0, 0, 60 + (Top - 60) * .5f), FVector(2 * R, 2 * R, Top - 60), 0, GWPalette::Paper, Flash, 3.5f);
            Decal(DecalRing, Base + FVector(0, 0, Top + 1), FVector2D(R * 1.7f), 0, GWPalette::Coral.CopyWithNewOpacity(.95f));
            Decal(DecalRing, Base + FVector(0, 0, Top + 1.2f), FVector2D(R * .95f), 0, GWPalette::Coral.CopyWithNewOpacity(.9f));
            Decal(DecalDot, Base + FVector(0, 0, Top + 1.4f), FVector2D(R * .45f), 0, GWPalette::Coral);
            Decal(DecalShadow, Base + FVector(0, 0, .6f), FVector2D(R * 2.6f), 0, FLinearColor(0, 0, 0, .5f));
            continue;
        }
        switch (U.Shape)
        {
        case 0:
            Solid(MeshCoin, Center, FVector(2 * R, 2 * R, Height), Yaw, Color, Flash, InkWidth, Outline);
            break;
        case 1:
            Solid(MeshBlock, Center, FVector(1.8f * R, 1.8f * R, Height), Yaw, Color, Flash, InkWidth, Outline);
            break;
        case 2:
            Solid(MeshBlock, Center, FVector(2.6f * R, 1.3f * R, Height * .9f), Yaw, Color, Flash, InkWidth, Outline);
            break;
        case 4:
            Solid(MeshPent, Center, FVector(2.1f * R, 2.1f * R, Height), Yaw, Color, Flash, InkWidth, Outline);
            break;
        case 5:
            Solid(MeshHex, Center, FVector(2.2f * R, 2.2f * R, Height), Yaw, Color, Flash, InkWidth, Outline);
            break;
        case 6:
            Solid(MeshTrap, Center, FVector(2.2f * R, 2 * R, Height * .8f), Yaw, Color, Flash, InkWidth, Outline);
            break;
        default:
            Solid(MeshTri, Center, FVector(MeshSizes[MeshTri].X * R / 40, MeshSizes[MeshTri].Y * R / 40, Height), Yaw,
                  Color, Flash, InkWidth, Outline);
            break;
        }
        if (U.Kind == 0 && U.Human)
            if (const int32* Worn = HatOf.Find(U.Id))
                Hat(*Worn, Base + FVector(0, 0, Height + Lift), R, Yaw, Time);
        if (U.Turret)
        {
            // A pinned turret stands on a paper disc with a marigold pin through its middle.
            Solid(MeshCoin, Base + FVector(0, 0, 6), FVector(R * 2.9f, R * 2.9f, 12), 0, GWPalette::PaperDeep, 0, 3);
            Solid(MeshStar, Base + FVector(0, 0, Height + Lift + 14), FVector(R * .7f, R * .7f, 14), Time * 30, GWPalette::Marigold, 0,
                  2.5f);
        }
        const FVector Aim = FRotator(0, U.Angle, 0).Vector(), Side(-Aim.Y, Aim.X, 0);
        if (U.Kind == 2)
        {
            // Two ink eyes give every monster a facing; the boss also wears a gold star.
            const float Eye = FMath::Clamp(R * .22f, 6.f, 26.f);
            for (float Sign : {-1.f, 1.f})
                Solid(MeshSphere, Base + Aim * R * .78f + Side * Sign * R * .34f + FVector(0, 0, Height * .7f + Lift),
                      FVector(Eye), 0, GWPalette::Paper, 0, Eye * .35f);
            if (R >= 100)
                Solid(MeshStar, Base + FVector(0, 0, Height + Lift + 30), FVector(R * .8f, R * .8f, 28), Time * 40,
                      GWPalette::Marigold, 0, 4);
        }
        else
        {
            const float Length = U.Weapon == 3 ? 58 : U.Weapon == 4 ? 48 : U.Weapon == 0 ? 30 : 42;
            const float Thick = U.Weapon == 5 ? 15 : U.Weapon == 2 ? 12 : 9;
            Solid(MeshBlock, Base + Aim * (R + Length * .5f - 8) + FVector(0, 0, Height * .55f + Lift),
                  FVector(Length, Thick, Thick), U.Angle, GWPalette::InkSoft, 0, 2);
        }
        Decal(DecalShadow, Base + FVector(0, 0, .6f), FVector2D(R * 2.7f), 0, FLinearColor(0, 0, 0, Launched ? .3f : .62f));
        if (Local && U.Id == Local->Id)
        {
            Decal(DecalLoop, Base + FVector(0, 0, 1.4f), FVector2D(R * 4), Time * 40, GWPalette::Marigold.CopyWithNewOpacity(.9f));
            const float Charge = C->ChargeProgress();
            const float Reach = R + 52 + FMath::Max(0.f, Charge) * 70;
            const FVector2D AimDir = C->MouseAim.IsNearlyZero() ? FVector2D(Aim.X, Aim.Y) : C->MouseAim;
            const float AimYaw = FMath::RadiansToDegrees(FMath::Atan2(AimDir.Y, AimDir.X));
            Decal(DecalDiamond, Base + FVector(AimDir.X, AimDir.Y, 0) * Reach + FVector(0, 0, 2.2f),
                  FVector2D(26, 54 + FMath::Max(0.f, Charge) * 40), AimYaw - 90,
                  FMath::Lerp(GWPalette::Marigold, GWPalette::Danger, FMath::Max(0.f, Charge)).CopyWithNewOpacity(.95f));
            if (Charge >= 0)
                Decal(DecalRing, Base + FVector(0, 0, 1.8f), FVector2D(R * (2.6f + Charge * 3)), -Time * 90,
                      FMath::Lerp(GWPalette::Marigold, GWPalette::Danger, Charge).CopyWithNewOpacity(.85f));
        }
        else if (U.Human)
            Decal(DecalLoop, Base + FVector(0, 0, 1.4f), FVector2D(R * 3.4f), Time * 30, GWPalette::Seat(U.Seat).CopyWithNewOpacity(.75f));
        else if (U.Bot)
            Decal(DecalLoop, Base + FVector(0, 0, 1.4f), FVector2D(R * 3.2f), -Time * 30, GWPalette::Seat(U.Seat).CopyWithNewOpacity(.6f));
        // A thrown teammate lying as bait: pinned by a tack, a hatched patch under them and the pull radius.
        if (U.Kind == 0 && U.Bait > 0)
        {
            const FLinearColor Mark = GWPalette::Seat(U.Seat);
            const float Pulse = .5f + .5f * FMath::Sin(Time * 6);
            Decal(DecalHatch, Base + FVector(0, 0, 1.0f), FVector2D(R * 5.2f), Time * 25, Mark.CopyWithNewOpacity(.55f));
            Decal(DecalRing, Base + FVector(0, 0, 1.6f), FVector2D(S->BaitRadius * 2), -Time * 12, Mark.CopyWithNewOpacity(.25f + .2f * Pulse));
            Solid(MeshStar, Base + FVector(0, 0, Height + Lift + 22), FVector(R * .7f, R * .7f, 14), Time * 70, Mark, 0, 2.5f);
        }
        if (Idle)
            Decal(DecalLoop, Base + FVector(0, 0, 1.2f), FVector2D(R * (Candidate ? 3.6f : 2.8f)), -Time * 25,
                  (Candidate ? GWPalette::Marigold : GWPalette::Teal).CopyWithNewOpacity(Candidate ? 1 : .55f));
        if (U.Kind == 2 && R >= 50)
            Decal(DecalHex, Base + FVector(0, 0, 1.3f), FVector2D(R * 3.2f), Time * (R >= 100 ? 25 : 0),
                  (R >= 100 ? GWPalette::Danger : GWPalette::Magenta).CopyWithNewOpacity(.7f));
        if (U.FollowerOf >= 0)
        {
            const FGWUnitView* Leader = S->FindUnit(U.FollowerOf);
            uint32 PreviousOrder = 0;
            for (const auto& Other : S->Units)
                if (Other.Alive && Other.FollowerOf == U.FollowerOf && Other.TailOrder < U.TailOrder &&
                    Other.TailOrder > PreviousOrder)
                {
                    PreviousOrder = Other.TailOrder;
                    Leader = &Other;
                }
            FVector LeaderPosition;
            if (Leader && VisualPosition(Leader->Id, LeaderPosition))
            {
                // Dotted marker "rope" between consecutive squad members.
                const FVector A(Base.X, Base.Y, 2), B(LeaderPosition.X, LeaderPosition.Y, 2);
                const int32 Dots = FMath::Clamp(FMath::FloorToInt(FVector::Dist2D(A, B) / 18), 1, 12);
                for (int32 I = 1; I < Dots; ++I)
                    Decal(DecalDot, FMath::Lerp(A, B, I / float(Dots)), FVector2D(9), 0, GWPalette::Teal.CopyWithNewOpacity(.75f));
            }
        }
    }
    ProjectilePositions.Reset();
    // My own shots and my tail's draw at full strength; teammates' at half, so eight players stay readable.
    TSet<int32> MySquad;
    if (Local)
    {
        MySquad.Add(Local->Id);
        for (const auto& U : S->Units)
            if (U.FollowerOf == Local->Id)
                MySquad.Add(U.Id);
    }
    for (const auto& Shot : S->Projectiles)
    {
        // Every client flies each shot from where it was sent; walls stop it before the host's word arrives.
        gwgame::Vec2 Pos{Shot.Position.X, Shot.Position.Y};
        const gwgame::Vec2 From{Shot.Origin.X, Shot.Origin.Y};
        double Fraction = 1;
        gwgame::Vec2 Normal;
        if (Geometry.raycast(From, Pos,
                             Shot.Weapon == 5   ? 10
                             : Shot.Weapon >= 7 ? 8
                                                : 4,
                             Fraction, Normal))
            Pos = From + (Pos - From) * Fraction;
        FVector Position(Pos.x, Pos.y, Shot.Position.Z);
        ProjectilePositions.Add(Shot.Id, Position);
        if (!Show3D)
            continue;
        const bool Faded = Shot.Team == 1 && Local && !MySquad.Contains(Shot.Owner);
        const float Alpha = Faded ? .5f : 1.f, Shrink = Faded ? .8f : 1.f;
        const FVector Dir = FVector(Shot.Direction).GetSafeNormal2D();
        const float Yaw = Dir.Rotation().Yaw;
        const FLinearColor Trail = Shot.Team == 1 ? GWPalette::Marigold : GWPalette::Violet;
        if (Shot.Weapon == 9)
        {
            // Shooter bolts: dark violet darts, the highest-contrast thing on the page.
            Solid(MeshDiamond, Position, FVector(30, 13, 13), Yaw, GWPalette::Violet, 0, 2.5f);
            Decal(DecalStreak, Position - Dir * 45, FVector2D(90, 16), Yaw, GWPalette::Night.CopyWithNewOpacity(.8f));
        }
        else if (Shot.Weapon == 6)
        {
            Solid(MeshSphere, Position, FVector(11 * Shrink), 0, GWPalette::Teal, 0, 1.5f);
            Decal(DecalStreak, Position - Dir * 35, FVector2D(70, 12), Yaw, GWPalette::Teal.CopyWithNewOpacity(.8f * Alpha));
        }
        else if (Shot.Weapon >= 7)
        {
            const bool AlongY = MeshSizes[MeshSword].Y > MeshSizes[MeshSword].X;
            const FVector Size = (AlongY ? FVector(16, 66, 8) : FVector(66, 16, 8)) * Shrink;
            Solid(MeshSword, Position, Size, Yaw + (AlongY ? -90 : 0), Shot.Weapon == 8 ? GWPalette::Marigold : GWPalette::Spark, 0,
                  2);
            Decal(DecalStreak, Position - Dir * 60, FVector2D(110, 14), Yaw, Trail.CopyWithNewOpacity(.6f * Alpha));
        }
        else if (Shot.Weapon == 5)
        {
            Solid(MeshDiamond, Position, FVector(34, 14, 14) * Shrink, Yaw, GWPalette::Fire, 0, 2.5f);
            Decal(DecalStreak, Position - Dir * 55, FVector2D(100, 18), Yaw, GWPalette::PaperDeep.CopyWithNewOpacity(.85f * Alpha));
        }
        else
        {
            const float Size = (Shot.Weapon == 3 ? 12 : 9) * Shrink;
            Solid(MeshSphere, Position, FVector(Size), 0, GWPalette::InkSoft, 0, 0);
            const float Length = Shot.Weapon == 3 ? 120 : Shot.Weapon == 4 ? 70 : 55;
            Decal(DecalStreak, Position - Dir * Length * .5f, FVector2D(Length, Size * 1.3f), Yaw, Trail.CopyWithNewOpacity(.85f * Alpha));
        }
    }
    // Pouring ink: a dotted stream of ink from the local player to the teammate being filled.
    if (Show3D && Local && Local->Alive && C->PourTarget >= 0)
    {
        FVector From = Local->Position, To;
        VisualPosition(Local->Id, From);
        if (const FGWUnitView* Mate = S->FindUnit(C->PourTarget); Mate && VisualPosition(Mate->Id, To))
        {
            const int32 Dots = FMath::Clamp(FMath::FloorToInt(FVector::Dist2D(From, To) / 40), 2, 40);
            for (int32 I = 1; I < Dots; ++I)
            {
                const float T = FMath::Fmod(I / float(Dots) + Time * .8f, 1.f);
                Decal(DecalDot, FMath::Lerp(From, To, T) + FVector(0, 0, 3 + 30 * FMath::Sin(T * PI)), FVector2D(16), 0,
                      GWPalette::Hex(0x2B3A67).CopyWithNewOpacity(.85f));
            }
        }
    }
    else if (Show3D && Local && Local->Alive && C->HoverMate >= 0)
        if (const FGWUnitView* Mate = S->FindUnit(C->HoverMate))
        {
            FVector At = Mate->Position;
            VisualPosition(Mate->Id, At);
            Decal(DecalRing, FVector(At.X, At.Y, 2), FVector2D(Mate->Radius * 4.2f), Time * 60, GWPalette::Hex(0x2B3A67).CopyWithNewOpacity(.8f));
        }
    if (Show3D && S->Defense() && CoreCell >= 0)
        DrawDefense(S, C, Local, Time);
    if (Show3D && S->Hub() && Geometry.width > 0)
        DrawHub(S, Local, Time);
    if (Show3D && S->Tutorial && CoreCell >= 0 && !S->Finished)
        DrawTutorial(S, Time);
    if (Show3D)
    {
        for (const auto& Crate : S->Crates)
        {
            const FVector Ground(Crate.Position.X, Crate.Position.Y, 0);
            const float Drop = FMath::Min(500.f, Crate.DropTime * 250);
            Solid(MeshBlock, Ground + FVector(0, 0, 26 + Drop), FVector(52), 18, GWPalette::Kraft, 0, 3.5f);
            Solid(MeshDiamond, Ground + FVector(0, 0, 76 + Drop + 5 * FMath::Sin(Time * 4)), FVector(16, 16, 24), Time * 120,
                  GWPalette::Cobalt, 0, 2);
            Decal(DecalRing, Ground + FVector(0, 0, 1.5f), FVector2D(130), Time * 30, GWPalette::Cobalt.CopyWithNewOpacity(.7f));
            Decal(DecalShadow, Ground + FVector(0, 0, .6f), FVector2D(70 + Drop * .1f), 0, FLinearColor(0, 0, 0, .55f));
        }
    }
    const FGWCinematic& Cinematic = FGWCinematic::Get();
    auto* Settings = UGWUserSettings::Get();
    float WantPitch = 55, WantDistance = Settings ? Settings->CameraDistance : 1650;
    // The tutorial follows the player (a little closer than a wave) until the drawing step needs the whole
    // page; the hub pulls back far enough to show a third of the room around the player.
    const bool CloseLesson = S->Tutorial && S->TutorialStep < static_cast<uint8>(gwgame::TutorialStep::Draw);
    if (Show3D && S->Hub())
        WantDistance *= 1.7f;
    else if (Show3D && CloseLesson && !S->Finished)
        WantDistance *= 1.45f;
    else if (Show3D && S->Defense() && CoreCell >= 0 && !S->Finished)
    {
        if (S->Phase == static_cast<uint8>(gwgame::DefensePhase::Build))
        {
            // Planning view: the whole page from high above, so the maze can be drawn as a plan. The focus
            // sits a little toward the near (bottom) edge, which perspective and the HUD cards would hide.
            Focus = FVector(Geometry.sizeX() * .5f, Geometry.sizeY() * .6f, 0);
            WantPitch = 66;
            WantDistance = FMath::Clamp(FMath::Max(Geometry.sizeX() * 1.3f, Geometry.sizeY() * 2.1f), 2600.f, 11000.f);
        }
        else
            WantDistance *= 1.6f;
    }
    ViewPitch = FMath::FInterpTo(ViewPitch, WantPitch, Delta, 4);
    ViewDistance = FMath::FInterpTo(ViewDistance, WantDistance, Delta, 4);
    CameraFocus = Cinematic.Active ? Cinematic.Focus : FMath::VInterpTo(CameraFocus, Focus, Delta, 7);
    const float Distance = (Cinematic.Active ? Cinematic.Distance : ViewDistance) * .9f;
    const float PitchDegrees = Cinematic.Active ? Cinematic.Pitch : ViewPitch, YawDegrees = Cinematic.Active ? Cinematic.Yaw : -90.f;
    const float Pitch = FMath::DegreesToRadians(PitchDegrees), Yaw = FMath::DegreesToRadians(YawDegrees);
    const FVector Shake = Show3D && Fx ? Fx->CameraShake(Delta) : FVector::ZeroVector;
    const FVector Back(-FMath::Cos(Yaw) * FMath::Cos(Pitch), -FMath::Sin(Yaw) * FMath::Cos(Pitch), FMath::Sin(Pitch));
    Camera->SetWorldLocation(CameraFocus + Shake + Back * Distance);
    Camera->SetWorldRotation(FRotator(-PitchDegrees, YawDegrees, 0));
    if (Fx)
    {
        if (Show3D)
            Fx->Update(*S, *this, CameraFocus, Delta);
        else if (WasInMatch)
            Fx->Stop();
    }
    for (int32 I = 0; I < MeshCount; ++I)
    {
        Upload(Bodies[I], BodyBatches[I]);
        Upload(Hulls[I], HullBatches[I]);
    }
    for (int32 I = 0; I < DecalCount; ++I)
        Upload(Decals[I], DecalBatches[I]);
    Floor->SetVisibility(Show3D);
    WallBodies->SetVisibility(Show3D);
    WallHulls->SetVisibility(Show3D);
    UpdateAudio(S, Focus, Delta);
    WasInMatch = S->InMatch;
}

void AGWWorld::UpdateAudio(const AGWGameState* S, FVector Focus, float Delta)
{
    auto* Settings = UGWUserSettings::Get();
    Music->SetVolumeMultiplier(Settings ? Settings->MusicVolume : .45f);
    // Defense keeps the calm menu theme while drawing and the battle themes once a wave marches.
    // The hub keeps the calm theme too.
    const bool Drawing = (S->Defense() && S->Phase == static_cast<uint8>(gwgame::DefensePhase::Build)) || S->Hub();
    FString Track = !S->InMatch || Drawing ? TEXT("Battle")
                    : S->Finished           ? TEXT("Results")
                    : S->BossActive         ? TEXT("EvolutionBoss")
                                            : TEXT("EvolutionDusk");
    if (Track != LastMusicTrack)
    {
        Music->Stop();
        FString Path = FString::Printf(TEXT("/Game/Audio/GeometricWarfare/%s.%s"), *Track, *Track);
        Music->SetSound(LoadObject<USoundBase>(nullptr, *Path));
        Music->FadeIn(.35f);
        LastMusicTrack = Track;
    }
    if (Music->Sound && !Music->IsPlaying())
        Music->Play();
    AudioTime -= Delta;
    int32 MaxShot = LastProjectile;
    for (auto& Shot : S->Projectiles)
    {
        MaxShot = FMath::Max(MaxShot, Shot.Id);
        // Turret bolts borrow the rifle; enemy bolts stay silent so the players' guns read clearly.
        if (Shot.Id > LastProjectile && AudioTime <= 0 && Shot.Weapon != 9 && (FVector(Shot.Position) - Focus).Size2D() < 1200)
        {
            const TCHAR* Names[] = {TEXT("Pistol"), TEXT("Rifle"), TEXT("Shotgun"), TEXT("Sniper"), TEXT("MachineGun"), TEXT("Rocket")};
            int32 Weapon = Shot.Weapon == 6 ? 1 : FMath::Clamp(static_cast<int32>(Shot.Weapon), 0, 5);
            FString Path = FString::Printf(TEXT("/Game/Audio/GeometricWarfare/%s.%s"), Names[Weapon], Names[Weapon]);
            auto* Sound = LoadObject<USoundBase>(nullptr, *Path);
            if (Sound)
                UGameplayStatics::PlaySound2D(this, Sound, (Settings ? Settings->EffectsVolume : .7f) * .45f);
            AudioTime = .10f;
        }
    }
    LastProjectile = MaxShot;
}

bool AGWWorld::VisualPosition(int32 Id, FVector& Position) const
{
    if (const auto* P = SmoothedPositions.Find(Id))
    {
        Position = *P;
        return true;
    }
    return false;
}

bool AGWWorld::CanGrab(const AGWGameState* S, const FGWUnitView& L, const FGWUnitView& T)
{
    if (!S || S->Prank == static_cast<uint8>(gwgame::PrankLevel::Off) || !T.Human || !T.Alive || !L.Alive || T.Id == L.Id ||
        T.Team != L.Team || T.FollowerOf >= 0 || L.FollowerOf >= 0 || T.State == 4 || T.Bait > 0 || T.Guard > 0 || L.Bait > 0)
        return false;
    if (S->Prank != static_cast<uint8>(gwgame::PrankLevel::Chaos) && !T.Throwable)
        return false;
    for (const auto& U : S->Units)
        if (U.Alive && U.Carried && (U.FollowerOf == L.Id || U.FollowerOf == T.Id))
            return false;
    return true;
}

bool AGWWorld::VisualProjectile(int32 Id, FVector& Position) const
{
    if (const auto* P = ProjectilePositions.Find(Id))
    {
        Position = *P;
        return true;
    }
    return false;
}

void AGWWorld::RebuildRoutes()
{
    Routes.Reset();
    if (CoreCell < 0)
        return;
    const std::vector<double> Costs = gwgame::routeCosts(Geometry, CoreCell, false);
    for (int32 Gate : Entrances)
    {
        const std::vector<int> Cells = gwgame::routeCells(Geometry, Costs, Gate, CoreCell);
        TArray<FVector>& Dots = Routes.AddDefaulted_GetRef();
        for (size_t I = 1; I < Cells.size(); ++I)
        {
            const gwgame::Vec2 A = Geometry.center(Cells[I - 1]), B = Geometry.center(Cells[I]);
            const double Length = (B - A).length();
            for (double T = 0; T < Length; T += 90)
            {
                const gwgame::Vec2 P = A + (B - A) * (T / Length);
                Dots.Add(FVector(P.x, P.y, 2.5f));
            }
        }
    }
}

bool AGWWorld::CanDraw(const AGWGameState* S, const FGWUnitView* Local, int32 Cell) const
{
    if (!S || !Local || !Local->Alive || !S->Cells.IsValidIndex(Cell) || Cell >= static_cast<int32>(Geometry.walls.size()))
        return false;
    const uint8 Flags = S->Cells[Cell];
    if (!(Flags & EGWCell::Paper) || (Flags & (EGWCell::Rock | EGWCell::Core | EGWCell::Entrance)))
        return false;
    if (Flags & EGWCell::Wall)
        return Geometry.integrity(Cell) < .999 && Local->Ink >= 1;
    if (Local->Ink + 1e-3f < 10)
        return false;
    const float Size = Geometry.cellSize;
    const FVector2D Lo((Cell % Geometry.width) * Size, (Cell / Geometry.width) * Size);
    for (const auto& U : S->Units)
        if (U.Alive)
        {
            const FVector2D Nearest(FMath::Clamp<float>(U.Position.X, Lo.X, Lo.X + Size), FMath::Clamp<float>(U.Position.Y, Lo.Y, Lo.Y + Size));
            if (FVector2D::DistSquared(Nearest, FVector2D(U.Position.X, U.Position.Y)) < U.Radius * U.Radius)
                return false;
        }
    std::vector<int> Gates;
    for (int32 Gate : Entrances)
        Gates.push_back(Gate);
    return gwgame::routeOpen(Geometry, Gates, CoreCell, Cell);
}

// The page around the fight: keep-clear paper, entrances, the core, the marching route, the drawing cursor,
// ink drops and the boss beam.
void AGWWorld::DrawDefense(const AGWGameState* S, const AGWController* C, const FGWUnitView* Local, float Time)
{
    using namespace GWPalette;
    using gwgame::EffectStyle;
    const float Size = Geometry.cellSize;
    const bool Building = S->Phase == static_cast<uint8>(gwgame::DefensePhase::Build);
    for (int32 Cell = 0; Cell < S->Cells.Num(); ++Cell)
    {
        const uint8 Flags = S->Cells[Cell];
        const gwgame::Vec2 Center = Geometry.center(Cell);
        const FVector Ground(Center.x, Center.y, 1.1f);
        if (Flags & EGWCell::Entrance)
        {
            Decal(DecalHatch, Ground, FVector2D(Size - 30), 0, Coral.CopyWithNewOpacity(.6f));
            Decal(DecalRing, Ground + FVector(0, 0, .4f), FVector2D(Size * .8f), Time * 40, Coral.CopyWithNewOpacity(.75f));
        }
        else if (!(Flags & (EGWCell::Paper | EGWCell::Rock | EGWCell::Wall | EGWCell::Core)))
            Decal(DecalHatch, Ground, FVector2D(Size - 40), 0, Pencil.CopyWithNewOpacity(.2f));
    }
    // Entrance chevrons point down the road the enemies will take.
    for (int32 I = 0; I < Routes.Num(); ++I)
        if (Routes[I].Num() >= 2)
        {
            const FVector Dir = (Routes[I][1] - Routes[I][0]).GetSafeNormal2D();
            const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
            for (int32 K = 0; K < 3; ++K)
            {
                const float Step = FMath::Fmod(Time * 1.5f + K / 3.f, 1.f);
                Decal(DecalDiamond, Routes[I][0] + Dir * (Step * 220 - 60) + FVector(0, 0, 1.5f), FVector2D(40, 70), Yaw - 90,
                      Coral.CopyWithNewOpacity(FMath::Sin(Step * PI)));
            }
        }
    for (const auto& Dots : Routes)
        for (int32 I = 2; I < Dots.Num(); ++I)
        {
            const float Wave = .5f + .5f * FMath::Sin(Time * 5 - I * .55f);
            Decal(DecalDot, Dots[I], FVector2D(15 + 9 * Wave), 0, Coral.CopyWithNewOpacity((Building ? .4f : .2f) + .4f * Wave));
        }
    // The core: a paper plinth, a crystal that drains from teal to red, a ring that shrinks with its health.
    const float Health = S->CoreMaxHP > 0 ? FMath::Clamp(S->CoreHP / S->CoreMaxHP, 0.f, 1.f) : 0;
    float Flash = 0;
    for (const auto& E : S->Effects)
        if (static_cast<EffectStyle>(E.Style) == EffectStyle::CoreHit)
            Flash = FMath::Max(Flash, FMath::Clamp((E.Remaining - .2f) / .3f, 0.f, 1.f));
    const FLinearColor CoreColor = FMath::Lerp(Danger, Teal, Health);
    const FVector Core = S->CorePosition;
    const float Radius = gwgame::DefenseState::CoreRadius;
    Decal(DecalShadow, Core + FVector(0, 0, .6f), FVector2D(Radius * 2.6f), 0, FLinearColor(0, 0, 0, .5f));
    Decal(DecalHex, Core + FVector(0, 0, 1.3f), FVector2D(Size * .95f), Time * 12, CoreColor.CopyWithNewOpacity(.55f));
    Solid(MeshHex, Core + FVector(0, 0, 30), FVector(Radius * 1.9f, Radius * 1.9f, 60), 0, PaperDeep, 0, 4);
    Solid(MeshHex, Core + FVector(0, 0, 80), FVector(Radius * 1.25f, Radius * 1.25f, 40), 30, CoreColor, Flash, 4);
    Solid(MeshDiamond, Core + FVector(0, 0, 215 + 14 * FMath::Sin(Time * 2)), FVector(Radius * .8f, Radius * .8f, 170), Time * 35, CoreColor,
          Flash, 4);
    Decal(DecalRing, Core + FVector(0, 0, 1.7f), FVector2D(Radius * 2.6f * (.55f + .45f * Health)), -Time * 20,
          CoreColor.CopyWithNewOpacity(.85f));
    // Drawing cursor: teal where a wall can go, red where it cannot (the HUD says why), coral to erase.
    if (C && Local && Local->Alive && C->HoverCell >= 0 && !S->Finished && S->Cells.IsValidIndex(C->HoverCell))
    {
        const int32 Cell = C->HoverCell;
        const uint8 Flags = S->Cells[Cell];
        const bool Wall = (Flags & EGWCell::Wall) != 0, OnPaper = (Flags & EGWCell::Paper) != 0;
        const bool Erase = C->Painting ? C->PaintErase : Wall && Geometry.integrity(Cell) > .95f;
        const bool Allowed = OnPaper && (Erase || CanDraw(S, Local, Cell));
        const FLinearColor Tint = !OnPaper ? Pencil : Erase ? Coral : Allowed ? (Wall ? Marigold : Teal) : Danger;
        const gwgame::Vec2 Center = Geometry.center(Cell);
        const float Z = Wall ? 143.f : 3.f;
        Decal(DecalSquare, FVector(Center.x, Center.y, Z), FVector2D(Size * .98f), 0, Tint.CopyWithNewOpacity(OnPaper ? .95f : .5f));
        if (OnPaper && !Allowed)
            Decal(DecalPlus, FVector(Center.x, Center.y, Z + .5f), FVector2D(Size * .45f), 45, Danger.CopyWithNewOpacity(.9f));
    }
    const FLinearColor InkBlue = Hex(0x2B3A67);
    for (const auto& Drop : S->InkDrops)
    {
        const FVector Ground(Drop.Position.X, Drop.Position.Y, 0);
        Solid(MeshDiamond, Ground + FVector(0, 0, 34 + 4 * FMath::Sin(Time * 4 + Drop.Id)), FVector(18, 18, 28), Time * 90 + Drop.Id * 40,
              InkBlue, 0, 2);
        Decal(DecalDot, Ground + FVector(0, 0, 1.2f), FVector2D(30), 0, InkBlue.CopyWithNewOpacity(.55f));
    }
    for (const auto& E : S->Effects)
    {
        const auto Style = static_cast<EffectStyle>(E.Style);
        if (Style != EffectStyle::LaserTelegraph && Style != EffectStyle::Laser)
            continue;
        const FVector A(E.From.X, E.From.Y, 3), B(E.To.X, E.To.Y, 3);
        const float Length = FVector::Dist2D(A, B), Yaw = FMath::RadiansToDegrees(FMath::Atan2(B.Y - A.Y, B.X - A.X));
        if (Length < 1)
            continue;
        if (Style == EffectStyle::LaserTelegraph)
            Decal(DecalStreak, (A + B) * .5f, FVector2D(Length, E.Radius * 2), Yaw, Danger.CopyWithNewOpacity(.3f + .3f * FMath::Sin(Time * 18)));
        else
        {
            const float Fade = FMath::Clamp(E.Remaining / .55f, 0.f, 1.f);
            Decal(DecalStreak, (A + B) * .5f + FVector(0, 0, 1), FVector2D(Length, E.Radius * 2.4f), Yaw, Spark.CopyWithNewOpacity(Fade));
            Solid(MeshBlock, (A + B) * .5f + FVector(0, 0, 70), FVector(Length, 30 * Fade + 4, 30 * Fade + 4), Yaw, Danger, 1, 0);
        }
    }
}

// The hub: every station is built from the same card-stock solids as the arena, standing in its wall cell and
// facing into the room, with a geometric keeper beside it. Station-local coordinates: u runs along the wall, v
// from the station's middle toward the room (its inner face is at v = +200), z up.
void AGWWorld::DrawHub(const AGWGameState* S, const FGWUnitView* Local, float Time)
{
    using namespace GWPalette;
    using gwgame::HubStation;
    const float Size = Geometry.cellSize, Fit = Size / 400.f;
    // The training mat, the spawn mark and the bullseye on the floor.
    for (int32 Cell = 0; Cell < S->Cells.Num(); ++Cell)
        if (S->Cells[Cell] & EGWCell::Paper)
        {
            const gwgame::Vec2 C = Geometry.center(Cell);
            Decal(DecalHatch, FVector(C.x, C.y, 1.0f), FVector2D(Size - 24), 0, Leaf.CopyWithNewOpacity(.22f));
            Decal(DecalSquare, FVector(C.x, C.y, 1.1f), FVector2D(Size - 10), 0, Leaf.CopyWithNewOpacity(.35f));
        }
    const std::vector<int> Spawn = gwgame::hubCells('P'), Eye = gwgame::hubCells('o');
    if (!Spawn.empty())
    {
        const gwgame::Vec2 C = Geometry.center(Spawn[0]);
        Decal(DecalHex, FVector(C.x, C.y, 1.2f), FVector2D(Size * .9f), Time * 8, Teal.CopyWithNewOpacity(.28f));
    }
    for (const auto& E : S->Effects)
        if (static_cast<gwgame::EffectStyle>(E.Style) == gwgame::EffectStyle::Bullseye && E.Id > SeenBullseye)
        {
            SeenBullseye = E.Id;
            LastBullseye = Time;
        }
    if (!Eye.empty())
    {
        const gwgame::Vec2 C = Geometry.center(Eye[0]);
        const FVector At(C.x, C.y, 1.3f);
        const float Hit = FMath::Clamp(1.f - (Time - static_cast<float>(LastBullseye)) / .8f, 0.f, 1.f);
        const float R = gwgame::BullseyeRadius;
        Decal(DecalRing, At, FVector2D(R * 2.f * (1 + .25f * Hit)), 0, Coral.CopyWithNewOpacity(.85f));
        Decal(DecalRing, At + FVector(0, 0, .2f), FVector2D(R * 1.3f), 0, Coral.CopyWithNewOpacity(.8f));
        Decal(DecalRing, At + FVector(0, 0, .4f), FVector2D(R * .65f), 0, Coral.CopyWithNewOpacity(.8f));
        Decal(DecalDot, At + FVector(0, 0, .6f), FVector2D(R * .3f), 0, Coral);
        Decal(DecalLoop, At + FVector(0, 0, .8f), FVector2D(R * 2.4f), Time * 30, Marigold.CopyWithNewOpacity(.35f + .6f * Hit));
    }
    const int32 Near = Local && Local->Alive ? gwgame::hubStationNear({Local->Position.X, Local->Position.Y}) : -1;
    const auto& Spots = gwgame::hubSpots();
    for (int32 I = 0; I < static_cast<int32>(Spots.size()); ++I)
    {
        const gwgame::HubSpot& Spot = Spots[I];
        const FVector2D F(Spot.facing.x, Spot.facing.y), T(F.Y, -F.X);
        const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(T.Y, T.X));
        // Station props are modelled in 400-unit cells (U along the wall, V into it, Z up) and scaled by Fit
        // to the hub's own cells; Span is the station's width in those model units.
        const float Span = (FMath::Abs(T.X) > .5f ? Spot.maxX - Spot.minX + 1 : Spot.maxY - Spot.minY + 1) * 400.f;
        auto At = [&](float U, float V, float Z)
        { return FVector(Spot.center.x + (T.X * U + F.X * V) * Fit, Spot.center.y + (T.Y * U + F.Y * V) * Fit, Z * Fit); };
        auto Box = [&](float U, float V, float Z, float SU, float SV, float SZ, const FLinearColor& Color, float InkWidth = 3.f)
        { Solid(MeshBlock, At(U, V, Z), FVector(SU, SV, SZ) * Fit, Yaw, Color, 0, InkWidth); };
        // A plinth fills the station's cells so it reads as furniture built into the wall.
        Box(0, -10, 22, Span - 16, 370, 44, PaperDeep * (.95f + .05f * Hash(I)), 3.5f);
        // Where to stand to use it: a ring on the floor in front, brighter for the station in reach.
        const FLinearColor Mark = I == 0 ? Coral : I == 1 ? Marigold : I == 2 ? Teal : I == 3 ? Cobalt : I == 4 ? Plum : I == 5 ? Hex(0x17A3BF) : Leaf;
        Decal(DecalLoop, FVector(Spot.front.x, Spot.front.y, 1.5f), FVector2D((I == Near ? 300 : 230) * Fit), Time * (I == Near ? 50 : 15),
              Mark.CopyWithNewOpacity(I == Near ? .95f : .35f));
        const float Bob = 6 * FMath::Sin(Time * 2.2f + I);
        // A keeper: a geometric body with two ink eyes looking into the room.
        // Raise lifts it onto something (model units), e.g. the box behind the shop counter.
        auto Keeper = [&](int32 Mesh, float U, float V, float Radius, const FLinearColor& Body, float Raise = 0)
        {
            const FVector Feet = At(U, V, Raise);
            Radius *= Fit;
            const float Tall = Radius * 1.25f, Stand = 44 * Fit;
            Solid(Mesh, Feet + FVector(0, 0, Stand + Tall * .5f + Bob), FVector(2.1f * Radius, 2.1f * Radius, Tall), Yaw, Body, 0, 3.5f);
            const FVector Side(T.X, T.Y, 0), Front(F.X, F.Y, 0);
            for (float Sign : {-1.f, 1.f})
                Solid(MeshSphere, Feet + Front * Radius * .8f + Side * Sign * Radius * .34f + FVector(0, 0, Stand + Tall * .75f + Bob), FVector(Radius * .26f),
                      0, Paper, 0, Radius * .09f);
            Decal(DecalShadow, Feet + FVector(0, 0, Stand + 1), FVector2D(Radius * 2.6f), 0, FLinearColor(0, 0, 0, .45f));
        };
        switch (static_cast<HubStation>(I))
        {
        case HubStation::Board:
        {
            // A cork board on two posts, pinned with page notes (one per page of the chapter).
            const float BoardW = Span - 220, Gap = BoardW * .3f;
            Box(-BoardW * .5f + 30, 120, 190, 24, 24, 300, Kraft);
            Box(BoardW * .5f - 30, 120, 190, 24, 24, 300, Kraft);
            Box(0, 132, 230, BoardW, 26, 230, Hex(0xC79A62));
            const FLinearColor Notes[] = {Paper, Hex(0xFFF2B0), Mint};
            for (int32 K = 0; K < 3; ++K)
            {
                Box((K - 1) * Gap, 150, 225 + (K % 2) * 22, 150, 8, 110, Notes[K], 2.f);
                Solid(MeshStar, At((K - 1) * Gap, 158, 272 + (K % 2) * 22), FVector(24, 24, 10) * Fit, Time * 20 + K * 40, Seat(K), 0, 2.f);
            }
            // The clerk stands in front of the board's right half.
            Keeper(MeshHex, BoardW * .5f - 130, 150, 46, Cobalt);
            break;
        }
        case HubStation::Achievements:
        {
            // A long wall of frames waiting for the room's achievements (phase 4), a crown on top.
            Box(0, 140, 170, Span - 160, 26, 240, Paper);
            for (int32 K = 0; K < 10; ++K)
            {
                const float U = -Span * .5f + 150 + (K % 5) * (Span - 300) / 4, Z = K < 5 ? 230 : 130;
                Solid(MeshCoin, At(U, 158, Z), FVector(64, 64, 10) * Fit, 0, K % 3 ? PaperShade : Marigold, 0, 2.5f);
                Solid(K % 2 ? MeshStar : MeshDiamond, At(U, 166, Z), FVector(26, 26, 10) * Fit, Time * 15 + K * 30, K % 3 ? PaperDeep : Spark, 0, 1.5f);
            }
            Solid(MeshStar, At(0, 140, 330 + Bob), FVector(70, 70, 22) * Fit, Time * 25, Marigold, 0, 3.f);
            break;
        }
        case HubStation::Shop:
        {
            // A counter under a striped awning, crates of triangles on it, a pentagon shopkeeper behind.
            Box(0, 110, 60, Span - 120, 120, 110, Kraft);
            for (int32 K = 0; K < 5; ++K)
                Box(-Span * .5f + 120 + K * (Span - 240) / 4, 120, 315, (Span - 240) / 4 + 6, 210, 14, K % 2 ? Paper : Coral, 2.f);
            Box(-Span * .5f + 70, 150, 160, 20, 20, 320, Kraft);
            Box(Span * .5f - 70, 150, 160, 20, 20, 320, Kraft);
            for (int32 K = 0; K < 3; ++K)
            {
                Box(-160 + K * 150, 100, 145, 80, 70, 60, Hex(0xE7C796), 2.5f);
                Solid(MeshTri, At(-160 + K * 150, 100, 190 + 4 * FMath::Sin(Time * 3 + K)), FVector(36, 36, 26) * Fit, Time * 40 + K * 60, Teal, 0, 2.f);
            }
            // The shopkeeper stands on a box behind the counter, head and shoulders above it.
            Box(-265, -20, 57, 110, 90, 114, Hex(0xE7C796), 2.5f);
            Keeper(MeshPent, -265, -20, 44, Teal, 70);
            break;
        }
        case HubStation::Research:
        {
            // A desk with a lamp and beakers before a chalkboard; a triangle in goggles minds it.
            Box(0, -130, 190, Span - 160, 20, 210, Night);
            Box(0, 110, 55, Span - 200, 150, 100, Kraft);
            Solid(MeshCoin, At(-180, 110, 112), FVector(70, 70, 12) * Fit, 0, InkSoft, 0, 2.f);
            Box(-180, 110, 170, 10, 10, 110, InkSoft, 2.f);
            Solid(MeshCoin, At(-140, 110, 230), FVector(90, 90, 40) * Fit, 0, Marigold, 0, 2.5f);
            for (int32 K = 0; K < 3; ++K)
                Solid(MeshCoin, At(20 + K * 70, 120, 130 + K * 6), FVector(40, 40, 50 + K * 12) * Fit, 0, K == 1 ? Spark : Frost, 0, 2.f);
            Keeper(MeshTri, 170, -40, 44, Marigold);
            break;
        }
        case HubStation::Wardrobe:
        {
            // A tall cabinet with a mirror and hats on top; a square tailor beside it.
            Box(-60, 80, 170, Span - 300, 150, 320, Hex(0x8A6E9E));
            Box(-60, 158, 170, 6, 6, 300, Night, 1.5f);
            Box(Span * .5f - 110, 140, 150, 110, 10, 220, Frost, 2.f);
            Solid(MeshTrap, At(-140, 80, 352), FVector(90, 70, 40) * Fit, Yaw, Coral, 0, 2.5f);
            Solid(MeshTrap, At(20, 80, 352), FVector(80, 64, 36) * Fit, Yaw, Marigold, 0, 2.5f);
            Keeper(MeshBlock, Span * .5f - 110, -40, 40, Seat(4));
            break;
        }
        case HubStation::Door:
        {
            // An open doorway onto the dark outside, and the mailbox invites go out from.
            Box(-250, 150, 160, 40, 60, 320, Kraft);
            Box(250, 150, 160, 40, 60, 320, Kraft);
            Box(0, 150, 330, 540, 60, 40, Kraft);
            Box(0, 40, 160, 460, 20, 300, Night);
            Decal(DecalSquare, At(0, 170, 45.5f), FVector2D(460, 120) * Fit, Yaw, Hex(0x17A3BF).CopyWithNewOpacity(.5f));
            Box(330, 150, 70, 14, 14, 140, InkSoft, 2.f);
            Box(330, 150, 170, 90, 70, 70, Coral);
            Box(380, 150, 205 + 10 * FMath::Sin(Time * 2), 10, 10, 60, Marigold, 1.5f);
            break;
        }
        case HubStation::Training:
        {
            // A signboard on an easel listing the keys (the sheet behind E has them in full).
            Box(-200, 130, 110, 20, 20, 220, Kraft);
            Box(200, 130, 110, 20, 20, 220, Kraft);
            Box(0, 140, 200, 520, 20, 200, Paper);
            for (int32 K = 0; K < 3; ++K)
                Solid(K == 0 ? MeshCoin : K == 1 ? MeshTri : MeshBlock, At(-150 + K * 150, 154, 210), FVector(60, 60, 14) * Fit, Yaw, K == 0 ? Coral : K == 1 ? Teal : Marigold,
                      0, 2.f);
            Keeper(MeshCoin, 0, -40, 40, Leaf);
            break;
        }
        default:
            break;
        }
    }
}

// The tutorial's marks: a ring where the first step sends the player, and the three suggested cells of the
// drawing step (a short wall across the road).
void AGWWorld::DrawTutorial(const AGWGameState* S, float Time)
{
    using namespace GWPalette;
    const gwgame::TutorialPage& Page = gwgame::tutorialPage();
    const float Pulse = .5f + .5f * FMath::Sin(Time * 5);
    if (S->TutorialStep == static_cast<uint8>(gwgame::TutorialStep::Move))
    {
        const gwgame::Vec2 C = Geometry.center(Page.moveY * Geometry.width + Page.moveX);
        Decal(DecalRing, FVector(C.x, C.y, 2), FVector2D(300 + 40 * Pulse), Time * 40, Marigold.CopyWithNewOpacity(.95f));
        Decal(DecalLoop, FVector(C.x, C.y, 2.2f), FVector2D(220), -Time * 60, Coral.CopyWithNewOpacity(.8f));
        Solid(MeshDiamond, FVector(C.x, C.y, 120 + 18 * Pulse), FVector(40, 40, 70), Time * 90, Marigold, 0, 3);
    }
    if (S->TutorialStep == static_cast<uint8>(gwgame::TutorialStep::Draw))
        for (const auto& Cell : Page.wall)
        {
            const int32 Id = Cell.second * Geometry.width + S->TutorialWallX;
            if (S->Cells.IsValidIndex(Id) && (S->Cells[Id] & EGWCell::Wall))
                continue;
            const gwgame::Vec2 C = Geometry.center(Id);
            Decal(DecalSquare, FVector(C.x, C.y, 2.4f), FVector2D(Geometry.cellSize * (.86f + .06f * Pulse)), 0, Teal.CopyWithNewOpacity(.55f + .4f * Pulse));
            Decal(DecalHatch, FVector(C.x, C.y, 2.2f), FVector2D(Geometry.cellSize * .8f), 0, Teal.CopyWithNewOpacity(.25f));
        }
}
