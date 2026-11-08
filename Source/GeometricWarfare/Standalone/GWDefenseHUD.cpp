#include "Engine/Canvas.h"
#include "GWController.h"
#include "GWGameInstance.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWPalette.h"
#include "GWPlayerState.h"
#include "GWProfile.h"
#include "GWText.h"
#include "GWWorld.h"

// Line-defense pages of the sketchbook HUD: the page cards of the hub's mission board, the wave board over the
// page (also the tutorial's), build hints, wave banners and the result card.
using namespace GWPalette;

namespace
{
const FLinearColor Graphite = Hex(0x5E6678), InkBlue = Hex(0x2B3A67);
FString PageId(int32 Index)
{
    const auto& Levels = gwgame::defenseLevels();
    return Index >= 0 && Index < static_cast<int32>(Levels.size()) ? FString(ANSI_TO_TCHAR(Levels[Index].id)) : FString();
}
FText PageTitle(int32 Index)
{
    const FString Id = PageId(Index);
    // The tutorial's page is no level of the chapter.
    if (Id.IsEmpty())
        return GWText::Get(TEXT("Tutorial.Title"));
    return FText::FromString(Id + TEXT("  ·  ") + GWText::Get(TEXT("Level.") + Id).ToString());
}
} // namespace

bool AGWHUD::LocalHost(AGWGameState* S) const
{
    const auto* P = PlayerOwner ? PlayerOwner->GetPlayerState<AGWPlayerState>() : nullptr;
    return P && S && P->Identity == S->HostIdentity;
}

void AGWHUD::StarRow(uint8 Stars, FVector2D Center, float Size, int32 Seed)
{
    for (int32 I = 0; I < 3; ++I)
        Glyph(5, Center + FVector2D((I - 1) * Size * 1.2f, 0), Size, (Stars >> I) & 1 ? Marigold : PaperShade, Seed + I);
}

void AGWHUD::LevelCard(int32 Index, float X, float Y, float W, float H, bool ViewOnly)
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    UGWProfileSave* Profile = GI ? GI->ActiveProfile() : nullptr;
    const gwgame::DefenseLevel& L = gwgame::defenseLevels()[Index];
    const FString Id = PageId(Index);
    // The host's own profile decides which pages open; a guest only looks at the host's choice.
    const bool Open = ViewOnly || !Profile || Profile->Unlocked(Index);
    const bool Selected = Open && BoardLevel == Index, Hover = Open && !ViewOnly && Hovered(X, Y, W, H);
    const float Rise = Hover && !Selected ? 6 * Scale : 0, CY = Y - Rise;
    Card(X, CY, W, H, Open ? Paper : PaperShade, Selected ? Coral : Ink, 610 + Index * 9, Selected ? 2.6f : Hover ? 1.9f : 1.2f,
         (Selected ? 3.f : 1.8f) * Scale);
    Tape(FVector2D(X + W * .5f, CY + 4 * Scale), 100 * Scale, 24 * Scale, (Index - 1) * 5.f, (Selected ? Coral : Teal).CopyWithNewOpacity(.85f));
    Label(PageTitle(Index), X + 22 * Scale, CY + 22 * Scale, 24 * Scale, Open ? Ink : InkSoft, false, W - 44 * Scale, EFace::Heavy);
    // A thumbnail of the page itself: rock, open paper, keep-clear floor, entrances and the core.
    const int32 Cols = static_cast<int32>(FCStringAnsi::Strlen(L.rows[0])), Rows = static_cast<int32>(L.rows.size());
    const float Cell = FMath::Min((W - 44 * Scale) / Cols, H * .46f / Rows);
    const float MX = X + (W - Cell * Cols) * .5f, MY = CY + 66 * Scale;
    Box(MX - 3 * Scale, MY - 3 * Scale, Cell * Cols + 6 * Scale, Cell * Rows + 6 * Scale, Ink.CopyWithNewOpacity(Open ? .85f : .4f));
    for (int32 Row = 0; Row < Rows; ++Row)
        for (int32 Col = 0; Col < Cols; ++Col)
        {
            const char C = L.rows[Row][Col];
            const FLinearColor Fill = C == '#' ? Graphite : C == 'S' ? Coral : C == 'C' ? Teal : C == ',' ? PaperShade : Paper;
            Box(MX + Col * Cell, MY + Row * Cell, Cell - FMath::Max(1.f, Scale), Cell - FMath::Max(1.f, Scale),
                Open ? Fill : FMath::Lerp(Fill, PaperShade, .6f));
        }
    const float InfoY = MY + Cell * Rows + 16 * Scale;
    Label(GWText::Number(TEXT("Board.Waves"), TEXT("Count"), L.waves), X + 22 * Scale, InfoY, 18 * Scale, InkSoft, false, W * .5f, EFace::Bold);
    StarRow(Profile ? Profile->BestStars(Id) : 0, FVector2D(X + W - 70 * Scale, InfoY + 12 * Scale), 26 * Scale, 620 + Index * 5);
    if (Profile && Profile->Cleared(Id))
        Glyph(16, FVector2D(X + W - 34 * Scale, CY + 34 * Scale), 30 * Scale, Teal, 630 + Index);
    if (!Open)
    {
        Glyph(9, FVector2D(X + W * .5f, MY + Cell * Rows * .5f), 54 * Scale, Marigold, 640 + Index);
        Key(TEXT("Board.Locked"), X + W * .5f, InfoY + 30 * Scale, 16 * Scale, InkSoft, true, W - 40 * Scale, EFace::Bold);
        return;
    }
    if (!ViewOnly)
        Hits.Add({FBox2D(FVector2D(X, CY), FVector2D(X + W, CY + H)), GWDefenseAction::Level + Index});
}

