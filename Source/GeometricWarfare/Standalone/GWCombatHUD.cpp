#include "Engine/Canvas.h"
#include "GWCinematic.h"
#include "GWController.h"
#include "GWMotion.h"
#include "InputCoreTypes.h"
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
    // A player whose machine is gone (left mid-run) still has a seat to go by.
    return Unit.Human && Unit.Seat >= 0 ? GWText::Number(TEXT("Team.Seat"), TEXT("Index"), Unit.Seat + 1) : FText::GetEmpty();
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
    const FGWCinematic& Cinematic = FGWCinematic::Get();
    if (Cinematic.Clean)
    {
        // A promo's clean frame may keep what the HUD says over the field: names, bait countdowns, wave banners.
        if (Cinematic.Story)
        {
            WorldTags(S, U);
            PrankTags(S, nullptr);
            if (S->Defense() && !S->Finished)
                DefenseBanners(S);
        }
        return;
    }
    const bool Furniture = !Cinematic.Promo;
    // An ink smear on the expedition: blots of ink creeping in over the field from the screen's edges (under the
    // tags and the cards, so the player's own numbers stay readable).
    if (U && S->Expedition() && U->Alive && (U->Act & EGWAfflict::Smeared))
        for (int32 K = 0; K < 14; ++K)
        {
            const float A = K * 2 * PI / 14 + Clock * .2f;
            const float Edge = FMath::Max(Width, Height) * .62f;
            const FVector2D At = FVector2D(Width * .5f, Height * .5f) + FVector2D(FMath::Cos(A) * Edge, FMath::Sin(A) * Edge * .75f);
            const float Size = (120 + 50 * FMath::Sin(K * 2.3f + Clock * 2)) * Scale;
            Circle(At, Size, Ink.CopyWithNewOpacity(.7f), Size * .9f, 4900 + K);
        }
    WorldTags(S, U);
    PrankTags(S, Furniture ? U : nullptr);
    // The expedition's tags over the floor's wares, loot, doors and ghosts lie under the cards.
    if (S->Expedition())
        ExpeditionTags(S, U);
    // Every card comes onto the page as the scene is revealed: the board drops from the top, the side cards slide
    // in from their edges, the gear pops up last.
    const float Entry = SceneAge(S);
    // The hub has no board of its own (its room card is drawn with the hub screen below).
    if (S->Hub())
    {
    }
    else if (S->Defense())
    {
        {
            const auto Drop = DropIn(FVector2D(Width * .5f, 60 * Scale), Entry, .1f, 70);
            DefenseBoard(S, U);
        }
        DefenseBanners(S);
    }
    else if (S->Expedition())
    {
        // The expedition: its floor board, the minimap in the top-right corner, and the room banners.
        {
            const auto Drop = DropIn(FVector2D(Width * .5f, 60 * Scale), Entry, .1f, 70);
            ExpeditionBoard(S, U);
        }
        if (Furniture && !S->Finished)
        {
            const auto Slide = SlideIn(FVector2D(Width - 130 * Scale, 110 * Scale), Entry, FVector2D(170, 0), .3f);
            Minimap(S, U);
        }
        ExpeditionBanners(S, U);
    }
    else
    {
        const auto Drop = DropIn(FVector2D(Width * .5f, 60 * Scale), Entry, .1f, 70);
        TopBoard(S);
    }
    if (S->Twist > 0 && !S->Finished && !S->Hub())
    {
        const auto Slide = SlideIn(FVector2D(Width - 120 * Scale, 160 * Scale), Entry, FVector2D(160, 0), .45f);
        TwistTag(S);
    }
    if (U && S->Expedition())
        ExpeditionScreen(S, U, Entry);
    else if (U)
    {
        {
            const auto Slide = SlideIn(FVector2D(220 * Scale, Height - 140 * Scale), Entry, FVector2D(-160, 40), .2f);
            PlayerCard(S, *U);
        }
        DodgeBar(S, *U);
        if (Furniture)
        {
            const auto Slide = SlideIn(FVector2D(Width - 180 * Scale, Height - 140 * Scale), Entry, FVector2D(160, 40), .28f);
            SquadCard(S, *U);
        }
        // In the hub the roster lists everyone; in the tutorial there is nobody else.
        if (Furniture && !S->Hub() && !S->Tutorial)
        {
            const auto Slide = SlideIn(FVector2D(150 * Scale, 150 * Scale), Entry, FVector2D(-160, 0), .32f);
            TeamCard(S, *U);
        }
        if (!U->Alive && !S->Finished)
        {
            const float Down = Since(HashCombine(static_cast<uint32>(U->Id), 0xDEADu));
            Box(0, 0, Width, Height, Ink.CopyWithNewOpacity(.28f * GWEase::Clamp01(Down / .3f)));
            const float W = 520 * Scale, H = 120 * Scale, X = (Width - W) * .5f, Y = Height * .4f;
            const auto Drop = DropIn(FVector2D(Width * .5f, Y + H * .5f), Down, .1f, 60);
            Card(X, Y, W, H, Paper, Ink, 811, 1.6f);
            Tape(FVector2D(X + 40 * Scale, Y + 6 * Scale), 70 * Scale, 20 * Scale, -30, Coral);
            const bool Returns = S->Defense() || S->Hub();
            Glyph(Returns ? 6 : 17, FVector2D(X + 72 * Scale, Y + H * .5f), 50 * Scale, Returns ? Teal : Coral, 813);
            // Defense brings fallen players back up on the platform (sooner if a teammate pays ink at their outline);
            // the hub at its spawn; Survival has no respawn.
            if (Returns && S->Defense())
            {
                Label(GWText::Number(TEXT("Defense.Respawn"), TEXT("Seconds"), FMath::Max(1, FMath::CeilToInt(U->Respawn))), X + 128 * Scale,
                      Y + 26 * Scale, 24 * Scale, Ink, false, W - 150 * Scale, EFace::Bold);
                Label(GWText::Number(TEXT("Defense.ReviveHint"), TEXT("Ink"), FMath::RoundToInt(S->ReviveInk)), X + 128 * Scale, Y + 64 * Scale,
                      15 * Scale, InkSoft, false, W - 150 * Scale);
            }
            else if (Returns)
                Label(GWText::Number(TEXT("Hub.Respawn"), TEXT("Seconds"), FMath::Max(1, FMath::CeilToInt(U->Respawn))), X + 128 * Scale,
                      Y + H * .5f - 16 * Scale, 26 * Scale, Ink, false, W - 150 * Scale, EFace::Bold);
            else
                Key(TEXT("Combat.NoRespawn"), X + 128 * Scale, Y + H * .5f - 16 * Scale, 26 * Scale, Ink, false, W - 150 * Scale, EFace::Bold);
            SpectateTag(S, *U, Y + H + 22 * Scale);
        }
    }
    if (Furniture && S->Defense() && !S->Finished && !S->Tutorial)
        DefenseHints(S, U);
    if (Furniture && S->Defense() && !S->Finished)
        BoxCursor(S, U);
    // The build ring sits over the cards (it closes itself when the build phase ends).
    BuildRing(S, U);
    if (Furniture)
        Controls(Entry);
    // Settings tab: a paper corner with a geometric gear that turns as the mouse comes near.
    if (Furniture)
    {
        const float X = 22 * Scale, Y = 18 * Scale, Sz = 56 * Scale;
        const auto Pop = PopIn(FVector2D(X + Sz * .5f, Y + Sz * .5f), Entry, .4f);
        const bool Hover = Hovered(X, Y, Sz, Sz);
        const float Lift = Eased(0x6EA2u, Hover ? 1.f : 0.f, 14);
        Card(X, Y - 2 * Scale * Lift, Sz, Sz, Paper, Ink, 44, 1.1f + .5f * Lift);
        {
            const FVector2D Hub(X + Sz * .5f, Y + Sz * .5f - 2 * Scale * Lift);
            PushMotion(Hub, FVector2D::ZeroVector, Lift * 45 + 6 * FMath::Sin(static_cast<float>(Clock) * .7f));
            const FMotionGuard Turn(this);
            Glyph(10, Hub, Sz * .62f, FMath::Lerp(PaperShade, Marigold, Lift), 45);
        }
        AddHit(FBox2D(FVector2D(X, Y), FVector2D(X + Sz, Y + Sz)), 4);
    }
    if (S->SharedLevel > SeenLevel && SeenLevel > 0)
    {
        LevelBanner = Clock;
        Confetti(FVector2D(Width * .5f, Height * .26f + 30 * Scale), 26, 900, 7);
    }
    SeenLevel = S->SharedLevel;
    const float Age = Clock - LevelBanner;
    if (Age < 2.4f && !S->UpgradePaused)
    {
        // The tape unrolls from the middle, the letters drop onto it, and it all lifts away at the end.
        const float Out = FMath::Clamp((2.4f - Age) / .4f, 0.f, 1.f), Y = Height * .26f;
        const auto Pop = PopIn(FVector2D(Width * .5f, Y + 30 * Scale), Age);
        Tape(FVector2D(Width * .5f, Y + 30 * Scale), 420 * Scale * GWEase::OutBack(Age / .35f), 64 * Scale, -2, Marigold.CopyWithNewOpacity(Out));
        Lettering(GWText::Get(TEXT("Combat.LevelUp")), Width * .5f, Y + 4 * Scale, 42 * Scale, Ink.CopyWithNewOpacity(Out), 61, Age - .1f);
    }
    if (S->UpgradePaused && U && !Settings && !S->Finished)
        DrawUpgradeChoices(S, *U);
    if (S->Finished)
    {
        if (S->Tutorial)
            TutorialFinished(S);
        else if (S->Defense())
            DefenseResults(S);
        else if (S->Expedition())
            ExpeditionResults(S);
        else
            DrawResults(S);
    }
    else if (S->Tutorial)
        TutorialCard(S, U);
    // Last, so a station sheet lies over everything (and takes every click while it is open).
    if (S->Hub())
        HubScreen(S, U);
    // The expedition's kit and revive sheets lie over everything too; they fold away with the run.
    if (S->Expedition() && !S->Finished && !Settings)
    {
        if (KitSheet)
            DrawKitSheet(S, U);
        if (ReviveSheet)
            DrawReviveSheet(S, U);
    }
    else if (!S->Expedition() || S->Finished)
    {
        KitSheet = false;
        if (ReviveSheet)
            CloseRevive();
    }
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

