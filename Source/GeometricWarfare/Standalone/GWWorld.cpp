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
#include "GWHUD.h"
#include "GWMotion.h"
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
    TEXT("/Game/Sketch/Materials/MI_GW_Decal_Plus.MI_GW_Decal_Plus"), TEXT("/Game/Sketch/Materials/MI_GW_Decal_Triangle.MI_GW_Decal_Triangle"),
    TEXT("/Game/Sketch/Materials/MI_GW_Decal_Compass.MI_GW_Decal_Compass"), TEXT("/Game/Sketch/Materials/MI_GW_Decal_HexRing.MI_GW_Decal_HexRing"),
    TEXT("/Game/Sketch/Materials/MI_GW_Decal_TriRing.MI_GW_Decal_TriRing")};
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
const float DeckTop = static_cast<float>(gwgame::DeckHeight);
// A tower in the box stands on a post on its block, half again the size of the turret it grew from.
const float TowerPost = 170, TowerScale = 1.5f;
// How high the floor is under a point: the platform's top over its cells, the paper elsewhere.
float FloorAt(const gwgame::Maze& M, FVector P)
{
    return M.isDeck(M.cell({P.X, P.Y})) ? DeckTop : 0.f;
}
// The height of a drawn block's top (a tower stands on it), as the walls are built in Rebuild.
float BlockTop(const gwgame::Maze& M, int32 Cell)
{
    if (Cell < 0 || Cell >= static_cast<int32>(M.walls.size()) || M.walls[Cell] <= 0 || M.isFixed(Cell))
        return 0;
    const float Full = M.kind(Cell) == gwgame::WallKind::Thick ? 165 : 140;
    const float HP = static_cast<float>(M.integrity(Cell) * 200);
    return HP >= 199.5f ? Full : 70 + HP * (Full - 70) / 200;
}
gwgame::Ladder LadderOf(const FGWLadderNet& N)
{
    gwgame::Ladder L;
    L.deck = N.Deck;
    L.pit = N.Pit;
    return L;
}
// The expedition's things on the floor: where a packed spot is, and an item's colour (gwgame::Gear).
FVector LootSpot(uint16 X, uint16 Y)
{
    return FVector(GWNet::UnpackCoord(X), GWNet::UnpackCoord(Y), 0);
}
FLinearColor LootTint(int32 Gear)
{
    static const uint32 Colors[gwgame::GearCount] = {0xF6F0E1, 0xD6BC90, 0xF7D84A, 0x2B3A67, 0xFFFFFF, 0xE8553F, 0xE23A33, 0x9AD8CB, 0xF2A93B,
                                                     0xC5CCD6, 0xF2C9A0, 0xF2A93B, 0x4B3F72, 0x5EC8E8, 0x9AD8CB, 0xC5CCD6, 0x2F6BD8, 0xB23A7A,
                                                     0x2F6BD8, 0xD6BC90, 0x3A9E45, 0xFFD84A, 0xE8553F, 0xFF6A2B, 0xD6BC90};
    return GWPalette::Hex(Colors[FMath::Clamp(Gear, 0, gwgame::GearCount - 1)]);
}
// A quality gun's shots (FGWShotNet::Tint): its quality's colour, the ice rifle's frost, a crayon round's own colour.
FLinearColor ShotTint(uint8 Tint)
{
    switch (Tint)
    {
    case 1:
    case 2:
        return GWPalette::Grade(1);
    case 3:
        return GWPalette::Frost;
    case 4:
        return GWPalette::Grade(2);
    case 5:
        return GWPalette::Volt;
    case 6:
        return GWPalette::Grade(4);
    case 7:
        return GWPalette::Danger;
    case 8:
        return GWPalette::Danger;
    case 9:
        return GWPalette::Cobalt;
    case 10:
        return GWPalette::Spark;
    case 11:
        return GWPalette::Leaf;
    default:
        return GWPalette::InkSoft;
    }
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
    if (JellyOn)
    {
        Center = JellyPivot + (Center - JellyPivot) * JellyScale;
        Size *= JellyScale;
    }
    const FQuat Rotation = FRotator(0, Yaw, 0).Quaternion();
    BodyBatches[Mesh].Add(FTransform(Rotation, Center, Size / MeshSizes[Mesh]), Color, Flash);
    if (Ink > 0)
        HullBatches[Mesh].Add(FTransform(Rotation, Center, (Size + FVector(Ink * 2)) / MeshSizes[Mesh]), Outline, 0);
}

void AGWWorld::Rotated(int32 Mesh, FVector Center, FVector Size, FRotator Rotation, const FLinearColor& Color, float Ink)
{
    if (!BodyBatches.IsValidIndex(Mesh))
        return;
    // A tilted part of a wobbling tower moves with it and scales evenly (its own axes are not the world's).
    if (JellyOn)
    {
        Center = JellyPivot + (Center - JellyPivot) * JellyScale;
        Size *= (JellyScale.X + JellyScale.Y + JellyScale.Z) / 3;
    }
    const FQuat Turn = Rotation.Quaternion();
    BodyBatches[Mesh].Add(FTransform(Turn, Center, Size / MeshSizes[Mesh]), Color, 0);
    if (Ink > 0)
        HullBatches[Mesh].Add(FTransform(Turn, Center, (Size + FVector(Ink * 2)) / MeshSizes[Mesh]), InkLine, 0);
}

void AGWWorld::TowerParts(FVector Foot, int32 Kind, int32 Sides, float Facing, float Time, float Flash)
{
    // The same parts the match draws for a tower in the box (a turret on a post, half again its size).
    using namespace GWPalette;
    const float R = 40 * TowerScale, Height = R * 1.1f;
    const FVector Base = Foot + FVector(0, 0, TowerPost), Center = Base + FVector(0, 0, Height * .5f);
    const FLinearColor Color = Sides >= 6 ? Marigold : Sides == 5 ? Leaf : Hex(0x3C8DBC);
    Solid(MeshBlock, Base - FVector(0, 0, TowerPost * .5f), FVector(R * 1.6f, R * 1.6f, TowerPost), 45, FMath::Lerp(Paper, Color, .45f), 0, 3);
    if (Kind == 1)
    {
        Solid(MeshCoin, Center, FVector(2 * R, 2 * R, Height), Facing, Color, Flash, 3.5f);
        const FVector Ahead = FRotator(0, Facing, 0).Vector();
        Rotated(MeshBlock, Base + FVector(0, 0, Height + 10) + Ahead * R * .3f, FVector(R * 1.3f, R * .55f, R * .55f), FRotator(40, Facing, 0), Graphite, 2.5f);
    }
    else if (Kind == 2)
    {
        Solid(MeshTri, Center, FVector(MeshSizes[MeshTri].X * R / 40, MeshSizes[MeshTri].Y * R / 40, Height), Facing, Color, Flash, 3.5f);
        for (int32 K = 0; K < 3; ++K)
            Solid(MeshCoin, Base + FVector(0, 0, Height + 14 + K * 16), FVector(R * (1.1f - .15f * K), R * (1.1f - .15f * K), 8), 0, Kraft, 0, 2);
        Solid(MeshSphere, Base + FVector(0, 0, Height + 70), FVector(R * .45f), 0, Spark, 0, 2);
    }
    else if (Sides >= 6)
        Solid(MeshHex, Center, FVector(2.2f * R, 2.2f * R, Height), Facing, Color, Flash, 3.5f);
    else if (Sides == 5)
        Solid(MeshPent, Center, FVector(2.1f * R, 2.1f * R, Height), Facing, Color, Flash, 3.5f);
    else
        Solid(MeshBlock, Center, FVector(1.8f * R, 1.8f * R, Height), Facing, Color, Flash, 3.5f);
    Solid(MeshCoin, Base + FVector(0, 0, 6), FVector(R * 2.9f, R * 2.9f, 12), 0, PaperDeep, 0, 3);
    Solid(MeshStar, Base + FVector(0, 0, Height + 14), FVector(R * .7f, R * .7f, 14), Time * 30, Marigold, 0, 2.5f);
    Decal(DecalShadow, Foot + FVector(0, 0, .6f), FVector2D(R * 2.7f), 0, FLinearColor(0, 0, 0, .55f));
}

void AGWWorld::AddSpentShot(FVector Position, FVector Velocity, uint8 Weapon, uint8 Team)
{
    // Keeps going the way it flew, slowed by the air, and starts to fall.
    if (SpentShots.Num() >= 160)
        SpentShots.RemoveAt(0, 16, EAllowShrinking::No);
    FSpentShot& Shot = SpentShots.AddDefaulted_GetRef();
    Shot.Position = Position;
    const FVector Flat(Velocity.X, Velocity.Y, 0);
    Shot.Velocity = Flat.GetClampedToMaxSize(1100) * .7f + FVector(0, 0, 150);
    Shot.Weapon = Weapon;
    Shot.Team = Team;
    Shot.Spin = (Hash(SpentShots.Num() * 7 + Weapon) - .5f) * 1400;
}

void AGWWorld::StepSpentShots(float Delta, bool OnMaze)
{
    using namespace GWPalette;
    auto Ground = [&](const FVector& At)
    {
        if (!OnMaze || Geometry.walls.empty())
            return 0.f;
        const int32 Cell = Geometry.cell({At.X, At.Y});
        return FMath::Max(FloorAt(Geometry, At), BlockTop(Geometry, Cell));
    };
    for (int32 I = SpentShots.Num() - 1; I >= 0; --I)
    {
        FSpentShot& Shot = SpentShots[I];
        Shot.Age += Delta;
        const FLinearColor Trail = Shot.Weapon == 9 ? Violet : Shot.Weapon == 6 ? Teal : Shot.Team == 1 ? Marigold : Violet;
        if (Shot.Rest < 0)
        {
            // Through the air: gravity pulls it down, drag slows it, it tumbles.
            Shot.Velocity.Z -= 2100 * Delta;
            Shot.Velocity.X *= FMath::Exp(-1.4f * Delta);
            Shot.Velocity.Y *= FMath::Exp(-1.4f * Delta);
            Shot.Position += Shot.Velocity * Delta;
            const float Floor0 = Ground(Shot.Position) + 5;
            if (Shot.Position.Z <= Floor0)
            {
                Shot.Position.Z = Floor0;
                const FVector Along = FVector(Shot.Velocity.X, Shot.Velocity.Y, 0);
                // The first touch skips it off the page: a puff of dust, chips and a pencil scuff along its way.
                if (!Shot.Bounced && Shot.Velocity.Z < -150)
                {
                    Shot.Bounced = true;
                    Shot.Velocity = Along * .45f + FVector(0, 0, -Shot.Velocity.Z * .3f);
                    if (Fx)
                        Fx->Cue(TEXT("Thud"), Shot.Position - FVector(0, 0, 3), Trail, Shot.Weapon == 3 ? 1.45f : 1.15f);
                    if (Scuffs.Num() >= 120)
                        Scuffs.RemoveAt(0, 20, EAllowShrinking::No);
                    FScuff& Mark = Scuffs.AddDefaulted_GetRef();
                    Mark.At = FVector(Shot.Position.X, Shot.Position.Y, Floor0 - 4);
                    Mark.Yaw = Along.Rotation().Yaw;
                    Mark.Length = FMath::Clamp(Along.Size() * .12f, 22.f, 70.f);
                    Mark.Color = Trail;
                }
                else
                {
                    // The second comes to rest where it lies.
                    Shot.Rest = 0;
                    Shot.Velocity = FVector::ZeroVector;
                }
            }
        }
        else
            Shot.Rest += Delta;
        if (Shot.Rest > 1.6f || Shot.Age > 5)
        {
            SpentShots.RemoveAtSwap(I, EAllowShrinking::No);
            continue;
        }
        // Lying still it shrinks away; in the air it trails a short streak along its fall.
        const float Fading = Shot.Rest < 0 ? 1.f : 1.f - GWEase::Clamp01((Shot.Rest - 1.f) / .6f);
        const FVector Dir = Shot.Velocity.GetSafeNormal();
        const float Yaw = Dir.IsNearlyZero() ? Shot.Spin * .1f : FVector(Dir.X, Dir.Y, 0).Rotation().Yaw;
        if (Shot.Weapon == 9)
            Rotated(MeshDiamond, Shot.Position, FVector(30, 13, 13) * Fading, FRotator(Shot.Age * Shot.Spin, Yaw, 0), Violet, 2.5f);
        else
            Solid(MeshSphere, Shot.Position, FVector((Shot.Weapon == 3 ? 12 : Shot.Weapon == 6 ? 11 : 9) * Fading), 0,
                  Shot.Weapon == 6 ? Teal : InkSoft, 0, Shot.Weapon == 6 ? 1.5f : 0);
        const float Speed = Shot.Velocity.Size();
        if (Shot.Rest < 0 && Speed > 80)
            Decal(DecalStreak, Shot.Position - Dir * 18, FVector2D(FMath::Clamp(Speed * .05f, 14.f, 46.f), 10), Yaw, Trail.CopyWithNewOpacity(.6f));
        Decal(DecalShadow, FVector(Shot.Position.X, Shot.Position.Y, Ground(Shot.Position) + .7f), FVector2D(22 * Fading), 0,
              FLinearColor(0, 0, 0, .3f * Fading));
    }
    // Scuffs stay on the page a little while, fading.
    for (int32 I = Scuffs.Num() - 1; I >= 0; --I)
    {
        FScuff& Mark = Scuffs[I];
        Mark.Age += Delta;
        if (Mark.Age > 3)
        {
            Scuffs.RemoveAtSwap(I, EAllowShrinking::No);
            continue;
        }
        // A pencil drag along the skip, darkest where it struck, and a small dent in the shot's colour.
        const float Fading = 1.f - GWEase::Clamp01((Mark.Age - 1.8f) / 1.2f);
        const FVector Along = FRotator(0, Mark.Yaw, 0).Vector();
        Decal(DecalHatch, Mark.At + Along * Mark.Length * .5f + FVector(0, 0, .9f), FVector2D(Mark.Length, 18), Mark.Yaw,
              Ink.CopyWithNewOpacity(.5f * Fading));
        Decal(DecalStreak, Mark.At + Along * Mark.Length * .45f + FVector(0, 0, 1), FVector2D(Mark.Length * .9f, 9), Mark.Yaw,
              Mark.Color.CopyWithNewOpacity(.55f * Fading));
        Decal(DecalHexRing, Mark.At + FVector(0, 0, 1.1f), FVector2D(26), Mark.Yaw, Mark.Color.CopyWithNewOpacity(.8f * Fading));
    }
}

