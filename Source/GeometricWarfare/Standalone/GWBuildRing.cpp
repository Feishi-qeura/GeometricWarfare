// The build ring of a defense page (design: 点击格子弹出圆环): a click on open floor in the build phase (or F) pops
// a ring of paper wedges round the cell, one per build that fits there; on a tower its upgrade and rubbing it out,
// on wire rubbing it out. The ring opens like a jelly (too big, then settling), a hovered wedge lifts and wobbles,
// the middle says what it does and the world sketches its reach on the floor; a click takes it and the ring folds
// away in a pinch of paper.
#include "Core/Defense.h"
#include "Core/Hub.h"
#include "GWController.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWMotion.h"
#include "GWPalette.h"
#include "GWPlayerState.h"
#include "GWText.h"
#include "GWWorld.h"

using namespace GWPalette;

namespace
{
// Measures at Scale 1: the paper hub in the middle, the wedges' inner and outer edges, each wedge's sweep
// (degrees), and how high over a tower's cell the ring is pinned (its body, not its foot).
constexpr float RingHub = 44, RingInner = 56, RingOuter = 126, RingSweep = 76, RingAnchor = 200;
constexpr double RingFoldSeconds = .22;
const FLinearColor RingWireColor = Hex(0x5E6678), RingDropColor = Hex(0x2B3A67), RingRubber = Hex(0xEE9B9B);

FLinearColor RingColor(AGWHUD::ERingSlot Kind)
{
    switch (Kind)
    {
    case AGWHUD::ERingSlot::Bolt:
        return Marigold;
    case AGWHUD::ERingSlot::Mortar:
        return Fire;
    case AGWHUD::ERingSlot::Tesla:
        return Cobalt;
    case AGWHUD::ERingSlot::Wire:
        return RingWireColor;
    case AGWHUD::ERingSlot::Upgrade:
        return Teal;
    default:
        return Coral;
    }
}
const TCHAR* RingName(AGWHUD::ERingSlot Kind)
{
    switch (Kind)
    {
    case AGWHUD::ERingSlot::Bolt:
        return TEXT("Build.0");
    case AGWHUD::ERingSlot::Mortar:
        return TEXT("Build.1");
    case AGWHUD::ERingSlot::Tesla:
        return TEXT("Build.2");
    case AGWHUD::ERingSlot::Wire:
        return TEXT("Build.3");
    case AGWHUD::ERingSlot::Upgrade:
        return TEXT("Ring.Upgrade");
    default:
        return TEXT("Ring.Remove");
    }
}
int32 RingCellAt(const AGWGameState* S, const FVector& P)
{
    if (S->CellSize <= 0 || S->GridWidth <= 0)
        return -1;
    const int32 X = FMath::FloorToInt(P.X / S->CellSize), Y = FMath::FloorToInt(P.Y / S->CellSize);
    return X >= 0 && Y >= 0 && X < S->GridWidth && Y < S->GridHeight ? Y * S->GridWidth + X : -1;
}
const FGWUnitView* RingTowerOn(const AGWGameState* S, int32 Cell)
{
    for (const auto& U : S->Units)
        if (U.Alive && U.Turret && U.Kind == 1 && RingCellAt(S, U.Position) == Cell)
            return &U;
    return nullptr;
}
// Bolt 0, mortar 1, tesla 2: clients tell the kinds apart by outline (a mortar is round, a tesla coil a triangle).
int32 RingTowerKind(const FGWUnitView& U)
{
    return U.Shape == static_cast<uint8>(gwgame::Shape::Circle) ? 1 : U.Shape == static_cast<uint8>(gwgame::Shape::Triangle) ? 2 : 0;
}
FVector2D RingPolar(float Degrees, float Radius)
{
    const float A = FMath::DegreesToRadians(Degrees);
    return FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius;
}
} // namespace

