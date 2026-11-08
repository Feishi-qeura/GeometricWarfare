#include "GWFx.h"
#include "Core/Types.h"
#include "GWGameState.h"
#include "GWPalette.h"
#include "GWSettings.h"
#include "GWWorld.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Scalability.h"

namespace
{
constexpr float Lift = 30;
const FName ColorParam(TEXT("User.Color")), BeamSizeParam(TEXT("User.BeamSize")), BeamDirParam(TEXT("User.BeamDir"));
const FName Impact(TEXT("Impact")), Muzzle(TEXT("Muzzle")), Blast(TEXT("Blast")), Death(TEXT("Death")), Spawn(TEXT("Spawn")),
    Burn(TEXT("Burn")), FrostLoop(TEXT("Frost")), FrostNova(TEXT("FrostNova")), ArcFx(TEXT("Arc")), Pulse(TEXT("Pulse")),
    LevelUp(TEXT("LevelUp")), Pickup(TEXT("Pickup")), Heal(TEXT("Heal")), Shield(TEXT("Shield")), Telegraph(TEXT("Telegraph")),
    Slam(TEXT("Slam")), Launch(TEXT("Launch")), AmbientFx(TEXT("Ambient"));
FVector Flat(FVector V, float Z)
{
    return FVector(V.X, V.Y, Z);
}
// Effects quality sets the one-shot budget per frame; simulation and gameplay never depend on it.
int32 QualityBudget()
{
    const int32 Quality = Scalability::GetQualityLevels().EffectsQuality;
    return Quality <= 0 ? 12 : Quality == 1 ? 22 : Quality == 2 ? 34 : 46;
}
} // namespace

void UGWFxDirector::Initialize(AActor* InOwner)
{
    Owner = InOwner;
    for (const FName& Name : {Impact, Muzzle, Blast, Death, Spawn, Burn, FrostLoop, FrostNova, ArcFx, Pulse, LevelUp, Pickup, Heal,
                              Shield, Telegraph, Slam, Launch, AmbientFx})
    {
        const FString Asset = TEXT("NS_GW_") + Name.ToString();
        if (auto* Loaded = LoadObject<UNiagaraSystem>(nullptr, *FString::Printf(TEXT("/Game/Sketch/VFX/%s.%s"), *Asset, *Asset)))
            Systems.Add(Name, Loaded);
    }
}

UNiagaraSystem* UGWFxDirector::System(FName Name)
{
    const auto* Found = Systems.Find(Name);
    return Found ? Found->Get() : nullptr;
}

FLinearColor UGWFxDirector::UnitColor(const FGWUnitView& U)
{
    using namespace GWPalette;
    if (U.Kind == 2)
        return U.Radius >= 100 ? Night : U.Elite || U.Radius >= 50 ? Magenta : U.Shape == 2 ? Hex(0x6B4A7A) : U.Shape == 3 ? Violet : Plum;
    // Players and AI teammates wear their seat colour (seat 0, the host, keeps the original coral).
    if (U.Human)
        return U.Seat >= 0 ? Seat(U.Seat) : Coral;
    if (U.Bot)
        return U.Seat >= 0 ? Seat(U.Seat) : Cobalt;
    if (U.Kind == 1)
    {
        // Allies brighten with their tier: loose triangles are mint, then teal, sea blue, leaf and marigold.
        if (U.Sides >= 6)
            return Marigold;
        if (U.Sides == 5)
            return Leaf;
        if (U.Sides == 4)
            return Hex(0x3C8DBC);
        return U.FollowerOf >= 0 || U.State == 4 ? Teal : Mint;
    }
    return Team(U.Team);
}

UNiagaraComponent* UGWFxDirector::Burst(FName Name, FVector Location, FLinearColor Color, float Scale, FRotator Rotation,
                                        float Range)
{
    UNiagaraSystem* Template = System(Name);
    if (!Template || !Owner || Budget <= 0 || FVector::DistSquared2D(Location, Focus) > Range * Range)
        return nullptr;
    UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        Owner, Template, Location, Rotation, FVector(Scale), true, false, ENCPoolMethod::AutoRelease, true);
    if (!Component)
        return nullptr;
    --Budget;
    ++Spawned;
    Component->SetVariableLinearColor(ColorParam, Color);
    Component->Activate(true);
    return Component;
}

