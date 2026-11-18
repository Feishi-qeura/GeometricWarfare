// The title page's battle (design: 主菜单的大乱斗背景; third pass, the user: like a real war, units look for cover
// and openings, many more of them, no tower, and level-ups picked from cards over a unit's head like the expedition's).
// Not a recording: a small live simulation of the match's own pieces, played in its diorama with its cues. Eight
// players in their seat colours (some in hats, every gun in the game) hold the left of a page of cover blocks against
// a horde from the right: runners that charge, gunners that take cover and trade fire, brutes that slam, an elite now
// and then and a boss every minute. Everyone with a gun hides behind a block, peeks out round its corner to fire,
// sprints to the next cover when the line moves, moves on when flanked and falls back when hurt; the two fronts push
// and give on a one-minute tide. Every ten seconds or so a player levels up: three skill cards come out over its head
// (the HUD draws them, AGWHUD::ShowcaseCards), one is picked and breaks into shards that fly into it, and from then
// on it fights with that skill (split shots, chain sparks, fire, frost or a shield). A player arrives with a tail of
// triangles and launches them into a crowd; another throws a teammate whose landing flattens one. Shots that miss run
// out of range and drop to the page. The fight never stops, so there is no loop to see; its events come round once a
// minute. A jump to a moment replays the fight from a fixed seed in fixed steps, so tests see the same moment.
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GWFx.h"
#include "GWGameState.h"
#include "GWMotion.h"
#include "GWPalette.h"
#include "GWWorld.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
constexpr float ShowStep = 1.f / 60;
constexpr float ShowCycle = AGWWorld::ShowcaseLoop;
constexpr float ShowPage = 12000;
const FVector2D ShowMid(ShowPage * .5, ShowPage * .5);
// The battlefield's half extent round the page's middle (the horde comes in past its right edge).
constexpr double ShowEdgeX = 1450, ShowEdgeY = 800;
constexpr uint64 ShowSeed = 0x5EED6A11Eull;

struct FShowGun
{
    float Period;
    int32 Burst;
    float Speed, Range, Spread;
    int32 Pellets;
    float Damage;
};
// The players' six guns (pistol, rifle, shotgun, sniper rifle, machine gun, rocket launcher) and a gunner's violet
// bolt: seconds between the shots of a burst, shots in a burst, speed, range, spread (degrees), pellets, damage.
const FShowGun ShowGuns[7] = {{.3f, 3, 1500, 950, 2.5f, 1, 14}, {.13f, 5, 2000, 1300, 2, 1, 11}, {.6f, 2, 1300, 700, 12, 5, 8},
                              {1.f, 1, 3400, 2300, .4f, 1, 60}, {.08f, 9, 1700, 1050, 3.5f, 1, 6}, {1.f, 1, 0, 1400, 0, 1, 65},
                              {.36f, 3, 950, 1100, 4, 1, 8}};
constexpr int32 ShowRocket = 5, ShowBolt = 6;

// The minute's events, by seconds into it.
const float ShowPicks[] = {4, 14, 26, 37, 49};
constexpr float ShowTailIn = 8, ShowTailOff = 15.5f, ShowBossWarn = 22, ShowBossDrop = 23.5f, ShowEvolve = 31, ShowThrowAt = 41,
                ShowBossEnd = 56;
// A card pick: how long the cards are up, and when the shards reach the unit.
constexpr float ShowPickLength = 2.7f, ShowAbsorb = 2.25f;

float ShowRand(uint64& State)
{
    State ^= State << 13;
    State ^= State >> 7;
    State ^= State << 17;
    return static_cast<float>(static_cast<double>(State >> 40) / 16777216.0);
}
float ShowCycleOf(double Clock)
{
    const double Wrapped = FMath::Fmod(Clock, static_cast<double>(ShowCycle));
    return static_cast<float>(Wrapped < 0 ? Wrapped + ShowCycle : Wrapped);
}
// The tide of the fight round the minute: 1 while the players push, 0 while the horde does.
float ShowTide(float At)
{
    static const FVector2f Keys[] = {{0, .35f}, {16, .9f}, {23, .6f}, {33, .05f}, {45, .4f}, {54, .55f}, {60, .35f}};
    constexpr int32 Count = UE_ARRAY_COUNT(Keys);
    for (int32 I = 0; I + 1 < Count; ++I)
        if (At < Keys[I + 1].X)
        {
            const float U = (At - Keys[I].X) / (Keys[I + 1].X - Keys[I].X);
            return FMath::Lerp(Keys[I].Y, Keys[I + 1].Y, U * U * (3 - 2 * U));
        }
    return Keys[0].Y;
}
FLinearColor ShowElementColor(int32 Element)
{
    return Element < 0 ? GWPalette::Marigold : GWPalette::SkillFamily(Element * 4);
}
float ShowHeading(const FVector2D& Way)
{
    return FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(Way.Y, Way.X)));
}
float ShowTurn(float From, float To, float MaxStep)
{
    return From + FMath::Clamp(FMath::FindDeltaAngleDegrees(From, To), -MaxStep, MaxStep);
}
} // namespace

struct FGWShowBattle
{
    enum class EKind : uint8
    {
        Hero,
        Runner,
        Gunner,
        Brute,
        Boss,
        Mate
    };
    enum class EMode : uint8
    {
        Move,
        Cover,
        Peek,
        Back,
        Charge,
        Carried,
        Flying,
        Down,
        Drop,
        Dead
    };
    struct FBlock
    {
        FVector2D C = FVector2D::ZeroVector, H = FVector2D(50, 50);
        bool Rock = false;
        float Flash = 0;
    };
    // A place to take cover: beside one face of a block, out along its normal; whoever holds it peeks round either end
    // of that face.
    struct FSlot
    {
        int32 Block = -1, User = -1;
        FVector2D At = FVector2D::ZeroVector, Face = FVector2D::ZeroVector, Normal = FVector2D::ZeroVector, Tangent = FVector2D::ZeroVector;
        double Reach = 50;
    };
    struct FUnit
    {
        int32 Id = 0, SeatId = 0, Gun = 0, Hat = 0, Slot = -1, Target = -1, Corner = 1, Burst = 0, Element = -1, Skill = -1;
        int32 Kills = 0, Leader = -1, Link = 0, Hitter = -1;
        uint8 Side = 1, Shape = 0;
        EKind Kind = EKind::Hero;
        EMode Mode = EMode::Move;
        bool Alive = true, Elite = false;
        FVector2D At = FVector2D::ZeroVector, Vel = FVector2D::ZeroVector, Push = FVector2D::ZeroVector, PeekAt = FVector2D::ZeroVector;
        FVector2D FlyFrom = FVector2D::ZeroVector, FlyTo = FVector2D::ZeroVector;
        float R = 25, Hp = 100, MaxHp = 100, Z = 0, Yaw = 0, ModeTime = 0, Wait = 0, Shift = 5, Retarget = 0, FireIn = 0;
        float SlamIn = 2, Winding = -1, RingIn = 3, NovaIn = 4, Burning = 0, Slowed = 0, Guard = 0, GuardIn = 0, Flash = 0;
        float Respawn = 0, Busy = 0, Struck = 0, FlyAge = 0, FlyTime = 1, FlyArc = 300, DropHeight = 0, DropTime = .5f;
        double Absorbed = -100, Landed = -100;
        FLinearColor FlashColor = FLinearColor::White;
    };
    struct FShot
    {
        FVector2D At = FVector2D::ZeroVector, Vel = FVector2D::ZeroVector, From = FVector2D::ZeroVector, To = FVector2D::ZeroVector;
        float Range = 1000, Gone = 0, Damage = 10, Age = 0, Flight = 1, Radius = 0, Z = 35;
        int32 Shooter = -1, Element = -1;
        uint8 Side = 1, Weapon = 0;
        bool Lobbed = false;
    };
    struct FPick
    {
        int32 Unit = -1, Chosen = 0;
        int32 Skills[3] = {0, 0, 0};
        float Age = 0;
        bool Given = false;
    };
    // What a step asks of the presentation: cues, sparks between two points, shots that ran out of range, shake.
    struct FCueOut
    {
        FName Name;
        FVector At = FVector::ZeroVector;
        FLinearColor Color = FLinearColor::White;
        float Size = 1;
        bool Aimed = false;
        FRotator Turn = FRotator::ZeroRotator;
    };
    struct FZapOut
    {
        FVector From = FVector::ZeroVector, To = FVector::ZeroVector;
        FLinearColor Color = FLinearColor::White;
    };
    struct FSpentOut
    {
        FVector At = FVector::ZeroVector, Velocity = FVector::ZeroVector;
        uint8 Weapon = 0, Side = 1;
    };

    TArray<FBlock> Blocks;
    TArray<FSlot> Slots;
    TArray<FUnit> Units;
    TArray<FShot> Shots;
    TArray<FPick> Picks;
    TArray<FCueOut> CuesOut;
    TArray<FZapOut> ZapsOut;
    TArray<FSpentOut> SpentOut;
    float JoltOut = 0;
    // A jump to a moment plays the steps in between quietly (no cues).
    bool Quiet = false;
    uint64 Rng = 1;
    double Clock = 0;
    int32 NextId = 1, HeroDeaths = 0, MonsterDeaths = 0;
    float SpawnIn = 0;
    // The boss: where it is warned of and comes down, and its id while it is up.
    FVector2D BossSpot = FVector2D::ZeroVector;
    double BossWarned = -100;
    int32 BossId = -1;
    // The throw: who throws whom where, and how long since the pick-up (-1: none under way).
    int32 Thrower = -1, Thrown = -1;
    FVector2D ThrowTo = FVector2D::ZeroVector;
    float ThrowAge = -1;
    // The tail: its leader, and where and when its triangles were launched.
    int32 TailLeader = -1;
    FVector2D TailTo = FVector2D::ZeroVector;
    double TailLaunched = -100;
    // The camera: the point it looks at (round the page's middle), its distance, pitch and yaw, eased toward the action.
    FVector2D CamAt = FVector2D::ZeroVector;
    float CamDistance = 2100, CamPitch = 47, CamYaw = -90;