// A tower's jelly wobble, as (x, y, z) scale about its foot, Seconds after it went up or grew.
FVector AGWWorld::JellyAt(float Seconds, bool Grew)
{
    const float T = FMath::Max(Seconds, 0.f);
    // G: overall size; Q: the stretch (tall and thin above zero, squat and wide below).
    float G = 1, Q = 0;
    if (!Grew)
    {
        // Up from nothing, overshooting about a quarter, dipping back and settling (two wobbles in under a second),
        // stretched tall on the way up and squat as it lands.
        G = 1 - FMath::Exp(-6 * T) * FMath::Cos(15 * T);
        Q = .32f * FMath::Exp(-4.5f * T) * FMath::Sin(15 * T + .3f);
    }
    else if (T < .1f)
        // Grown: first it gathers itself, squashing down...
        Q = -.34f * FMath::Sin(T / .1f * HALF_PI);
    else
    {
        // ...then springs up past its new size and wobbles back.
        const float K = T - .1f;
        Q = -.34f * FMath::Exp(-4 * K) * FMath::Cos(15 * K);
        G = 1 + .14f * FMath::Exp(-4 * K) * FMath::Sin(15 * K);
    }
    G = FMath::Max(G, 0.f);
    return FVector(G * (1 - .5f * Q), G * (1 - .5f * Q), G * (1 + Q));
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
    case 8:
        // The expedition's own hat: a folded newspaper boat riding across the head, its sail up.
        Solid(MeshTrap, Top + FVector(0, 0, R * .2f), FVector(R * 2.f, R * 1.f, R * .4f), Yaw + 90, GWPalette::Hex(0xE9E2D0), 0, 2.5f);
        Rotated(MeshTri, Top + FVector(0, 0, R * .78f), FVector(R * 1.1f, R * 1.3f, R * .2f), FRotator(90, Yaw + 90, 0), GWPalette::Hex(0xE9E2D0), 2);
        break;
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
    FoeTrails.Reset();
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
    if (NewMatch)
    {
        // A new page arrives: its blocks wait flat on the paper until the wipe opens (see IntroAge).
        IntroPending = true;
        IntroCovered = false;
        MatchSeenAt = GWClock::Seconds();
        Landed.Reset();
        UnitsLanded.Reset();
    }
    BuildWalls(S, IntroAge());
    const float Map = Geometry.sizeX();
    Floor->SetWorldLocation(FVector(Map * .5f, Geometry.sizeY() * .5f, -20));
    Floor->SetWorldScale3D(FVector(Map * 3 / 100, Geometry.sizeY() * 3 / 100, .4f));
    if (FloorMaterial)
    {
        FloorMaterial->SetScalarParameterValue(TEXT("MapSize"), Map);
        FloorMaterial->SetScalarParameterValue(TEXT("MapSizeY"), S->GridPage() ? Geometry.sizeY() : 0.f);
        FloorMaterial->SetScalarParameterValue(TEXT("GridMajor"), S->GridPage() ? Geometry.cellSize : 1000.f);
        // The expedition's paper is the stage's pen's (the floor shader's Pen: -1 the plain graph paper).
        FloorMaterial->SetScalarParameterValue(TEXT("Pen"), S->Expedition() ? static_cast<float>(static_cast<int32>(S->Biome())) : -1.f);
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
        GhostShot = 0;
        GhostAt = -100;
    }
}

float AGWWorld::IntroAge()
{
    const FGWTransition& T = FGWTransition::Get();
    if (!T.Enabled() || FGWCinematic::Get().Active)
        return 1000;
    const double Now = GWClock::Seconds();
    if (IntroPending)
    {
        // The intro starts as the wipe opens on this page; a page that arrives with no wipe at all starts at once.
        IntroCovered |= T.Stage == FGWTransition::EStage::Closing || T.Stage == FGWTransition::EStage::Closed;
        if (IntroCovered && T.Stage == FGWTransition::EStage::Opening)
            IntroStart = T.StageStart + .3; // the first tiles have cleared the middle of the page
        else if (!IntroCovered && Now - MatchSeenAt > .15)
            IntroStart = Now;
        else
            return -1;
        IntroPending = false;
    }
    return static_cast<float>(Now - IntroStart);
}

