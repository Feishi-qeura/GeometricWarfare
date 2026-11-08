#include "Engine/Canvas.h"
#include "GWCinematic.h"
#include "GWController.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWPalette.h"
#include "GWPlayerState.h"
#include "GWText.h"
#include "GWWorld.h"

// In-match sketchbook HUD: a pinned level board, paper cards for the player, their squad and their
// teammates, world-space prompts and the paused upgrade index cards.
using namespace GWPalette;

namespace
{
FText Timer(float Seconds)
{
    const int32 S = FMath::Max(0, FMath::CeilToInt(Seconds));
    return FText::FromString(FString::Printf(TEXT("%02d:%02d"), S / 60, S % 60));
}
int32 ShapeGlyph(uint8 Shape)
{
    switch (Shape)
    {
    case 1:
        return 1;
    case 2:
        return 2;
    case 3:
        return 3;
    case 4:
        return 21;
    case 5:
        return 4;
    case 6:
        return 22;
    default:
        return 0;
    }
}
} // namespace

bool AGWHUD::WorldToScreen(AGWGameState*, FVector World, FVector2D& Screen) const
{
    return PlayerOwner && PlayerOwner->ProjectWorldLocationToScreen(World, Screen);
}

FText AGWHUD::TeammateName(AGWGameState* S, const FGWUnitView& Unit) const
{
    if (Unit.Bot)
    {
        // Bots are numbered by join order, which never changes during a run.
        int32 Index = 1;
        for (const auto& Other : S->Units)
            Index += Other.Bot && Other.Id < Unit.Id;
        return GWText::Number(TEXT("Team.Bot"), TEXT("Index"), Index);
    }
    for (const auto& State : S->PlayerArray)
        if (auto* PS = Cast<AGWPlayerState>(State); PS && PS->UnitId == Unit.Id)
            return FText::FromString(PS->GetPlayerName());
    return FText::GetEmpty();
}

void AGWHUD::Combat(AGWGameState* S)
{
    DrawCombatFeedback(S);
    auto* P = PlayerOwner->GetPlayerState<AGWPlayerState>();
    const auto* U = P ? S->FindUnit(P->UnitId) : nullptr;
    if (!S->Finished)
    {
        // A soft paper vignette frames the 3D diorama like a page.
        for (int32 I = 0; I < 4; ++I)
        {
            const float T = 30 * Scale * (4 - I);
            const FLinearColor Edge = Ink.CopyWithNewOpacity(.022f);
            Box(0, 0, Width, T * .5f, Edge);
            Box(0, Height - T * .5f, Width, T * .5f, Edge);
            Box(0, 0, T * .5f, Height, Edge);
            Box(Width - T * .5f, 0, T * .5f, Height, Edge);
        }
    }
    if (FGWCinematic::Get().Clean)
        return;
    WorldTags(S, U);
    PrankTags(S, U);
    // The hub has no board of its own (its room card is drawn with the hub screen below).
    if (S->Hub())
    {
    }
    else if (S->Defense())
    {
        DefenseBoard(S, U);
        DefenseBanners(S);
    }
    else
        TopBoard(S);
    if (U)
    {
        PlayerCard(S, *U);
        SquadCard(S, *U);
        // In the hub the roster lists everyone; in the tutorial there is nobody else.
        if (!S->Hub() && !S->Tutorial)
            TeamCard(S, *U);
        if (!U->Alive && !S->Finished)
        {
            Box(0, 0, Width, Height, Ink.CopyWithNewOpacity(.28f));
            const float W = 520 * Scale, H = 120 * Scale, X = (Width - W) * .5f, Y = Height * .4f;
            Card(X, Y, W, H, Paper, Ink, 811, 1.6f);
            Tape(FVector2D(X + 40 * Scale, Y + 6 * Scale), 70 * Scale, 20 * Scale, -30, Coral);
            const bool Returns = S->Defense() || S->Hub();
            Glyph(Returns ? 6 : 17, FVector2D(X + 72 * Scale, Y + H * .5f), 50 * Scale, Returns ? Teal : Coral, 813);
            // Defense brings fallen players back at the core (the hub at its spawn); Survival has no respawn.
            if (Returns)
                Label(GWText::Number(TEXT("Defense.Respawn"), TEXT("Seconds"), FMath::Max(1, FMath::CeilToInt(U->Respawn))), X + 128 * Scale,
                      Y + H * .5f - 16 * Scale, 26 * Scale, Ink, false, W - 150 * Scale, EFace::Bold);
            else
                Key(TEXT("Combat.NoRespawn"), X + 128 * Scale, Y + H * .5f - 16 * Scale, 26 * Scale, Ink, false, W - 150 * Scale, EFace::Bold);
        }
    }
    if (S->Defense() && !S->Finished && !S->Tutorial)
        DefenseHints(S, U);
    Controls();
    // Settings tab: a paper corner with a geometric gear.
    {
        const float X = 22 * Scale, Y = 18 * Scale, Sz = 56 * Scale;
        const bool Hover = Hovered(X, Y, Sz, Sz);
        Card(X, Y - (Hover ? 2 * Scale : 0), Sz, Sz, Paper, Ink, 44, Hover ? 1.6f : 1.1f);
        Glyph(10, FVector2D(X + Sz * .5f, Y + Sz * .5f - (Hover ? 2 * Scale : 0)), Sz * .62f, Hover ? Marigold : PaperShade, 45);
        Hits.Add({FBox2D(FVector2D(X, Y), FVector2D(X + Sz, Y + Sz)), 4});
    }
    if (S->SharedLevel > SeenLevel && SeenLevel > 0)
        LevelBanner = Clock;
    SeenLevel = S->SharedLevel;
    const float Age = Clock - LevelBanner;
    if (Age < 2.4f && !S->UpgradePaused)
    {
        const float In = FMath::Clamp(Age / .25f, 0.f, 1.f), Out = FMath::Clamp((2.4f - Age) / .4f, 0.f, 1.f);
        const float Y = Height * .26f - (1 - In) * 40 * Scale;
        Tape(FVector2D(Width * .5f, Y + 30 * Scale), 420 * Scale, 64 * Scale, -2, Marigold.CopyWithNewOpacity(Out));
        Lettering(GWText::Get(TEXT("Combat.LevelUp")), Width * .5f, Y + 4 * Scale, 42 * Scale, Ink.CopyWithNewOpacity(Out), 61);
    }
    if (S->UpgradePaused && U && !Settings && !S->Finished)
        DrawUpgradeChoices(S, *U);
    if (S->Finished)
    {
        if (S->Tutorial)
            TutorialFinished(S);
        else if (S->Defense())
            DefenseResults(S);
        else
            DrawResults(S);
    }
    else if (S->Tutorial)
        TutorialCard(S, U);
    // Last, so a station sheet lies over everything (and takes every click while it is open).
    if (S->Hub())
        HubScreen(S, U);
}