bool AGWHUD::RingAllowed(AGWGameState* S, const FGWUnitView* Local) const
{
    if (!S || !Local || !Local->Alive || OverlayOpen() || !S->Defense() || S->Phase != static_cast<uint8>(gwgame::DefensePhase::Build) ||
        !Cast<AGWController>(PlayerOwner))
        return false;
    // The tutorial keeps the left button for shooting its targets until it gets to towers.
    return !S->Tutorial || S->TutorialStep >= static_cast<uint8>(gwgame::TutorialStep::Tower);
}

TArray<AGWHUD::FRingSlot> AGWHUD::RingSlots(AGWGameState* S, const FGWUnitView& Local, int32 Cell) const
{
    TArray<FRingSlot> Out;
    if (!S || !S->Cells.IsValidIndex(Cell))
        return Out;
    // (A tower's post makes its cell rock while it stands, so towers are looked for before rock is ruled out.)
    const uint8 Flags = S->Cells[Cell];
    if (!(Flags & EGWCell::Paper) || (Flags & (EGWCell::Core | EGWCell::Entrance | EGWCell::Deck)))
        return Out;
    const float Worth = S->TowerWorth.IsValidIndex(Cell) ? S->TowerWorth[Cell] : 0.f;
    FRingSlot Rub;
    Rub.Kind = ERingSlot::Remove;
    Rub.Angle = 90;
    Rub.Price = Worth;
    if (const FGWUnitView* Tower = RingTowerOn(S, Cell))
    {
        // A tower: one more side (or level) on top, rubbing it out underneath.
        FRingSlot Grow;
        Grow.Kind = ERingSlot::Upgrade;
        Grow.Angle = -90;
        Grow.Tower = Rub.Tower = RingTowerKind(*Tower);
        Grow.Next = Tower->Sides + 1;
        Grow.Price = Tower->Sides <= 4 ? S->TowerFiveInk : S->TowerSixInk;
        if (Tower->Sides >= gwgame::MaxSides)
        {
            Grow.Denied = 7;
            Grow.Next = Tower->Sides;
            Grow.Price = 0;
        }
        else if (Local.Ink + 1e-3f < Grow.Price)
            Grow.Denied = 1;
        Out.Add(Grow);
        Out.Add(Rub);
        return Out;
    }
    if (S->Wire.IsValidIndex(Cell) && S->Wire[Cell] > 0)
    {
        Out.Add(Rub);
        return Out;
    }
    if (Flags & (EGWCell::Wall | EGWCell::Rock))
        return Out;
    // Open floor: the four builds clockwise from the top. Towers are posts the enemies walk round (never on
    // anyone, never the one that seals the last road); wire lies flat. The host still has the last word.
    bool Pit = false;
    for (const auto& L : S->Ladders)
        Pit |= L.Pit == Cell;
    bool Standing = false;
    const float Size = S->CellSize;
    const FVector2D Lo((Cell % S->GridWidth) * Size, (Cell / S->GridWidth) * Size);
    for (const auto& U : S->Units)
        if (U.Alive && !U.Deck)
        {
            const FVector2D Nearest(FMath::Clamp<float>(U.Position.X, Lo.X, Lo.X + Size), FMath::Clamp<float>(U.Position.Y, Lo.Y, Lo.Y + Size));
            Standing |= FVector2D::DistSquared(Nearest, FVector2D(U.Position.X, U.Position.Y)) < U.Radius * U.Radius;
        }
    const auto* C = Cast<AGWController>(PlayerOwner);
    const bool Road = !C || !C->WorldView || C->WorldView->RoadStaysOpen(Cell);
    const float Prices[4] = {S->TowerInk, S->MortarInk, S->TeslaInk, S->WireInk};
    for (int32 I = 0; I < 4; ++I)
    {
        FRingSlot Slot;
        Slot.Kind = static_cast<ERingSlot>(I);
        Slot.Angle = -90.f + 90.f * I;
        Slot.Price = Prices[I];
        const bool Post = I < 3;
        Slot.Denied = Pit ? 4 : Post && Standing ? 3 : Post && !Road ? 2 : Local.Ink + 1e-3f < Slot.Price ? 1 : 0;
        Out.Add(Slot);
    }
    return Out;
}