void AGWWorld::BuildWalls(const AGWGameState* S, float Intro)
{
    const bool Hub = S->Rules == static_cast<uint8>(gwgame::RuleSet::Hub);
    const FVector2D Mid(Geometry.sizeX() * .5f, Geometry.sizeY() * .5f);
    const float Cell = FMath::Max(100.f, static_cast<float>(Geometry.cellSize));
    WallsRising = Intro < 3.f;
    // How far a cell's block has risen: a wave out from the middle of the page, each block overshooting a little
    // and settling, flat card outlines until the wave reaches them.
    auto Rise = [&](int32 Id, gwgame::Vec2 Lo, gwgame::Vec2 Hi, float Top, const FLinearColor& Color) -> float
    {
        if (!WallsRising)
            return 1;
        const FVector2D At((Lo.x + Hi.x) * .5, (Lo.y + Hi.y) * .5);
        const float Delay = .05f + static_cast<float>(FVector2D::Distance(At, Mid)) / Cell * .085f + Hash(Id * 13 + 5) * .12f;
        const float K = (Intro - Delay) / .55f;
        if (K >= 1 && !Landed.Contains(Id))
        {
            Landed.Add(Id);
            if (Hash(Id * 7 + 1) < .28f)
                Landings.Add({FVector(At.X, At.Y, Top + 4), Color});
        }
        return Intro < 0 ? 0.f : FMath::Max(0.f, GWEase::OutBack(K, 1.6f));
    };
    FBatch WallBatch, WallHullBatch;
    // The expedition's floor is cut out of rock: rock beside the open floor stands as walls in card of the stage's pen,
    // a shut door as a coral slab across its corridor, and rock away from every room as a low wash of the pen's ink.
    const bool Expedition = S->Rules == static_cast<uint8>(gwgame::RuleSet::Expedition);
    TSet<int32> DoorCells;
    FLinearColor StageWall = GWPalette::Kraft, StageVoid = GWPalette::PaperDeep;
    if (Expedition)
    {
        for (const auto& Room : S->Rooms)
            for (const int16 Door : Room.Doors)
                DoorCells.Add(Door);
        const FLinearColor Tone = GWPalette::Biome(static_cast<int32>(S->Biome()));
        StageWall = FMath::Lerp(GWPalette::Kraft, Tone, .3f);
        StageVoid = FMath::Lerp(GWPalette::PaperDeep, Tone, .4f) * .6f;
    }
    auto OpenCell = [&](int32 X, int32 Y)
    {
        if (X < 0 || Y < 0 || X >= Geometry.width || Y >= Geometry.height)
            return false;
        const int32 N = Y * Geometry.width + X;
        return !Geometry.isFixed(N) && Geometry.walls[N] <= 0;
    };
    // Card-stock blocks: low enough to read the arena over them, hatched on the shaded faces.
    // Level rock stands taller in graphite; the walls the players draw are kraft card.
    Geometry.solids({0, 0}, {Geometry.sizeX(), Geometry.sizeY()},
                    [&](int Id, gwgame::Vec2 Lo, gwgame::Vec2 Hi, int HP)
                    {
                        // A hub station stands in its wall cell as a prop (DrawHub) rather than a block.
                        if (Hub && HubStationCell(Id))
                            return;
                        // An expedition stand or machine is drawn with its shelf (its footprint is all the rules see).
                        if (S->Cells.IsValidIndex(Id) && (S->Cells[Id] & (EGWCell::Rock | EGWCell::Wall)) == (EGWCell::Rock | EGWCell::Wall))
                            return;
                        if (Expedition && Geometry.isFixed(Id))
                        {
                            const FVector2D Middle((Lo.x + Hi.x) * .5, (Lo.y + Hi.y) * .5);
                            if (DoorCells.Contains(Id))
                            {
                                const float Tall = FMath::Max(2.f, 120 * Rise(Id, Lo, Hi, 120, GWPalette::Coral));
                                const FVector Size(Hi.x - Lo.x - 30, Hi.y - Lo.y - 30, Tall);
                                const FVector Center(Middle.X, Middle.Y, Tall * .5f);
                                WallBatch.Add(FTransform(FQuat::Identity, Center, Size / MeshSizes[MeshBlock]), GWPalette::Coral * .92f, 0);
                                WallHullBatch.Add(FTransform(FQuat::Identity, Center, (Size + FVector(10)) / MeshSizes[MeshBlock]), InkLine, 0);
                                return;
                            }
                            const int32 X = Id % Geometry.width, Y = Id / Geometry.width;
                            bool Edge = false;
                            for (int32 DY = -1; DY <= 1 && !Edge; ++DY)
                                for (int32 DX = -1; DX <= 1 && !Edge; ++DX)
                                    Edge |= (DX || DY) && OpenCell(X + DX, Y + DY);
                            if (!Edge)
                            {
                                const FVector Size(Hi.x - Lo.x, Hi.y - Lo.y, 8);
                                WallBatch.Add(FTransform(FQuat::Identity, FVector(Middle.X, Middle.Y, 4), Size / MeshSizes[MeshBlock]),
                                              StageVoid * (.94f + .06f * Hash(Id)), 0);
                                return;
                            }
                            const float Tall = FMath::Max(2.f, 150 * Rise(Id, Lo, Hi, 150, StageWall));
                            const FVector Size(Hi.x - Lo.x - 2, Hi.y - Lo.y - 2, Tall);
                            const FVector Center(Middle.X, Middle.Y, Tall * .5f);
                            WallBatch.Add(FTransform(FQuat::Identity, Center, Size / MeshSizes[MeshBlock]), StageWall * (.9f + .1f * Hash(Id)), 0);
                            WallHullBatch.Add(FTransform(FQuat::Identity, Center, (Size + FVector(10)) / MeshSizes[MeshBlock]), InkLine, 0);
                            return;
                        }
                        // The box's platform: a steel ruler standing above every wall, the box wall its side.
                        if (Geometry.isDeck(Id))
                        {
                            const float Top = DeckTop * FMath::Max(.02f, Rise(Id, Lo, Hi, DeckTop, GWPalette::Ruler));
                            const FVector Size(Hi.x - Lo.x, Hi.y - Lo.y, Top);
                            const FVector Center((Lo.x + Hi.x) * .5, (Lo.y + Hi.y) * .5, Top * .5f);
                            WallBatch.Add(FTransform(FQuat::Identity, Center, Size / MeshSizes[MeshBlock]),
                                          GWPalette::Ruler * (.95f + .05f * Hash(Id)), 0);
                            WallHullBatch.Add(FTransform(FQuat::Identity, Center, (Size + FVector(6)) / MeshSizes[MeshBlock]), InkLine, 0);
                            return;
                        }
                        // A tower's post is drawn with the tower (it is rock only to the rules).
                        if (S->Cells.IsValidIndex(Id) && (S->Cells[Id] & (EGWCell::Rock | EGWCell::Paper)) == (EGWCell::Rock | EGWCell::Paper))
                            return;
                        const bool Rock = Geometry.isFixed(Id);
                        // The marker's thick wall stands taller in dark card, the highlighter's bounce wall glows
                        // fluorescent lime; plain walls are kraft.
                        const gwgame::WallKind Kind = Rock ? gwgame::WallKind::Plain : Geometry.kind(Id);
                        const float Full = Kind == gwgame::WallKind::Thick ? 165 : 140;
                        // The pencil sketchbook's pillars are dashed pencil walls (shots and the eraser break them): pale
                        // graphite card with a soft pencil edge.
                        const bool Sketch = Expedition && !Rock;
                        const FLinearColor Card = Rock                                ? Graphite
                                                  : Sketch                           ? FMath::Lerp(GWPalette::Paper, GWPalette::Pencil, .4f)
                                                  : Kind == gwgame::WallKind::Thick  ? ThickCard
                                                  : Kind == gwgame::WallKind::Bounce ? Highlighter
                                                                                     : GWPalette::Kraft;
                        const float Standing = Rock ? 180 : Sketch ? 130 : HP >= 200 ? Full : 70 + HP * (Full - 70) / 200;
                        const float Tall = FMath::Max(2.f, Standing * Rise(Id, Lo, Hi, Standing, Card));
                        const FVector Size(Hi.x - Lo.x - 2, Hi.y - Lo.y - 2, Tall);
                        const FVector Center((Lo.x + Hi.x) * .5, (Lo.y + Hi.y) * .5, Tall * .5f);
                        const FLinearColor Tint = Card * (.9f + .1f * Hash(Id));
                        WallBatch.Add(FTransform(FQuat::Identity, Center, Size / MeshSizes[MeshBlock]), Tint, 0);
                        if (Sketch)
                        {
                            // Dashes: four short pencil hulls round the block instead of one closed outline.
                            for (int32 K = 0; K < 4; ++K)
                            {
                                const FVector Off(K == 0 ? -Size.X * .3f : K == 1 ? Size.X * .3f : 0.f, K == 2 ? -Size.Y * .3f : K == 3 ? Size.Y * .3f : 0.f, 0);
                                const FVector Dash(K < 2 ? Size.X * .3f : Size.X + 8, K < 2 ? Size.Y + 8 : Size.Y * .3f, Tall + 6);
                                WallHullBatch.Add(FTransform(FQuat::Identity, Center + Off, Dash / MeshSizes[MeshBlock]), GWPalette::Pencil * .6f, 0);
                            }
                            return;
                        }
                        WallHullBatch.Add(FTransform(FQuat::Identity, Center, (Size + FVector(10)) / MeshSizes[MeshBlock]), InkLine, 0);
                    });
    Upload(WallBodies, WallBatch);
    Upload(WallHulls, WallHullBatch);
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
    // The title page (offline, between matches) plays the looping brawl behind the menu; a match takes the
    // diorama back.
    if (!S->InMatch && !S->IsRoom && GetNetMode() != NM_Client)
    {
        TickShowcase(S, Delta);
        return;
    }
    if (ShowActive)
        StopShowcase();
    const bool Show3D = S->InMatch;
    const auto* Prefs = UGWUserSettings::Get();
    const double RealNow = GWClock::Seconds();
    const bool Rebuilt = LastSeed != S->Seed || LastVersion != S->WallVersion || WasInMatch != S->InMatch;
    if (Rebuilt)
        Rebuild(S);
    // While the page assembles, its blocks are rebuilt every frame as they rise.
    const float Intro = IntroAge();
    if (WallsRising && !Rebuilt)
        BuildWalls(S, Intro);
    SelectedCandidate = -1;
    SelectedGrab = false;
    const FGWUnitView* Local = P ? S->FindUnit(P->UnitId) : nullptr;
    FVector Focus = Local ? FVector(Local->Position) : FVector(Geometry.sizeX() * .5f, Geometry.sizeY() * .5f, 0);
    if (Local && Local->Alive)
        C->PredictedPosition(Local->Id, Focus);
    if (Local && !Local->Alive)
    {
        // Spectate the teammate the controller follows (a living player first; the mouse buttons step through them).
        if (const FGWUnitView* Watch = S->FindUnit(C->SpectateId))
            if (!VisualPosition(Watch->Id, Focus))
                Focus = Watch->Position;
    }
    double Closest = GetDefault<UGWRuntimeSettings>()->RecruitDistance;
    // E takes the closest ally or grabbable teammate on the same level (the same rule as the host's
    // Battle::candidate). On the expedition E is for the floor's things first and recruits nobody (Battle::recruit);
    // with nothing in reach it grabs a willing teammate (2026-11-17), so only players are offered there.
    const bool Expedition = S->Expedition();
    if (Local && Local->Alive && Local->FollowerOf < 0 && Local->Bait <= 0 && Local->State != 4 && !Local->Climbing)
        for (const auto& U : S->Units)
        {
            // The expedition's paper crane and sharpener (item bodies, Breed 100+) are nobody's to recruit.
            const bool Ally = !Expedition && U.Kind == 1 && U.FollowerOf < 0 && U.State != 4 && U.Recruitable && !U.Turret && U.Breed < 100;
            if (!U.Alive || U.Team != Local->Team || U.Deck != Local->Deck || U.Climbing || !(Ally || CanGrab(S, *Local, U)))
                continue;
            double Distance = (FVector(U.Position) - FVector(Local->Position)).Size2D();
            if (Distance < Closest &&
                Geometry.clear({Local->Position.X, Local->Position.Y}, {U.Position.X, U.Position.Y}, 25, Local->Deck))
            {
                Closest = Distance;
                SelectedCandidate = U.Id;
                SelectedGrab = U.Human;
            }
        }
    if (!Expedition && Local && Local->Alive && Local->FollowerOf < 0 && Local->Bait <= 0 && Local->State != 4 && !Local->Climbing && SelectedCandidate < 0)
        for (const auto& U : S->Units)
            if (U.Id != Local->Id && U.Alive && U.Human && U.FollowerOf < 0 && U.Team == Local->Team && U.Deck == Local->Deck && !U.Climbing)
            {
                double D = (FVector(U.Position) - FVector(Local->Position)).Size2D();
                if (D < Closest && Geometry.clear({Local->Position.X, Local->Position.Y}, {U.Position.X, U.Position.Y}, 25, Local->Deck))
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
    // Towers wobble from the moment they are seen to go up or grow (a new page starts the record afresh).
    if (JellySeed != S->Seed || Jellies.Num() > 512)
    {
        JellySeed = S->Seed;
        Jellies.Reset();
    }
    // Smoothing runs every frame so menus can still ask for a unit's visual position.
    for (const auto& U : S->Units)
    {
        JellyOn = false;
        if (!U.Alive)
            continue;
        FVector Target = U.Position;
        const bool Predicted = Local && U.Id == Local->Id && C->PredictedPosition(U.Id, Target);
        FVector& Smoothed = SmoothedPositions.FindOrAdd(U.Id, Target);
        Smoothed = Predicted ? Target : FMath::VInterpTo(Smoothed, Target, Delta, 18);
        if (!Show3D)
            continue;
        const bool Tower = U.Turret && S->Defense();
        const float R = U.Radius * (Tower ? TowerScale : 1.f), Height = R * (U.Kind == 2 ? 1.25f : 1.1f);
        const bool Idle = U.Kind == 1 && U.FollowerOf < 0 && U.State != 4 && !U.Turret;
        const bool Launched = U.State == 4;
        // As the page assembles, everyone drops onto it from above (after the blocks have risen) and lands with a
        // compass mark.
        float Drop = 0;
        if (Intro < 3)
        {
            const float K = (Intro - .85f - Hash(U.Id * 3 + 1) * .4f) / .55f;
            Drop = Intro < 0 ? 1400.f : 1400.f * FMath::Square(1 - GWEase::Clamp01(K));
            if (K >= 1 && !UnitsLanded.Contains(U.Id))
            {
                UnitsLanded.Add(U.Id);
                if (U.Kind != 2 || U.Dummy)
                    Arrivals.Add({FVector(Smoothed.X, Smoothed.Y, U.Deck ? DeckTop + 2 : 2.f), UGWFxDirector::UnitColor(U)});
            }
        }
        const float Lift = (Idle ? 4 + 4 * FMath::Sin(Time * 3 + U.Id) : 0) + (Launched ? 18 : 0) + (U.Carried ? 14 : 0) + Drop;
        float Yaw = U.Angle + (Launched ? Time * 900 : 0);
        // Which floor the body stands on: the platform's top, part way up a ladder, a tower's block, or the paper.
        float FloorZ = U.Deck ? DeckTop : 0.f;
        // A climber (2026-11-17): up the rails for the first half of the climb (the ladder meets the platform's lip half
        // way to its head), a bob on every rung, facing the ladder with the hands reaching up the rails in turn; then a
        // little hop over the lip onto the platform (the same path backwards going down).
        float ClimbRung = -1;
        FVector ClimbAlong = FVector::ZeroVector, ClimbAcross = FVector::ZeroVector, ClimbShift = FVector::ZeroVector;
        if (U.Climbing)
        {
            float Best = 1e9f;
            double BestAlong = 0;
            gwgame::Vec2 BestFoot{}, BestHead{};
            for (const auto& Net : S->Ladders)
            {
                const gwgame::Ladder L = LadderOf(Net);
                if (!Geometry.isDeck(L.deck) || L.pit < 0)
                    continue;
                const gwgame::Vec2 Foot = gwgame::ladderFoot(Geometry, L), Head = gwgame::ladderHead(Geometry, L);
                const gwgame::Vec2 At{Smoothed.X, Smoothed.Y};
                const double Along = (Head - Foot).lengthSquared() > 1 ? std::clamp((At - Foot).dot(Head - Foot) / (Head - Foot).lengthSquared(), 0., 1.) : 0;
                const float Off = static_cast<float>(gwgame::distanceToSegment(At, Foot, Head));
                if (Off < Best)
                {
                    Best = Off;
                    BestAlong = Along;
                    BestFoot = Foot;
                    BestHead = Head;
                }
            }
            if (Best < 1e9f)
            {
                const gwgame::Vec2 Dir = (BestHead - BestFoot).normalized();
                Yaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Dir.y), static_cast<float>(Dir.x)));
                ClimbAlong = FVector(Dir.x, Dir.y, 0);
                ClimbAcross = FVector(-Dir.y, Dir.x, 0);
                if (BestAlong <= .5)
                {
                    const float T = static_cast<float>(BestAlong / .5);
                    ClimbRung = T * 7;
                    FloorZ = T * DeckTop + 8 * FMath::Abs(FMath::Sin(ClimbRung * PI));
                    // On the ladder's outer face, not inside it: the rungs pass behind the body.
                    ClimbShift = -ClimbAlong * R;
                }
                else
                {
                    const float T = static_cast<float>((BestAlong - .5) / .5);
                    FloorZ = DeckTop + 50 * 4 * T * (1 - T);
                }
            }
        }
        else if (U.Turret)
            FloorZ = Tower ? TowerPost : BlockTop(Geometry, Geometry.cell({Smoothed.X, Smoothed.Y}));
        else if (Launched)
            FloorZ = FloorAt(Geometry, Smoothed);
        const FVector Base = FVector(Smoothed.X, Smoothed.Y, FloorZ) + ClimbShift;
        // A tower seen going up (after the page has assembled) or growing a side wobbles like jelly about its foot
        // for a second: everything drawn for it below is squashed with it.
        if (Tower)
        {
            FJelly& Wobble = Jellies.FindOrAdd(U.Id);
            if (Wobble.Sides == 0)
            {
                Wobble.Sides = U.Sides;
                Wobble.Start = Intro < 3 ? -100 : RealNow;
                Wobble.Grew = false;
            }
            else if (U.Sides > Wobble.Sides)
            {
                Wobble.Sides = U.Sides;
                Wobble.Start = RealNow;
                Wobble.Grew = true;
            }
            const float Since = static_cast<float>(RealNow - Wobble.Start);
            if (Since < 1)
            {
                JellyOn = true;
                JellyPivot = FVector(Smoothed.X, Smoothed.Y, 0);
                JellyScale = JellyAt(Since, Wobble.Grew);
            }
        }
        FLinearColor Color = UGWFxDirector::UnitColor(U);
        // An expedition's enemies wear the stage's pen (its elites a little less, so they stand out).
        if (S->Expedition() && U.Kind == 2)
            Color = FMath::Lerp(Color, GWPalette::Biome(static_cast<int32>(S->Biome())), U.Elite ? .3f : .55f);
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
        if (U.Breed >= 100)
        {
            // An item's body on the field: the paper crane decoy (wings beating) or the pencil sharpener's turret.
            if (U.Breed - 100 == static_cast<int32>(gwgame::Gear::Crane))
            {
                const float Beat = 25 * FMath::Sin(Time * 9 + U.Id);
                Rotated(MeshTri, Center, FVector(R * 1.6f, R * 1.1f, R * .5f), FRotator(0, Yaw, 0), GWPalette::Coral, InkWidth);
                for (float Sign : {-1.f, 1.f})
                    Rotated(MeshTri, Center + FRotator(0, Yaw + 90 * Sign, 0).Vector() * R * .7f + FVector(0, 0, R * .3f), FVector(R * 1.2f, R * .9f, 6),
                            FRotator(0, Yaw + 90 * Sign, Sign * (30 + Beat)), GWPalette::Paper, 2.5f);
                Decal(DecalRing, Base + FVector(0, 0, 1.5f), FVector2D(gwgame::CraneRadius * 2), Time * 15, GWPalette::Coral.CopyWithNewOpacity(.25f));
            }
            else
            {
                Solid(MeshBlock, Center, FVector(R * 1.8f, R * 1.8f, Height), Yaw, GWPalette::Magenta, Flash, InkWidth);
                Solid(MeshCoin, Base + FVector(0, 0, Height + 8), FVector(R * 1.2f, R * 1.2f, 12), Time * 200, GWPalette::Paper, 0, 2);
                Solid(MeshDiamond, Base + FRotator(0, U.Angle, 0).Vector() * R + FVector(0, 0, Height * .6f), FVector(14, 14, 26), U.Angle, Graphite, 0, 1.5f);
            }
            Decal(DecalShadow, Base + FVector(0, 0, .6f), FVector2D(R * 2.7f), 0, FLinearColor(0, 0, 0, .55f));
            continue;
        }
        const bool Bestiary = S->Expedition() && U.Kind == 2 && U.Breed >= 0 && U.Breed < gwgame::FoeCount;
        if (Bestiary)
            FoeBody(U, Base, R, Lift, Flash, Time);
        else
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
        // A climber's hands on the rails, reaching up in turn (one per rung).
        if (ClimbRung >= 0)
            for (int32 K = 0; K < 2; ++K)
            {
                const float Reach = .5f + .5f * FMath::Sin(ClimbRung * PI + K * PI);
                Solid(MeshSphere, Base + ClimbAcross * (K ? R * 1.15f : -R * 1.15f) + ClimbAlong * R * .8f + FVector(0, 0, Height * .7f + Lift + 26 * Reach),
                      FVector(R * .42f), 0, Color, Flash, 2.f, Outline);
            }
        if (U.Kind == 0 && U.Human)
        {
            // On an expedition the hat is the kit's (brought from the wardrobe or found on the floor), with the other looks.
            const int32 Worn = S->Expedition() ? U.Wear[0] : HatOf.FindRef(U.Id);
            if (Worn > 0)
                Hat(Worn, Base + FVector(0, 0, Height + Lift), R, Yaw, Time);
            if (S->Expedition())
                Looks(U, Base, R, Height, Lift, Yaw, Time);
        }
        if (U.Turret)
        {
            // A pinned turret stands on a paper disc with a marigold pin through its middle; a tower in the box
            // also has a post in its own colour under the disc, down to its block.
            if (Tower)
            {
                // The post from the floor up (round and as wide as what bodies bump into: gwgame::TowerPostRadius), a
                // mortar's stubby barrel, a tesla's coil and spark ball.
                const float Post = static_cast<float>(gwgame::TowerPostRadius) * 2;
                Solid(MeshCoin, Base - FVector(0, 0, TowerPost * .5f), FVector(Post, Post, TowerPost), 0, FMath::Lerp(GWPalette::Paper, Color, .45f), 0, 3);
                const FVector Facing = FRotator(0, U.Angle, 0).Vector();
                if (U.Shape == static_cast<uint8>(gwgame::Shape::Circle))
                    Rotated(MeshBlock, Base + FVector(0, 0, Height + 10) + Facing * R * .3f, FVector(R * 1.3f, R * .55f, R * .55f), FRotator(40, U.Angle, 0),
                            Graphite, 2.5f);
                else if (U.Shape == static_cast<uint8>(gwgame::Shape::Triangle))
                {
                    for (int32 K = 0; K < 3; ++K)
                        Solid(MeshCoin, Base + FVector(0, 0, Height + 14 + K * 16), FVector(R * (1.1f - .15f * K), R * (1.1f - .15f * K), 8), 0, GWPalette::Kraft, 0, 2);
                    Solid(MeshSphere, Base + FVector(0, 0, Height + 70), FVector(R * .45f), 0, GWPalette::Spark, 0, 2);
                }
            }
            Solid(MeshCoin, Base + FVector(0, 0, 6), FVector(R * 2.9f, R * 2.9f, 12), 0, GWPalette::PaperDeep, 0, 3);
            Solid(MeshStar, Base + FVector(0, 0, Height + Lift + 14), FVector(R * .7f, R * .7f, 14), Time * 30, GWPalette::Marigold, 0,
                  2.5f);
        }
        const FVector Aim = FRotator(0, U.Angle, 0).Vector(), Side(-Aim.Y, Aim.X, 0);
        if (U.Kind == 2)
        {
            // Two ink eyes give every monster a facing; the boss also wears a gold star (the bestiary draws its own).
            const float Eye = FMath::Clamp(R * .22f, 6.f, 26.f);
            if (!Bestiary)
                for (float Sign : {-1.f, 1.f})
                    Solid(MeshSphere, Base + Aim * R * .78f + Side * Sign * R * .34f + FVector(0, 0, Height * .7f + Lift),
                          FVector(Eye), 0, GWPalette::Paper, 0, Eye * .35f);
            if (R >= 100 && !Bestiary)
                Solid(MeshStar, Base + FVector(0, 0, Height + Lift + 30), FVector(R * .8f, R * .8f, 28), Time * 40,
                      GWPalette::Marigold, 0, 4);
            if (S->Expedition())
            {
                // Elites stand on a magenta hexagon; the run's items leave their marks: stars round a stunned head, the
                // red stamp's cross, gold dust, the highlighter's ring and a wet patch.
                if (U.Elite && R < 50 && !Bestiary)
                    Decal(DecalHex, Base + FVector(0, 0, 1.3f), FVector2D(R * 3.4f), Time * 20, GWPalette::Magenta.CopyWithNewOpacity(.75f));
                if (U.Marks & EGWMark::Stunned)
                    for (int32 K = 0; K < 3; ++K)
                    {
                        const float A = Time * 4 + K * 2.094f;
                        Solid(MeshStar, Base + FVector(FMath::Cos(A) * R * .7f, FMath::Sin(A) * R * .7f, Height + Lift + 22), FVector(14, 14, 6), Time * 200,
                              GWPalette::Spark, 0, 1.5f);
                    }
                if (U.Marks & EGWMark::Stamped)
                    Decal(DecalPlus, Base + FVector(0, 0, 1.5f), FVector2D(R * 2.6f), 45, GWPalette::Danger.CopyWithNewOpacity(.85f));
                if (U.Marks & EGWMark::Gilded)
                    Solid(MeshDiamond, Base + FVector(0, 0, Height + Lift + 30 + 6 * FMath::Sin(Time * 5)), FVector(14, 14, 22), Time * 120, GWPalette::Marigold,
                          0, 1.5f);
                if (U.Marks & EGWMark::Highlighted)
                    Decal(DecalRing, Base + FVector(0, 0, 1.7f), FVector2D(R * 3.2f), Time * 40, GWPalette::Spark.CopyWithNewOpacity(.9f));
                if (U.Marks & EGWMark::Wet)
                    Decal(DecalDot, Base + FVector(0, 0, 1.2f), FVector2D(R * 2.4f), 0, GWPalette::Frost.CopyWithNewOpacity(.45f));
                // A staple or a tack stuck in it (2026-11-17): red drops running off it and a red spatter underneath.
                if (U.Status & EGWStatus::Bleeding)
                {
                    // Round the body's edge, where a top-down camera still sees them; the spatter peeks out underneath.
                    for (int32 K = 0; K < 4; ++K)
                    {
                        const float T = FMath::Frac(Time * 1.2f + K / 4.f + U.Id * .13f);
                        Solid(MeshSphere, Base + FRotator(0, K * 90 + 45 + U.Id * 37, 0).Vector() * (R * 1.1f + 6) + FVector(0, 0, (Height + Lift) * (1 - T)),
                              FVector(12 + R * .08f), 0, GWPalette::Danger, 0, 1.4f);
                    }
                    Decal(DecalDot, Base + FVector(0, 0, 1.25f), FVector2D(R * 3.2f), U.Id * 47.f, GWPalette::Danger.CopyWithNewOpacity(.45f));
                }
            }
        }
        else if (S->Expedition() && U.Kind == 0 && U.Human)
        {
            // What the floor has left on a player: bleeding, a red pen's circle, the judge's red seal, a cinnabar streak.
            if (U.Act & EGWAfflict::Bleeding)
                for (int32 K = 0; K < 3; ++K)
                {
                    const float T = FMath::Frac(Time * 1.4f + K / 3.f);
                    Solid(MeshSphere, Base + FRotator(0, K * 120 + U.Id * 31, 0).Vector() * R * .7f + FVector(0, 0, Height * (1 - T) + Lift), FVector(9), 0,
                          GWPalette::Danger, 0, 1);
                }
            if (U.Act & EGWAfflict::Circled)
            {
                Decal(DecalRing, Base + FVector(0, 0, 2.f), FVector2D(R * 3.6f), 0, GWPalette::Danger.CopyWithNewOpacity(.85f));
                Decal(DecalPlus, Base + FVector(0, 0, 2.1f), FVector2D(R * 1.6f), 45, GWPalette::Danger.CopyWithNewOpacity(.5f));
            }
            if (U.Act & EGWAfflict::Sealed)
                Solid(MeshBlock, Base + FVector(0, 0, Height + Lift + 40 + 6 * FMath::Sin(Time * 4)), FVector(30, 30, 16), Time * 40, GWPalette::Danger, 0, 2);
            if (U.Act & EGWAfflict::Streak)
                Decal(DecalCompass, Base + FVector(0, 0, 2.2f), FVector2D(R * 3.2f), Time * 160, GWPalette::Danger.CopyWithNewOpacity(.8f));
            // An expedition hand holds what its slot holds (2026-11-17): an item from the belt, bare fists, the long gun
            // in both hands, or a pistol in either hand or both, each in its own quality's colour.
            const bool HandsUp = U.Selected < gwgame::HandSlots;
            const FGWUnitView::FSlot& Hand = U.Hotbar[FMath::Clamp(HandsUp ? 0 : U.Selected, 0, 4)];
            const FVector Grip = Base + FVector(0, 0, Height * .55f + Lift);
            const uint8 GunSlot = static_cast<uint8>(gwgame::SlotKind::Weapon);
            if (!HandsUp && Hand.Kind == static_cast<uint8>(gwgame::SlotKind::Item))
                LootModel(static_cast<uint8>(gwgame::LootKind::Item), Hand.What, Grip + Aim * (R + 18), Time, U.Id);
            else if (U.Weapon == gwgame::FistWeapon || (U.Hotbar[0].Kind != GunSlot && U.Hotbar[1].Kind != GunSlot))
                Solid(MeshSphere, Grip + Aim * (R + 6), FVector(16), 0, GWPalette::Kraft, 0, 2);
            else
            {
                const float Length = U.Weapon == 3 ? 58 : U.Weapon == 4 ? 48 : U.Weapon == 0 ? 30 : 42;
                const float Thick = U.Weapon == 5 ? 15 : U.Weapon == 2 ? 12 : 9;
                for (int32 K = 0; K < gwgame::HandSlots; ++K)
                {
                    const FGWUnitView::FSlot& Gun = U.Hotbar[K];
                    if (Gun.Kind != GunSlot)
                        continue;
                    const int32 Quality = static_cast<int32>(gwgame::pieceQuality(gwgame::LootKind::Weapon, Gun.What, Gun.Variant));
                    // A long gun sits in the middle; pistols in the hand they are in (left, right).
                    const float Offset = gwgame::twoHanded(Gun.What) ? 0.f : U.Dual ? (K ? 12.f : -12.f) : (K ? 8.f : -8.f);
                    Solid(MeshBlock, Grip + Aim * (R + Length * .5f - 8) + Side * Offset, FVector(Length, Thick, Thick), U.Angle,
                          Quality > 0 ? GWPalette::Grade(Quality) : GWPalette::InkSoft, 0, 2);
                }
            }
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
            // The local player stands on a slowly turning hexagon badge (everyone else on a loop of their colour).
            Decal(DecalHexRing, Base + FVector(0, 0, 1.4f), FVector2D(R * 3.9f), Time * 18, GWPalette::Marigold.CopyWithNewOpacity(.85f));
            const float Charge = C->ChargeProgress();
            const float Reach = R + 52 + FMath::Max(0.f, Charge) * 70;
            const FVector2D AimDir = C->MouseAim.IsNearlyZero() ? FVector2D(Aim.X, Aim.Y) : C->MouseAim;
            const float AimYaw = FMath::RadiansToDegrees(FMath::Atan2(AimDir.Y, AimDir.X));
            Decal(DecalDiamond, Base + FVector(AimDir.X, AimDir.Y, 0) * Reach + FVector(0, 0, 2.2f),
                  FVector2D(26, 54 + FMath::Max(0.f, Charge) * 40), AimYaw - 90,
                  FMath::Lerp(GWPalette::Marigold, GWPalette::Danger, FMath::Max(0.f, Charge)).CopyWithNewOpacity(.95f));
            if (Charge >= 0)
                Decal(DecalTriRing, Base + FVector(0, 0, 1.8f), FVector2D(R * (2.8f + Charge * 3.2f)), -Time * 90,
                      FMath::Lerp(GWPalette::Marigold, GWPalette::Danger, Charge).CopyWithNewOpacity(.85f));
            // The rocket's trigger held: a fuse of dots winding round the player as the charge builds, and (2026-11-17)
            // the aiming preview: a dotted arc to where the shell would come down if let go now and a reticle the size
            // of its blast there, both following the charge and the cursor.
            if (const float Rocket = C->RocketProgress(); Rocket >= 0)
            {
                const int32 Dots = 24, Lit = FMath::RoundToInt(Rocket * Dots);
                const FLinearColor Heat = FMath::Lerp(GWPalette::Marigold, GWPalette::Danger, Rocket);
                for (int32 I = 0; I < Dots; ++I)
                {
                    const float A = -PI * .5f + I * 2 * PI / Dots;
                    const FVector At = Base + FVector(FMath::Cos(A), FMath::Sin(A), 0) * (R + 34) + FVector(0, 0, 2.6f);
                    Decal(DecalDot, At, FVector2D(I < Lit ? 15 : 9), 0, (I < Lit ? Heat : GWPalette::InkSoft).CopyWithNewOpacity(I < Lit ? .95f : .35f));
                }
                const gwgame::Vec2 From{Base.X, Base.Y};
                const gwgame::Vec2 Land =
                    gwgame::rocketLanding(From, {AimDir.X, AimDir.Y}, Rocket, Geometry.sizeX() > 0 ? Geometry.sizeX() : 1e6, Geometry.sizeY() > 0 ? Geometry.sizeY() : 1e6);
                const FVector Landing(Land.x, Land.y, FloorAt(Geometry, FVector(Land.x, Land.y, 0)) + 2.6f);
                const double Span = (Land - From).length();
                const FVector Start = Base + FVector(0, 0, Height * .55f + Lift);
                const int32 Steps = 16;
                for (int32 I = 1; I < Steps; ++I)
                {
                    const float T = static_cast<float>(I) / Steps;
                    const FVector Along = FMath::Lerp(Start, Landing, T) + FVector(0, 0, static_cast<float>(gwgame::rocketArc(Span, T)));
                    Solid(MeshSphere, Along, FVector(12 + 5 * Rocket), 0, Heat, 0, 1.2f);
                }
                // A wax-seal shell reaches farther (Expedition.h BurnReach).
                const bool Wax = S->Expedition() && Local->Hotbar[0].Kind == static_cast<uint8>(gwgame::SlotKind::Weapon) &&
                                 gwgame::gunVariant(Local->Hotbar[0].Variant).trick == gwgame::GunTrick::Burn && Local->Hotbar[0].Variant >= 0;
                const float Blast = static_cast<float>(gwgame::RocketBlast * (Wax ? gwgame::BurnReach : 1.));
                Decal(DecalCompass, Landing, FVector2D(Blast * 2), -Time * 80, Heat.CopyWithNewOpacity(.75f));
                Decal(DecalRing, Landing + FVector(0, 0, .2f), FVector2D(Blast * 2 * (.92f + .08f * FMath::Sin(Time * 8))), 0, Heat.CopyWithNewOpacity(.55f));
                Decal(DecalPlus, Landing + FVector(0, 0, .4f), FVector2D(80), 45, GWPalette::Danger.CopyWithNewOpacity(.85f));
            }
        }
        else
        {
            // A friend winding up the rocket: everyone sees it coming.
            if (U.RocketCharge > 0)
                Decal(DecalCompass, Base + FVector(0, 0, 2.f), FVector2D(R * (2.4f + 2.4f * U.RocketCharge)), Time * 120,
                      FMath::Lerp(GWPalette::Marigold, GWPalette::Danger, U.RocketCharge).CopyWithNewOpacity(.7f));
            if (U.Human)
                Decal(DecalLoop, Base + FVector(0, 0, 1.4f), FVector2D(R * 3.4f), Time * 30, GWPalette::Seat(U.Seat).CopyWithNewOpacity(.75f));
            else if (U.Bot)
                Decal(DecalLoop, Base + FVector(0, 0, 1.4f), FVector2D(R * 3.2f), -Time * 30, GWPalette::Seat(U.Seat).CopyWithNewOpacity(.6f));
        }
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
                const FVector A(Base.X, Base.Y, Base.Z + 2), B(LeaderPosition.X, LeaderPosition.Y, Base.Z + 2);
                const int32 Dots = FMath::Clamp(FMath::FloorToInt(FVector::Dist2D(A, B) / 18), 1, 12);
                for (int32 I = 1; I < Dots; ++I)
                    Decal(DecalDot, FMath::Lerp(A, B, I / float(Dots)), FVector2D(9), 0, GWPalette::Teal.CopyWithNewOpacity(.75f));
            }
        }
    }
    JellyOn = false;
    // Ink poured into wounds (R, 2026-11-17; a shared circle since the second playtest that day): the circle a held R
    // fills (MendReach round the mender; the local player sees theirs while R is down even with nobody hurt in it), drops
    // of ink flowing to every hurt player inside (circling and sinking into the mender's own wounds), a green plus rising
    // over each, and a teal loop on everyone the mend slows.
    {
        const FLinearColor InkBlue = GWPalette::Hex(0x2B3A67);
        const float Reach = static_cast<float>(gwgame::MendReach), Lift = 34;
        const bool Mends = S->InMatch && (S->Expedition() || S->Defense());
        for (const auto& U : S->Units)
        {
            const bool Flowing = U.Alive && U.Human && U.Mending == U.Id;
            const bool Mine = Mends && Local && U.Id == Local->Id && U.Alive && C->MendHeld();
            if (!Flowing && !Mine)
                continue;
            FVector From = U.Position;
            VisualPosition(U.Id, From);
            // The circle: a soft teal pool inside a ring of ink dots turning slowly (brighter while ink flows).
            Decal(DecalGlow, From + FVector(0, 0, 1.5f), FVector2D(Reach * 2.1f), 0, GWPalette::Teal.CopyWithNewOpacity(Flowing ? .2f : .12f));
            const FLinearColor Rim = GWPalette::Teal.CopyWithNewOpacity(Flowing ? .9f : .6f);
            for (int32 K = 0; K < 44; ++K)
            {
                const float A = K * 2 * PI / 44 + Time * .35f;
                Decal(DecalDot, From + FVector(FMath::Cos(A) * Reach, FMath::Sin(A) * Reach, 1.8f), FVector2D(K % 4 ? 15.f : 24.f), 0, Rim);
            }
            if (!Flowing)
                continue;
            for (const auto& V : S->Units)
            {
                if (!V.Alive || !V.Human || V.Team != U.Team || V.Deck != U.Deck || V.Ghost || V.Extracted || V.HP >= V.MaxHP - .5f ||
                    FVector::Dist2D(V.Position, U.Position) > Reach + V.Radius)
                    continue;
                FVector To = V.Position;
                VisualPosition(V.Id, To);
                if (V.Id == U.Id)
                    for (int32 K = 0; K < 5; ++K)
                    {
                        const float T = FMath::Frac(Time * .9f + K / 5.f), A = Time * 3 + K * 1.2566f;
                        Solid(MeshSphere, From + FVector(FMath::Cos(A), FMath::Sin(A), 0) * U.Radius * (1.6f - T) + FVector(0, 0, Lift + 40 * (1 - T)),
                              FVector(9 - 4 * T), 0, InkBlue, 0, 1);
                    }
                else
                {
                    const FVector Span = To - From;
                    for (int32 K = 0; K < 6; ++K)
                    {
                        const float T = FMath::Frac(Time * 1.6f + K / 6.f + V.Id * .13f);
                        Solid(MeshSphere, From + Span * T + FVector(0, 0, Lift + 60 * T * (1 - T) * 2), FVector(10 - 3 * T), 0, InkBlue, 0, 1);
                    }
                }
                const float Rise = FMath::Frac(Time * 1.1f + V.Id * .21f);
                Solid(MeshBlock, To + FVector(0, 0, Lift + 30 + 40 * Rise), FVector(6, 20, 6), 0, GWPalette::Leaf, 0, 1.2f);
                Solid(MeshBlock, To + FVector(0, 0, Lift + 30 + 40 * Rise), FVector(20, 6, 6), 0, GWPalette::Leaf, 0, 1.2f);
                Decal(DecalLoop, To + FVector(0, 0, 1.7f), FVector2D(V.Radius * 3.3f), Time * 60, GWPalette::Teal.CopyWithNewOpacity(.6f));
            }
        }
    }
    ProjectilePositions.Reset();
    TSet<int32> LiveShots;
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
        // Every client flies each shot from where it was sent; walls stop it before the host's word arrives (shots
        // pass over the platform's edge, a tower's bolts and a lobbed shell over everything).
        gwgame::Vec2 Pos{Shot.Position.X, Shot.Position.Y};
        const gwgame::Vec2 From{Shot.Origin.X, Shot.Origin.Y};
        double Fraction = 1;
        gwgame::Vec2 Normal;
        const bool Lobbed = Shot.Weapon == gwgame::LobWeapon || Shot.Weapon == gwgame::MortarWeapon || Shot.Weapon == gwgame::FoeLob;
        const bool High = Shot.Weapon == 6 || Lobbed;
        if (!High && Geometry.shotRay(From, Pos, Shot.Weapon >= 7 ? 8 : 4, Shot.Deck, Fraction, Normal))
            Pos = From + (Pos - From) * Fraction;
        // A shot fired from the platform leaves its muzzle up there and stays at that height while it is over the
        // platform, then drops toward the box floor; a tower's bolt leaves from the top of its tower.
        const float Travel = static_cast<float>((Pos - From).length());
        float Z = Shot.Position.Z;
        if (Shot.Deck)
            Z += DeckTop * FMath::Clamp(1.f - (Travel - static_cast<float>(Geometry.deckRun(From, Pos, 4))) / 650.f, 0.f, 1.f);
        else if (Shot.Weapon == 6)
            Z += TowerPost * FMath::Clamp(1.f - Travel / 500.f, 0.f, 1.f);
        if (Lobbed)
        {
            // A lobbed shell: its height follows the arc between the ends of its ground track.
            const double Speed = FVector(Shot.Velocity).Size2D();
            const double Age = Speed > 1 ? (Pos - From).length() / Speed : 0, Flight = Age + FMath::Max(0.f, Shot.Remaining);
            const gwgame::Vec2 Land = From + gwgame::Vec2{Shot.Velocity.X, Shot.Velocity.Y} * Flight;
            const double T = Flight > 1e-4 ? Age / Flight : 1;
            const float Z0 = Geometry.isDeck(Geometry.cell(From)) ? DeckTop + 60 : 60.f, Z1 = FloorAt(Geometry, FVector(Land.x, Land.y, 0)) + 10;
            Z = FMath::Lerp(Z0, Z1, static_cast<float>(T)) + static_cast<float>(gwgame::rocketArc((Land - From).length(), T));
            // Remember the local player's own last shell: its arc stays drawn as a guide for the next one.
            if (Local && Shot.Owner == Local->Id && Shot.Weapon == gwgame::LobWeapon && Shot.Id != GhostShot)
            {
                GhostShot = Shot.Id;
                GhostFrom = FVector(From.x, From.y, Z0);
                GhostTo = FVector(Land.x, Land.y, Z1);
                GhostAt = Time;
            }
        }
        FVector Position(Pos.x, Pos.y, Z);
        ProjectilePositions.Add(Shot.Id, Position);
        // How it looks now: if it runs out of range before the next frame (not stopped by a wall or a hit), it
        // drops to the floor from here. Shells and blades end their own way.
        LiveShots.Add(Shot.Id);
        FShotTrace& Trace = ShotTraces.FindOrAdd(Shot.Id);
        Trace.Position = Position;
        Trace.Velocity = FVector(Shot.Velocity.X, Shot.Velocity.Y, 0);
        Trace.Weapon = Shot.Weapon;
        Trace.Team = Shot.Team;
        Trace.RanOut = !Lobbed && (Shot.Weapon <= 4 || Shot.Weapon == 6 || Shot.Weapon == 9) && Fraction >= 1 && Shot.RanOut(.12f);
        if (!Show3D)
            continue;
        const bool Faded = Shot.Team == 1 && Local && !MySquad.Contains(Shot.Owner);
        const float Alpha = Faded ? .5f : 1.f, Shrink = Faded ? .8f : 1.f;
        const FVector Dir = FVector(Shot.Direction).GetSafeNormal2D();
        const float Yaw = Dir.Rotation().Yaw;
        const FLinearColor Trail = Shot.Team == 1 ? GWPalette::Marigold : GWPalette::Violet;
        if (Lobbed)
        {
            // The shell: a fat fire-red rocket (a mortar's a round graphite one) nosing along its arc, its shadow on the floor below, and a warning ring
            // where it will land so friends can get out of the way.
            const double Speed = FVector(Shot.Velocity).Size2D();
            const double Age = Speed > 1 ? (Pos - From).length() / Speed : 0, Flight = Age + FMath::Max(0.f, Shot.Remaining);
            const gwgame::Vec2 Land = From + gwgame::Vec2{Shot.Velocity.X, Shot.Velocity.Y} * Flight;
            const double Span = (Land - From).length();
            const double T = Flight > 1e-4 ? Age / Flight : 1;
            const float Climb = static_cast<float>((gwgame::rocketArc(Span, FMath::Min(1., T + .02)) - gwgame::rocketArc(Span, FMath::Max(0., T - .02))) /
                                                   FMath::Max(1., Span * .04));
            const float Pitch = FMath::RadiansToDegrees(FMath::Atan(Climb));
            if (Shot.Weapon == gwgame::FoeLob)
                Solid(MeshSphere, Position, FVector(34, 34, 30), Time * 200, GWPalette::Biome(static_cast<int32>(S->Biome())) * .6f, 0, 3);
            else if (Shot.Weapon == gwgame::MortarWeapon)
                Solid(MeshSphere, Position, FVector(26 * Shrink), 0, Graphite, 0, 2.5f);
            else
                Rotated(MeshDiamond, Position, FVector(56, 22, 22) * Shrink, FRotator(Pitch, Yaw, 0), Shot.Tint ? ShotTint(Shot.Tint) : GWPalette::Fire, 3);
            if (Shot.Weapon != gwgame::FoeLob)
                Solid(MeshSphere, Position - FRotator(Pitch, Yaw, 0).Vector() * 30, FVector(16 * Shrink), 0, GWPalette::Spark, 0, 0);
            const FVector Ground(Pos.x, Pos.y, FloorAt(Geometry, FVector(Pos.x, Pos.y, 0)) + .7f);
            Decal(DecalShadow, Ground, FVector2D(60 + 40 * static_cast<float>(1 - FMath::Abs(T - .5) * 2)), 0, FLinearColor(0, 0, 0, .45f));
            const FVector Landing(Land.x, Land.y, FloorAt(Geometry, FVector(Land.x, Land.y, 0)) + 2.5f);
            const float Blast = Shot.Weapon == gwgame::MortarWeapon ? static_cast<float>(gwgame::MortarBlast)
                                : Shot.Weapon == gwgame::FoeLob   ? static_cast<float>(gwgame::FoeLobBlast)
                                                                  : static_cast<float>(gwgame::RocketBlast);
            const FLinearColor Warn = Shot.Weapon == gwgame::MortarWeapon ? GWPalette::Marigold : GWPalette::Danger;
            // Where it comes down: a turning reticle the size of the blast.
            Decal(DecalCompass, Landing, FVector2D(Blast * 2), -Time * 80, Warn.CopyWithNewOpacity(.4f + .35f * static_cast<float>(T)));
            Decal(DecalPlus, Landing + FVector(0, 0, .3f), FVector2D(70), 45, GWPalette::Danger.CopyWithNewOpacity(.6f));
            continue;
        }
        if (FoeShotModel(Shot.Weapon, Position, Dir, Yaw, Time, Shot.Id))
            continue;
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
        else if (Shot.Tint == 1 || Shot.Tint == 2)
        {
            // Spiked rounds (2026-11-17): a real staple (a steel crown across the flight, two legs pointing ahead) and a
            // real thumbtack (a round coloured head, a steel pin pointing ahead), each with a short green streak.
            const FVector Lat(-Dir.Y, Dir.X, 0);
            const FLinearColor Steel = GWPalette::Hex(0xAEB8C6);
            if (Shot.Tint == 1)
            {
                Solid(MeshBlock, Position - Dir * 4 * Shrink, FVector(4, 22, 4) * Shrink, Yaw, Steel, 0, 1.4f);
                for (const float Hand : {-1.f, 1.f})
                    Solid(MeshBlock, Position + Dir * 3 * Shrink + Lat * 9 * Hand * Shrink, FVector(14, 3.5f, 3.5f) * Shrink, Yaw, Steel, 0, 1.2f);
            }
            else
            {
                static const uint32 Heads[4] = {0xE23A33, 0x2F6BD8, 0xF2C230, 0x3FA34D};
                Rotated(MeshCoin, Position - Dir * 4 * Shrink, FVector(17, 17, 6) * Shrink, FRotator(90, Yaw, 0), GWPalette::Hex(Heads[Shot.Id % 4]), 1.4f);
                Solid(MeshBlock, Position + Dir * 8 * Shrink, FVector(14, 3, 3) * Shrink, Yaw, Steel, 0, 1.f);
            }
            Decal(DecalStreak, Position - Dir * 34, FVector2D(60, 9), Yaw, ShotTint(Shot.Tint).CopyWithNewOpacity(.75f * Alpha));
        }
        else
        {
            // A quality gun's rounds are a size up and wear its colour, the trail too.
            const float Size = (Shot.Weapon == 3 ? 12 : 9) * Shrink * (Shot.Tint ? 1.25f : 1.f);
            Solid(MeshSphere, Position, FVector(Size), 0, Shot.Tint ? ShotTint(Shot.Tint) : GWPalette::InkSoft, 0, Shot.Tint ? 1.2f : 0.f);
            const float Length = Shot.Weapon == 3 ? 120 : Shot.Weapon == 4 ? 70 : 55;
            Decal(DecalStreak, Position - Dir * Length * .5f, FVector2D(Length, Size * 1.3f), Yaw,
                  (Shot.Tint ? ShotTint(Shot.Tint) : Trail).CopyWithNewOpacity(.85f * Alpha));
        }
    }
    // A shot gone since last frame that was about to run out of range drops to the floor from where it was.
    for (auto It = ShotTraces.CreateIterator(); It; ++It)
        if (!LiveShots.Contains(It.Key()))
        {
            const FShotTrace& Gone = It.Value();
            if (Show3D && Gone.RanOut)
                AddSpentShot(Gone.Position, Gone.Velocity, Gone.Weapon, Gone.Team);
            It.RemoveCurrent();
        }
    if (Show3D)
        StepSpentShots(Delta, true);
    // The arc of the local player's last rocket, dotted like a thrown stone's (a guide for the next shot).
    if (Show3D && GhostShot > 0 && Time - GhostAt < 9 && Local && Local->Weapon == gwgame::RocketWeapon)
    {
        const float Span = FVector::Dist2D(GhostFrom, GhostTo), Fade = FMath::Clamp((9.f - static_cast<float>(Time - GhostAt)) / 2.f, 0.f, 1.f);
        const int32 Dots = FMath::Clamp(FMath::FloorToInt(Span / 70), 4, 40);
        for (int32 I = 1; I < Dots; ++I)
        {
            const float T = I / float(Dots);
            const FVector Dot = FMath::Lerp(GhostFrom, GhostTo, T) + FVector(0, 0, static_cast<float>(gwgame::rocketArc(Span, T)));
            Decal(DecalDot, Dot, FVector2D(I % 2 ? 13 : 9), 0, GWPalette::InkSoft.CopyWithNewOpacity(.55f * Fade));
        }
        Decal(DecalCompass, GhostTo + FVector(0, 0, 1), FVector2D(120), Time * 20, GWPalette::InkSoft.CopyWithNewOpacity(.45f * Fade));
    }
    // Pouring ink: a dotted stream of ink from the local player to the teammate being filled.
    if (Show3D && Local && Local->Alive && C->PourTarget >= 0)
    {
        FVector From = Local->Position, To;
        VisualPosition(Local->Id, From);
        From.Z = FloorAt(Geometry, From);
        if (const FGWUnitView* Mate = S->FindUnit(C->PourTarget); Mate && VisualPosition(Mate->Id, To))
        {
            To.Z = Mate->Deck ? DeckTop : 0.f;
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
            Decal(DecalHexRing, FVector(At.X, At.Y, (Mate->Deck ? DeckTop : 0.f) + 2), FVector2D(Mate->Radius * 4.2f), Time * 60,
                  GWPalette::Hex(0x2B3A67).CopyWithNewOpacity(.8f));
        }
    if (Show3D && S->Defense() && CoreCell >= 0)
    {
        DrawDefense(S, C, Local, Time);
        DrawBox(S, C, Local, Time);
    }
    if (Show3D && S->Hub() && Geometry.width > 0)
        DrawHub(S, Local, Time);
    if (Show3D && S->Tutorial && CoreCell >= 0 && !S->Finished)
        DrawTutorial(S, Time);
    if (Show3D && S->Expedition() && Geometry.width > 0)
        DrawExpedition(S, Local, Time);
    if (Show3D)
    {
        for (const auto& Crate : S->Crates)
        {
            const FVector Ground(Crate.Position.X, Crate.Position.Y, FloorAt(Geometry, Crate.Position));
            const float Drop = FMath::Min(500.f, Crate.DropTime * 250);
            Solid(MeshBlock, Ground + FVector(0, 0, 26 + Drop), FVector(52), 18, GWPalette::Kraft, 0, 3.5f);
            Solid(MeshDiamond, Ground + FVector(0, 0, 76 + Drop + 5 * FMath::Sin(Time * 4)), FVector(16, 16, 24), Time * 120,
                  GWPalette::Cobalt, 0, 2);
            Decal(DecalHexRing, Ground + FVector(0, 0, 1.5f), FVector2D(140), Time * 30, GWPalette::Cobalt.CopyWithNewOpacity(.75f));
            Decal(DecalShadow, Ground + FVector(0, 0, .6f), FVector2D(70 + Drop * .1f), 0, FLinearColor(0, 0, 0, .55f));
        }
    }
    const FGWCinematic& Cinematic = FGWCinematic::Get();
    auto* Settings = UGWUserSettings::Get();
    float WantPitch = 55, WantDistance = Settings ? Settings->CameraDistance : 1650;
    // The tutorial follows the player (a little closer than a wave) until the drawing step needs the whole
    // page; the hub pulls back far enough to show a third of the room around the player.
    const bool CloseLesson = S->Tutorial && S->TutorialStep < static_cast<uint8>(gwgame::TutorialStep::Draw);
    // Up on the platform the camera looks at the platform's top, not the floor below it.
    if (Local && Local->Deck)
        Focus.Z = DeckTop;
    if (Show3D && S->Hub())
        WantDistance *= 1.7f;
    else if (Show3D && S->Expedition() && !S->Finished)
    {
        // An expedition room: pulled back far enough to frame the room the player is in, leaning toward its middle.
        const int32 Room = S->RoomAt(Focus);
        if (S->Rooms.IsValidIndex(Room))
        {
            const FGWRoomNet& InRoom = S->Rooms[Room];
            const float Cell = static_cast<float>(Geometry.cellSize);
            const float Span = FMath::Max<float>(InRoom.W, InRoom.H * 1.4f) * Cell;
            WantDistance = FMath::Clamp(Span * 1.05f, WantDistance * 1.15f, WantDistance * 2.2f);
            Focus = FMath::Lerp(Focus, FVector((InRoom.X + InRoom.W * .5f) * Cell, (InRoom.Y + InRoom.H * .5f) * Cell, 0), .3f);
        }
        else
            WantDistance *= 1.2f;
    }
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
        {
            // High enough that a player up on the platform sees at least 65% of the box (DefenseRulesTests checks it).
            WantDistance *= static_cast<float>(gwgame::WaveCameraScale);
            // Up on the platform the view leans into the box: the player sits near the screen's edge with the
            // box ahead, not in the middle of the table around it.
            if (Local && Local->Deck && !CloseLesson)
            {
                const FVector Into = FVector(Geometry.sizeX() * .5f, Geometry.sizeY() * .5f, 0) - Focus;
                Focus.X += FMath::Clamp(Into.X, -static_cast<float>(gwgame::WaveLeanX), static_cast<float>(gwgame::WaveLeanX));
                Focus.Y += FMath::Clamp(Into.Y, -static_cast<float>(gwgame::WaveLeanY), static_cast<float>(gwgame::WaveLeanY));
            }
        }
    }
    // The page's arrival: held high over the middle of the page while it is covered, the camera swoops down onto
    // the player as the wipe opens (slower than its everyday follow).
    if (Intro < 0)
    {
        ViewPitch = 80;
        ViewDistance = WantDistance * 1.9f;
        CameraFocus = FVector(Geometry.sizeX() * .5f, Geometry.sizeY() * .5f, 0);
    }
    const bool Swoop = Intro < 1.6f;
    ViewPitch = FMath::FInterpTo(ViewPitch, WantPitch, Delta, Swoop ? 2.6f : 4.f);
    ViewDistance = FMath::FInterpTo(ViewDistance, WantDistance, Delta, Swoop ? 2.6f : 4.f);
    CameraFocus = Cinematic.Active ? Cinematic.Focus : FMath::VInterpTo(CameraFocus, Focus, Delta, Swoop ? 3.f : 7.f);
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
        {
            Fx->Update(*S, *this, CameraFocus, Delta);
            // Blocks landing as the page assembles pop a little, and everyone arriving is marked with a compass.
            for (const auto& Landing : Landings)
                Fx->Cue(TEXT("Impact"), Landing.Key, Landing.Value, .9f);
            for (const auto& Arrival : Arrivals)
                Fx->Cue(TEXT("Spawn"), Arrival.Key, Arrival.Value, 1.3f);
        }
        else if (WasInMatch)
            Fx->Stop();
    }
    Landings.Reset();
    Arrivals.Reset();
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
    const auto* C = GetWorld()->GetFirstPlayerController();
    const auto* H = C ? Cast<AGWHUD>(C->GetHUD()) : nullptr;
    if (H && H->IntroPlaying())
    {
        Music->Stop();
        LastMusicTrack.Empty();
        return;
    }
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
        if (Shot.Id > LastProjectile && AudioTime <= 0 && !gwgame::foeShot(Shot.Weapon) && (FVector(Shot.Position) - Focus).Size2D() < 1200)
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