void AGWHUD::TopBoard(AGWGameState* S)
{
    const float CX = Width * .5f, Y = 16 * Scale;
    const float W = 520 * Scale, H = 72 * Scale;
    Card(CX - W * .5f, Y, W, H, Paper, Ink, 601, 1.3f);
    Tape(FVector2D(CX - W * .5f + 18 * Scale, Y + 10 * Scale), 60 * Scale, 18 * Scale, -38, Teal);
    const FVector2D Badge(CX - W * .5f + 52 * Scale, Y + H * .5f);
    Glyph(4, Badge, 64 * Scale, Teal, 602);
    Label(FText::AsNumber(S->SharedLevel), Badge.X, Badge.Y - 15 * Scale, 26 * Scale, Paper, true, 0, EFace::Heavy);
    const float BarX = Badge.X + 44 * Scale, BarW = W - 230 * Scale;
    Label(GWText::Number(TEXT("Combat.Level"), TEXT("Level"), S->SharedLevel), BarX, Y + 10 * Scale, 17 * Scale, Ink, false, BarW,
          EFace::Bold);
    const float Need = FMath::Max(1.f, S->SharedLevel * 100.f);
    Meter(BarX, Y + 38 * Scale, BarW, 18 * Scale, S->XP / Need, Marigold, 603);
    const FText Progress = FText::FromString(FString::Printf(TEXT("%s %d / %d"), *GWText::Get(TEXT("Combat.XP")).ToString(),
                                                             FMath::FloorToInt(S->XP), FMath::RoundToInt(Need)));
    Label(Progress, BarX + BarW - Measure(Progress, 14 * Scale).X, Y + 13 * Scale, 14 * Scale, InkSoft);
    Card(CX + W * .5f - 106 * Scale, Y + 12 * Scale, 92 * Scale, 48 * Scale, Ink, Ink, 604, 0);
    Label(Timer(S->Duration - S->Elapsed), CX + W * .5f - 60 * Scale, Y + 19 * Scale, 26 * Scale, Paper, true, 0, EFace::Bold);
    if (S->BossActive && !S->Finished)
    {
        const float BW = FMath::Min(Width * .44f, 640 * Scale), BH = 48 * Scale, X = CX - BW * .5f, BY = Y + H + 12 * Scale;
        Card(X, BY, BW, BH, Paper, Ink, 650, 1);
        Glyph(18, FVector2D(X + 38 * Scale, BY + BH * .5f), 30 * Scale, Marigold, 651);
        Key(TEXT("Combat.Boss"), X + 70 * Scale, BY + 6 * Scale, 15 * Scale, Ink, false, BW * .5f, EFace::Bold);
        Meter(X + 70 * Scale, BY + 27 * Scale, BW - 90 * Scale, 13 * Scale, S->BossHP, Danger, 652, false);
    }
}