    // ---- Geometry ----
    // Where along segment A-B (0..1) it enters the box Center +- Half, if it does.
    static bool SegmentBox(const FVector2D& A, const FVector2D& B, const FVector2D& Center, const FVector2D& Half, double& T)
    {
        double Enter = 0, Leave = 1;
        auto Slab = [&](double P, double Dir, double Lo, double Hi)
        {
            if (FMath::Abs(Dir) < 1e-9)
                return P >= Lo && P <= Hi;
            double Ta = (Lo - P) / Dir, Tb = (Hi - P) / Dir;
            if (Ta > Tb)
                Swap(Ta, Tb);
            Enter = FMath::Max(Enter, Ta);
            Leave = FMath::Min(Leave, Tb);
            return Enter <= Leave;
        };
        if (!Slab(A.X, B.X - A.X, Center.X - Half.X, Center.X + Half.X) || !Slab(A.Y, B.Y - A.Y, Center.Y - Half.Y, Center.Y + Half.Y))
            return false;
        T = Enter;
        return true;
    }
    static bool SegmentCircle(const FVector2D& A, const FVector2D& B, const FVector2D& Center, double Radius, double& T)
    {
        const FVector2D D = B - A, F = A - Center;
        const double Qa = D.SizeSquared(), Qb = 2 * (F | D), Qc = F.SizeSquared() - Radius * Radius;
        if (Qc <= 0)
        {
            T = 0;
            return true;
        }
        const double Disc = Qb * Qb - 4 * Qa * Qc;
        if (Qa < 1e-9 || Disc < 0)
            return false;
        T = (-Qb - FMath::Sqrt(Disc)) / (2 * Qa);
        return T >= 0 && T <= 1;
    }
    int32 FirstBlock(const FVector2D& A, const FVector2D& B, double Pad, double& T) const
    {
        int32 Best = -1;
        T = 2;
        for (int32 I = 0; I < Blocks.Num(); ++I)
        {
            double Along;
            if (SegmentBox(A, B, Blocks[I].C, Blocks[I].H + FVector2D(Pad, Pad), Along) && Along < T)
            {
                T = Along;
                Best = I;
            }
        }
        return Best;
    }
    // Whether nothing stands between A and B.
    bool Clear(const FVector2D& A, const FVector2D& B) const
    {
        double T;
        for (const FBlock& K : Blocks)
            if (SegmentBox(A, B, K.C, K.H - FVector2D(4, 4), T))
                return false;
        return true;
    }
    // Where to head for Goal: straight there, or round the nearer corner of the first block in the way.
    FVector2D Waypoint(const FUnit& U, const FVector2D& Goal) const
    {
        double T;
        const int32 B = FirstBlock(U.At, Goal, U.R + 2, T);
        if (B < 0)
            return Goal;
        const FBlock& K = Blocks[B];
        const FVector2D E = K.H + FVector2D(U.R + 18, U.R + 18);
        FVector2D Best = Goal;
        double Cost = 1e18;
        for (const FVector2D& Sign : {FVector2D(-1, -1), FVector2D(1, -1), FVector2D(1, 1), FVector2D(-1, 1)})
        {
            const FVector2D P = K.C + FVector2D(Sign.X * E.X, Sign.Y * E.Y);
            double Hit;
            if (FirstBlock(U.At, P, U.R, Hit) >= 0 && Hit < .98)
                continue;
            const double Length = FVector2D::Distance(U.At, P) + FVector2D::Distance(P, Goal);
            if (Length < Cost)
            {
                Cost = Length;
                Best = P;
            }
        }
        return Best;
    }
    void BuildSlots()
    {
        Slots.Reset();
        for (int32 B = 0; B < Blocks.Num(); ++B)
        {
            const FBlock& K = Blocks[B];
            for (int32 F = 0; F < 4; ++F)
            {
                const FVector2D N = F == 0 ? FVector2D(-1, 0) : F == 1 ? FVector2D(1, 0) : F == 2 ? FVector2D(0, -1) : FVector2D(0, 1);
                const FVector2D Along(-N.Y, N.X);
                const double Half = FMath::Abs(Along.X) * K.H.X + FMath::Abs(Along.Y) * K.H.Y, Depth = FMath::Abs(N.X) * K.H.X + FMath::Abs(N.Y) * K.H.Y;
                const int32 Count = Half >= 90 ? 2 : 1;
                for (int32 I = 0; I < Count; ++I)
                {
                    FSlot& S = Slots.AddDefaulted_GetRef();
                    S.Block = B;
                    S.Normal = N;
                    S.Tangent = Along;
                    S.Reach = Half;
                    S.Face = K.C + N * Depth;
                    S.At = S.Face + N * 36 + Along * (Count == 2 ? (I ? .5 : -.5) * Half : 0.);
                }
            }
        }
    }
    // Where a unit in slot S stands to shoot round the end of its face (Corner +1 or -1).
    static FVector2D PeekPoint(const FSlot& S, int32 Corner, float R)
    {
        return S.Face + S.Normal * (R + 10) + S.Tangent * (Corner * (S.Reach + R + 12));
    }

    // ---- Units ----
    FUnit* Find(int32 Id)
    {
        for (FUnit& U : Units)
            if (U.Id == Id)
                return &U;
        return nullptr;
    }
    const FUnit* Find(int32 Id) const
    {
        for (const FUnit& U : Units)
            if (U.Id == Id)
                return &U;
        return nullptr;
    }
    static void SetMode(FUnit& U, EMode Mode)
    {
        U.Mode = Mode;
        U.ModeTime = 0;
    }
    static FLinearColor BodyColor(const FUnit& U)
    {
        using namespace GWPalette;
        switch (U.Kind)
        {
        case EKind::Hero:
            return Seat(U.SeatId);
        case EKind::Mate:
            return Teal;
        case EKind::Boss:
            return Night;
        default:
            return U.Elite ? Magenta : U.Shape == 2 ? Hex(0x6B4A7A) : U.Shape == 3 ? Violet : Plum;
        }
    }
    void Cue(const TCHAR* Name, const FVector2D& At, float Z, const FLinearColor& Color, float Size)
    {
        if (Quiet)
            return;
        FCueOut& Out = CuesOut.AddDefaulted_GetRef();
        Out.Name = FName(Name);
        Out.At = FVector(At.X, At.Y, Z);
        Out.Color = Color;
        Out.Size = Size;
    }
    void CueToward(const TCHAR* Name, const FVector2D& At, float Z, const FLinearColor& Color, float Size, const FVector2D& Way, float Up)
    {
        if (Quiet)
            return;
        FCueOut& Out = CuesOut.AddDefaulted_GetRef();
        Out.Name = FName(Name);
        Out.At = FVector(At.X, At.Y, Z);
        Out.Color = Color;
        Out.Size = Size;
        Out.Aimed = true;
        Out.Turn = FVector(Way.X, Way.Y, Up).Rotation();
    }
    void Shake(float Amount)
    {
        if (!Quiet)
            JoltOut += Amount;
    }
    FUnit& AddUnit(EKind Kind, uint8 Side, const FVector2D& At)
    {
        FUnit& U = Units.AddDefaulted_GetRef();
        U.Id = NextId++;
        U.Kind = Kind;
        U.Side = Side;
        U.At = At;
        U.Yaw = Side == 1 ? 0.f : 180.f;
        return U;
    }
    // A player's gun, hat and health, afresh each time it comes back.
    void Outfit(FUnit& U)
    {
        U.Gun = FMath::Min(5, static_cast<int32>(ShowRand(Rng) * 6));
        U.Hat = ShowRand(Rng) < .55f ? 1 + FMath::Min(6, static_cast<int32>(ShowRand(Rng) * 7)) : 0;
        U.R = 25;
        U.MaxHp = U.Hp = 110;
        U.Element = U.Skill = -1;
        U.Guard = U.Burning = U.Slowed = 0;
        U.Absorbed = -100;
        U.Shift = 4 + ShowRand(Rng) * 5;
    }
    void AddMonster(EKind Kind, const FVector2D& At, bool Deployed)
    {
        FUnit& U = AddUnit(Kind, 2, At);
        if (Kind == EKind::Runner)
        {
            U.Shape = ShowRand(Rng) < .5f ? 0 : 1;
            U.R = 22;
            U.MaxHp = 40;
        }
        else if (Kind == EKind::Gunner)
        {
            U.Shape = 3;
            U.R = 24;
            U.MaxHp = 55;
            U.Gun = ShowBolt;
        }
        else
        {
            U.Shape = 2;
            U.R = 34;
            U.MaxHp = 240;
        }
        if (Kind != EKind::Runner && ShowRand(Rng) < .1f)
        {
            U.Elite = true;
            U.R *= 1.45f;
            U.MaxHp *= 2.5f;
        }
        U.Hp = U.MaxHp;
        U.Shift = 4 + ShowRand(Rng) * 5;
        U.FireIn = ShowRand(Rng);
        if (Kind != EKind::Gunner)
        {
            SetMode(U, EMode::Charge);
            return;
        }
        ChooseSlot(U, false);
        if (Deployed && Slots.IsValidIndex(U.Slot))
        {
            U.At = Slots[U.Slot].At;
            SetMode(U, EMode::Cover);
            U.Wait = ShowRand(Rng);
        }
    }
    // Where each side's line stands now (x round the page's middle).
    float Front(uint8 Side) const
    {
        const float Tide = ShowTide(ShowCycleOf(Clock));
        return Side == 1 ? -650 + 450 * Tide : 330 + 330 * Tide;
    }
    void Release(FUnit& U)
    {
        if (Slots.IsValidIndex(U.Slot) && Slots[U.Slot].User == U.Id)
            Slots[U.Slot].User = -1;
        U.Slot = -1;
    }
    // The best free cover near the unit's line: hidden from as many of the other side's guns as can be, near the line
    // (further back when falling back), not too far to run.
    void ChooseSlot(FUnit& U, bool Fallback)
    {
        float Line = Front(U.Side);
        if (U.Kind == EKind::Hero && U.Gun == 2)
            Line += 180; // a shotgun wants to be close
        if (Fallback)
            Line += U.Side == 1 ? -380 : 380;
        TArray<FVector2D, TInlineAllocator<32>> Threats;
        for (const FUnit& E : Units)
            if (E.Alive && E.Side != U.Side && E.Z < 1 && (E.Kind == EKind::Hero || E.Kind == EKind::Gunner || E.Kind == EKind::Boss))
                Threats.Add(E.At);
        float Best = -1e9f;
        int32 Chosen = -1;
        for (int32 I = 0; I < Slots.Num(); ++I)
        {
            const FSlot& S = Slots[I];
            if (S.User >= 0 && S.User != U.Id)
                continue;
            const float X = static_cast<float>(S.At.X);
            if (U.Side == 1 ? X > Line + 300 : X < Line - 300)
                continue;
            int32 Hidden = 0;
            for (const FVector2D& T : Threats)
                Hidden += !Clear(T, S.At);
            const float Cover = Threats.Num() ? static_cast<float>(Hidden) / Threats.Num() : .5f;
            const float Score = 3 * Cover - FMath::Abs(X - Line) / 320 - static_cast<float>(FVector2D::Distance(U.At, S.At)) / 1400 +
                                ShowRand(Rng) * .6f;
            if (Score > Best)
            {
                Best = Score;
                Chosen = I;
            }
        }
        Release(U);
        if (Chosen >= 0)
        {
            U.Slot = Chosen;
            Slots[Chosen].User = U.Id;
        }
        SetMode(U, EMode::Move);
    }
    // The nearest of the other side within Reach (only those who charge, when Chargers).
    const FUnit* Closest(const FUnit& U, double Reach, bool Chargers) const
    {
        const FUnit* Best = nullptr;
        double Near = Reach * Reach;
        for (const FUnit& E : Units)
        {
            if (!E.Alive || E.Side == U.Side || E.Z > 10)
                continue;
            if (Chargers && E.Kind != EKind::Runner && E.Kind != EKind::Brute && E.Kind != EKind::Boss)
                continue;
            if (const double D = FVector2D::DistSquared(U.At, E.At); D < Near)
            {
                Near = D;
                Best = &E;
            }
        }
        return Best;
    }
    // Whether a gun on the other side can see the unit where it hides (it then moves on, now and then).
    bool Exposed(const FUnit& U)
    {
        for (const FUnit& E : Units)
            if (E.Alive && E.Side != U.Side && E.Z < 1 && (E.Kind == EKind::Hero || E.Kind == EKind::Gunner) &&
                FVector2D::DistSquared(E.At, U.At) < 1000.0 * 1000.0 && Clear(E.At, U.At))
                return ShowRand(Rng) < .4f;
        return false;
    }
    // Something to shoot round either end of the unit's cover (or from where it stands): the nearest, the boss and
    // whoever is out in the open before those behind cover.
    bool FindShot(FUnit& U)
    {
        const FShowGun& G = ShowGuns[U.Gun];
        const int32 First = ShowRand(Rng) < .5f ? 1 : -1;
        for (int32 Pass = 0; Pass < 3; ++Pass)
        {
            FVector2D From = U.At;
            if (Pass < 2)
            {
                if (!Slots.IsValidIndex(U.Slot))
                    continue;
                From = PeekPoint(Slots[U.Slot], Pass == 0 ? First : -First, U.R);
            }
            float Best = 1e18f;
            const FUnit* Pick = nullptr;
            for (const FUnit& E : Units)
            {
                if (!E.Alive || E.Side == U.Side || E.Z > 40 || E.Mode == EMode::Carried)
                    continue;
                const float D = static_cast<float>(FVector2D::Distance(From, E.At));
                if (D > G.Range * .92f || !Clear(From, E.At))
                    continue;
                const float Score = D * (E.Kind == EKind::Boss ? .5f : 1.f) * (E.Mode == EMode::Cover ? 1.3f : 1.f);
                if (Score < Best)
                {
                    Best = Score;
                    Pick = &E;
                }
            }
            if (Pick)
            {
                U.Target = Pick->Id;
                U.PeekAt = From;
                U.Corner = Pass == 0 ? First : -First;
                return true;
            }
        }
        return false;
    }
    void Engage(FUnit& U, const FUnit& T, const FVector2D& From)
    {
        U.Target = T.Id;
        U.PeekAt = From;
        U.Burst = ShowGuns[U.Gun].Burst;
        U.FireIn = .1f;
        SetMode(U, EMode::Peek);
    }
    void Shoot(FUnit& U, const FUnit& T)
    {
        const FShowGun& G = ShowGuns[U.Gun];
        const FVector2D Way = (T.At - U.At).GetSafeNormal();
        const FVector2D Mouth = U.At + Way * (U.R + 18);
        U.Yaw = ShowHeading(Way);
        const FLinearColor Trail = U.Side == 2 ? GWPalette::Violet : ShowElementColor(U.Element);
        if (U.Gun == ShowRocket)
        {
            FShot& S = Shots.AddDefaulted_GetRef();
            S.Lobbed = true;
            S.From = Mouth;
            S.To = T.At + FVector2D(ShowRand(Rng) - .5f, ShowRand(Rng) - .5f) * 120;
            S.Flight = .8f + static_cast<float>(FVector2D::Distance(Mouth, S.To)) / 2200;
            S.Damage = G.Damage;
            S.Radius = 180;
            S.Shooter = U.Id;
            S.Side = U.Side;
            S.Element = U.Element;
            S.Weapon = ShowRocket;
            CueToward(TEXT("Muzzle"), Mouth, 40, GWPalette::Fire, 1.f, Way, .5f);
            return;
        }
        // Split shots (the ballistics family) fan three rounds where a gun fires one.
        const int32 Count = U.Element == 0 && G.Pellets == 1 ? 3 : G.Pellets;
        for (int32 P = 0; P < Count; ++P)
        {
            const float Fan = G.Pellets > 1 ? G.Spread : 8.f;
            const float Off = (P - (Count - 1) * .5f) * (Count > 1 ? Fan : 0.f) +
                              (ShowRand(Rng) - .5f) * 2 * (G.Pellets > 1 ? G.Spread * .3f : G.Spread);
            FShot& S = Shots.AddDefaulted_GetRef();
            S.At = Mouth;
            S.Vel = Way.GetRotated(Off) * G.Speed;
            S.Range = G.Range;
            S.Damage = G.Damage * (U.Element == 0 ? .75f : 1.f);
            S.Shooter = U.Id;
            S.Side = U.Side;
            S.Weapon = static_cast<uint8>(U.Side == 2 ? 9 : U.Gun);
            S.Element = U.Element;
        }
        CueToward(TEXT("Muzzle"), Mouth, 35, Trail, U.Gun == 3 ? 1.1f : .75f, Way, 0);
    }