void AGWHUD::DodgeBar(AGWGameState* S, const FGWUnitView& Local)
{
    // The dodge's cooldown: a small pencil bar over the player's head, filling back up.
    auto* C = Cast<AGWController>(PlayerOwner);
    if (!C || !Local.Alive || C->DodgeCooldown() <= 0)
        return;
    FVector World;
    if (!C->PredictedPosition(Local.Id, World))
        World = Local.Position;
    FVector2D Head;
    if (!WorldToScreen(S, World + FVector(0, 0, (Local.Deck ? gwgame::DeckHeight : 0) + Local.Radius * 2.6f + 40), Head))
        return;
    const float W = 52 * Scale, H = 7 * Scale, Fill = 1.f - static_cast<float>(C->DodgeCooldown() / FMath::Max(.05, C->DodgeRest()));
    Box(Head.X - W * .5f - Scale, Head.Y - H * .5f - Scale, W + 2 * Scale, H + 2 * Scale, Ink);
    Box(Head.X - W * .5f, Head.Y - H * .5f, W, H, Paper);
    Box(Head.X - W * .5f, Head.Y - H * .5f, W * FMath::Clamp(Fill, 0.f, 1.f), H, Teal);
}

void AGWHUD::SpectateTag(AGWGameState* S, const FGWUnitView& Local, float Y)
{
    auto* C = Cast<AGWController>(PlayerOwner);
    const FGWUnitView* Watch = C && !Local.Alive ? S->FindUnit(C->SpectateId) : nullptr;
    if (!Watch)
        return;
    FFormatNamedArguments Args;
    Args.Add(TEXT("Name"), TeammateName(S, *Watch));
    const FText Name = FText::Format(GWText::Get(TEXT("Spectate.Watching")), Args);
    const FText Switch = GWText::Get(TEXT("Spectate.Switch"));
    const bool More = C->SpectateCount > 1;
    const float NameSize = 17 * Scale, SwitchSize = 13 * Scale;
    const float NameW = Measure(Name, NameSize, EFace::Heavy).X, SwitchW = More ? Measure(Switch, SwitchSize, EFace::Bold).X + 24 * Scale : 0;
    const float W = NameW + SwitchW + 70 * Scale, H = 42 * Scale, X = (Width - W) * .5f;
    Card(X, Y, W, H, Paper, Ink, 960, 1.2f);
    Tape(FVector2D(X + 22 * Scale, Y + 4 * Scale), 46 * Scale, 16 * Scale, -24, Seat(Watch->Seat));
    // An eye: two lids and the pupil in the watched player's colour.
    const FVector2D Eye(X + 30 * Scale, Y + H * .5f);
    Arc(Eye + FVector2D(0, 7 * Scale), 13 * Scale, -PI * .82f, -PI * .18f, Ink, 2.2f * Scale);
    Arc(Eye - FVector2D(0, 7 * Scale), 13 * Scale, PI * .18f, PI * .82f, Ink, 2.2f * Scale);
    Disc(Eye, 4.5f * Scale, Seat(Watch->Seat));
    Label(Name, X + 52 * Scale, Y + 10 * Scale, NameSize, Ink, false, 0, EFace::Heavy);
    if (More)
        Label(Switch, X + 52 * Scale + NameW + 24 * Scale, Y + 14 * Scale, SwitchSize, InkSoft, false, 0, EFace::Bold);
}