void AGWHUD::PlayerCard(AGWGameState* S, const FGWUnitView& U)
{
    // Defense adds a row for the player's own ink between health and weapon.
    const float Extra = S->Defense() ? 26 * Scale : 0;
    const float W = 400 * Scale, H = 116 * Scale + Extra, X = 24 * Scale, Y = Height - H - 70 * Scale;
    const FLinearColor TeamColor = U.Seat >= 0 ? Seat(U.Seat) : Coral;
    Card(X, Y, W, H, Paper, Ink, 611, 1.4f);
    Tape(FVector2D(X + 10 * Scale, Y + 8 * Scale), 60 * Scale, 18 * Scale, -40, TeamColor);
    const FVector2D Portrait(X + 52 * Scale, Y + H * .5f);
    Disc(Portrait, 38 * Scale, TeamColor.CopyWithNewOpacity(.16f));
    Circle(Portrait, 38 * Scale, Ink, 2 * Scale, 612);
    Glyph(ShapeGlyph(U.Shape), Portrait, 46 * Scale, TeamColor, 613);
    const float BarX = X + 104 * Scale, BarW = W - 124 * Scale;
    Key(TEXT("Combat.Health"), BarX, Y + 10 * Scale, 15 * Scale, InkSoft, false, BarW * .5f, EFace::Bold);
    const FText Health = FText::FromString(FString::Printf(TEXT("%.0f / %.0f"), U.HP, U.MaxHP));
    Label(Health, BarX + BarW - Measure(Health, 15 * Scale, EFace::Bold).X, Y + 9 * Scale, 15 * Scale, Ink, false, 0, EFace::Bold);
    Meter(BarX, Y + 32 * Scale, BarW, 18 * Scale, U.MaxHP > 0 ? U.HP / U.MaxHP : 0, U.HP < U.MaxHP * .3f ? Danger : TeamColor, 614);
    if (U.Armor > 0)
        Meter(BarX, Y + 54 * Scale, BarW, 7 * Scale, U.Armor / 140.f, Frost, 615, false);
    if (S->Defense())
        InkMeter(U, BarX, Y + 58 * Scale, BarW);
    const float WY = Y + Extra;
    Label(GWText::Get(FString::Printf(TEXT("Weapon.%d"), U.Weapon)), BarX, WY + 68 * Scale, 20 * Scale, Ink, false, BarW * .42f,
          EFace::Heavy);
    const int32 Capacity = gwgame::weaponStats(U.Weapon).ammo;
    if (U.Reload > 0)
    {
        const float T = 1 - U.Reload / FMath::Max(.01f, static_cast<float>(gwgame::weaponStats(U.Weapon).reload));
        Arc(FVector2D(BarX + BarW - 14 * Scale, WY + 82 * Scale), 11 * Scale, -PI * .5f, -PI * .5f + T * 2 * PI, Marigold, 3 * Scale);
        Key(TEXT("Combat.Reload"), BarX + BarW * .48f, WY + 72 * Scale, 15 * Scale, Marigold, false, BarW * .4f, EFace::Bold);
    }
    else if (Capacity <= 12)
        for (int32 I = 0; I < Capacity; ++I)
        {
            const float PX = BarX + BarW - (Capacity - I) * 11 * Scale;
            const bool Loaded = I < U.Ammo;
            Box(PX, WY + 74 * Scale, 7 * Scale, 18 * Scale, Loaded ? Marigold : PaperShade);
            Pen(FVector2D(PX, WY + 74 * Scale), FVector2D(PX, WY + 92 * Scale), Ink, 1, I, .2f);
            Pen(FVector2D(PX + 7 * Scale, WY + 74 * Scale), FVector2D(PX + 7 * Scale, WY + 92 * Scale), Ink, 1, I + 30, .2f);
        }
    else
    {
        const FText Rounds = FText::AsNumber(U.Ammo);
        const float NumberW = Measure(Rounds, 18 * Scale, EFace::Bold).X;
        Meter(BarX + BarW * .48f, WY + 78 * Scale, BarW * .52f - NumberW - 8 * Scale, 12 * Scale, U.Ammo / float(Capacity), Marigold, 616,
              false);
        Label(Rounds, BarX + BarW - NumberW, WY + 72 * Scale, 18 * Scale, Ink, false, 0, EFace::Bold);
    }
    // Lingering states stick above the card.
    if (U.Status & EGWStatus::Slowed)
    {
        const FVector2D C(X + 48 * Scale, Y - 38 * Scale);
        Disc(C + FVector2D(2, 3) * Scale, 27 * Scale, Ink.CopyWithNewOpacity(.15f));
        Disc(C, 27 * Scale, Paper);
        Circle(C, 27 * Scale, Ink, 2 * Scale, 13);
        Glyph(13, C, 22 * Scale, Frost, 83);
    }
}

void AGWHUD::TeamCard(AGWGameState* S, const FGWUnitView& Local)
{
    // Everyone else fighting for the team: co-op players first, then the AI teammates.
    TArray<const FGWUnitView*> Mates;
    for (const auto& Other : S->Units)
        if (Other.Id != Local.Id && Other.Team == Local.Team && Other.Kind == 0 && (Other.Human || Other.Bot))
            Mates.Add(&Other);
    if (Mates.IsEmpty())
        return;
    Mates.Sort([](const FGWUnitView& A, const FGWUnitView& B) { return A.Human != B.Human ? A.Human : A.Id < B.Id; });
    const float X = 22 * Scale, Y = 92 * Scale, W = 260 * Scale, RowH = 40 * Scale;
    const int32 Rows = FMath::Min(Mates.Num(), 9);
    Card(X, Y, W, 38 * Scale + Rows * RowH, Paper.CopyWithNewOpacity(.95f), Ink, 761, 1.1f);
    Tape(FVector2D(X + W - 22 * Scale, Y + 6 * Scale), 54 * Scale, 16 * Scale, 34, Coral);
    Key(TEXT("Combat.Team"), X + 16 * Scale, Y + 8 * Scale, 15 * Scale, InkSoft, false, W * .6f, EFace::Bold);
    for (int32 I = 0; I < Rows; ++I)
    {
        const FGWUnitView& Mate = *Mates[I];
        const float RY = Y + 34 * Scale + I * RowH;
        const FLinearColor Color = Mate.Seat >= 0 ? Seat(Mate.Seat) : Mate.Bot ? Cobalt : Coral;
        // A teammate lying as bait or riding in someone's tail says so beside their name (the name gives way).
        float NameRoom = W - 64 * Scale;
        if (Mate.Alive && (Mate.Bait > 0 || Mate.Carried))
        {
            const FText Tag = GWText::Get(Mate.Bait > 0 ? TEXT("Bait.Tag") : TEXT("Carried.Tag"));
            const float TagW = Measure(Tag, 12 * Scale, EFace::Heavy).X + 12 * Scale;
            Highlight(X + W - TagW - 12 * Scale, RY + 2 * Scale, TagW, 17 * Scale, Color.CopyWithNewOpacity(.4f), 790 + I);
            Label(Tag, X + W - TagW * .5f - 12 * Scale, RY + 2 * Scale, 12 * Scale, Ink, true, 0, EFace::Heavy);
            NameRoom -= TagW + 8 * Scale;
        }
        Glyph(Mate.Alive ? ShapeGlyph(Mate.Shape) : 17, FVector2D(X + 26 * Scale, RY + 16 * Scale), 22 * Scale, Mate.Alive ? Color : Pencil,
              770 + I);
        Label(TeammateName(S, Mate), X + 48 * Scale, RY + 2 * Scale, 15 * Scale, Mate.Alive ? Ink : Pencil, false, NameRoom, EFace::Bold);
        if (Mate.Alive)
            Meter(X + 48 * Scale, RY + 23 * Scale, W - 66 * Scale, 7 * Scale, Mate.MaxHP > 0 ? Mate.HP / Mate.MaxHP : 0,
                  Mate.HP < Mate.MaxHP * .3f ? Danger : Color, 780 + I, false);
    }
}