    // ---- Harm ----
    void Harm(FUnit& V, float Damage, const FVector2D& Way, float Knock, int32 By, bool Flashes)
    {
        if (!V.Alive)
            return;
        V.Hp -= Damage;
        V.Push += Way * Knock * (V.Kind == EKind::Boss ? .06f : V.Kind == EKind::Brute ? .4f : 1.f);
        if (Flashes)
        {
            V.Flash = 1;
            V.FlashColor = FLinearColor::White;
            V.Struck = .7f;
        }
        if (V.Hp <= 0)
            Slay(V, By);
    }
    void Slay(FUnit& V, int32 By)
    {
        if (!V.Alive)
            return;
        V.Alive = false;
        SetMode(V, EMode::Dead);
        Release(V);
        V.Burning = V.Slowed = V.Guard = 0;
        if (FUnit* Killer = Find(By); Killer && Killer != &V)
            ++Killer->Kills;
        const FLinearColor Body = BodyColor(V);
        const bool Big = V.Kind == EKind::Boss || V.Kind == EKind::Brute || V.Elite;
        Cue(TEXT("Death"), V.At, 30 + V.Z, Body, V.Kind == EKind::Boss ? 3.f : V.R / 22);
        if (Big)
            Cue(TEXT("Shatter"), V.At, 60, Body, V.Kind == EKind::Boss ? 2.4f : 1.2f);
        if (V.Kind == EKind::Boss)
        {
            Cue(TEXT("Confetti"), V.At, 140, GWPalette::Marigold, 1.4f);
            Shake(1);
            BossId = -1;
        }
        else if (Big)
            Shake(.25f);
        V.Z = 0;
        if (V.Kind == EKind::Hero)
        {
            V.Respawn = 3 + ShowRand(Rng);
            ++HeroDeaths;
        }
        else if (V.Side == 2)
            ++MonsterDeaths;
        Picks.RemoveAll([Id = V.Id](const FPick& P) { return P.Unit == Id; });
    }
    void AreaHit(const FVector2D& At, float Radius, float Damage, float Knock, int32 By, uint8 Side, int32 Element)
    {
        for (FUnit& V : Units)
            if (V.Alive && V.Side != Side && V.Z < 60 && V.Mode != EMode::Carried)
                if (const double D = FVector2D::Distance(V.At, At); D < Radius + V.R)
                {
                    const FVector2D Way = D > 1 ? (V.At - At) / D : FVector2D(1, 0);
                    if (Element == 2)
                        V.Burning = 3;
                    if (Element == 3)
                        V.Slowed = 2.4f;
                    Harm(V, Damage * (1 - .5f * static_cast<float>(D / (Radius + V.R))), Way, Knock, By, true);
                }
    }
    // A round meets a unit: a shield takes it, otherwise it hurts and its skill does the rest (fire catches, frost
    // slows, a spark jumps on to the next enemy).
    void Strike(const FShot& S, int32 Index, const FVector2D& P)
    {
        FUnit& V = Units[Index];
        if (V.Guard > 0)
        {
            Cue(TEXT("Impact"), P, 40, GWPalette::Teal, .8f);
            return;
        }
        Cue(TEXT("Impact"), P, 40, S.Side == 2 ? GWPalette::Violet : ShowElementColor(S.Element), S.Weapon == 3 ? 1.1f : .7f);
        if (S.Element == 2)
        {
            V.Burning = 3;
            V.Hitter = S.Shooter;
        }
        if (S.Element == 3)
            V.Slowed = 2.4f;
        const uint8 Victims = V.Side;
        Harm(V, S.Damage, S.Vel.GetSafeNormal(), S.Weapon == 3 ? 90.f : 30.f, S.Shooter, true);
        if (S.Element != 1)
            return;
        int32 Next = -1;
        double Near = 300.0 * 300.0;
        for (int32 J = 0; J < Units.Num(); ++J)
            if (J != Index && Units[J].Alive && Units[J].Side == Victims && Units[J].Z < 40)
                if (const double D = FVector2D::DistSquared(Units[J].At, P); D < Near)
                {
                    Near = D;
                    Next = J;
                }
        if (Next < 0)
            return;
        if (!Quiet)
        {
            FZapOut& Zap = ZapsOut.AddDefaulted_GetRef();
            Zap.From = FVector(P.X, P.Y, 45);
            Zap.To = FVector(Units[Next].At.X, Units[Next].At.Y, 40);
            Zap.Color = ShowElementColor(1);
        }
        Harm(Units[Next], S.Damage * .6f, (Units[Next].At - P).GetSafeNormal(), 20, S.Shooter, true);
    }
    void StepShots(float Dt)
    {
        for (int32 I = Shots.Num() - 1; I >= 0; --I)
        {
            FShot& S = Shots[I];
            if (S.Lobbed)
            {
                S.Age += Dt;
                if (S.Age >= S.Flight)
                {
                    const FShot Done = S;
                    Shots.RemoveAtSwap(I, EAllowShrinking::No);
                    AreaHit(Done.To, Done.Radius, Done.Damage, 140, Done.Shooter, Done.Side, Done.Element);
                    Cue(TEXT("Blast"), Done.To, 20, Done.Element >= 0 ? ShowElementColor(Done.Element) : GWPalette::Fire, 1.f);
                    Shake(.12f);
                }
                continue;
            }
            const FVector2D Next = S.At + S.Vel * Dt;
            double TB;
            const int32 Block = FirstBlock(S.At, Next, 0, TB);
            double TU = 2;
            int32 Hit = -1;
            for (int32 J = 0; J < Units.Num(); ++J)
            {
                const FUnit& V = Units[J];
                if (!V.Alive || V.Side == S.Side || V.Z > 60 || V.Mode == EMode::Carried)
                    continue;
                double T;
                if (SegmentCircle(S.At, Next, V.At, V.R + 5, T) && T < TU)
                {
                    TU = T;
                    Hit = J;
                }
            }
            if (Hit >= 0 && TU <= TB)
            {
                const FShot Done = S;
                Shots.RemoveAtSwap(I, EAllowShrinking::No);
                Strike(Done, Hit, Done.At + (Next - Done.At) * TU);
                continue;
            }
            if (Block >= 0)
            {
                // Chips off its cover.
                Blocks[Block].Flash = 1;
                Cue(TEXT("Impact"), S.At + (Next - S.At) * TB, 40, S.Side == 2 ? GWPalette::Violet : GWPalette::Kraft, .5f);
                Shots.RemoveAtSwap(I, EAllowShrinking::No);
                continue;
            }
            S.Gone += static_cast<float>((Next - S.At).Size());
            S.At = Next;
            if (S.Gone >= S.Range || FMath::Abs(S.At.X) > ShowEdgeX + 500 || FMath::Abs(S.At.Y) > ShowEdgeY + 500)
            {
                if (!Quiet)
                {
                    FSpentOut& Out = SpentOut.AddDefaulted_GetRef();
                    Out.At = FVector(S.At.X, S.At.Y, S.Z);
                    Out.Velocity = FVector(S.Vel.X, S.Vel.Y, 0);
                    Out.Weapon = S.Weapon;
                    Out.Side = S.Side;
                }
                Shots.RemoveAtSwap(I, EAllowShrinking::No);
            }
        }
    }

    // ---- Movement ----
    void Walk(FUnit& U, const FVector2D& Goal, float Speed, float Dt)
    {
        const FVector2D Way = Waypoint(U, Goal) - U.At;
        const double Length = Way.Size();
        const FVector2D Want = Length > 2 ? Way / Length * FMath::Min(static_cast<double>(Speed), Length / FMath::Max(Dt, 1e-3f)) : FVector2D::ZeroVector;
        U.Vel += (Want - U.Vel) * (1 - FMath::Exp(-14 * Dt));
        U.At += U.Vel * Dt;
        if (U.Vel.SizeSquared() > 400)
            U.Yaw = ShowTurn(U.Yaw, ShowHeading(U.Vel), Dt * 720);
    }
    // Bodies stay out of the blocks and apart (the boss shoulders everyone aside), and on the page.
    void Resolve()
    {
        auto Grounded = [](const FUnit& U) { return U.Alive && U.Z < 1 && U.Mode != EMode::Carried && U.Mode != EMode::Flying; };
        for (FUnit& U : Units)
        {
            if (!Grounded(U))
                continue;
            for (const FBlock& K : Blocks)
            {
                const FVector2D E = K.H + FVector2D(U.R, U.R), D = U.At - K.C;
                const double Ox = E.X - FMath::Abs(D.X), Oy = E.Y - FMath::Abs(D.Y);
                if (Ox <= 0 || Oy <= 0)
                    continue;
                if (Ox < Oy)
                    U.At.X += D.X < 0 ? -Ox : Ox;
                else
                    U.At.Y += D.Y < 0 ? -Oy : Oy;
            }
            U.At.X = FMath::Clamp(U.At.X, -ShowEdgeX - 140, ShowEdgeX + 140);
            U.At.Y = FMath::Clamp(U.At.Y, -ShowEdgeY, ShowEdgeY);
        }
        for (int32 I = 0; I < Units.Num(); ++I)
            for (int32 J = I + 1; J < Units.Num(); ++J)
            {
                FUnit& A = Units[I];
                FUnit& B = Units[J];
                if (!Grounded(A) || !Grounded(B))
                    continue;
                const FVector2D D = B.At - A.At;
                const double Gap = A.R + B.R + 4, Length = D.Size();
                if (Length >= Gap || Length < 1e-3)
                    continue;
                const FVector2D N = D / Length;
                const double Over = Gap - Length, Share = A.Kind == EKind::Boss ? 0. : B.Kind == EKind::Boss ? 1. : .5;
                A.At -= N * Over * Share;
                B.At += N * Over * (1 - Share);
            }
    }