void AGWHUD::InkMeter(const FGWUnitView& Unit, float X, float Y, float W)
{
    Glyph(23, FVector2D(X + 9 * Scale, Y + 8 * Scale), 18 * Scale, InkBlue, 615);
    Key(TEXT("Defense.Ink"), X + 22 * Scale, Y - 1 * Scale, 14 * Scale, InkSoft, false, 60 * Scale, EFace::Bold);
    const FText Value = FText::FromString(FString::Printf(TEXT("%.0f / %.0f"), Unit.Ink, Unit.MaxInk));
    const float ValueW = Measure(Value, 14 * Scale, EFace::Bold).X;
    Label(Value, X + W - ValueW, Y - 1 * Scale, 14 * Scale, Ink, false, 0, EFace::Bold);
    const float BarX = X + 80 * Scale, BarW = W - 80 * Scale - ValueW - 10 * Scale;
    Meter(BarX, Y + 3 * Scale, BarW, 11 * Scale, Unit.MaxInk > 0 ? Unit.Ink / Unit.MaxInk : 0, InkBlue, 616, false);
}

void AGWHUD::DefenseBoard(AGWGameState* S, const FGWUnitView* Local)
{
    const bool Build = S->Phase == static_cast<uint8>(gwgame::DefensePhase::Build);
    // The tutorial's build phase has no clock, and its one wave waits for the last step.
    const bool Lesson = S->Tutorial, WaveReady = !Lesson || S->TutorialStep == static_cast<uint8>(gwgame::TutorialStep::Defend);
    const float CX = Width * .5f, Y = 16 * Scale, W = 680 * Scale, H = 84 * Scale, X = CX - W * .5f;
    Card(X, Y, W, H, Paper, Ink, 601, 1.3f);
    Tape(FVector2D(X + 18 * Scale, Y + 10 * Scale), 60 * Scale, 18 * Scale, -38, Build ? Teal : Coral);
    FFormatNamedArguments Args;
    Args.Add(TEXT("Wave"), FMath::Max(1, Build ? S->WavesCleared + 1 : S->Wave));
    Args.Add(TEXT("Count"), S->WaveCount);
    Label(FText::Format(GWText::Get(TEXT("Defense.Wave")), Args), X + 30 * Scale, Y + 10 * Scale, 24 * Scale, Ink, false, 200 * Scale, EFace::Heavy);
    Highlight(X + 28 * Scale, Y + 50 * Scale, 150 * Scale, 22 * Scale, (Build ? Teal : Coral).CopyWithNewOpacity(.3f), 602);
    Key(Build ? TEXT("Defense.Build") : TEXT("Defense.Fight"), X + 34 * Scale, Y + 50 * Scale, 16 * Scale, Ink, false, 160 * Scale, EFace::Bold);
    // The core: the one number that decides the page.
    const float MeterX = X + 230 * Scale, MeterW = W - 230 * Scale - 150 * Scale;
    const float Health = S->CoreMaxHP > 0 ? FMath::Clamp(S->CoreHP / S->CoreMaxHP, 0.f, 1.f) : 0;
    Glyph(6, FVector2D(MeterX + 12 * Scale, Y + 22 * Scale), 22 * Scale, FMath::Lerp(Danger, Teal, Health), 603);
    Key(TEXT("Defense.Core"), MeterX + 30 * Scale, Y + 11 * Scale, 16 * Scale, Ink, false, MeterW * .5f, EFace::Bold);
    const FText Points = FText::FromString(FString::Printf(TEXT("%.0f / %.0f"), S->CoreHP, S->CoreMaxHP));
    Label(Points, MeterX + MeterW - Measure(Points, 15 * Scale, EFace::Bold).X, Y + 11 * Scale, 15 * Scale, Ink, false, 0, EFace::Bold);
    Meter(MeterX, Y + 42 * Scale, MeterW, 20 * Scale, Health, Health < .35f ? Danger : Teal, 604);
    // Right: the build countdown, or how many enemies are still out.
    const float RX = X + W - 136 * Scale;
    if (Build && Lesson)
        Key(TEXT("Tutorial.NoClock"), RX + 60 * Scale, Y + 30 * Scale, 16 * Scale, InkSoft, true, 130 * Scale, EFace::Bold);
    else if (Build)
    {
        Card(RX, Y + 12 * Scale, 120 * Scale, 58 * Scale, Ink, Ink, 605, 0);
        const int32 Left = FMath::Max(0, FMath::CeilToInt(S->PhaseTime));
        Label(FText::FromString(FString::Printf(TEXT("%d:%02d"), Left / 60, Left % 60)), RX + 60 * Scale, Y + 22 * Scale, 28 * Scale, Paper,
              true, 0, EFace::Bold);
    }
    else
        Label(GWText::Number(TEXT("Defense.Enemies"), TEXT("Count"), S->EnemiesLeft), RX + 60 * Scale, Y + 30 * Scale, 17 * Scale, Ink, true,
              130 * Scale, EFace::Bold);
    // Live star objectives in the top-right corner (the tutorial keeps that corner for its skip button).
    if (!Lesson)
    {
        const float SW = 300 * Scale, SX = Width - SW - 22 * Scale, SY = 18 * Scale;
        Card(SX, SY, SW, 112 * Scale, Paper.CopyWithNewOpacity(.95f), Ink, 690, 1);
        Key(TEXT("Defense.Objectives"), SX + 16 * Scale, SY + 8 * Scale, 15 * Scale, InkSoft, false, SW * .6f, EFace::Bold);
        for (int32 I = 0; I < 3; ++I)
        {
            const bool Kept = (S->Stars >> I) & 1;
            const float LY = SY + 34 * Scale + I * 24 * Scale;
            Glyph(Kept ? 5 : 17, FVector2D(SX + 26 * Scale, LY + 9 * Scale), 18 * Scale, Kept ? Marigold : Danger, 691 + I);
            Key(*FString::Printf(TEXT("Star.%d"), I), SX + 44 * Scale, LY, 14 * Scale, Kept ? Ink : Pencil, false, SW - 56 * Scale);
        }
    }
    if (S->BossActive && !S->Finished)
    {
        const float BW = FMath::Min(Width * .44f, 640 * Scale), BH = 48 * Scale, BX = CX - BW * .5f, BY = Y + H + 12 * Scale;
        Card(BX, BY, BW, BH, Paper, Ink, 650, 1);
        Glyph(22, FVector2D(BX + 38 * Scale, BY + BH * .5f), 30 * Scale, Night, 651);
        Key(TEXT("Combat.Boss"), BX + 70 * Scale, BY + 6 * Scale, 15 * Scale, Ink, false, BW * .5f, EFace::Bold);
        Meter(BX + 70 * Scale, BY + 27 * Scale, BW - 90 * Scale, 13 * Scale, S->BossHP, Danger, 652, false);
    }
    // Calling the next wave early: the host's button; everyone else sees who decides.
    if (Build && !S->Finished && WaveReady)
    {
        const float BW = 300 * Scale, BH = 54 * Scale, BX = CX - BW * .5f, BY = Y + H + 14 * Scale;
        if (LocalHost(S))
        {
            Button(GWDefenseAction::CallWave, GWText::Get(TEXT("Defense.StartWave")), BX, BY, BW, BH, true, false, Coral);
            Keycap(GWText::Get(TEXT("Key.Enter")), BX + BW + 12 * Scale, BY + 10 * Scale, 34 * Scale, Ink);
        }
        else
            Key(TEXT("Defense.HostStarts"), CX, BY + 14 * Scale, 17 * Scale, InkSoft, true, BW * 1.5f, EFace::Bold);
    }
    (void)Local;
}