void AGWHUD::TwistTag(AGWGameState* S)
{
    // The party playlist's twist on this mission, pinned under the top-right card (the teammates card holds the left).
    const FText Name = GWText::Get(FString::Printf(TEXT("Twist.%d"), FMath::Clamp(static_cast<int32>(S->Twist), 1, 3)));
    FFormatNamedArguments Args;
    Args.Add(TEXT("Name"), Name);
    const FText Text = FText::Format(GWText::Get(TEXT("Twist.Tag")), Args);
    const float Size = 15 * Scale, W = Measure(Text, Size, EFace::Bold).X + 48 * Scale, H = 34 * Scale, X = Width - W - 24 * Scale, Y = 144 * Scale;
    Card(X, Y, W, H, Hex(0xFFE3F1), Magenta, 680, 1.f);
    Glyph(15, FVector2D(X + 18 * Scale, Y + H * .5f), 18 * Scale, Magenta, 681);
    Label(Text, X + 34 * Scale, Y + 7 * Scale, Size, Ink, false, 0, EFace::Bold);
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
    // Health reads from a plus mark and its number (no label); the number jumps when it changes.
    Glyph(7, FVector2D(BarX + 8 * Scale, Y + 19 * Scale), 15 * Scale, U.HP < U.MaxHP * .3f ? Danger : TeamColor, 617);
    const FText Health = FText::FromString(FString::Printf(TEXT("%.0f / %.0f"), U.HP, U.MaxHP));
    {
        const float HealthW = Measure(Health, 15 * Scale, EFace::Bold).X;
        const FVector2D Spot(BarX + BarW - HealthW * .5f, Y + 19 * Scale);
        PushMotion(Spot, FVector2D::ZeroVector, 0, Bump(HashCombine(static_cast<uint32>(U.Id), 0x4E41u), FMath::CeilToFloat(U.HP)));
        const FMotionGuard Pulse(this);
        Label(Health, BarX + BarW - HealthW, Y + 9 * Scale, 15 * Scale, Ink, false, 0, EFace::Bold);
    }
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
    // On the box and in the hub a squad is the exception: the card only comes up while there is one (Survival is built
    // around it and keeps it up).
    const bool SquadMode = !S->Defense() && !S->Hub() && !S->Tutorial && !S->Expedition();
    const float Shown = Eased(0x5A0Du, SquadMode || !Squad.IsEmpty() || (C && C->ChargeProgress() >= 0) ? 1.f : 0.f, 9);
    if (Shown < .02f)
        return;
    PushMotion(FVector2D(X + W, Y + H), FVector2D((1 - Shown) * 120 * Scale, 0), 0, 1, Shown);
    const FMotionGuard Away(this);
    Card(X, Y, W, H, Paper, Ink, 711, 1.4f);
    Tape(FVector2D(X + W - 12 * Scale, Y + 8 * Scale), 60 * Scale, 18 * Scale, 40, Teal);
    Glyph(3, FVector2D(X + 28 * Scale, Y + 22 * Scale), 20 * Scale, Teal, 712);
    {
        const FVector2D Spot(X + 40 * Scale, Y + 50 * Scale);
        PushMotion(Spot, FVector2D::ZeroVector, 0, Bump(0x5A0Eu, static_cast<float>(Squad.Num())));
        const FMotionGuard Pulse(this);
        Label(FText::FromString(FString::Printf(TEXT("× %d"), Squad.Num())), X + 18 * Scale, Y + 30 * Scale, 34 * Scale, Teal, false, 0,
              EFace::Heavy);
    }
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
    // Recruit / join prompt floats above the highlighted candidate like a speech bubble. The expedition offers only a
    // teammate to grab, and only with nothing of the floor's in reach (E is ExpeditionPrompts' then).
    FReach Reach;
    const bool FloorFirst = S->Expedition() && Local && ExpeditionReach(S, *Local, Reach);
    if (Local && Local->Alive && !Settings && !S->UpgradePaused && !S->Finished && !FloorFirst)
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
    // The box: a fallen teammate's outline in reach up on the platform (E brings them back for ink), or the end of a
    // ladder (walking into it climbs).
    const FGWUnitView* Fallen = nullptr;
    bool AtLadder = false;
    if (S->Defense() && !Local->Carried && Local->Bait <= 0 && !Local->Climbing)
    {
        float Nearest = S->ReviveReach;
        if (Local->Deck)
            for (const auto& U : S->Units)
                if (!U.Alive && U.Id != Local->Id && U.Kind == 0 && (U.Human || U.Bot) && U.Respawn > 0 && U.Team == Local->Team &&
                    FVector::Dist2D(U.Position, Local->Position) < Nearest)
                {
                    Nearest = FVector::Dist2D(U.Position, Local->Position);
                    Fallen = &U;
                }
        if (C->WorldView)
            for (const auto& Net : S->Ladders)
            {
                gwgame::Ladder L;
                L.deck = Net.Deck;
                L.pit = Net.Pit;
                if (!C->WorldView->Geometry.isDeck(L.deck) || L.pit < 0)
                    continue;
                const gwgame::Vec2 End = Local->Deck ? gwgame::ladderHead(C->WorldView->Geometry, L) : gwgame::ladderFoot(C->WorldView->Geometry, L);
                AtLadder |= FVector2D::Distance(FVector2D(End.x, End.y), FVector2D(Local->Position)) < gwgame::LadderUse * 1.3f;
            }
    }
    FText Hint;
    if (Fallen)
    {
        FFormatNamedArguments Args;
        Args.Add(TEXT("Name"), TeammateName(S, *Fallen));
        Args.Add(TEXT("Ink"), FMath::RoundToInt(S->ReviveInk));
        Bubble(FText::FromString(TEXT("E")), FText::Format(GWText::Get(TEXT("Key.Revive")), Args), 30);
    }
    else if (AtLadder && !Holding)
        Bubble(FText::FromString(Local->Deck ? TEXT("↓") : TEXT("↑")), GWText::Get(Local->Deck ? TEXT("Key.ClimbDown") : TEXT("Key.ClimbUp")), 30);
    else if (Local->Carried)
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

void AGWHUD::Controls(float Entry)
{
    // The key strip shows while a scene is new (its first eight seconds) and whenever Tab is held; otherwise it
    // folds down into a single Tab keycap in the same place. The expedition's Tab opens the kit sheet (which lists the
    // keys), so there the strip shows on arrival only.
    const auto* State = GetWorld()->GetGameState<AGWGameState>();
    const bool Expedition = State && State->Expedition();
    const bool Held = PlayerOwner && PlayerOwner->IsInputKeyDown(EKeys::Tab) && !Expedition;
    const float Open = Eased(0xC0E7u, Held || Entry < 8.f ? 1.f : 0.f, 8);
    if (Open < .97f && !Expedition)
    {
        const FText Tab = FText::FromString(TEXT("Tab"));
        const float Size = 26 * Scale, KeyW = Measure(Tab, Size * .55f, EFace::Bold).X + Size * .55f;
        const float X = Width * .5f - KeyW * .5f - 16 * Scale, Y = Height - 46 * Scale;
        PushMotion(FVector2D(Width * .5f, Y), FVector2D(0, Open * 40 * Scale), 0, 1, 1 - Open);
        const FMotionGuard Folded(this);
        Keycap(Tab, X, Y, Size, InkSoft);
        Glyph(10, FVector2D(X + KeyW + 18 * Scale, Y + Size * .5f), 18 * Scale, PaperDeep, 4120);
    }
    if (Open <= .02f)
        return;
    const auto Slide = SlideIn(FVector2D(Width * .5f, Height - 40 * Scale), Entry, FVector2D(0, 80), .5f);
    PushMotion(FVector2D(Width * .5f, Height - 40 * Scale), FVector2D(0, (1 - Open) * 70 * Scale), 0, 1, Open);
    const FMotionGuard Fold(this);
    struct FControl
    {
        FText Key;
        const TCHAR* Text;
    };
    // In a defense page's build phase the left button also opens the build ring on the floor (the tutorial's from its
    // drawing step on).
    const bool Building = State && State->Building();
    TArray<FControl> Items = {{FText::FromString(TEXT("WASD")), TEXT("Key.Move")},
                              {GWText::Get(TEXT("Key.Mouse")), TEXT("Key.Aim")},
                              {GWText::Get(TEXT("Key.LMB")), Building ? TEXT("Key.FireBuild") : TEXT("Key.Fire")}};
    if (State && State->Hub())
        Items.Append({{FText::FromString(TEXT("E")), TEXT("Key.Use")},
                      {FText::FromString(TEXT("F")), TEXT("Key.Merge")},
                      {FText::FromString(TEXT("Q")), TEXT("Key.Throw")},
                      {GWText::Get(TEXT("Key.Wheel")), TEXT("Key.Zoom")}});
    else if (State && State->Defense())
    {
        // Blocks and ladders go up only while building: not in a wave, nor before the tutorial's drawing step.
        if (Building)
            Items.Add({GWText::Get(TEXT("Key.RMB")), TEXT("Key.DrawBox")});
        Items.Append({{FText::FromString(TEXT("E")), TEXT("Key.GrabRevive")},
                      {FText::FromString(TEXT("R")), TEXT("Key.Mend")},
                      {FText::FromString(TEXT("Shift")), TEXT("Key.Dodge")},
                      {FText::FromString(TEXT("Q")), TEXT("Key.Throw")},
                      {GWText::Get(TEXT("Key.Enter")), TEXT("Defense.StartWave")}});
    }
    else if (Expedition)
        Items.Append({{FText::FromString(TEXT("E")), TEXT("Key.Take")},
                      {FText::FromString(TEXT("1-5")), TEXT("Key.Slots")},
                      {FText::FromString(TEXT("G")), TEXT("Key.Drop")},
                      {FText::FromString(TEXT("Tab")), TEXT("Key.Kit")},
                      {FText::FromString(TEXT("R")), TEXT("Key.Mend")},
                      {FText::FromString(TEXT("Shift")), TEXT("Key.Dodge")},
                      {FText::FromString(TEXT("Q")), TEXT("Key.Throw")}});
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
    const float BannerY = Height * .1f, Dealt = Since(HashCombine(static_cast<uint32>(Unit.Id), 0xCA4Du + static_cast<uint32>(Unit.PendingUpgrades)));
    {
        const auto Pop = PopIn(FVector2D(Width * .5f, BannerY + 24 * Scale), Dealt);
        Tape(FVector2D(Width * .5f, BannerY + 24 * Scale), FMath::Min(Width * .6f, 700 * Scale) * GWEase::OutBack(Dealt / .35f), 56 * Scale, -1,
             Marigold.CopyWithNewOpacity(1.6f));
        Key(TEXT("Combat.WorldPaused"), Width * .5f, BannerY + 6 * Scale, 30 * Scale, Ink, true, Width * .55f, EFace::Heavy);
    }
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
        // Dealt like playing cards: up from below the table, fanned, one after another.
        const float P = GWEase::Clamp01((Dealt - .15f - Slot * .1f) / .45f);
        PushMotion(FVector2D(X + W * .5f, Top + H), FVector2D(0, (1 - GWEase::OutBack(P, 1.2f)) * Height * .6f), (1 - GWEase::OutCubic(P)) * (Slot - 1) * 18.f, 1,
                   GWEase::Clamp01(P * 3));
        const FMotionGuard Deal(this);
        const bool Hover = Hovered(X, Top, W, H);
        const float Rise = Eased(HashCombine(static_cast<uint32>(Index), 0xCA4Eu), Hover ? 1.f : 0.f, 14) * 10 * Scale, Y = Top - Rise;
        const FLinearColor Family = SkillFamily(Skill);
        Card(X, Y, W, H, Paper, Hover ? Family : Ink, 700 + Index * 31, Hover ? 2.8f : 1.6f, (Hover ? 3.4f : 2.2f) * Scale);
        Box(X + 8 * Scale, Y + 8 * Scale, W - 16 * Scale, 78 * Scale, Family.CopyWithNewOpacity(.9f));
        Hatch(X + 8 * Scale, Y + 8 * Scale, W - 16 * Scale, 78 * Scale, (Family * .5f).CopyWithNewOpacity(.4f), 9 * Scale, 1.4f * Scale,
              Index);
        // Cards are picked with the mouse (design v2.1 4.1: 1-5 belong to the expedition's hotbar alone).
        Key(Skill < 0 ? TEXT("Skill.Recovery") : *FString::Printf(TEXT("Skill.Family.%d"), FMath::Clamp(Skill / 4, 0, 4)),
            X + 22 * Scale, Y + 18 * Scale, 16 * Scale, Paper, false, W * .6f, EFace::Heavy);
        Tape(FVector2D(X + W * .5f, Y + 4 * Scale), 80 * Scale, 20 * Scale, (Index - 1) * 6.f, Paper.CopyWithNewOpacity(1.3f));
        SkillIcon(Skill, FVector2D(X + W * .5f, Y + 104 * Scale), 76 * Scale, 760 + Index);
        const FText Title = GWText::Get(Skill < 0 ? TEXT("Skill.Recovery") : FString::Printf(TEXT("Skill.%d"), Skill));
        Label(Title, X + W * .5f, Y + 152 * Scale, 25 * Scale, Ink, true, W - 36 * Scale, EFace::Heavy);
        if (Skill >= 0)
        {
            const int32 Rank = Unit.Skills.IsValidIndex(Skill) ? Unit.Skills[Skill] : 0;
            for (int32 Pip = 0; Pip < 5; ++Pip)
            {
                const FVector2D Pin(X + W * .5f + (Pip - 2) * 30 * Scale, Y + 200 * Scale);
                const bool Owned = Pip < Rank, Next = Pip == Rank;
                Glyph(6, Pin, (Next ? 26 : 20) * Scale, Owned ? Ink : Next ? Family : PaperShade, 780 + Pip);
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
        AddHit(FBox2D(FVector2D(X, Y), FVector2D(X + W, Top + H)), 100 + Index);
    }
}

void AGWHUD::DrawResults(AGWGameState* S)
{
    auto* P = PlayerOwner->GetPlayerState<AGWPlayerState>();
    const float Age = Since(HashCombine(S->Seed, 0x4E5Du));
    Box(0, 0, Width, Height, Paper.CopyWithNewOpacity(.9f * GWEase::Clamp01(Age / .25f)));
    if (Age > .25f)
        Grained(0, 0, Width, Height);
    const bool Won = S->Winner == 1;
    const FLinearColor Stamp = Won ? Coral : InkSoft;
    const float W = FMath::Min(Width * .62f, 820 * Scale), H = FMath::Min(Height * .7f, 560 * Scale);
    const float X = (Width - W) * .5f, Y = (Height - H) * .45f;
    const auto Drop = DropIn(FVector2D(Width * .5f, Height * .5f), Age, .05f, 90);
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
    {
        // The stamp slams down onto the card (confetti on a win).
        const auto Slam = StampIn(Center, Age, .35f);
        Polygon(Border, Stamp.CopyWithNewOpacity(.08f), Stamp, 4 * Scale, 991);
        Lettering(Result, Center.X, Center.Y - Size.Y * .5f, StampSize, Stamp, 992);
    }
    if (Won && Age > .55f && Once(HashCombine(S->Seed, 0x57A9u)))
        Confetti(Center, 46, 1100, 8);
    if (Won)
        Key(TEXT("Result.Survived"), X + W * .5f, Y + H * .47f, 26 * Scale, Ink, true, W * .8f, EFace::Bold);
    Label(GWText::Number(TEXT("Result.Reached"), TEXT("Level"), S->SharedLevel), X + W * .5f, Y + H * .56f, 21 * Scale, InkSoft, true,
          W * .8f, EFace::Bold);
    Label(GWText::Number(TEXT("Result.Survival"), TEXT("Seconds"), FMath::FloorToInt(S->Elapsed)), X + W * .5f, Y + H * .63f, 19 * Scale, InkSoft,
          true, W * .8f);
    Button(220, GWText::Get(TEXT("Menu.Lobby")), X + W * .08f, Y + H * .76f, W * .4f, 62 * Scale, P && P->Identity == S->HostIdentity,
           false, Teal);
    Button(214, GWText::Get(TEXT("Menu.Leave")), X + W * .52f, Y + H * .76f, W * .4f, 62 * Scale, true, false, Coral);
}