    // ---- Minds ----
    // Players and gunners: cover, peek, shoot, back into cover; on to new cover when the line moves or the cover is
    // seen round; back when hurt.
    void Soldier(FUnit& U, float Dt)
    {
        const float Pace = (U.Kind == EKind::Hero ? 380.f : 300.f) * (U.Slowed > 0 ? .5f : 1.f);
        const FSlot* S = Slots.IsValidIndex(U.Slot) ? &Slots[U.Slot] : nullptr;
        switch (U.Mode)
        {
        case EMode::Move:
            if (!S)
            {
                ChooseSlot(U, false);
                return;
            }
            Walk(U, S->At, Pace * 1.25f, Dt);
            if (FVector2D::Distance(U.At, S->At) < 12)
            {
                SetMode(U, EMode::Cover);
                U.Wait = .25f + ShowRand(Rng) * .6f;
            }
            else if (U.ModeTime > 6)
                ChooseSlot(U, false);
            return;
        case EMode::Cover:
        {
            if (!S)
            {
                ChooseSlot(U, false);
                return;
            }
            Walk(U, S->At, Pace, Dt);
            U.Yaw = ShowTurn(U.Yaw, ShowHeading(-S->Normal), Dt * 360);
            U.Hp = FMath::Min(U.MaxHp, U.Hp + 3 * Dt);
            if (U.Busy > 0)
                return;
            // Something charging at it: stand and shoot.
            if (const FUnit* Near = Closest(U, 300, true); Near && Clear(U.At, Near->At))
            {
                Engage(U, *Near, U.At);
                return;
            }
            U.Wait -= Dt;
            if (U.Wait > 0)
                return;
            if (U.Hp < U.MaxHp * .35f && U.Struck > 0)
            {
                ChooseSlot(U, true);
                return;
            }
            if (U.Shift <= 0 || Exposed(U))
            {
                U.Shift = 5 + ShowRand(Rng) * 6;
                ChooseSlot(U, false);
                return;
            }
            if (FindShot(U))
            {
                SetMode(U, EMode::Peek);
                U.Burst = ShowGuns[U.Gun].Burst;
                U.FireIn = .16f;
            }
            else
                U.Wait = .3f;
            return;
        }
        case EMode::Peek:
        {
            Walk(U, U.PeekAt, Pace * 1.3f, Dt);
            const FUnit* T = Find(U.Target);
            const bool There = FVector2D::Distance(U.At, U.PeekAt) < 16;
            if (!T || !T->Alive || (There && !Clear(U.At, T->At)))
            {
                if (!(U.Burst > 0 && FindShot(U)))
                    SetMode(U, EMode::Back);
                return;
            }
            U.Yaw = ShowTurn(U.Yaw, ShowHeading(T->At - U.At), Dt * 900);
            if (There)
            {
                U.FireIn -= Dt;
                if (U.FireIn <= 0 && U.Burst > 0)
                {
                    Shoot(U, *T);
                    --U.Burst;
                    U.FireIn += ShowGuns[U.Gun].Period;
                }
                if (U.Burst <= 0 && U.FireIn <= 0)
                    SetMode(U, EMode::Back);
            }
            if (U.ModeTime > 3.5f || U.Busy > 0)
                SetMode(U, EMode::Back);
            return;
        }
        case EMode::Back:
            if (!S)
            {
                ChooseSlot(U, false);
                return;
            }
            Walk(U, S->At, Pace * 1.3f, Dt);
            if (FVector2D::Distance(U.At, S->At) < 12)
            {
                SetMode(U, EMode::Cover);
                U.Wait = .4f + ShowRand(Rng) * (U.Struck > 0 ? 1.6f : .9f);
            }
            return;
        default:
            SetMode(U, EMode::Move);
            return;
        }
    }
    // Runners bite whoever is nearest; brutes walk up and slam the ground round them.
    void Charger(FUnit& U, float Dt)
    {
        U.Retarget -= Dt;
        U.FireIn -= Dt;
        U.SlamIn -= Dt;
        FUnit* T = Find(U.Target);
        if (U.Retarget <= 0 || !T || !T->Alive || T->Z > 10)
        {
            U.Retarget = .5f;
            T = nullptr;
            double Best = 1e18;
            for (FUnit& E : Units)
                if (E.Alive && E.Side != U.Side && E.Z < 10)
                    if (const double D = FVector2D::DistSquared(U.At, E.At); D < Best)
                    {
                        Best = D;
                        T = &E;
                    }
            U.Target = T ? T->Id : -1;
        }
        if (U.Winding >= 0)
        {
            U.Winding += Dt;
            U.Vel *= FMath::Exp(-12 * Dt);
            if (U.Winding >= .7f)
            {
                U.Winding = -1;
                U.SlamIn = 2.6f;
                AreaHit(U.At, 160, 22, 170, U.Id, U.Side, -1);
                Cue(TEXT("Slam"), U.At, 2, GWPalette::Magenta, .7f);
                Shake(.25f);
            }
            return;
        }
        const float Pace = (U.Kind == EKind::Brute ? 190.f : 330.f) * (U.Slowed > 0 ? .45f : 1.f);
        if (!T)
        {
            Walk(U, FVector2D(Front(1), U.At.Y), Pace, Dt);
            return;
        }
        const double D = FVector2D::Distance(U.At, T->At);
        if (D > U.R + T->R + 8)
            Walk(U, T->At, Pace, Dt);
        else
            U.Vel *= FMath::Exp(-10 * Dt);
        U.Yaw = ShowTurn(U.Yaw, ShowHeading(T->At - U.At), Dt * 540);
        if (U.Kind == EKind::Brute)
        {
            if (D < 170 && U.SlamIn <= 0)
            {
                U.Winding = 0;
                Cue(TEXT("Telegraph"), U.At, 3, GWPalette::Danger, .55f);
            }
        }
        else if (D < U.R + T->R + 14 && U.FireIn <= 0)
        {
            U.FireIn = .75f;
            Cue(TEXT("Impact"), (U.At + T->At) * .5, 40, GWPalette::Magenta, .6f);
            Harm(*T, 9, (T->At - U.At).GetSafeNormal(), 80, U.Id, true);
        }
    }
    // The boss lumbers at the players' line, rings the page with bolts and slams whoever comes close.
    void BossMind(FUnit& U, float Dt)
    {
        U.SlamIn -= Dt;
        U.RingIn -= Dt;
        if (U.Winding >= 0)
        {
            U.Winding += Dt;
            if (U.Winding >= 1)
            {
                U.Winding = -1;
                U.SlamIn = 5.5f;
                AreaHit(U.At, 300, 26, 260, U.Id, U.Side, -1);
                Cue(TEXT("Slam"), U.At, 2, GWPalette::Danger, 1.6f);
                Cue(TEXT("Blast"), U.At, 20, GWPalette::Night, 1.2f);
                Shake(.6f);
            }
            return;
        }
        Walk(U, FVector2D(FMath::Max(static_cast<double>(Front(1) + 520), -100.0), BossSpot.Y), U.Slowed > 0 ? 45.f : 85.f, Dt);
        if (U.SlamIn <= 0 && Closest(U, 340, false))
        {
            U.Winding = 0;
            Cue(TEXT("Telegraph"), U.At, 3, GWPalette::Danger, 1.3f);
        }
        if (U.RingIn <= 0)
        {
            U.RingIn = 3.4f;
            const float Spin = ShowRand(Rng) * 30;
            for (int32 K = 0; K < 14; ++K)
            {
                const FVector2D Way = FVector2D(1, 0).GetRotated(Spin + K * 360.f / 14);
                FShot& S = Shots.AddDefaulted_GetRef();
                S.At = U.At + Way * (U.R + 10);
                S.Vel = Way * 720;
                S.Range = 1000;
                S.Damage = 9;
                S.Shooter = U.Id;
                S.Side = 2;
                S.Weapon = 9;
                S.Z = 60;
            }
            Cue(TEXT("Pulse"), U.At, 80, GWPalette::Violet, 1.3f);
            Shake(.15f);
        }
    }
    // A tail's triangle keeps a link's length behind the one ahead of it; without its leader the tail scatters.
    void MateMind(FUnit& U, float Dt)
    {
        const FUnit* Head = Find(U.Leader);
        if (!Head || !Head->Alive)
        {
            Slay(U, -1);
            return;
        }
        const FUnit* Ahead = Head;
        for (const FUnit& M : Units)
            if (M.Alive && M.Kind == EKind::Mate && M.Leader == U.Leader && M.Link == U.Link - 1 && M.Mode != EMode::Flying)
                Ahead = &M;
        const FVector2D Gap = Ahead->At - U.At;
        const double Length = Gap.Size();
        if (Length > 50)
            U.At += Gap / Length * FMath::Min(Length - 50, 560.0 * Dt);
        if (Length > 1)
            U.Yaw = ShowTurn(U.Yaw, ShowHeading(Gap), Dt * 540);
    }
    // A skill's own upkeep: frost bursts round its holder, sparks crackle, the shield comes back.
    void Skills(FUnit& U, float Dt)
    {
        if (U.Element < 0)
            return;
        U.NovaIn -= Dt;
        if (U.Element == 3 && U.NovaIn <= 0 && Closest(U, 280, false))
        {
            U.NovaIn = 4.5f;
            Cue(TEXT("FrostNova"), U.At, 20, ShowElementColor(3), .85f);
            for (FUnit& V : Units)
                if (V.Alive && V.Side != U.Side && V.Z < 10)
                    if (const double D = FVector2D::Distance(V.At, U.At); D < 300)
                    {
                        V.Slowed = 3;
                        Harm(V, 12, (V.At - U.At) / FMath::Max(D, 1.0), 70, U.Id, true);
                    }
        }
        else if (U.Element == 1 && U.NovaIn <= 0)
        {
            U.NovaIn = 2.8f;
            Cue(TEXT("Pulse"), U.At, 50, ShowElementColor(1), .45f);
        }
        if (U.Element == 4)
        {
            U.GuardIn -= Dt;
            if (U.GuardIn <= 0)
            {
                U.GuardIn = 9;
                U.Guard = 4.5f;
                Cue(TEXT("Shield"), U.At, 40, ShowElementColor(4), 1.f);
            }
        }
    }
    // Flying: a launched triangle or a thrown teammate along its arc, hitting the ground round where it lands.
    void Fly(FUnit& U, float Dt)
    {
        U.FlyAge += Dt;
        const float P = FMath::Min(1.f, U.FlyAge / U.FlyTime);
        U.At = FMath::Lerp(U.FlyFrom, U.FlyTo, static_cast<double>(P));
        U.Z = (U.Kind == EKind::Mate ? 30 : 74) * (1 - P) + U.FlyArc * 4 * P * (1 - P);
        if (P < 1)
            return;
        U.Z = 0;
        if (U.Kind == EKind::Mate)
        {
            AreaHit(U.At, 160, 55, 150, U.Leader, 1, -1);
            Cue(TEXT("Blast"), U.At, 20, GWPalette::Teal, .7f);
            Cue(TEXT("Death"), U.At, 30, GWPalette::Teal, .8f);
            Shake(.12f);
            U.Alive = false;
            SetMode(U, EMode::Dead);
            return;
        }
        AreaHit(U.At, 220, 70, 180, U.Id, 1, -1);
        Cue(TEXT("Slam"), U.At, 2, BodyColor(U), 1.f);
        Cue(TEXT("Blast"), U.At, 20, BodyColor(U), 1.f);
        Shake(.5f);
        SetMode(U, EMode::Down);
        U.Wait = 1.3f;
    }
    // Dropping in from above: a player back in the fight, the tail's triangles, the boss.
    void Fall(FUnit& U, float Dt)
    {
        U.FlyAge += Dt;
        const float P = FMath::Min(1.f, U.FlyAge / U.DropTime);
        U.Z = U.DropHeight * (1 - P * P);
        if (P < 1)
            return;
        U.Z = 0;
        U.Landed = Clock;
        if (U.Kind == EKind::Boss)
        {
            Cue(TEXT("Slam"), U.At, 2, GWPalette::Danger, 1.7f);
            Cue(TEXT("Blast"), U.At, 20, GWPalette::Night, 1.4f);
            AreaHit(U.At, 280, 20, 240, U.Id, 2, -1);
            Shake(.9f);
            SetMode(U, EMode::Charge);
            return;
        }
        Cue(TEXT("Spawn"), U.At, 2, BodyColor(U), U.Kind == EKind::Mate ? .7f : 1.f);
        if (U.Kind == EKind::Mate)
            SetMode(U, EMode::Charge);
        else
            ChooseSlot(U, false);
    }
    void Revive(FUnit& U)
    {
        Outfit(U);
        U.Alive = true;
        U.At = FVector2D(-ShowEdgeX + 40, (ShowRand(Rng) - .5f) * 2 * (ShowEdgeY - 150));
        U.Vel = U.Push = FVector2D::ZeroVector;
        U.DropHeight = 520;
        U.DropTime = .55f;
        U.FlyAge = 0;
        U.Z = U.DropHeight;
        SetMode(U, EMode::Drop);
    }
    void Think(FUnit& U, float Dt)
    {
        U.Flash = FMath::Max(0.f, U.Flash - Dt / .14f);
        U.Struck -= Dt;
        U.Shift -= Dt;
        U.Busy -= Dt;
        if (!U.Alive)
        {
            if (U.Kind == EKind::Hero)
            {
                U.Respawn -= Dt;
                if (U.Respawn <= 0)
                    Revive(U);
            }
            return;
        }
        U.Slowed -= Dt;
        U.Guard -= Dt;
        if (U.Burning > 0)
        {
            U.Burning -= Dt;
            Harm(U, 9 * Dt, FVector2D::ZeroVector, 0, U.Hitter, false);
            if (!U.Alive)
                return;
        }
        if (U.Mode == EMode::Drop)
        {
            Fall(U, Dt);
            return;
        }
        if (U.Mode == EMode::Flying)
        {
            Fly(U, Dt);
            return;
        }
        if (U.Mode == EMode::Carried)
            return;
        U.At += U.Push * Dt;
        U.Push *= FMath::Exp(-7 * Dt);
        U.ModeTime += Dt;
        if (U.Mode == EMode::Down)
        {
            U.Wait -= Dt;
            if (U.Wait <= 0)
                ChooseSlot(U, false);
            return;
        }
        Skills(U, Dt);
        switch (U.Kind)
        {
        case EKind::Hero:
        case EKind::Gunner:
            Soldier(U, Dt);
            break;
        case EKind::Runner:
        case EKind::Brute:
            Charger(U, Dt);
            break;
        case EKind::Boss:
            BossMind(U, Dt);
            break;
        case EKind::Mate:
            MateMind(U, Dt);
            break;
        }
    }