bool AGWHUD::OpenRing(int32 Cell)
{
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    auto* C = Cast<AGWController>(PlayerOwner);
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    const FGWUnitView* Local = S && P ? S->FindUnit(P->UnitId) : nullptr;
    if (!C || !RingAllowed(S, Local) || RingSlots(S, *Local, Cell).IsEmpty())
        return false;
    if (C->RingCell != Cell)
    {
        C->RingCell = Cell;
        RingOpenedAt = Clock;
        RingClosedAt = -10;
        RingTaken = RingShake = -1;
    }
    return true;
}

void AGWHUD::ToggleRing(int32 Cell)
{
    const auto* C = Cast<AGWController>(PlayerOwner);
    if (C && C->RingCell == Cell)
        CloseRing();
    else
        OpenRing(Cell);
}

void AGWHUD::CloseRing()
{
    auto* C = Cast<AGWController>(PlayerOwner);
    if (!C || C->RingCell < 0)
        return;
    C->RingCell = C->RingPreview = -1;
    RingClosedAt = Clock;
    RingTaken = -1;
}

void AGWHUD::TakeRingSlot(int32 Index)
{
    auto* C = Cast<AGWController>(PlayerOwner);
    if (!C || C->RingCell < 0 || !RingShown.IsValidIndex(Index))
        return;
    const FRingSlot Slot = RingShown[Index];
    // A wedge that cannot be taken shakes its head (and the notice line says why).
    if (Slot.Denied)
    {
        RingShake = Index;
        RingShakeAt = Clock;
        if (Slot.Denied <= 4)
            C->NoticeKey = FString::Printf(TEXT("Defense.Denied.%d"), Slot.Denied);
        return;
    }
    const int32 Cell = C->RingCell;
    if (Slot.Kind == ERingSlot::Remove)
        C->RemoveAt(Cell);
    else
        C->BuildAt(Cell, Slot.Kind == ERingSlot::Upgrade ? Slot.Tower : static_cast<int32>(Slot.Kind));
    // The taken wedge flies apart in paper as the ring folds away.
    Confetti(RingCenter + RingPolar(Slot.Angle, (RingInner + RingOuter) * .5f * Scale), 14, 640, 6);
    CloseRing();
    RingTaken = Index;
}

int32 AGWHUD::RingSlotAt(FVector2D Point) const
{
    const FVector2D D = Point - RingCenter;
    const float R = D.Size() / Scale;
    if (R < RingInner - 6 || R > RingOuter + 18)
        return -1;
    const float A = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
    for (int32 I = 0; I < RingShown.Num(); ++I)
        if (FMath::Abs(FMath::FindDeltaAngleDegrees(RingShown[I].Angle, A)) <= RingSweep * .5f + 5)
            return I;
    return -1;
}

bool AGWHUD::RingClick(float X, float Y)
{
    const auto* C = Cast<AGWController>(PlayerOwner);
    if (!C || C->RingCell < 0)
        return false;
    const FVector2D Point(X, Y);
    if (const int32 Index = RingSlotAt(Point); Index >= 0)
    {
        TakeRingSlot(Index);
        return true;
    }
    // Its middle (or a gap between wedges) folds it away; a click anywhere else folds it and goes on.
    const bool Inside = FVector2D::Distance(Point, RingCenter) <= (RingOuter + 18) * Scale;
    CloseRing();
    return Inside;
}

bool AGWHUD::RingFromClick()
{
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    auto* C = Cast<AGWController>(PlayerOwner);
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    const FGWUnitView* Local = S && P ? S->FindUnit(P->UnitId) : nullptr;
    if (!C || !RingAllowed(S, Local) || C->HoverCell < 0 || C->HoverMate >= 0)
        return false;
    // A click on someone (a paper target, a teammate) is a shot, not a build.
    FVector Hit;
    if (C->CursorWorld(Hit))
        for (const auto& U : S->Units)
            if (U.Alive && !U.Turret && U.Id != Local->Id && FVector::Dist2D(Hit, U.Position) < U.Radius * 1.25f + 20)
                return false;
    return OpenRing(C->HoverCell);
}