void AGWHUD::SquadCard(AGWGameState* S, const FGWUnitView& U)
{
    auto* C = Cast<AGWController>(PlayerOwner);
    const float W = 320 * Scale, H = 116 * Scale, X = Width - W - 24 * Scale, Y = Height - H - 70 * Scale;
    TArray<const FGWUnitView*> Squad;
    for (const auto& A : S->Units)
        if (A.Alive && A.FollowerOf == U.Id)
            Squad.Add(&A);
    Squad.Sort([](const FGWUnitView& A, const FGWUnitView& B) { return A.TailOrder < B.TailOrder; });
    Card(X, Y, W, H, Paper, Ink, 711, 1.4f);
    Tape(FVector2D(X + W - 12 * Scale, Y + 8 * Scale), 60 * Scale, 18 * Scale, 40, Teal);
    Key(TEXT("Combat.Squad"), X + 18 * Scale, Y + 10 * Scale, 17 * Scale, InkSoft, false, W * .5f, EFace::Bold);
    Label(FText::FromString(FString::Printf(TEXT("× %d"), Squad.Num())), X + 18 * Scale, Y + 30 * Scale, 34 * Scale, Teal, false, 0,
          EFace::Heavy);
    for (int32 I = 0; I < Squad.Num() && I < 10; ++I)
    {
        const FVector2D P(X + 110 * Scale + (I % 5) * 30 * Scale, Y + 32 * Scale + (I / 5) * 30 * Scale);
        Glyph(ShapeGlyph(Squad[I]->Shape), P, 22 * Scale, Squad[I]->Human ? Seat(Squad[I]->Seat) : Teal, 720 + I);
        if (I == 0)
            Circle(P, 15 * Scale, Marigold, 2 * Scale, 730);
    }
    const float Charge = C ? C->ChargeProgress() : -1;
    const FVector2D Ring(X + W - 46 * Scale, Y + H - 40 * Scale);
    if (Charge >= 0)
    {
        Disc(Ring, 30 * Scale, PaperShade);
        Arc(Ring, 26 * Scale, -PI * .5f, -PI * .5f + Charge * 2 * PI, Charge > .99f ? Danger : Marigold, 7 * Scale);
        Label(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Charge * 100))), Ring.X, Ring.Y - 10 * Scale,
              16 * Scale, Ink, true, 0, EFace::Heavy);
    }
    else
        Keycap(FText::FromString(TEXT("Q")), Ring.X - 16 * Scale, Ring.Y - 16 * Scale, 32 * Scale, Squad.Num() ? Ink : Pencil);
    Keycap(FText::FromString(TEXT("E")), X + 18 * Scale, Y + H - 40 * Scale, 28 * Scale, Ink);
    const FText RecruitText = GWText::Get(U.FollowerOf >= 0 ? TEXT("Key.Leave") : TEXT("Key.Recruit"));
    Label(RecruitText, X + 54 * Scale, Y + H - 36 * Scale, 14 * Scale, InkSoft, false, W - 130 * Scale);
    // F merges the last triangle forward; the key lights up only when that merge is possible.
    TArray<const FGWUnitView*> Allies;
    for (const FGWUnitView* Member : Squad)
        if (!Member->Human)
            Allies.Add(Member);
    const bool CanMerge = Allies.Num() >= 2 && Allies.Last()->Sides == 3 && Allies[Allies.Num() - 2]->Sides < 6;
    const float MergeX = X + 64 * Scale + Measure(RecruitText, 14 * Scale).X;
    Keycap(FText::FromString(TEXT("F")), MergeX, Y + H - 40 * Scale, 28 * Scale, CanMerge ? Ink : Pencil);
    Key(TEXT("Key.Merge"), MergeX + 36 * Scale, Y + H - 36 * Scale, 14 * Scale, CanMerge ? Ink : Pencil, false, 90 * Scale);
}

