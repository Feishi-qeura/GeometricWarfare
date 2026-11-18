#include "Engine/Canvas.h"
#include "GWController.h"
#include "GWGameInstance.h"
#include "GWGameMode.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWMotion.h"
#include "GWPalette.h"
#include "GWPlayerState.h"
#include "GWProfile.h"
#include "GWSettings.h"
#include "GWText.h"
#include "GWWorld.h"

// The hub (大厅 = 房间) and the tutorial on the sketchbook HUD: the room card with the invite button, who is in
// the room, paper tags over the stations with the E prompt, the deploy countdown, the station sheets, and the
// tutorial's step card and finished card.
using namespace GWPalette;

namespace
{
// One icon and one marker colour per station, in gwgame::HubStation order: board (flag), achievement wall
// (crown), shop (crate), research desk (spark), wardrobe (hat), door (letter), training corner (throw).
const int32 StationGlyphs[] = {8, 18, 20, 11, 22, 19, 15};
FLinearColor StationColor(int32 Index)
{
    const FLinearColor Colors[] = {Coral, Marigold, Teal, Cobalt, Plum, Hex(0x17A3BF), Leaf};
    return Colors[FMath::Clamp(Index, 0, 6)];
}
FText PageName(int32 Index)
{
    const auto& Levels = gwgame::defenseLevels();
    if (Index == AGWGameMode::ExpeditionLevel)
        return GWText::Get(TEXT("Board.Mode.1"));
    if (Index == AGWGameMode::PlaylistLevel)
        return GWText::Get(TEXT("Board.Mode.2"));
    if (Index < 0 || Index >= static_cast<int32>(Levels.size()))
        return GWText::Get(TEXT("Tutorial.Title"));
    const FString Id = ANSI_TO_TCHAR(Levels[Index].id);
    return FText::FromString(Id + TEXT("  ·  ") + GWText::Get(TEXT("Level.") + Id).ToString());
}
} // namespace

int32 AGWHUD::NearStation(AGWGameState* S, const FGWUnitView* Local) const
{
    auto* C = Cast<AGWController>(PlayerOwner);
    // Riding in someone's tail, flying or lying as bait, a player is in no position to shop.
    if (!S || !S->Hub() || !Local || !Local->Alive || Local->FollowerOf >= 0 || Local->Bait > 0 ||
        Local->State == static_cast<uint8>(gwgame::UnitState::Launched))
        return -1;
    FVector At = Local->Position;
    if (C && C->WorldView)
        C->WorldView->VisualPosition(Local->Id, At);
    return gwgame::hubStationNear({At.X, At.Y});
}

bool AGWHUD::Interact()
{
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    const auto* P = PlayerOwner ? PlayerOwner->GetPlayerState<AGWPlayerState>() : nullptr;
    const FGWUnitView* Local = S && P ? S->FindUnit(P->UnitId) : nullptr;
    // The expedition: E at the shop's buyer opens the bag sheet to sell, and at a fallen teammate's outline (with nothing
    // of the floor's nearer) the revive sheet, paper & pen or not (everything else E does is the host's).
    if (S && S->Expedition())
    {
        if (OpenTrade())
            return true;
        FReach Reach;
        if (Local && ExpeditionReach(S, *Local, Reach) && Reach.Ghost >= 0 && !Settings)
        {
            ReviveTarget = Reach.Ghost;
            return OpenRevive(true);
        }
        return false;
    }
    const int32 Station = NearStation(S, Local);
    if (Station < 0 || Settings || Panel != EPanel::None)
        return false;
    OpenPanel(static_cast<EPanel>(Station + 1));
    return true;
}

void AGWHUD::OpenPanel(EPanel Next)
{
    Panel = Next;
    if (Next != EPanel::None)
        if (auto* GI = GetGameInstance<UGWGameInstance>())
            GI->MarkStationSeen(static_cast<int32>(Next) - 1);
    // The board opens on the page the host is looking at (a guest cannot change it).
    if (auto* S = GetWorld()->GetGameState<AGWGameState>(); S && Next == EPanel::Board && !LocalHost(S))
        BoardLevel = S->BoardLevel;
}

void AGWHUD::HubScreen(AGWGameState* S, const FGWUnitView* Local)
{
    const float Entry = SceneAge(S);
    StationTags(S, Local);
    {
        const auto Drop = DropIn(FVector2D(Width * .5f, 58 * Scale), Entry, .1f, 70);
        RoomCard(S);
    }
    {
        const auto Slide = SlideIn(FVector2D(170 * Scale, 200 * Scale), Entry, FVector2D(-200, 0), .22f);
        Roster(S, Local);
    }
    DeployBanner(S);
    if (Panel != EPanel::None)
        HubPanel(S);
}

void AGWHUD::RoomCard(AGWGameState* S)
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    const EGWRoomKind Kind = GI ? GI->RoomKind() : EGWRoomKind::Offline;
    FString HostName;
    for (const auto& Item : S->PlayerArray)
        if (const auto* PS = Cast<AGWPlayerState>(Item); PS && PS->Identity == S->HostIdentity)
            HostName = PS->GetPlayerName();
    FText Title = GWText::Get(TEXT("Hub.Mine"));
    if (!LocalHost(S))
    {
        FFormatNamedArguments Args;
        Args.Add(TEXT("Name"), FText::FromString(HostName));
        Title = FText::Format(GWText::Get(TEXT("Hub.Title")), Args);
    }
    const float W = FMath::Min(720 * Scale, Width * .52f), H = 84 * Scale, X = (Width - W) * .5f, Y = 16 * Scale;
    const FLinearColor Accent = Kind == EGWRoomKind::Steam ? Teal : Kind == EGWRoomKind::Direct ? Cobalt : Pencil;
    Card(X, Y, W, H, Paper, Ink, 1101, 1.3f);
    Tape(FVector2D(X + 18 * Scale, Y + 10 * Scale), 60 * Scale, 18 * Scale, -38, Accent);
    Glyph(4, FVector2D(X + 46 * Scale, Y + H * .5f), 40 * Scale, Coral, 1102);
    const float TextX = X + 80 * Scale, ButtonW = 190 * Scale, TextW = W - (TextX - X) - ButtonW - 30 * Scale;
    Label(Title, TextX, Y + 9 * Scale, 24 * Scale, Ink, false, TextW, EFace::Heavy);
    // Where the room stands, as three marker chips: who can come in (a check, a cross or a link), how full it is,
    // and the trolling level the host has set.
    float ChipX = TextX - 4 * Scale;
    auto Chip = [&](int32 GlyphKind, FLinearColor Color, const FText& Text, int32 Seed)
    {
        const float TextWidth = Measure(Text, 15 * Scale, EFace::Bold).X, ChipW = TextWidth + 34 * Scale;
        if (ChipX + ChipW > TextX + TextW)
            return;
        Highlight(ChipX, Y + 48 * Scale, ChipW, 22 * Scale, Color.CopyWithNewOpacity(.24f), Seed);
        Glyph(GlyphKind, FVector2D(ChipX + 13 * Scale, Y + 59 * Scale), 14 * Scale, Color, Seed + 1);
        Label(Text, ChipX + 26 * Scale, Y + 48 * Scale, 15 * Scale, InkSoft, false, 0, EFace::Bold);
        ChipX += ChipW + 8 * Scale;
    };
    const int32 PrankLevel = FMath::Clamp(static_cast<int32>(S->Prank), 0, 2);
    Chip(Kind == EGWRoomKind::Steam ? 16 : Kind == EGWRoomKind::Offline ? 17 : 14, Accent, GWText::Get(Kind == EGWRoomKind::Steam ? TEXT("Hub.KindShort.1")
                                                                                                : Kind == EGWRoomKind::Offline ? TEXT("Hub.KindShort.0")
                                                                                                                               : TEXT("Hub.KindShort.2")),
         1103);
    {
        PushMotion(FVector2D(ChipX + 30 * Scale, Y + 59 * Scale), FVector2D::ZeroVector, 0, Bump(0x5EA7u, static_cast<float>(S->PlayerArray.Num())));
        const FMotionGuard Pulse(this);
        Chip(0, Coral, FText::FromString(FString::Printf(TEXT("%d / 8"), S->PlayerArray.Num())), 1105);
    }
    Chip(15, PrankLevel == 2 ? Coral : PrankLevel == 1 ? Teal : Pencil, GWText::Get(FString::Printf(TEXT("Prank.%d"), PrankLevel)), 1107);
    Button(GWDefenseAction::Invite, GWText::Get(TEXT("Hub.Invite")), X + W - ButtonW - 16 * Scale, Y + 16 * Scale, ButtonW, 52 * Scale,
           Kind == EGWRoomKind::Steam, false, Cobalt);
}

