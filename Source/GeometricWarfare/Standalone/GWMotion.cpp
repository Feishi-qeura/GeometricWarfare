#include "GWMotion.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "GWController.h"
#include "GWGameMode.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWPalette.h"
#include "GWPlayerState.h"
#include "GWText.h"
#include "GWWorld.h"

// The sketchbook's motion: a stack of 2D transforms over the canvas (cards dropping in, sliding, popping, stamps
// slamming), appear timers, eased values, paper confetti on the HUD, and the page wipe between scenes.
using namespace GWPalette;

namespace
{
float Hash01(uint32 X)
{
    X ^= X >> 16;
    X *= 0x7feb352du;
    X ^= X >> 15;
    X *= 0x846ca68bu;
    X ^= X >> 16;
    return (X & 0xffffff) / 16777215.f;
}
FText PageCode(int32 Index)
{
    const auto& Levels = gwgame::defenseLevels();
    return Index >= 0 && Index < static_cast<int32>(Levels.size()) ? FText::FromString(ANSI_TO_TCHAR(Levels[Index].id)) : FText::GetEmpty();
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Transform stack
// ---------------------------------------------------------------------------------------------------------------

void AGWHUD::PushMotion(FVector2D Pivot, FVector2D Offset, float Turn, float Grow, float Alpha)
{
    const float Rad = FMath::DegreesToRadians(Turn), Cs = FMath::Cos(Rad) * Grow, Sn = FMath::Sin(Rad) * Grow;
    FAffine Local;
    Local.A = Cs;
    Local.B = -Sn;
    Local.C = Sn;
    Local.D = Cs;
    Local.X = static_cast<float>(Pivot.X + Offset.X - (Cs * Pivot.X - Sn * Pivot.Y));
    Local.Y = static_cast<float>(Pivot.Y + Offset.Y - (Sn * Pivot.X + Cs * Pivot.Y));
    const FAffine Outer = Motions.IsEmpty() ? FAffine() : Motions.Last().Matrix;
    FMotion& Entry = Motions.AddDefaulted_GetRef();
    Entry.Matrix.A = Outer.A * Local.A + Outer.B * Local.C;
    Entry.Matrix.B = Outer.A * Local.B + Outer.B * Local.D;
    Entry.Matrix.C = Outer.C * Local.A + Outer.D * Local.C;
    Entry.Matrix.D = Outer.C * Local.B + Outer.D * Local.D;
    Entry.Matrix.X = Outer.A * Local.X + Outer.B * Local.Y + Outer.X;
    Entry.Matrix.Y = Outer.C * Local.X + Outer.D * Local.Y + Outer.Y;
    Entry.Fade = Fade;
    // A motion that has settled is the identity: nothing for the canvas to do (and no extra batch).
    Entry.Pushed = !(FMath::IsNearlyEqual(Grow, 1.f, 1e-4f) && FMath::IsNearlyZero(Turn, 1e-3f) && Offset.IsNearlyZero(1e-3));
    if (Entry.Pushed && Canvas && Canvas->Canvas)
        Canvas->Canvas->PushRelativeTransform(FMatrix(FPlane(Local.A, Local.C, 0, 0), FPlane(Local.B, Local.D, 0, 0), FPlane(0, 0, 1, 0),
                                                      FPlane(Local.X, Local.Y, 0, 1)));
    Fade *= FMath::Clamp(Alpha, 0.f, 1.f);
}

void AGWHUD::PopMotion()
{
    if (Motions.IsEmpty())
        return;
    const FMotion Entry = Motions.Pop(EAllowShrinking::No);
    Fade = Entry.Fade;
    if (Entry.Pushed && Canvas && Canvas->Canvas)
        Canvas->Canvas->PopTransform();
}

void AGWHUD::ClearMotions()
{
    while (!Motions.IsEmpty())
        PopMotion();
    Fade = 1;
}

FVector2D AGWHUD::FromScreen(FVector2D P) const
{
    if (Motions.IsEmpty())
        return P;
    const FAffine& M = Motions.Last().Matrix;
    const float Det = M.A * M.D - M.B * M.C;
    if (FMath::Abs(Det) < 1e-6f)
        return FVector2D(-1e6, -1e6);
    const FVector2D Q(P.X - M.X, P.Y - M.Y);
    return FVector2D((M.D * Q.X - M.B * Q.Y) / Det, (-M.C * Q.X + M.A * Q.Y) / Det);
}

void AGWHUD::AddHit(const FBox2D& Bounds, int32 Action)
{
    // A card on its way in takes no clicks until it has nearly arrived (a click on the old place would surprise).
    if (Fade < .6f)
        return;
    if (Motions.IsEmpty())
    {
        Hits.Add({Bounds, Action});
        return;
    }
    const FAffine& M = Motions.Last().Matrix;
    FBox2D Out(ForceInit);
    for (const FVector2D& Corner : {Bounds.Min, FVector2D(Bounds.Max.X, Bounds.Min.Y), Bounds.Max, FVector2D(Bounds.Min.X, Bounds.Max.Y)})
        Out += M.Apply(Corner);
    Hits.Add({Out, Action});
}

// ---------------------------------------------------------------------------------------------------------------
// Timers, eased values, entrances
// ---------------------------------------------------------------------------------------------------------------

float AGWHUD::Since(uint32 Id)
{
    FSeen& Entry = SeenAt.FindOrAdd(Id, FSeen{Clock, -100, 0});
    if (Absent(Entry))
        Entry.First = Clock;
    Entry.Last = Clock;
    Entry.Frame = HudFrame;
    // Under the page wipe nothing has appeared yet: entrances start a beat after the tiles begin to clear.
    if (Covered)
        Entry.First = Clock + .28;
    return FMath::Max(0.f, static_cast<float>(Clock - Entry.First));
}

float AGWHUD::SinceRaw(uint32 Id)
{
    FSeen& Entry = SeenAt.FindOrAdd(Id, FSeen{Clock, -100, 0});
    if (Absent(Entry))
        Entry.First = Clock;
    Entry.Last = Clock;
    Entry.Frame = HudFrame;
    return static_cast<float>(Clock - Entry.First);
}

float AGWHUD::SceneAge(AGWGameState* S)
{
    // Counted from the reveal (Since waits under the page wipe): the cards come in as the tiles flip away.
    return Since(HashCombine(S ? S->Seed : 0u, 0x5CE7u + (S ? static_cast<uint32>(S->Rules) : 0u)));
}

float AGWHUD::Eased(uint32 Id, float Target, float Rate, float* Ghost)
{
    FVector2D* Value = EasedValues.Find(Id);
    if (!Value)
        Value = &EasedValues.Add(Id, FVector2D(Target, Target));
    Value->X = FMath::FInterpTo(static_cast<float>(Value->X), Target, FrameDelta, Rate);
    // The ghost jumps up with a rise and trails a drop after a short hold, so a hit shows the chunk it took.
    Value->Y = Target >= Value->Y ? Target : FMath::FInterpTo(static_cast<float>(Value->Y), Target, FrameDelta, 2.6f);
    if (Ghost)
        *Ghost = static_cast<float>(Value->Y);
    return static_cast<float>(Value->X);
}

float AGWHUD::Bump(uint32 Id, float Value)
{
    FVector2D& Entry = Bumps.FindOrAdd(Id, FVector2D(Value, -10));
    if (!FMath::IsNearlyEqual(static_cast<float>(Entry.X), Value, .01f))
    {
        Entry.X = Value;
        Entry.Y = Clock;
    }
    const float Age = static_cast<float>(Clock - Entry.Y);
    return 1 + .32f * FMath::Exp(-Age * 9) * FMath::Clamp(Age * 30, 0.f, 1.f);
}

AGWHUD::FMotionGuard AGWHUD::DropIn(FVector2D Pivot, float T, float Delay, float Rise)
{
    const float P = (T - Delay) / .42f;
    const float Settle = GWEase::OutBack(P, 1.5f);
    PushMotion(Pivot, FVector2D(0, -(1 - Settle) * Rise * Scale), (1 - GWEase::OutCubic(P)) * -3.5f, .965f + .035f * Settle,
               GWEase::Clamp01(P * 4));
    return FMotionGuard(this);
}

AGWHUD::FMotionGuard AGWHUD::SlideIn(FVector2D Pivot, float T, FVector2D From, float Delay)
{
    const float P = (T - Delay) / .4f;
    PushMotion(Pivot, From * Scale * (1 - GWEase::OutBack(P, 1.2f)), 0, 1, GWEase::Clamp01(P * 3));
    return FMotionGuard(this);
}

AGWHUD::FMotionGuard AGWHUD::PopIn(FVector2D Pivot, float T, float Delay)
{
    const float P = (T - Delay) / .34f;
    PushMotion(Pivot, FVector2D::ZeroVector, (1 - GWEase::OutCubic(P)) * -6, FMath::Max(.01f, GWEase::OutBack(P, 2.f)), GWEase::Clamp01(P * 5));
    return FMotionGuard(this);
}

AGWHUD::FMotionGuard AGWHUD::StampIn(FVector2D Pivot, float T, float Delay)
{
    // Falls onto the page from above the camera (2.4x), lands with a jolt and rocks to rest.
    const float P = (T - Delay) / .2f;
    const float Grow = P < 1 ? 2.4f - 1.4f * GWEase::InCubic(P) : 1 + .04f * GWEase::Wobble(T - Delay - .2f, 30, 9);
    PushMotion(Pivot, FVector2D::ZeroVector, P < 1 ? -14 * (1 - P) : 2.5f * GWEase::Wobble(T - Delay - .2f, 24, 6), Grow, GWEase::Clamp01(P * 2.5f));
    return FMotionGuard(this);
}

// ---------------------------------------------------------------------------------------------------------------
// Paper confetti on the HUD
// ---------------------------------------------------------------------------------------------------------------

void AGWHUD::Confetti(FVector2D At, int32 Count, float Speed, float Size, float Direction, float Spread)
{
    static const FLinearColor Colors[] = {Coral, Teal, Marigold, Cobalt, Mint, Violet};
    const FVector2D Where = Motions.IsEmpty() ? At : Motions.Last().Matrix.Apply(At);
    for (int32 I = 0; I < Count && Bits.Num() < 360; ++I)
    {
        FBit Bit;
        const float A = Direction + FMath::FRandRange(-Spread * .5f, Spread * .5f), V = Speed * FMath::FRandRange(.35f, 1.f) * Scale;
        Bit.Position = Where;
        Bit.Velocity = FVector2D(FMath::Cos(A), FMath::Sin(A)) * V;
        Bit.Angle = FMath::FRandRange(0.f, 2 * PI);
        Bit.Spin = FMath::FRandRange(-10.f, 10.f);
        Bit.Size = Size * FMath::FRandRange(.6f, 1.15f) * Scale;
        Bit.Life = FMath::FRandRange(.6f, 1.15f);
        Bit.Sides = FMath::RandRange(3, 6);
        Bit.Color = Colors[FMath::RandRange(0, UE_ARRAY_COUNT(Colors) - 1)];
        Bits.Add(Bit);
    }
}

void AGWHUD::DrawBits()
{
    for (int32 I = Bits.Num() - 1; I >= 0; --I)
    {
        FBit& Bit = Bits[I];
        Bit.Age += FrameDelta;
        if (Bit.Age >= Bit.Life)
        {
            Bits.RemoveAtSwap(I, EAllowShrinking::No);
            continue;
        }
        Bit.Velocity.Y += 1100 * Scale * FrameDelta;
        Bit.Velocity *= FMath::Max(0.f, 1 - 1.6f * FrameDelta);
        Bit.Position += Bit.Velocity * FrameDelta;
        Bit.Angle += Bit.Spin * FrameDelta;
        // Paper flutters: each bit flips through its edge as it falls.
        const float Flip = FMath::Cos(Bit.Angle * 1.7f), Alpha = 1 - FMath::SmoothStep(.6f, 1.f, Bit.Age / Bit.Life);
        TArray<FVector2D> Points;
        for (int32 K = 0; K < Bit.Sides; ++K)
        {
            const float A = Bit.Angle + K * 2 * PI / Bit.Sides;
            Points.Add(Bit.Position + FVector2D(FMath::Cos(A) * Bit.Size, FMath::Sin(A) * Bit.Size * (.35f + .65f * FMath::Abs(Flip))));
        }
        Fill(Points, Bit.Color.CopyWithNewOpacity(Alpha));
        for (int32 K = 0; K < Points.Num(); ++K)
            Line(Points[K], Points[(K + 1) % Points.Num()], Ink.CopyWithNewOpacity(.8f * Alpha), FMath::Max(1.f, Scale));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The page wipe
// ---------------------------------------------------------------------------------------------------------------

void AGWHUD::CoverThen(int32 Title, TFunction<void()> Action)
{
    FGWTransition& T = FGWTransition::Get();
    if (!T.Enabled())
    {
        Action();
        return;
    }
    if (PendingAction)
        return;
    T.Close(Title, true);
    PendingAction = MoveTemp(Action);
}

int32 AGWHUD::SceneTitle(AGWGameState* S) const
{
    if (!S || !S->InMatch)
        return S && (S->IsRoom || GetNetMode() == NM_Client) ? FGWTransition::HubPage : FGWTransition::TitlePage;
    if (S->Hub())
        return FGWTransition::HubPage;
    if (S->Tutorial)
        return FGWTransition::TutorialPage;
    if (S->Expedition())
        return FGWTransition::ExpeditionPage;
    return S->Defense() ? S->Level : FGWTransition::SurvivalPage;
}

void AGWHUD::Transition(AGWGameState* S)
{
    FGWTransition& T = FGWTransition::Get();
    auto* C = Cast<AGWController>(PlayerOwner);
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    // The scene on screen: the title page, a room still on its way, or a match (its seed and rules).
    const bool Waiting = S && !S->InMatch && (S->IsRoom || GetNetMode() == NM_Client);
    const uint32 Scene = !S ? 0u : S->InMatch ? HashCombine(S->Seed, static_cast<uint32>(S->Rules) + 7u) : Waiting ? 1u : 2u;
    const bool Ready = S && (S->InMatch ? P && S->FindUnit(P->UnitId) && (!S->GridPage() || (C->WorldView && C->WorldView->Geometry.width > 0))
                                        : !Waiting);
    const double Now = FGWTransition::Now();
    if (!T.Enabled())
    {
        T.Stage = FGWTransition::EStage::Idle;
        SceneKey = Scene;
        return;
    }
    if (Scene != SceneKey)
    {
        // A new scene: the page stays covered (at once, if no wipe was running) until it is in place.
        SceneKey = Scene;
        SceneArrived = Now;
        T.Title = SceneTitle(S);
        T.AwaitChange = false;
        DeployCover = false;
        if (T.Stage != FGWTransition::EStage::Closed)
            T.SetStage(FGWTransition::EStage::Closed);
    }
    // The hub's deploy countdown: every machine in the room covers its own screen as the count runs out.
    if (S && S->Hub() && T.Stage == FGWTransition::EStage::Idle && S->DeployIn() > 0 && S->DeployIn() <= FGWTransition::CloseSeconds + .1f)
    {
        const int32 Level = S->DeployLevel;
        T.Close(Level == AGWGameMode::ExpeditionLevel ? FGWTransition::ExpeditionPage
                : Level == AGWGameMode::PlaylistLevel ? FGWTransition::PlaylistPage
                                                      : Level,
                true, 8);
        DeployCover = true;
    }
    if (DeployCover && S && S->Hub() && S->DeployIn() < 0 && T.AwaitChange && T.Age() > FGWTransition::CloseSeconds + .2)
        T.AwaitChange = false; // called off: back to the hub
    if (T.Stage == FGWTransition::EStage::Closing && T.Age() >= FGWTransition::CloseSeconds)
        T.SetStage(FGWTransition::EStage::Closed);
    if (T.Stage == FGWTransition::EStage::Closed)
    {
        if (PendingAction)
        {
            TFunction<void()> Action = MoveTemp(PendingAction);
            PendingAction = nullptr;
            Action();
        }
        // A mission's title stays up long enough to read; the hub and the title page open sooner. A room that
        // never arrives opens on its waiting card after a while.
        const double Hold = T.Title >= 0 || T.Title == FGWTransition::TutorialPage || T.Title == FGWTransition::ExpeditionPage ? 1.15
                            : T.Title == FGWTransition::HubPage                                                                 ? .55
                                                                                                                                : .35;
        const bool Awaiting = T.AwaitChange && Now < T.AwaitUntil;
        if (!Awaiting && !PendingAction && ((Ready && Now - SceneArrived >= Hold) || Now - SceneArrived > 12))
            T.SetStage(FGWTransition::EStage::Opening);
    }
    if (T.Stage == FGWTransition::EStage::Opening && T.Age() >= FGWTransition::OpenSeconds)
        T.SetStage(FGWTransition::EStage::Idle);
    if (T.Stage == FGWTransition::EStage::Idle)
        return;

    // Triangle tiles sweep across from the left: in, a beat, then out the same way like a page sliding past.
    const bool Opening = T.Stage == FGWTransition::EStage::Opening;
    const float Age = static_cast<float>(T.Age());
    const float TileTime = Opening ? .3f : .25f, Sweep = (Opening ? FGWTransition::OpenSeconds : FGWTransition::CloseSeconds) - TileTime;
    const float Side = 150 * Scale, Tall = Side * .8660254f;
    const int32 Rows = FMath::CeilToInt(Height / Tall) + 1, Cols = FMath::CeilToInt(Width / (Side * .5f)) + 2;
    const FLinearColor Accents[] = {Coral, Teal, Marigold, Cobalt, Mint};
    float Cover = 0;
    int32 Tiles = 0;
    for (int32 Row = 0; Row < Rows; ++Row)
        for (int32 Col = -1; Col <= Cols; ++Col)
        {
            const bool Up = ((Col + Row) & 1) == 0;
            const float X = Col * Side * .5f, Y0 = Row * Tall, Y1 = Y0 + Tall;
            const FVector2D A = Up ? FVector2D(X - Side * .5f, Y1) : FVector2D(X - Side * .5f, Y0);
            const FVector2D B = Up ? FVector2D(X + Side * .5f, Y1) : FVector2D(X + Side * .5f, Y0);
            const FVector2D Tip = Up ? FVector2D(X, Y0) : FVector2D(X, Y1);
            const FVector2D Mid = (A + B + Tip) / 3;
            const uint32 Id = static_cast<uint32>((Row + 7) * 997 + Col + 13);
            const float Delay = (GWEase::Clamp01(static_cast<float>(Mid.X + Mid.Y * .55f) / (Width + Height * .55f)) * .9f + Hash01(Id) * .1f) * Sweep;
            float Grow = 1, Turn = 0;
            if (T.Stage == FGWTransition::EStage::Closing)
            {
                // Each tile unfolds from its middle with a quarter turn and settles into the tiling.
                const float K = GWEase::Clamp01((Age - Delay) / TileTime);
                Grow = GWEase::OutBack(K, .9f);
                Turn = (1 - GWEase::OutCubic(K)) * 50;
            }
            else if (Opening)
            {
                // ...and folds away into its middle again, turning the other way.
                const float K = GWEase::Clamp01((Age - Delay) / TileTime);
                Grow = 1 - GWEase::InBack(K, .8f);
                Turn = -GWEase::InCubic(K) * 40;
            }
            ++Tiles;
            Cover += GWEase::Clamp01(Grow);
            if (Grow <= .01f)
                continue;
            const float Rad = FMath::DegreesToRadians(Turn), Cs = FMath::Cos(Rad) * Grow * 1.035f, Sn = FMath::Sin(Rad) * Grow * 1.035f;
            auto Place = [&](const FVector2D& V)
            {
                const FVector2D D = V - Mid;
                return Mid + FVector2D(D.X * Cs - D.Y * Sn, D.X * Sn + D.Y * Cs);
            };
            const TArray<FVector2D> Points = {Place(A), Place(B), Place(Tip)};
            const float Pick = Hash01(Id * 31u + 5u);
            const FLinearColor Tint = Pick < .07f ? Accents[static_cast<int32>(Hash01(Id * 7u) * 5) % 5]
                                      : Pick < .2f ? PaperDeep
                                      : Up       ? Paper
                                                 : PaperShade;
            Fill(Points, Tint);
            if (Grow > .4f)
                for (int32 K = 0; K < 3; ++K)
                    Line(Points[K], Points[(K + 1) % 3], Ink.CopyWithNewOpacity(.16f * GWEase::Clamp01(Grow)), FMath::Max(1.f, 1.2f * Scale));
        }
    Cover = Tiles > 0 ? Cover / Tiles : 1;
    // The title card rides on the covered page.
    const float CardShow = T.Stage == FGWTransition::EStage::Closed ? 1.f
                       : Opening                               ? 1 - GWEase::Clamp01(Age / .22f)
                                                               : GWEase::Clamp01((Cover - .75f) / .25f);
    if (CardShow > 0)
        TitleCard(S, CardShow);
}

void AGWHUD::TitleCard(AGWGameState* S, float Show)
{
    const FGWTransition& T = FGWTransition::Get();
    FText Big, Small;
    FLinearColor Accent = Coral;
    int32 GlyphKind = 4;
    if (T.Title >= 0)
    {
        // A page: its number large, its name under it, in the chapter's colour.
        Big = PageCode(T.Title);
        Small = GWText::Get(TEXT("Level.") + Big.ToString());
        const FLinearColor Chapters[] = {Teal, Marigold, Plum};
        Accent = Chapters[FMath::Clamp(static_cast<int32>(Big.ToString().Left(1).IsNumeric() ? FCString::Atoi(*Big.ToString().Left(1)) - 1 : 0), 0, 2)];
        GlyphKind = 19;
    }
    else if (T.Title == FGWTransition::HubPage)
    {
        Big = GWText::Get(TEXT("Hub.Mine"));
        if (S && S->InMatch && !LocalHost(S))
            for (const auto& Item : S->PlayerArray)
                if (const auto* PS = Cast<AGWPlayerState>(Item); PS && PS->Identity == S->HostIdentity)
                {
                    FFormatNamedArguments Args;
                    Args.Add(TEXT("Name"), FText::FromString(PS->GetPlayerName()));
                    Big = FText::Format(GWText::Get(TEXT("Hub.Title")), Args);
                }
        Accent = Teal;
        GlyphKind = 4;
    }
    else if (T.Title == FGWTransition::TutorialPage)
    {
        Big = GWText::Get(TEXT("Tutorial.Title"));
        Accent = Marigold;
        GlyphKind = 16;
    }
    else if (T.Title == FGWTransition::ExpeditionPage)
    {
        // A floor of the expedition: its stage-floor in big figures, the stage's pen under it, in the pen's colour (the
        // wipe covers every portal ride, and the next floor is laid out under it).
        if (S && S->Expedition())
        {
            Big = FText::FromString(FString::Printf(TEXT("%d-%d"), S->ExpStage, S->ExpFloor));
            Small = FText::FromString(GWText::Get(TEXT("Board.Mode.1")).ToString() + TEXT("  ·  ") +
                                      GWText::Get(FString::Printf(TEXT("Biome.%d"), static_cast<int32>(S->Biome()))).ToString());
            Accent = Biome(static_cast<int32>(S->Biome()));
        }
        else
        {
            Big = GWText::Get(TEXT("Board.Mode.1"));
            Accent = Cobalt;
        }
        GlyphKind = 18;
    }
    else if (T.Title == FGWTransition::PlaylistPage)
    {
        Big = GWText::Get(TEXT("Board.Mode.2"));
        Accent = Magenta;
        GlyphKind = 15;
    }
    else
    {
        Big = GWText::Get(TEXT("Game.Title"));
        Accent = Coral;
        GlyphKind = 3;
    }
    float BigSize = (T.Title >= 0 || (T.Title == FGWTransition::ExpeditionPage && S && S->Expedition()) ? 112 : 64) * Scale;
    const float SmallSize = 30 * Scale;
    // A long name (the game's own in Japanese, "X's hub" with a long Steam name) shrinks to keep its card on the screen.
    const float BigRoom = Width - 280 * Scale, BigWide = LetteringWidth(Big.ToString(), BigSize);
    if (BigWide > BigRoom && BigRoom > 0)
        BigSize *= BigRoom / BigWide * .98f;
    const FVector2D BigExtent(LetteringWidth(Big.ToString(), BigSize), Measure(Big, BigSize, EFace::Heavy).Y);
    const float W = FMath::Max(BigExtent.X + 160 * Scale, 460 * Scale), H = BigExtent.Y + (Small.IsEmpty() ? 120 : 170) * Scale;
    const float X = (Width - W) * .5f, Y = (Height - H) * .46f;
    const auto Motion = PopIn(FVector2D(Width * .5f, Height * .5f), SinceRaw(FCrc::StrCrc32(TEXT("Transition.Card"))));
    const float Keep = Fade;
    Fade *= Show;
    Card(X, Y, W, H, Paper, Ink, 1501, 2.4f, 2.6f * Scale);
    Tape(FVector2D(X + W * .5f, Y + 4 * Scale), 150 * Scale, 30 * Scale, -3, Accent.CopyWithNewOpacity(.9f));
    Highlight(X + (W - BigExtent.X) * .5f - 12 * Scale, Y + 28 * Scale + BigExtent.Y * .62f, BigExtent.X + 24 * Scale, BigExtent.Y * .24f,
              Accent.CopyWithNewOpacity(.45f), 1502);
    Lettering(Big, X + W * .5f, Y + 28 * Scale, BigSize, Ink, 1503, SinceRaw(FCrc::StrCrc32(TEXT("Transition.Letters"))));
    if (!Small.IsEmpty())
        Label(Small, X + W * .5f, Y + 36 * Scale + BigExtent.Y, SmallSize, InkSoft, true, W - 60 * Scale, EFace::Bold);
    // The loader: a shape gaining sides (triangle to hexagon) as it turns, between two compass points.
    const float Turn = static_cast<float>(Clock) * 2.2f;
    const int32 Sides = 3 + static_cast<int32>(FMath::Fmod(static_cast<float>(Clock) * 2.5f, 4.f));
    const FVector2D Spot(X + W * .5f, Y + H - 40 * Scale);
    TArray<FVector2D> Shape;
    for (int32 K = 0; K < Sides; ++K)
        Shape.Add(Spot + FVector2D(FMath::Cos(Turn + K * 2 * PI / Sides), FMath::Sin(Turn + K * 2 * PI / Sides)) * 15 * Scale);
    Polygon(Shape, Accent, Ink, 1.8f * Scale, 1504);
    for (const float Side : {-1.f, 1.f})
        Glyph(GlyphKind, Spot + FVector2D(Side * 46 * Scale, 0), 16 * Scale, PaperDeep, 1505);
    Fade = Keep;
}