    // ---- The horde keeps coming ----
    void Spawning(float Dt)
    {
        SpawnIn -= Dt;
        if (SpawnIn > 0)
            return;
        const float At = ShowCycleOf(Clock);
        const bool Surge = At >= ShowBossWarn && At < 44;
        int32 Runners = 0, Gunners = 0, Brutes = 0;
        for (const FUnit& U : Units)
            if (U.Alive)
            {
                Runners += U.Kind == EKind::Runner;
                Gunners += U.Kind == EKind::Gunner;
                Brutes += U.Kind == EKind::Brute;
            }
        const float Need[3] = {(Surge ? 10.f : 7.f) - Runners, 8.f - Gunners, 2.f - Brutes};
        const float Weights[3] = {Need[0] / (Surge ? 10.f : 7.f), Need[1] / 8, Need[2] / 2};
        int32 Kind = 0;
        for (int32 K = 1; K < 3; ++K)
            if (Weights[K] > Weights[Kind])
                Kind = K;
        if (Need[Kind] <= 0)
        {
            SpawnIn = .3f;
            return;
        }
        AddMonster(Kind == 0 ? EKind::Runner : Kind == 1 ? EKind::Gunner : EKind::Brute,
                   FVector2D(ShowEdgeX + 80, (ShowRand(Rng) - .5f) * 2 * (ShowEdgeY - 120)), false);
        SpawnIn = .28f + ShowRand(Rng) * .35f;
    }

    // ---- The minute's events ----
    bool Ready(const FUnit& U) const
    {
        return U.Kind == EKind::Hero && U.Alive && U.Z < 1 && U.Busy <= 0 && U.Id != Thrower && U.Id != Thrown && U.Id != TailLeader &&
               (U.Mode == EMode::Cover || U.Mode == EMode::Move || U.Mode == EMode::Back || U.Mode == EMode::Peek) &&
               !Picks.ContainsByPredicate([&](const FPick& P) { return P.Unit == U.Id; });
    }
    // The thickest knot of the horde within Reach of From (the middle of it).
    FVector2D Crowd(const FVector2D& From, double Reach) const
    {
        FVector2D Best(Front(2), 0);
        int32 Most = 0;
        for (const FUnit& U : Units)
        {
            if (!U.Alive || U.Side != 2 || U.Z > 1 || U.Kind == EKind::Boss || FVector2D::Distance(U.At, From) > Reach)
                continue;
            int32 Count = 0;
            FVector2D Sum = FVector2D::ZeroVector;
            for (const FUnit& V : Units)
                if (V.Alive && V.Side == 2 && V.Z < 1 && FVector2D::Distance(V.At, U.At) < 200)
                {
                    ++Count;
                    Sum += V.At;
                }
            if (Count > Most)
            {
                Most = Count;
                Best = Sum / Count;
            }
        }
        return Best;
    }
    // A player levels up: three cards of different skill families over its head, one of them picked at random. The
    // player nearest the middle of the view gets it, one without a skill before one with.
    void StartPick()
    {
        FUnit* Best = nullptr;
        double Score = 1e18;
        for (FUnit& U : Units)
        {
            if (!Ready(U))
                continue;
            const double Rank = FVector2D::Distance(U.At, CamAt) + (U.Element >= 0 ? 700 : 0) + (U.Mode == EMode::Cover ? 0 : 150);
            if (Rank < Score)
            {
                Score = Rank;
                Best = &U;
            }
        }
        if (!Best)
            return;
        FPick& P = Picks.AddDefaulted_GetRef();
        P.Unit = Best->Id;
        int32 Families[5] = {0, 1, 2, 3, 4};
        for (int32 I = 4; I > 0; --I)
            Swap(Families[I], Families[FMath::Min(I, static_cast<int32>(ShowRand(Rng) * (I + 1)))]);
        for (int32 I = 0; I < 3; ++I)
            P.Skills[I] = Families[I] * 4 + FMath::Min(3, static_cast<int32>(ShowRand(Rng) * 4));
        P.Chosen = FMath::Min(2, static_cast<int32>(ShowRand(Rng) * 3));
        Best->Busy = ShowPickLength;
        if (Best->Mode == EMode::Peek)
            SetMode(*Best, EMode::Back);
        Cue(TEXT("LevelUp"), Best->At, 2, GWPalette::Marigold, .9f);
    }
    void StepPicks(float Dt)
    {
        for (int32 I = Picks.Num() - 1; I >= 0; --I)
        {
            FPick& P = Picks[I];
            P.Age += Dt;
            FUnit* U = Find(P.Unit);
            if (!U || !U->Alive)
            {
                Picks.RemoveAt(I);
                continue;
            }
            if (!P.Given && P.Age >= ShowAbsorb)
            {
                // The shards are in: the player has the skill from now on.
                P.Given = true;
                const int32 Family = P.Skills[P.Chosen] / 4;
                U->Element = Family;
                U->Skill = P.Skills[P.Chosen];
                U->Absorbed = Clock;
                U->Flash = 1;
                U->FlashColor = ShowElementColor(Family);
                U->NovaIn = 1.5f;
                U->GuardIn = 0;
                Cue(TEXT("Pickup"), U->At, 40, ShowElementColor(Family), 1.2f);
                Cue(TEXT("Pulse"), U->At, 50, ShowElementColor(Family), .8f);
            }
            if (P.Age >= ShowPickLength)
                Picks.RemoveAt(I);
        }
    }
    // A player arrives with three triangles in tow (the one down longest comes back with them, or the one furthest
    // back brings them).
    void StartTail()
    {
        FUnit* Head = nullptr;
        for (FUnit& U : Units)
            if (U.Kind == EKind::Hero && !U.Alive && (!Head || U.Respawn < Head->Respawn))
                Head = &U;
        if (!Head)
        {
            double Back = 1e18;
            for (FUnit& U : Units)
                if (Ready(U) && U.At.X < Back)
                {
                    Back = U.At.X;
                    Head = &U;
                }
        }
        if (!Head)
            return;
        if (!Head->Alive)
            Revive(*Head);
        TailLeader = Head->Id;
        const FVector2D From = Head->At;
        const int32 Leader = Head->Id;
        // (Head may move in memory as the triangles are added.)
        for (int32 K = 1; K <= 3; ++K)
        {
            FUnit& M = AddUnit(EKind::Mate, 1, From + FVector2D(-48.0 * K, K % 2 ? 16.0 : -16.0));
            M.Shape = 3;
            M.R = 20;
            M.MaxHp = M.Hp = 45;
            M.Leader = Leader;
            M.Link = K;
            M.DropHeight = 520 + 70 * K;
            M.DropTime = .6f + .08f * K;
            M.Z = M.DropHeight;
            SetMode(M, EMode::Drop);
        }
    }
    void LaunchTail()
    {
        const FUnit* Head = Find(TailLeader);
        if (!Head || !Head->Alive)
        {
            TailLeader = -1;
            return;
        }
        TailTo = Crowd(Head->At, 1300);
        TailLaunched = Clock;
        const int32 Leader = Head->Id;
        int32 K = 0;
        for (FUnit& M : Units)
            if (M.Alive && M.Kind == EKind::Mate && M.Leader == Leader && M.Mode != EMode::Drop && M.Mode != EMode::Flying)
            {
                M.FlyFrom = M.At;
                M.FlyTo = TailTo + FVector2D(60, 0).GetRotated(K * 120.f + 30);
                M.FlyTime = .75f + .1f * K;
                M.FlyArc = 280;
                M.FlyAge = 0;
                SetMode(M, EMode::Flying);
                CueToward(TEXT("Launch"), M.At, 30, GWPalette::Teal, .7f, (M.FlyTo - M.At).GetSafeNormal(), .35f);
                ++K;
            }
        TailLeader = -1;
    }
    void WarnBoss()
    {
        BossSpot = FVector2D(Front(2) + 120, (ShowRand(Rng) - .5f) * 500);
        BossWarned = Clock;
        Cue(TEXT("Telegraph"), BossSpot, 3, GWPalette::Danger, 1.5f);
    }
    void DropBoss()
    {
        if (BossId >= 0)
            return;
        FUnit& B = AddUnit(EKind::Boss, 2, BossSpot);
        B.Shape = 6;
        B.R = 120;
        B.MaxHp = B.Hp = 2600;
        B.Elite = true;
        B.DropHeight = 1800;
        B.DropTime = .55f;
        B.Z = B.DropHeight;
        B.RingIn = 2.5f;
        B.SlamIn = 3;
        SetMode(B, EMode::Drop);
        BossId = B.Id;
    }
    // A gunner near the middle of the view evolves into an elite.
    void Evolve()
    {
        FUnit* Best = nullptr;
        double Score = 1e18;
        for (FUnit& U : Units)
            if (U.Alive && U.Kind == EKind::Gunner && !U.Elite && U.Z < 1)
                if (const double D = FVector2D::Distance(U.At, CamAt); D < Score)
                {
                    Score = D;
                    Best = &U;
                }
        if (!Best)
            return;
        Best->Elite = true;
        Best->R *= 1.45f;
        Best->MaxHp *= 2.5f;
        Best->Hp = Best->MaxHp;
        Best->Flash = 1;
        Best->FlashColor = GWPalette::Magenta;
        Cue(TEXT("Assemble"), Best->At, 2, GWPalette::Magenta, 1.3f);
        Cue(TEXT("Pulse"), Best->At, 60, GWPalette::Magenta, .9f);
    }
    // A player picks up the teammate beside it and throws it into the thickest knot of the horde in reach.
    void StartThrow()
    {
        FUnit* Hurler = nullptr;
        FUnit* Carried = nullptr;
        double Best = 1e18;
        for (FUnit& A : Units)
        {
            if (!Ready(A))
                continue;
            for (FUnit& B : Units)
                if (&B != &A && Ready(B))
                    if (const double Rank = FVector2D::Distance(A.At, B.At); Rank < 320 && Rank + FVector2D::Distance(A.At, CamAt) * .3 < Best)
                    {
                        Best = Rank + FVector2D::Distance(A.At, CamAt) * .3;
                        Hurler = &A;
                        Carried = &B;
                    }
        }
        if (!Hurler)
            return;
        ThrowTo = Crowd(Hurler->At, 1000);
        Thrower = Hurler->Id;
        Thrown = Carried->Id;
        ThrowAge = 0;
        Hurler->Busy = 1.6f;
        if (Hurler->Mode == EMode::Peek)
            SetMode(*Hurler, EMode::Back);
        Release(*Carried);
        SetMode(*Carried, EMode::Carried);
        Carried->Busy = 4;
        Cue(TEXT("Pickup"), Carried->At, 60, GWPalette::Teal, .8f);
    }
    void StepThrow(float Dt)
    {
        if (ThrowAge < 0)
            return;
        ThrowAge += Dt;
        FUnit* Hurler = Find(Thrower);
        FUnit* Carried = Find(Thrown);
        if (!Carried || !Carried->Alive || ThrowAge > 4)
        {
            ThrowAge = -1;
            Thrower = Thrown = -1;
            return;
        }
        if (Carried->Mode != EMode::Carried)
            return;
        if (!Hurler || !Hurler->Alive)
        {
            // Its carrier fell: it is dropped where it was.
            Carried->Z = 0;
            SetMode(*Carried, EMode::Down);
            Carried->Wait = .6f;
            ThrowAge = -1;
            Thrower = Thrown = -1;
            return;
        }
        Carried->At = Hurler->At + FVector2D(-6, 4);
        Carried->Z = 74 + 7 * FMath::Sin(ThrowAge * 9);
        Carried->Yaw = Hurler->Yaw;
        Hurler->Yaw = ShowTurn(Hurler->Yaw, ShowHeading(ThrowTo - Hurler->At), Dt * 400);
        Hurler->Busy = FMath::Max(Hurler->Busy, .2f);
        if (ThrowAge < .9f)
            return;
        Carried->FlyFrom = Carried->At;
        Carried->FlyTo = ThrowTo;
        Carried->FlyTime = static_cast<float>(FMath::Clamp(FVector2D::Distance(Carried->At, ThrowTo) / 850, .7, 1.2));
        Carried->FlyArc = 330;
        Carried->FlyAge = 0;
        SetMode(*Carried, EMode::Flying);
        CueToward(TEXT("Launch"), Hurler->At, 60, BodyColor(*Hurler), 1.f, (ThrowTo - Hurler->At).GetSafeNormal(), .4f);
    }
    void Events(double Before, double After)
    {
        auto Crossed = [&](float At)
        {
            const double When = At + FMath::FloorToDouble((After - At) / ShowCycle) * ShowCycle;
            return When > Before && When <= After;
        };
        for (const float At : ShowPicks)
            if (Crossed(At))
                StartPick();
        if (Crossed(ShowTailIn))
            StartTail();
        if (Crossed(ShowTailOff))
            LaunchTail();
        if (Crossed(ShowBossWarn))
            WarnBoss();
        if (Crossed(ShowBossDrop))
            DropBoss();
        if (Crossed(ShowEvolve))
            Evolve();
        if (Crossed(ShowThrowAt))
            StartThrow();
        if (Crossed(ShowBossEnd))
            if (FUnit* B = Find(BossId); B && B->Alive)
                Slay(*B, -1);
    }