void AGWHUD::WorldTags(AGWGameState* S, const FGWUnitView* Local)
{
    auto* C = Cast<AGWController>(PlayerOwner);
    if (!C || !C->WorldView)
        return;
    // Recruit / join prompt floats above the highlighted candidate like a speech bubble.
    if (Local && Local->Alive && !Settings && !S->UpgradePaused && !S->Finished)
        if (const auto* Candidate = S->FindUnit(C->WorldView->SelectedCandidate))
        {
            FVector World = Candidate->Position;
            C->WorldView->VisualPosition(Candidate->Id, World);
            FVector2D P;
            if (WorldToScreen(S, World + FVector(0, 0, Candidate->Radius * 2.4f + 30), P))
            {
                const FText Text = GWText::Get(!Candidate->Human            ? TEXT("Key.Recruit")
                                               : C->WorldView->SelectedGrab ? TEXT("Key.Grab")
                                                                            : TEXT("Key.Join"));
                const float W = Measure(Text, 17 * Scale, EFace::Bold).X + 64 * Scale, H = 40 * Scale;
                const float X = P.X - W * .5f, Y = P.Y - H - 18 * Scale - 3 * Scale * FMath::Sin(Clock * 5);
                Card(X, Y, W, H, Paper, Ink, 901, 1.2f);
                Polygon({FVector2D(P.X - 9 * Scale, Y + H - 1), FVector2D(P.X + 9 * Scale, Y + H - 1), FVector2D(P.X, Y + H + 14 * Scale)},
                        Paper, Ink, 1.6f * Scale, 902);
                Keycap(FText::FromString(TEXT("E")), X + 8 * Scale, Y + 6 * Scale, 28 * Scale, Ink);
                Label(Text, X + 44 * Scale, Y + 9 * Scale, 17 * Scale, Ink, false, 0, EFace::Bold);
            }
        }
    // Name tags for other players and small hand-drawn health bars for elites, bosses and wounded allies.
    TMap<int32, FString> Names;
    for (const auto& State : S->PlayerArray)
        if (auto* PS = Cast<AGWPlayerState>(State))
            if (PS->UnitId >= 0)
                Names.Add(PS->UnitId, PS->GetPlayerName());
    for (const auto& U : S->Units)
    {
        if (!U.Alive || (Local && U.Id == Local->Id))
            continue;
        const bool Big = U.Kind == 2 && (U.Radius >= 50 || (U.Dummy && U.HP < U.MaxHP));
        const bool Wounded = U.Kind != 2 && U.HP < U.MaxHP;
        const FString* Tag = U.Human ? Names.Find(U.Id) : nullptr;
        if (!Big && !Wounded && !Tag && !U.Bot)
            continue;
        FVector World = U.Position;
        C->WorldView->VisualPosition(U.Id, World);
        FVector2D P;
        if (!WorldToScreen(S, World + FVector(0, 0, U.Radius * 2.2f + 20), P) || P.X < -100 || P.Y < -60 || P.X > Width + 100 ||
            P.Y > Height + 60)
            continue;
        if (Big || Wounded)
        {
            const float BarW = FMath::Clamp(U.Radius * 1.6f, 44.f, 180.f) * Scale;
            Meter(P.X - BarW * .5f, P.Y, BarW, (Big ? 9 : 6) * Scale, U.HP / FMath::Max(1.f, U.MaxHP),
                  U.Kind == 2 ? (U.Radius >= 100 ? Danger : Magenta) : Teal, U.Id, false);
        }
        if (Tag || U.Bot)
        {
            const FText Text = Tag ? FText::FromString(*Tag) : TeammateName(S, U);
            const float W = Measure(Text, 15 * Scale, EFace::Bold).X;
            // The name sits on a marker stroke of the player's seat colour, matching their body.
            Highlight(P.X - W * .5f - 6 * Scale, P.Y - 24 * Scale, W + 12 * Scale, 20 * Scale,
                      (U.Seat >= 0 ? Seat(U.Seat) : U.Bot ? Cobalt : Coral).CopyWithNewOpacity(.45f), U.Id);
            Label(Text, P.X, P.Y - 24 * Scale, 15 * Scale, Ink, true, 0, EFace::Bold);
        }
    }
}