void AGWHUD::Roster(AGWGameState* S, const FGWUnitView* Local)
{
    const bool Host = LocalHost(S);
    const int32 Rows = FMath::Min(S->PlayerArray.Num(), 8);
    const float X = 22 * Scale, Y = 92 * Scale, W = 310 * Scale, RowH = 34 * Scale;
    const float H = 46 * Scale + Rows * RowH + 54 * Scale;
    Card(X, Y, W, H, Paper.CopyWithNewOpacity(.96f), Ink, 1110, 1.1f);
    Tape(FVector2D(X + W - 24 * Scale, Y + 6 * Scale), 54 * Scale, 16 * Scale, 34, Teal);
    Label(GWText::Number(TEXT("Hub.Here"), TEXT("Count"), S->PlayerArray.Num()), X + 16 * Scale, Y + 10 * Scale, 16 * Scale, InkSoft, false,
          W * .7f, EFace::Bold);
    for (int32 I = 0; I < Rows; ++I)
    {
        const auto* PS = Cast<AGWPlayerState>(S->PlayerArray[I]);
        if (!PS)
            continue;
        const FGWUnitView* U = S->FindUnit(PS->UnitId);
        const int32 SeatIndex = U && U->Seat >= 0 ? U->Seat : I;
        const bool IsHost = PS->Identity == S->HostIdentity, Me = PS == PlayerOwner->GetPlayerState<AGWPlayerState>();
        const float RY = Y + 42 * Scale + I * RowH;
        Disc(FVector2D(X + 26 * Scale, RY + 12 * Scale), 9 * Scale, Seat(SeatIndex));
        Circle(FVector2D(X + 26 * Scale, RY + 12 * Scale), 9 * Scale, Ink, 1.4f * Scale, 1120 + I);
        const EFace Face = Me ? EFace::Bold : EFace::Body;
        const FText Tag = GWText::Get(TEXT("Throwable.Tag"));
        const float TagW = PS->Throwable ? Measure(Tag, 12 * Scale, EFace::Heavy).X + 12 * Scale : 0;
        const float Room = W - 60 * Scale - (Host && !IsHost ? 76 * Scale : 0) - (IsHost ? 24 * Scale : 0) - (TagW > 0 ? TagW + 8 * Scale : 0);
        // A long name is cut short with an ellipsis (shrinking it would end up unreadable under the tags).
        FString Shown = PS->GetPlayerName();
        if (Measure(FText::FromString(Shown), 16 * Scale, Face).X > Room)
        {
            while (Shown.Len() > 1 && Measure(FText::FromString(Shown + TEXT("\u2026")), 16 * Scale, Face).X > Room)
                Shown.LeftChopInline(1);
            Shown += TEXT("\u2026");
        }
        const FText PlayerName = FText::FromString(Shown);
        Label(PlayerName, X + 44 * Scale, RY + 2 * Scale, 16 * Scale, Me ? Ink : InkSoft, false, 0, Face);
        float TagX = X + 50 * Scale + Measure(PlayerName, 16 * Scale, Face).X;
        if (IsHost)
        {
            // The host wears the crown on the roster (and only the host deploys).
            Glyph(18, FVector2D(TagX + 9 * Scale, RY + 12 * Scale), 18 * Scale, Marigold, 1130 + I);
            TagX += 24 * Scale;
        }
        if (TagW > 0)
        {
            Highlight(TagX, RY + 2 * Scale, TagW, 19 * Scale, Seat(SeatIndex).CopyWithNewOpacity(.4f), 1140 + I);
            Label(Tag, TagX + TagW * .5f, RY + 3 * Scale, 12 * Scale, Ink, true, 0, EFace::Heavy);
        }
        if (Host && !IsHost)
            Button(GWDefenseAction::Kick + I, GWText::Get(TEXT("Room.Kick")), X + W - 74 * Scale, RY - 3 * Scale, 62 * Scale, 30 * Scale, true,
                   false, Danger);
    }
    // This player's own permission to be grabbed and thrown (it travels with them to every room).
    if (const auto* Prefs = UGWUserSettings::Get())
        Button(GWDefenseAction::Throwable, GWText::Get(Prefs->Throwable ? TEXT("Throwable.On") : TEXT("Throwable.Off")), X + 12 * Scale,
               Y + H - 48 * Scale, W - 24 * Scale, 38 * Scale, true, Prefs->Throwable, Coral);
    (void)Local;
}

bool AGWHUD::EdgePointer(AGWGameState* S, FVector World, int32 GlyphKind, const FText& Caption, FLinearColor Color, int32 Seed, float TopEdge)
{
    // The play area: below the corner card, above the player cards.
    const float Left = 70 * Scale, Right = Width - 70 * Scale, Top = TopEdge, Bottom = Height - 220 * Scale;
    FVector2D P;
    if (!WorldToScreen(S, World, P) || (P.X > Left && P.X < Right && P.Y > Top && P.Y < Bottom))
        return false;
    const FVector2D Mid((Left + Right) * .5f, (Top + Bottom) * .5f);
    const FVector2D Dir = (P - Mid).GetSafeNormal();
    if (Dir.IsNearlyZero())
        return false;
    const float ToX = Dir.X > 0 ? (Right - Mid.X) / Dir.X : Dir.X < 0 ? (Left - Mid.X) / Dir.X : 1e6f;
    const float ToY = Dir.Y > 0 ? (Bottom - Mid.Y) / Dir.Y : Dir.Y < 0 ? (Top - Mid.Y) / Dir.Y : 1e6f;
    const FVector2D Side(-Dir.Y, Dir.X);
    const FVector2D Tip = Mid + Dir * (FMath::Min(ToX, ToY) + 4 * Scale * FMath::Sin(Clock * 5 + Seed));
    // The tag sits behind the arrow, toward the middle of the screen.
    const float TextW = Caption.IsEmpty() ? 0 : Measure(Caption, 14 * Scale, EFace::Bold).X;
    const float W = 34 * Scale + (TextW > 0 ? TextW + 10 * Scale : 0), H = 30 * Scale;
    const FVector2D Center = Tip - Dir * (FMath::Abs(Dir.X) * W * .5f + FMath::Abs(Dir.Y) * H * .5f + 20 * Scale);
    Card(Center.X - W * .5f, Center.Y - H * .5f, W, H, Paper, Ink, Seed, 1.f);
    Glyph(GlyphKind, FVector2D(Center.X - W * .5f + 17 * Scale, Center.Y), 18 * Scale, Color, Seed + 1);
    if (TextW > 0)
        Label(Caption, Center.X - W * .5f + 32 * Scale, Center.Y - 9 * Scale, 14 * Scale, Ink, false, 0, EFace::Bold);
    // A dart, long and notched, so which way it points reads at a glance.
    Polygon({Tip + Dir * 9 * Scale, Tip - Dir * 15 * Scale + Side * 10 * Scale, Tip - Dir * 8 * Scale, Tip - Dir * 15 * Scale - Side * 10 * Scale}, Color,
            Ink, 1.5f * Scale, Seed + 2);
    return true;
}

