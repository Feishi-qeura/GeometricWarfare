#include "GWGameState.h"
#include "GWMotion.h"
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
    return Elapsed + FMath::Clamp(GWClock::Seconds() - ElapsedArrival, 0.0, .25);
}
void AGWGameState::OnRep_Elapsed()
{
    ElapsedArrival = GWClock::Seconds();
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
    LootSlots.Init(FGWLootNet(), LootSlotCount);
    PublishedEffect = 0;
    Units.Reset();
    Projectiles.Reset();
    Effects.Reset();
    DamageNumbers.Reset();
    InkDrops.Reset();
    Loot.Reset();
}
int32 AGWGameState::RoomAt(FVector World) const
{
    if (CellSize <= 0)
        return -1;
    const int32 X = FMath::FloorToInt(World.X / CellSize), Y = FMath::FloorToInt(World.Y / CellSize);
    for (int32 I = 0; I < Rooms.Num(); ++I)
    {
        const FGWRoomNet& R = Rooms[I];
        if (X >= R.X && Y >= R.Y && X < R.X + R.W && Y < R.Y + R.H)
            return I;
    }
    return -1;
}
const FGWLootView* AGWGameState::LootNear(FVector World, float Reach) const
{
    const FGWLootView* Best = nullptr;
    float Nearest = Reach;
    for (const auto& L : Loot)
        if (L.Kind == static_cast<uint8>(gwgame::LootKind::Weapon) || L.Kind == static_cast<uint8>(gwgame::LootKind::Item) ||
            L.Kind == static_cast<uint8>(gwgame::LootKind::Look))
        {
            const float D = FVector::Dist2D(L.Position, World);
            if (D < Nearest)
            {
                Nearest = D;
                Best = &L;
            }
        }
    return Best;
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
    ElapsedArrival = GWClock::Seconds();
    SharedLevel = B.sharedLevel;
    XP = B.sharedXP;
    Prank = static_cast<uint8>(B.config.prank);
    BaitRadius = B.config.baitRadius;
    BaitSeconds = B.config.baitSeconds;
    RollDelay = B.config.rollDelay;
    TowerInk = B.config.towerInk;
    MortarInk = B.config.mortarInk;
    TeslaInk = B.config.teslaInk;
    WireInk = B.config.wireInk;
    TowerFiveInk = B.config.towerFiveInk;
    TowerSixInk = B.config.towerSixInk;
    LadderInk = B.config.ladderInk;
    ReviveInk = B.config.reviveInk;
    ReviveReach = B.config.reviveReach;
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
                   (U.dummy ? EGWStatus::Dummy : 0) | (U.deck ? EGWStatus::Deck : 0) | (U.climb >= 0 ? EGWStatus::Climbing : 0) |
                   (U.bleedTime > 0 ? EGWStatus::Bleeding : 0);
        N.Sides = static_cast<uint8>(U.sides);
        N.Flags = (U.alive ? EGWUnitFlag::Alive : 0) | (U.human ? EGWUnitFlag::Human : 0) | (U.bot ? EGWUnitFlag::Bot : 0) |
                  (U.turret ? EGWUnitFlag::Turret : 0) | (U.elite ? EGWUnitFlag::Elite : 0) | (U.carried ? EGWUnitFlag::Carried : 0) |
                  (U.shardDelay <= 0 ? EGWUnitFlag::Recruitable : 0) | (U.bait > 0 ? EGWUnitFlag::Bait : 0);
        N.Seat = U.seat >= 0 && U.seat < 255 ? static_cast<uint8>(U.seat) : 255;
        N.Breed = U.gadget >= 0 ? static_cast<uint8>(100 + U.gadget) : U.breed >= 0 && U.breed < 100 ? static_cast<uint8>(U.breed) : 255;
        N.Marks = (U.stun > 0 ? EGWMark::Stunned : 0) | (U.stamp > 0 ? EGWMark::Stamped : 0) | (U.gold > 0 ? EGWMark::Gilded : 0) |
                  (U.mark > 0 ? EGWMark::Highlighted : 0) | (U.wet > 0 ? EGWMark::Wet : 0) | (U.erased > 0 ? EGWMark::Erased : 0);
        if (U.kind == gwgame::UnitKind::Monster)
        {
            N.Act = static_cast<uint8>(FMath::Clamp(U.act, 0, 255));
            // A boss's tools; an eel's beads left (it is no boss: its parts byte carries them).
            N.Parts = U.breed == static_cast<int>(gwgame::Foe::Eel) ? static_cast<uint8>(FMath::Clamp(U.acts, 0, 5)) : U.parts;
            if (U.breed == static_cast<int>(gwgame::Foe::Rainbow) && U.act == static_cast<int>(gwgame::FoeAct::Split))
                N.Parts |= gwgame::PartsSplit;
        }
        else if (B.expeditionMode() && U.player())
        {
            const gwgame::Kit& K = U.kit;
            N.Act = (K.bleed > 0 ? EGWAfflict::Bleeding : 0) | (K.marked > 0 ? EGWAfflict::Circled : 0) | (K.sealed > 0 ? EGWAfflict::Sealed : 0) |
                    (K.smear > 0 ? EGWAfflict::Smeared : 0) | (K.drag < .99 ? EGWAfflict::Dragged : 0) | (K.streak > 0 ? EGWAfflict::Streak : 0) |
                    (K.fogged ? EGWAfflict::Fogged : 0) | (K.dryBrush ? EGWAfflict::DryBrush : 0);
            N.Parts = 0;
        }
        else
            N.Act = N.Parts = 0;
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
        P.DashRank = static_cast<uint8>(B.effectiveRankOf(U, gwgame::Attribute::DashCooldown));
        P.KickRank = static_cast<uint8>(B.effectiveRankOf(U, gwgame::Attribute::DashSpeed));
        P.Items = U.perks.itemsUsed;
        P.RocketCharge = U.rocketHeld ? static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(U.rocketCharge / gwgame::RocketChargeSeconds * 255), 0, 255)) : 0;
        P.Revives = U.stats.revives;
        P.Mending = U.mendOn;
        // The walking pace on top of research (the expedition's kit and floors, an ink mend's slow anywhere).
        P.Pace = static_cast<float>(B.moveScale(U) / gwgame::attributeScale(gwgame::Attribute::Speed, B.effectiveRankOf(U, gwgame::Attribute::Speed)));
        if (B.expeditionMode())
        {
            const gwgame::Kit& K = U.kit;
            for (const auto& Slot : K.slots)
                P.Hotbar.Add(GWNet::PackSlot(static_cast<uint8>(Slot.kind), Slot.what, Slot.count, Slot.variant));
            const int32 Open = B.packSlots(U);
            for (int32 Bag = 0; Bag < Open; ++Bag)
                P.Hotbar.Add(GWNet::PackSlot(static_cast<uint8>(K.pack[Bag].kind), K.pack[Bag].what, K.pack[Bag].count, K.pack[Bag].variant));
            P.PackSize = static_cast<uint8>(Open);
            P.Coins = K.coins;
            P.Selected = static_cast<uint8>(K.selected);
            P.Pouch = K.pouch;
            for (int32 W : K.wear)
                P.Wear.Add(W < 0 ? 255 : static_cast<uint8>(FMath::Clamp(W, 0, 254)));
            P.KitFlags = (K.ghost ? EGWKit::Ghost : 0) | (K.extracted ? EGWKit::Extracted : 0) | (K.finalExit ? EGWKit::FinalExit : 0) |
                         (K.note ? EGWKit::Note : 0) | (U.dual ? EGWKit::Dual : 0);
            P.Banked = K.banked;
            P.Trait = K.trait < 0 ? 255 : static_cast<uint8>(K.trait);
            P.Bonus = static_cast<uint8>(K.shape);
            const double HoldTime = K.holdTarget == -100 ? gwgame::ExitHoldSeconds : gwgame::ChainedHoldSeconds;
            P.Hold = K.holdTarget != -1 ? static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(K.hold / HoldTime * 255), 0, 255)) : 0;
            P.Clip = static_cast<int16>(B.clipSize(U));
            P.Shield = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(K.tape + K.graphite + K.blot), 0, 255));
            P.Pens = static_cast<uint8>(B.revivePens(U.id));
            for (const auto& Slot : K.slots)
                P.Nibs.Add(Slot.kind == gwgame::SlotKind::Weapon && Slot.nib >= 0 ? static_cast<uint8>(Slot.nib) : 255);
            P.Vote = K.vote >= 0 ? static_cast<uint8>(K.vote) : 255;
            P.Borrowed = static_cast<uint8>(K.borrowed & 255);
            P.Reforges = static_cast<uint8>(FMath::Clamp(K.reforges, 0, 255));
        }
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
        // Ladders are few and change rarely: the list travels whole when it changes.
        bool LaddersChanged = Ladders.Num() != static_cast<int32>(D.ladders.size());
        for (int32 I = 0; !LaddersChanged && I < Ladders.Num(); ++I)
            LaddersChanged = Ladders[I].Deck != D.ladders[I].deck || Ladders[I].Pit != D.ladders[I].pit;
        if (LaddersChanged)
        {
            Ladders.Reset();
            for (const auto& L : D.ladders)
            {
                FGWLadderNet N;
                N.Deck = static_cast<int16>(L.deck);
                N.Pit = static_cast<int16>(L.pit);
                Ladders.Add(N);
            }
        }
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
    else if (Ladders.Num())
        Ladders.Reset();
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
                // The high bit: fired from the platform and not bounced since (the client draws it from up there).
                N.Team = static_cast<uint8>(P.team | (P.fromDeck && P.bounces == 0 ? 0x80 : 0));
                N.Weapon = P.weapon;
                N.Tint = static_cast<uint8>(FMath::Clamp(P.tint, 0, 255));
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
            N.Subject = E.style == gwgame::EffectStyle::Denied || E.style == gwgame::EffectStyle::FoeCue || E.style == gwgame::EffectStyle::FoeBurst
                            ? static_cast<int32>(E.pulse)
                            : -1;
            PublishedEffect = FMath::Max(PublishedEffect, E.id);
        }
    const bool Grid = B.defenseMode() || B.hubMode() || B.expeditionMode();
    // Each expedition floor is a page of its own: its seed tells every client to build it afresh.
    if (B.expeditionMode())
        Seed = static_cast<uint32>(B.maze.seed);
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
            TSet<int32> Posts;
            for (const int Cell : B.defense.towerCells)
                Posts.Add(Cell);
            for (int32 I = 0; I < Cells.Num(); ++I)
            {
                uint8 Flags = B.maze.isFixed(I) ? EGWCell::Rock : B.maze.walls[I] > 0 ? EGWCell::Wall : 0;
                // A fixture (an expedition stand or machine) is rock with a footprint: Rock | Wall, its patch below. A
                // tower's post is one too (2026-11-17) but keeps travelling as Rock | Paper; its patch goes along.
                if (B.maze.isFixture(I) && !Posts.Contains(I))
                    Flags |= EGWCell::Wall;
                if (Flags == EGWCell::Wall)
                    Flags |= B.maze.kind(I) == gwgame::WallKind::Thick ? EGWCell::Thick : B.maze.kind(I) == gwgame::WallKind::Bounce ? EGWCell::Bounce : 0;
                if (B.maze.isDeck(I))
                    Flags |= EGWCell::Deck;
                if (I < static_cast<int32>(B.defense.buildable.size()) && B.defense.buildable[I])
                    Flags |= EGWCell::Paper;
                if (I == B.defense.coreCell)
                    Flags |= EGWCell::Core;
                Cells[I] = Flags;
            }
            for (int Gate : B.defense.entrances)
                if (Cells.IsValidIndex(Gate))
                    Cells[Gate] |= EGWCell::Entrance;
            // A raid gate is an entrance up on the platform (Deck | Entrance), never one of the tunnels.
            for (int Gate : B.defense.raids)
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
    if (B.defenseMode() && B.defense.wire.size() == B.maze.walls.size())
    {
        Wire.SetNumZeroed(static_cast<int32>(B.defense.wire.size()));
        for (int32 I = 0; I < Wire.Num(); ++I)
            Wire[I] = B.defense.wire[I] > 0 ? static_cast<uint8>(FMath::Clamp(FMath::CeilToInt(B.defense.wire[I] * 15), 1, 15)) : 0;
    }
    else if (Wire.Num())
        Wire.Reset();
    // The rub-out refunds, as the host pays them (Battle::eraseWall): a tower's share of what was spent on it by its
    // health left, wire what is left of it.
    if (B.defenseMode() && !B.maze.walls.empty())
    {
        const int32 Count = static_cast<int32>(B.maze.walls.size());
        TArray<uint16> Worth;
        Worth.SetNumZeroed(Count);
        for (const auto& U : B.units)
            if (U.alive && U.turret && U.kind == gwgame::UnitKind::Companion)
            {
                const int Cell = B.maze.cell(U.position);
                if (Cell >= 0 && Cell < Count && Cell < static_cast<int>(B.defense.towerPaid.size()))
                    Worth[Cell] = static_cast<uint16>(FMath::Clamp(
                        std::floor(B.defense.towerPaid[Cell] * std::clamp(U.hp / std::max(1., U.maxHp), 0., 1.)), 0., 65535.));
            }
        for (int32 I = 0; I < Count && I < static_cast<int32>(B.defense.wire.size()); ++I)
            if (B.defense.wire[I] > 0)
                Worth[I] = static_cast<uint16>(FMath::Clamp(std::floor(B.config.wireInk * B.defense.wire[I]), 0., 65535.));
        if (Worth != TowerWorth)
            TowerWorth = MoveTemp(Worth);
    }
    else if (TowerWorth.Num())
        TowerWorth.Reset();
    PublishExpedition();
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
void AGWGameState::PublishExpedition()
{
    using namespace GWNet;
    const auto& B = *Simulation;
    if (!B.expeditionMode())
    {
        if (ExpStage || Rooms.Num() || Offers.Num() || Chests.Num() || Exits.Num() || Zones.Num())
        {
            ExpStage = ExpFloor = ExpFloorsCleared = 0;
            Rooms.Reset();
            Offers.Reset();
            Chests.Reset();
            Exits.Reset();
            Zones.Reset();
            PortalOpen = false;
            ExpBoss = -1;
        }
        for (auto& Slot : LootSlots)
            Slot.Id = 0;
        return;
    }
    const auto& X = B.expedition;
    ExpStage = static_cast<uint8>(X.stage);
    ExpFloor = static_cast<uint8>(X.floor);
    ExpFloorsCleared = static_cast<uint8>(X.floorsCleared);
    ExpBiomes = 0;
    for (int32 S = 0; S < gwgame::ExpeditionStages; ++S)
        ExpBiomes = static_cast<uint16>(ExpBiomes | (static_cast<uint16>(X.biomes[S]) << (S * 3)));
    ExpParty = static_cast<uint8>(X.party);
    ExpCurrent = static_cast<uint8>(FMath::Max(0, X.current));
    // The floor's rooms (eight at most) travel whole; replication only sends what changed.
    TArray<FGWRoomNet> NewRooms;
    for (const auto& R : X.rooms)
    {
        FGWRoomNet N;
        N.X = static_cast<uint8>(R.x);
        N.Y = static_cast<uint8>(R.y);
        N.W = static_cast<uint8>(R.w);
        N.H = static_cast<uint8>(R.h);
        N.Kind = static_cast<uint8>(R.kind);
        N.State = static_cast<uint8>(R.state);
        N.Links = R.links;
        N.Event = R.event == gwgame::RoomEvent::Count ? 255 : static_cast<uint8>(R.event);
        N.Explored = R.explored;
        N.Wave = static_cast<uint8>(R.wave);
        N.Waves = static_cast<uint8>(R.waves);
        N.Clock = R.state == gwgame::RoomState::Armed           ? static_cast<float>(R.clock)
                  : R.event == gwgame::RoomEvent::Chained       ? static_cast<float>(FMath::Clamp(R.eventClock / gwgame::ChainedHoldSeconds, 0., 1.))
                  : R.event == gwgame::RoomEvent::Arena && R.eventState == 1 ? static_cast<float>(R.eventClock)
                                                                            : 0.f;
        for (int32 Door : R.doors)
            N.Doors.Add(static_cast<int16>(Door));
        NewRooms.Add(MoveTemp(N));
    }
    Rooms = MoveTemp(NewRooms);
    // Loot keeps a slot per id; a taken thing just empties its slot.
    if (LootSlots.Num() != LootSlotCount)
        LootSlots.Init(FGWLootNet(), LootSlotCount);
    TBitArray<> Live(false, LootSlotCount);
    for (const auto& L : X.loot)
    {
        if (L.amount < 0)
            continue;
        const int32 Slot = L.id % LootSlotCount;
        FGWLootNet& N = LootSlots[Slot];
        N.Id = L.id;
        N.Kind = static_cast<uint8>(L.kind);
        N.What = static_cast<uint16>(FMath::Clamp(L.what, 0, 65535));
        N.Amount = static_cast<int16>(FMath::Clamp(L.amount, 0, 32767));
        N.Owner = static_cast<int8>(FMath::Clamp(L.kind == gwgame::LootKind::Look ? L.owner : -1, -1, 127));
        N.X = PackCoord(L.position.x);
        N.Y = PackCoord(L.position.y);
        N.Variant = static_cast<int8>(FMath::Clamp(L.variant, -1, 127));
        const bool Up = L.airborne();
        N.Hop = Up ? static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(L.hop / 4), 1, 255)) : 0;
        N.Flight = Up ? static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(L.flight * 100), 1, 255)) : 0;
        Live[Slot] = true;
    }
    for (int32 Slot = 0; Slot < LootSlotCount; ++Slot)
        if (!Live[Slot] && LootSlots[Slot].Id != 0)
            LootSlots[Slot].Id = 0;
    TArray<FGWOfferNet> NewOffers;
    for (const auto& O : X.offers)
    {
        FGWOfferNet N;
        N.Room = static_cast<uint8>(FMath::Max(0, O.room));
        N.Kind = static_cast<uint8>(O.kind);
        N.What = static_cast<uint8>(FMath::Clamp(O.what, 0, 255));
        N.Price = static_cast<uint16>(FMath::Clamp(O.price, 0, 65535));
        N.X = PackCoord(O.position.x);
        N.Y = PackCoord(O.position.y);
        N.Sold = O.sold;
        N.Extra = static_cast<int8>(FMath::Clamp(O.extra, -1, 127));
        NewOffers.Add(N);
    }
    Offers = MoveTemp(NewOffers);
    TArray<FGWChestNet> NewChests;
    for (const auto& C : X.chests)
    {
        FGWChestNet N;
        N.Id = C.id;
        N.Room = static_cast<uint8>(FMath::Max(0, C.room));
        N.X = PackCoord(C.position.x);
        N.Y = PackCoord(C.position.y);
        N.Opened = C.opened;
        N.Grade = static_cast<uint8>(C.grade);
        N.Quality = static_cast<uint8>(FMath::Clamp(C.quality, 1, 5));
        NewChests.Add(N);
    }
    Chests = MoveTemp(NewChests);
    TArray<FGWExitNet> NewExits;
    for (const auto& E : X.exits)
    {
        FGWExitNet N;
        N.X = PackCoord(E.at.x);
        N.Y = PackCoord(E.at.y);
        N.Room = static_cast<uint8>(FMath::Max(0, E.room));
        N.Final = E.final;
        NewExits.Add(N);
    }
    Exits = MoveTemp(NewExits);
    TArray<FGWZoneNet> NewZones;
    for (const auto& Z : X.zones)
    {
        FGWZoneNet N;
        N.Kind = static_cast<uint8>(Z.kind);
        N.FromX = PackCoord(Z.from.x);
        N.FromY = PackCoord(Z.from.y);
        N.ToX = PackCoord(Z.to.x);
        N.ToY = PackCoord(Z.to.y);
        N.Radius = static_cast<float>(Z.radius);
        N.Remaining = static_cast<float>(FMath::Min(Z.remaining, 1e6));
        N.What = static_cast<uint8>(FMath::Clamp(Z.what, 0, 255));
        N.Life = static_cast<float>(FMath::Min(Z.life, 1e6));
        N.Charged = Z.kind == static_cast<int>(gwgame::ZoneKind::Puddle) && Z.charge > 0;
        NewZones.Add(N);
    }
    Zones = MoveTemp(NewZones);
    PortalAt = FVector(X.portal.x, X.portal.y, 0);
    PortalOpen = X.portalOpen;
    PortalWait = static_cast<float>(X.portalClock);
    ExpBoss = X.bossId;
    ExpKills = X.kills;
    ExpElites = X.eliteKills;
    ExpChests = X.chestsOpened;
    ExpFights = X.fightsCleared;
    ExpPrank = static_cast<int8>(FMath::Clamp(X.floorPrank, -1, 127));
    ExpVoteClock = static_cast<float>(X.voteClock);
}
void AGWGameState::DecodeUnits()
{
    using namespace GWNet;
    const double Now = GWClock::Seconds();
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
        V.Deck = (N.Status & EGWStatus::Deck) != 0;
        V.Climbing = (N.Status & EGWStatus::Climbing) != 0;
        V.Sides = N.Sides;
        V.Alive = (N.Flags & EGWUnitFlag::Alive) != 0;
        V.Human = (N.Flags & EGWUnitFlag::Human) != 0;
        V.Bot = (N.Flags & EGWUnitFlag::Bot) != 0;
        V.Turret = (N.Flags & EGWUnitFlag::Turret) != 0;
        V.Elite = (N.Flags & EGWUnitFlag::Elite) != 0;
        V.Carried = (N.Flags & EGWUnitFlag::Carried) != 0;
        V.Recruitable = (N.Flags & EGWUnitFlag::Recruitable) != 0;
        V.Seat = N.Seat == 255 ? -1 : N.Seat;
        V.Breed = N.Breed == 255 ? -1 : N.Breed;
        V.Marks = N.Marks;
        V.Act = N.Act;
        V.Parts = N.Parts;
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
                V.DashRank = P.DashRank;
                V.KickRank = P.KickRank;
                V.Items = P.Items;
                V.RocketCharge = P.RocketCharge / 255.f;
                V.Revives = P.Revives;
                V.MovementScale *= static_cast<float>(gwgame::attributeScale(gwgame::Attribute::Speed, P.SpeedRank)) * FMath::Max(.1f, P.Pace);
                V.PackSize = FMath::Clamp<int32>(P.PackSize, 0, 10);
                for (int32 S = 0; S < 5 + V.PackSize && S < P.Hotbar.Num(); ++S)
                {
                    FGWUnitView::FSlot& Slot = S < 5 ? V.Hotbar[S] : V.Pack[S - 5];
                    GWNet::UnpackSlot(P.Hotbar[S], Slot.Kind, Slot.What, Slot.Count, Slot.Variant);
                }
                V.Coins = P.Coins;
                V.Selected = P.Selected;
                V.Pouch = P.Pouch;
                for (int32 W = 0; W < 6; ++W)
                    V.Wear[W] = W < P.Wear.Num() && P.Wear[W] != 255 ? P.Wear[W] : -1;
                V.Ghost = (P.KitFlags & EGWKit::Ghost) != 0;
                V.Extracted = (P.KitFlags & EGWKit::Extracted) != 0;
                V.FinalExit = (P.KitFlags & EGWKit::FinalExit) != 0;
                V.Note = (P.KitFlags & EGWKit::Note) != 0;
                V.Dual = (P.KitFlags & EGWKit::Dual) != 0;
                V.Banked = P.Banked;
                V.Trait = P.Trait == 255 ? -1 : P.Trait;
                V.Bonus = P.Bonus;
                V.Hold = P.Hold / 255.f;
                V.Clip = P.Clip;
                V.Shield = P.Shield;
                V.Pens = P.Pens;
                for (int32 S = 0; S < 5; ++S)
                    V.Nibs[S] = S < P.Nibs.Num() && P.Nibs[S] != 255 ? P.Nibs[S] : -1;
                V.Vote = P.Vote == 255 ? -1 : P.Vote;
                V.Borrowed = P.Borrowed;
                V.Reforges = P.Reforges;
                V.Mending = P.Mending;
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
        V.Team = Shot.Team & 0x7F;
        V.Deck = (Shot.Team & 0x80) != 0;
        V.Weapon = Shot.Weapon;
        V.Velocity = FVector(Shot.VX, Shot.VY, 0);
        V.Direction = V.Velocity.GetSafeNormal();
        V.Origin = FVector(UnpackCoord(Shot.X), UnpackCoord(Shot.Y), 35);
        V.Position = V.Origin + V.Velocity * FMath::Max(0., Age);
        V.Remaining = End - Now;
        V.Age = static_cast<float>(FMath::Max(0., Age));
        V.Life = Shot.Life;
        V.Stopped = Shot.Ended > 0 && Shot.Ended < Shot.Born + Shot.Life - .02;
        V.Tint = Shot.Tint;
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
    Loot.Reset();
    for (const auto& L : LootSlots)
        if (L.Id != 0)
        {
            FGWLootView V;
            V.Id = L.Id;
            V.Kind = L.Kind;
            V.What = L.What;
            V.Amount = L.Amount;
            V.Owner = L.Owner;
            V.Variant = L.Variant;
            V.Position = FVector(UnpackCoord(L.X), UnpackCoord(L.Y), 0);
            V.Hop = L.Hop * 4.f;
            V.Flight = L.Flight / 100.f;
            Loot.Add(V);
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
        Grid[I] = Cells[I] & EGWCell::Deck ? 3 : Cells[I] & EGWCell::Rock ? 2 : Cells[I] & EGWCell::Wall ? 1 : 0;
        if ((Cells[I] & EGWCell::Entrance) && !(Cells[I] & EGWCell::Deck))
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
        if (Patch.Health.Num() == gwgame::Maze::FragmentCount && Patch.Cell >= 0 && Patch.Cell < Cells.Num() &&
            (Grid[Patch.Cell] == 1 || (Cells[Patch.Cell] & (EGWCell::Rock | EGWCell::Wall)) == (EGWCell::Rock | EGWCell::Wall) ||
             (Cells[Patch.Cell] & (EGWCell::Rock | EGWCell::Paper)) == (EGWCell::Rock | EGWCell::Paper)))
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
    DOREPLIFETIME(AGWGameState, Ladders);
    DOREPLIFETIME(AGWGameState, TowerInk);
    DOREPLIFETIME(AGWGameState, MortarInk);
    DOREPLIFETIME(AGWGameState, TeslaInk);
    DOREPLIFETIME(AGWGameState, WireInk);
    DOREPLIFETIME(AGWGameState, TowerFiveInk);
    DOREPLIFETIME(AGWGameState, TowerSixInk);
    DOREPLIFETIME(AGWGameState, LadderInk);
    DOREPLIFETIME(AGWGameState, ReviveInk);
    DOREPLIFETIME(AGWGameState, ReviveReach);
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
    DOREPLIFETIME(AGWGameState, ExpStage);
    DOREPLIFETIME(AGWGameState, ExpFloor);
    DOREPLIFETIME(AGWGameState, ExpFloorsCleared);
    DOREPLIFETIME(AGWGameState, ExpBiomes);
    DOREPLIFETIME(AGWGameState, ExpParty);
    DOREPLIFETIME(AGWGameState, ExpCurrent);
    DOREPLIFETIME(AGWGameState, Rooms);
    DOREPLIFETIME(AGWGameState, LootSlots);
    DOREPLIFETIME(AGWGameState, Offers);
    DOREPLIFETIME(AGWGameState, Chests);
    DOREPLIFETIME(AGWGameState, Exits);
    DOREPLIFETIME(AGWGameState, Zones);
    DOREPLIFETIME(AGWGameState, PortalAt);
    DOREPLIFETIME(AGWGameState, PortalOpen);
    DOREPLIFETIME(AGWGameState, PortalWait);
    DOREPLIFETIME(AGWGameState, ExpBoss);
    DOREPLIFETIME(AGWGameState, ExpKills);
    DOREPLIFETIME(AGWGameState, ExpElites);
    DOREPLIFETIME(AGWGameState, ExpChests);
    DOREPLIFETIME(AGWGameState, ExpFights);
    DOREPLIFETIME(AGWGameState, Twist);
    DOREPLIFETIME(AGWGameState, Wire);
    DOREPLIFETIME(AGWGameState, TowerWorth);
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
    DOREPLIFETIME(AGWGameState, ExpPrank);
    DOREPLIFETIME(AGWGameState, ExpVoteClock);
}
