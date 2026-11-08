#include "GWGameState.h"
#include "Net/UnrealNetwork.h"
AGWGameState::AGWGameState()
{
    PrimaryActorTick.bCanEverTick = true;
    bAlwaysRelevant = true;
    SetNetUpdateFrequency(20);
    SetMinNetUpdateFrequency(10);
}
const FGWUnitView* AGWGameState::FindUnit(int32 Id) const
{
    for (const auto& U : Units)
        if (U.Id == Id)
            return &U;
    return nullptr;
}
void AGWGameState::Tick(float Delta)
{
    Super::Tick(Delta);
    RefreshViews();
}
double AGWGameState::Clock() const
{
    if (HasAuthority() && Simulation)
        return Simulation->elapsed;
    if (!InMatch || Finished || UpgradePaused)
        return Elapsed;
    // Between snapshots the clock runs on; a stalled connection never runs it far ahead.
    return Elapsed + FMath::Clamp(FPlatformTime::Seconds() - ElapsedArrival, 0.0, .25);
}
void AGWGameState::OnRep_Elapsed()
{
    ElapsedArrival = FPlatformTime::Seconds();
}
void AGWGameState::OnRep_Units()
{
    DecodeUnits();
}
void AGWGameState::ResetRings()
{
    UnitSlots.Reset();
    PlayerSlots.Reset();
    ShotSlots.Init(FGWShotNet(), ShotSlotCount);
    EffectSlots.Init(FGWEffectNet(), EffectSlotCount);
    DamageSlots.Init(FGWDamageNet(), DamageSlotCount);
    InkSlots.Init(FGWInkDropNet(), InkSlotCount);
    PublishedEffect = 0;
    Units.Reset();
    Projectiles.Reset();
    Effects.Reset();
    DamageNumbers.Reset();
    InkDrops.Reset();
}
void AGWGameState::Publish()
{
    using namespace GWNet;
    if (!HasAuthority() || !Simulation)
        return;
    const auto& B = *Simulation;
    if (ShotSlots.Num() != ShotSlotCount)
        ResetRings();
    PlayerSpeed = B.config.playerSpeed;
    Duration = B.config.duration;
    Finished = B.finished;
    UpgradePaused = B.upgradePaused();
    Winner = B.winner;
    Elapsed = B.elapsed;
    ElapsedArrival = FPlatformTime::Seconds();
    SharedLevel = B.sharedLevel;
    XP = B.sharedXP;
    Prank = static_cast<uint8>(B.config.prank);
    BaitRadius = B.config.baitRadius;
    BaitSeconds = B.config.baitSeconds;
    RollDelay = B.config.rollDelay;
    // One wire slot per simulation slot: a unit that does not move or change costs nothing to send, and a
    // recycled slot is sent once as a new unit.
    UnitSlots.SetNum(B.units.size());
    PlayerSlots.Reset();
    for (int32 I = 0; I < UnitSlots.Num(); ++I)
    {
        const gwgame::Unit& U = B.units[I];
        FGWUnitNet& N = UnitSlots[I];
        N.Id = U.id;
        N.FollowerOf = U.followerOf;
        N.TailOrder = U.tailOrder;
        N.X = PackCoord(U.position.x);
        N.Y = PackCoord(U.position.y);
        N.Facing = PackAngle(std::atan2(U.aim.y, U.aim.x));
        N.Health = U.maxHp > 0 ? static_cast<uint16>(FMath::RoundToInt(FMath::Clamp(U.hp / U.maxHp, 0., 1.) * 65535)) : 0;
        N.MaxHP = U.maxHp;
        N.Radius = U.radius;
        N.Armor = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(U.armor), 0, 255));
        N.Team = U.team;
        N.Shape = static_cast<uint8>(U.shape);
        N.State = static_cast<uint8>(U.state);
        N.Kind = static_cast<uint8>(U.kind);
        N.Weapon = U.weapon;
        N.Status = (U.burnTime > 0 ? EGWStatus::Burning : 0) | (U.slowTime > 0 ? EGWStatus::Slowed : 0) |
                   (U.armor > .5 ? EGWStatus::Armored : 0) |
                   (U.player() && U.skills[17] > 0 && U.hp < U.maxHp ? EGWStatus::Regenerating : 0) |
                   (U.dummy ? EGWStatus::Dummy : 0);
        N.Sides = static_cast<uint8>(U.sides);
        N.Flags = (U.alive ? EGWUnitFlag::Alive : 0) | (U.human ? EGWUnitFlag::Human : 0) | (U.bot ? EGWUnitFlag::Bot : 0) |
                  (U.turret ? EGWUnitFlag::Turret : 0) | (U.elite ? EGWUnitFlag::Elite : 0) | (U.carried ? EGWUnitFlag::Carried : 0) |
                  (U.shardDelay <= 0 ? EGWUnitFlag::Recruitable : 0) | (U.bait > 0 ? EGWUnitFlag::Bait : 0);
        N.Seat = U.seat >= 0 && U.seat < 255 ? static_cast<uint8>(U.seat) : 255;
        if (U.kind != gwgame::UnitKind::Fighter || !U.player())
            continue;
        // Only a human's own client predicts and reads its own card; AI teammates send what everyone sees.
        FGWPlayerNet P;
        P.Unit = U.id;
        P.Respawn = U.respawn;
        P.Kills = U.kills;
        P.Held = U.stats.held;
        P.Thrown = U.stats.thrown;
        P.Throws = U.stats.throws;
        P.Walls = U.stats.walls;
        P.Pushes = U.stats.pushes;
        P.Damage = FMath::RoundToInt(U.stats.damage);
        P.InkGiven = FMath::RoundToInt(U.stats.inkGiven);
        P.BaitBonus = U.stats.baitBonus;
        P.Bullseyes = U.stats.bullseyes;
        P.Bait = U.bait;
        P.Guard = U.guard;
        P.Throwable = U.throwable;
        P.SpeedRank = static_cast<uint8>(B.effectiveRankOf(U, gwgame::Attribute::Speed));
        P.Items = U.perks.itemsUsed;
        if (U.human)
        {
            P.AckSequence = U.ackSequence;
            // Knockback is rounded off once it is too small to matter, so it stops changing.
            P.ImpulseX = std::abs(U.impulse.x) < 2 ? 0.f : static_cast<float>(U.impulse.x);
            P.ImpulseY = std::abs(U.impulse.y) < 2 ? 0.f : static_cast<float>(U.impulse.y);
            P.Reload = U.reload;
            P.Charging = U.charging;
            P.ChargeTime = U.chargeTime;
            P.Ammo = U.ammo;
            P.PendingUpgrades = U.pendingUpgrades;
            for (int Rank : U.skills)
                P.Skills.Add(static_cast<uint8>(FMath::Clamp(Rank, 0, 255)));
            for (int Choice : U.choices)
                P.Choices.Add(Choice);
            P.Ink = U.ink;
            P.MaxInk = U.maxInk;
        }
        PlayerSlots.Add(MoveTemp(P));
    }
    Rules = static_cast<uint8>(B.config.rules);
    Tutorial = B.tutorialMode();
    if (Tutorial)
    {
        const gwgame::TutorialProgress Progress = B.tutorialProgress();
        TutorialStep = static_cast<uint8>(B.tutorialStep());
        TutorialDone = static_cast<uint8>(FMath::Clamp(Progress.done, 0, 255));
        TutorialNeeded = static_cast<uint8>(FMath::Clamp(Progress.needed, 1, 255));
        TutorialWallX = static_cast<uint8>(B.tutorialWall()[0] % FMath::Max(1, B.maze.width));
    }
    HubBullseyes = B.hub.bullseyes;
    if (B.defenseMode())
    {
        const auto& D = B.defense;
        Phase = static_cast<uint8>(D.phase);
        Level = D.level;
        Wave = D.wave;
        WaveCount = D.waves;
        WavesCleared = D.wavesCleared;
        PhaseTime = D.phaseTime;
        CoreHP = D.coreHealth;
        CoreMaxHP = D.coreMaxHealth;
        CorePosition = FVector(D.core.x, D.core.y, 0);
        int32 Left = static_cast<int32>(D.queue.size() - D.queued);
        for (const auto& U : B.units)
            Left += gwgame::Battle::enemy(U);
        EnemiesLeft = Left;
        WallsLost = D.wallsLost;
        Stars = D.stars();
        // Ink drops keep a slot per id; a picked drop just empties its slot.
        TBitArray<> Live(false, InkSlotCount);
        for (const auto& Drop : D.drops)
        {
            const int32 Slot = Drop.id % InkSlotCount;
            FGWInkDropNet& N = InkSlots[Slot];
            if (N.Id != Drop.id)
            {
                N.Id = Drop.id;
                N.X = PackCoord(Drop.position.x);
                N.Y = PackCoord(Drop.position.y);
                N.Amount = Drop.amount;
            }
            Live[Slot] = true;
        }
        for (int32 Slot = 0; Slot < InkSlotCount; ++Slot)
            if (!Live[Slot] && InkSlots[Slot].Id != 0)
                InkSlots[Slot].Id = 0;
    }
    // Shots are sent when fired (or bounced) and flown by every client; an early stop is one number.
    {
        TBitArray<> Live(false, ShotSlotCount);
        for (const auto& P : B.projectiles)
        {
            if (P.remaining <= 0)
                continue;
            const int32 Slot = P.id % ShotSlotCount;
            FGWShotNet& N = ShotSlots[Slot];
            const int16 VX = PackSpeed(P.velocity.x), VY = PackSpeed(P.velocity.y);
            if (N.Id != P.id || N.VX != VX || N.VY != VY)
            {
                N.Id = P.id;
                N.Owner = P.owner;
                N.X = PackCoord(P.position.x);
                N.Y = PackCoord(P.position.y);
                N.VX = VX;
                N.VY = VY;
                N.Born = B.elapsed;
                N.Life = P.remaining;
                N.Ended = 0;
                N.Team = P.team;
                N.Weapon = P.weapon;
            }
            Live[Slot] = true;
        }
        for (int32 Slot = 0; Slot < ShotSlotCount; ++Slot)
        {
            FGWShotNet& N = ShotSlots[Slot];
            if (!Live[Slot] && N.Id > 0 && N.Ended <= 0 && N.Born + N.Life > B.elapsed + 1e-3)
                N.Ended = FMath::Max(static_cast<float>(B.elapsed), N.Born + 1e-3f);
        }
    }
    Crates.Reset();
    for (auto& C : B.crates)
        if (C.active)
        {
            FGWCrateView V;
            V.Position = FVector(C.position.x, C.position.y, 0);
            V.DropTime = C.dropTime;
            V.Weapon = C.weapon;
            Crates.Add(V);
        }
    // Cues are written once, into the slot of their id, with the time they started.
    for (const auto& E : B.effects)
        if (E.id > PublishedEffect)
        {
            FGWEffectNet& N = EffectSlots[E.id % EffectSlotCount];
            N.Id = E.id;
            N.FromX = PackCoord(E.from.x);
            N.FromY = PackCoord(E.from.y);
            N.ToX = PackCoord(E.to.x);
            N.ToY = PackCoord(E.to.y);
            N.Radius = E.radius;
            N.Born = B.elapsed;
            N.Life = E.remaining;
            N.Kind = E.kind;
            N.Team = E.team;
            N.Style = static_cast<uint8>(E.style);
            N.Subject = E.style == gwgame::EffectStyle::Denied ? static_cast<int32>(E.pulse) : -1;
            PublishedEffect = FMath::Max(PublishedEffect, E.id);
        }
    const bool Grid = B.defenseMode() || B.hubMode();
    if (WallVersion != B.maze.version || (Grid && Cells.Num() != static_cast<int32>(B.maze.walls.size())))
    {
        BrokenWalls.Reset();
        if (Grid)
        {
            // A grid page (defense, tutorial or hub) travels whole: every cell's wall, rock, paper, entrance and
            // core flags.
            GridWidth = B.maze.width;
            GridHeight = B.maze.height;
            CellSize = B.maze.cellSize;
            Cells.SetNumZeroed(B.maze.walls.size());
            for (int32 I = 0; I < Cells.Num(); ++I)
            {
                uint8 Flags = B.maze.isFixed(I) ? EGWCell::Rock : B.maze.walls[I] > 0 ? EGWCell::Wall : 0;
                if (Flags == EGWCell::Wall)
                    Flags |= B.maze.kind(I) == gwgame::WallKind::Thick ? EGWCell::Thick : B.maze.kind(I) == gwgame::WallKind::Bounce ? EGWCell::Bounce : 0;
                if (I < static_cast<int32>(B.defense.buildable.size()) && B.defense.buildable[I])
                    Flags |= EGWCell::Paper;
                if (I == B.defense.coreCell)
                    Flags |= EGWCell::Core;
                Cells[I] = Flags;
            }
            for (int Gate : B.defense.entrances)
                if (Cells.IsValidIndex(Gate))
                    Cells[Gate] |= EGWCell::Entrance;
        }
        else
        {
            Cells.Reset();
            gwgame::Maze Initial;
            Initial.generate(Seed, true);
            for (size_t I = 0; I < B.maze.walls.size(); ++I)
                if (Initial.walls[I] > 0 && B.maze.walls[I] <= 0)
                    BrokenWalls.Add(I);
        }
        WallVersion = B.maze.version;
        // Stable order keeps replication deltas small; a patch only changes when its own cell does.
        TArray<FGWWallPatchView> Patches;
        for (size_t I = 0; I < B.maze.walls.size(); ++I)
            if (auto Patch = B.maze.fragments.find(static_cast<int>(I)); Patch != B.maze.fragments.end())
            {
                FGWWallPatchView V;
                V.Cell = I;
                V.Health.Append(Patch->second.data(), Patch->second.size());
                Patches.Add(MoveTemp(V));
            }
        WallPatches = MoveTemp(Patches);
    }
    // Damage numbers keep the slot of their id; a burst that grows only changes its amount.
    for (const auto& N : B.damageNumbers)
    {
        FGWDamageNet& D = DamageSlots[N.id % DamageSlotCount];
        if (D.Id != N.id)
        {
            D.Id = N.id;
            D.X = PackCoord(N.position.x);
            D.Y = PackCoord(N.position.y);
            D.Born = B.elapsed - (.9 - N.remaining);
            D.Victim = N.victim;
            D.Team = N.team;
        }
        D.Amount = N.amount;
    }
    BossActive = false;
    for (const auto& U : B.units)
        if (gwgame::Battle::enemy(U) && U.radius >= 100)
        {
            BossActive = true;
            BossHP = U.hp / U.maxHp;
            break;
        }
    DecodeUnits();
    ViewFrame = 0;
    RefreshViews();
}
void AGWGameState::DecodeUnits()
{
    using namespace GWNet;
    const double Now = FPlatformTime::Seconds();
    TArray<FGWUnitView> Previous = MoveTemp(Units);
    Units.Reset();
    Units.SetNum(UnitSlots.Num());
    for (int32 I = 0; I < UnitSlots.Num(); ++I)
    {
        const FGWUnitNet& N = UnitSlots[I];
        FGWUnitView& V = Units[I];
        V.Id = N.Id;
        V.FollowerOf = N.FollowerOf;
        V.TailOrder = N.TailOrder;
        V.Position = FVector(UnpackCoord(N.X), UnpackCoord(N.Y), 0);
        V.Angle = N.Facing * (360.f / 256);
        V.MaxHP = N.MaxHP;
        V.HP = N.Health / 65535.f * N.MaxHP;
        V.Radius = N.Radius;
        V.Armor = N.Armor;
        V.Team = N.Team;
        V.Shape = N.Shape;
        V.State = N.State;
        V.Kind = N.Kind;
        V.Weapon = N.Weapon;
        V.Status = N.Status;
        V.MovementScale = (N.Status & EGWStatus::Slowed) != 0 ? .5f : 1.f;
        V.Dummy = (N.Status & EGWStatus::Dummy) != 0;
        V.Sides = N.Sides;
        V.Alive = (N.Flags & EGWUnitFlag::Alive) != 0;
        V.Human = (N.Flags & EGWUnitFlag::Human) != 0;
        V.Bot = (N.Flags & EGWUnitFlag::Bot) != 0;
        V.Turret = (N.Flags & EGWUnitFlag::Turret) != 0;
        V.Elite = (N.Flags & EGWUnitFlag::Elite) != 0;
        V.Carried = (N.Flags & EGWUnitFlag::Carried) != 0;
        V.Recruitable = (N.Flags & EGWUnitFlag::Recruitable) != 0;
        V.Seat = N.Seat == 255 ? -1 : N.Seat;
        if (Previous.IsValidIndex(I) && Previous[I].Id == V.Id)
        {
            V.HitAt = Previous[I].HitAt;
            if (V.Alive && Previous[I].Alive && V.HP < Previous[I].HP - .5f)
                V.HitAt = Now;
        }
    }
    for (const auto& P : PlayerSlots)
        for (auto& V : Units)
            if (V.Id == P.Unit)
            {
                V.AckSequence = P.AckSequence;
                V.Impulse = FVector(P.ImpulseX, P.ImpulseY, 0);
                V.Reload = P.Reload;
                V.Charging = P.Charging;
                V.ChargeTime = P.ChargeTime;
                V.Ammo = P.Ammo;
                V.PendingUpgrades = P.PendingUpgrades;
                V.Skills.Reset();
                for (uint8 Rank : P.Skills)
                    V.Skills.Add(Rank);
                V.Choices = P.Choices;
                V.Ink = P.Ink;
                V.MaxInk = P.MaxInk;
                V.Respawn = P.Respawn;
                V.Bait = P.Bait;
                V.Guard = P.Guard;
                V.Throwable = P.Throwable;
                V.Kills = P.Kills;
                V.Held = P.Held;
                V.Thrown = P.Thrown;
                V.Throws = P.Throws;
                V.Walls = P.Walls;
                V.Pushes = P.Pushes;
                V.Damage = P.Damage;
                V.InkGiven = P.InkGiven;
                V.BaitBonus = P.BaitBonus;
                V.Bullseyes = P.Bullseyes;
                // The owner's prediction walks at the speed rank the host counts in this room.
                V.SpeedRank = P.SpeedRank;
                V.Items = P.Items;
                V.MovementScale *= static_cast<float>(gwgame::attributeScale(gwgame::Attribute::Speed, P.SpeedRank));
                break;
            }
}
void AGWGameState::RefreshViews()
{
    using namespace GWNet;
    if (ViewFrame == GFrameCounter)
        return;
    ViewFrame = GFrameCounter;
    const double Now = Clock();
    Projectiles.Reset();
    for (const auto& Shot : ShotSlots)
    {
        if (Shot.Id <= 0)
            continue;
        const double Age = Now - Shot.Born, End = Shot.Ended > 0 ? Shot.Ended : Shot.Born + Shot.Life;
        if (Age < -.1 || Now >= End || Age > Shot.Life + .1)
            continue;
        FGWProjectileView V;
        V.Id = Shot.Id;
        V.Owner = Shot.Owner;
        V.Team = Shot.Team;
        V.Weapon = Shot.Weapon;
        V.Velocity = FVector(Shot.VX, Shot.VY, 0);
        V.Direction = V.Velocity.GetSafeNormal();
        V.Origin = FVector(UnpackCoord(Shot.X), UnpackCoord(Shot.Y), 35);
        V.Position = V.Origin + V.Velocity * FMath::Max(0., Age);
        V.Remaining = End - Now;
        Projectiles.Add(V);
    }
    Effects.Reset();
    for (const auto& E : EffectSlots)
    {
        const double Age = Now - E.Born, Remaining = E.Life - Age;
        if (E.Id == 0 || Age < -.1 || Remaining <= 0)
            continue;
        FGWEffectView V;
        V.Id = E.Id;
        V.From = FVector(UnpackCoord(E.FromX), UnpackCoord(E.FromY), 20);
        V.To = FVector(UnpackCoord(E.ToX), UnpackCoord(E.ToY), 20);
        V.Radius = E.Radius;
        V.Kind = E.Kind;
        V.Team = E.Team;
        V.Style = E.Style;
        V.Subject = E.Subject;
        V.Remaining = Remaining;
        Effects.Add(V);
    }
    Effects.Sort([](const FGWEffectView& A, const FGWEffectView& B) { return A.Id < B.Id; });
    DamageNumbers.Reset();
    for (const auto& D : DamageSlots)
    {
        const double Age = Now - D.Born, Remaining = .9 - Age;
        if (D.Id == 0 || Age < -.1 || Remaining <= 0)
            continue;
        FGWDamageNumberView V;
        V.Id = D.Id;
        V.Position = FVector(UnpackCoord(D.X), UnpackCoord(D.Y), 0);
        V.Amount = D.Amount;
        V.Remaining = Remaining;
        V.Victim = D.Victim;
        V.Team = D.Team;
        DamageNumbers.Add(V);
    }
    InkDrops.Reset();
    for (const auto& Drop : InkSlots)
        if (Drop.Id != 0)
        {
            FGWInkDropView V;
            V.Id = Drop.Id;
            V.Position = FVector(UnpackCoord(Drop.X), UnpackCoord(Drop.Y), 0);
            V.Amount = Drop.Amount;
            InkDrops.Add(V);
        }
}
bool AGWGameState::BuildGeometry(gwgame::Maze& Out, TArray<int32>& Entrances, int32& CoreCell) const
{
    if (!GridPage() || GridWidth <= 0 || GridHeight <= 0 || Cells.Num() != GridWidth * GridHeight)
        return false;
    std::vector<int> Grid(Cells.Num());
    Entrances.Reset();
    CoreCell = -1;
    for (int32 I = 0; I < Cells.Num(); ++I)
    {
        Grid[I] = Cells[I] & EGWCell::Rock ? 2 : Cells[I] & EGWCell::Wall ? 1 : 0;
        if (Cells[I] & EGWCell::Entrance)
            Entrances.Add(I);
        if (Cells[I] & EGWCell::Core)
            CoreCell = I;
    }
    Out.fromGrid(GridWidth, GridHeight, CellSize, Grid);
    for (int32 I = 0; I < Cells.Num(); ++I)
        Out.kinds[I] = static_cast<uint8>(Cells[I] & EGWCell::Thick    ? gwgame::WallKind::Thick
                                          : Cells[I] & EGWCell::Bounce ? gwgame::WallKind::Bounce
                                                                       : gwgame::WallKind::Plain);
    for (const auto& Patch : WallPatches)
        if (Patch.Health.Num() == gwgame::Maze::FragmentCount && Patch.Cell >= 0 && Patch.Cell < Cells.Num() && Grid[Patch.Cell] == 1)
        {
            gwgame::Maze::WallPatch Values;
            FMemory::Memcpy(Values.data(), Patch.Health.GetData(), Values.size());
            Out.applyPatch(Patch.Cell, Values);
        }
    Out.seed = Seed;
    Out.version = WallVersion;
    return true;
}
void AGWGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AGWGameState, Rules);
    DOREPLIFETIME(AGWGameState, Tutorial);
    DOREPLIFETIME(AGWGameState, TutorialStep);
    DOREPLIFETIME(AGWGameState, TutorialDone);
    DOREPLIFETIME(AGWGameState, TutorialNeeded);
    DOREPLIFETIME(AGWGameState, TutorialWallX);
    DOREPLIFETIME(AGWGameState, BoardLevel);
    DOREPLIFETIME(AGWGameState, Tier);
    DOREPLIFETIME(AGWGameState, MaxTier);
    DOREPLIFETIME(AGWGameState, DeployLevel);
    DOREPLIFETIME(AGWGameState, DeployAt);
    DOREPLIFETIME(AGWGameState, HubBullseyes);
    DOREPLIFETIME(AGWGameState, Phase);
    DOREPLIFETIME(AGWGameState, Level);
    DOREPLIFETIME(AGWGameState, Wave);
    DOREPLIFETIME(AGWGameState, WaveCount);
    DOREPLIFETIME(AGWGameState, WavesCleared);
    DOREPLIFETIME(AGWGameState, PhaseTime);
    DOREPLIFETIME(AGWGameState, CoreHP);
    DOREPLIFETIME(AGWGameState, CoreMaxHP);
    DOREPLIFETIME(AGWGameState, CorePosition);
    DOREPLIFETIME(AGWGameState, EnemiesLeft);
    DOREPLIFETIME(AGWGameState, WallsLost);
    DOREPLIFETIME(AGWGameState, Stars);
    DOREPLIFETIME(AGWGameState, StickersEarned);
    DOREPLIFETIME(AGWGameState, GridWidth);
    DOREPLIFETIME(AGWGameState, GridHeight);
    DOREPLIFETIME(AGWGameState, CellSize);
    DOREPLIFETIME(AGWGameState, Cells);
    DOREPLIFETIME(AGWGameState, InMatch);
    DOREPLIFETIME(AGWGameState, Finished);
    DOREPLIFETIME(AGWGameState, UpgradePaused);
    DOREPLIFETIME(AGWGameState, IsRoom);
    DOREPLIFETIME(AGWGameState, HostIdentity);
    DOREPLIFETIME(AGWGameState, Winner);
    DOREPLIFETIME(AGWGameState, SharedLevel);
    DOREPLIFETIME(AGWGameState, XP);
    DOREPLIFETIME(AGWGameState, Elapsed);
    DOREPLIFETIME(AGWGameState, Duration);
    DOREPLIFETIME(AGWGameState, PlayerSpeed);
    DOREPLIFETIME(AGWGameState, Seed);
    DOREPLIFETIME(AGWGameState, WallVersion);
    DOREPLIFETIME(AGWGameState, BossActive);
    DOREPLIFETIME(AGWGameState, BossHP);
    DOREPLIFETIME(AGWGameState, Prank);
    DOREPLIFETIME(AGWGameState, BaitRadius);
    DOREPLIFETIME(AGWGameState, BaitSeconds);
    DOREPLIFETIME(AGWGameState, RollDelay);
    DOREPLIFETIME(AGWGameState, Resuming);
    DOREPLIFETIME(AGWGameState, ResumeLevel);
    DOREPLIFETIME(AGWGameState, ResumeElapsed);
    DOREPLIFETIME(AGWGameState, ResumeSeats);
    DOREPLIFETIME(AGWGameState, UnitSlots);
    DOREPLIFETIME(AGWGameState, PlayerSlots);
    DOREPLIFETIME(AGWGameState, ShotSlots);
    DOREPLIFETIME(AGWGameState, EffectSlots);
    DOREPLIFETIME(AGWGameState, DamageSlots);
    DOREPLIFETIME(AGWGameState, InkSlots);
    DOREPLIFETIME(AGWGameState, Crates);
    DOREPLIFETIME(AGWGameState, BrokenWalls);
    DOREPLIFETIME(AGWGameState, WallPatches);
}