void UGWFxDirector::Arc(FVector From, FVector To, FLinearColor Color, float Width)
{
    const FVector Delta = To - From;
    const float Length = Delta.Size();
    UNiagaraSystem* Template = System(ArcFx);
    if (Length < 12 || !Template || Budget <= 0 || FVector::DistSquared2D(From, Focus) > 2600.f * 2600.f)
        return;
    UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        Owner, Template, (From + To) * .5f, FRotator::ZeroRotator, FVector(1), true, false, ENCPoolMethod::AutoRelease, true);
    if (!Component)
        return;
    --Budget;
    ++Spawned;
    Component->SetVariableLinearColor(ColorParam, Color);
    Component->SetVariableVec2(BeamSizeParam, FVector2D(Width, Length + Width * .4f));
    Component->SetVariableVec3(BeamDirParam, Delta / Length);
    Component->Activate(true);
}

void UGWFxDirector::Effect(const FGWEffectView& E)
{
    using gwgame::EffectStyle;
    using namespace GWPalette;
    const auto Style = static_cast<EffectStyle>(E.Style);
    const FVector From = Flat(E.From, Lift), To = Flat(E.To, Lift), Ground = Flat(E.From, 2);
    auto Kick = [&](float Amount)
    {
        const float Falloff = 1 - FMath::Clamp(FVector::Dist2D(E.From, Focus) / 1800.f, 0.f, 1.f);
        Shake = FMath::Min(1.f, Shake + Amount * Falloff);
    };
    switch (E.Kind)
    {
    case 1:
    {
        if (Style == EffectStyle::BossSlam)
        {
            Burst(Slam, Ground, Danger, E.Radius / 320.f, FRotator::ZeroRotator, 4000);
            Kick(.9f);
            return;
        }
        if (Style == EffectStyle::Laser)
        {
            // The beam itself is drawn by the world; here it scorches the paper along its length.
            Arc(From, To, Danger, 120);
            for (int32 I = 1; I <= 5; ++I)
                Burst(Impact, FMath::Lerp(From, To, I / 6.f), Spark, 1.3f, FRotator::ZeroRotator, 5000);
            Kick(.8f);
            return;
        }
        const FLinearColor Color = Style == EffectStyle::Fragment     ? SkillFamily(0)
                                   : Style == EffectStyle::Combustion ? Fire
                                   : Style == EffectStyle::KillBlast  ? Violet
                                   : Style == EffectStyle::Rocket     ? Marigold
                                                                      : Team(E.Team);
        Burst(Blast, Ground, Color, FMath::Clamp(E.Radius / 150.f, .5f, 3.f));
        Kick(FMath::Clamp(E.Radius / 520.f, .1f, .6f));
        return;
    }
    case 2:
        if (Style == EffectStyle::FrostNova || FVector::DistSquared2D(E.From, E.To) < 1)
        {
            Burst(FrostNova, Ground, Frost, FMath::Max(.4f, E.Radius / 150.f));
            return;
        }
        Arc(From, To,
            Style == EffectStyle::Chain     ? Volt
            : Style == EffectStyle::Voltage ? Spark
            : Style == EffectStyle::Spread  ? Fire
                                            : Team(E.Team),
            Style == EffectStyle::Spread ? 30 : 44);
        if (Style == EffectStyle::Chain || Style == EffectStyle::Voltage)
            Burst(Impact, To, Style == EffectStyle::Chain ? Volt : Spark, .7f);
        return;
    case 3:
        Burst(Blast, Ground, Marigold, 2.4f, FRotator::ZeroRotator, 4000);
        Burst(LevelUp, Ground, Marigold, 1.6f, FRotator::ZeroRotator, 4000);
        Kick(1);
        return;
    case 5:
        if (Style == EffectStyle::LaserTelegraph)
            return;
        Burst(Telegraph, Ground, Danger, E.Radius / 320.f, FRotator::ZeroRotator, 4000);
        return;
    case 6:
        Burst(Death, From, Kraft, 1.4f);
        return;
    case gwgame::EventEffect:
        switch (Style)
        {
        case EffectStyle::Launch:
            Burst(Launch, Ground, Ink, 1 + E.Radius * .6f, (E.To - E.From).Rotation());
            break;
        case EffectStyle::Recruit:
            Arc(From, To, Teal, 30);
            Burst(Pickup, Flat(E.To, 2), Teal, .7f);
            break;
        case EffectStyle::PickupWeapon:
            Burst(Pickup, Ground, Cobalt, 1);
            break;
        case EffectStyle::SpawnAlly:
            Burst(Spawn, Ground, Teal, 1, FRotator::ZeroRotator, 2200);
            break;
        case EffectStyle::SpawnEnemy:
            Burst(Spawn, Ground, E.Radius >= 100 ? Danger : Plum, FMath::Clamp(E.Radius / 27.f, .8f, 4.f),
                  FRotator::ZeroRotator, E.Radius >= 100 ? 5000 : 2000);
            break;
        case EffectStyle::Shove:
            // A friendly bullet shoved a teammate: a small paper puff pointing along the push.
            Burst(Impact, From, Teal, .8f, (E.To - E.From).Rotation(), 2200);
            break;
        case EffectStyle::Voltage:
            Burst(Pulse, Ground, Spark, E.Radius / 210.f);
            break;
        case EffectStyle::Emergency:
            Burst(Heal, From, Leaf, 1.4f);
            Burst(Shield, From, Teal, 1.3f);
            break;
        case EffectStyle::Upgrade:
            Burst(Pickup, Ground, SkillFamily(FMath::RoundToInt(E.Radius)), 1.3f, FRotator::ZeroRotator, 4000);
            break;
        case EffectStyle::WallDraw:
            // A fresh stroke of ink settles into a card wall; a repair glints marigold.
            Burst(Spawn, Ground, E.Radius > .5f ? Marigold : Ink, 1.6f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::WallErase:
            Burst(Death, Ground, Kraft, 1.2f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::WallBreak:
            Burst(Death, Flat(E.From, 70), Kraft, 2.4f, FRotator::ZeroRotator, 9000);
            Kick(.35f);
            break;
        case EffectStyle::Shatter:
            Burst(Blast, Ground, Teal, .9f);
            Burst(Death, From, Mint, 1.2f);
            Kick(.25f);
            break;
        case EffectStyle::Merge:
            Burst(LevelUp, Ground, Marigold, .7f);
            Arc(Flat(E.To, Lift), From, Marigold, 26);
            break;
        case EffectStyle::CoreHit:
            Burst(Impact, Flat(E.To, 120), Danger, 1.6f, FRotator::ZeroRotator, 9000);
            Kick(.3f);
            break;
        case EffectStyle::InkPickup:
            Burst(Pickup, Flat(E.To, 2), Hex(0x2B3A67), .7f);
            break;
        case EffectStyle::WaveStart:
            Burst(Pulse, Ground, Coral, 3, FRotator::ZeroRotator, 9000);
            Kick(.4f);
            break;
        case EffectStyle::WaveClear:
            Burst(LevelUp, Ground, Teal, 2.2f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::Turret:
            Burst(Shield, Ground, Teal, 1, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::Respawn:
            Burst(Spawn, Ground, Coral, 1.3f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::Denied:
            Burst(Impact, Flat(E.From, 30), Danger, .8f, FRotator::ZeroRotator, 9000);
            break;
        // Throwing teammates: the radius of these cues carries the seat colour of the player involved.
        case EffectStyle::Grab:
            Arc(From, To, Seat(FMath::RoundToInt(E.Radius)), 34);
            Burst(Pickup, Flat(E.To, 2), Seat(FMath::RoundToInt(E.Radius)), 1.1f);
            break;
        case EffectStyle::Bait:
            // A thrown teammate slaps onto the paper: a ring as wide as the pull and a puff of paper.
            Burst(Pulse, Ground, Marigold, FMath::Max(.6f, E.Radius / 210.f), FRotator::ZeroRotator, 6000);
            Burst(Death, From, Kraft, 1.3f, FRotator::ZeroRotator, 6000);
            Kick(.35f);
            break;
        case EffectStyle::Roll:
            Burst(Launch, Ground, Seat(FMath::RoundToInt(E.Radius)), 1.1f, (E.To - E.From).Rotation(), 4000);
            break;
        case EffectStyle::Escape:
            Burst(Impact, From, Seat(FMath::RoundToInt(E.Radius)), 1.1f, (E.To - E.From).Rotation(), 4000);
            break;
        case EffectStyle::InkPour:
            Arc(Flat(E.From, Lift + 20), Flat(E.To, Lift), Hex(0x2B3A67), 18);
            break;
        // The hub's bullseye takes a throw; a tutorial step is done.
        case EffectStyle::Bullseye:
            Burst(LevelUp, Ground, Coral, 1.4f, FRotator::ZeroRotator, 6000);
            Burst(Pulse, Ground, Marigold, 1.f, FRotator::ZeroRotator, 6000);
            break;
        case EffectStyle::StepDone:
            Burst(LevelUp, Ground, Teal, 1.6f, FRotator::ZeroRotator, 9000);
            break;
        default:
            break;
        }
        return;
    default:
        return;
    }
}

void UGWFxDirector::Loops(FName Name, TArray<TObjectPtr<UNiagaraComponent>>& Pool, TArray<int32>& Owners,
                          const TArray<TPair<int32, FVector>>& Wanted, const TMap<int32, float>& Scales, int32 Limit,
                          FLinearColor Color)
{
    TMap<int32, FVector> Want;
    for (int32 I = 0; I < Wanted.Num() && I < Limit; ++I)
        Want.Add(Wanted[I].Key, Wanted[I].Value);
    for (int32 I = 0; I < Pool.Num(); ++I)
        if (Owners[I] >= 0 && !Want.Contains(Owners[I]))
        {
            if (Pool[I])
                Pool[I]->Deactivate();
            Owners[I] = -1;
        }
    UNiagaraSystem* Template = System(Name);
    if (!Template || !Owner)
        return;
    for (const auto& Pair : Want)
    {
        int32 Slot = Owners.Find(Pair.Key);
        if (Slot == INDEX_NONE)
        {
            Slot = Owners.Find(-1);
            if (Slot == INDEX_NONE)
            {
                auto* Component = NewObject<UNiagaraComponent>(Owner);
                Component->SetAsset(Template);
                Component->SetAutoActivate(false);
                Component->SetAutoDestroy(false);
                Component->RegisterComponent();
                Slot = Pool.Add(Component);
                Owners.Add(-1);
            }
            Owners[Slot] = Pair.Key;
            Pool[Slot]->SetWorldLocation(Pair.Value);
            Pool[Slot]->SetVariableLinearColor(ColorParam, Color);
            Pool[Slot]->Activate(true);
        }
        Pool[Slot]->SetWorldLocation(Pair.Value);
        Pool[Slot]->SetWorldScale3D(FVector(Scales.FindRef(Pair.Key)));
    }
}

void UGWFxDirector::Update(const AGWGameState& S, const AGWWorld& World, FVector InFocus, float Delta)
{
    using namespace GWPalette;
    Now += Delta;
    Focus = InFocus;
    Budget = QualityBudget();
    if (Seed != S.Seed)
    {
        Stop();
        Units.Reset();
        Shots.Reset();
        LastEffect = 0;
        LastLevel = S.SharedLevel;
        Seed = S.Seed;
    }
    if (!Ambient && System(AmbientFx) && Owner)
    {
        Ambient = NewObject<UNiagaraComponent>(Owner);
        Ambient->SetAsset(System(AmbientFx));
        Ambient->SetAutoActivate(false);
        Ambient->RegisterComponent();
    }
    if (Ambient)
    {
        Ambient->SetWorldLocation(Flat(Focus, 0));
        if (!Ambient->IsActive())
            Ambient->Activate(true);
    }
    uint32 Newest = LastEffect;
    for (const FGWEffectView& E : S.Effects)
        if (E.Id > LastEffect)
        {
            Effect(E);
            Newest = FMath::Max(Newest, E.Id);
        }
    LastEffect = Newest;

    TSet<int32> Present;
    TArray<TPair<int32, FVector>> Burning, Slowed;
    TMap<int32, float> Scales;
    for (const FGWUnitView& U : S.Units)
    {
        Present.Add(U.Id);
        FVector Position = U.Position;
        World.VisualPosition(U.Id, Position);
        FUnitMemory& M = Units.FindOrAdd(U.Id);
        const FLinearColor Color = UnitColor(U);
        if (M.Seen && M.Alive && !U.Alive)
            Burst(Death, Flat(M.Position, Lift), M.Color, FMath::Clamp(M.Radius / 25.f, .8f, 4.f), FRotator::ZeroRotator,
                  M.Radius >= 100 ? 5000 : 2600);
        if (U.Alive && M.Seen && M.Alive && U.Kind != 2)
        {
            if (M.Armor - U.Armor > 4 && Now - M.LastShield > .35)
            {
                Burst(Shield, Flat(Position, Lift), Frost, FMath::Max(.8f, U.Radius / 25.f));
                M.LastShield = Now;
            }
            if (U.HP - M.HP > 25 && Now - M.LastHeal > .5)
            {
                Burst(Heal, Flat(Position, Lift), Leaf, 1.2f);
                M.LastHeal = Now;
            }
            else if ((U.Status & EGWStatus::Regenerating) && Now - M.LastHeal > 1.6)
            {
                Burst(Heal, Flat(Position, Lift), Leaf, .7f);
                M.LastHeal = Now;
            }
        }
        M.Alive = U.Alive;
        M.HP = U.HP;
        M.Armor = U.Armor;
        M.Radius = U.Radius;
        M.Kind = U.Kind;
        M.Position = Position;
        M.Color = Color;
        M.Seen = true;
        if (!U.Alive || FVector::DistSquared2D(Position, Focus) > 2400.f * 2400.f)
            continue;
        Scales.Add(U.Id, FMath::Clamp(U.Radius / 25.f, .8f, 4.f));
        const FVector Base = Flat(Position, 0);
        if (U.Status & EGWStatus::Burning)
            Burning.Add({U.Id, Base});
        if (U.Status & EGWStatus::Slowed)
            Slowed.Add({U.Id, Base});
    }
    for (auto It = Units.CreateIterator(); It; ++It)
        if (!Present.Contains(It.Key()))
            It.RemoveCurrent();

    if (S.SharedLevel > LastLevel)
        for (const FGWUnitView& U : S.Units)
            if (U.Human && U.Alive)
            {
                FVector Position = U.Position;
                World.VisualPosition(U.Id, Position);
                Burst(LevelUp, Flat(Position, 2), Marigold, 1, FRotator::ZeroRotator, 4000);
            }
    LastLevel = S.SharedLevel;

    TSet<int32> Live;
    int32 Flashes = 0;
    for (const FGWProjectileView& P : S.Projectiles)
    {
        Live.Add(P.Id);
        FVector Position = P.Position;
        World.VisualProjectile(P.Id, Position);
        FShotMemory& M = Shots.FindOrAdd(P.Id);
        if (!M.Seen && P.Weapon < 6 && Flashes < 6 && Budget > 10)
            Flashes += Burst(Muzzle, Position, P.Team == 1 ? Marigold : Violet, .8f, FVector(P.Direction).Rotation(), 1800) != nullptr;
        M.Position = Position;
        M.Remaining = P.Remaining;
        M.Weapon = P.Weapon;
        M.Team = P.Team;
        M.Seen = true;
    }
    for (auto It = Shots.CreateIterator(); It; ++It)
        if (!Live.Contains(It.Key()))
        {
            const FShotMemory& M = It.Value();
            if (M.Remaining > .06f && M.Weapon < 5 && Budget > 4)
                Burst(Impact, M.Position, M.Team == 1 ? Spark : Violet, .9f, FRotator::ZeroRotator, 2000);
            It.RemoveCurrent();
        }

    auto Nearest = [&](TArray<TPair<int32, FVector>>& List)
    {
        List.Sort([&](const auto& A, const auto& B)
                  { return FVector::DistSquared2D(A.Value, Focus) < FVector::DistSquared2D(B.Value, Focus); });
    };
    Nearest(Burning);
    Nearest(Slowed);
    const int32 Quality = FMath::Clamp(Scalability::GetQualityLevels().EffectsQuality, 0, 3);
    Loops(Burn, BurnLoops, BurnOwners, Burning, Scales, 12 + Quality * 10);
    Loops(FrostLoop, FrostLoops, FrostOwners, Slowed, Scales, 12 + Quality * 10);
}

void UGWFxDirector::Stop()
{
    for (auto* Pool : {&BurnLoops, &FrostLoops})
        for (const auto& Component : *Pool)
            if (Component)
                Component->Deactivate();
    for (auto* Owners : {&BurnOwners, &FrostOwners})
        for (int32& Id : *Owners)
            Id = -1;
    if (Ambient)
        Ambient->Deactivate();
}

int32 UGWFxDirector::LoopCount() const
{
    int32 Count = 0;
    for (const auto* Owners : {&BurnOwners, &FrostOwners})
        for (int32 Id : *Owners)
            Count += Id >= 0;
    return Count;
}

FVector UGWFxDirector::CameraShake(float Delta)
{
    Shake = FMath::Max(0.f, Shake - Delta * 2.4f);
    if (const auto* Prefs = UGWUserSettings::Get(); Prefs && !Prefs->ScreenShake)
        return FVector::ZeroVector;
    const float Amplitude = Shake * Shake * 16;
    return FVector(FMath::PerlinNoise1D(Now * 23.1f) * Amplitude, FMath::PerlinNoise1D(Now * 19.7f + 7) * Amplitude, 0);
}