void AGWHUD::StationTags(AGWGameState* S, const FGWUnitView* Local)
{
    const int32 Near = NearStation(S, Local);
    const auto& Spots = gwgame::hubSpots();
    const float Fit = gwgame::HubCell / 400;
    auto* GI = GetGameInstance<UGWGameInstance>();
    for (int32 I = 0; I < static_cast<int32>(Spots.size()); ++I)
    {
        const gwgame::HubSpot& Spot = Spots[I];
        // A paper tag pinned above the station's front edge, at the height of its props; a station out of view
        // gets a small tag on the screen edge pointing to it instead.
        const FVector Anchor(Spot.center.x + Spot.facing.x * 120 * Fit, Spot.center.y + Spot.facing.y * 120 * Fit, 330 * Fit);
        const FVector Front(Spot.front.x, Spot.front.y, 0);
        FVector2D P, Floor;
        if (!WorldToScreen(S, Front, Floor) || Floor.X < 0 || Floor.Y < 0 || Floor.X > Width || Floor.Y > Height)
        {
            // Only stations this save has never opened point the way (a new player is shown around once).
            if (Panel == EPanel::None && !Settings && GI && !GI->StationSeen(I))
                EdgePointer(S, Front, StationGlyphs[I], GWText::Get(FString::Printf(TEXT("Station.%d"), I)), StationColor(I), 1170 + I * 3,
                            250 * Scale);
            continue;
        }
        // Its floor ring is in view: the tag stays on screen below the room card even when the props reach
        // past the top edge.
        if (!WorldToScreen(S, Anchor, P))
            P = Floor;
        P.Y = FMath::Max(P.Y, 150 * Scale);
        const bool Here = I == Near && Panel == EPanel::None && !Settings;
        const FText Caption = GWText::Get(FString::Printf(TEXT("Station.%d"), I));
        const FText Use = GWText::Get(TEXT("Hub.Open"));
        const float TextW = Measure(Caption, 17 * Scale, EFace::Heavy).X, UseW = Here ? Measure(Use, 15 * Scale, EFace::Bold).X + 48 * Scale : 0;
        const float W = TextW + 54 * Scale + UseW, H = 38 * Scale;
        // Kept whole on screen when the station is at the edge of the view (its pointer stays under it).
        P.X = FMath::Clamp(P.X, W * .5f + 8 * Scale, Width - W * .5f - 8 * Scale);
        const float X = P.X - W * .5f, Y = P.Y - H - (Here ? 3 * Scale * FMath::Sin(Clock * 5) : 0);
        // A tag pops up off its station whenever it comes into view, and grows a little while the player is at it.
        const auto Pop = PopIn(FVector2D(P.X, Y + H), FMath::Min(Since(HashCombine(static_cast<uint32>(I), 0x57A7u)), SceneAge(S)), .3f + I * .05f);
        PushMotion(FVector2D(P.X, Y + H), FVector2D::ZeroVector, 0, 1 + .06f * Eased(HashCombine(static_cast<uint32>(I), 0x57A8u), Here ? 1.f : 0.f, 12));
        const FMotionGuard Grown(this);
        Card(X, Y, W, H, Here ? Hex(0xFFF2B0) : Paper, Ink, 1150 + I, Here ? 1.5f : 1.f);
        if (Here)
            Polygon({FVector2D(P.X - 9 * Scale, Y + H - 1), FVector2D(P.X + 9 * Scale, Y + H - 1), FVector2D(P.X, Y + H + 13 * Scale)},
                    Hex(0xFFF2B0), Ink, 1.6f * Scale, 1157 + I);
        Glyph(StationGlyphs[I], FVector2D(X + 21 * Scale, Y + H * .5f), 22 * Scale, StationColor(I), 1160 + I);
        Label(Caption, X + 40 * Scale, Y + 8 * Scale, 17 * Scale, Ink, false, 0, EFace::Heavy);
        if (Here)
        {
            Keycap(FText::FromString(TEXT("E")), X + 48 * Scale + TextW, Y + 5 * Scale, 28 * Scale, Ink);
            Label(Use, X + 82 * Scale + TextW, Y + 10 * Scale, 15 * Scale, InkSoft, false, 0, EFace::Bold);
        }
    }
}

void AGWHUD::DeployBanner(AGWGameState* S)
{
    const float Left = S->DeployIn();
    if (Left < 0)
        return;
    FFormatNamedArguments Args;
    Args.Add(TEXT("Page"), PageName(S->DeployLevel));
    Args.Add(TEXT("Seconds"), FMath::Max(1, FMath::CeilToInt(Left)));
    const FText Text = FText::Format(GWText::Get(TEXT("Hub.Deploying")), Args);
    const float Size = 30 * Scale, W = FMath::Max(560 * Scale, Measure(Text, Size, EFace::Heavy).X + 130 * Scale), Y = Height * .27f;
    const float Pulse = .5f + .5f * FMath::Sin(Clock * 8), Age = Since(HashCombine(S->Seed, GetTypeHash(S->DeployAt)));
    // The tape slaps on and unrolls; every second of the count jolts it.
    const auto Pop = PopIn(FVector2D(Width * .5f, Y + 26 * Scale), Age);
    PushMotion(FVector2D(Width * .5f, Y + 26 * Scale), FVector2D::ZeroVector, 0, Bump(0xDE91u, static_cast<float>(FMath::CeilToInt(Left))));
    const FMotionGuard Jolt(this);
    Tape(FVector2D(Width * .5f, Y + 26 * Scale), W * GWEase::OutBack(Age / .3f), 66 * Scale, -2, FMath::Lerp(Coral, Marigold, Pulse * .3f).CopyWithNewOpacity(.94f));
    Lettering(Text, Width * .5f, Y + 4 * Scale, Size, Ink, 1170, Age - .05f);
    if (LocalHost(S))
        Button(GWDefenseAction::DeployCancel, GWText::Get(TEXT("Hub.CancelDeploy")), Width * .5f - 110 * Scale, Y + 74 * Scale, 220 * Scale,
               48 * Scale, true, false, Pencil);
}

void AGWHUD::PanelFrame(const TCHAR* TitleKey, const TCHAR* SubtitleKey, FLinearColor Accent, float Tall, float& X, float& Y, float& W, float& H)
{
    const float PW = FMath::Min(1140 * Scale, Width * .88f), PH = FMath::Min(Tall * Scale, Height * .86f);
    const float PX = (Width - PW) * .5f, PY = (Height - PH) * .5f;
    Card(PX, PY, PW, PH, Paper, Ink, 1200, 2.4f, 2.4f * Scale);
    Tape(FVector2D(PX + PW * .5f, PY + 4 * Scale), 150 * Scale, 28 * Scale, -2, Accent.CopyWithNewOpacity(.9f));
    const FText Title = GWText::Get(TitleKey);
    const float TitleSize = 40 * Scale, TitleW = Measure(Title, TitleSize, EFace::Heavy).X, TitleX = PX + 42 * Scale;
    Highlight(TitleX - 6 * Scale, PY + 30 * Scale + TitleSize * .72f, TitleW + 12 * Scale, 12 * Scale, Accent.CopyWithNewOpacity(.4f), 1201);
    Lettering(Title, TitleX + TitleW * .5f, PY + 26 * Scale, TitleSize, Ink, 1202);
    float Below = PY + 26 * Scale + TitleSize * 1.5f;
    if (SubtitleKey)
    {
        Key(SubtitleKey, TitleX + 2 * Scale, Below, 17 * Scale, InkSoft, false, PW - 240 * Scale, EFace::Bold);
        Below += 34 * Scale;
    }
    Button(GWDefenseAction::PanelClose, FText::FromString(TEXT("×")), PX + PW - 78 * Scale, PY + 24 * Scale, 54 * Scale, 50 * Scale);
    X = TitleX;
    Y = Below + 16 * Scale;
    W = PW - 84 * Scale;
    H = PY + PH - 30 * Scale - Y;
}

