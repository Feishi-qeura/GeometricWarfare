#include "Engine/Canvas.h"
#include "GWCinematic.h"
#include "GWController.h"
#include "GWGameInstance.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWMotion.h"
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
    const float Rise = Eased(HashCombine(static_cast<uint32>(Index), 0x1E7Cu), Hover && !Selected ? 1.f : 0.f, 14) * 6 * Scale, CY = Y - Rise;
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
            const FLinearColor Fill = C == '#' ? Graphite : C == 'S' || C == 'R' ? Coral : C == 'C' ? Teal : C == ',' ? PaperShade : C == '=' ? Ruler : Paper;
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
        AddHit(FBox2D(FVector2D(X, CY), FVector2D(X + W, CY + H)), GWDefenseAction::Level + Index);
}

void AGWHUD::InkMeter(const FGWUnitView& Unit, float X, float Y, float W)
{
    // The ink drop says what the bar is; the number jumps as ink comes in or goes out.
    Glyph(23, FVector2D(X + 8 * Scale, Y + 8 * Scale), 18 * Scale, InkBlue, 615);
    const FText Value = FText::FromString(FString::Printf(TEXT("%.0f / %.0f"), Unit.Ink, Unit.MaxInk));
    const float ValueW = Measure(Value, 14 * Scale, EFace::Bold).X;
    {
        PushMotion(FVector2D(X + W - ValueW * .5f, Y + 8 * Scale), FVector2D::ZeroVector, 0,
                   Bump(HashCombine(static_cast<uint32>(Unit.Id), 0x14Cu), FMath::RoundToFloat(Unit.Ink)));
        const FMotionGuard Pulse(this);
        Label(Value, X + W - ValueW, Y - 1 * Scale, 14 * Scale, Ink, false, 0, EFace::Bold);
    }
    const float BarX = X + 24 * Scale, BarW = W - 24 * Scale - ValueW - 10 * Scale;
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
    // The phase as a marker tag: a hammer chip while building, a sword-red one while a wave is out. The marker is
    // drawn across again each time the phase turns.
    const float PhaseAge = Since(HashCombine(S->Seed, 0xFA5Eu + static_cast<uint32>(S->Phase) * 31u + static_cast<uint32>(S->WavesCleared)));
    Highlight(X + 28 * Scale, Y + 50 * Scale, 150 * Scale * GWEase::OutCubic(PhaseAge / .35f), 22 * Scale, (Build ? Teal : Coral).CopyWithNewOpacity(.3f), 602);
    Key(Build ? TEXT("Defense.Build") : TEXT("Defense.Fight"), X + 34 * Scale, Y + 50 * Scale, 16 * Scale, Ink, false, 160 * Scale, EFace::Bold);
    // The core: the one number that decides the page (its gem says what it is).
    const float MeterX = X + 230 * Scale, MeterW = W - 230 * Scale - 150 * Scale;
    const float Health = S->CoreMaxHP > 0 ? FMath::Clamp(S->CoreHP / S->CoreMaxHP, 0.f, 1.f) : 0;
    {
        const FVector2D Gem(MeterX + 12 * Scale, Y + 22 * Scale);
        PushMotion(Gem, FVector2D::ZeroVector, 0, Bump(HashCombine(S->Seed, 0xC0BEu), FMath::RoundToFloat(S->CoreHP)));
        const FMotionGuard Pulse(this);
        Glyph(6, Gem, 22 * Scale, FMath::Lerp(Danger, Teal, Health), 603);
    }
    const FText Points = FText::FromString(FString::Printf(TEXT("%.0f / %.0f"), S->CoreHP, S->CoreMaxHP));
    Label(Points, MeterX + MeterW - Measure(Points, 15 * Scale, EFace::Bold).X, Y + 11 * Scale, 15 * Scale, Ink, false, 0, EFace::Bold);
    Meter(MeterX, Y + 42 * Scale, MeterW, 20 * Scale, Health, Health < .35f ? Danger : Teal, 604);
    // Right: the build countdown (its digits jump each second), or how many enemies are still out.
    const float RX = X + W - 136 * Scale;
    if (Build && Lesson)
        Key(TEXT("Tutorial.NoClock"), RX + 60 * Scale, Y + 30 * Scale, 16 * Scale, InkSoft, true, 130 * Scale, EFace::Bold);
    else if (Build)
    {
        const int32 Left = FMath::Max(0, FMath::CeilToInt(S->PhaseTime));
        const FLinearColor Face = Left <= 5 ? Danger : Ink;
        Card(RX, Y + 12 * Scale, 120 * Scale, 58 * Scale, Face, Face, 605, 0);
        PushMotion(FVector2D(RX + 60 * Scale, Y + 41 * Scale), FVector2D::ZeroVector, 0, Left <= 10 ? Bump(HashCombine(S->Seed, 0x7105u), static_cast<float>(Left)) : 1.f);
        const FMotionGuard Pulse(this);
        Label(FText::FromString(FString::Printf(TEXT("%d:%02d"), Left / 60, Left % 60)), RX + 60 * Scale, Y + 22 * Scale, 28 * Scale, Paper,
              true, 0, EFace::Bold);
    }
    else
    {
        // An enemy triangle and how many are left.
        const FText Count = FText::AsNumber(S->EnemiesLeft);
        const float CountW = Measure(Count, 26 * Scale, EFace::Heavy).X;
        Glyph(3, FVector2D(RX + 50 * Scale - CountW * .5f, Y + 42 * Scale), 26 * Scale, Violet, 606);
        PushMotion(FVector2D(RX + 74 * Scale, Y + 42 * Scale), FVector2D::ZeroVector, 0, Bump(HashCombine(S->Seed, 0xE7E7u), static_cast<float>(S->EnemiesLeft)));
        const FMotionGuard Pulse(this);
        Label(Count, RX + 70 * Scale - CountW * .5f, Y + 26 * Scale, 26 * Scale, Ink, false, 0, EFace::Heavy);
    }
    // Live star objectives in the top-right corner (the tutorial keeps that corner for its skip button). While a
    // wave is out the card folds down to its three marks.
    if (!Lesson)
    {
        const float SW = 300 * Scale, SX = Width - SW - 22 * Scale, SY = 18 * Scale;
        const float Open = Eased(HashCombine(S->Seed, 0x0B1Eu), Build ? 1.f : 0.f, 8);
        const auto Slide = SlideIn(FVector2D(SX + SW, SY), PhaseAge + .6f, FVector2D(140, 0), .25f);
        const float CW = FMath::Lerp(118 * Scale, SW, Open), CH = FMath::Lerp(48 * Scale, 112 * Scale, Open), CX0 = Width - CW - 22 * Scale;
        Card(CX0, SY, CW, CH, Paper.CopyWithNewOpacity(.95f), Ink, 690, 1);
        if (Open > .5f)
        {
            const float Keep = Fade;
            Fade *= GWEase::Clamp01((Open - .5f) * 2);
            Key(TEXT("Defense.Objectives"), SX + 16 * Scale, SY + 8 * Scale, 15 * Scale, InkSoft, false, SW * .6f, EFace::Bold);
            for (int32 I = 0; I < 3; ++I)
            {
                const bool Kept = (S->Stars >> I) & 1;
                const float LY = SY + 34 * Scale + I * 24 * Scale;
                Glyph(Kept ? 5 : 17, FVector2D(SX + 26 * Scale, LY + 9 * Scale), 18 * Scale, Kept ? Marigold : Danger, 691 + I);
                Key(*FString::Printf(TEXT("Star.%d"), I), SX + 44 * Scale, LY, 14 * Scale, Kept ? Ink : Pencil, false, SW - 56 * Scale);
            }
            Fade = Keep;
        }
        else
            for (int32 I = 0; I < 3; ++I)
            {
                // A lost star shakes once as it is crossed out.
                const bool Kept = (S->Stars >> I) & 1;
                const FVector2D Mark(CX0 + 26 * Scale + I * 33 * Scale, SY + 24 * Scale);
                PushMotion(Mark, FVector2D::ZeroVector, 0, Bump(HashCombine(S->Seed, 0x57A0u + I), Kept ? 1.f : 0.f));
                const FMotionGuard Pulse(this);
                Glyph(Kept ? 5 : 17, Mark, 22 * Scale, Kept ? Marigold : Danger, 691 + I);
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
    // The controls live in the key strip (Tab), the build bar and the cursor's tag; what is left for a note is what
    // a page does differently: a platform broken by rock (chapter 2) or raid gates on its outer rim (chapter 3).
    // It is pinned up for the first build phase of such a page and taken down after a while.
    if (S->Phase != static_cast<uint8>(gwgame::DefensePhase::Build) || S->WavesCleared > 0 || !Local || !Local->Alive)
        return;
    bool Broken = false, Raids = false;
    for (int32 I = 0; I < S->Cells.Num() && S->GridWidth > 0; ++I)
    {
        const int32 CX = I % S->GridWidth, CY = I / S->GridWidth;
        const bool Rim = CX == 0 || CY == 0 || CX == S->GridWidth - 1 || CY == S->GridHeight - 1;
        Raids |= (S->Cells[I] & (EGWCell::Deck | EGWCell::Entrance)) == (EGWCell::Deck | EGWCell::Entrance);
        Broken |= Rim && !(S->Cells[I] & EGWCell::Deck);
    }
    if (!Raids && !Broken)
        return;
    const float Age = SceneAge(S);
    if (Age > 16)
        return;
    const FText Line = GWText::Get(Raids ? TEXT("Defense.RaidHint") : TEXT("Defense.RimHint"));
    const float W = 420 * Scale, X = 24 * Scale, TextX = X + 54 * Scale, TextW = W - 70 * Scale;
    const float H = Paragraph(Line, TextX, -1000, 15 * Scale, TextW, FLinearColor::Transparent) + 34 * Scale;
    const float Y = Height - 70 * Scale - 142 * Scale - 18 * Scale - H;
    const float Leave = GWEase::InCubic((Age - 15.4f) / .6f);
    const auto Slide = SlideIn(FVector2D(X, Y + H * .5f), Age, FVector2D(-200, 0), .9f);
    PushMotion(FVector2D(X, Y), FVector2D(-Leave * 300 * Scale, 0), 0, 1, 1 - Leave);
    const FMotionGuard Away(this);
    Card(X, Y, W, H, Hex(0xFFF7D6), Ink, 720, 1.2f);
    Tape(FVector2D(X + W * .5f, Y + 3 * Scale), 90 * Scale, 20 * Scale, -2, Raids ? Danger : Plum);
    Glyph(Raids ? 15 : 19, FVector2D(X + 28 * Scale, Y + H * .5f), 26 * Scale, Raids ? Danger : Plum, 721);
    Paragraph(Line, TextX, Y + 16 * Scale, 15 * Scale, TextW, Ink);
}

void AGWHUD::BoxCursor(AGWGameState* S, const FGWUnitView* Local)
{
    auto* C = Cast<AGWController>(PlayerOwner);
    float MX = 0, MY = 0;
    if (!C || !C->WorldView || !Local || !Local->Alive || Settings || OverlayOpen() || C->HoverCell < 0 || C->Painting || C->RingCell >= 0 ||
        !S->Cells.IsValidIndex(C->HoverCell) || !PlayerOwner->GetMousePosition(MX, MY) || S->Phase != static_cast<uint8>(gwgame::DefensePhase::Build))
        return;
    const int32 Cell = C->HoverCell;
    FText KeyText, Text;
    FLinearColor Tint = Teal;
    if (S->Cells[Cell] & EGWCell::Deck)
    {
        const int32 State = C->WorldView->LadderState(S, Local, Cell);
        KeyText = GWText::Get(TEXT("Key.RMB"));
        if (State == 0)
            Text = GWText::Number(TEXT("Box.Ladder"), TEXT("Ink"), FMath::RoundToInt(S->LadderInk));
        else if (State == 1)
        {
            Text = GWText::Get(TEXT("Box.LadderErase"));
            Tint = Coral;
        }
    }
    else if ((S->Cells[Cell] & EGWCell::Paper) && RingAllowed(S, Local))
    {
        // A click here opens the build ring: the builds on open floor, upgrade or rub out on a tower or wire.
        const TArray<FRingSlot> Slots = RingSlots(S, *Local, Cell);
        if (Slots.Num())
        {
            KeyText = GWText::Get(TEXT("Key.LMB"));
            Tint = Marigold;
            Text = GWText::Get(Slots[0].Kind == ERingSlot::Upgrade ? TEXT("Box.ClickTower") : Slots[0].Kind == ERingSlot::Remove ? TEXT("Ring.Remove")
                                                                                                                                  : TEXT("Box.Click"));
        }
    }
    if (Text.IsEmpty())
        return;
    const float Size = 22 * Scale, KeyW = FMath::Max(Size, Measure(KeyText, Size * .55f, EFace::Bold).X + Size * .55f);
    const float W = KeyW + Measure(Text, 14 * Scale, EFace::Bold).X + 26 * Scale, H = Size + 12 * Scale;
    const float X = FMath::Min(MX + 22 * Scale, Width - W - 8 * Scale), Y = FMath::Min(MY + 20 * Scale, Height - H - 8 * Scale);
    Card(X, Y, W, H, Paper, Tint, 950, 1.1f, 1.6f * Scale);
    Keycap(KeyText, X + 6 * Scale, Y + 6 * Scale, Size, Ink);
    Label(Text, X + KeyW + 14 * Scale, Y + 8 * Scale, 14 * Scale, Ink, false, 0, EFace::Bold);
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
    if (S->WavesCleared > SeenCleared && !S->Finished && !FGWCinematic::Get().QuietClears)
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
                C->NoticeKey = FString::Printf(TEXT("Defense.Denied.%d"), FMath::Clamp(FMath::RoundToInt(E.Radius), 1, 6));
        }
    // The clear banner sits lower than the wave banner: an early call can put both on screen at once.
    auto Banner = [&](double Start, const FText& Text, FLinearColor Color, int32 Seed, float Row)
    {
        const float Age = Clock - Start;
        if (Age >= 2.4f || S->Finished)
            return;
        // The tape is slapped on and unrolls from the middle, the letters drop onto it one by one, a burst of
        // paper flies off its ends, and the strip lifts away at the end.
        const float Out = GWEase::InCubic((Age - 2.f) / .4f), Y = Height * Row - Out * 40 * Scale;
        const float W = FMath::Max(420 * Scale, Measure(Text, 42 * Scale, EFace::Heavy).X + 120 * Scale);
        const auto Pop = PopIn(FVector2D(Width * .5f, Y + 30 * Scale), Age);
        PushMotion(FVector2D(Width * .5f, Y + 30 * Scale), FVector2D::ZeroVector, Out * 3, 1, 1 - Out);
        const FMotionGuard Lift(this);
        Tape(FVector2D(Width * .5f, Y + 30 * Scale), W * GWEase::OutBack(Age / .3f), 64 * Scale, -2, Color);
        Lettering(Text, Width * .5f, Y + 4 * Scale, 42 * Scale, Ink, Seed, Age - .08f);
        if (Age > .2f && Once(HashCombine(static_cast<uint32>(Seed), GetTypeHash(Start))))
            for (const float Side : {-1.f, 1.f})
                Confetti(FVector2D(Width * .5f + Side * W * .5f, Y + 30 * Scale), 12, 700, 6, Side > 0 ? 0.f : PI, 1.6f);
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
    // The page fades to paper, the result card is pinned down, the stamp slams, then the stars pop in one by one.
    const float Age = Since(HashCombine(S->Seed, 0xDE5Du));
    Box(0, 0, Width, Height, Paper.CopyWithNewOpacity(.9f * GWEase::Clamp01(Age / .25f)));
    if (Age > .25f)
        Grained(0, 0, Width, Height);
    const bool Won = S->Winner == 1, Host = LocalHost(S);
    const FLinearColor Stamp = Won ? Coral : InkSoft;
    // Alone there are no titles to deal, so the card is only as tall as its stars and numbers.
    const bool Crowd = S->PlayerArray.Num() >= 2;
    const float W = FMath::Min(Width * .66f, 900 * Scale), H = FMath::Min(Height * .78f, (Crowd ? 640 : 520) * Scale);
    const float X = (Width - W) * .5f, Y = (Height - H) * .45f;
    const auto Drop = DropIn(FVector2D(Width * .5f, Height * .5f), Age, .05f, 90);
    Card(X, Y, W, H, Paper, Ink, 990, 2.4f, 2.6f * Scale);
    Tape(FVector2D(X + 30 * Scale, Y + 18 * Scale), 90 * Scale, 26 * Scale, -38, Stamp);
    Tape(FVector2D(X + W - 30 * Scale, Y + 18 * Scale), 90 * Scale, 26 * Scale, 38, Stamp);
    if (Won)
        Doodles(FBox2D(FVector2D(X + 20 * Scale, Y + 20 * Scale), FVector2D(X + W - 20 * Scale, Y + H * .3f)), 5, .5f);
    const FText Result = GWText::Get(Won ? TEXT("Result.Victory") : TEXT("Result.Defeat"));
    const float StampSize = 74 * Scale;
    const FVector2D Size = Measure(Result, StampSize, EFace::Heavy);
    // The stamp, the page's name and the lists sit at fixed heights from the top (the card's height only changes
    // what room is left for the titles).
    const FVector2D Center(X + W * .5f, Y + 108 * Scale);
    TArray<FVector2D> Border;
    const float A = FMath::DegreesToRadians(-6.f);
    for (const FVector2D& Corner : {FVector2D(-1, -1), FVector2D(1, -1), FVector2D(1, 1), FVector2D(-1, 1)})
    {
        const FVector2D L(Corner.X * (Size.X * .5f + 34 * Scale), Corner.Y * (Size.Y * .5f + 14 * Scale));
        Border.Add(Center + FVector2D(L.X * FMath::Cos(A) - L.Y * FMath::Sin(A), L.X * FMath::Sin(A) + L.Y * FMath::Cos(A)));
    }
    {
        const auto Slam = StampIn(Center, Age, .35f);
        Polygon(Border, Stamp.CopyWithNewOpacity(.08f), Stamp, 4 * Scale, 991);
        Lettering(Result, Center.X, Center.Y - Size.Y * .5f, StampSize, Stamp, 992);
    }
    if (Won && Age > .55f && Once(HashCombine(S->Seed, 0x57A9u)))
        Confetti(Center, 46, 1100, 8);
    Label(PageTitle(S->Level), X + W * .5f, Y + 194 * Scale, 22 * Scale, Ink, true, W * .8f, EFace::Heavy);
    Key(Won ? TEXT("Result.Held") : TEXT("Result.CoreLost"), X + W * .5f, Y + 228 * Scale, 18 * Scale, InkSoft, true, W * .8f, EFace::Bold);
    // A won page lists each star beside the objective that earned it; the numbers sit to the right. Each earned
    // star pops onto its line with a pinch of confetti.
    const float ListY = Y + 272 * Scale;
    if (Won)
        for (int32 I = 0; I < 3; ++I)
        {
            const bool Kept = (S->Stars >> I) & 1;
            const float LY = ListY + I * 36 * Scale;
            const FVector2D Spot(X + W * .2f, LY + 13 * Scale);
            const float StarAt = .75f + I * .22f;
            {
                const auto Pop = PopIn(Spot, Age, StarAt);
                Glyph(5, Spot, 30 * Scale, Kept ? Marigold : PaperShade, 993 + I);
            }
            if (Kept && Age > StarAt + .12f && Once(HashCombine(S->Seed, 0x57A0u + I)))
                Confetti(Spot, 12, 600, 5);
            const auto Slide = SlideIn(FVector2D(X + W * .2f, LY), Age, FVector2D(40, 0), StarAt);
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
    // A run with friends ends with its titles (design doc: 颁奖), dealt in after the stars.
    if (Crowd)
    {
        const auto Deal = SlideIn(FVector2D(X + W * .5f, Y + H - 160 * Scale), Age, FVector2D(0, 60), 1.4f);
        Awards(S, X + 30 * Scale, Y + H - 92 * Scale - 128 * Scale, W - 60 * Scale, 116 * Scale);
    }
    // Next steps: the host restarts or moves on for everyone; anyone may leave.
    const int32 Pages = static_cast<int32>(gwgame::defenseLevels().size());
    const bool HasNext = Won && S->Level + 1 < Pages;
    const float BY = Y + H - 92 * Scale, Gap = 16 * Scale;
    const auto Rise = SlideIn(FVector2D(X + W * .5f, BY), Age, FVector2D(0, 70), 1.1f);
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