void AGWHUD::DefenseHints(AGWGameState* S, const FGWUnitView* Local)
{
    if (S->Phase != static_cast<uint8>(gwgame::DefensePhase::Build) || !Local || !Local->Alive)
        return;
    // A sticky note above the player card, sized to its text, leaving the bottom centre to notices.
    const float W = 400 * Scale, X = 24 * Scale, TextX = X + 50 * Scale, TextW = W - 66 * Scale;
    const FText Build = GWText::Get(TEXT("Defense.BuildHint")), Turret = GWText::Get(TEXT("Defense.TurretHint"));
    const float BuildH = Paragraph(Build, TextX, -1000, 15 * Scale, TextW, FLinearColor::Transparent);
    const float TurretH = Paragraph(Turret, TextX, -1000, 15 * Scale, TextW, FLinearColor::Transparent);
    const float H = BuildH + TurretH + 44 * Scale, Y = Height - 70 * Scale - 142 * Scale - 18 * Scale - H;
    Card(X, Y, W, H, Hex(0xFFF7D6), Ink, 720, 1.2f);
    Tape(FVector2D(X + W * .5f, Y + 3 * Scale), 90 * Scale, 20 * Scale, -2, Marigold);
    Glyph(19, FVector2D(X + 26 * Scale, Y + 30 * Scale), 24 * Scale, Teal, 721);
    Paragraph(Build, TextX, Y + 16 * Scale, 15 * Scale, TextW, Ink);
    const float TurretY = Y + 26 * Scale + BuildH;
    Glyph(1, FVector2D(X + 26 * Scale, TurretY + 12 * Scale), 20 * Scale, Hex(0x3C8DBC), 722);
    Paragraph(Turret, TextX, TurretY, 15 * Scale, TextW, InkSoft);
}