int32 AGWWorld::LadderState(const AGWGameState* S, const FGWUnitView* Local, int32 Cell) const
{
    if (!S || !Local || !Local->Alive || !Geometry.isDeck(Cell))
        return -1;
    for (const auto& L : S->Ladders)
        if (L.Deck == Cell)
            return 1;
    const int32 Pit = gwgame::ladderPit(Geometry, Cell);
    if (Pit < 0 || !S->Cells.IsValidIndex(Pit) || (S->Cells[Pit] & (EGWCell::Wall | EGWCell::Core | EGWCell::Entrance)) ||
        Local->Ink + 1e-3f < S->LadderInk)
        return -1;
    return 0;
}
bool AGWWorld::RoadStaysOpen(int32 Cell) const
{
    if (CoreCell < 0 || Cell < 0 || Cell >= static_cast<int32>(Geometry.walls.size()))
        return true;
    std::vector<int> Gates;
    for (int32 Gate : Entrances)
        Gates.push_back(Gate);
    return gwgame::routeOpen(Geometry, Gates, CoreCell, Cell);
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
    for (const auto& L : S->Ladders)
        if (L.Pit == Cell)
            return false;
    const float Size = Geometry.cellSize;
    const FVector2D Lo((Cell % Geometry.width) * Size, (Cell / Geometry.width) * Size);
    for (const auto& U : S->Units)
        if (U.Alive && !U.Deck)
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
            // A raid gate is marked up on the platform's top, a tunnel's on the floor.
            const FVector Mark = Flags & EGWCell::Deck ? FVector(Center.x, Center.y, DeckTop + 1.4f) : Ground;
            Decal(DecalHatch, Mark, FVector2D(Size - 30), 0, Coral.CopyWithNewOpacity(.6f));
            Decal(DecalCompass, Mark + FVector(0, 0, .4f), FVector2D(Size * .88f), Time * 40, Coral.CopyWithNewOpacity(.85f));
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
    // The core: a paper plinth, a crystal that drains from teal to red, a hexagon band that shrinks with its health.
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
    Decal(DecalHexRing, Core + FVector(0, 0, 1.7f), FVector2D(Radius * 2.9f * (.55f + .45f * Health)), -Time * 20,
          CoreColor.CopyWithNewOpacity(.85f));
    // The build ring's cell: a marigold square breathing under it and, for the hovered slot, a hexagon where the
    // build would stand with its reach sketched round it (and a mortar's dead zone in red).
    if (C && C->RingCell >= 0 && Building && !S->Finished && S->Cells.IsValidIndex(C->RingCell))
    {
        const gwgame::Vec2 Center = Geometry.center(C->RingCell);
        const FVector Ground(Center.x, Center.y, 3.4f);
        Decal(DecalSquare, Ground, FVector2D(Size * (.92f + .04f * FMath::Sin(Time * 6))), 0, Marigold.CopyWithNewOpacity(.9f));
        Decal(DecalCompass, Ground + FVector(0, 0, .2f), FVector2D(Size * .8f), Time * 30, Ink.CopyWithNewOpacity(.35f));
        if (C->RingPreview >= 0)
        {
            const int32 Kind = C->RingPreview, Level = FMath::Max(0, C->RingPreviewSides - 4);
            Decal(DecalHexRing, Ground + FVector(0, 0, .4f), FVector2D(Size * .66f), Time * 40, Marigold.CopyWithNewOpacity(.95f));
            // The reach as a ring of marigold dots turning slowly round the cell (big and small in turn), and a
            // mortar's dead zone in red.
            auto DotRing = [&](float DotRadius, float DotGap, float DotSize, FLinearColor DotTint, float DotSpin)
            {
                const int32 Count = FMath::Clamp(FMath::RoundToInt(2 * PI * DotRadius / DotGap), 16, 180);
                for (int32 K = 0; K < Count; ++K)
                {
                    const float Turn = K * 2 * PI / Count + Time * DotSpin;
                    Decal(DecalDot, Ground + FVector(FMath::Cos(Turn) * DotRadius, FMath::Sin(Turn) * DotRadius, .3f),
                          FVector2D(K % 2 ? DotSize * .6f : DotSize), 0, DotTint);
                }
            };
            const float Reach = Kind == 0 ? 720.f + 80 * Level : Kind == 1 ? 1500.f + 150 * Level : Kind == 2 ? 430.f + 40 * Level : 0.f;
            if (Reach > 0)
                DotRing(Reach, 64, 26, Marigold.CopyWithNewOpacity(.9f), .12f);
            if (Kind == 1)
                DotRing(gwgame::MortarMinRange, 56, 20, Danger.CopyWithNewOpacity(.75f), -.2f);
        }
    }
    // Drawing cursor (while no ring is open): teal where a wall can go, red where it cannot (the HUD says why), coral
    // to erase; on the platform's edge the cursor is a ladder's. The tutorial shows none before its drawing step.
    else if (C && Local && Local->Alive && S->Building() && C->HoverCell >= 0 && !S->Finished && S->Cells.IsValidIndex(C->HoverCell) &&
             !(S->Cells[C->HoverCell] & EGWCell::Deck))
    {
        const int32 Cell = C->HoverCell;
        const uint8 Flags = S->Cells[Cell];
        const bool Wall = (Flags & EGWCell::Wall) != 0, OnPaper = (Flags & EGWCell::Paper) != 0;
        const bool Erase = C->Painting ? C->PaintErase : Wall && Geometry.integrity(Cell) > .95f;
        // A tower's post (buildable floor gone rock while it stands) takes a click for its ring: marigold, not refused.
        const bool Post = OnPaper && (Flags & EGWCell::Rock) && !C->Painting;
        const bool Allowed = OnPaper && (Erase || Post || CanDraw(S, Local, Cell));
        const FLinearColor Tint = !OnPaper ? Pencil : Erase ? Coral : Post ? Marigold : Allowed ? (Wall ? Marigold : Teal) : Danger;
        const gwgame::Vec2 Center = Geometry.center(Cell);
        const float Z = Wall ? BlockTop(Geometry, Cell) + 3.f : 3.f;
        Decal(DecalSquare, FVector(Center.x, Center.y, Z), FVector2D(Size * .98f), 0, Tint.CopyWithNewOpacity(OnPaper ? .95f : .5f));
        if (OnPaper && !Allowed)
            Decal(DecalPlus, FVector(Center.x, Center.y, Z + .5f), FVector2D(Size * .45f), 45, Danger.CopyWithNewOpacity(.9f));
    }
    // Barbed wire: coils of graphite with barbs, fading as it wears out.
    for (int32 Cell = 0; Cell < S->Wire.Num(); ++Cell)
        if (S->Wire[Cell] > 0)
        {
            const gwgame::Vec2 Center = Geometry.center(Cell);
            const float Life = S->Wire[Cell] / 15.f;
            for (int32 K = 0; K < 3; ++K)
            {
                const FVector At(Center.x + (K - 1) * Size * .28f, Center.y + (K % 2 ? 40.f : -40.f), 2.6f);
                Decal(DecalLoop, At, FVector2D(Size * .42f), K * 60.f, Graphite.CopyWithNewOpacity(.35f + .55f * Life));
                Decal(DecalPlus, At + FVector(0, 0, .2f), FVector2D(Size * .16f), 45 + K * 20.f, Ink.CopyWithNewOpacity(.3f + .5f * Life));
            }
        }
    const FLinearColor InkBlue = Hex(0x2B3A67);
    for (const auto& Drop : S->InkDrops)
    {
        const FVector Ground(Drop.Position.X, Drop.Position.Y, FloorAt(Geometry, Drop.Position));
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
        // Where to stand to use it: a compass rose on the floor in front, bigger, brighter and turning faster for
        // the station in reach.
        const FLinearColor Mark = I == 0 ? Coral : I == 1 ? Marigold : I == 2 ? Teal : I == 3 ? Cobalt : I == 4 ? Plum : I == 5 ? Hex(0x17A3BF) : Leaf;
        Decal(DecalCompass, FVector(Spot.front.x, Spot.front.y, 1.5f), FVector2D((I == Near ? 310 : 240) * Fit), Time * (I == Near ? 45 : 12),
              Mark.CopyWithNewOpacity(I == Near ? .95f : .45f));
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
        const float Z = FloorAt(Geometry, FVector(C.x, C.y, 0));
        Decal(DecalCompass, FVector(C.x, C.y, Z + 2), FVector2D(320 + 40 * Pulse), Time * 40, Marigold.CopyWithNewOpacity(.95f));
        Decal(DecalLoop, FVector(C.x, C.y, Z + 2.2f), FVector2D(220), -Time * 60, Coral.CopyWithNewOpacity(.8f));
        Solid(MeshDiamond, FVector(C.x, C.y, Z + 120 + 18 * Pulse), FVector(40, 40, 70), Time * 90, Marigold, 0, 3);
    }
    if (S->TutorialStep == static_cast<uint8>(gwgame::TutorialStep::Ladder))
    {
        // The platform cell the ladder step suggests (any edge cell will do).
        const gwgame::Vec2 C = Geometry.center(Page.ladder.second * Geometry.width + Page.ladder.first);
        Decal(DecalSquare, FVector(C.x, C.y, DeckTop + 2.4f), FVector2D(Geometry.cellSize * (.86f + .06f * Pulse)), 0, Teal.CopyWithNewOpacity(.55f + .4f * Pulse));
        Solid(MeshDiamond, FVector(C.x, C.y, DeckTop + 120 + 18 * Pulse), FVector(40, 40, 70), Time * 90, Teal, 0, 3);
    }
    if (S->TutorialStep == static_cast<uint8>(gwgame::TutorialStep::Tower))
    {
        // The floor cell the tower step suggests (any open floor will do).
        const gwgame::Vec2 C = Geometry.center(Page.tower.second * Geometry.width + Page.tower.first);
        Decal(DecalSquare, FVector(C.x, C.y, 2.4f), FVector2D(Geometry.cellSize * (.86f + .06f * Pulse)), 0, Marigold.CopyWithNewOpacity(.55f + .4f * Pulse));
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

// The box: the platform reads as a steel ruler round the page (ink ticks along its inner edge, where the drop is),
// the entrances as dark tunnel mouths in the box wall under it, ladders as kraft rails leaning on the wall, and a
// fallen teammate as a dashed outline in their colour waiting on the platform for someone with ink.
void AGWWorld::DrawBox(const AGWGameState* S, const AGWController* C, const FGWUnitView* Local, float Time)
{
    using namespace GWPalette;
    const float Size = Geometry.cellSize;
    const int32 W = Geometry.width, H = Geometry.height;
    static const int32 Steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int32 Cell = 0; Cell < W * H; ++Cell)
    {
        if (!Geometry.isDeck(Cell))
            continue;
        const int32 X = Cell % W, Y = Cell / W;
        const gwgame::Vec2 Center = Geometry.center(Cell);
        for (const auto& Step : Steps)
        {
            const int32 NX = X + Step[0], NY = Y + Step[1];
            if (NX < 0 || NY < 0 || NX >= W || NY >= H || Geometry.isDeck(NY * W + NX))
                continue;
            // Ticks every 50 units along the edge facing the box, longer each 200: a ruler's marks.
            const FVector Out(Step[0], Step[1], 0), Along(-Step[1], Step[0], 0);
            const FVector Edge = FVector(Center.x, Center.y, DeckTop + 1.2f) + Out * (Size * .5f - 6);
            for (int32 K = -3; K <= 4; ++K)
            {
                const bool Major = (K + 4) % 4 == 0;
                const float Length = Major ? 64.f : 32.f;
                const FVector At = Edge + Along * (K * Size / 8 - Size / 16) - Out * (Length * .5f);
                Decal(DecalStreak, At, FVector2D(Length, Major ? 9.f : 6.f), FMath::RadiansToDegrees(FMath::Atan2(Out.Y, Out.X)),
                      Ink.CopyWithNewOpacity(Major ? .75f : .5f));
            }
            Decal(DecalStreak, Edge, FVector2D(Size, 7), FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X)), RulerEdge.CopyWithNewOpacity(.9f));
        }
    }
    // Tunnel mouths: a dark arch in the box wall under the platform, behind each entrance.
    for (int32 Gate : Entrances)
    {
        const int32 X = Gate % W, Y = Gate / W;
        for (const auto& Step : Steps)
        {
            const int32 NX = X + Step[0], NY = Y + Step[1];
            if (NX < 0 || NY < 0 || NX >= W || NY >= H || !Geometry.isDeck(NY * W + NX))
                continue;
            const gwgame::Vec2 A = Geometry.center(Gate), B = Geometry.center(NY * W + NX);
            const FVector Face = FVector((A.x + B.x) * .5f, (A.y + B.y) * .5f, 0), Into(A.x - B.x, A.y - B.y, 0);
            const FVector Dir = Into.GetSafeNormal();
            const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
            Solid(MeshBlock, Face + Dir * 6 + FVector(0, 0, 80), FVector(12, 250, 160), Yaw, Night, 0, 3);
            // The round top of the arch: a disc stood on edge against the wall.
            Rotated(MeshCoin, Face + Dir * 6 + FVector(0, 0, 160), FVector(250, 250, 12), FRotator(90, Yaw, 0), Night, 3);
            Decal(DecalHatch, Face + Dir * 90 + FVector(0, 0, 1.3f), FVector2D(220, 260), Yaw, Night.CopyWithNewOpacity(.35f));
        }
    }
    // Ladders: two kraft rails from the foot on the box floor up to the platform's lip, rungs between.
    auto DrawLadder = [&](const gwgame::Ladder& L, const FLinearColor& Color, float Alpha)
    {
        const gwgame::Vec2 Foot = gwgame::ladderFoot(Geometry, L), Head = gwgame::ladderHead(Geometry, L);
        const gwgame::Vec2 Lip = (Foot + Head) * .5;
        const FVector Bottom(Foot.x, Foot.y, 0), Top(Lip.x, Lip.y, DeckTop + 8);
        const FVector Rise = Top - Bottom, Up = Rise.GetSafeNormal();
        const FVector Side = FVector(-(Head - Foot).y, (Head - Foot).x, 0).GetSafeNormal();
        const FRotator Lean = Up.Rotation();
        for (float Sign : {-1.f, 1.f})
            Rotated(MeshBlock, Bottom + Rise * .5f + Side * Sign * 58, FVector(Rise.Size(), 14, 14), Lean, Color.CopyWithNewOpacity(Alpha), Alpha > .9f ? 2.5f : 0);
        for (int32 K = 1; K <= 6; ++K)
            Rotated(MeshBlock, Bottom + Rise * (K / 7.f) + Up * 0, FVector(10, 116, 9), Lean, Color.CopyWithNewOpacity(Alpha), Alpha > .9f ? 1.5f : 0);
        Decal(DecalShadow, FVector(Foot.x, Foot.y, .6f), FVector2D(150), 0, FLinearColor(0, 0, 0, .35f * Alpha));
    };
    for (const auto& Net : S->Ladders)
        if (Geometry.isDeck(Net.Deck) && Net.Pit >= 0)
            DrawLadder(LadderOf(Net), Kraft, 1);
    // Raid gates (chapter 3): the raiders' own ladder, ink-black with coral rungs, leaning on the outside of the
    // box from the table up to the platform's outer lip.
    for (int32 Cell = 0; Cell < S->Cells.Num(); ++Cell)
    {
        if ((S->Cells[Cell] & (EGWCell::Deck | EGWCell::Entrance)) != (EGWCell::Deck | EGWCell::Entrance))
            continue;
        const int32 X = Cell % W, Y = Cell / W;
        const FVector Out(X == 0 ? -1.f : X == W - 1 ? 1.f : 0.f, X == 0 || X == W - 1 ? 0.f : Y == 0 ? -1.f : 1.f, 0.f);
        const gwgame::Vec2 Middle = Geometry.center(Cell);
        const FVector Lip = FVector(Middle.x, Middle.y, DeckTop + 8) + Out * (Size * .5f);
        const FVector Bottom = FVector(Lip.X, Lip.Y, 0) + Out * 170;
        const FVector Rise = Lip - Bottom;
        const FVector Side(-Out.Y, Out.X, 0);
        const FRotator Lean = Rise.GetSafeNormal().Rotation();
        for (float Sign : {-1.f, 1.f})
            Rotated(MeshBlock, Bottom + Rise * .5f + Side * Sign * 58, FVector(Rise.Size(), 14, 14), Lean, Night, 2.5f);
        for (int32 K = 1; K <= 6; ++K)
            Rotated(MeshBlock, Bottom + Rise * (K / 7.f), FVector(10, 116, 9), Lean, Coral, 1.5f);
        Decal(DecalShadow, Bottom + FVector(0, 0, .6f), FVector2D(150), 0, FLinearColor(0, 0, 0, .35f));
    }
    // The cursor on the platform's edge: a pale ladder where one can hang, a coral ring on one to rub out.
    if (C && Local && Local->Alive && !S->Finished && Geometry.isDeck(C->HoverCell) && S->Phase == static_cast<uint8>(gwgame::DefensePhase::Build))
    {
        const int32 State = LadderState(S, Local, C->HoverCell);
        const gwgame::Vec2 Center = Geometry.center(C->HoverCell);
        const FVector Top(Center.x, Center.y, DeckTop + 2.5f);
        if (State == 0)
        {
            gwgame::Ladder Ghost;
            Ghost.deck = C->HoverCell;
            Ghost.pit = gwgame::ladderPit(Geometry, C->HoverCell);
            DrawLadder(Ghost, Teal, .55f);
            Decal(DecalSquare, Top, FVector2D(Size * .9f), 0, Teal.CopyWithNewOpacity(.8f));
        }
        else if (State == 1)
            Decal(DecalSquare, Top, FVector2D(Size * .9f), 0, Coral.CopyWithNewOpacity(.85f));
        else if (gwgame::ladderPit(Geometry, C->HoverCell) >= 0)
        {
            Decal(DecalSquare, Top, FVector2D(Size * .9f), 0, Danger.CopyWithNewOpacity(.6f));
            Decal(DecalPlus, Top + FVector(0, 0, .5f), FVector2D(Size * .4f), 45, Danger.CopyWithNewOpacity(.85f));
        }
    }
    // Fallen teammates wait up on the platform as dashed outlines in their colour, until someone pays ink or the
    // count runs out.
    for (const auto& U : S->Units)
    {
        if (U.Alive || U.Kind != 0 || !(U.Human || U.Bot) || U.Respawn <= 0)
            continue;
        const FLinearColor Mark = Seat(U.Seat);
        const FVector At(U.Position.X, U.Position.Y, FloorAt(Geometry, U.Position) + 1.6f);
        const float Pulse = .5f + .5f * FMath::Sin(Time * 4 + U.Id);
        const bool InReach = Local && Local->Alive && Local->Deck && Local->Id != U.Id &&
                             FVector::Dist2D(FVector(Local->Position), FVector(U.Position)) < S->ReviveReach;
        Decal(DecalLoop, At, FVector2D(U.Radius * (3.2f + .4f * Pulse)), Time * 50, Mark.CopyWithNewOpacity(.85f));
        Decal(DecalHatch, At + FVector(0, 0, .2f), FVector2D(U.Radius * 2.6f), 0, Mark.CopyWithNewOpacity(.35f));
        Decal(DecalPlus, At + FVector(0, 0, .4f), FVector2D(U.Radius * 1.4f), 0, Mark.CopyWithNewOpacity(.9f));
        if (InReach)
            Decal(DecalRing, At + FVector(0, 0, .6f), FVector2D(S->ReviveReach * 2), -Time * 30, Leaf.CopyWithNewOpacity(.55f + .3f * Pulse));
    }
}