void AGWHUD::PrankTags(AGWGameState* S, const FGWUnitView* Local)
{
    auto* C = Cast<AGWController>(PlayerOwner);
    if (!C || !C->WorldView || S->Finished || Settings || S->UpgradePaused)
        return;
    auto Above = [&](const FGWUnitView& U, float Rise, FVector2D& P)
    {
        FVector World = U.Position;
        C->WorldView->VisualPosition(U.Id, World);
        return WorldToScreen(S, World + FVector(0, 0, U.Radius * 2.2f + Rise), P) && P.X > -100 && P.Y > -60 && P.X < Width + 100 &&
               P.Y < Height + 60;
    };
    // Everyone lying as bait wears a countdown and how many enemies are inside their pull right now, taped
    // just above their name tag (same anchor as WorldTags, one tag-height higher).
    for (const auto& U : S->Units)
    {
        FVector2D P;
        if (!U.Alive || U.Kind != 0 || U.Bait <= 0 || !Above(U, 20, P))
            continue;
        P.Y -= 46 * Scale;
        int32 Pulled = 0;
        for (const auto& Enemy : S->Units)
            Pulled += Enemy.Alive && Enemy.Kind == 2 && FVector::Dist2D(Enemy.Position, U.Position) < S->BaitRadius;
        FFormatNamedArguments Args;
        Args.Add(TEXT("Seconds"), FMath::Max(1, FMath::CeilToInt(U.Bait)));
        Args.Add(TEXT("Count"), Pulled);
        const FText Text = FText::Format(GWText::Get(TEXT("Bait.Countdown")), Args);
        const float W = Measure(Text, 16 * Scale, EFace::Heavy).X + 34 * Scale;
        Tape(FVector2D(P.X, P.Y - 12 * Scale), W, 30 * Scale, -3, Seat(U.Seat).CopyWithNewOpacity(.9f));
        Label(Text, P.X, P.Y - 24 * Scale, 16 * Scale, Ink, true, 0, EFace::Heavy);
    }
    if (!Local || !Local->Alive)
        return;
    // A speech bubble over the local player's head with the one key that matters right now.
    auto Bubble = [&](const FText& KeyText, const FText& Text, float Rise)
    {
        FVector2D P;
        if (!Above(*Local, Rise, P))
            return;
        const float W = Measure(Text, 17 * Scale, EFace::Bold).X + 64 * Scale, H = 40 * Scale;
        const float X = P.X - W * .5f, Y = P.Y - H - 18 * Scale - 3 * Scale * FMath::Sin(Clock * 5);
        Card(X, Y, W, H, Paper, Ink, 931, 1.2f);
        Polygon({FVector2D(P.X - 9 * Scale, Y + H - 1), FVector2D(P.X + 9 * Scale, Y + H - 1), FVector2D(P.X, Y + H + 14 * Scale)}, Paper, Ink,
                1.6f * Scale, 932);
        Keycap(KeyText, X + 8 * Scale, Y + 6 * Scale, 28 * Scale, Ink);
        Label(Text, X + 44 * Scale, Y + 9 * Scale, 17 * Scale, Ink, false, 0, EFace::Bold);
    };
    const FGWUnitView* Holding = nullptr;
    for (const auto& U : S->Units)
        if (U.Alive && U.Carried && U.FollowerOf == Local->Id)
            Holding = &U;
    FText Hint;
    if (Local->Carried)
    {
        Bubble(FText::FromString(TEXT("E")), GWText::Get(TEXT("Key.Wriggle")), 30);
        const FGWUnitView* Holder = S->FindUnit(Local->FollowerOf);
        FFormatNamedArguments Args;
        Args.Add(TEXT("Name"), Holder ? TeammateName(S, *Holder) : FText::GetEmpty());
        Hint = FText::Format(GWText::Get(TEXT("Carried.Hint")), Args);
    }
    else if (Local->Bait > 0)
    {
        if (S->BaitSeconds - Local->Bait >= S->RollDelay)
            Bubble(FText::FromString(TEXT("E")), GWText::Get(TEXT("Key.Roll")), 110);
        Hint = GWText::Get(TEXT("Bait.Hint"));
    }
    else if (Holding)
    {
        FFormatNamedArguments Args;
        Args.Add(TEXT("Name"), TeammateName(S, *Holding));
        Bubble(FText::FromString(TEXT("Q")), FText::Format(GWText::Get(TEXT("Key.ThrowMate")), Args), 30);
    }
    if (!Hint.IsEmpty())
    {
        const float W = FMath::Min(Width * .5f, 620 * Scale), TextH = Paragraph(Hint, 0, -1000, 16 * Scale, W - 80 * Scale, FLinearColor::Transparent);
        const float H = TextH + 30 * Scale, X = (Width - W) * .5f, Y = Height - 200 * Scale - H;
        Card(X, Y, W, H, Hex(0xFFF7D6), Ink, 940, 1.2f);
        Tape(FVector2D(X + W * .5f, Y + 3 * Scale), 90 * Scale, 20 * Scale, 2, Seat(Local->Seat));
        Glyph(3, FVector2D(X + 32 * Scale, Y + H * .5f), 26 * Scale, Seat(Local->Seat), 941);
        Paragraph(Hint, X + 60 * Scale, Y + 15 * Scale, 16 * Scale, W - 80 * Scale, Ink);
    }
}

void AGWHUD::Controls()
{
    struct FControl
    {
        FText Key;
        const TCHAR* Text;
    };
    const auto* State = GetWorld()->GetGameState<AGWGameState>();
    TArray<FControl> Items = {{FText::FromString(TEXT("WASD")), TEXT("Key.Move")},
                              {GWText::Get(TEXT("Key.Mouse")), TEXT("Key.Aim")},
                              {GWText::Get(TEXT("Key.LMB")), TEXT("Key.Fire")}};
    if (State && State->Hub())
        Items.Append({{FText::FromString(TEXT("E")), TEXT("Key.Use")},
                      {FText::FromString(TEXT("F")), TEXT("Key.Merge")},
                      {FText::FromString(TEXT("Q")), TEXT("Key.Throw")},
                      {GWText::Get(TEXT("Key.Wheel")), TEXT("Key.Zoom")}});
    else if (State && State->Defense())
        Items.Append({{GWText::Get(TEXT("Key.RMB")), TEXT("Key.Draw")},
                      {FText::FromString(TEXT("E")), TEXT("Key.Recruit")},
                      {FText::FromString(TEXT("F")), TEXT("Key.Merge")},
                      {FText::FromString(TEXT("Q")), TEXT("Key.Throw")},
                      {GWText::Get(TEXT("Key.Enter")), TEXT("Defense.StartWave")}});
    else
        Items.Append({{FText::FromString(TEXT("E")), TEXT("Key.Recruit")},
                      {FText::FromString(TEXT("Q")), TEXT("Key.Throw")},
                      {GWText::Get(TEXT("Key.Wheel")), TEXT("Key.Zoom")}});
    Items.Add({FText::FromString(TEXT("Esc")), TEXT("Key.Menu")});
    const float Size = 26 * Scale, TextSize = 14 * Scale, Gap = 18 * Scale;
    float Total = 0;
    TArray<float> Widths;
    for (const auto& Item : Items)
    {
        const float KeyW = FMath::Max(Size, Measure(Item.Key, Size * .55f, EFace::Bold).X + Size * .55f);
        const float W = KeyW + 6 * Scale + Measure(GWText::Get(Item.Text), TextSize).X;
        Widths.Add(W);
        Total += W + Gap;
    }
    Total -= Gap;
    const float Y = Height - 46 * Scale;
    Tape(FVector2D(Width * .5f, Y + Size * .5f), Total + 50 * Scale, Size + 18 * Scale, 0, Paper.CopyWithNewOpacity(1.4f));
    float X = Width * .5f - Total * .5f;
    for (int32 I = 0; I < Items.Num(); ++I)
    {
        Keycap(Items[I].Key, X, Y, Size, Ink);
        const float KeyW = FMath::Max(Size, Measure(Items[I].Key, Size * .55f, EFace::Bold).X + Size * .55f);
        Key(Items[I].Text, X + KeyW + 6 * Scale, Y + 5 * Scale, TextSize, InkSoft);
        X += Widths[I] + Gap;
    }
}