void AGWHUD::DefenseBanners(AGWGameState* S)
{
    if (BannerSeed != S->Seed)
    {
        BannerSeed = S->Seed;
        SeenWave = S->Wave;
        SeenCleared = S->WavesCleared;
        SeenDenied = 0;
        WaveBanner = ClearBanner = -10;
    }
    if (S->Wave > SeenWave)
        WaveBanner = Clock;
    if (S->WavesCleared > SeenCleared && !S->Finished)
        ClearBanner = Clock;
    SeenWave = S->Wave;
    SeenCleared = S->WavesCleared;
    // A refused stroke tells only the player who drew it why (no ink, a sealed core, someone in the way).
    auto* C = Cast<AGWController>(PlayerOwner);
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    for (const auto& E : S->Effects)
        if (static_cast<gwgame::EffectStyle>(E.Style) == gwgame::EffectStyle::Denied && E.Id > SeenDenied)
        {
            SeenDenied = E.Id;
            if (P && E.Subject == P->UnitId && C)
                C->NoticeKey = FString::Printf(TEXT("Defense.Denied.%d"), FMath::Clamp(FMath::RoundToInt(E.Radius), 1, 3));
        }
    // The clear banner sits lower than the wave banner: an early call can put both on screen at once.
    auto Banner = [&](double Start, const FText& Text, FLinearColor Color, int32 Seed, float Row)
    {
        const float Age = Clock - Start;
        if (Age >= 2.4f || S->Finished)
            return;
        const float In = FMath::Clamp(Age / .25f, 0.f, 1.f), Out = FMath::Clamp((2.4f - Age) / .4f, 0.f, 1.f);
        const float Y = Height * Row - (1 - In) * 40 * Scale;
        const float W = FMath::Max(420 * Scale, Measure(Text, 42 * Scale, EFace::Heavy).X + 120 * Scale);
        Tape(FVector2D(Width * .5f, Y + 30 * Scale), W, 64 * Scale, -2, Color.CopyWithNewOpacity(Out));
        Lettering(Text, Width * .5f, Y + 4 * Scale, 42 * Scale, Ink.CopyWithNewOpacity(Out), Seed);
    };
    FFormatNamedArguments Wave;
    Wave.Add(TEXT("Wave"), S->Wave);
    Banner(WaveBanner, FText::Format(GWText::Get(TEXT("Defense.WaveBanner")), Wave), Coral, 731, .28f);
    Banner(ClearBanner, GWText::Number(TEXT("Defense.ClearBanner"), TEXT("Count"), 10 + 2 * S->WavesCleared), Teal, 732, .4f);
}