    // ---- The camera: the event under way, else the line of contact swept slowly from end to end ----
    void Direct(float Dt, bool Snap)
    {
        const float At = ShowCycleOf(Clock);
        FVector2D Want((Front(1) + Front(2)) * .5f, 330 * FMath::Sin(static_cast<float>(Clock) * .11f));
        float Distance = 2150, Pitch = 47;
        const float Yaw = -90 + 7 * FMath::Sin(static_cast<float>(Clock) * .09f);
        const FUnit* Picked = nullptr;
        for (const FPick& P : Picks)
            if (const FUnit* U = Find(P.Unit))
                Picked = U;
        const FUnit* Hurler = Find(Thrower);
        const FUnit* Head = Find(TailLeader);
        if (Picked)
        {
            // Room above the player for its cards.
            Want = Picked->At + FVector2D(0, -120);
            Distance = 1450;
            Pitch = 41;
        }
        else if (At >= ShowBossWarn - .4f && At < ShowBossDrop + 3.5f)
        {
            Want = BossSpot + FVector2D(-120, 0);
            Distance = At < ShowBossDrop ? 2050.f : 1700.f;
            Pitch = At < ShowBossDrop ? 52.f : 30.f;
        }
        else if (ThrowAge >= 0 && Hurler)
        {
            Want = FMath::Lerp(Hurler->At, ThrowTo, .5);
            Distance = 1850;
            Pitch = 43;
        }
        else if (Clock - TailLaunched < 1.8)
        {
            Want = TailTo + FVector2D(-260, 0);
            Distance = 1850;
            Pitch = 43;
        }
        else if (Head && At >= ShowTailIn && At < ShowTailOff)
        {
            Want = Head->At + FVector2D(300, 0);
            Distance = 1750;
            Pitch = 42;
        }
        if (Snap)
        {
            CamAt = Want;
            CamDistance = Distance;
            CamPitch = Pitch;
            CamYaw = Yaw;
            return;
        }
        CamAt += (Want - CamAt) * (1 - FMath::Exp(-Dt / .9f));
        CamDistance += (Distance - CamDistance) * (1 - FMath::Exp(-Dt / 1.4f));
        CamPitch += (Pitch - CamPitch) * (1 - FMath::Exp(-Dt / 1.6f));
        CamYaw += (Yaw - CamYaw) * (1 - FMath::Exp(-Dt / 2.5f));
    }

    // ---- Running ----
    void Reset(uint64 Seed)
    {
        Rng = Seed | 1;
        for (int32 I = 0; I < 6; ++I)
            ShowRand(Rng);
        Clock = 0;
        NextId = 1;
        HeroDeaths = MonsterDeaths = 0;
        SpawnIn = 0;
        BossWarned = TailLaunched = -100;
        BossId = Thrower = Thrown = TailLeader = -1;
        ThrowAge = -1;
        Units.Reset();
        Units.Reserve(96);
        Shots.Reset();
        Picks.Reset();
        CuesOut.Reset();
        ZapsOut.Reset();
        SpentOut.Reset();
        JoltOut = 0;
        // Cover: a staggered page of drawn crates and graphite rocks, a little more of it in the middle.
        struct FLay
        {
            double X, Y, W, H;
            bool Rock;
        };
        static const FLay Layout[] = {{-760, -470, 50, 50, false}, {-820, -60, 50, 100, false}, {-760, 380, 50, 50, false},
                                      {-1120, -300, 60, 60, true}, {-1100, 220, 50, 50, false}, {-300, -600, 100, 50, false},
                                      {-260, 220, 50, 50, false},  {40, -280, 50, 100, false},  {60, 540, 100, 50, false},
                                      {300, -20, 60, 60, true},    {620, -480, 50, 50, false},  {700, 140, 50, 100, false},
                                      {640, 560, 50, 50, false},   {1060, -200, 60, 60, true},  {1050, 330, 50, 50, false}};
        Blocks.Reset();
        for (const FLay& L : Layout)
        {
            FBlock& K = Blocks.AddDefaulted_GetRef();
            K.C = FVector2D(L.X, L.Y);
            K.H = FVector2D(L.W, L.H);
            K.Rock = L.Rock;
        }
        BuildSlots();
        // Everyone already at it: the players in cover on the left, the horde's gunners in cover on the right, its runners
        // and brutes on their way across.
        for (int32 Chair = 0; Chair < 8; ++Chair)
        {
            FUnit& U = AddUnit(EKind::Hero, 1, FVector2D(-900, (Chair - 3.5) * 180));
            U.SeatId = Chair;
            Outfit(U);
            ChooseSlot(U, false);
            if (Slots.IsValidIndex(U.Slot))
                U.At = Slots[U.Slot].At;
            SetMode(U, EMode::Cover);
            U.Wait = ShowRand(Rng);
        }
        for (int32 I = 0; I < 8; ++I)
            AddMonster(EKind::Gunner, FVector2D(900, (I - 3.5) * 190), true);
        for (int32 I = 0; I < 7; ++I)
            AddMonster(EKind::Runner, FVector2D(150 + ShowRand(Rng) * 600, (ShowRand(Rng) - .5f) * 1300), true);
        for (int32 I = 0; I < 2; ++I)
            AddMonster(EKind::Brute, FVector2D(700 + 200 * I, (I - .5) * 600), true);
        Direct(0, true);
    }
    void Step(float Dt)
    {
        const double Before = Clock;
        Clock += Dt;
        Events(Before, Clock);
        Spawning(Dt);
        const int32 Count = Units.Num();
        for (int32 I = 0; I < Count; ++I)
            Think(Units[I], Dt);
        StepThrow(Dt);
        Resolve();
        StepShots(Dt);
        StepPicks(Dt);
        for (FBlock& K : Blocks)
            K.Flash = FMath::Max(0.f, K.Flash - Dt * 6);
        Units.RemoveAll([](const FUnit& U) { return !U.Alive && U.Kind != EKind::Hero; });
        Direct(Dt, false);
    }
    // Starts the fight afresh and plays it quietly up to To seconds in (fixed steps, so the same seed gives the same
    // moment), the camera already in place.
    void Restart(uint64 Seed, double To)
    {
        Reset(Seed);
        Quiet = true;
        while (Clock + 1e-6 < To)
            Step(ShowStep);
        Quiet = false;
        CuesOut.Reset();
        ZapsOut.Reset();
        SpentOut.Reset();
        JoltOut = 0;
        Direct(0, true);
    }
    // Plays on by Delta (sub-stepped, so a long frame does not throw anyone through a wall).
    void Advance(float Delta)
    {
        const float Dt = FMath::Clamp(Delta, 0.f, .1f);
        const int32 Steps = FMath::Max(1, FMath::CeilToInt(Dt / ShowStep));
        for (int32 I = 0; I < Steps; ++I)
            Step(Dt / Steps);
    }
};

void AGWWorld::StopShowcase()
{
    ShowActive = false;
    if (Fx)
        Fx->Stop();
    if (Camera)
    {
        // The match's camera shows the diorama as painted: no depth of field, no vignette.
        Camera->SetFieldOfView(50);
        FPostProcessSettings& Post = Camera->PostProcessSettings;
        Post.bOverride_DepthOfFieldFocalDistance = Post.bOverride_DepthOfFieldFstop = Post.bOverride_DepthOfFieldSensorWidth = false;
        Post.VignetteIntensity = 0;
    }
    SpentShots.Reset();
    Scuffs.Reset();
    ShowcasePicks.Reset();
}

FString AGWWorld::ShowcaseStats() const
{
    if (!Battle)
        return TEXT("no battle");
    const FGWShowBattle& B = *Battle;
    int32 Players = 0, Horde = 0, Skilled = 0, InCover = 0;
    for (const auto& U : B.Units)
        if (U.Alive)
        {
            Players += U.Kind == FGWShowBattle::EKind::Hero;
            Horde += U.Side == 2;
            Skilled += U.Element >= 0;
            InCover += U.Mode == FGWShowBattle::EMode::Cover;
        }
    return FString::Printf(TEXT("t %.1f: players up %d/8 (%d with skills), horde %d, %d in cover, %d shots, %d cards, boss %s, lost %d players and %d of the horde"),
                           B.Clock, Players, Skilled, Horde, InCover, B.Shots.Num(), B.Picks.Num(), B.BossId >= 0 ? TEXT("up") : TEXT("-"),
                           B.HeroDeaths, B.MonsterDeaths);
}