void AGWHUD::DrawUpgradeChoices(AGWGameState* S, const FGWUnitView& Unit)
{
    Box(0, 0, Width, Height, Ink.CopyWithNewOpacity(.6f));
    if (Unit.PendingUpgrades <= 0 || !Unit.Alive)
    {
        int32 Waiting = 0;
        for (const auto& U : S->Units)
            Waiting += U.Human && U.Alive && U.PendingUpgrades > 0;
        const float W = 620 * Scale, H = 150 * Scale, X = (Width - W) * .5f, Y = Height * .38f;
        Card(X, Y, W, H, Paper, Ink, 950, 2);
        Tape(FVector2D(X + W * .5f, Y + 4 * Scale), 110 * Scale, 24 * Scale, 2, Teal);
        for (int32 I = 0; I < 3; ++I)
            Disc(FVector2D(X + W * .5f + (I - 1) * 22 * Scale, Y + 112 * Scale), (Boil % 3 == I ? 7 : 4) * Scale, Teal, 12);
        Key(TEXT("Combat.WaitingUpgrades"), X + W * .5f, Y + 30 * Scale, 26 * Scale, Ink, true, W - 40 * Scale, EFace::Heavy);
        Label(GWText::Number(TEXT("Combat.Waiting.Count"), TEXT("Count"), Waiting), X + W * .5f, Y + 70 * Scale, 17 * Scale, InkSoft,
              true, W - 40 * Scale);
        return;
    }
    const float BannerY = Height * .1f;
    Tape(FVector2D(Width * .5f, BannerY + 24 * Scale), FMath::Min(Width * .6f, 700 * Scale), 56 * Scale, -1, Marigold.CopyWithNewOpacity(1.6f));
    Key(TEXT("Combat.WorldPaused"), Width * .5f, BannerY + 6 * Scale, 30 * Scale, Ink, true, Width * .55f, EFace::Heavy);
    Key(TEXT("Combat.ChooseHint"), Width * .5f, BannerY + 64 * Scale, 17 * Scale, Paper, true, Width * .7f, EFace::Bold);
    // Spare slots all mean "recovery"; show that card once.
    TArray<int32> Shown;
    bool Recovery = false;
    for (int32 I = 0; I < 3 && Unit.Choices.IsValidIndex(I); ++I)
        if (Unit.Choices[I] >= 0 || !Recovery)
        {
            Shown.Add(I);
            Recovery |= Unit.Choices[I] < 0;
        }
    const float W = FMath::Min(Width * .27f, 420 * Scale), H = FMath::Min(Height * .52f, 440 * Scale), Gap = 34 * Scale;
    const float Left = (Width - Shown.Num() * W - (Shown.Num() - 1) * Gap) * .5f, Top = Height * .25f;
    for (int32 Slot = 0; Slot < Shown.Num(); ++Slot)
    {
        const int32 Index = Shown[Slot], Skill = Unit.Choices[Index];
        const float X = Left + Slot * (W + Gap);
        const bool Hover = Hovered(X, Top, W, H);
        const float Rise = Hover ? 10 * Scale : 0, Y = Top - Rise;
        const FLinearColor Family = SkillFamily(Skill);
        Card(X, Y, W, H, Paper, Hover ? Family : Ink, 700 + Index * 31, Hover ? 2.8f : 1.6f, (Hover ? 3.4f : 2.2f) * Scale);
        Box(X + 8 * Scale, Y + 8 * Scale, W - 16 * Scale, 78 * Scale, Family.CopyWithNewOpacity(.9f));
        Hatch(X + 8 * Scale, Y + 8 * Scale, W - 16 * Scale, 78 * Scale, (Family * .5f).CopyWithNewOpacity(.4f), 9 * Scale, 1.4f * Scale,
              Index);
        Key(Skill < 0 ? TEXT("Skill.Recovery") : *FString::Printf(TEXT("Skill.Family.%d"), FMath::Clamp(Skill / 4, 0, 4)),
            X + 22 * Scale, Y + 18 * Scale, 16 * Scale, Paper, false, W * .6f, EFace::Heavy);
        Keycap(FText::AsNumber(Index + 1), X + W - 46 * Scale, Y + 16 * Scale, 30 * Scale, Ink);
        Tape(FVector2D(X + W * .5f, Y + 4 * Scale), 80 * Scale, 20 * Scale, (Index - 1) * 6.f, Paper.CopyWithNewOpacity(1.3f));
        SkillIcon(Skill, FVector2D(X + W * .5f, Y + 104 * Scale), 76 * Scale, 760 + Index);
        const FText Title = GWText::Get(Skill < 0 ? TEXT("Skill.Recovery") : FString::Printf(TEXT("Skill.%d"), Skill));
        Label(Title, X + W * .5f, Y + 152 * Scale, 25 * Scale, Ink, true, W - 36 * Scale, EFace::Heavy);
        if (Skill >= 0)
        {
            const int32 Rank = Unit.Skills.IsValidIndex(Skill) ? Unit.Skills[Skill] : 0;
            for (int32 Pip = 0; Pip < 5; ++Pip)
            {
                const FVector2D P(X + W * .5f + (Pip - 2) * 30 * Scale, Y + 200 * Scale);
                const bool Owned = Pip < Rank, Next = Pip == Rank;
                Glyph(6, P, (Next ? 26 : 20) * Scale, Owned ? Ink : Next ? Family : PaperShade, 780 + Pip);
            }
            if (Rank == 0)
            {
                Tape(FVector2D(X + W - 60 * Scale, Y + 112 * Scale), 76 * Scale, 26 * Scale, 12, Coral);
                Key(TEXT("Skill.New"), X + W - 60 * Scale, Y + 102 * Scale, 14 * Scale, Paper, true, 66 * Scale, EFace::Heavy);
            }
            FFormatNamedArguments Args;
            Args.Add(TEXT("Current"), Rank);
            Args.Add(TEXT("Next"), FMath::Min(5, Rank + 1));
            Label(FText::Format(GWText::Get(TEXT("Combat.SkillRank")), Args), X + W * .5f, Y + 222 * Scale, 15 * Scale, InkSoft, true,
                  W - 30 * Scale, EFace::Bold);
        }
        Line(FVector2D(X + 24 * Scale, Y + 250 * Scale), FVector2D(X + W - 24 * Scale, Y + 250 * Scale), Pencil.CopyWithNewOpacity(.6f),
             1);
        Paragraph(GWText::Get(Skill < 0 ? TEXT("Skill.Recovery.Description") : FString::Printf(TEXT("Skill.%d.Description"), Skill)),
                  X + 24 * Scale, Y + 264 * Scale, 18 * Scale, W - 48 * Scale, Ink);
        Hits.Add({FBox2D(FVector2D(X, Y), FVector2D(X + W, Top + H)), 100 + Index});
    }
}