void AGWWorld::LootModel(uint8 Kind, int32 What, FVector At, float Time, int32 Seed, int32 Variant)
{
    const float Spin = Time * 60 + Hash(Seed) * 360;
    switch (static_cast<gwgame::LootKind>(Kind))
    {
    case gwgame::LootKind::Weapon:
    {
        // A gun turning over its spot: barrel and grip in ink (a quality gun's barrel in its quality's colour, a star
        // over it from purple up), its kind told by the barrel's length.
        const int32 Quality = static_cast<int32>(gwgame::pieceQuality(gwgame::LootKind::Weapon, What, Variant));
        const float Length = What == 3 ? 62 : What == 4 ? 52 : What == 0 ? 34 : What == 5 ? 56 : 46;
        const float Thick = What == 5 ? 15 : What == 2 ? 12 : 9;
        const FVector Along = FRotator(0, Spin, 0).Vector();
        Solid(MeshBlock, At, FVector(Length, Thick, Thick), Spin, Quality > 0 ? GWPalette::Grade(Quality) : GWPalette::InkSoft, 0, 2);
        Solid(MeshBlock, At - Along * Length * .3f - FVector(0, 0, Thick), FVector(Thick, Thick * .9f, Thick * 1.6f), Spin, GWPalette::Ink, 0, 1.5f);
        if (Quality >= 3)
            Solid(MeshStar, At + FVector(0, 0, 34 + 4 * FMath::Sin(Time * 4 + Seed)), FVector(16, 16, 6), -Spin * 2, GWPalette::Grade(Quality), 0, 1.5f);
        break;
    }
    case gwgame::LootKind::Item:
        Solid(MeshDiamond, At, FVector(26, 26, 36), Spin, LootTint(What), 0, 2.5f);
        break;
    case gwgame::LootKind::Look:
        Solid(MeshStar, At, FVector(34, 34, 12), Spin, GWPalette::Marigold, 0, 2);
        break;
    default:
        break;
    }
}