bool AGWHUD::Awards(AGWGameState* S, float X, float Y, float W, float H)
{
    struct FAward
    {
        const TCHAR* Title;
        const TCHAR* Detail;
        int32 Icon;
        int32 Value;
        const FGWUnitView* Who;
    };
    auto Best = [&](const TCHAR* Title, const TCHAR* Detail, int32 GlyphKind, TFunctionRef<int32(const FGWUnitView&)> Score)
    {
        FAward Award{Title, Detail, GlyphKind, 0, nullptr};
        for (const auto& U : S->Units)
            if (U.Kind == 0 && U.Human && Score(U) > Award.Value)
            {
                Award.Value = Score(U);
                Award.Who = &U;
            }
        return Award;
    };
    const FAward Candidates[] = {Best(TEXT("Award.MVP"), TEXT("Award.MVP.Detail"), 5, [](const FGWUnitView& U) { return U.Damage; }),
                                 Best(TEXT("Award.Bait"), TEXT("Award.Bait.Detail"), 0, [](const FGWUnitView& U) { return U.Held; }),
                                 Best(TEXT("Award.Cannonball"), TEXT("Award.Cannonball.Detail"), 3, [](const FGWUnitView& U) { return U.Thrown; }),
                                 Best(TEXT("Award.Walls"), TEXT("Award.Walls.Detail"), 1, [](const FGWUnitView& U) { return U.Walls; }),
                                 Best(TEXT("Award.Scapegoat"), TEXT("Award.Scapegoat.Detail"), 17, [](const FGWUnitView& U) { return U.Pushes; })};
    TArray<const FAward*> Shown;
    for (const FAward& Award : Candidates)
        if (Award.Who && Shown.Num() < 4)
            Shown.Add(&Award);
    if (Shown.IsEmpty())
        return false;
    const float Gap = 12 * Scale, CellW = (W - (Shown.Num() - 1) * Gap) / Shown.Num();
    for (int32 I = 0; I < Shown.Num(); ++I)
    {
        const FAward& Award = *Shown[I];
        const float CX = X + I * (CellW + Gap);
        const FLinearColor Mark = Seat(Award.Who->Seat);
        Card(CX, Y, CellW, H, Paper, Ink, 1010 + I * 7, 1.2f);
        Tape(FVector2D(CX + CellW * .5f, Y + 3 * Scale), 70 * Scale, 18 * Scale, (I - 1.5f) * 4, Mark.CopyWithNewOpacity(.9f));
        Glyph(Award.Icon, FVector2D(CX + 30 * Scale, Y + 38 * Scale), 30 * Scale, Mark, 1020 + I);
        Key(Award.Title, CX + 54 * Scale, Y + 16 * Scale, 15 * Scale, InkSoft, false, CellW - 64 * Scale, EFace::Bold);
        Label(TeammateName(S, *Award.Who), CX + 54 * Scale, Y + 38 * Scale, 18 * Scale, Ink, false, CellW - 64 * Scale, EFace::Heavy);
        Label(GWText::Number(Award.Detail, TEXT("Count"), Award.Value), CX + 16 * Scale, Y + H - 34 * Scale, 14 * Scale, InkSoft, false,
              CellW - 28 * Scale);
    }
    return true;
}