void AGWHUD::HubPanel(AGWGameState* S)
{
    // A station sheet is modal: the room behind it takes no clicks until it is closed (Esc or ×). It is slapped
    // down on the table over a dimming room.
    ClearMotions();
    const int32 Kind = static_cast<int32>(Panel) - 1;
    const float Age = Since(HashCombine(static_cast<uint32>(Kind), 0x5EE7u));
    Box(0, 0, Width, Height, Ink.CopyWithNewOpacity(.38f * GWEase::Clamp01(Age / .2f)));
    Hits.Reset();
    const FString TitleKey = FString::Printf(TEXT("Station.%d"), Kind);
    float X = 0, Y = 0, W = 0, H = 0;
    // Each sheet is as tall as what it holds: the board fills the page, the rest are shorter notes.
    const float Tall = Panel == EPanel::Board ? 660 : Panel == EPanel::Door ? 520 : Panel == EPanel::Training ? 560 : 660;
    const auto Drop = DropIn(FVector2D(Width * .5f, Height * .5f), Age, 0, 120);
    PanelFrame(*TitleKey, nullptr, StationColor(Kind), Tall, X, Y, W, H);
    switch (Panel)
    {
    case EPanel::Board:
        BoardPanel(S, X, Y, W, H);
        break;
    case EPanel::Achievements:
        AchievementsPanel(S, X, Y, W, H);
        break;
    case EPanel::Door:
        DoorPanel(S, X, Y, W, H);
        break;
    case EPanel::Training:
        TrainingPanel(S, X, Y, W, H);
        break;
    case EPanel::Shop:
        ShopPanel(S, X, Y, W, H);
        break;
    case EPanel::Research:
        ResearchPanel(S, X, Y, W, H);
        break;
    case EPanel::Wardrobe:
        WardrobePanel(S, X, Y, W, H);
        break;
    default:
        break;
    }
    // A sheet's fine print shows only where the mouse asks for it: a sticky note beside the pointer.
    float MX = 0, MY = 0;
    if (!PanelTip.IsEmpty() && PlayerOwner && PlayerOwner->GetMousePosition(MX, MY))
    {
        ClearMotions();
        const float TipW = FMath::Min(360 * Scale, Width * .3f), TipH = Paragraph(PanelTip, 0, -1000, 15 * Scale, TipW - 28 * Scale, FLinearColor::Transparent) + 24 * Scale;
        const float TX = FMath::Min(MX + 18 * Scale, Width - TipW - 8 * Scale), TY = FMath::Min(MY + 22 * Scale, Height - TipH - 8 * Scale);
        const auto Pop = PopIn(FVector2D(TX, TY), Since(GetTypeHash(PanelTip.ToString())));
        Card(TX, TY, TipW, TipH, Hex(0xFFF7D6), Ink, 1290, 1.3f, 1.6f * Scale);
        Paragraph(PanelTip, TX + 14 * Scale, TY + 12 * Scale, 15 * Scale, TipW - 28 * Scale, Ink);
    }
    PanelTip = FText::GetEmpty();
}