void AGWHUD::DrawResults(AGWGameState* S)
{
    auto* P = PlayerOwner->GetPlayerState<AGWPlayerState>();
    Box(0, 0, Width, Height, Paper.CopyWithNewOpacity(.9f));
    Grained(0, 0, Width, Height);
    const bool Won = S->Winner == 1;
    const FLinearColor Stamp = Won ? Coral : InkSoft;
    const float W = FMath::Min(Width * .62f, 820 * Scale), H = FMath::Min(Height * .7f, 560 * Scale);
    const float X = (Width - W) * .5f, Y = (Height - H) * .45f;
    Card(X, Y, W, H, Paper, Ink, 990, 2.4f, 2.6f * Scale);
    Tape(FVector2D(X + 30 * Scale, Y + 18 * Scale), 90 * Scale, 26 * Scale, -38, Stamp);
    Tape(FVector2D(X + W - 30 * Scale, Y + 18 * Scale), 90 * Scale, 26 * Scale, 38, Stamp);
    if (Won)
        Doodles(FBox2D(FVector2D(X + 20 * Scale, Y + 20 * Scale), FVector2D(X + W - 20 * Scale, Y + H * .45f)), 5, .6f);
    const FText Result = GWText::Get(Won ? TEXT("Result.Victory") : TEXT("Result.Defeat"));
    const float StampSize = 84 * Scale;
    const FVector2D Size = Measure(Result, StampSize, EFace::Heavy);
    const FVector2D Center(X + W * .5f, Y + H * .27f);
    TArray<FVector2D> Border;
    const float A = FMath::DegreesToRadians(-6.f);
    for (const FVector2D& Corner : {FVector2D(-1, -1), FVector2D(1, -1), FVector2D(1, 1), FVector2D(-1, 1)})
    {
        const FVector2D Local(Corner.X * (Size.X * .5f + 34 * Scale), Corner.Y * (Size.Y * .5f + 14 * Scale));
        Border.Add(Center + FVector2D(Local.X * FMath::Cos(A) - Local.Y * FMath::Sin(A), Local.X * FMath::Sin(A) + Local.Y * FMath::Cos(A)));
    }
    Polygon(Border, Stamp.CopyWithNewOpacity(.08f), Stamp, 4 * Scale, 991);
    Lettering(Result, Center.X, Center.Y - Size.Y * .5f, StampSize, Stamp, 992);
    if (Won)
        Key(TEXT("Result.Survived"), X + W * .5f, Y + H * .47f, 26 * Scale, Ink, true, W * .8f, EFace::Bold);
    Label(GWText::Number(TEXT("Result.Reached"), TEXT("Level"), S->SharedLevel), X + W * .5f, Y + H * .56f, 21 * Scale, InkSoft, true,
          W * .8f, EFace::Bold);
    Label(GWText::Number(TEXT("Result.Survival"), TEXT("Seconds"), FMath::FloorToInt(S->Elapsed)), X + W * .5f, Y + H * .63f,
          19 * Scale, InkSoft, true, W * .8f);
    Button(220, GWText::Get(TEXT("Menu.Lobby")), X + W * .08f, Y + H * .76f, W * .4f, 62 * Scale, P && P->Identity == S->HostIdentity,
           false, Teal);
    Button(214, GWText::Get(TEXT("Menu.Leave")), X + W * .52f, Y + H * .76f, W * .4f, 62 * Scale, true, false, Coral);
}
