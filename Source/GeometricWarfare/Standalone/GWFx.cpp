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
    Slam(TEXT("Slam")), Launch(TEXT("Launch")), AmbientFx(TEXT("Ambient")), BuildFx(TEXT("Build")), Erase(TEXT("Erase")),
    Shatter(TEXT("Shatter")), Confetti(TEXT("Confetti")), Assemble(TEXT("Assemble")), InkSplash(TEXT("InkSplash")),
    TowerBuildFx(TEXT("TowerBuild")), TowerUp(TEXT("TowerUpgrade")), Thud(TEXT("Thud")), EvolveAura(TEXT("EvolveAura"));
// The floor under the cues this frame (the box's platform lifts everything that happens on it).
const gwgame::Maze* Floor = nullptr;
FVector Flat(FVector V, float Z)
{
    const float Up = Floor && Floor->isDeck(Floor->cell({V.X, V.Y})) ? static_cast<float>(gwgame::DeckHeight) : 0.f;
    return FVector(V.X, V.Y, Z + Up);
}
// Effects quality sets the one-shot budget per frame; simulation and gameplay never depend on it.
int32 QualityBudget()
{
    const int32 Quality = Scalability::GetQualityLevels().EffectsQuality;
    return Quality <= 0 ? 12 : Quality == 1 ? 22 : Quality == 2 ? 34 : 46;
}
} // namespace

TArray<UGWFxDirector::FNote>* UGWFxDirector::Notes = nullptr;