void AGWWorld::Looks(const FGWUnitView& U, FVector Base, float R, float Height, float Lift, float Yaw, float Time)
{
    const FVector Front = FRotator(0, Yaw, 0).Vector(), Side(-Front.Y, Front.X, 0);
    // Masks over the face: red and blue paper glasses, an eye patch, scribbled-in shades.
    const FVector Face = Base + FVector(0, 0, Height * .72f + Lift) + Front * R * .95f;
    const FRotator Facing(90, Yaw, 0);
    switch (U.Wear[1])
    {
    case 1:
        Rotated(MeshCoin, Face - Side * R * .32f, FVector(R * .5f, R * .5f, 4), Facing, GWPalette::Danger, 1.5f);
        Rotated(MeshCoin, Face + Side * R * .32f, FVector(R * .5f, R * .5f, 4), Facing, GWPalette::Cobalt, 1.5f);
        break;
    case 2:
        Rotated(MeshCoin, Face + Side * R * .3f, FVector(R * .48f, R * .48f, 4), Facing, GWPalette::Ink, 1.5f);
        break;
    case 3:
        Rotated(MeshCoin, Face - Side * R * .3f, FVector(R * .46f, R * .46f, 4), Facing, GWPalette::Ink, 1.5f);
        Rotated(MeshCoin, Face + Side * R * .3f, FVector(R * .46f, R * .46f, 4), Facing, GWPalette::Ink, 1.5f);
        break;
    default:
        break;
    }
    // Capes on the back: paper-crane wings, a red scarf, notebook-spiral wings.
    const FVector Back = Base + FVector(0, 0, Height * .6f + Lift) - Front * R * .9f;
    const float Flap = 8 * FMath::Sin(Time * 6 + U.Id);
    switch (U.Wear[2])
    {
    case 1:
    case 3:
        for (float Sign : {-1.f, 1.f})
            Rotated(MeshTri, Back + Side * Sign * R * .75f + FVector(0, 0, R * .3f), FVector(R * 1.2f, R * .9f, 5), FRotator(0, Yaw + 180, Sign * (35 + Flap)),
                    U.Wear[2] == 1 ? GWPalette::Paper : GWPalette::PaperShade, 2);
        if (U.Wear[2] == 3)
            for (int32 K = 0; K < 3; ++K)
                Solid(MeshCoin, Back + FVector(0, 0, R * (.1f + .3f * K)), FVector(R * .3f, R * .3f, 4), Yaw, GWPalette::InkSoft, 0, 1);
        break;
    case 2:
        Solid(MeshCoin, Base + FVector(0, 0, Height * .86f + Lift), FVector(R * 2.06f, R * 2.06f, R * .24f), 0, GWPalette::Danger, 0, 1.5f);
        Solid(MeshBlock, Back - Front * R * .2f + FVector(0, 0, R * .1f + Flap * .3f), FVector(R * .9f, R * .32f, 5), Yaw, GWPalette::Danger, 0, 1.5f);
        break;
    default:
        break;
    }
    // A charm hanging at the side: a tiny eraser, a paperclip chain, a compass.
    const FVector Hip = Base + Side * R * 1.05f + FVector(0, 0, Height * .35f + Lift + 3 * FMath::Sin(Time * 5 + U.Id));
    switch (U.Wear[3])
    {
    case 1:
        Solid(MeshBlock, Hip, FVector(16, 10, 10), Yaw, GWPalette::Paper, 0, 1.2f);
        Solid(MeshBlock, Hip + Front * 4, FVector(8, 11, 11), Yaw, GWPalette::Cobalt, 0, 1.2f);
        break;
    case 2:
        for (int32 K = 0; K < 3; ++K)
            Solid(MeshCoin, Hip - FVector(0, 0, K * 9), FVector(10, 10, 3), Yaw + K * 60, GWPalette::Frost, 0, 1);
        break;
    case 3:
        for (float Sign : {-1.f, 1.f})
            Rotated(MeshDiamond, Hip + Front * Sign * 5, FVector(6, 6, 24), FRotator(Sign * 18, Yaw, 0), GWPalette::Ink, 1);
        break;
    default:
        break;
    }
    // A trail underfoot: watercolour prints, a pencil's scratches, bamboo steps.
    const FVector Foot = Base + FVector(0, 0, 1.1f);
    switch (U.Wear[4])
    {
    case 1:
    {
        const FLinearColor Steps[3] = {GWPalette::Coral, GWPalette::Marigold, GWPalette::Teal};
        for (int32 K = 1; K <= 3; ++K)
            Decal(DecalDot, Foot - Front * (R + K * 34) + Side * (K % 2 ? 10.f : -10.f), FVector2D(20), 0, Steps[K - 1].CopyWithNewOpacity(.7f));
        break;
    }
    case 2:
        Decal(DecalStreak, Foot - Front * (R + 55), FVector2D(110, 10), Yaw, GWPalette::Pencil.CopyWithNewOpacity(.6f));
        break;
    case 3:
        for (int32 K = 1; K <= 3; ++K)
            Decal(DecalSquare, Foot - Front * (R + K * 36), FVector2D(14, 26), Yaw, GWPalette::Ink.CopyWithNewOpacity(.5f));
        break;
    default:
        break;
    }
}