void AGWHUD::DefenseResults(AGWGameState* S)
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    Box(0, 0, Width, Height, Paper.CopyWithNewOpacity(.9f));
    Grained(0, 0, Width, Height);
    const bool Won = S->Winner == 1, Host = LocalHost(S);
    const FLinearColor Stamp = Won ? Coral : InkSoft;
    const float W = FMath::Min(Width * .66f, 900 * Scale), H = FMath::Min(Height * .78f, 640 * Scale);
    const float X = (Width - W) * .5f, Y = (Height - H) * .45f;
    Card(X, Y, W, H, Paper, Ink, 990, 2.4f, 2.6f * Scale);
    Tape(FVector2D(X + 30 * Scale, Y + 18 * Scale), 90 * Scale, 26 * Scale, -38, Stamp);
    Tape(FVector2D(X + W - 30 * Scale, Y + 18 * Scale), 90 * Scale, 26 * Scale, 38, Stamp);
    if (Won)
        Doodles(FBox2D(FVector2D(X + 20 * Scale, Y + 20 * Scale), FVector2D(X + W - 20 * Scale, Y + H * .3f)), 5, .5f);
    const FText Result = GWText::Get(Won ? TEXT("Result.Victory") : TEXT("Result.Defeat"));
    const float StampSize = 74 * Scale;
    const FVector2D Size = Measure(Result, StampSize, EFace::Heavy);
    const FVector2D Center(X + W * .5f, Y + H * .17f);
    TArray<FVector2D> Border;
    const float A = FMath::DegreesToRadians(-6.f);
    for (const FVector2D& Corner : {FVector2D(-1, -1), FVector2D(1, -1), FVector2D(1, 1), FVector2D(-1, 1)})
    {
        const FVector2D L(Corner.X * (Size.X * .5f + 34 * Scale), Corner.Y * (Size.Y * .5f + 14 * Scale));
        Border.Add(Center + FVector2D(L.X * FMath::Cos(A) - L.Y * FMath::Sin(A), L.X * FMath::Sin(A) + L.Y * FMath::Cos(A)));
    }
    Polygon(Border, Stamp.CopyWithNewOpacity(.08f), Stamp, 4 * Scale, 991);
    Lettering(Result, Center.X, Center.Y - Size.Y * .5f, StampSize, Stamp, 992);
    Label(PageTitle(S->Level), X + W * .5f, Y + H * .3f, 22 * Scale, Ink, true, W * .8f, EFace::Heavy);
    Key(Won ? TEXT("Result.Held") : TEXT("Result.CoreLost"), X + W * .5f, Y + H * .3f + 34 * Scale, 18 * Scale, InkSoft, true, W * .8f,
        EFace::Bold);
    // A won page lists each star beside the objective that earned it; the numbers sit to the right.
    const float ListY = Y + H * .42f;
    if (Won)
        for (int32 I = 0; I < 3; ++I)
        {
            const bool Kept = (S->Stars >> I) & 1;
            const float LY = ListY + I * 36 * Scale;
            Glyph(5, FVector2D(X + W * .2f, LY + 13 * Scale), 30 * Scale, Kept ? Marigold : PaperShade, 993 + I);
            Key(*FString::Printf(TEXT("Star.%d"), I), X + W * .2f + 26 * Scale, LY + 2 * Scale, 17 * Scale, Kept ? Ink : Pencil, false, W * .34f,
                EFace::Bold);
        }
    const float NumX = Won ? X + W * .62f : X + W * .5f - 120 * Scale;
    Label(GWText::Number(TEXT("Result.Waves"), TEXT("Count"), S->WavesCleared), NumX, ListY, 19 * Scale, Ink, false, W * .34f, EFace::Bold);
    Label(GWText::Number(TEXT("Result.Stickers"), TEXT("Count"), S->StickersEarned), NumX, ListY + 36 * Scale, 19 * Scale, Ink, false, W * .34f,
          EFace::Bold);
    if (Won && GI && GI->LastClearBonus > 0 && GI->LastClearLevel == PageId(S->Level))
        Label(GWText::Number(TEXT("Result.FirstClear"), TEXT("Count"), GI->LastClearBonus), NumX, ListY + 72 * Scale, 17 * Scale, Coral, false,
              W * .34f, EFace::Bold);
    // What this player's own bait earned (the host adds it to their reward packets wave by wave).
    if (const auto* Me = PlayerOwner->GetPlayerState<AGWPlayerState>())
        if (const FGWUnitView* Mine = S->FindUnit(Me->UnitId); Mine && Mine->BaitBonus > 0)
            Label(GWText::Number(TEXT("Result.BaitStickers"), TEXT("Count"), Mine->BaitBonus), NumX, ListY + 108 * Scale, 17 * Scale, Teal,
                  false, W * .34f, EFace::Bold);
    // A run with friends ends with its titles (design doc: 颁奖).
    if (S->PlayerArray.Num() >= 2)
        Awards(S, X + 30 * Scale, Y + H - 92 * Scale - 128 * Scale, W - 60 * Scale, 116 * Scale);
    // Next steps: the host restarts or moves on for everyone; anyone may leave.
    const int32 Pages = static_cast<int32>(gwgame::defenseLevels().size());
    const bool HasNext = Won && S->Level + 1 < Pages;
    const float BY = Y + H - 92 * Scale, Gap = 16 * Scale;
    const bool GuestLeave = S->IsRoom && !Host;
    const int32 Buttons = 2 + (HasNext ? 1 : 0) + (GuestLeave ? 1 : 0);
    const float BW = (W - 60 * Scale - (Buttons - 1) * Gap) / Buttons;
    float BX = X + 30 * Scale;
    Button(GWDefenseAction::Retry, GWText::Get(TEXT("Result.Retry")), BX, BY, BW, 60 * Scale, Host, false, Marigold);
    BX += BW + Gap;
    if (HasNext)
    {
        Button(GWDefenseAction::Next, GWText::Get(TEXT("Result.Next")), BX, BY, BW, 60 * Scale, Host, false, Coral);
        BX += BW + Gap;
    }
    // Back to the hub for everyone (the host's call); a guest may also go home on their own.
    Button(220, GWText::Get(TEXT("Hub.Back")), BX, BY, BW, 60 * Scale, Host, false, Teal);
    BX += BW + Gap;
    if (GuestLeave)
        Button(214, GWText::Get(TEXT("Hub.LeaveRoom")), BX, BY, BW, 60 * Scale, true, false, Coral);
}