void UGWFxDirector::Initialize(AActor* InOwner)
{
    Owner = InOwner;
    for (const FName& Name : {Impact, Muzzle, Blast, Death, Spawn, Burn, FrostLoop, FrostNova, ArcFx, Pulse, LevelUp, Pickup, Heal,
                              Shield, Telegraph, Slam, Launch, AmbientFx, BuildFx, Erase, Shatter, Confetti, Assemble, InkSplash, TowerBuildFx,
                              TowerUp, Thud, EvolveAura})
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
        // The boss falls: a gold blast, the level-up construction and the whole box of confetti.
        Burst(Blast, Ground, Marigold, 2.4f, FRotator::ZeroRotator, 4000);
        Burst(LevelUp, Ground, Marigold, 1.6f, FRotator::ZeroRotator, 4000);
        Burst(Confetti, Ground, Marigold, 1.4f, FRotator::ZeroRotator, 4000);
        Kick(1);
        return;
    case 5:
        if (Style == EffectStyle::LaserTelegraph)
            return;
        Burst(Telegraph, Ground, Danger, E.Radius / 320.f, FRotator::ZeroRotator, 4000);
        return;
    case 6:
        Burst(Shatter, From, Kraft, 1.f);
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
            Burst(Assemble, Ground, Teal, .8f, FRotator::ZeroRotator, 2200);
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
            // A fresh stroke of ink settles into a card block (plates drop in on a sketched square); a repair
            // glints marigold.
            Burst(BuildFx, Ground, E.Radius > .5f ? Marigold : Kraft, 1.2f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::WallErase:
            Burst(Erase, Flat(E.From, 40), Kraft, 1.2f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::WallBreak:
            Burst(Shatter, Flat(E.From, 30), Kraft, 1.3f, FRotator::ZeroRotator, 9000);
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
            Burst(Death, Flat(E.To, 60), Teal, .9f, FRotator::ZeroRotator, 9000);
            Kick(.3f);
            break;
        case EffectStyle::InkPickup:
            Burst(InkSplash, Flat(E.To, 2), Hex(0x2B3A67), .7f);
            break;
        case EffectStyle::WaveStart:
            Burst(Pulse, Ground, Coral, 3, FRotator::ZeroRotator, 9000);
            Burst(Telegraph, Ground, Coral, .6f, FRotator::ZeroRotator, 9000);
            Kick(.4f);
            break;
        case EffectStyle::WaveClear:
            Burst(LevelUp, Ground, Teal, 2.2f, FRotator::ZeroRotator, 9000);
            Burst(Confetti, Ground, Teal, 1.3f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::Turret:
            Burst(BuildFx, Ground, Teal, 1, FRotator::ZeroRotator, 9000);
            Burst(Shield, Ground, Teal, 1, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::Respawn:
            Burst(Assemble, Ground, Coral, 1.1f, FRotator::ZeroRotator, 9000);
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
            Burst(InkSplash, Flat(E.To, 2), Hex(0x2B3A67), .5f, FRotator::ZeroRotator, 4000);
            break;
        // The hub's bullseye takes a throw; a tutorial step is done.
        case EffectStyle::Bullseye:
            Burst(LevelUp, Ground, Coral, 1.4f, FRotator::ZeroRotator, 6000);
            Burst(Confetti, Ground, Coral, 1.f, FRotator::ZeroRotator, 6000);
            break;
        case EffectStyle::StepDone:
            Burst(LevelUp, Ground, Teal, 1.6f, FRotator::ZeroRotator, 9000);
            Burst(Confetti, Ground, Teal, .9f, FRotator::ZeroRotator, 9000);
            break;
        // The box: ladders drawn and rubbed out, towers raised, revives, climbs, hops off the platform, rocket fire.
        case EffectStyle::LadderDraw:
            Arc(Flat(E.From, Lift), Flat(E.To, Lift), Kraft, 30);
            Burst(BuildFx, Flat(E.To, 2), Kraft, .9f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::Zap:
        {
            // A tesla spark: two kinked strokes of cobalt and spark yellow between the coil (or the last enemy) and the next.
            const FVector A = Flat(E.From, 70), B = Flat(E.To, 40), Mid = (A + B) * .5f + FVector(FMath::FRandRange(-40.f, 40.f), FMath::FRandRange(-40.f, 40.f), 20);
            Arc(A, Mid, Cobalt, 10);
            Arc(Mid, B, Cobalt, 10);
            Arc(A, B, Spark, 5);
            break;
        }
        case EffectStyle::Dash:
            // The dodge: a paper streak along the dash, speed lines and triangles trailing off where it began.
            Arc(Flat(E.From, 30), Flat(E.To, 30), Paper, 16);
            Burst(Launch, Flat(E.From, 2), Kraft, .8f, (E.To - E.From).Rotation(), 9000);
            break;
        case EffectStyle::LadderErase:
            Burst(Erase, Flat(E.To, 40), Kraft, 1.1f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::TowerBuild:
        {
            // A new tower: plates fly in and stack onto its post in its kind's colour (bolt marigold, mortar fire,
            // tesla cobalt) while it pops up like jelly. One more side: bands climb it and the top bursts in gold.
            FLinearColor Kind = Marigold;
            if (Playing)
                for (const FGWUnitView& U : Playing->Units)
                    if (U.Alive && U.Turret && FVector::DistSquared2D(U.Position, E.From) < 80 * 80)
                    {
                        Kind = U.Shape == static_cast<uint8>(gwgame::Shape::Circle) ? Fire : U.Shape == static_cast<uint8>(gwgame::Shape::Triangle) ? Cobalt : Marigold;
                        break;
                    }
            if (E.Radius >= 5)
                Burst(TowerUp, Flat(E.From, 2), Kind, 1 + (E.Radius - 5) * .15f, FRotator::ZeroRotator, 9000);
            else
                Burst(TowerBuildFx, Flat(E.From, 2), Kind, 1.f, FRotator::ZeroRotator, 9000);
            Kick(.12f);
            break;
        }
        case EffectStyle::Revive:
            Arc(Flat(E.From, Lift), Flat(E.To, Lift), Leaf, 30);
            Burst(Heal, Flat(E.To, Lift), Leaf, 1.6f, FRotator::ZeroRotator, 9000);
            Burst(Assemble, Flat(E.To, 2), Seat(FMath::RoundToInt(E.Radius)), 1.f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::Climb:
            Burst(Impact, Flat(E.From, 10), Kraft, .6f, FRotator::ZeroRotator, 3000);
            break;
        case EffectStyle::HopDown:
            Burst(Impact, Flat(E.To, 10), Plum, 1.f, FRotator::ZeroRotator, 3000);
            break;
        case EffectStyle::RocketLob:
            Burst(Launch, Flat(E.From, 60), Fire, 1 + E.Radius * .8f, (E.To - E.From).Rotation() + FRotator(35, 0, 0), 4000);
            Kick(.12f + .1f * E.Radius);
            break;
        // The expedition's moments: doors slamming and swinging open, cleared rooms, chests, the shelf, the portal,
        // the way out, a fall scattering a kit, a drawn revive, the eraser, items, pairs and the event machines.
        case EffectStyle::DoorShut:
            Burst(Thud, Ground, Coral, 1.3f, FRotator::ZeroRotator, 9000);
            Burst(Impact, Flat(E.From, 60), Coral, 1.f, FRotator::ZeroRotator, 9000);
            Kick(.3f);
            break;
        case EffectStyle::DoorOpen:
            Burst(Erase, Flat(E.From, 40), Teal, .9f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::RoomClear:
            Burst(LevelUp, Ground, Teal, E.Radius > 1.5f ? 3.f : 2.f, FRotator::ZeroRotator, 9000);
            Burst(Confetti, Ground, Teal, E.Radius > 1.5f ? 1.6f : 1.1f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::StickerPickup:
            Burst(Pickup, Flat(E.To, 2), Marigold, .55f, FRotator::ZeroRotator, 2500);
            break;
        case EffectStyle::ChestOpen:
            Burst(Confetti, Flat(E.From, 40), Marigold, 1.1f, FRotator::ZeroRotator, 9000);
            Burst(LevelUp, Ground, Marigold, 1.f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::Purchase:
            Arc(From, To, Marigold, 24);
            Burst(Pickup, Flat(E.To, 2), Marigold, 1.f, FRotator::ZeroRotator, 6000);
            break;
        // Coins swept up (a brass glint), and a sale at the shop's buyer (coins arcing from the counter to the seller).
        case EffectStyle::CoinPickup:
            Burst(Pickup, Flat(E.To, 2), GWPalette::CoinBrass, .5f, FRotator::ZeroRotator, 2500);
            break;
        case EffectStyle::HealPickup:
            // A heart taken, or the breather after a cleared room (2026-11-17): green pluses rising off the body.
            Burst(Heal, Flat(E.To, Lift), Leaf, 1.2f, FRotator::ZeroRotator, 4000);
            Burst(Pickup, Flat(E.To, 2), GWPalette::Danger, .5f, FRotator::ZeroRotator, 2500);
            break;
        case EffectStyle::Sale:
            Arc(From, To, GWPalette::CoinBrass, 24);
            Burst(Pickup, Flat(E.To, 2), GWPalette::CoinLight, 1.f, FRotator::ZeroRotator, 6000);
            Burst(Confetti, Flat(E.From, 60), GWPalette::CoinBrass, .6f, FRotator::ZeroRotator, 6000);
            break;
        // A staple or a tack sets an enemy bleeding (a red ink splash), and a chest's toss lands (a little paper puff).
        case EffectStyle::Bleed:
            Burst(InkSplash, Flat(E.From, 30), GWPalette::Danger, FMath::Clamp(E.Radius / 40.f, .45f, 1.2f), FRotator::ZeroRotator, 3000);
            break;
        case EffectStyle::LootLand:
            Burst(Pickup, Flat(E.From, 4), GWPalette::PaperDeep, .45f, FRotator::ZeroRotator, 2500);
            break;
        case EffectStyle::PortalOpen:
            Burst(Assemble, Ground, Cobalt, 2.f, FRotator::ZeroRotator, 9000);
            Burst(Pulse, Ground, Cobalt, 2.5f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::FloorStart:
            Burst(Pulse, Ground, Teal, 2.f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::Extract:
            Burst(LevelUp, Ground, Teal, 2.f, FRotator::ZeroRotator, 9000);
            Burst(Confetti, Ground, Marigold, 1.4f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::Scatter:
            Burst(Shatter, Flat(E.From, 30), Kraft, 1.4f, FRotator::ZeroRotator, 9000);
            Burst(Death, From, Pencil, 1.2f, FRotator::ZeroRotator, 9000);
            Kick(.3f);
            break;
        case EffectStyle::DrawRevive:
            Burst(Assemble, Flat(E.To, 2), Leaf, 1.4f, FRotator::ZeroRotator, 9000);
            Burst(Heal, Flat(E.To, Lift), Leaf, 1.4f, FRotator::ZeroRotator, 9000);
            break;
        case EffectStyle::EraseSwipe:
            Burst(Erase, Flat(E.To, 40), Paper, 1.4f, FRotator::ZeroRotator, 9000);
            Kick(.15f);
            break;
        case EffectStyle::ItemUse:
            Burst(Pulse, Ground, Marigold, .9f, FRotator::ZeroRotator, 6000);
            break;
        case EffectStyle::DualWield:
            Burst(LevelUp, Ground, Coral, 1.f, FRotator::ZeroRotator, 6000);
            break;
        case EffectStyle::PartErased:
            Burst(Shatter, Flat(E.From, 80), Kraft, 2.f, FRotator::ZeroRotator, 9000);
            Kick(.4f);
            break;
        case EffectStyle::LootPickup:
            Burst(Pickup, Flat(E.To, 2), Cobalt, .9f, FRotator::ZeroRotator, 4000);
            break;
        case EffectStyle::EventFire:
            Burst(Pulse, Ground, Magenta, 1.5f, FRotator::ZeroRotator, 9000);
            Burst(Confetti, Ground, Magenta, .8f, FRotator::ZeroRotator, 9000);
            break;
        // The bestiary's moments (FoeRules.inl; Subject says which): a stub's heal, a splash, a copy, a crescent sweep, a
        // crack or a jam, a click, a written wall, a cross cut, a gulp of ink, a cinnabar streak, an ink sac bursting.
        // Its warnings (FoeCue) are drawn on the floor by the world.
        case EffectStyle::FoeBurst:
            switch (E.Subject)
            {
            case 1:
                Arc(From, To, Leaf, 16);
                Burst(Heal, Flat(E.To, Lift), Leaf, .8f, FRotator::ZeroRotator, 4000);
                break;
            case 2:
                Burst(InkSplash, Ground, Frost, FMath::Clamp(E.Radius / 50.f, .6f, 2.f), FRotator::ZeroRotator, 4000);
                break;
            case 3:
                Burst(Assemble, Ground, Cobalt, 1.f, FRotator::ZeroRotator, 4000);
                break;
            case 4:
                Burst(Impact, Flat(E.To, 40), Ink, 1.7f, (E.To - E.From).Rotation(), 4000);
                Kick(.15f);
                break;
            case 5:
                Burst(Shatter, Flat(E.From, 40), Pencil, 1.2f, FRotator::ZeroRotator, 4000);
                break;
            case 6:
                Burst(Pulse, Ground, Cobalt, .8f, FRotator::ZeroRotator, 4000);
                break;
            case 7:
                Arc(From, To, Ink, 40);
                Burst(Erase, Ground, Ink, 1.2f, FRotator::ZeroRotator, 4000);
                break;
            case 8:
                Burst(Impact, Ground, Danger, 2.2f, FRotator::ZeroRotator, 6000);
                Kick(.25f);
                break;
            case 9:
                Burst(Shield, From, Night, 1.6f, FRotator::ZeroRotator, 6000);
                break;
            case 10:
                Burst(LevelUp, Ground, Danger, 1.f, FRotator::ZeroRotator, 4000);
                break;
            case 11:
                Burst(InkSplash, Ground, Night, 2.6f, FRotator::ZeroRotator, 6000);
                Kick(.3f);
                break;
            default:
                Burst(Pulse, Ground, Plum, 1.f, FRotator::ZeroRotator, 4000);
                break;
            }
            break;
        case EffectStyle::BossPhase:
            Burst(LevelUp, Ground, Magenta, 2.6f, FRotator::ZeroRotator, 9000);
            Burst(Confetti, Ground, Magenta, 1.3f, FRotator::ZeroRotator, 9000);
            Kick(.6f);
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
    Floor = &World.Geometry;
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
    Playing = &S;
    for (const FGWEffectView& E : S.Effects)
        if (E.Id > LastEffect)
        {
            if (Notes)
                Notes->Add({TEXT("fx"), E.Kind, E.Style, Flat(E.From, Lift), Flat(E.To, Lift), E.Radius});
            Effect(E);
            Newest = FMath::Max(Newest, E.Id);
        }
    Playing = nullptr;
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
        {
            if (Notes)
                Notes->Add({TEXT("die"), M.Kind, 0, Flat(M.Position, Lift), Flat(M.Position, Lift), M.Radius});
            Burst(Death, Flat(M.Position, Lift), M.Color, FMath::Clamp(M.Radius / 25.f, .8f, 4.f), FRotator::ZeroRotator,
                  M.Radius >= 100 ? 5000 : 2600);
            // Elites and the boss break into big slabs of their own colour as well.
            if (M.Radius >= 50)
                Burst(Shatter, Flat(M.Position, Lift), M.Color, FMath::Clamp(M.Radius / 60.f, 1.f, 2.4f), FRotator::ZeroRotator, 5000);
        }
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
        if (!M.Seen && Notes)
            Notes->Add({TEXT("shot"), P.Weapon, P.Team, Position, Position + FVector(P.Direction) * 100, 0});
        if (!M.Seen && P.Weapon < 6 && Flashes < 6 && Budget > 10)
            Flashes += Burst(Muzzle, Position, P.Team == 1 ? Marigold : Violet, .8f, FVector(P.Direction).Rotation(), 1800) != nullptr;
        M.Position = Position;
        M.Remaining = P.Remaining;
        M.Weapon = P.Weapon;
        M.Team = P.Team;
        M.Seen = true;
        M.RanOut = P.RanOut(.12f);
    }
    // A shot gone before its range ran out hit something: a burst there. One that ran out of range is not gone: the
    // world drops it to the floor (AGWWorld's spent shots).
    for (auto It = Shots.CreateIterator(); It; ++It)
        if (!Live.Contains(It.Key()))
        {
            const FShotMemory& M = It.Value();
            if (Notes)
                Notes->Add({TEXT("gone"), M.Weapon, M.Team + (M.RanOut ? 256 : 0), M.Position, M.Position, 0});
            if (!M.RanOut && M.Weapon < 5 && Budget > 4)
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

void UGWFxDirector::Showcase(FVector InFocus, float Delta)
{
    Now += Delta;
    Focus = InFocus;
    Budget = QualityBudget();
    Floor = nullptr;
    if (!Ambient && System(AmbientFx) && Owner)
    {
        Ambient = NewObject<UNiagaraComponent>(Owner);
        Ambient->SetAsset(System(AmbientFx));
        Ambient->SetAutoActivate(false);
        Ambient->RegisterComponent();
    }
    if (Ambient)
    {
        Ambient->SetWorldLocation(FVector(Focus.X, Focus.Y, 0));
        if (!Ambient->IsActive())
            Ambient->Activate(true);
    }
    // Warm-up: a Niagara system's first spawn hitches (its instance and its shaders' pipeline states are made then), so
    // the title page spawns each one once where nobody sees it (under the floor, which the camera always looks down
    // on), before the brawl calls for them.
    if (true || !Owner || WarmFrames > Systems.Num() + 30)
        return;
    if (Warmed < Systems.Num())
    {
        TArray<TObjectPtr<UNiagaraSystem>> All;
        Systems.GenerateValueArray(All);
        if (UNiagaraSystem* Template = All[Warmed].Get())
            if (auto* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(Owner->GetWorld(), Template, FVector(Focus.X, Focus.Y, -1500),
                                                                                 FRotator::ZeroRotator, FVector(.01f), false, true,
                                                                                 ENCPoolMethod::None, false))
                Warming.Add(Component);
        ++Warmed;
    }
    if (++WarmFrames > Systems.Num() + 30)
    {
        for (const auto& Component : Warming)
            if (Component)
                Component->DestroyComponent();
        Warming.Reset();
    }
}

void UGWFxDirector::ShowcaseStatus(const TArray<TPair<int32, FVector>>& Burning, const TArray<TPair<int32, FVector>>& Slowed,
                                   const TMap<int32, float>& Sizes)
{
    Loops(Burn, BurnLoops, BurnOwners, Burning, Sizes, 14);
    Loops(FrostLoop, FrostLoops, FrostOwners, Slowed, Sizes, 14);
}

void UGWFxDirector::Hold(int32 Slot, FName Name, bool On, FVector At, FLinearColor Color, float Scale)
{
    if (Slot < 0 || Slot > 15)
        return;
    if (Held.Num() <= Slot)
        Held.SetNum(Slot + 1);
    TObjectPtr<UNiagaraComponent>& Component = Held[Slot];
    if (!On)
    {
        if (Component && Component->IsActive())
            Component->Deactivate();
        return;
    }
    UNiagaraSystem* Template = System(Name);
    if (!Template || !Owner)
        return;
    if (!Component || Component->GetAsset() != Template)
    {
        if (Component)
            Component->DestroyComponent();
        Component = NewObject<UNiagaraComponent>(Owner);
        Component->SetAsset(Template);
        Component->SetAutoActivate(false);
        Component->RegisterComponent();
    }
    Component->SetWorldLocation(At);
    Component->SetWorldScale3D(FVector(Scale));
    Component->SetVariableLinearColor(ColorParam, Color);
    if (!Component->IsActive())
        Component->Activate(true);
}

void UGWFxDirector::Stop()
{
    for (const auto& Component : Held)
        if (Component)
            Component->Deactivate();
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