void AGWWorld::DrawExpedition(const AGWGameState* S, const FGWUnitView* Local, float Time)
{
    using gwgame::LootKind;
    const FLinearColor Tone = GWPalette::Biome(static_cast<int32>(S->Biome()));
    const float Cell = static_cast<float>(Geometry.cellSize);
    auto RoomMiddle = [&](const FGWRoomNet& R) { return FVector((R.X + R.W * .5f) * Cell, (R.Y + R.H * .5f) * Cell, 0); };
    // Doorways: an open one is a hatched mat of the pen's ink; an armed room's doorways flash before they shut; a shut
    // door (a coral slab, BuildWalls) wears a turning lock.
    for (const FGWRoomNet& R : S->Rooms)
    {
        const bool Armed = R.State == static_cast<uint8>(gwgame::RoomState::Armed);
        for (const int16 Door : R.Doors)
        {
            if (Door < 0 || Door >= static_cast<int32>(Geometry.walls.size()))
                continue;
            const FVector At((Door % Geometry.width + .5f) * Cell, (Door / Geometry.width + .5f) * Cell, 0);
            if (Geometry.isFixed(Door))
            {
                Solid(MeshDiamond, At + FVector(0, 0, 150 + 6 * FMath::Sin(Time * 3 + Door)), FVector(26, 26, 36), Time * 60, GWPalette::Coral, 0, 2.5f);
                continue;
            }
            Decal(DecalHatch, At + FVector(0, 0, 1.1f), FVector2D(Cell * .8f), 0, Tone.CopyWithNewOpacity(.3f));
            if (Armed)
                Decal(DecalCompass, At + FVector(0, 0, 1.6f), FVector2D(Cell * .9f), Time * 120,
                      GWPalette::Danger.CopyWithNewOpacity(.45f + .35f * FMath::Sin(Time * 9)));
        }
    }
    // The portal: a dim ring in its room until the room is cleared, then a turning gate of the pen's colour with a
    // diamond over it (and the first one in waiting starts the clock for the stragglers).
    for (const FGWRoomNet& R : S->Rooms)
        if (R.Kind == static_cast<uint8>(gwgame::RoomKind::Portal) && !S->PortalOpen)
            Decal(DecalRing, RoomMiddle(R) + FVector(0, 0, 1.2f), FVector2D(gwgame::PortalRadius * 2), 0, GWPalette::Pencil.CopyWithNewOpacity(.4f));
    if (S->PortalOpen)
    {
        const FVector At(S->PortalAt.X, S->PortalAt.Y, 0);
        const float Size = gwgame::PortalRadius * 2;
        Decal(DecalGlow, At + FVector(0, 0, 1.f), FVector2D(Size * 1.3f), 0, GWPalette::Cobalt.CopyWithNewOpacity(.35f));
        Decal(DecalCompass, At + FVector(0, 0, 1.4f), FVector2D(Size), Time * 50, GWPalette::Cobalt.CopyWithNewOpacity(.85f));
        Decal(DecalHexRing, At + FVector(0, 0, 1.6f), FVector2D(Size * .7f), -Time * 80, Tone.CopyWithNewOpacity(.8f));
        if (S->PortalWait > 0)
            Decal(DecalRing, At + FVector(0, 0, 1.8f), FVector2D(Size * (1 - S->PortalWait / static_cast<float>(gwgame::PortalWait)) + 20), 0,
                  GWPalette::Marigold.CopyWithNewOpacity(.85f));
        Solid(MeshDiamond, At + FVector(0, 0, 120 + 12 * FMath::Sin(Time * 3)), FVector(40, 40, 70), Time * 90, GWPalette::Cobalt, 0, 3);
    }
    // Exit doors: a paper door frame standing open, its threshold glowing (teal for the last boss's way out, leaf for
    // an early exit).
    for (const auto& Exit : S->Exits)
    {
        const FVector At = LootSpot(Exit.X, Exit.Y);
        const FLinearColor Frame = Exit.Final ? GWPalette::Teal : GWPalette::Leaf;
        for (const float Sign : {-1.f, 1.f})
            Solid(MeshBlock, At + FVector(Sign * 72, 0, 95), FVector(18, 18, 190), 0, Frame, 0, 3);
        Solid(MeshBlock, At + FVector(0, 0, 196), FVector(162, 22, 18), 0, Frame, 0, 3);
        Decal(DecalGlow, At + FVector(0, 0, 1.2f), FVector2D(gwgame::ExitRadius * 2), 0, Frame.CopyWithNewOpacity(.5f));
        Decal(DecalLoop, At + FVector(0, 0, 1.6f), FVector2D(gwgame::ExitRadius * 2), Time * 30, Frame.CopyWithNewOpacity(.8f));
        if (!Exit.Final && Local && Local->Alive && Local->Hold > 0 && FVector::Dist2D(Local->Position, At) < gwgame::ExitRadius)
            Decal(DecalTriRing, At + FVector(0, 0, 1.9f), FVector2D(gwgame::ExitRadius * 2 * Local->Hold + 30), Time * 90, GWPalette::Marigold.CopyWithNewOpacity(.9f));
    }
    // Chests: a kraft box (gold from a boss, graphite and chained in EV05; an elite's a size smaller, a boss's a size up),
    // its lid painted in the chest's quality with a star of that colour turning over it, or thrown back once opened. A
    // purple chest and better glows on the floor; orange and red ones ring it in turning hexagons.
    for (const auto& Chest : S->Chests)
    {
        const FVector At = LootSpot(Chest.X, Chest.Y);
        const FLinearColor Body = Chest.Grade == 1 ? GWPalette::Marigold : Chest.Grade == 2 ? Graphite : GWPalette::Kraft;
        const FLinearColor Lid = GWPalette::Grade(Chest.Quality);
        const float K = Chest.Grade == 1 ? 1.22f : Chest.Grade == 4 ? .85f : 1.f;
        const float Yaw = Hash(Chest.Id) * 30 - 15;
        const FVector Back = FRotator(0, Yaw, 0).RotateVector(FVector(0, -1, 0));
        if (!Chest.Opened && Chest.Quality >= 3)
            Decal(DecalGlow, At + FVector(0, 0, 1.f), FVector2D(220 * K), 0, Lid.CopyWithNewOpacity(.35f + .1f * FMath::Sin(Time * 3 + Chest.Id)));
        if (!Chest.Opened && Chest.Quality >= 4)
            Decal(DecalHexRing, At + FVector(0, 0, 1.3f), FVector2D(190 * K), Time * 35, Lid.CopyWithNewOpacity(.8f));
        Solid(MeshBlock, At + FVector(0, 0, 26 * K), FVector(84, 58, 52) * K, Yaw, Body, 0, 3);
        if (Chest.Opened)
            Rotated(MeshBlock, At + Back * 22 * K + FVector(0, 0, 78 * K), FVector(88, 62, 12) * K, FRotator(0, Yaw, -70), Lid, 2.5f);
        else
        {
            Solid(MeshBlock, At + FVector(0, 0, 58 * K), FVector(90, 64, 14) * K, Yaw, Lid, 0, 3);
            Solid(MeshBlock, At + FVector(0, 0, 40 * K), FVector(14, 64.5f, 48) * K, Yaw, GWPalette::Marigold, 0, 1.5f);
            Solid(MeshStar, At + FVector(0, 0, (104 + 6 * FMath::Sin(Time * 3 + Chest.Id)) * K), FVector(24, 24, 10) * K, Time * 60, Lid, 0, 2);
            if (Chest.Grade == 2)
            {
                Decal(DecalRing, At + FVector(0, 0, 1.4f), FVector2D(420), Time * 20, GWPalette::Danger.CopyWithNewOpacity(.55f));
                Decal(DecalLoop, At + FVector(0, 0, 1.7f), FVector2D(150), -Time * 40, GWPalette::Ink.CopyWithNewOpacity(.6f));
            }
        }
        Decal(DecalShadow, At + FVector(0, 0, .6f), FVector2D(120 * K), 0, FLinearColor(0, 0, 0, .5f));
    }
    // The shelves: each ware turning over a paper pedestal (an empty pedestal once sold); the event machines are paper
    // cabinets with a dial, greyed out once used up.
    for (int32 I = 0; I < S->Offers.Num(); ++I)
    {
        const FGWOfferNet& O = S->Offers[I];
        const FVector At = LootSpot(O.X, O.Y);
        if (O.Kind == static_cast<uint8>(gwgame::ServiceLoot) && O.What == static_cast<uint8>(gwgame::Service::Trade))
        {
            // The buyer (收购商): a paper-bag shopkeeper behind a round kraft counter, a stack of brass coins on it and one
            // spinning over its head, two ink eyes on the room.
            // It looks out toward the camera, turned a little toward the room.
            FVector Facing = FVector(0, 1, 0);
            if (S->Rooms.IsValidIndex(O.Room))
                Facing = ((RoomMiddle(S->Rooms[O.Room]) - At).GetSafeNormal2D() * .35f + FVector(0, 1, 0)).GetSafeNormal2D();
            const float Yaw = Facing.Rotation().Yaw;
            const FVector Side(-Facing.Y, Facing.X, 0);
            const float Bob = 5 * FMath::Sin(Time * 2.4f + I);
            Solid(MeshCoin, At + FVector(0, 0, 32), FVector(gwgame::TraderRadius * 2.3f, gwgame::TraderRadius * 2.3f, 64), 0, GWPalette::Kraft, 0, 3);
            Solid(MeshCoin, At + FVector(0, 0, 66), FVector(gwgame::TraderRadius * 2.5f, gwgame::TraderRadius * 2.5f, 8), 0, GWPalette::PaperDeep, 0, 2.5f);
            for (int32 K = 0; K < 3; ++K)
                Solid(MeshHex, At + Facing * 30 + Side * 26 + FVector(0, 0, 74 + K * 7), FVector(24, 24, 6), K * 20.f, GWPalette::CoinBrass, 0, 1.2f);
            const FVector Body = At - Facing * 18 + FVector(0, 0, 118 + Bob);
            Solid(MeshBlock, Body, FVector(64, 52, 92), Yaw, GWPalette::Hex(0xC9A46E), 0, 3);
            Rotated(MeshBlock, Body + FVector(0, 0, 50), FVector(66, 54, 12), FRotator(0, Yaw, 8), GWPalette::Hex(0xB38A55), 2.5f);
            for (float Sign : {-1.f, 1.f})
                Solid(MeshSphere, Body + Facing * 30 + Side * Sign * 15 + FVector(0, 0, 22), FVector(14), 0, GWPalette::Ink, 0, 0);
            // A little ink grin under the eyes.
            Solid(MeshBlock, Body + Facing * 27 + FVector(0, 0, -2), FVector(4, 18, 4), Yaw, GWPalette::Ink, 0, 0);
            Solid(MeshHex, At - Facing * 18 + FVector(0, 0, 196 + 8 * FMath::Sin(Time * 3 + I)), FVector(34, 34, 8), Time * 120, GWPalette::CoinLight, 0, 2);
            Decal(DecalShadow, At + FVector(0, 0, .6f), FVector2D(150), 0, FLinearColor(0, 0, 0, .5f));
            Decal(DecalCompass, At + FVector(0, 0, 1.2f), FVector2D(gwgame::TradeReach * 2), Time * 15, GWPalette::CoinBrass.CopyWithNewOpacity(.35f));
            continue;
        }
        if (O.Kind == static_cast<uint8>(gwgame::ServiceLoot))
        {
            static const FLinearColor Cabinets[] = {GWPalette::Leaf, GWPalette::Danger, GWPalette::Marigold, GWPalette::Marigold, GWPalette::Cobalt,
                                                    GWPalette::Paper, GWPalette::Kraft,  GWPalette::Teal,     GWPalette::Coral,    GWPalette::Magenta};
            const FLinearColor Body = O.Sold ? GWPalette::PaperDeep : Cabinets[FMath::Clamp<int32>(O.What, 0, static_cast<int32>(UE_ARRAY_COUNT(Cabinets)) - 1)];
            Solid(MeshBlock, At + FVector(0, 0, 60), FVector(96, 70, 120), 0, Body, 0, 3.5f);
            Solid(MeshCoin, At + FVector(0, 0, 128), FVector(56, 56, 14), O.Sold ? 0.f : Time * 40, GWPalette::Paper, 0, 2.5f);
            Solid(MeshDiamond, At + FVector(0, 0, 152), FVector(14, 14, 24), O.Sold ? 0.f : Time * 90, GWPalette::Marigold, 0, 1.5f);
            Decal(DecalShadow, At + FVector(0, 0, .6f), FVector2D(130), 0, FLinearColor(0, 0, 0, .5f));
            continue;
        }
        Solid(MeshCoin, At + FVector(0, 0, 18), FVector(70, 70, 36), 0, GWPalette::PaperDeep, 0, 3);
        if (!O.Sold)
            LootModel(O.Kind, O.What, At + FVector(0, 0, 72 + 6 * FMath::Sin(Time * 2 + I)), Time, 7000 + I,
                      O.Kind == static_cast<uint8>(LootKind::Weapon) ? O.Extra : -1);
    }
    // Loot on the floor: heaps of stickers, ink blots, guns, items and looks (a look ringed in its owner's colour).
    // A chest's toss (2026-11-17) rides up its arc from when this machine first saw it, spinning, with a dotted trail
    // behind it and its shadow shrinking under it.
    static const FLinearColor StickerColors[4] = {GWPalette::Coral, GWPalette::Marigold, GWPalette::Teal, GWPalette::Cobalt};
    TSet<int32> Tossed;
    for (const FGWLootView& L : S->Loot)
    {
        const FVector Ground(L.Position.X, L.Position.Y, 0);
        float Up = 0, Spin = 0;
        if (L.Hop > 0 && L.Flight > 0)
        {
            Tossed.Add(L.Id);
            const float Born = TossBorn.Contains(L.Id) ? TossBorn[L.Id] : TossBorn.Add(L.Id, Time);
            const float T = FMath::Clamp((Time - Born) / L.Flight, 0.f, 1.f);
            Up = 4 * L.Hop * T * (1 - T);
            Spin = T * 540;
            TArray<FVector>& Trail = TossTrail.FindOrAdd(L.Id);
            Trail.Add(Ground + FVector(0, 0, 30 + Up));
            if (Trail.Num() > 9)
                Trail.RemoveAt(0);
            const int32 Quality = L.Kind == static_cast<uint8>(LootKind::Weapon) || L.Kind == static_cast<uint8>(LootKind::Item)
                                      ? static_cast<int32>(gwgame::pieceQuality(static_cast<LootKind>(L.Kind), L.What, L.Variant))
                                      : 0;
            const FLinearColor Streak = Quality > 0                                      ? GWPalette::Grade(Quality)
                                        : L.Kind == static_cast<uint8>(LootKind::Coins)    ? GWPalette::CoinBrass
                                        : L.Kind == static_cast<uint8>(LootKind::Stickers) ? GWPalette::Marigold
                                        : L.Kind == static_cast<uint8>(LootKind::Heart)    ? GWPalette::Danger
                                                                                           : GWPalette::InkSoft;
            for (int32 K = 0; K + 1 < Trail.Num(); ++K)
                Solid(MeshSphere, Trail[K], FVector(5 + 9 * static_cast<float>(K) / Trail.Num()), 0, Streak, 0, 1.f);
            Decal(DecalShadow, Ground + FVector(0, 0, .6f), FVector2D(FMath::Max(24.f, 64 - Up * .25f)), 0, FLinearColor(0, 0, 0, .3f));
        }
        const FVector At = Ground + FVector(0, 0, Up);
        if (L.Kind == static_cast<uint8>(LootKind::Stickers))
        {
            const int32 Count = FMath::Clamp(1 + L.Amount / 8, 1, 4);
            for (int32 K = 0; K < Count; ++K)
                Solid(MeshCoin, At + FVector((Hash(L.Id * 5 + K) - .5f) * 36, (Hash(L.Id * 7 + K) - .5f) * 36, 4 + K * 5), FVector(26, 26, 5),
                      Hash(L.Id + K) * 360 + Spin, StickerColors[(L.Id + K) % 4], 0, 1.5f);
            if (Up <= 0)
                Decal(DecalGlow, Ground + FVector(0, 0, 1.f), FVector2D(70), 0, GWPalette::Marigold.CopyWithNewOpacity(.35f));
            continue;
        }
        if (L.Kind == static_cast<uint8>(LootKind::Coins))
        {
            // Expedition coins: a little stack of brass hexagon tokens (taller for a bigger pile).
            const int32 Count = FMath::Clamp(1 + L.Amount / 25, 1, 5);
            for (int32 K = 0; K < Count; ++K)
                Solid(MeshHex, At + FVector((Hash(L.Id * 3 + K) - .5f) * 16, (Hash(L.Id * 11 + K) - .5f) * 16, 4 + K * 6), FVector(24, 24, 5),
                      Hash(L.Id + K) * 60 + Spin, K % 2 ? GWPalette::CoinLight : GWPalette::CoinBrass, 0, 1.2f);
            if (Up <= 0)
                Decal(DecalGlow, Ground + FVector(0, 0, 1.f), FVector2D(64), 0, GWPalette::CoinBrass.CopyWithNewOpacity(.35f));
            continue;
        }
        if (L.Kind == static_cast<uint8>(LootKind::Heart))
        {
            // A heart (2026-11-17), lying flat so the camera above reads its outline: two red discs on a block turned on
            // its corner (lobes away from the camera, the point toward it), beating and swaying over a soft red glow.
            const float Beat = 1 + .12f * FMath::Max(0.f, FMath::Sin(Time * 6 + L.Id));
            const FVector Mid = At + FVector(0, 0, 26 + 5 * FMath::Sin(Time * 2.5f + L.Id));
            const float Sway = Spin + 14 * FMath::Sin(Time * 1.5f + L.Id);
            const FRotator Turn(0, Sway, 0);
            const FLinearColor Red = GWPalette::Danger;
            const float Side = 38, Half = Side * .7071f;
            Solid(MeshBlock, Mid + Turn.RotateVector(FVector(0, 3, 0)) * Beat, FVector(Side, Side, 12) * Beat, Sway + 45, Red, 0, 2.f);
            for (const float Lobe : {-1.f, 1.f})
                Solid(MeshCoin, Mid + Turn.RotateVector(FVector(Lobe * Half * .5f, 3 - Half * .5f, 0)) * Beat, FVector(Side, Side, 12) * Beat, 0, Red, 0, 2.f);
            Solid(MeshCoin, Mid + Turn.RotateVector(FVector(-Half * .55f, 3 - Half * .62f, 7)) * Beat, FVector(9, 9, 2) * Beat, 0, GWPalette::Paper, 0, 0);
            if (Up <= 0)
                Decal(DecalGlow, Ground + FVector(0, 0, 1.f), FVector2D(96 * Beat), 0, Red.CopyWithNewOpacity(.35f));
            continue;
        }
        if (L.Kind == static_cast<uint8>(LootKind::Ink))
        {
            if (Up <= 0)
                Decal(DecalDot, Ground + FVector(0, 0, 1.1f), FVector2D(40 + FMath::Min(L.Amount, 40)), Hash(L.Id) * 360,
                      GWPalette::Hex(0x2B3A67).CopyWithNewOpacity(.85f));
            Solid(MeshSphere, At + FVector(0, 0, 12 + 3 * FMath::Sin(Time * 4 + L.Id)), FVector(15), 0, GWPalette::Hex(0x2B3A67), 0, 1.5f);
            continue;
        }
        // A ring in the piece's quality (an armoury gun coral, a look its owner's colour); a quality piece glows too.
        const int32 Quality = L.Kind == static_cast<uint8>(LootKind::Look) ? 0 : static_cast<int32>(gwgame::pieceQuality(static_cast<LootKind>(L.Kind), L.What, L.Variant));
        const FLinearColor Ring = Quality > 0                                    ? GWPalette::Grade(Quality)
                                  : L.Kind == static_cast<uint8>(LootKind::Weapon) ? GWPalette::Coral
                                  : L.Kind == static_cast<uint8>(LootKind::Item) ? GWPalette::Teal
                                  : L.Owner >= 0                                 ? GWPalette::Seat(L.Owner)
                                                                                 : GWPalette::Marigold;
        if (Up <= 0)
        {
            if (Quality > 0)
                Decal(DecalGlow, Ground + FVector(0, 0, .9f), FVector2D(70 + 14 * Quality), 0, Ring.CopyWithNewOpacity(.3f));
            Decal(DecalLoop, Ground + FVector(0, 0, 1.2f), FVector2D(92), Time * 40, Ring.CopyWithNewOpacity(.75f));
            Decal(DecalShadow, Ground + FVector(0, 0, .6f), FVector2D(60), 0, FLinearColor(0, 0, 0, .45f));
        }
        LootModel(L.Kind, L.What, At + FVector(0, 0, 30 + 5 * FMath::Sin(Time * 3 + L.Id)), Time, L.Id, L.Variant);
    }
    // Forget the tosses that have landed (or gone).
    for (auto It = TossBorn.CreateIterator(); It; ++It)
        if (!Tossed.Contains(It.Key()))
        {
            TossTrail.Remove(It.Key());
            It.RemoveCurrent();
        }
    // The items' zones: correction fluid's white patch, double-sided tape's strip.
    for (const auto& Zone : S->Zones)
    {
        const FVector From = LootSpot(Zone.FromX, Zone.FromY), To = LootSpot(Zone.ToX, Zone.ToY);
        if (Zone.Kind == 0)
        {
            Decal(DecalGlow, From + FVector(0, 0, 1.f), FVector2D(Zone.Radius * 2), 0, GWPalette::Paper.CopyWithNewOpacity(.85f));
            Decal(DecalRing, From + FVector(0, 0, 1.3f), FVector2D(Zone.Radius * 2), Time * 10, GWPalette::Leaf.CopyWithNewOpacity(.6f));
        }
        else if (Zone.Kind == 1)
        {
            const FVector Along = To - From;
            const float Length = FMath::Max(40.f, static_cast<float>(Along.Size2D()));
            Decal(DecalHatch, (From + To) * .5f + FVector(0, 0, 1.2f), FVector2D(Length, 46), Along.Rotation().Yaw, GWPalette::Mint.CopyWithNewOpacity(.75f));
        }
    }
    DrawHazards(S, Time);
    // The fallen: a paper outline floating where they fell, ringed in their colour, until someone draws them back.
    for (const auto& U : S->Units)
    {
        if (!U.Human || U.Alive || !U.Ghost || U.Extracted)
            continue;
        const FVector At(U.Position.X, U.Position.Y, 0);
        const FLinearColor Tint = GWPalette::Seat(U.Seat);
        const float Bob = 6 * FMath::Sin(Time * 2 + U.Id);
        Decal(DecalLoop, At + FVector(0, 0, 1.4f), FVector2D(U.Radius * 4.2f), Time * 25, Tint.CopyWithNewOpacity(.75f));
        Decal(DecalHexRing, At + FVector(0, 0, 1.2f), FVector2D(U.Radius * 3), -Time * 15, GWPalette::Pencil.CopyWithNewOpacity(.5f));
        Solid(MeshCoin, At + FVector(0, 0, 44 + Bob), FVector(U.Radius * 1.7f, U.Radius * 1.7f, 6), 0, GWPalette::PaperShade, 0, 3.5f);
        Solid(MeshSphere, At + FVector(0, 0, 66 + Bob), FVector(12), 0, Tint, 0, 1.5f);
    }
}
