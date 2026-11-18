#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GWCinematic.h"
#include "GWController.h"
#include "GWFx.h"
#include "GWGameInstance.h"
#include "GWGameMode.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWMotion.h"
#include "GWPlayerState.h"
#include "GWProfile.h"
#include "GWSettings.h"
#include "GWText.h"
#include "GWWorld.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Scalability.h"
#include "UnrealClient.h"

// Plates for the 2026-11 development-update video: scripted moments of real line-defense pages, an eight-seat room,
// the hub's station sheets and the title page's brawl, one PNG per frame in Saved/Promo/Plates/<shot>. Launch with
// -UseFixedTimeStep -FPS=30 so every frame moves the game on by exactly 1/30 s however long its capture takes; the
// sketchbook's wall-clock timers (page wipe, page assembly, hit flashes, the towers' jelly) follow the same frame
// clock through GWClock. Groups: GW.Promo.Capture.<Defense|Coop|Hub|Brawl>; -PromoStride=N saves only every Nth
// frame (a quick look at the staging). The teammates are pretend players, a player state each (name, seat colour,
// hat) driven by the capture like the autopilot; the real profiles wait as *.sav.playtest meanwhile, so no reward
// ever reaches them.
namespace
{
using namespace gwgame;

constexpr float PromoStep = 1.f / 30;

struct FPromoContext
{
    UWorld* World = nullptr;
    AGWController* Controller = nullptr;
    AGWGameMode* GameMode = nullptr;
    AGWGameState* State = nullptr;
    AGWPlayerState* Player = nullptr;
    AGWHUD* Hud = nullptr;
    UGWGameInstance* Instance = nullptr;
    Battle* Sim() const
    {
        return State ? State->Simulation.Get() : nullptr;
    }
    Unit* Me() const
    {
        return Sim() ? Sim()->find(Player->UnitId) : nullptr;
    }
};

// Everyone the capture drives: where they hang about, how far they sway from it and whether they shoot.
struct FPromoActor
{
    TWeakObjectPtr<AGWPlayerState> State;
    Vec2 Anchor{};
    double Sway = 70, Phase = 0;
    bool Fire = true, Drive = true;
};

// Shared by every shot of a group, so a shot picks up the page where the previous one left it.
struct FPromoState
{
    FVector Focus = FVector::ZeroVector;
    float Distance = 3000, Pitch = 56, Yaw = -90;
    TArray<FPromoActor> Crew;
    int32 Mate = -1, Boss = -1, Landing = -1;
    bool Started = false;
    Vec2 Spot{}, Aim{0, 1};
    uint64 Random = 0x51A7E5EEDull;
    double Clock = 0;
};

struct FPromoShot
{
    FString Name;
    int32 Frames = 150, Warmup = 10;
    // Clean: field and combat feedback only (Story keeps tags and banners); Promo: the HUD without its furniture.
    bool Clean = true, Story = false, Promo = false, Camera = true, Wipes = false, QuietClears = false;
    TFunction<bool(FPromoContext&, FPromoState&)> Setup;
    // Frame counts up from -Warmup; capture starts at 0.
    TFunction<void(FPromoContext&, FPromoState&, int32)> Tick;
};

float PromoRamp(int32 Frame, int32 From, int32 To)
{
    const float T = FMath::Clamp(static_cast<float>(Frame - From) / FMath::Max(1, To - From), 0.f, 1.f);
    return T * T * (3 - 2 * T);
}
float PromoRand(uint64& State)
{
    State ^= State << 13;
    State ^= State >> 7;
    State ^= State << 17;
    return static_cast<float>(static_cast<double>(State >> 40) / 16777216.0);
}
FVector PromoFlat(Vec2 P, float Z = 0)
{
    return FVector(P.x, P.y, Z);
}
int32 PromoCell(const Battle& B, int32 X, int32 Y)
{
    return Y * B.maze.width + X;
}
Vec2 PromoAt(const Battle& B, int32 X, int32 Y)
{
    return B.maze.center(PromoCell(B, X, Y));
}
FVector PromoSeen(const FPromoContext& X, int32 Id)
{
    FVector At;
    if (X.Controller->WorldView && X.Controller->WorldView->VisualPosition(Id, At))
        return FVector(At.X, At.Y, 0);
    const Unit* U = X.Sim() ? X.Sim()->find(Id) : nullptr;
    return U ? PromoFlat(U->position) : FVector::ZeroVector;
}
void PromoFollow(FPromoState& St, const FVector& Target, float Rate)
{
    St.Focus = FMath::VInterpTo(St.Focus, Target, PromoStep, Rate);
}
const Unit* PromoNearestFoe(const Battle& B, Vec2 From, double Range, bool Dummies)
{
    const Unit* Best = nullptr;
    double Closest = Range * Range;
    for (const auto& U : B.units)
        if (U.alive && U.kind == UnitKind::Monster && (Dummies || !U.dummy))
        {
            const double D = (U.position - From).lengthSquared();
            if (D < Closest)
            {
                Closest = D;
                Best = &U;
            }
        }
    return Best;
}
// The middle of the thickest knot of enemies (how many stand within a blast of each).
Vec2 PromoCrowd(const Battle& B, Vec2 Fallback, int32* Count = nullptr)
{
    Vec2 Best = Fallback;
    int32 Most = 0;
    for (const auto& A : B.units)
    {
        if (!Battle::enemy(A))
            continue;
        int32 Near = 0;
        Vec2 Sum{};
        for (const auto& C : B.units)
            if (Battle::enemy(C) && (C.position - A.position).lengthSquared() < 320. * 320.)
            {
                ++Near;
                Sum += C.position;
            }
        if (Near > Most)
        {
            Most = Near;
            Best = Sum * (1. / Near);
        }
    }
    if (Count)
        *Count = Most;
    return Best;
}
void PromoParkProfiles(bool Park)
{
    for (int32 Slot = 0; Slot < UGWProfileSave::SlotCount; ++Slot)
    {
        const FString Path = UGWProfileSave::SlotPath(Slot), Backup = Path + TEXT(".playtest");
        if (Park)
        {
            if (IFileManager::Get().FileExists(*Backup))
                UGWProfileSave::Delete(Slot); // left over from an interrupted run
            else if (IFileManager::Get().FileExists(*Path))
                IFileManager::Get().Move(*Backup, *Path, true);
        }
        else
        {
            UGWProfileSave::Delete(Slot);
            if (IFileManager::Get().FileExists(*Backup))
                IFileManager::Get().Move(*Path, *Backup, true);
        }
    }
}
// Seven pretend friends with the seat colours 1-7, a hat each and a gun each; the host is seat 0 (coral).
struct FPromoFriend
{
    const TCHAR* Nick;
    uint8 Hat;
    int32 Weapon;
};
const FPromoFriend PromoFriends[7] = {{TEXT("三角饭团"), 1, 2}, {TEXT("圆规大师"), 4, 3}, {TEXT("方块君"), 3, 1}, {TEXT("六边形战士"), 7, 4},
                                      {TEXT("五角星星"), 2, 0},   {TEXT("橡皮擦"), 5, 1},   {TEXT("直角先生"), 6, 2}};

void PromoInvite(FPromoContext& X, FPromoState& St, int32 Count)
{
    for (int32 I = 0; I < Count && I < 7; ++I)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AGWPlayerState* Friend = X.World->SpawnActor<AGWPlayerState>(AGWPlayerState::StaticClass(), FTransform::Identity, Params);
        if (!Friend)
            continue;
        Friend->SetPlayerName(PromoFriends[I].Nick);
        Friend->Identity = 9001 + I;
        Friend->Team = 1;
        Friend->Hat = PromoFriends[I].Hat;
        Friend->Throwable = true;
        // A few medals each for the achievement wall.
        uint64 Bits = 0;
        for (int32 K = 0; K < 64; ++K)
            if (PromoRand(St.Random) < .22f + .05f * I)
                Bits |= 1ull << K;
        Friend->Achievements = Bits;
        FPromoActor Actor;
        Actor.State = Friend;
        Actor.Phase = 1.3 * (I + 1);
        St.Crew.Add(Actor);
    }
}
int32 PromoUnitOf(const FPromoActor& A)
{
    return A.State.IsValid() ? A.State->UnitId : -1;
}
void PromoLoadout(FPromoContext& X, FPromoState& St)
{
    Battle* B = X.Sim();
    if (!B)
        return;
    for (int32 I = 0; I < St.Crew.Num(); ++I)
        if (Unit* U = B->find(PromoUnitOf(St.Crew[I])))
        {
            if (PromoFriends[I].Weapon > 0)
                B->pickupWeapon(U->id, PromoFriends[I].Weapon);
            U->ammo = 9999;
            U->throwable = true;
        }
    if (Unit* Me = X.Me())
        Me->ammo = 9999;
}
// Nobody the capture drives falls in a shot (their cards keep their real maximums): health is topped up every frame.
void PromoHeal(FPromoContext& X, FPromoState& St)
{
    Battle* B = X.Sim();
    if (!B)
        return;
    auto Heal = [B](int32 Id)
    {
        if (Unit* U = B->find(Id); U && U->alive)
        {
            U->hp = U->maxHp;
            U->ammo = FMath::Max(U->ammo, 50);
        }
    };
    Heal(X.Player->UnitId);
    for (const FPromoActor& A : St.Crew)
        Heal(PromoUnitOf(A));
}
// A defense page on a fixed seed (a recapture meets the same waves), everyone in the room joined as on a real deploy.
bool PromoStartPage(FPromoContext& X, FPromoState& St, int32 Level, uint32 Seed)
{
    AGWGameState* S = X.State;
    if (S->InMatch && !S->Finished && !S->Hub())
        S->Finished = true;
    if (!X.GameMode->BeginDefense(X.Controller, Level, 0))
        return false;
    auto Sim = MakeUnique<Battle>(GetDefault<UGWRuntimeSettings>()->Rules());
    Sim->config = S->Simulation->config;
    if (!Sim->startDefense(Seed, Level))
        return false;
    S->Simulation = MoveTemp(Sim);
    S->Seed = Seed | 1u;
    S->WallVersion = MAX_uint32;
    for (const auto& Item : S->PlayerArray)
        if (auto* Person = Cast<AGWPlayerState>(Item))
        {
            Person->UnitId = S->Simulation->joinHuman(Person->Identity);
            S->Simulation->setThrowable(Person->UnitId, Person->Throwable);
        }
    PromoLoadout(X, St);
    S->Publish();
    return true;
}
// Everyone the capture drives plays like the autopilot: a stroll round an anchor, the gun on the nearest enemy in reach.
void PromoDrive(FPromoContext& X, FPromoState& St, int32 Id, const FPromoActor& A, bool Dummies = false)
{
    Battle* B = X.Sim();
    Unit* U = B ? B->find(Id) : nullptr;
    if (!U || !U->alive || U->carried || U->bait > 0 || U->state == UnitState::Launched || U->climb >= 0 || U->followerOf >= 0 || U->charging)
        return;
    Input In;
    const double T = St.Clock;
    const Vec2 Goal = A.Anchor + Vec2{std::cos(T * .8 + A.Phase), std::sin(T * 1.1 + A.Phase * 1.7)} * A.Sway;
    const Vec2 To = Goal - U->position;
    const double Far = To.length();
    In.move = Far > 18 ? To * (1. / Far) * std::min(1., Far / 140) : Vec2{};
    if (const Unit* Foe = PromoNearestFoe(*B, U->position, 1600, Dummies))
    {
        In.aim = (Foe->position - U->position).normalized();
        In.fire = A.Fire;
    }
    else
        In.aim = St.Aim;
    B->setInput(Id, In);
}
void PromoDriveAll(FPromoContext& X, FPromoState& St, bool Dummies = false)
{
    for (const FPromoActor& A : St.Crew)
        if (A.Drive)
            PromoDrive(X, St, PromoUnitOf(A), A, Dummies);
}
// Puts a unit on a platform cell facing into the box.
void PromoStand(Battle& B, int32 Id, Vec2 At)
{
    if (Unit* U = B.find(Id))
    {
        U->position = At;
        U->deck = true;
        U->velocity = U->impulse = {};
        const Vec2 Middle{B.maze.sizeX() * .5, B.maze.sizeY() * .5};
        U->aim = (Middle - At).normalized();
    }
}
void PromoTopUp(FPromoContext& X)
{
    if (Unit* Me = X.Me())
        Me->ink = Me->maxInk;
}
void PromoCalm(Battle& B)
{
    for (auto& U : B.units)
        if (U.kind == UnitKind::Monster && !U.dummy)
            U.alive = false;
    B.defense.queue.clear();
    B.defense.queued = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Defense: page 1-2 opens under the page wipe, is drawn and built on, holds a wave, takes a rocket, meets its
// boss and is won. Four players: the host and three friends.
// ---------------------------------------------------------------------------------------------------------------
TArray<FPromoShot> PromoDefenseShots()
{
    TArray<FPromoShot> Shots;
    constexpr int32 Page = 1;
    {
        FPromoShot Open;
        Open.Name = TEXT("td_open");
        Open.Frames = 165;
        Open.Warmup = 12;
        Open.Clean = false;
        Open.Promo = true;
        Open.Camera = false;
        Open.Wipes = true;
        Open.Setup = [](FPromoContext& X, FPromoState& St)
        {
            PromoInvite(X, St, 3);
            St.Started = false;
            return X.GameMode->BeginHub();
        };
        Open.Tick = [](FPromoContext& X, FPromoState& St, int32 Frame)
        {
            FGWTransition& Wipe = FGWTransition::Get();
            if (Frame == 0)
                Wipe.Close(Page, true);
            if (Frame >= 0 && !St.Started && Wipe.Stage == FGWTransition::EStage::Closed)
            {
                St.Started = PromoStartPage(X, St, Page, 0x1A2B3C4D);
                if (Battle* B = X.Sim())
                {
                    const int32 Ids[] = {X.Player->UnitId, PromoUnitOf(St.Crew[0]), PromoUnitOf(St.Crew[1]), PromoUnitOf(St.Crew[2])};
                    const Vec2 Spots[] = {PromoAt(*B, 5, 6), PromoAt(*B, 3, 0), PromoAt(*B, 8, 0), PromoAt(*B, 9, 6)};
                    for (int32 I = 0; I < 4; ++I)
                        PromoStand(*B, Ids[I], Spots[I]);
                    X.State->Publish();
                }
            }
        };
        Shots.Add(Open);
    }
    {
        // Walls go down one cell at a time into a serpentine; the ring raises a bolt tower, a mortar and a tesla coil
        // (each pops up like jelly), then grows the bolt tower a side.
        FPromoShot Build;
        Build.Name = TEXT("td_build");
        Build.Frames = 200;
        Build.Warmup = 4;
        Build.Clean = false;
        Build.Promo = true;
        Build.Camera = false;
        Build.Setup = [](FPromoContext& X, FPromoState&)
        {
            PromoTopUp(X);
            // The cues set themselves up on first use (a hitch): each plays once off the page before the shot.
            for (const TCHAR* Name : {TEXT("NS_GW_TowerBuild"), TEXT("NS_GW_TowerUpgrade")})
                if (auto* System = LoadObject<UNiagaraSystem>(nullptr, *FString::Printf(TEXT("/Game/Sketch/VFX/%s.%s"), Name, Name)))
                    UNiagaraFunctionLibrary::SpawnSystemAtLocation(X.World, System, FVector(-5000, -5000, -2000));
            return X.Sim() != nullptr;
        };
        Build.Tick = [](FPromoContext& X, FPromoState& St, int32 Frame)
        {
            Battle* B = X.Sim();
            if (!B || Frame < 0)
                return;
            static const int32 Walls[][2] = {{4, 1}, {4, 2}, {4, 3}, {4, 4}, {8, 5}, {8, 4}, {8, 3}, {8, 2}};
            const int32 Painted = Frame / 5;
            if (Frame % 5 == 0 && Painted < static_cast<int32>(UE_ARRAY_COUNT(Walls)))
            {
                PromoTopUp(X);
                B->paintWall(X.Player->UnitId, PromoCell(*B, Walls[Painted][0], Walls[Painted][1]));
                X.State->Publish();
            }
            struct FRingStep
            {
                int32 Open, Take, X, Y, Slot;
            };
            // (7, 4) would seal the serpentine's only gap past the middle rocks: the tesla coil goes by the gates instead.
            static const FRingStep Steps[] = {{50, 68, 5, 2, 0}, {84, 102, 10, 5, 1}, {118, 136, 3, 3, 2}, {154, 172, 5, 2, 0}};
            for (const FRingStep& Step : Steps)
            {
                if (Frame == Step.Open)
                {
                    PromoTopUp(X);
                    X.State->Publish();
                    X.Hud->OpenRing(PromoCell(*B, Step.X, Step.Y));
                    X.Hud->RingHoverOverride = Step.Slot;
                }
                if (Frame == Step.Take)
                {
                    X.Hud->TakeRingSlot(Step.Slot);
                    X.Hud->RingHoverOverride = -1;
                }
            }
        };
        Shots.Add(Build);
    }
    {
        // Wave 1 (called early, with company) marches through the serpentine; the towers and the four players on the
        // platform hold it.
        FPromoShot Wave;
        Wave.Name = TEXT("td_wave");
        Wave.Frames = 225;
        Wave.Warmup = 2;
        Wave.Story = true;
        Wave.Setup = [](FPromoContext& X, FPromoState& St)
        {
            Battle* B = X.Sim();
            if (!B)
                return false;
            B->callWave(X.Player->UnitId);
            const int32 Ids[] = {X.Player->UnitId, PromoUnitOf(St.Crew[0]), PromoUnitOf(St.Crew[1]), PromoUnitOf(St.Crew[2])};
            const Vec2 Spots[] = {PromoAt(*B, 6, 0), PromoAt(*B, 2, 0), PromoAt(*B, 4, 6), PromoAt(*B, 8, 6)};
            for (int32 I = 0; I < 4; ++I)
                PromoStand(*B, Ids[I], Spots[I]);
            for (int32 I = 0; I < 3; ++I)
                St.Crew[I].Anchor = Spots[I + 1];
            St.Spot = Spots[0];
            St.Aim = {0, 1};
            // The wave is already through the gates when the camera arrives (the banner still comes in on screen).
            for (int32 I = 0; I < 60 * 4; ++I)
                B->advance(1. / 60);
            X.State->Publish();
            St.Focus = FVector(B->maze.sizeX() * .47, B->maze.sizeY() * .52, 0);
            St.Distance = 4700;
            St.Pitch = 58;
            St.Yaw = -90;
            return true;
        };
        Wave.Tick = [](FPromoContext& X, FPromoState& St, int32 Frame)
        {
            Battle* B = X.Sim();
            if (!B)
                return;
            FPromoActor Me;
            Me.State = X.Player;
            Me.Anchor = St.Spot;
            PromoDrive(X, St, X.Player->UnitId, Me);
            PromoDriveAll(X, St);
            // More of them, from both gates.
            if (Frame >= 0 && Frame < 150 && Frame % 9 == 0)
            {
                const Shape Kinds[] = {Shape::Circle, Shape::Triangle, Shape::Circle, Shape::Square};
                B->spawnWaveEnemy(Kinds[(Frame / 9) % 4], Frame % 54 == 0, (Frame / 9) % 2);
            }
            const float K = PromoRamp(Frame, 0, 225);
            St.Distance = FMath::Lerp(4700.f, 3900.f, K);
            St.Pitch = FMath::Lerp(58.f, 54.f, K);
            St.Yaw = FMath::Lerp(-90.f, -84.f, K);
            St.Focus = FVector(B->maze.sizeX() * FMath::Lerp(.47f, .5f, K), B->maze.sizeY() * .52f, 0);
        };
        Shots.Add(Wave);
    }
    {
        // The rocket launcher: a shell lobbed from the platform over the walls into the thickest knot of the wave.
        FPromoShot Rocket;
        Rocket.Name = TEXT("td_rocket");
        Rocket.Frames = 105;
        Rocket.Warmup = 2;
        Rocket.Story = true;
        Rocket.Setup = [](FPromoContext& X, FPromoState& St)
        {
            Battle* B = X.Sim();
            Unit* Me = X.Me();
            if (!B || !Me)
                return false;
            int32 Count = 0;
            const Vec2 Knot = PromoCrowd(*B, PromoAt(*B, 6, 4), &Count);
            // The platform cell above or below the knot.
            const int32 Column = FMath::Clamp(B->maze.cell(Knot) % B->maze.width, 1, B->maze.width - 2);
            const bool Top = Knot.y < B->maze.sizeY() * .5;
            const Vec2 From = PromoAt(*B, FMath::Clamp(Column - 2, 1, B->maze.width - 2), Top ? 0 : B->maze.height - 1);
            PromoStand(*B, Me->id, From);
            B->pickupWeapon(Me->id, RocketWeapon);
            Me->ammo = 99;
            St.Spot = Knot;
            St.Aim = (Knot - From).normalized();
            Me->aim = St.Aim;
            St.Started = false;
            St.Focus = PromoFlat(Knot - Vec2{0, 140});
            St.Distance = 2500;
            St.Pitch = 56;
            St.Yaw = -90;
            UE_LOG(LogTemp, Display, TEXT("Promo rocket: knot of %d at (%.0f, %.0f)"), Count, Knot.x, Knot.y);
            return true;
        };
        Rocket.Tick = [](FPromoContext& X, FPromoState& St, int32 Frame)
        {
            Battle* B = X.Sim();
            Unit* Me = X.Me();
            if (!B || !Me)
                return;
            PromoDriveAll(X, St);
            if (Frame < 16)
            {
                // Follow the knot as it walks, so the shell meets it.
                St.Spot = PromoCrowd(*B, St.Spot);
                Me->aim = (St.Spot + Vec2{0, 0} - Me->position).normalized();
                B->setInput(Me->id, Input{{}, Me->aim, false, 0, false});
            }
            if (Frame == 16)
            {
                const double Reach = (St.Spot - Me->position).length() + 60;
                Me->aim = (St.Spot - Me->position).normalized();
                B->lob(*Me, (Reach - RocketMinRange) / (RocketMaxRange - RocketMinRange));
                X.State->Publish();
            }
            // The blast is the subject: the camera sits on the knot (the launcher stays in the lower edge of the frame).
            PromoFollow(St, PromoFlat(St.Spot - Vec2{0, 140}), 2.f);
            St.Distance = FMath::Lerp(2500.f, 2150.f, PromoRamp(Frame, 16, 70));
            St.Pitch = 58;
        };
        Shots.Add(Rocket);
    }
    {
        // The last wave brings the boss: its laser sweeps the maze and cuts walls while everyone pours fire into it.
        FPromoShot Boss;
        Boss.Name = TEXT("td_boss");
        Boss.Frames = 180;
        Boss.Warmup = 4;
        Boss.Story = true;
        Boss.QuietClears = true;
        Boss.Setup = [](FPromoContext& X, FPromoState& St)
        {
            Battle* B = X.Sim();
            if (!B)
                return false;
            auto& D = B->defense;
            PromoCalm(*B);
            B->advance(.1);
            D.wavesCleared = D.waves - 1;
            D.coreHealth = D.coreMaxHealth;
            D.phase = DefensePhase::Build;
            B->callWave(X.Player->UnitId);
            for (int32 I = 0; I < 60 * 9; ++I)
                B->advance(1. / 60);
            St.Boss = -1;
            for (auto& U : B->units)
                if (U.alive && U.kind == UnitKind::Monster && U.shape == Shape::Trapezoid)
                {
                    U.position = PromoAt(*B, 6, 4);
                    U.laserCooldown = .9;
                    U.hp = U.maxHp;
                    St.Boss = U.id;
                    break;
                }
            const int32 Ids[] = {X.Player->UnitId, PromoUnitOf(St.Crew[0]), PromoUnitOf(St.Crew[1]), PromoUnitOf(St.Crew[2])};
            const Vec2 Spots[] = {PromoAt(*B, 5, 0), PromoAt(*B, 8, 0), PromoAt(*B, 5, 6), PromoAt(*B, 9, 6)};
            for (int32 I = 0; I < 4; ++I)
                PromoStand(*B, Ids[I], Spots[I]);
            for (int32 I = 0; I < 3; ++I)
                St.Crew[I].Anchor = Spots[I + 1];
            St.Spot = Spots[0];
            if (Unit* Me = X.Me())
                B->pickupWeapon(Me->id, 4);
            X.State->Publish();
            St.Focus = PromoFlat(PromoAt(*B, 6, 3));
            St.Distance = 2900;
            St.Pitch = 57;
            St.Yaw = -95;
            UE_LOG(LogTemp, Display, TEXT("Promo boss: id %d"), St.Boss);
            return St.Boss > 0;
        };
        Boss.Tick = [](FPromoContext& X, FPromoState& St, int32 Frame)
        {
            Battle* B = X.Sim();
            if (!B)
                return;
            FPromoActor Me;
            Me.State = X.Player;
            Me.Anchor = St.Spot;
            PromoDrive(X, St, X.Player->UnitId, Me);
            PromoDriveAll(X, St);
            if (Unit* Boss = B->find(St.Boss))
            {
                // It must outlive the shot.
                Boss->hp = FMath::Max(Boss->hp, Boss->maxHp * .45);
                PromoFollow(St, PromoSeen(X, Boss->id) + FVector(0, -150, 0), 1.6f);
            }
            const float K = PromoRamp(Frame, 0, 180);
            St.Distance = FMath::Lerp(2900.f, 2400.f, K);
            St.Yaw = FMath::Lerp(-95.f, -86.f, K);
        };
        Shots.Add(Boss);
    }
    {
        // Every enemy down on the last wave: the page is won and the result card is stamped.
        FPromoShot Win;
        Win.Name = TEXT("td_victory");
        Win.Frames = 120;
        Win.Warmup = 1;
        Win.Clean = false;
        Win.Promo = true;
        Win.Camera = false;
        Win.Setup = [](FPromoContext& X, FPromoState&)
        {
            Battle* B = X.Sim();
            if (!B)
                return false;
            PromoCalm(*B);
            B->advance(.1);
            X.State->Publish();
            return true;
        };
        Shots.Add(Win);
    }
    return Shots;
}

// ---------------------------------------------------------------------------------------------------------------
// Coop: eight seats. The hub fills up, a friend is grabbed and thrown into the box as bait, eight hold the raid page,
// and the result card hands out the room's titles.
// ---------------------------------------------------------------------------------------------------------------
TArray<FPromoShot> PromoCoopShots()
{
    TArray<FPromoShot> Shots;
    {
        FPromoShot Hub;
        Hub.Name = TEXT("coop_hub");
        Hub.Frames = 165;
        Hub.Warmup = 30;
        Hub.Clean = false;
        Hub.Promo = true;
        Hub.Setup = [](FPromoContext& X, FPromoState& St)
        {
            PromoInvite(X, St, 7);
            if (!X.GameMode->BeginHub())
                return false;
            Battle* B = X.Sim();
            PromoLoadout(X, St);
            // Spread round the room: a knot at the board, two at the training corner, the rest walking about.
            const Vec2 Middle{B->maze.sizeX() * .5, B->maze.sizeY() * .5};
            for (int32 I = 0; I < St.Crew.Num(); ++I)
            {
                const double A = I * 2 * PI / St.Crew.Num();
                St.Crew[I].Anchor = Middle + Vec2{std::cos(A) * 560, std::sin(A) * 330};
                St.Crew[I].Sway = 150;
                St.Crew[I].Fire = I % 3 == 0;
                if (Unit* U = B->find(PromoUnitOf(St.Crew[I])))
                    U->position = St.Crew[I].Anchor;
            }
            St.Spot = Middle + Vec2{-120, 160};
            if (Unit* Me = X.Me())
                Me->position = St.Spot;
            X.State->Publish();
            St.Focus = PromoFlat(Middle + Vec2{-260, 90});
            St.Distance = 2700;
            St.Pitch = 58;
            St.Yaw = -90;
            return true;
        };
        Hub.Tick = [](FPromoContext& X, FPromoState& St, int32 Frame)
        {
            Battle* B = X.Sim();
            if (!B)
                return;
            FPromoActor Me;
            Me.State = X.Player;
            Me.Anchor = St.Spot;
            Me.Sway = 160;
            Me.Fire = false;
            PromoDrive(X, St, X.Player->UnitId, Me, true);
            PromoDriveAll(X, St, true);
            const Vec2 Middle{B->maze.sizeX() * .5, B->maze.sizeY() * .5};
            const float K = PromoRamp(Frame, -30, 165);
            St.Focus = PromoFlat(Middle + Vec2{FMath::Lerp(-260.f, 260.f, K), 90});
            St.Distance = FMath::Lerp(2700.f, 2450.f, K);
        };
        Shots.Add(Hub);
    }
    {
        FPromoShot Throw;
        Throw.Name = TEXT("coop_throw");
        Throw.Frames = 200;
        Throw.Warmup = 6;
        Throw.Story = true;
        Throw.Setup = [](FPromoContext& X, FPromoState& St)
        {
            if (!PromoStartPage(X, St, 3, 0x7E57AB1E))
                return false;
            Battle* B = X.Sim();
            // A wave that never runs out on its own: the capture brings the enemies.
            B->defense.phase = DefensePhase::Wave;
            B->defense.queue.clear();
            SpawnOrder Never;
            Never.at = 1e9;
            B->defense.queue.push_back(Never);
            B->defense.queued = 0;
            const Vec2 Spots[] = {PromoAt(*B, 3, 0), PromoAt(*B, 8, 0), PromoAt(*B, 10, 0), PromoAt(*B, 2, 7),
                                  PromoAt(*B, 4, 7), PromoAt(*B, 8, 7), PromoAt(*B, 10, 7)};
            PromoStand(*B, X.Player->UnitId, PromoAt(*B, 3, 0));
            for (int32 I = 0; I < St.Crew.Num(); ++I)
            {
                St.Crew[I].Anchor = Spots[I];
                St.Crew[I].Sway = 50;
                PromoStand(*B, PromoUnitOf(St.Crew[I]), Spots[I]);
            }
            // The friend to throw stands beside the host and waits.
            St.Mate = PromoUnitOf(St.Crew[0]);
            St.Crew[0].Drive = false;
            if (Unit* Mate = B->find(St.Mate))
                Mate->position = PromoAt(*B, 3, 0) + Vec2{95, 0};
            St.Spot = PromoAt(*B, 3, 3);
            // Enemies on the box floor round where the friend will land, and more walking in.
            const Vec2 Offsets[] = {{-430, -150}, {-380, 240}, {380, -240}, {440, 200}, {60, 460}, {-150, 420}, {520, -20}, {-520, 40}};
            for (const Vec2& Offset : Offsets)
                if (Unit* M = B->find(B->spawnWaveEnemy(Shape::Circle, false, 0)))
                {
                    M->position = B->maze.walkable(St.Spot + Offset, M->radius) ? St.Spot + Offset : St.Spot + Offset * .5;
                    M->hp = M->maxHp = 900;
                }
            X.State->Publish();
            St.Started = false;
            St.Focus = PromoFlat(PromoAt(*B, 3, 0) + Vec2{40, 150});
            St.Distance = 1900;
            St.Pitch = 55;
            St.Yaw = -90;
            return true;
        };
        Throw.Tick = [](FPromoContext& X, FPromoState& St, int32 Frame)
        {
            Battle* B = X.Sim();
            Unit* Me = X.Me();
            if (!B || !Me)
                return;
            PromoDriveAll(X, St);
            if (Frame < 50)
                B->setInput(Me->id, Input{{}, (St.Spot - Me->position).normalized(), false, 0, false});
            if (Frame == 14)
                UE_LOG(LogTemp, Display, TEXT("Promo throw: grabbed %d"), B->recruit(Me->id));
            if (Frame == 34)
                B->beginCharge(Me->id);
            if (Frame == 52)
            {
                Me->chargeTime = .5;
                UE_LOG(LogTemp, Display, TEXT("Promo throw: thrown %d"), B->releaseCharge(Me->id, (St.Spot - Me->position).normalized()));
                St.Started = true;
            }
            if (Frame > 60)
            {
                FPromoActor Host;
                Host.State = X.Player;
                Host.Anchor = PromoAt(*B, 3, 0);
                Host.Sway = 40;
                PromoDrive(X, St, Me->id, Host);
            }
            // More of them walking in from the gates.
            if (Frame > 40 && Frame < 170 && Frame % 14 == 0)
                B->spawnWaveEnemy(Frame % 28 ? Shape::Triangle : Shape::Circle, false, (Frame / 14) % 2);
            const FVector Mate = PromoSeen(X, St.Mate);
            if (Frame < 50)
                PromoFollow(St, PromoFlat(PromoAt(*B, 3, 0) + Vec2{40, 150}), 3);
            else if (Frame < 95)
                PromoFollow(St, Mate + FVector(0, 120, 0), 3.2f);
            else
                PromoFollow(St, Mate + FVector(60, 80, 0), 1.6f);
            St.Distance = Frame < 50 ? 1900.f : FMath::Lerp(1900.f, 2700.f, PromoRamp(Frame, 50, 130));
        };
        Shots.Add(Throw);
    }
    {
        FPromoShot Hold;
        Hold.Name = TEXT("coop_defend");
        Hold.Frames = 200;
        Hold.Warmup = 2;
        Hold.Story = true;
        Hold.Setup = [](FPromoContext& X, FPromoState& St)
        {
            if (!PromoStartPage(X, St, 7, 0x3E2D1C0B))
                return false;
            Battle* B = X.Sim();
            const int32 Me = X.Player->UnitId;
            PromoTopUp(X);
            static const int32 Walls[][2] = {{3, 2}, {3, 3}, {3, 4}, {3, 5}, {8, 6}, {8, 5}, {8, 4}, {8, 3}};
            for (const auto& W : Walls)
            {
                PromoTopUp(X);
                B->paintWall(Me, PromoCell(*B, W[0], W[1]));
            }
            static const int32 Towers[][3] = {{5, 3, 0}, {6, 6, 1}, {9, 2, 2}, {5, 5, 0}, {7, 1, 2}};
            for (const auto& T : Towers)
            {
                PromoTopUp(X);
                B->buildTower(Me, PromoCell(*B, T[0], T[1]), T[2]);
            }
            B->defense.wavesCleared = 7;
            B->callWave(Me);
            const Vec2 Spots[] = {PromoAt(*B, 1, 0), PromoAt(*B, 5, 0), PromoAt(*B, 10, 0), PromoAt(*B, 3, 8), PromoAt(*B, 6, 8),
                                  PromoAt(*B, 9, 8), PromoAt(*B, 11, 5)};
            PromoStand(*B, Me, PromoAt(*B, 7, 0));
            St.Spot = PromoAt(*B, 7, 0);
            for (int32 I = 0; I < St.Crew.Num(); ++I)
            {
                St.Crew[I].Anchor = Spots[I];
                St.Crew[I].Sway = 60;
                St.Crew[I].Drive = true;
                PromoStand(*B, PromoUnitOf(St.Crew[I]), Spots[I]);
            }
            for (int32 I = 0; I < 60 * 6; ++I)
                B->advance(1. / 60);
            X.State->Publish();
            St.Focus = FVector(B->maze.sizeX() * .5, B->maze.sizeY() * .56, 0);
            St.Distance = 4500;
            St.Pitch = 58;
            St.Yaw = -97;
            return true;
        };
        Hold.Tick = [](FPromoContext& X, FPromoState& St, int32 Frame)
        {
            Battle* B = X.Sim();
            if (!B)
                return;
            FPromoActor Me;
            Me.State = X.Player;
            Me.Anchor = St.Spot;
            PromoDrive(X, St, X.Player->UnitId, Me);
            PromoDriveAll(X, St);
            if (Frame >= 0 && Frame < 160 && Frame % 7 == 0)
            {
                const Shape Kinds[] = {Shape::Circle, Shape::Triangle, Shape::Square, Shape::Circle, Shape::Triangle};
                const int32 Gates = FMath::Max(1, static_cast<int32>(B->defense.raids.size()));
                const bool Raid = Frame % 21 == 0;
                B->spawnWaveEnemy(Kinds[(Frame / 7) % 5], Frame % 35 == 0, Raid ? (Frame / 21) % Gates : (Frame / 7) % 2, Raid);
            }
            const float K = PromoRamp(Frame, 0, 200);
            St.Distance = FMath::Lerp(4500.f, 3900.f, K);
            St.Yaw = FMath::Lerp(-97.f, -84.f, K);
        };
        Shots.Add(Hold);
    }
    {
        FPromoShot Win;
        Win.Name = TEXT("coop_results");
        Win.Frames = 120;
        Win.Warmup = 1;
        Win.Clean = false;
        Win.Promo = true;
        Win.Camera = false;
        Win.Setup = [](FPromoContext& X, FPromoState&)
        {
            Battle* B = X.Sim();
            if (!B)
                return false;
            auto& D = B->defense;
            PromoCalm(*B);
            D.wavesCleared = D.waves - 1;
            D.phase = DefensePhase::Build;
            B->callWave(X.Player->UnitId);
            PromoCalm(*B);
            B->advance(.2);
            X.State->Publish();
            return true;
        };
        Shots.Add(Win);
    }
    return Shots;
}

// ---------------------------------------------------------------------------------------------------------------
// Hub: the station sheets behind the systems still being built (weapons in the shop, the achievement wall, the
// wardrobe, the research desk) and the board's expedition and party tabs.
// ---------------------------------------------------------------------------------------------------------------
TArray<FPromoShot> PromoHubShots()
{
    TArray<FPromoShot> Shots;
    auto Sheet = [&Shots](const TCHAR* Name, AGWHUD::EPanel Panel, int32 Frames, int32 Tab)
    {
        FPromoShot Shot;
        Shot.Name = Name;
        Shot.Frames = Frames;
        Shot.Warmup = Shots.IsEmpty() ? 45 : 8;
        Shot.Clean = false;
        Shot.Promo = true;
        Shot.Camera = false;
        const bool First = Shots.IsEmpty();
        Shot.Setup = [Panel, Tab, First](FPromoContext& X, FPromoState& St)
        {
            if (First)
            {
                PromoInvite(X, St, 7);
                if (!X.GameMode->BeginHub())
                    return false;
                PromoLoadout(X, St);
                X.Instance->GrantStickers(810);
                Battle* B = X.Sim();
                const Vec2 Middle{B->maze.sizeX() * .5, B->maze.sizeY() * .5};
                for (int32 I = 0; I < St.Crew.Num(); ++I)
                {
                    const double A = I * 2 * PI / St.Crew.Num();
                    St.Crew[I].Anchor = Middle + Vec2{std::cos(A) * 700, std::sin(A) * 420};
                    St.Crew[I].Sway = 160;
                    St.Crew[I].Fire = false;
                    if (Unit* U = B->find(PromoUnitOf(St.Crew[I])))
                        U->position = St.Crew[I].Anchor;
                }
                if (Unit* Me = X.Me())
                    Me->position = Middle;
                X.State->Publish();
            }
            X.Hud->OpenPanel(AGWHUD::EPanel::None);
            X.Hud->OpenPanel(Panel);
            if (Panel == AGWHUD::EPanel::Shop)
                X.Hud->SetShopPage(Tab);
            if (Panel == AGWHUD::EPanel::Board)
                X.Hud->ShowMode(Tab);
            return true;
        };
        Shot.Tick = [](FPromoContext& X, FPromoState& St, int32)
        {
            PromoDriveAll(X, St, true);
        };
        Shots.Add(Shot);
    };
    Sheet(TEXT("hub_shop"), AGWHUD::EPanel::Shop, 80, 0);
    Sheet(TEXT("hub_achievements"), AGWHUD::EPanel::Achievements, 80, 0);
    Sheet(TEXT("hub_wardrobe"), AGWHUD::EPanel::Wardrobe, 80, 0);
    Sheet(TEXT("hub_research"), AGWHUD::EPanel::Research, 80, 0);
    Sheet(TEXT("hub_expedition"), AGWHUD::EPanel::Board, 60, 1);
    Sheet(TEXT("hub_party"), AGWHUD::EPanel::Board, 60, 2);
    // Close-ups for the systems still being built: the six guns side by side, then everyone's hats.
    auto Lineup = [](FPromoContext& X, FPromoState& St, int32 Count)
    {
        TArray<int32> Ids = {X.Player->UnitId};
        for (int32 I = 0; I < St.Crew.Num() && Ids.Num() < Count; ++I)
            Ids.Add(PromoUnitOf(St.Crew[I]));
        return Ids;
    };
    {
        FPromoShot Guns;
        Guns.Name = TEXT("hub_guns");
        Guns.Frames = 150;
        Guns.Warmup = 10;
        Guns.Setup = [Lineup](FPromoContext& X, FPromoState& St)
        {
            X.Hud->OpenPanel(AGWHUD::EPanel::None);
            Battle* B = X.Sim();
            if (!B)
                return false;
            const Vec2 Middle{B->maze.sizeX() * .5, B->maze.sizeY() * .58};
            const TArray<int32> Ids = Lineup(X, St, 6);
            for (FPromoActor& A : St.Crew)
            {
                A.Drive = false;
                if (Unit* U = B->find(PromoUnitOf(A)))
                    U->position = Middle + Vec2{0, 1400};
            }
            // Pistol, rifle, shotgun, sniper rifle, machine gun and rocket launcher, left to right.
            for (int32 I = 0; I < Ids.Num(); ++I)
                if (Unit* U = B->find(Ids[I]))
                {
                    U->position = Middle + Vec2{(I - 2.5) * 175, 0};
                    U->velocity = U->impulse = {};
                    if (I > 0)
                        B->pickupWeapon(U->id, I);
                    U->ammo = 9999;
                    U->aim = {0, -1};
                }
            St.Focus = PromoFlat(Middle + Vec2{0, -220});
            St.Distance = 1550;
            St.Pitch = 52;
            St.Yaw = -96;
            X.State->Publish();
            return true;
        };
        Guns.Tick = [Lineup](FPromoContext& X, FPromoState& St, int32 Frame)
        {
            Battle* B = X.Sim();
            if (!B)
                return;
            const TArray<int32> Ids = Lineup(X, St, 6);
            for (int32 I = 0; I < Ids.Num(); ++I)
            {
                Input In;
                In.aim = Vec2{std::sin(St.Clock * 1.3 + I) * .22, -1}.normalized();
                // The launcher is held to charge and let go every second and a half; the rest keep the trigger down.
                In.fire = I == 5 ? Frame % 45 < 30 : true;
                B->setInput(Ids[I], In);
            }
            St.Distance = FMath::Lerp(1550.f, 1380.f, PromoRamp(Frame, 0, 150));
            St.Yaw = FMath::Lerp(-96.f, -86.f, PromoRamp(Frame, 0, 150));
        };
        Shots.Add(Guns);
    }
    {
        FPromoShot Hats;
        Hats.Name = TEXT("hub_hats");
        Hats.Frames = 150;
        Hats.Warmup = 10;
        Hats.Setup = [Lineup](FPromoContext& X, FPromoState& St)
        {
            Battle* B = X.Sim();
            if (!B)
                return false;
            X.Player->Hat = 2;
            const Vec2 Middle{B->maze.sizeX() * .5, B->maze.sizeY() * .58};
            const TArray<int32> Ids = Lineup(X, St, 8);
            // A gentle arc facing the camera.
            for (int32 I = 0; I < Ids.Num(); ++I)
                if (Unit* U = B->find(Ids[I]))
                {
                    const double K = I - (Ids.Num() - 1) * .5;
                    U->position = Middle + Vec2{K * 150, -std::abs(K) * 26};
                    U->velocity = U->impulse = {};
                }
            St.Focus = PromoFlat(Middle + Vec2{0, -40});
            St.Distance = 1300;
            St.Pitch = 46;
            St.Yaw = -98;
            X.State->Publish();
            return true;
        };
        Hats.Tick = [Lineup](FPromoContext& X, FPromoState& St, int32 Frame)
        {
            Battle* B = X.Sim();
            if (!B)
                return;
            const TArray<int32> Ids = Lineup(X, St, 8);
            for (int32 I = 0; I < Ids.Num(); ++I)
            {
                // Each one looks round at its neighbours and back at the camera.
                Input In;
                const double A = PI * .5 + std::sin(St.Clock * 1.6 + I * .9) * .9;
                In.aim = Vec2{std::cos(A), std::sin(A)};
                B->setInput(Ids[I], In);
            }
            St.Yaw = FMath::Lerp(-98.f, -82.f, PromoRamp(Frame, 0, 150));
            St.Distance = FMath::Lerp(1300.f, 1150.f, PromoRamp(Frame, 0, 150));
        };
        Shots.Add(Hats);
    }
    return Shots;
}

// The title page's brawl, clean: a whole minute of it (its events come round once a minute).
TArray<FPromoShot> PromoBrawlShots()
{
    FPromoShot Brawl;
    Brawl.Name = TEXT("brawl");
    Brawl.Frames = 1800;
    Brawl.Warmup = 20;
    Brawl.Camera = false;
    Brawl.Setup = [](FPromoContext& X, FPromoState&)
    {
        X.Hud->SetPage(AGWHUD::EPage::Main);
        if (X.Controller->WorldView)
            X.Controller->WorldView->ShowcaseSeek = 0;
        return !X.State->InMatch;
    };
    return {Brawl};
}

class FPromoCapture : public IAutomationLatentCommand
{
public:
    FPromoCapture(FAutomationTestBase* InTest, TArray<FPromoShot> InShots) : Test(InTest), Shots(MoveTemp(InShots))
    {
        FParse::Value(FCommandLine::Get(), TEXT("PromoStride="), Stride);
        Stride = FMath::Max(1, Stride);
    }
    virtual bool Update() override
    {
        FPromoContext X;
        X.World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        X.Controller = X.World ? Cast<AGWController>(X.World->GetFirstPlayerController()) : nullptr;
        X.GameMode = X.World ? X.World->GetAuthGameMode<AGWGameMode>() : nullptr;
        X.State = X.World ? X.World->GetGameState<AGWGameState>() : nullptr;
        X.Player = X.Controller ? X.Controller->GetPlayerState<AGWPlayerState>() : nullptr;
        X.Hud = X.Controller ? Cast<AGWHUD>(X.Controller->GetHUD()) : nullptr;
        X.Instance = X.World ? X.World->GetGameInstance<UGWGameInstance>() : nullptr;
        if (!X.Controller || !X.GameMode || !X.State || !X.Player || !X.Hud || !X.Instance || !X.Controller->WorldView)
        {
            if (++Waiting > 900)
            {
                Test->AddError(TEXT("Promo capture never reached the title page"));
                return Finish(X);
            }
            return false;
        }
        // One capture frame is one fixed step: the sketchbook's wall-clock timers step with it.
        double& Clock = GWClock::Override();
        Clock = Clock < 0 ? FPlatformTime::Seconds() : Clock + PromoStep;
        Scratch.Clock += PromoStep;
        if (Index < 0)
        {
            Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
            Levels.SetFromSingleQualityLevel(3);
            Scalability::SetQualityLevels(Levels);
            // No live input fights the scripted players (and no stale prediction moves the host).
            X.Controller->SetActorTickEnabled(false);
            GWText::SetCulture(TEXT("zh-Hans"));
            PromoParkProfiles(true);
            Parked = true;
            OriginalSlot = X.Instance->ProfileSlot();
            X.Instance->SelectProfile(0);
            X.Player->SetPlayerName(TEXT("铅笔头"));
            UGWFxDirector::Notes = &SoundNotes;
            Index = 0;
            Frame = Pending;
            return false;
        }
        if (Index >= Shots.Num())
            return Finish(X);
        FPromoShot& Shot = Shots[Index];
        FGWCinematic& Cinematic = FGWCinematic::Get();
        // The sound sheet: what the effects director played since the last update is on the plate requested then.
        NoteFrame(X, Shot, Frame == Pending ? Pending : Frame - 1);
        if (Frame == Pending)
        {
            Cinematic.Active = Shot.Camera;
            Cinematic.Clean = Shot.Clean && !Shot.Promo;
            Cinematic.Story = Shot.Story;
            Cinematic.Promo = Shot.Promo;
            Cinematic.QuietClears = Shot.QuietClears;
            Cinematic.SteamRoom = true;
            FGWTransition::Get().ForceInTests = Shot.Wipes;
            // No sticky note from an earlier shot rides into this one.
            X.Controller->NoticeKey.Empty();
            X.Instance->StatusKey.Empty();
            Directory = FPaths::ProjectSavedDir() / TEXT("Promo") / TEXT("Plates") / Shot.Name;
            IFileManager::Get().DeleteDirectory(*Directory, false, true);
            IFileManager::Get().MakeDirectory(*Directory, true);
            if (Shot.Setup && !Shot.Setup(X, Scratch))
            {
                Test->AddError(FString::Printf(TEXT("Promo shot %s could not be staged"), *Shot.Name));
                ++Index;
                return false;
            }
            Frame = -Shot.Warmup;
            ShotClock = FPlatformTime::Seconds();
            ShotGameTime = X.World->GetTimeSeconds();
        }
        else if (Frame >= Shot.Frames)
        {
            // Quiet frames let the last screenshot land before the next setup changes the scene.
            if (++Frame >= Shot.Frames + 3)
            {
                Sheet.Insert(TEXT("frame,what,a,b,x,y,x2,y2,size,wx,wy"), 0);
                FFileHelper::SaveStringArrayToFile(Sheet, *(Directory / TEXT("events.csv")));
                UE_LOG(LogTemp, Display, TEXT("Promo plate %s: %d frames, %d sound notes, %.2fs game time, %.1fs wall"), *Shot.Name,
                       Shot.Frames, Sheet.Num() - 1, X.World->GetTimeSeconds() - ShotGameTime, FPlatformTime::Seconds() - ShotClock);
                Sheet.Reset();
                ++Index;
                Frame = Pending;
            }
            return false;
        }
        if (Shot.Tick)
            Shot.Tick(X, Scratch, Frame);
        PromoHeal(X, Scratch);
        if (X.State->Simulation)
            X.State->Publish();
        Cinematic.Focus = Scratch.Focus;
        Cinematic.Distance = Scratch.Distance;
        Cinematic.Pitch = Scratch.Pitch;
        Cinematic.Yaw = Scratch.Yaw;
        if (Frame >= 0 && Frame % Stride == 0)
            FScreenshotRequest::RequestScreenshot(Directory / FString::Printf(TEXT("%04d.png"), Frame), false, false);
        ++Frame;
        return false;
    }

private:
    static constexpr int32 Pending = MIN_int32;
    FAutomationTestBase* Test;
    TArray<FPromoShot> Shots;
    FPromoState Scratch;
    FString Directory;
    TArray<UGWFxDirector::FNote> SoundNotes;
    TArray<FString> Sheet;

    // One line per note on a plate: where it happened on screen (0..1 of the frame, -1 when off it) and in the world.
    void NoteFrame(FPromoContext& X, const FPromoShot& Shot, int32 Plate)
    {
        if (Plate >= 0 && Plate < Shot.Frames)
        {
            FVector2D ViewSize(1920, 1080);
            if (GEngine && GEngine->GameViewport)
                GEngine->GameViewport->GetViewportSize(ViewSize);
            auto Place = [&](const FVector& Spot)
            {
                FVector2D OnScreen;
                if (!X.Controller->ProjectWorldLocationToScreen(Spot, OnScreen, false) || ViewSize.X < 1 || ViewSize.Y < 1)
                    return FVector2D(-1, -1);
                return FVector2D(OnScreen.X / ViewSize.X, OnScreen.Y / ViewSize.Y);
            };
            for (const UGWFxDirector::FNote& Each : SoundNotes)
            {
                const FVector2D Start = Place(Each.From), End = Place(Each.To);
                Sheet.Add(FString::Printf(TEXT("%d,%s,%d,%d,%.4f,%.4f,%.4f,%.4f,%.2f,%.0f,%.0f"), Plate, *Each.What.ToString(), Each.A, Each.B,
                                          Start.X, Start.Y, End.X, End.Y, Each.Size, Each.From.X, Each.From.Y));
            }
        }
        SoundNotes.Reset();
    }
    int32 Index = -1, Frame = Pending, Waiting = 0, Stride = 1, OriginalSlot = 0;
    double ShotClock = 0, ShotGameTime = 0;
    bool Parked = false;

    bool Finish(FPromoContext& X)
    {
        UGWFxDirector::Notes = nullptr;
        FGWCinematic::Get() = FGWCinematic();
        FGWTransition::Get().ForceInTests = false;
        GWClock::Override() = -1;
        for (const FPromoActor& A : Scratch.Crew)
            if (A.State.IsValid())
                A.State->Destroy();
        if (Parked)
        {
            PromoParkProfiles(false);
            if (auto* Settings = UGWUserSettings::Get())
            {
                Settings->ProfileSlot = OriginalSlot;
                Settings->SaveSettings();
            }
            if (X.Instance)
                X.Instance->ReloadProfile();
        }
        if (X.Controller)
            X.Controller->SetActorTickEnabled(true);
        return true;
    }
};
} // namespace

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FGWPromoCapture, "GW.Promo.Capture", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
void FGWPromoCapture::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
    for (const TCHAR* Group : {TEXT("Defense"), TEXT("Coop"), TEXT("Hub"), TEXT("Brawl")})
    {
        OutBeautifiedNames.Add(Group);
        OutTestCommands.Add(Group);
    }
}
bool FGWPromoCapture::RunTest(const FString& Parameters)
{
    TArray<FPromoShot> Shots = Parameters == TEXT("Defense") ? PromoDefenseShots()
                               : Parameters == TEXT("Coop")  ? PromoCoopShots()
                               : Parameters == TEXT("Hub")   ? PromoHubShots()
                                                             : PromoBrawlShots();
    ADD_LATENT_AUTOMATION_COMMAND(FPromoCapture(this, MoveTemp(Shots)));
    return true;
}
#endif