void AGWHUD::BoardPanel(AGWGameState* S, float X, float Y, float W, float H)
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    const bool Host = LocalHost(S);
    UGWProfileSave* Profile = GI ? GI->ActiveProfile() : nullptr;
    if (!Host)
        BoardLevel = S->BoardLevel;
    else if (Profile && !Profile->Unlocked(BoardLevel))
        BoardLevel = 0;
    // Modes: line defense, the expedition and the party playlist (the host's pick; a guest sees the pages).
    const int32 Mode = Host ? BoardMode : 0;
    const float Gap = 18 * Scale, TabH = 44 * Scale, TabW = FMath::Min(250 * Scale, (W - 2 * Gap) / 3);
    const FLinearColor ModeColors[3] = {Coral, Cobalt, Magenta};
    for (int32 I = 0; I < 3; ++I)
        Button(GWDefenseAction::Mode + I, GWText::Get(FString::Printf(TEXT("Board.Mode.%d"), I)), X + I * (TabW + Gap), Y, TabW, TabH, Host || I == 0,
               Mode == I, ModeColors[I]);
    // Chapters: three notebooks of three pages, one notebook open at a time (the selected page's, unless the host
    // turned to another tab).
    const auto& Levels = gwgame::defenseLevels();
    const int32 Shown = FMath::Clamp(Host && BoardChapter >= 0 ? BoardChapter : Levels[FMath::Clamp(BoardLevel, 0, static_cast<int32>(Levels.size()) - 1)].id[0] - '1', 0, 2);
    const float ChapterW = FMath::Min(150 * Scale, (W - 3 * (TabW + Gap)) / 3 - 8 * Scale);
    const FLinearColor ChapterColors[] = {Teal, Marigold, Plum};
    TArray<int32> Pages;
    for (int32 I = 0; I < static_cast<int32>(Levels.size()); ++I)
        if (Levels[I].id[0] - '1' == Shown)
            Pages.Add(I);
    const int32 Count = FMath::Max(1, Pages.Num());
    const float CardY = Y + TabH + 18 * Scale, CardW = (W - (Count - 1) * Gap) / Count, CardH = FMath::Min(H * .5f, CardW * .78f);
    if (Mode == 0)
    {
        for (int32 I = 0; I < 3; ++I)
            Button(GWDefenseAction::Chapter + I, GWText::Number(TEXT("Board.ChapterTab"), TEXT("Index"), I + 1),
                   X + W - (3 - I) * (ChapterW + 8 * Scale) + 8 * Scale, Y, ChapterW, TabH, true, Shown == I, ChapterColors[I]);
        for (int32 K = 0; K < Pages.Num(); ++K)
            LevelCard(Pages[K], X + K * (CardW + Gap), CardY, CardW, CardH, !Host);
    }
    else
        ModeCard(S, X, CardY, W, CardH);
    // Under the pages: the stars, the AI teammates, the trolling level and the deploy button.
    const float RowY = CardY + CardH + 18 * Scale, RowH = Y + H - RowY;
    const float ObjW = W * .27f, BotW = W * .21f, PrankW = W * .27f;
    const float BotX = X + ObjW + Gap, PrankX = BotX + BotW + Gap, GoX = PrankX + PrankW + Gap, GoW = X + W - GoX;
    Card(X, RowY, ObjW, RowH, Paper.CopyWithNewOpacity(.96f), Ink, 1210, 1.f);
    // A page's three stars; the expedition and the playlist say how they play instead.
    Key(Mode == 0 ? TEXT("Defense.Objectives") : TEXT("Board.HowItPlays"), X + 16 * Scale, RowY + 12 * Scale, 16 * Scale, InkSoft, false, ObjW - 32 * Scale,
        EFace::Bold);
    if (Mode != 0)
        Paragraph(GWText::Get(FString::Printf(TEXT("Mode.%d.Facts"), Mode)), X + 16 * Scale, RowY + 42 * Scale, 15 * Scale, ObjW - 32 * Scale, Ink);
    for (int32 I = 0; I < 3 && Mode == 0; ++I)
    {
        const float LY = RowY + 42 * Scale + I * 26 * Scale;
        if (LY > RowY + RowH - 18 * Scale)
            break;
        Glyph(5, FVector2D(X + 26 * Scale, LY + 9 * Scale), 18 * Scale, Marigold, 1211 + I);
        Key(*FString::Printf(TEXT("Star.%d"), I), X + 42 * Scale, LY, 15 * Scale, Ink, false, ObjW - 56 * Scale);
    }
    Card(BotX, RowY, BotW, RowH, Paper.CopyWithNewOpacity(.96f), Ink, 1215, 1.f);
    Key(TEXT("Board.Bots"), BotX + 16 * Scale, RowY + 12 * Scale, 16 * Scale, InkSoft, false, BotW - 32 * Scale, EFace::Bold);
    if (Mode == 1)
    {
        // The expedition is played by people only (its kits, ghosts and revive drawings are theirs).
        const float PickW = (BotW - 32 * Scale - 3 * 8 * Scale) / 4;
        for (int32 I = 0; I < 4; ++I)
            Button(GWDefenseAction::Bots + I, FText::AsNumber(I), BotX + 16 * Scale + I * (PickW + 8 * Scale), RowY + 40 * Scale, PickW, 38 * Scale,
                   false, I == 0, Cobalt);
        if (Hovered(BotX, RowY, BotW, RowH))
            PanelTip = GWText::Get(TEXT("Board.BotsExpedition"));
    }
    else if (S->PlayerArray.Num() <= 1)
    {
        const float PickW = (BotW - 32 * Scale - 3 * 8 * Scale) / 4;
        for (int32 I = 0; I < 4; ++I)
            Button(GWDefenseAction::Bots + I, FText::AsNumber(I), BotX + 16 * Scale + I * (PickW + 8 * Scale), RowY + 40 * Scale, PickW, 38 * Scale,
                   Host, BoardBots == I, Cobalt);
        // What bringing AI teammates costs is a tooltip, not a paragraph.
        if (Hovered(BotX, RowY, BotW, RowH))
            PanelTip = GWText::Get(TEXT("Board.BotsNote"));
    }
    else
    {
        // With friends in the room no AI comes along: the picks show greyed at zero.
        const float PickW = (BotW - 32 * Scale - 3 * 8 * Scale) / 4;
        for (int32 I = 0; I < 4; ++I)
            Button(GWDefenseAction::Bots + I, FText::AsNumber(I), BotX + 16 * Scale + I * (PickW + 8 * Scale), RowY + 40 * Scale, PickW, 38 * Scale,
                   false, I == 0, Cobalt);
        if (Hovered(BotX, RowY, BotW, RowH))
            PanelTip = GWText::Get(TEXT("Board.BotsFriends"));
    }
    // Who comes along, drawn: the player and the picked AI teammates in a row (the friends in the room instead),
    // hopping in place, with a dashed spot for each teammate who could still come; one pops in when the pick goes up.
    {
        const bool Alone = S->PlayerArray.Num() <= 1;
        const int32 Crew = FMath::Clamp(Alone ? 1 + (Mode == 1 ? 0 : BoardBots) : S->PlayerArray.Num(), 1, 8), Spots = Alone ? 4 : Crew;
        const float Top = RowY + 86 * Scale, Room = RowY + RowH - 12 * Scale - Top;
        if (Room > 30 * Scale)
        {
            const float Step = FMath::Min(40 * Scale, (BotW - 40 * Scale) / Spots), Size = FMath::Min(26 * Scale, Step * .8f);
            const float Left = BotX + BotW * .5f - Step * (Spots - 1) * .5f, Ground = Top + Room * .5f + Size * .4f;
            Pen(FVector2D(BotX + 20 * Scale, Ground + Size * .55f), FVector2D(BotX + BotW - 20 * Scale, Ground + Size * .55f),
                Pencil.CopyWithNewOpacity(.5f), 1.2f * Scale, 1280, .5f);
            for (int32 I = Crew; I < Spots; ++I)
                for (int32 K = 0; K < 8; K += 2)
                    Arc(FVector2D(Left + I * Step, Ground), Size * .42f, K * PI / 4 + .2f, (K + 1) * PI / 4 + .2f, Pencil.CopyWithNewOpacity(.55f),
                        1.4f * Scale);
            for (int32 I = 0; I < Crew; ++I)
            {
                const FVector2D At(Left + I * Step, Ground - FMath::Abs(FMath::Sin(static_cast<float>(Clock) * 3.2f + I * 1.3f)) * 5 * Scale);
                const auto Pop = PopIn(At, Since(HashCombine(0xC4E3u, static_cast<uint32>(I))));
                Pen(At, At + FVector2D(Size * .8f, 0), InkSoft, Size * .2f, 1281 + I, .2f);
                Glyph(0, At, Size, GWPalette::Seat(I), 1290 + I);
            }
        }
    }
    // The room's trolling level: the host chooses, everyone reads what it allows.
    Card(PrankX, RowY, PrankW, RowH, Paper.CopyWithNewOpacity(.96f), Ink, 1220, 1.f);
    Key(TEXT("Prank.Title"), PrankX + 16 * Scale, RowY + 12 * Scale, 16 * Scale, InkSoft, false, PrankW - 32 * Scale, EFace::Bold);
    const int32 PrankIndex = FMath::Clamp(static_cast<int32>(S->Prank), 0, 2);
    const FLinearColor Accents[3] = {Pencil, Teal, Coral};
    const float PickW = (PrankW - 32 * Scale - 2 * 8 * Scale) / 3;
    for (int32 I = 0; I < 3; ++I)
        Button(GWDefenseAction::Prank + I, GWText::Get(FString::Printf(TEXT("Prank.%d"), I)), PrankX + 16 * Scale + I * (PickW + 8 * Scale),
               RowY + 40 * Scale, PickW, 38 * Scale, Host, PrankIndex == I, Accents[I]);
    if (Hovered(PrankX, RowY, PrankW, RowH))
        PanelTip = GWText::Get(FString::Printf(TEXT("Prank.%d.Note"), PrankIndex));
    // The level, drawn: a shape lobbed along a dotted arc between two teammates (crossed out when it is off, a
    // juggle of three spinning shapes in chaos).
    {
        const float Top = RowY + 86 * Scale, Bottom = RowY + RowH - 14 * Scale;
        if (Bottom - Top > 30 * Scale)
        {
            const float Size = 20 * Scale;
            const FVector2D From(PrankX + 34 * Scale, Bottom - Size * .5f), To(PrankX + PrankW - 34 * Scale, Bottom - Size * .5f);
            const float Peak = FMath::Min(Bottom - Top - Size * 1.2f, 50 * Scale);
            auto ArcAt = [&](float T) { return FMath::Lerp(From, To, T) - FVector2D(0, Peak * 4 * T * (1 - T)); };
            for (int32 K = 1; K < 14; ++K)
                Disc(ArcAt(K / 14.f), 1.8f * Scale, InkSoft.CopyWithNewOpacity(PrankIndex == 0 ? .3f : .65f), 8);
            Glyph(0, From, Size, GWPalette::Seat(0), 1300);
            Glyph(0, To, Size, GWPalette::Seat(1), 1301);
            if (PrankIndex == 0)
                Glyph(17, ArcAt(.5f), Size, Danger, 1302);
            else
                for (int32 K = 0; K < (PrankIndex == 1 ? 1 : 3); ++K)
                {
                    const float T = FMath::Frac(static_cast<float>(Clock) / (PrankIndex == 1 ? 1.9f : 1.15f) + K / 3.f);
                    const FVector2D At = ArcAt(T);
                    PushMotion(At, FVector2D::ZeroVector, T * (PrankIndex == 1 ? 360.f : 720.f));
                    const FMotionGuard Spin(this);
                    Glyph(K == 0 ? 3 : K == 1 ? 1 : 4, At, Size * .85f, K == 0 ? Accents[PrankIndex] : K == 1 ? Marigold : Cobalt, 1303 + K);
                }
        }
    }
    Card(GoX, RowY, GoW, RowH, Paper, Ink, 1225, 1.4f);
    Tape(FVector2D(GoX + GoW * .5f, RowY + 4 * Scale), 80 * Scale, 20 * Scale, 3, Coral);
    Label(PageName(Mode == 1 ? AGWGameMode::ExpeditionLevel : Mode == 2 ? AGWGameMode::PlaylistLevel : BoardLevel), GoX + 16 * Scale, RowY + 18 * Scale,
          18 * Scale, Ink, false, GoW - 32 * Scale, EFace::Heavy);
    if (Host)
        Button(GWDefenseAction::Deploy, GWText::Get(Mode == 2 ? TEXT("Board.Roll") : TEXT("Board.Play")), GoX + 16 * Scale, RowY + RowH - 72 * Scale,
               GoW - 32 * Scale, 56 * Scale,
               S->DeployIn() < 0 && GI && !GI->Busy, false, Coral);
    else
        Paragraph(GWText::Get(TEXT("Hub.HostDeploys")), GoX + 16 * Scale, RowY + 54 * Scale, 15 * Scale, GoW - 32 * Scale, InkSoft);
}