void AGWHUD::BuildRing(AGWGameState* S, const FGWUnitView* Local)
{
    auto* C = Cast<AGWController>(PlayerOwner);
    if (!C || !S)
        return;
    C->RingPreview = -1;
    if (C->RingCell >= 0)
    {
        // Whatever ends the build phase (or the player's part in it, or the floor under the ring) folds it away.
        TArray<FRingSlot> Slots;
        if (RingAllowed(S, Local))
            Slots = RingSlots(S, *Local, C->RingCell);
        const int32 Cell = C->RingCell;
        const FVector World((Cell % FMath::Max(1, S->GridWidth) + .5f) * S->CellSize, (Cell / FMath::Max(1, S->GridWidth) + .5f) * S->CellSize,
                            RingTowerOn(S, Cell) ? RingAnchor : 0.f);
        FVector2D Spot;
        if (Slots.IsEmpty() || !WorldToScreen(S, World, Spot))
        {
            CloseRing();
            return;
        }
        RingShown = MoveTemp(Slots);
        // Pinned over its cell, kept whole on screen (above the key strip); a dotted leader points back at a cell
        // too near the edge for it.
        const float Margin = (RingOuter + 16) * Scale;
        RingCenter = FVector2D(FMath::Clamp<float>(Spot.X, Margin, Width - Margin), FMath::Clamp<float>(Spot.Y, Margin, Height - Margin - 46 * Scale));
        const float Gap = FVector2D::Distance(RingCenter, Spot);
        if (Gap > (RingHub + 12) * Scale)
        {
            const FVector2D Dir = (Spot - RingCenter) / Gap;
            for (float D = (RingHub + 8) * Scale; D < Gap; D += 12 * Scale)
                Disc(RingCenter + Dir * D, 2.4f * Scale, Ink.CopyWithNewOpacity(.6f), 8);
            Glyph(6, Spot, 16 * Scale, Marigold, 1590);
        }
        DrawRing(S, GWEase::Clamp01(static_cast<float>(Clock - RingOpenedAt) / .3f), 0);
        return;
    }
    const float Fold = static_cast<float>((Clock - RingClosedAt) / RingFoldSeconds);
    if (Fold < 1 && RingShown.Num())
        DrawRing(S, 1, FMath::Max(Fold, .001f));
}