void AGWWorld::TickShowcase(AGWGameState* S, float Delta)
{
    using namespace GWPalette;
    using EShowKind = FGWShowBattle::EKind;
    using EShowMode = FGWShowBattle::EMode;
    using FShowUnit = FGWShowBattle::FUnit;
    if (!Battle)
        Battle = MakePimpl<FGWShowBattle>();
    FGWShowBattle& B = *Battle;
    if (!ShowActive)
    {
        // The page is laid out once: a big sheet of graph paper with the fight in its middle, no maze.
        ShowActive = true;
        if (Fx)
            Fx->Stop();
        SpentShots.Reset();
        Scuffs.Reset();
        ShotTraces.Reset();
        Floor->SetWorldLocation(FVector(ShowMid.X, ShowMid.Y, -20));
        Floor->SetWorldScale3D(FVector(ShowPage * 3 / 100, ShowPage * 3 / 100, .4f));
        if (FloorMaterial)
        {
            FloorMaterial->SetScalarParameterValue(TEXT("MapSize"), ShowPage);
            FloorMaterial->SetScalarParameterValue(TEXT("MapSizeY"), 0.f);
            FloorMaterial->SetScalarParameterValue(TEXT("GridMajor"), 1000.f);
            FloorMaterial->SetScalarParameterValue(TEXT("Pen"), -1.f);
        }
        Floor->SetVisibility(true);
        WallBodies->SetVisibility(false);
        WallHulls->SetVisibility(false);
        // A fresh fight each time the title page comes back, already under way.
        B.Restart(FPlatformTime::Cycles64(), 2.5);
    }
    if (ShowcaseSeek >= 0)
    {
        // Tests jump to a moment (the same moment every time).
        B.Restart(ShowSeed, ShowcaseSeek);
        ShowcaseSeek = -1;
        SpentShots.Reset();
        Scuffs.Reset();
    }
    else
        B.Advance(Delta);
    const float Spin = static_cast<float>(B.Clock);
    auto Spot = [&](const FVector2D& At, float Z) { return FVector(ShowMid.X + At.X, ShowMid.Y + At.Y, Z); };
    {
        static bool Reported = false;
        if (!Reported)
        {
            if (B.CamAt.ContainsNaN() || FMath::IsNaN(B.CamDistance) || FMath::IsNaN(B.CamPitch) || FMath::IsNaN(B.CamYaw))
            {
                Reported = true;
                UE_LOG(LogTemp, Warning, TEXT("GW showcase NaN camera at t %.2f: %s %f %f %f"), B.Clock, *B.CamAt.ToString(), B.CamDistance, B.CamPitch, B.CamYaw);
            }
            for (const FShowUnit& U : B.Units)
                if (!Reported && (U.At.ContainsNaN() || FMath::IsNaN(U.Z) || FMath::IsNaN(U.Yaw) || FMath::IsNaN(U.R) || U.Vel.ContainsNaN()))
                {
                    Reported = true;
                    UE_LOG(LogTemp, Warning, TEXT("GW showcase NaN unit %d kind %d mode %d at t %.2f: at %s z %f yaw %f vel %s push %s"), U.Id,
                           static_cast<int32>(U.Kind), static_cast<int32>(U.Mode), B.Clock, *U.At.ToString(), U.Z, U.Yaw, *U.Vel.ToString(),
                           *U.Push.ToString());
                }
            for (const auto& Shot : B.Shots)
                if (!Reported && (Shot.At.ContainsNaN() || Shot.Vel.ContainsNaN()))
                {
                    Reported = true;
                    UE_LOG(LogTemp, Warning, TEXT("GW showcase NaN shot from %d weapon %d at t %.2f"), Shot.Shooter, Shot.Weapon, B.Clock);
                }
        }
    }

    // ---- The camera's aim (the menu sits on the left: the subject is framed right of the middle) ----
    const float Distance = B.CamDistance;
    const float PitchDegrees = B.CamPitch + .7f * FMath::Sin(Spin * .31f), YawDegrees = B.CamYaw + 1.1f * FMath::Sin(Spin * .23f);
    const float YawRad = FMath::DegreesToRadians(YawDegrees), PitchRad = FMath::DegreesToRadians(PitchDegrees);
    const FVector Right(-FMath::Sin(YawRad), FMath::Cos(YawRad), 0);
    const FVector Focus = Spot(B.CamAt, 0) - Right * Distance * .15f + FVector(14 * FMath::Sin(Spin * .37f), 12 * FMath::Cos(Spin * .29f), 0);

    // ---- What the fight asked for this frame ----
    const FVector Offset(ShowMid.X, ShowMid.Y, 0);
    if (Fx)
    {
        Fx->Showcase(Focus, Delta);
        for (const auto& Out : B.CuesOut)
        {
            if (Out.Aimed)
                Fx->CueToward(Out.Name, Out.At + Offset, Out.Color, Out.Size, Out.Turn);
            else
                Fx->Cue(Out.Name, Out.At + Offset, Out.Color, Out.Size);
        }
        for (const auto& Out : B.ZapsOut)
            Fx->Zap(Out.From + Offset, Out.To + Offset, Out.Color, 10);
        if (B.JoltOut > 0)
            Fx->Jolt(B.JoltOut);
        // Burning and frozen units carry their loops; a skill just taken in glows round its player a moment.
        TArray<TPair<int32, FVector>> Burning, Slowed;
        TMap<int32, float> Sizes;
        for (const FShowUnit& U : B.Units)
            if (U.Alive)
            {
                if (U.Burning > 0)
                    Burning.Add({U.Id, Spot(U.At, U.Z + 25)});
                if (U.Slowed > 0)
                    Slowed.Add({U.Id, Spot(U.At, U.Z + 15)});
                Sizes.Add(U.Id, U.R / 22);
            }
        Fx->ShowcaseStatus(Burning, Slowed, Sizes);
        for (const FShowUnit& U : B.Units)
            if (U.Kind == EShowKind::Hero)
                Fx->Hold(U.SeatId, TEXT("EvolveAura"), U.Alive && B.Clock - U.Absorbed < 1.8, Spot(U.At, 20), ShowElementColor(U.Element), 1.1f);
    }
    for (const auto& Out : B.SpentOut)
        AddSpentShot(Out.At + Offset, Out.Velocity, Out.Weapon, Out.Side);
    B.CuesOut.Reset();
    B.ZapsOut.Reset();
    B.SpentOut.Reset();
    B.JoltOut = 0;

    // ---- The page's cover ----
    for (const auto& K : B.Blocks)
        Solid(MeshBlock, Spot(K.C, K.Rock ? 80 : 70), FVector(K.H.X * 2, K.H.Y * 2, K.Rock ? 160 : 140), 0, K.Rock ? Hex(0x5E6678) : Kraft,
              K.Flash * .45f, 3.5f);

    // ---- The players ----
    auto Player = [&](const FShowUnit& U)
    {
        const float R = U.R, Height = R * 1.1f;
        const FLinearColor Color = Seat(U.SeatId);
        const bool Crouched = U.Mode == EShowMode::Cover || U.Mode == EShowMode::Back;
        const bool Lying = U.Mode == EShowMode::Down, Tumbling = U.Mode == EShowMode::Carried || U.Mode == EShowMode::Flying;
        const FVector Base = Spot(U.At, 0);
        const float Tall = Height * (Lying ? .6f : Crouched ? .84f : 1.f);
        const bool Tinted = U.Flash > 0 && !U.FlashColor.Equals(FLinearColor::White);
        const FLinearColor Shade = Tinted ? FMath::Lerp(Color, U.FlashColor, U.Flash) : Color;
        if (Lying)
        {
            // Down where it landed, a hatched patch round it, a star turning over it.
            Decal(DecalHatch, Base + FVector(0, 0, 1), FVector2D(R * 5.2f), Spin * 25, Color.CopyWithNewOpacity(.55f));
            Solid(MeshStar, Base + FVector(0, 0, Tall + 22), FVector(R * .7f, R * .7f, 14), Spin * 70, Color, 0, 2.5f);
        }
        if (Tumbling)
            Rotated(MeshCoin, Base + FVector(0, 0, Height * .5f + U.Z), FVector(2 * R, 2 * R, Height),
                    U.Mode == EShowMode::Flying ? FRotator(-U.FlyAge * 760, U.Yaw, 0) : FRotator(24 * FMath::Sin(Spin * 7), U.Yaw, 18 * FMath::Cos(Spin * 5)),
                    Color, 3.5f);
        else
            Solid(MeshCoin, Base + FVector(0, 0, Tall * .5f + U.Z), FVector(2 * R, 2 * R, Tall), U.Yaw, Shade, Tinted ? 0.f : U.Flash * .6f, 3.5f);
        if (U.Mode != EShowMode::Flying)
        {
            const FVector Aim = FRotator(0, U.Yaw, 0).Vector();
            const int32 Gun = U.Gun;
            const float Length = Gun == 3 ? 58 : Gun == 4 ? 48 : Gun == 0 ? 30 : 42, Thick = Gun == 5 ? 15 : Gun == 2 ? 12 : 9;
            const FVector Hand = Base + FVector(0, 0, Tall * .55f + U.Z);
            Solid(MeshBlock, Hand + Aim * (R + Length * .5f - 8), FVector(Length, Thick, Thick), U.Yaw, InkSoft, 0, 2);
            // A skill shows on the gun's tip in its family's colour.
            if (U.Element >= 0)
                Solid(MeshSphere, Hand + Aim * (R + Length - 4), FVector(Thick * .95f), 0, ShowElementColor(U.Element), 0, 1.5f);
            if (U.Hat > 0)
                Hat(U.Hat, Base + FVector(0, 0, Tall + U.Z), R, U.Yaw, Spin);
        }
        const float Up = FMath::Min(U.Z, 400.f) / 700;
        Decal(DecalShadow, Base + FVector(0, 0, .6f), FVector2D(R * 2.7f * (1 - Up)), 0, FLinearColor(0, 0, 0, .55f));
        Decal(DecalLoop, Base + FVector(0, 0, 1.4f), FVector2D(R * 3.4f), Spin * 30, Color.CopyWithNewOpacity(.75f));
        if (U.Element >= 0 && U.Z < 5)
        {
            // Its skill: a turning hexagon band in the family's colour and three shards circling it.
            const FLinearColor Family = ShowElementColor(U.Element);
            Decal(DecalHexRing, Base + FVector(0, 0, 1.8f), FVector2D(R * 4.2f), -Spin * 50, Family.CopyWithNewOpacity(.8f));
            for (int32 K = 0; K < 3; ++K)
            {
                const float A = Spin * 2.4f + K * 2.094f;
                Solid(MeshDiamond, Base + FVector(FMath::Cos(A) * (R + 16), FMath::Sin(A) * (R + 16), 18 + 6 * FMath::Sin(Spin * 3 + K)),
                      FVector(12, 7, 7), FMath::RadiansToDegrees(A) + 90, Family, 0, 1.5f);
            }
        }
        if (U.Guard > 0)
        {
            // The shield: six teal plates circling it, a hexagon band on the paper.
            for (int32 K = 0; K < 6; ++K)
            {
                const float A = -Spin * 1.6f + K * 1.047f;
                Solid(MeshHex, Base + FVector(FMath::Cos(A) * (R + 22), FMath::Sin(A) * (R + 22), 26), FVector(18, 4, 22), FMath::RadiansToDegrees(A) + 90,
                      Teal, 0, 1.5f);
            }
            Decal(DecalHexRing, Base + FVector(0, 0, 2), FVector2D(R * 5), Spin * 40, Teal.CopyWithNewOpacity(.85f));
        }
    };

    // ---- The horde ----
    auto Monster = [&](const FVector2D& At, float R, uint8 Shape, FLinearColor Color, float Yaw, float Rise, float Flash0, bool Crowned)
    {
        const float Height = R * 1.25f, InkWidth = R >= 100 ? 7.f : R >= 50 ? 5.5f : 3.5f;
        const FVector Base = Spot(At, 0), Center = Base + FVector(0, 0, Height * .5f + Rise);
        switch (Shape)
        {
        case 0:
            Solid(MeshCoin, Center, FVector(2 * R, 2 * R, Height), Yaw, Color, Flash0, InkWidth);
            break;
        case 1:
            Solid(MeshBlock, Center, FVector(1.8f * R, 1.8f * R, Height), Yaw, Color, Flash0, InkWidth);
            break;
        case 2:
            Solid(MeshBlock, Center, FVector(2.6f * R, 1.3f * R, Height * .9f), Yaw, Color, Flash0, InkWidth);
            break;
        case 4:
            Solid(MeshPent, Center, FVector(2.1f * R, 2.1f * R, Height), Yaw, Color, Flash0, InkWidth);
            break;
        case 5:
            Solid(MeshHex, Center, FVector(2.2f * R, 2.2f * R, Height), Yaw, Color, Flash0, InkWidth);
            break;
        case 6:
            Solid(MeshTrap, Center, FVector(2.2f * R, 2 * R, Height * .8f), Yaw, Color, Flash0, InkWidth);
            break;
        default:
            Solid(MeshTri, Center, FVector(MeshSizes[MeshTri].X * R / 40, MeshSizes[MeshTri].Y * R / 40, Height), Yaw, Color, Flash0, InkWidth);
            break;
        }
        // Two ink eyes give every monster a facing; the big ones wear a gold star.
        const FVector Ahead = FRotator(0, Yaw, 0).Vector(), Side(-Ahead.Y, Ahead.X, 0);
        const float Eye = FMath::Clamp(R * .22f, 6.f, 26.f);
        for (const float Sign : {-1.f, 1.f})
            Solid(MeshSphere, Base + Ahead * R * .78f + Side * Sign * R * .34f + FVector(0, 0, Height * .7f + Rise), FVector(Eye), 0, Paper, 0,
                  Eye * .35f);
        if (Crowned)
            Solid(MeshStar, Base + FVector(0, 0, Height + Rise + 30), FVector(R * .8f, R * .8f, 28), Spin * 40, Marigold, 0, 4);
        Decal(DecalShadow, Base + FVector(0, 0, .6f), FVector2D(R * 2.7f), 0, FLinearColor(0, 0, 0, Rise > 300 ? .25f : .62f));
        if (R >= 50 || Crowned)
            Decal(DecalHex, Base + FVector(0, 0, 1.3f), FVector2D(R * 3.2f), Spin * (R >= 100 ? 25 : 0), (R >= 100 ? Danger : Magenta).CopyWithNewOpacity(.7f));
    };

    for (const FShowUnit& U : B.Units)
    {
        if (!U.Alive)
            continue;
        switch (U.Kind)
        {
        case EShowKind::Hero:
            Player(U);
            break;
        case EShowKind::Mate:
        {
            // A tail's triangle, roped to the one ahead of it with dots.
            const float R = U.R, Height = R * 1.1f;
            Solid(MeshTri, Spot(U.At, Height * .5f + U.Z), FVector(MeshSizes[MeshTri].X * R / 40, MeshSizes[MeshTri].Y * R / 40, Height),
                  U.Mode == EShowMode::Flying ? Spin * 900 : U.Yaw, Teal, U.Flash * .6f, 5.5f);
            Decal(DecalShadow, Spot(U.At, .6f), FVector2D(R * 2.7f * (1 - FMath::Min(U.Z, 400.f) / 700)), 0, FLinearColor(0, 0, 0, .5f));
            if (U.Mode == EShowMode::Charge)
            {
                const FShowUnit* Ahead = B.Find(U.Leader);
                for (const FShowUnit& M : B.Units)
                    if (M.Alive && M.Kind == EShowKind::Mate && M.Leader == U.Leader && M.Link == U.Link - 1 && M.Mode == EShowMode::Charge)
                        Ahead = &M;
                if (Ahead && Ahead->Z < 1)
                {
                    const int32 Dots = FMath::Clamp(FMath::FloorToInt(static_cast<float>(FVector2D::Distance(U.At, Ahead->At)) / 18), 1, 8);
                    for (int32 D = 1; D < Dots; ++D)
                        Decal(DecalDot, Spot(FMath::Lerp(U.At, Ahead->At, static_cast<double>(D) / Dots), 2), FVector2D(9), 0, Teal.CopyWithNewOpacity(.75f));
                }
            }
            break;
        }
        case EShowKind::Boss:
        {
            // The boss squashes like jelly as it lands.
            const float Settled = static_cast<float>(B.Clock - U.Landed);
            JellyOn = U.Mode != EShowMode::Drop && Settled < 1.2f;
            JellyPivot = Spot(U.At, 0);
            JellyScale = JellyAt(Settled, true);
            Monster(U.At, U.R, 6, Night, U.Yaw + 10 * FMath::Sin(Spin * 1.3f), U.Z, U.Flash * .3f, true);
            JellyOn = false;
            break;
        }
        default:
            Monster(U.At, U.R, U.Shape, FGWShowBattle::BodyColor(U), U.Yaw, U.Z, U.Flash * .6f, U.Elite);
            break;
        }
        // A slam being wound up: its reach turns on the paper.
        if (U.Winding >= 0)
        {
            const float Wound = FMath::Clamp(U.Winding / (U.Kind == EShowKind::Boss ? 1.f : .7f), 0.f, 1.f);
            Decal(DecalCompass, Spot(U.At, 2.5f), FVector2D((U.Kind == EShowKind::Boss ? 600 : 330) * (.7f + .3f * Wound)), -Spin * 90,
                  Danger.CopyWithNewOpacity(.35f + .45f * Wound));
        }
    }

    // ---- Shots ----
    for (const auto& Shot : B.Shots)
    {
        if (Shot.Lobbed)
        {
            // A rocket's arc to where its mark stood, its landing ring turning.
            const float U = FMath::Clamp(Shot.Age / Shot.Flight, 0.f, 1.f);
            const FVector2D Way = (Shot.To - Shot.From).GetSafeNormal();
            const FVector At = Spot(FMath::Lerp(Shot.From, Shot.To, static_cast<double>(U)), 60 * (1 - U) + 20 * U + 380 * 4 * U * (1 - U));
            const float Climb = (380 * 4 * (1 - 2 * U) - 40) / FMath::Max(1.f, static_cast<float>(FVector2D::Distance(Shot.From, Shot.To)));
            const FRotator Nose(FMath::RadiansToDegrees(FMath::Atan(Climb)), ShowHeading(Way), 0);
            Rotated(MeshDiamond, At, FVector(56, 22, 22), Nose, Fire, 3);
            Solid(MeshSphere, At - Nose.Vector() * 30, FVector(16), 0, Spark, 0, 0);
            Decal(DecalShadow, Spot(FMath::Lerp(Shot.From, Shot.To, static_cast<double>(U)), .7f), FVector2D(60), 0, FLinearColor(0, 0, 0, .4f));
            Decal(DecalCompass, Spot(Shot.To, 2.5f), FVector2D(Shot.Radius * 2), -Spin * 80, Danger.CopyWithNewOpacity(.4f + .35f * U));
            continue;
        }
        const FVector2D Way = Shot.Vel.GetSafeNormal();
        const float Yaw = ShowHeading(Way);
        const FVector At = Spot(Shot.At, Shot.Z), Back(Way.X, Way.Y, 0);
        if (Shot.Weapon == 9)
        {
            Solid(MeshDiamond, At, FVector(30, 13, 13), Yaw, Violet, 0, 2.5f);
            Decal(DecalStreak, At - Back * 45, FVector2D(90, 16), Yaw, Night.CopyWithNewOpacity(.8f));
            continue;
        }
        const FLinearColor Trail = ShowElementColor(Shot.Element);
        const float Size = Shot.Weapon == 3 ? 12.f : 9.f, Length = Shot.Weapon == 3 ? 120.f : Shot.Weapon == 4 ? 70.f : 55.f;
        Solid(MeshSphere, At, FVector(Size), 0, Shot.Element >= 0 ? Trail : InkSoft, 0, 0);
        Decal(DecalStreak, At - Back * Length * .5f, FVector2D(Length, Size * 1.3f), Yaw, Trail.CopyWithNewOpacity(.85f));
    }

    // ---- Where things will come down ----
    const double SinceWarn = B.Clock - B.BossWarned;
    if (SinceWarn >= 0 && SinceWarn < ShowBossDrop - ShowBossWarn + .6)
    {
        const float Grow = FMath::Clamp(static_cast<float>(SinceWarn / (ShowBossDrop - ShowBossWarn + .5)), 0.f, 1.f);
        Decal(DecalShadow, Spot(B.BossSpot, .6f), FVector2D(300 * Grow), 0, FLinearColor(0, 0, 0, .5f * Grow));
        Decal(DecalCompass, Spot(B.BossSpot, 2), FVector2D(820 * (.4f + .6f * Grow)), Spin * 60, Danger.CopyWithNewOpacity(.75f));
    }
    if (const FShowUnit* Flung = B.ThrowAge >= 0 ? B.Find(B.Thrown) : nullptr; Flung && (Flung->Mode == EShowMode::Carried || Flung->Mode == EShowMode::Flying))
    {
        const float Near = FMath::Clamp(B.ThrowAge / 1.9f, 0.f, 1.f);
        Decal(DecalCompass, Spot(B.ThrowTo, 2.4f), FVector2D(440 - 100 * Near), -Spin * 70, Coral.CopyWithNewOpacity(.5f + .35f * Near));
        Decal(DecalPlus, Spot(B.ThrowTo, 2.6f), FVector2D(80), 45, Danger.CopyWithNewOpacity(.7f));
    }
    if (B.Clock - B.TailLaunched < 1.1)
        for (int32 K = 0; K < 3; ++K)
            Decal(DecalTriRing, Spot(B.TailTo + FVector2D(60, 0).GetRotated(K * 120.f + 30), 2.2f), FVector2D(150), Spin * 70 + K * 40,
                  Teal.CopyWithNewOpacity(.75f));

    // ---- Level-ups, for the HUD's cards ----
    ShowcasePicks.Reset();
    for (const auto& Pick : B.Picks)
        if (const FShowUnit* U = B.Find(Pick.Unit))
        {
            FGWShowcasePick& Out = ShowcasePicks.AddDefaulted_GetRef();
            Out.Age = Pick.Age;
            Out.Chosen = Pick.Chosen;
            for (int32 K = 0; K < 3; ++K)
                Out.Skills[K] = Pick.Skills[K];
            Out.Head = Spot(U->At, U->Z + U->R * 1.1f + 60);
            Out.Body = Spot(U->At, U->Z + 24);
            Out.Color = Seat(U->SeatId);
        }

    StepSpentShots(Delta, false);

    // ---- The camera: depth of field on the action (a tilt-shift diorama), a light vignette, a slow handheld roll ----
    const float Fov = FMath::GetMappedRangeValueClamped(FVector2f(1350, 3300), FVector2f(42, 52), Distance);
    const FVector Back(-FMath::Cos(YawRad) * FMath::Cos(PitchRad), -FMath::Sin(YawRad) * FMath::Cos(PitchRad), FMath::Sin(PitchRad));
    const FVector Shake = Fx ? Fx->CameraShake(Delta) : FVector::ZeroVector;
    CameraFocus = Focus;
    Camera->SetFieldOfView(Fov);
    Camera->SetWorldLocation(Focus + Shake + Back * Distance);
    Camera->SetWorldRotation(FRotator(-PitchDegrees, YawDegrees, .8f * FMath::Sin(Spin * .19f)));
    FPostProcessSettings& Post = Camera->PostProcessSettings;
    Post.bOverride_DepthOfFieldFocalDistance = Post.bOverride_DepthOfFieldFstop = Post.bOverride_DepthOfFieldSensorWidth = true;
    Post.DepthOfFieldFocalDistance = Distance;
    Post.DepthOfFieldFstop = 1.f;
    Post.DepthOfFieldSensorWidth = 200;
    Post.VignetteIntensity = .3f;
    for (int32 I = 0; I < MeshCount; ++I)
    {
        Upload(Bodies[I], BodyBatches[I]);
        Upload(Hulls[I], HullBatches[I]);
    }
    for (int32 I = 0; I < DecalCount; ++I)
        Upload(Decals[I], DecalBatches[I]);
    UpdateAudio(S, Focus, Delta);
    WasInMatch = false;
}