void AGWHUD::ModeCard(AGWGameState* S, float X, float Y, float W, float H)
{
    // The expedition's or the playlist's page on the board: what it is, in a few lines, beside a doodle of it.
    const int32 Mode = FMath::Clamp(BoardMode, 1, 2);
    const FLinearColor Accent = Mode == 1 ? Cobalt : Magenta;
    Card(X, Y, W, H, Paper, Ink, 1240, 1.3f);
    Tape(FVector2D(X + W * .5f, Y + 4 * Scale), 120 * Scale, 24 * Scale, -3, Accent.CopyWithNewOpacity(.85f));
    const float ArtW = FMath::Min(W * .32f, H * 1.1f);
    if (Mode == 1)
    {
        // Four stages of 3, 4, 5 and 6 floors down the page, each ending on its boss's crown, and the way out after the
        // last. The first stage is always pencil; the other three pens are drawn for each run.
        const float Left = X + 24 * Scale, Step = (ArtW - 48 * Scale) / gwgame::ExpeditionStages, Top = Y + 34 * Scale;
        const float Gap = (H - 78 * Scale) / 6;
        FVector2D LastCrown = FVector2D::ZeroVector;
        for (int32 Stage = 1; Stage <= gwgame::ExpeditionStages; ++Stage)
        {
            const int32 Floors = gwgame::stageFloors(Stage);
            const float CX = Left + (Stage - .5f) * Step;
            const FLinearColor Tone = Stage == 1 ? Biome(0) : Accent;
            for (int32 F = 0; F < Floors; ++F)
            {
                const FVector2D At(CX, Top + F * Gap);
                if (F > 0)
                    Pen(At - FVector2D(0, Gap - 7 * Scale), At - FVector2D(0, 7 * Scale), InkSoft, 1.4f * Scale, 1241 + Stage * 8 + F, .3f);
                if (F == Floors - 1)
                {
                    Glyph(18, At, 22 * Scale, Coral, 1280 + Stage);
                    if (Stage > 1)
                        for (int32 K = 1; K < 6; ++K)
                            Disc(FMath::Lerp(LastCrown, FVector2D(CX, Top), K / 6.f), 1.8f * Scale, Pencil);
                    LastCrown = At;
                }
                else
                    Disc(At, 5.5f * Scale, Tone, 12);
            }
            Label(FText::AsNumber(Stage), CX, Top - 30 * Scale, 14 * Scale, InkSoft, true, 0, EFace::Heavy);
        }
        RoomIcon(static_cast<int32>(gwgame::RoomKind::Event), static_cast<int32>(gwgame::RoomEvent::Exit), LastCrown + FVector2D(Step * .55f, 0), 26 * Scale, Teal, 1290);
    }
    else
        for (int32 I = 0; I < 3; ++I)
            Glyph(I == 0 ? 8 : I == 1 ? 11 : 15, FVector2D(X + ArtW * (.25f + .25f * I), Y + H * (.4f + .12f * (I % 2))), 46 * Scale,
                  I == 0 ? Coral : I == 1 ? Marigold : Teal, 1250 + I);
    const float TX = X + ArtW + 24 * Scale, TW = X + W - TX - 24 * Scale;
    Key(*FString::Printf(TEXT("Mode.%d.Title"), Mode), TX, Y + 22 * Scale, 26 * Scale, Ink, false, TW, EFace::Heavy);
    Paragraph(GWText::Get(FString::Printf(TEXT("Mode.%d.Note"), Mode)), TX, Y + 62 * Scale, 16 * Scale, TW, Ink);
    (void)S;
}

void AGWHUD::DoorPanel(AGWGameState* S, float X, float Y, float W, float H)
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    const EGWRoomKind Kind = GI ? GI->RoomKind() : EGWRoomKind::Offline;
    // In this player's own hub with Steam up, the right-hand column lists Steam friends who are in a room now, each
    // with a Join button: the way into a friend's hub that needs neither an invite nor the overlay.
    const bool Friends = GI && LocalHost(S) && GI->PlatformReady(EFU_OnlineProvider::Steam);
    if (Friends)
    {
        GI->RefreshFriendRooms();
        const float ListW = W * .4f, LX = X + W - ListW;
        Pen(FVector2D(LX - 22 * Scale, Y), FVector2D(LX - 22 * Scale, Y + H - 8 * Scale), Pencil.CopyWithNewOpacity(.4f), 1, 1262, .4f);
        Glyph(16, FVector2D(LX + 12 * Scale, Y + 15 * Scale), 24 * Scale, Teal, 1263);
        Label(GWText::Get(TEXT("Door.FriendRooms")), LX + 34 * Scale, Y, 22 * Scale, Ink, false, ListW - 34 * Scale, EFace::Heavy);
        float Row = Y + 46 * Scale;
        if (GI->FriendRooms.IsEmpty())
            Paragraph(GWText::Get(TEXT("Door.NoFriendRooms")), LX, Row, 16 * Scale, ListW, InkSoft);
        const float RowH = 54 * Scale, JoinW = FMath::Min(130 * Scale, ListW * .38f);
        for (int32 I = 0; I < GI->FriendRooms.Num() && I < 8 && Row + RowH <= Y + H; ++I)
        {
            Glyph(I % 2 ? 1 : 0, FVector2D(LX + 10 * Scale, Row + 21 * Scale), 14 * Scale, GWPalette::Seat(I), 1264 + I);
            Label(FText::FromString(GI->FriendRooms[I].Name), LX + 26 * Scale, Row + 8 * Scale, 19 * Scale, Ink, false, ListW - JoinW - 40 * Scale,
                  EFace::Bold);
            Button(GWDefenseAction::FriendJoin + I, GWText::Get(TEXT("Door.Join")), LX + ListW - JoinW, Row, JoinW, 44 * Scale, !GI->Busy, false, Teal);
            Pen(FVector2D(LX, Row + RowH - 4 * Scale), FVector2D(LX + ListW, Row + RowH - 4 * Scale), Pencil.CopyWithNewOpacity(.3f), 1, 1272 + I, .4f);
            Row += RowH;
        }
        W -= ListW + 44 * Scale;
    }
    float Cursor = Y;
    Glyph(Kind == EGWRoomKind::Steam ? 16 : Kind == EGWRoomKind::Offline ? 17 : 14, FVector2D(X + 18 * Scale, Cursor + 16 * Scale), 30 * Scale,
          Kind == EGWRoomKind::Steam ? Teal : Kind == EGWRoomKind::Offline ? Danger : Cobalt, 1260);
    Cursor += Paragraph(GWText::Get(FString::Printf(TEXT("Door.%d"), static_cast<int32>(Kind))), X + 46 * Scale, Cursor, 19 * Scale, W - 46 * Scale, Ink) +
              24 * Scale;
    const float ButtonW = FMath::Min(360 * Scale, W * (Friends ? .8f : .5f));
    if (Kind == EGWRoomKind::Offline)
        Button(GWDefenseAction::Reconnect, GWText::Get(TEXT("Door.Reconnect")), X, Cursor, ButtonW, 58 * Scale, GI && !GI->Busy, false, Teal);
    else
        Button(GWDefenseAction::Invite, GWText::Get(TEXT("Door.OpenInvite")), X, Cursor, ButtonW, 58 * Scale, Kind == EGWRoomKind::Steam, false,
               Cobalt);
    Cursor += 86 * Scale;
    // How friends get in, and when they cannot.
    for (const TCHAR* Line : {TEXT("Door.Friends"), TEXT("Door.Busy"), TEXT("Door.Full")})
    {
        if (Cursor > Y + H - 30 * Scale)
            break;
        Glyph(3, FVector2D(X + 12 * Scale, Cursor + 11 * Scale), 16 * Scale, Teal, 1261);
        Cursor += Paragraph(GWText::Get(Line), X + 32 * Scale, Cursor, 16 * Scale, W - 32 * Scale, InkSoft) + 14 * Scale;
    }
    (void)S;
}