void AGWHUD::DrawRing(AGWGameState* S, float Open, float Fold)
{
    auto* C = Cast<AGWController>(PlayerOwner);
    const FVector2D O = RingCenter;
    // The hovered wedge (none while folding).
    int32 Hover = -1;
    float MX = 0, MY = 0;
    if (Fold <= 0)
    {
        if (RingHoverOverride >= 0)
            Hover = RingShown.IsValidIndex(RingHoverOverride) ? RingHoverOverride : -1;
        else if (PlayerOwner && PlayerOwner->GetMousePosition(MX, MY))
            Hover = RingSlotAt(FVector2D(MX, MY));
    }
    // The whole ring pops open like jelly (too big, then settling) with a quarter turn, and folds small.
    const float Grow = Fold > 0 ? 1 - .45f * GWEase::InCubic(Fold) : .3f + .7f * GWEase::OutBack(Open, 2.6f);
    const float Turn = Fold > 0 ? Fold * 30 : (1 - GWEase::OutCubic(Open)) * -40;
    const float Alpha = Fold > 0 ? 1 - Fold : GWEase::Clamp01(Open * 3);
    PushMotion(O, FVector2D::ZeroVector, Turn, Grow, Alpha);
    const FMotionGuard Whole(this);
    // A pencil track round the whole ring under the wedges, and the paper hub in the middle.
    Circle(O, (RingInner + RingOuter) * .5f * Scale, Pencil.CopyWithNewOpacity(.5f), 1.4f * Scale, 1500);
    Disc(O, RingHub * Scale, Paper.CopyWithNewOpacity(.97f));
    Circle(O, RingHub * Scale, Ink, 1.8f * Scale, 1501);
    for (int32 I = 0; I < RingShown.Num(); ++I)
    {
        const FRingSlot& Slot = RingShown[I];
        // Dealt out one after another from the hub, clockwise.
        const float Deal = Fold > 0 ? 1.f : GWEase::Clamp01((static_cast<float>(Clock - RingOpenedAt) - .045f * I) / .22f);
        const bool Hot = I == Hover;
        const uint32 Id = HashCombine(0x81A6u, static_cast<uint32>(I));
        const float Lift = Eased(Id, Hot ? 1.f : 0.f, 16);
        // Hovering sets it wobbling like jelly; a refused click shakes it no.
        const float Wobble = Hot ? .12f * GWEase::Wobble(Since(HashCombine(Id, 0x407u)), 26, 8) : 0.f;
        float Shake = 0;
        if (I == RingShake && Clock - RingShakeAt < .5)
        {
            const float T = static_cast<float>(Clock - RingShakeAt);
            Shake = FMath::Sin(T * 55) * 8 * FMath::Exp(-T * 7) * Scale;
        }
        const float Taken = I == RingTaken && Fold > 0 ? Fold : 0.f;
        const FVector2D Dir = RingPolar(Slot.Angle, 1);
        const FVector2D Mid = O + Dir * (RingInner + RingOuter) * .5f * Scale;
        const float Out = (Lift * 10 - (1 - GWEase::OutBack(Deal, 2.2f)) * 36 + Taken * 26) * Scale;
        PushMotion(Mid, Dir * Out + FVector2D(Shake, 0), 0, (1 + .05f * Lift + Wobble) * (1 + .35f * Taken), Deal * (1 - Taken));
        const FMotionGuard Wedge(this);
        // The wedge: a faceted band of paper (two flat facets outside, one inside), inked round, with its colour as
        // a marker stripe along the outer edge.
        const float A0 = Slot.Angle - RingSweep * .5f, A1 = Slot.Angle + RingSweep * .5f;
        const TArray<FVector2D> Points = {O + RingPolar(A0, RingInner * Scale), O + RingPolar(A0, RingOuter * Scale),
                                          O + RingPolar(Slot.Angle, (RingOuter + 7) * Scale), O + RingPolar(A1, RingOuter * Scale),
                                          O + RingPolar(A1, RingInner * Scale)};
        const FLinearColor Accent = RingColor(Slot.Kind);
        const FLinearColor Face = Slot.Denied ? PaperShade : Hot ? FMath::Lerp(Paper, Accent, .22f) : Paper;
        Polygon(Points, Face.CopyWithNewOpacity(.97f), Ink, (Hot ? 2.6f : 1.7f) * Scale, 1510 + I * 7);
        const float Band = (RingOuter - 8) * Scale;
        const FLinearColor Stripe = Accent.CopyWithNewOpacity(Slot.Denied ? .3f : .9f);
        Pen(O + RingPolar(A0 + 5, Band), O + RingPolar(Slot.Angle, Band + 7 * Scale), Stripe, 5.5f * Scale, 1530 + I, .25f);
        Pen(O + RingPolar(Slot.Angle, Band + 7 * Scale), O + RingPolar(A1 - 5, Band), Stripe, 5.5f * Scale, 1540 + I, .25f);
        // Its icon and its ink: the price (red when short), what rubbing out pays back, nothing when grown full. The
        // top and bottom wedges stack them along the ring (icon outside, ink inside, clear of the stripe); the side
        // ones put the ink under the icon.
        const bool Upright = FMath::Abs(Dir.Y) > .7f;
        const FVector2D Icon = Upright ? O + Dir * 97 * Scale : Mid - FVector2D(0, 9 * Scale);
        RingIcon(Slot, Icon, 32 * Scale, Slot.Denied ? Pencil : Accent, 1550 + I * 5);
        if (Slot.Denied != 7)
        {
            const FText Price = Slot.Kind == ERingSlot::Remove ? FText::FromString(FString::Printf(TEXT("+%d"), FMath::RoundToInt(Slot.Price)))
                                                               : FText::AsNumber(FMath::RoundToInt(Slot.Price));
            const float TW = Measure(Price, 13 * Scale, EFace::Bold).X;
            const FVector2D Under = Upright ? O + Dir * 69 * Scale - FVector2D(0, 7 * Scale) : Icon + FVector2D(0, 25 * Scale);
            Glyph(23, FVector2D(Under.X - TW * .5f - 3 * Scale, Under.Y + 8 * Scale), 11 * Scale, RingDropColor, 1570 + I);
            Label(Price, Under.X + 5 * Scale, Under.Y, 13 * Scale, Slot.Denied == 1 ? Danger : InkSoft, true, 0, EFace::Bold);
        }
    }
    // The middle: what the hovered wedge does, or what stands on the cell.
    const float NameW = (RingHub * 2 - 14) * Scale;
    if (RingShown.IsValidIndex(Hover))
    {
        const FRingSlot& Slot = RingShown[Hover];
        Key(RingName(Slot.Kind), O.X, O.Y - 18 * Scale, 15 * Scale, Ink, true, NameW, EFace::Bold);
        FText Note;
        if (Slot.Denied)
            Note = GWText::Get(*FString::Printf(TEXT("Ring.Denied.%d"), Slot.Denied));
        else if (Slot.Kind == ERingSlot::Remove)
            Note = GWText::Number(TEXT("Ring.Refund"), TEXT("Ink"), FMath::RoundToInt(Slot.Price));
        else if (Slot.Kind == ERingSlot::Upgrade)
            Note = Slot.Tower == 0 ? GWText::Number(TEXT("Ring.Sides"), TEXT("Sides"), Slot.Next) : GWText::Number(TEXT("Ring.Level"), TEXT("Level"), Slot.Next - 3);
        else
            Note = GWText::Number(TEXT("Build.Cost"), TEXT("Ink"), FMath::RoundToInt(Slot.Price));
        Label(Note, O.X, O.Y + 3 * Scale, 12 * Scale, Slot.Denied ? Danger : InkSoft, true, NameW, EFace::Bold);
        // Its reach, sketched on the floor round the cell by the world.
        if (C && !Slot.Denied && Slot.Kind != ERingSlot::Remove)
        {
            C->RingPreview = Slot.Kind == ERingSlot::Upgrade ? Slot.Tower : static_cast<int32>(Slot.Kind);
            C->RingPreviewSides = Slot.Kind == ERingSlot::Upgrade ? Slot.Next : 4;
        }
    }
    else if (const FGWUnitView* Tower = C && C->RingCell >= 0 ? RingTowerOn(S, C->RingCell) : nullptr)
    {
        // A tower: its name and three pips for its level.
        Key(*FString::Printf(TEXT("Build.%d"), RingTowerKind(*Tower)), O.X, O.Y - 18 * Scale, 15 * Scale, Ink, true, NameW, EFace::Bold);
        const int32 Level = FMath::Clamp(Tower->Sides - 3, 1, 3);
        for (int32 K = 0; K < 3; ++K)
            Glyph(6, O + FVector2D((K - 1) * 15 * Scale, 12 * Scale), 12 * Scale, K < Level ? Teal : PaperShade, 1580 + K);
    }
    else if (C && C->RingCell >= 0 && S->Wire.IsValidIndex(C->RingCell) && S->Wire[C->RingCell] > 0)
    {
        Key(TEXT("Build.3"), O.X, O.Y - 18 * Scale, 15 * Scale, Ink, true, NameW, EFace::Bold);
        Label(GWText::Number(TEXT("Ring.WireLeft"), TEXT("Left"), FMath::RoundToInt(S->Wire[C->RingCell] * 100 / 15.f)), O.X, O.Y + 3 * Scale, 12 * Scale,
              InkSoft, true, NameW, EFace::Bold);
    }
    else
        Glyph(7, O, 26 * Scale, PaperDeep, 1585);
}