void AGWHUD::TrainingPanel(AGWGameState* S, float X, float Y, float W, float H)
{
    // The training corner stands in for the tutorial an invited player skipped: every key, one line each.
    struct FRow
    {
        FText KeyText;
        const TCHAR* Text;
    };
    const FRow Rows[] = {{FText::FromString(TEXT("WASD")), TEXT("Training.Move")},  {GWText::Get(TEXT("Key.LMB")), TEXT("Training.Fire")},
                         {FText::FromString(TEXT("E")), TEXT("Training.Recruit")},   {FText::FromString(TEXT("F")), TEXT("Training.Tower")},
                         {FText::FromString(TEXT("Q")), TEXT("Training.Throw")},     {GWText::Get(TEXT("Key.RMB")), TEXT("Training.Draw")},
                         {GWText::Get(TEXT("Key.Enter")), TEXT("Training.Wave")},    {FText::FromString(TEXT("Esc")), TEXT("Training.Menu")}};
    const float ColW = (W - 30 * Scale) * .5f, RowH = 50 * Scale, Size = 30 * Scale;
    for (int32 I = 0; I < static_cast<int32>(UE_ARRAY_COUNT(Rows)); ++I)
    {
        const float RX = X + (I / 4) * (ColW + 30 * Scale), RY = Y + (I % 4) * RowH;
        Keycap(Rows[I].KeyText, RX, RY, Size, Ink);
        const float KeyW = FMath::Max(Size, Measure(Rows[I].KeyText, Size * .55f, EFace::Bold).X + Size * .55f);
        Paragraph(GWText::Get(Rows[I].Text), RX + KeyW + 12 * Scale, RY + 4 * Scale, 16 * Scale, ColW - KeyW - 16 * Scale, Ink);
    }
    float Cursor = Y + 4 * RowH + 14 * Scale;
    Pen(FVector2D(X, Cursor), FVector2D(X + W, Cursor), Pencil.CopyWithNewOpacity(.4f), 1, 1270, .4f);
    Cursor += 16 * Scale;
    Cursor += Paragraph(GWText::Get(TEXT("Training.Intro")), X, Cursor, 16 * Scale, W, InkSoft) + 12 * Scale;
    const auto* P = PlayerOwner->GetPlayerState<AGWPlayerState>();
    const FGWUnitView* Mine = P ? S->FindUnit(P->UnitId) : nullptr;
    FFormatNamedArguments Args;
    Args.Add(TEXT("Mine"), Mine ? Mine->Bullseyes : 0);
    Args.Add(TEXT("All"), S->HubBullseyes);
    Glyph(0, FVector2D(X + 12 * Scale, Cursor + 12 * Scale), 22 * Scale, Coral, 1271);
    Label(FText::Format(GWText::Get(TEXT("Training.Bullseye")), Args), X + 32 * Scale, Cursor, 18 * Scale, Ink, false, W - 32 * Scale, EFace::Bold);
    Cursor += 44 * Scale;
    // The tutorial itself can be replayed by a player alone in the room.
    const bool Alone = LocalHost(S) && S->PlayerArray.Num() <= 1;
    const float ButtonW = FMath::Min(300 * Scale, W * .4f);
    if (Cursor + 56 * Scale <= Y + H)
    {
        Button(GWDefenseAction::Tutorial, GWText::Get(TEXT("Training.Replay")), X, Cursor, ButtonW, 52 * Scale, Alone, false, Marigold);
        if (!Alone)
            Paragraph(GWText::Get(TEXT("Training.ReplayAlone")), X + ButtonW + 20 * Scale, Cursor + 6 * Scale, 14 * Scale, W - ButtonW - 20 * Scale,
                      InkSoft);
    }
}

void AGWHUD::TutorialPointer(AGWGameState* S, const FGWUnitView* Local)
{
    if (!Local || !Local->Alive || Settings || S->Finished)
        return;
    // The step's goal when the camera has it off-screen: the ring of the first step, the nearest standing
    // target, the nearest free triangle.
    const auto Step = static_cast<gwgame::TutorialStep>(S->TutorialStep);
    const gwgame::TutorialPage& Lesson = gwgame::tutorialPage();
    TOptional<FVector> Goal;
    double Best = TNumericLimits<double>::Max();
    auto Consider = [&](const FVector& At)
    {
        const double D = FVector::DistSquared2D(At, Local->Position);
        if (D < Best)
        {
            Best = D;
            Goal = At;
        }
    };
    if (Step == gwgame::TutorialStep::Move)
        Consider(FVector((Lesson.moveX + .5f) * S->CellSize, (Lesson.moveY + .5f) * S->CellSize, gwgame::DeckHeight));
    else if (Step == gwgame::TutorialStep::Ladder)
        Consider(FVector((Lesson.ladder.first + .5f) * S->CellSize, (Lesson.ladder.second + .5f) * S->CellSize, gwgame::DeckHeight));
    else if (Step == gwgame::TutorialStep::Tower)
        Consider(FVector((Lesson.tower.first + .5f) * S->CellSize, (Lesson.tower.second + .5f) * S->CellSize, 0));
    else if (Step == gwgame::TutorialStep::Shoot)
        for (const auto& U : S->Units)
            if (U.Alive && U.Dummy)
                Consider(U.Position);
    if (Goal)
        EdgePointer(S, *Goal, Step == gwgame::TutorialStep::Ladder ? 1 : 0, FText::GetEmpty(), Marigold, 1290, 345 * Scale);
}

void AGWHUD::TutorialCard(AGWGameState* S, const FGWUnitView* Local)
{
    const int32 Count = gwgame::TutorialStepCount;
    const int32 Step = FMath::Clamp(static_cast<int32>(S->TutorialStep), 0, Count);
    if (SeenStep >= 0 && Step > SeenStep)
    {
        StepBanner = Clock;
        Confetti(FVector2D(Width * .5f, Height * .24f + 28 * Scale), 24, 800, 6.5f);
    }
    SeenStep = Step;
    TutorialPointer(S, Local);
    // Skipping is always one click away (a friend's invite can also pull the player out at any time).
    Button(GWDefenseAction::TutorialSkip, GWText::Get(TEXT("Tutorial.Skip")), Width - 232 * Scale, 18 * Scale, 210 * Scale, 48 * Scale, true, false,
           Pencil);
    // 完成! flashes above the play area, clear of the player in the middle.
    const float Age = Clock - StepBanner;
    if (Age < 1.6f)
    {
        const float Out = FMath::Clamp((1.6f - Age) / .4f, 0.f, 1.f);
        const auto Pop = PopIn(FVector2D(Width * .5f, Height * .24f + 28 * Scale), Age);
        Tape(FVector2D(Width * .5f, Height * .24f + 28 * Scale), 360 * Scale * GWEase::OutBack(Age / .3f), 62 * Scale, -2, Teal.CopyWithNewOpacity(Out));
        Lettering(GWText::Get(TEXT("Tutorial.StepDone")), Width * .5f, Height * .24f, 40 * Scale, Ink.CopyWithNewOpacity(Out), 1301, Age - .06f);
    }
    if (Step >= Count)
        return;
    const float X = 22 * Scale, Y = 92 * Scale, W = 450 * Scale, TextW = W - 44 * Scale;
    const FText Body = GWText::Get(FString::Printf(TEXT("Tutorial.%d"), Step));
    // The ladder step adds the catch: what climbs down can be climbed up.
    const bool Retry = Step == static_cast<int32>(gwgame::TutorialStep::Ladder);
    const FText Extra = Retry ? GWText::Get(TEXT("Tutorial.LadderNote")) : FText::GetEmpty();
    const float BodyH = Paragraph(Body, 0, -1000, 18 * Scale, TextW, FLinearColor::Transparent);
    const float ExtraH = Retry ? Paragraph(Extra, 0, -1000, 14 * Scale, TextW, FLinearColor::Transparent) + 8 * Scale : 0;
    const bool Counted = S->TutorialNeeded > 1;
    const float H = 62 * Scale + BodyH + ExtraH + (Counted ? 34 * Scale : 0) + 36 * Scale;
    // Each new step's card slides in from the left edge; its title's marker stroke is drawn across.
    const float Shown = FMath::Min(Since(HashCombine(static_cast<uint32>(Step), 0x7E5Cu)), SceneAge(S));
    const auto Slide = SlideIn(FVector2D(X + W * .5f, Y + H * .5f), Shown, FVector2D(-260, 0), .15f);
    Card(X, Y, W, H, Hex(0xFFF7D6), Ink, 1300, 1.5f);
    Tape(FVector2D(X + W * .5f, Y + 3 * Scale), 110 * Scale, 22 * Scale, -2, Marigold);
    const FText Title = GWText::Get(FString::Printf(TEXT("Tutorial.%d.Title"), Step));
    Highlight(X + 18 * Scale, Y + 42 * Scale, (Measure(Title, 28 * Scale, EFace::Heavy).X + 12 * Scale) * GWEase::OutCubic((Shown - .3f) / .4f), 12 * Scale,
              Marigold.CopyWithNewOpacity(.5f), 1302);
    Label(Title, X + 22 * Scale, Y + 20 * Scale, 28 * Scale, Ink, false, TextW, EFace::Heavy);
    float Cursor = Y + 64 * Scale;
    Cursor += Paragraph(Body, X + 22 * Scale, Cursor, 18 * Scale, TextW, Ink);
    if (Retry)
        Cursor += Paragraph(Extra, X + 22 * Scale, Cursor + 8 * Scale, 14 * Scale, TextW, InkSoft) + 8 * Scale;
    if (Counted)
    {
        const float Done = FMath::Clamp(static_cast<float>(S->TutorialDone) / S->TutorialNeeded, 0.f, 1.f);
        const FText Progress = FText::FromString(FString::Printf(TEXT("%d / %d"), S->TutorialDone, S->TutorialNeeded));
        const float ProgressW = Measure(Progress, 16 * Scale, EFace::Bold).X;
        Meter(X + 22 * Scale, Cursor + 14 * Scale, TextW - ProgressW - 14 * Scale, 14 * Scale, Done, Teal, 1303, true);
        Label(Progress, X + 22 * Scale + TextW - ProgressW, Cursor + 8 * Scale, 16 * Scale, Ink, false, 0, EFace::Bold);
        Cursor += 34 * Scale;
    }
    // Seven dots: what is done, what is now, what comes.
    for (int32 I = 0; I < Count; ++I)
    {
        const FVector2D Dot(X + 30 * Scale + I * 30 * Scale, Cursor + 20 * Scale);
        if (I < Step)
            Glyph(16, Dot, 20 * Scale, Teal, 1310 + I);
        else
        {
            Disc(Dot, (I == Step ? 8 : 5) * Scale, I == Step ? Marigold : PaperDeep);
            Circle(Dot, (I == Step ? 8 : 5) * Scale, Ink, 1.2f * Scale, 1320 + I);
        }
    }
    (void)Local;
}

void AGWHUD::TutorialFinished(AGWGameState* S)
{
    const float Age = Since(HashCombine(S->Seed, 0x7F1Du));
    Box(0, 0, Width, Height, Paper.CopyWithNewOpacity(.9f * GWEase::Clamp01(Age / .25f)));
    if (Age > .25f)
        Grained(0, 0, Width, Height);
    const float W = FMath::Min(Width * .62f, 860 * Scale), H = FMath::Min(Height * .7f, 560 * Scale);
    const float X = (Width - W) * .5f, Y = (Height - H) * .45f;
    const auto Drop = DropIn(FVector2D(Width * .5f, Height * .5f), Age, .05f, 90);
    Card(X, Y, W, H, Paper, Ink, 1350, 2.4f, 2.6f * Scale);
    Tape(FVector2D(X + 30 * Scale, Y + 18 * Scale), 90 * Scale, 26 * Scale, -38, Teal);
    Tape(FVector2D(X + W - 30 * Scale, Y + 18 * Scale), 90 * Scale, 26 * Scale, 38, Teal);
    Doodles(FBox2D(FVector2D(X + 20 * Scale, Y + 20 * Scale), FVector2D(X + W - 20 * Scale, Y + H * .3f)), 7, .45f);
    {
        const auto Slam = StampIn(FVector2D(X + W * .5f, Y + H * .1f + 31 * Scale), Age, .3f);
        Lettering(GWText::Get(TEXT("Tutorial.Finished")), X + W * .5f, Y + H * .1f, 62 * Scale, Teal, 1351);
    }
    if (Age > .5f && Once(HashCombine(S->Seed, 0x7F1Eu)))
        Confetti(FVector2D(X + W * .5f, Y + H * .1f + 31 * Scale), 46, 1100, 8);
    float Cursor = Y + H * .1f + 96 * Scale;
    // Every step learned, in two columns of ticks.
    const float ColW = (W - 120 * Scale) * .5f;
    for (int32 I = 0; I < gwgame::TutorialStepCount; ++I)
    {
        const float CX = X + 60 * Scale + (I % 2) * ColW, CY = Cursor + (I / 2) * 34 * Scale;
        Glyph(16, FVector2D(CX + 10 * Scale, CY + 12 * Scale), 20 * Scale, Teal, 1360 + I);
        Key(*FString::Printf(TEXT("Tutorial.%d.Title"), I), CX + 30 * Scale, CY, 18 * Scale, Ink, false, ColW - 40 * Scale, EFace::Bold);
    }
    Cursor += 4 * 34 * Scale + 10 * Scale;
    Paragraph(GWText::Get(TEXT("Tutorial.FinishedNote")), X + 60 * Scale, Cursor, 16 * Scale, W - 120 * Scale, InkSoft);
    Button(GWDefenseAction::TutorialExit, GWText::Get(TEXT("Tutorial.Enter")), X + W * .5f - 160 * Scale, Y + H - 92 * Scale, 320 * Scale, 62 * Scale,
           true, false, Coral);
    (void)S;
}

void AGWHUD::LeaveTutorial()
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    auto* C = Cast<AGWController>(PlayerOwner);
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (!GI || !C)
        return;
    // Finishing and skipping both count: the tutorial is offered once per save.
    GI->MarkTutorialDone();
    if (Settings)
        ToggleSettings();
    // Replayed from a hub, it returns there in place; from the title, the player's own hub opens.
    const auto* G = GetWorld()->GetAuthGameMode<AGWGameMode>();
    if (G && G->RunFromHub())
        C->ServerAction(S && S->Finished ? 8 : 21, 0);
    else
        GI->OpenHub();
}

const TCHAR* AGWHUD::LeaveLabel(AGWGameState* S) const
{
    if (!S || !S->InMatch)
        return TEXT("Menu.Leave");
    if (S->Tutorial)
        return TEXT("Tutorial.Skip");
    if (!LocalHost(S))
        return TEXT("Hub.LeaveRoom");
    return S->Hub() ? TEXT("Hub.ToTitle") : TEXT("Hub.Abandon");
}

void AGWHUD::LeaveNow(AGWGameState* S)
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    auto* C = Cast<AGWController>(PlayerOwner);
    if (!GI || !C)
        return;
    if (!S || !S->InMatch)
        GI->LeaveRoom(false);
    else if (S->Tutorial)
        LeaveTutorial();
    // A guest goes home to their own hub; the host gives up the mission, or closes the room from its hub.
    else if (!LocalHost(S))
        GI->LeaveRoom(true);
    else if (!S->Hub())
        C->ServerAction(21, 0);
    else
        GI->LeaveRoom(false);
}

void AGWHUD::EnteringCard()
{
    const float W = 560 * Scale, H = 110 * Scale, X = (Width - W) * .5f, Y = Height * .42f;
    Card(X, Y, W, H, Paper, Ink, 1380, 1.6f);
    Tape(FVector2D(X + W * .5f, Y + 3 * Scale), 100 * Scale, 22 * Scale, -2, Teal);
    for (int32 I = 0; I < 3; ++I)
        Disc(FVector2D(X + 54 * Scale + I * 22 * Scale, Y + H * .5f), (Boil % 3 == I ? 8 : 5) * Scale, Teal, 12);
    Key(TEXT("Network.Entering"), X + 130 * Scale, Y + H * .5f - 14 * Scale, 22 * Scale, Ink, false, W - 160 * Scale, EFace::Bold);
}