void AGWHUD::RingIcon(const FRingSlot& Slot, FVector2D C, float Size, FLinearColor Color, int32 Seed)
{
    const float R = Size * .5f;
    switch (Slot.Kind)
    {
    case ERingSlot::Bolt:
        // The bolt tower: a square block that grows sides, with its bolt sticking out.
        Pen(C, C + FVector2D(R * 1.05f, -R * .2f), Ink, Size * .12f, Seed, .2f);
        Glyph(1, C, Size * .72f, Color, Seed + 1);
        break;
    case ERingSlot::Mortar:
        // The mortar: a round body with a stubby barrel, and the dotted lob of its shell.
        Pen(C, C + FVector2D(R * .75f, -R * .75f), Ink, Size * .22f, Seed, .2f);
        Glyph(0, C + FVector2D(0, R * .12f), Size * .78f, Color, Seed + 1);
        for (int32 K = 0; K < 3; ++K)
            Disc(C + FVector2D(R * (.55f + .3f * K), -R * (1.05f - .25f * K * K)), Size * .045f, Ink, 8);
        break;
    case ERingSlot::Tesla:
        // The tesla coil: a triangle with a spark on top.
        Glyph(3, C + FVector2D(0, R * .1f), Size * .9f, Color, Seed);
        Glyph(11, C + FVector2D(0, -R * .05f), Size * .42f, Spark, Seed + 1);
        break;
    case ERingSlot::Wire:
        // Barbed wire: three coils with barbs.
        for (int32 K = 0; K < 3; ++K)
        {
            const FVector2D At = C + FVector2D((K - 1) * R * .62f, (K % 2 ? -1 : 1) * R * .12f);
            Circle(At, R * .36f, Color, FMath::Max(1.5f, Size * .07f), Seed + K);
            Pen(At + FVector2D(-R * .14f, -R * .14f), At + FVector2D(R * .14f, R * .14f), Ink, FMath::Max(1.2f, Size * .05f), Seed + 4 + K, .2f);
        }
        break;
    case ERingSlot::Upgrade:
    {
        // The shape the tower grows into (a bolt tower's next polygon), with a chevron pointing up.
        const int32 Glyphs[3] = {Slot.Next >= 6 ? 4 : 21, 0, 3};
        Glyph(Glyphs[FMath::Clamp(Slot.Tower, 0, 2)], C + FVector2D(-R * .12f, R * .12f), Size * .7f, Color, Seed);
        const FVector2D Tip = C + FVector2D(R * .55f, -R * .7f);
        Polygon({Tip, Tip + FVector2D(R * .42f, R * .5f), Tip + FVector2D(-R * .42f, R * .5f)}, Slot.Denied ? Pencil : Marigold, Ink,
                FMath::Max(1.4f, Size * .05f), Seed + 1);
        break;
    }
    default:
    {
        // Rubbing out: a tilted eraser with a paper sleeve, crumbs under it.
        auto Turned = [&](float X, float Y)
        {
            const float A = FMath::DegreesToRadians(-28.f);
            return C + FVector2D(X * FMath::Cos(A) - Y * FMath::Sin(A), X * FMath::Sin(A) + Y * FMath::Cos(A)) * R;
        };
        Polygon({Turned(-.85f, -.38f), Turned(.05f, -.38f), Turned(.05f, .38f), Turned(-.85f, .38f)}, Slot.Denied ? PaperShade : RingRubber, Ink,
                FMath::Max(1.4f, Size * .05f), Seed);
        Polygon({Turned(.05f, -.38f), Turned(.85f, -.38f), Turned(.85f, .38f), Turned(.05f, .38f)}, Color, Ink, FMath::Max(1.4f, Size * .05f),
                Seed + 1);
        for (int32 K = 0; K < 3; ++K)
            Disc(C + FVector2D((K - 1) * R * .45f - R * .3f, R * (.78f + .08f * K)), Size * .04f, Pencil, 6);
        break;
    }
    }
}
