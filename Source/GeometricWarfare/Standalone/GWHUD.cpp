#include "GWHUD.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "GWCinematic.h"
#include "GWController.h"
#include "GWGameInstance.h"
#include "GWGameMode.h"
#include "GWGameState.h"
#include "GWPalette.h"
#include "GWPlayerState.h"
#include "GWProfile.h"
#include "GWSaveGame.h"
#include "GWSettings.h"
#include "GWText.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"

using namespace GWPalette;

namespace
{
// Canvas hit boxes carry one of these actions. The in-match HUD (GWCombatHUD.cpp) uses the same numbers.
namespace HudAction
{
constexpr int32 Single = 1, SaveBook = 3, Notebook = 4, Guide = 5, Quit = 6, Back = 7;
constexpr int32 Upgrade = 100;
constexpr int32 Leave = 214, Lobby = 220;
constexpr int32 Language = 300, Adjust = 310, Resume = 400, SaveTo = 410;
constexpr int32 SlotContinue = 500, SlotDelete = 510, SlotConfirm = 520, SlotCancel = 530;
} // namespace HudAction
FText FormatClock(float Seconds)
{
    const int32 Whole = FMath::Max(0, FMath::FloorToInt(Seconds));
    return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Whole / 60, Whole % 60));
}
} // namespace

void AGWHUD::BeginPlay()
{
    Super::BeginPlay();
    // Roboto weights for Latin and umlauts, Droid as the CJK fallback for every weight.
    auto MakeFont = [&](const TCHAR* Latin)
    {
        UFont* Made = NewObject<UFont>(this);
        Made->FontCacheType = EFontCacheType::Runtime;
        const FString Fonts = FPaths::EngineContentDir() / TEXT("Slate/Fonts/");
        auto& Composite = Made->GetMutableInternalCompositeFont();
        Composite = FCompositeFont(FName(TEXT("Regular")), Fonts + TEXT("DroidSansFallback.ttf"), EFontHinting::Default,
                                   EFontLoadingPolicy::LazyLoad);
        Composite.DefaultTypeface.Fonts.Empty();
        Composite.DefaultTypeface.Fonts.Add(
            FTypefaceEntry(FName(TEXT("Regular")), Fonts + Latin, EFontHinting::Default, EFontLoadingPolicy::LazyLoad));
        Composite.FallbackTypeface.Typeface.Fonts.Add(FTypefaceEntry(
            FName(TEXT("Regular")), Fonts + TEXT("DroidSansFallback.ttf"), EFontHinting::Default, EFontLoadingPolicy::LazyLoad));
        return Made;
    };
    Font = MakeFont(TEXT("Roboto-Medium.ttf"));
    BoldFont = MakeFont(TEXT("Roboto-Bold.ttf"));
    HeavyFont = MakeFont(TEXT("Roboto-Black.ttf"));
    Grain = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Sketch/Textures/T_GW_PaperGrain.T_GW_PaperGrain"));
    GWText::Initialize();
}

bool AGWHUD::OverlayOpen() const
{
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    return Settings || Panel != EPanel::None || !S || !S->InMatch || S->Finished || S->UpgradePaused;
}

void AGWHUD::ToggleSettings()
{
    Settings = !Settings;
    if (Settings)
        if (auto* GI = GetGameInstance<UGWGameInstance>())
            GI->RefreshSaves();
    // A player alone owns the whole world (offline, or a room nobody else is in yet): the notebook pauses it.
    if (auto* G = GetWorld()->GetAuthGameMode<AGWGameMode>())
        G->LocalPause = Settings && (GetNetMode() == NM_Standalone || G->AloneInRoom());
}

bool AGWHUD::Back()
{
    // Esc closes a station sheet before anything else.
    if (!Settings && Panel != EPanel::None)
    {
        Panel = EPanel::None;
        return true;
    }
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (!Settings && S && !S->InMatch && !S->IsRoom && Page != EPage::Main)
    {
        SetPage(EPage::Main);
        return true;
    }
    ToggleSettings();
    return true;
}

void AGWHUD::ChooseUpgrade(int32 Index)
{
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    auto* C = Cast<AGWController>(PlayerOwner);
    auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    const auto* U = S && P ? S->FindUnit(P->UnitId) : nullptr;
    if (U && S->UpgradePaused && !Settings && U->Alive && U->PendingUpgrades > 0 && U->Choices.IsValidIndex(Index))
        C->ServerAction(7, Index);
}

bool AGWHUD::Click(float X, float Y)
{
    for (int I = Hits.Num() - 1; I >= 0; --I)
        if (Hits[I].Bounds.IsInside({X, Y}))
        {
            Activate(Hits[I].Action);
            return true;
        }
    return OverlayOpen();
}

void AGWHUD::SetPage(EPage Next)
{
    Page = Next;
    ConfirmDelete = -1;
    if (auto* GI = GetGameInstance<UGWGameInstance>(); GI && Page == EPage::Saves)
    {
        GI->RefreshSaves();
        GI->RefreshProfiles();
    }
}

void AGWHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas)
        return;
    Width = Canvas->SizeX;
    Height = Canvas->SizeY;
    Scale = FMath::Max(.5f, FMath::Min(Width / 1600, Height / 900));
    Clock = GetWorld()->GetRealTimeSeconds();
    Boil = FMath::FloorToInt(Clock * 6);
    Hits.Reset();
    NoticeMinX = 0;
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (!S)
        return;
    // A station sheet belongs to the hub: a deploy (or anything else that leaves it) closes the sheet.
    if (!S->Hub())
        Panel = EPanel::None;
    if (!S->Tutorial)
        SeenStep = -1;
    if (S->InMatch)
        Combat(S);
    else
    {
        if (Settings && GetNetMode() == NM_Standalone)
            if (auto* G = GetWorld()->GetAuthGameMode<AGWGameMode>())
                G->LocalPause = false;
        Menu(S);
    }
    if (Settings)
        DrawSettings();
    Notice();
}

void AGWHUD::Notice()
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    auto* C = Cast<AGWController>(PlayerOwner);
    const FString Current = C && !C->NoticeKey.IsEmpty() ? C->NoticeKey : GI ? GI->StatusKey : FString();
    if (Current != SeenNotice)
    {
        SeenNotice = Current;
        NoticeTime = Clock;
    }
    if (Current.IsEmpty())
        return;
    // Results expire after six seconds; "creating/searching/joining/signing in" stays while it is in progress.
    const bool Working = GI && (GI->Busy || GI->EpicSigningIn);
    if (!Working && Clock - NoticeTime > 6)
    {
        if (C && C->NoticeKey == Current)
            C->NoticeKey.Empty();
        else if (GI && GI->StatusKey == Current)
            GI->StatusKey.Empty();
        return;
    }
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    const bool InMatch = S && S->InMatch;
    const float W = FMath::Min(Width * .5f, 640 * Scale), H = 52 * Scale;
    const float X = FMath::Max((Width - W) * .5f, NoticeMinX), Y = InMatch ? Height - 132 * Scale : Height - 84 * Scale;
    Card(X, Y, W, H, Hex(0xFFF2B0), Ink, 77, 1.3f);
    Tape(FVector2D(X + W * .5f, Y + 2 * Scale), 80 * Scale, 20 * Scale, -4, Marigold);
    Glyph(Working ? 10 : 6, FVector2D(X + 30 * Scale, Y + H * .5f), 22 * Scale, Marigold, 5);
    Label(GWText::Get(Current), X + W * .5f + 12 * Scale, Y + 14 * Scale, 18 * Scale, Ink, true, W - 86 * Scale, EFace::Bold);
}

void AGWHUD::Menu(AGWGameState* S)
{
    DrawMenuBattle();
    if (FGWCinematic::Get().Clean)
        return;
    // A room shows its hub as soon as it arrives; until then (a guest's first moments) a card, not the title.
    if (S->IsRoom || GetNetMode() == NM_Client)
    {
        EnteringCard();
        return;
    }
    switch (Page)
    {
    case EPage::Saves:
        SavesPage();
        break;
    case EPage::Intro:
        IntroPage();
        break;
    default:
        MainPage();
        break;
    }
}

void AGWHUD::PageHeader(const TCHAR* TitleKey, const TCHAR* SubtitleKey)
{
    const float X = Width * .06f, Y = Height * .05f, Size = 54 * Scale;
    const FText Title = GWText::Get(TitleKey);
    const float W = Measure(Title, Size, EFace::Heavy).X;
    Highlight(X - 8 * Scale, Y + Size * .74f, W + 16 * Scale, 16 * Scale, Coral.CopyWithNewOpacity(.45f), 5);
    Lettering(Title, X + W * .5f, Y, Size, Ink, 9);
    Glyph(3, FVector2D(X + W + 38 * Scale, Y + 24 * Scale), 30 * Scale, Coral, 15);
    Glyph(0, FVector2D(X + W + 70 * Scale, Y + 44 * Scale), 20 * Scale, Cobalt, 16);
    if (SubtitleKey)
        Key(SubtitleKey, X + 2 * Scale, Y + Size * 1.4f, 19 * Scale, InkSoft, false, Width * .62f, EFace::Bold);
    // The game's name stays pinned in the corner of every sub page.
    const FText Logo = GWText::Get(TEXT("Game.Title"));
    const float LogoSize = 24 * Scale, LogoW = Measure(Logo, LogoSize, EFace::Heavy).X;
    Highlight(Width - X - LogoW - 6 * Scale, Y + 26 * Scale, LogoW + 12 * Scale, 10 * Scale, Marigold.CopyWithNewOpacity(.4f), 6);
    Label(Logo, Width - X - LogoW, Y + 8 * Scale, LogoSize, Ink, false, 0, EFace::Heavy);
}

void AGWHUD::MenuItem(int32 Id, int32 GlyphKind, const FText& Text, float X, float Y, float W, float H, FLinearColor Accent)
{
    const bool Hover = Hovered(X - 20 * Scale, Y, W + 20 * Scale, H);
    const float Shift = Hover ? 12 * Scale : 0, Size = 28 * Scale;
    if (Hover)
    {
        Highlight(X + 54 * Scale + Shift, Y + H * .22f, W - 64 * Scale, H * .58f, Accent.CopyWithNewOpacity(.32f), Id);
        const FVector2D Tip(X - 4 * Scale + Shift, Y + H * .5f);
        Polygon({Tip, Tip + FVector2D(-14 * Scale, -9 * Scale), Tip + FVector2D(-14 * Scale, 9 * Scale)}, Accent, Ink, 1.4f * Scale, Id + 7);
    }
    Glyph(GlyphKind, FVector2D(X + 24 * Scale + Shift, Y + H * .5f), 28 * Scale, Hover ? Accent : PaperDeep, Id * 3);
    Label(Text, X + 60 * Scale + Shift, Y + (H - Size * 1.3f) * .5f, Size, Hover ? Ink : InkSoft, false, W - 76 * Scale, EFace::Heavy);
    Pen(FVector2D(X + 54 * Scale, Y + H - 2 * Scale), FVector2D(X + W, Y + H - 2 * Scale), Pencil.CopyWithNewOpacity(.35f), 1, Id * 5, .4f);
    Hits.Add({FBox2D(FVector2D(X - 20 * Scale, Y), FVector2D(X + W, Y + H)), Id});
}

void AGWHUD::MainPage()
{
    // A conventional title screen: the logo top-left, one vertical menu in the lower-left, the brawl behind.
    const float X = FMath::Max(48 * Scale, Width * .06f);
    const FText Title = GWText::Get(TEXT("Game.Title"));
    const float TitleSize = 86 * Scale, TitleY = Height * .09f;
    const float TitleW = Measure(Title, TitleSize, EFace::Heavy).X;
    Doodles(FBox2D(FVector2D(X - 20 * Scale, TitleY - 30 * Scale), FVector2D(X + TitleW + 160 * Scale, TitleY + TitleSize * 1.9f)), 3, .3f);
    Highlight(X - 10 * Scale, TitleY + TitleSize * .74f, TitleW + 20 * Scale, 22 * Scale, Coral.CopyWithNewOpacity(.5f), 4);
    Lettering(Title, X + TitleW * .5f, TitleY, TitleSize, Ink, 7);
    Glyph(3, FVector2D(X + TitleW + 48 * Scale, TitleY + 30 * Scale), 42 * Scale, Coral, 11);
    Glyph(0, FVector2D(X + TitleW + 96 * Scale, TitleY + 58 * Scale), 30 * Scale, Cobalt, 12);
    Glyph(1, FVector2D(X + TitleW + 66 * Scale, TitleY + 100 * Scale), 22 * Scale, Marigold, 13);
    Key(TEXT("Game.Subtitle"), X + 4 * Scale, TitleY + TitleSize * 1.42f, 24 * Scale, InkSoft, false, Width * .5f, EFace::Bold);
    struct FItem
    {
        int32 Id, GlyphKind;
        const TCHAR* Text;
        FLinearColor Accent;
    };
    // One road for solo and co-op (design doc): start, saves, settings, quit.
    const FItem Items[] = {{GWDefenseAction::Start, 19, TEXT("Menu.Start"), Coral},
                           {HudAction::SaveBook, 20, TEXT("Menu.Saves"), Teal},
                           {HudAction::Notebook, 10, TEXT("Menu.Settings"), Marigold},
                           {HudAction::Quit, 17, TEXT("Menu.Quit"), Danger}};
    const int32 Count = static_cast<int32>(UE_ARRAY_COUNT(Items));
    const float ItemW = FMath::Min(390 * Scale, Width * .34f), ItemH = 56 * Scale, Gap = 10 * Scale;
    const float StackH = Count * ItemH + (Count - 1) * Gap, Top = Height - 72 * Scale - StackH;
    // The list sits on an index card with a red margin, like the first page of the sketchbook.
    Card(X - 30 * Scale, Top - 30 * Scale, ItemW + 60 * Scale, StackH + 58 * Scale, Paper.CopyWithNewOpacity(.94f), Ink, 140, 1.6f,
         2.2f * Scale);
    Tape(FVector2D(X + ItemW * .5f, Top - 28 * Scale), 120 * Scale, 26 * Scale, -3, Teal.CopyWithNewOpacity(.85f));
    Line(FVector2D(X + 46 * Scale, Top - 18 * Scale), FVector2D(X + 46 * Scale, Top + StackH + 20 * Scale), Coral.CopyWithNewOpacity(.45f),
         1.5f);
    for (int32 I = 0; I < Count; ++I)
        MenuItem(Items[I].Id, Items[I].GlyphKind, GWText::Get(Items[I].Text), X, Top + I * (ItemH + Gap), ItemW, ItemH, Items[I].Accent);
    NoticeMinX = X + ItemW + 60 * Scale;
    const FText Footer = GWText::Get(TEXT("Menu.Footer"));
    const float FooterW = Measure(Footer, 15 * Scale).X;
    Label(Footer, Width - FooterW - 26 * Scale, Height - 38 * Scale, 15 * Scale, InkSoft);
}

void AGWHUD::SavesPage()
{
    // A save is one player profile (design doc): stickers, stars and opened pages, three slots.
    auto* GI = GetGameInstance<UGWGameInstance>();
    if (!GI)
        return;
    PageHeader(TEXT("Menu.Saves"), TEXT("Saves.Subtitle"));
    const float Left = Width * .06f, Span = Width * .88f, Gap = 26 * Scale;
    const int32 Slots = UGWProfileSave::SlotCount;
    const float CardW = (Span - (Slots - 1) * Gap) / Slots, Top = Height * .2f, CardH = Height * .52f;
    for (int32 Slot = 0; Slot < Slots && GI->Profiles.IsValidIndex(Slot); ++Slot)
    {
        const FGWProfileSummary& Profile = GI->Profiles[Slot];
        const bool Active = GI->ProfileSlot() == Slot;
        const float X = Left + Slot * (CardW + Gap), Inner = CardW - 48 * Scale;
        const FLinearColor Accent = !Profile.Used ? Pencil : Active ? Coral : Teal;
        Card(X, Top, CardW, CardH, Profile.Used ? Paper : PaperShade, Active ? Coral : Ink, 700 + Slot * 7, Profile.Used ? 1.5f : .8f,
             (Active ? 3.f : 1.8f) * Scale);
        Tape(FVector2D(X + CardW * .5f, Top + 4 * Scale), 110 * Scale, 24 * Scale, (Slot - 1) * 4.f, Accent.CopyWithNewOpacity(.85f));
        Label(GWText::Number(TEXT("Board.Profile"), TEXT("Slot"), Slot + 1), X + 24 * Scale, Top + 28 * Scale, 26 * Scale, Ink, false, Inner,
              EFace::Heavy);
        if (Active)
        {
            Highlight(X + CardW - 150 * Scale, Top + 30 * Scale, 120 * Scale, 26 * Scale, Coral.CopyWithNewOpacity(.3f), 745 + Slot);
            Key(TEXT("Profile.Active"), X + CardW - 90 * Scale, Top + 31 * Scale, 16 * Scale, Ink, true, 110 * Scale, EFace::Bold);
        }
        const float ButtonY = Top + CardH - 74 * Scale, HalfW = (Inner - 12 * Scale) * .5f;
        if (!Profile.Used)
        {
            Glyph(19, FVector2D(X + CardW * .5f, Top + CardH * .4f), 70 * Scale, PaperDeep, 740 + Slot);
            Key(TEXT("Profile.Empty"), X + CardW * .5f, Top + CardH * .56f, 22 * Scale, InkSoft, true, Inner, EFace::Bold);
            Button(GWDefenseAction::ProfileUse + Slot, GWText::Get(TEXT("Profile.New")), X + 24 * Scale, ButtonY, Inner, 52 * Scale, true,
                   false, Teal);
            continue;
        }
        const FVector2D Badge(X + 60 * Scale, Top + 150 * Scale);
        Glyph(5, Badge, 76 * Scale, Marigold, 760 + Slot);
        Label(FText::AsNumber(Profile.Stars), Badge.X, Badge.Y - 14 * Scale, 24 * Scale, Ink, true, 0, EFace::Heavy);
        Label(GWText::Number(TEXT("Profile.Stars"), TEXT("Count"), Profile.Stars), X + 112 * Scale, Top + 118 * Scale, 19 * Scale, Ink, false,
              Inner - 90 * Scale, EFace::Bold);
        Label(GWText::Number(TEXT("Profile.Cleared"), TEXT("Count"), Profile.LevelsCleared), X + 112 * Scale, Top + 148 * Scale, 17 * Scale,
              InkSoft, false, Inner - 90 * Scale);
        Label(GWText::Number(TEXT("Board.Stickers"), TEXT("Count"), Profile.Stickers), X + 24 * Scale, Top + 214 * Scale, 18 * Scale, Ink, false,
              Inner, EFace::Bold);
        Label(FText::FromString(Profile.PlayedAt.ToString(TEXT("%Y-%m-%d  %H:%M"))), X + 24 * Scale, Top + 246 * Scale, 16 * Scale, Pencil, false,
              Inner);
        if (ConfirmDelete == Slot)
        {
            Key(TEXT("Profile.ConfirmDelete"), X + CardW * .5f, ButtonY - 34 * Scale, 17 * Scale, Danger, true, Inner, EFace::Bold);
            Button(GWDefenseAction::ProfileConfirm + Slot, GWText::Get(TEXT("Saves.Delete")), X + 24 * Scale, ButtonY, HalfW, 52 * Scale, true,
                   true, Danger);
            Button(GWDefenseAction::ProfileCancel + Slot, GWText::Get(TEXT("Saves.Cancel")), X + 36 * Scale + HalfW, ButtonY, HalfW, 52 * Scale);
        }
        else
        {
            Button(GWDefenseAction::ProfileUse + Slot, GWText::Get(TEXT("Profile.Use")), X + 24 * Scale, ButtonY, HalfW, 52 * Scale, !Active,
                   Active, Teal);
            Button(GWDefenseAction::ProfileDelete + Slot, GWText::Get(TEXT("Saves.Delete")), X + 36 * Scale + HalfW, ButtonY, HalfW, 52 * Scale);
        }
    }
    // Where progress lives, in one sentence the player can trust.
    const float NoteY = Top + CardH + 24 * Scale, NoteH = Height - 112 * Scale - NoteY;
    if (NoteH > 50 * Scale)
    {
        Card(Left, NoteY, Span, NoteH, Paper.CopyWithNewOpacity(.92f), Ink, 790, .8f);
        Glyph(14, FVector2D(Left + 36 * Scale, NoteY + NoteH * .5f), 34 * Scale, Teal, 791);
        Paragraph(GWText::Get(TEXT("Profile.Note")), Left + 70 * Scale, NoteY + 14 * Scale, 16 * Scale, Span - 96 * Scale, InkSoft);
    }
    Button(HudAction::Back, GWText::Get(TEXT("Menu.Back")), Left, Height - 86 * Scale, 220 * Scale, 56 * Scale, !GI->Busy);
    NoticeMinX = Left + 250 * Scale;
}

void AGWHUD::IntroPage()
{
    PageHeader(TEXT("Menu.Intro"), TEXT("Intro.Subtitle"));
    const float X = Width * .06f, Y = Height * .18f, W = Width * .88f, H = Height - 112 * Scale - Y;
    Card(X, Y, W, H, Paper, Ink, 820, 1.6f, 2.2f * Scale);
    for (float LineY = Y + 70 * Scale; LineY < Y + H - 16 * Scale; LineY += 34 * Scale)
        Line(FVector2D(X + 20 * Scale, LineY), FVector2D(X + W - 20 * Scale, LineY), Grid.CopyWithNewOpacity(.35f), 1);
    Line(FVector2D(X + 64 * Scale, Y + 10 * Scale), FVector2D(X + 64 * Scale, Y + H - 10 * Scale), Coral.CopyWithNewOpacity(.5f), 1.5f);
    struct FSection
    {
        int32 GlyphKind;
        FLinearColor Color;
        const TCHAR* Title;
        const TCHAR* Body;
    };
    const FSection Sections[] = {{20, Coral, TEXT("Intro.GoalTitle"), TEXT("Intro.Goal")},
                                 {4, Teal, TEXT("Intro.GrowthTitle"), TEXT("Intro.Growth")},
                                 {15, Marigold, TEXT("Intro.SquadTitle"), TEXT("Intro.Squad")},
                                 {7, Cobalt, TEXT("Intro.WeaponsTitle"), TEXT("Intro.Weapons")},
                                 {0, Coral, TEXT("Intro.TeamTitle"), TEXT("Intro.Team")},
                                 {14, Plum, TEXT("Intro.OnlineTitle"), TEXT("Intro.Online")}};
    const float ColumnW = (W - 130 * Scale) * .5f, Top = Y + 30 * Scale, Bottom = Y + H - 76 * Scale;
    float Cursor[2] = {Top, Top};
    for (int32 I = 0; I < static_cast<int32>(UE_ARRAY_COUNT(Sections)); ++I)
    {
        const int32 Column = I < 3 ? 0 : 1;
        const float SX = X + 86 * Scale + Column * (ColumnW + 40 * Scale);
        float& SY = Cursor[Column];
        if (SY > Bottom - 40 * Scale)
            continue;
        Glyph(Sections[I].GlyphKind, FVector2D(SX + 14 * Scale, SY + 14 * Scale), 26 * Scale, Sections[I].Color, 830 + I);
        Key(Sections[I].Title, SX + 38 * Scale, SY, 22 * Scale, Ink, false, ColumnW - 40 * Scale, EFace::Heavy);
        SY += 34 * Scale;
        SY += Paragraph(GWText::Get(Sections[I].Body), SX + 38 * Scale, SY, 17 * Scale, ColumnW - 40 * Scale, InkSoft) + 22 * Scale;
    }
    // The controls, as keycaps on the bottom ruled line.
    struct FControl
    {
        FText KeyText;
        const TCHAR* Text;
    };
    const FControl Items[] = {{FText::FromString(TEXT("WASD")), TEXT("Key.Move")}, {GWText::Get(TEXT("Key.Mouse")), TEXT("Key.Aim")},
                              {GWText::Get(TEXT("Key.LMB")), TEXT("Key.Fire")},    {FText::FromString(TEXT("E")), TEXT("Key.Recruit")},
                              {FText::FromString(TEXT("Q")), TEXT("Key.Throw")},   {FText::FromString(TEXT("1 2 3")), TEXT("Key.Pick")},
                              {FText::FromString(TEXT("Esc")), TEXT("Key.Menu")}};
    float KX = X + 86 * Scale;
    const float KY = Y + H - 56 * Scale, Size = 28 * Scale;
    for (const auto& Item : Items)
    {
        Keycap(Item.KeyText, KX, KY, Size, Ink);
        const float KeyW = FMath::Max(Size, Measure(Item.KeyText, Size * .55f, EFace::Bold).X + Size * .55f);
        Key(Item.Text, KX + KeyW + 8 * Scale, KY + 5 * Scale, 15 * Scale, InkSoft);
        KX += KeyW + 18 * Scale + Measure(GWText::Get(Item.Text), 15 * Scale).X + 22 * Scale;
    }
    Button(HudAction::Back, GWText::Get(TEXT("Menu.Back")), X, Height - 86 * Scale, 220 * Scale, 56 * Scale);
    NoticeMinX = X + 250 * Scale;
}

void AGWHUD::DrawSettings()
{
    Box(0, 0, Width, Height, Ink.CopyWithNewOpacity(.55f));
    Hits.Reset();
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    // Only Survival runs are saved mid-run; a defense page keeps its progress in each profile.
    const bool InRun = S && S->InMatch && !S->Finished && !S->Defense();
    const float PW = FMath::Min(1000 * Scale, Width * .84f), PH = FMath::Min((InRun ? 844 : 784) * Scale, Height * .92f);
    const float X = (Width - PW) * .5f, Y = (Height - PH) * .5f;
    Card(X, Y, PW, PH, Paper, Ink, 900, 2.5f, 2.6f * Scale);
    // Notebook page: ruled lines, a red margin and spiral binding.
    for (float LineY = Y + 92 * Scale; LineY < Y + PH - 20 * Scale; LineY += 36 * Scale)
        Line(FVector2D(X + 50 * Scale, LineY), FVector2D(X + PW - 18 * Scale, LineY), Grid.CopyWithNewOpacity(.45f), 1);
    Line(FVector2D(X + 82 * Scale, Y + 10 * Scale), FVector2D(X + 82 * Scale, Y + PH - 10 * Scale), Coral.CopyWithNewOpacity(.55f),
         1.6f);
    for (float RingY = Y + 34 * Scale; RingY < Y + PH - 20 * Scale; RingY += 46 * Scale)
    {
        Disc(FVector2D(X + 24 * Scale, RingY), 6 * Scale, Ink.CopyWithNewOpacity(.8f), 12);
        Arc(FVector2D(X + 14 * Scale, RingY), 12 * Scale, -PI * .5f, PI * .5f, InkSoft, 2.2f * Scale);
    }
    const float Left = X + 104 * Scale, Span = PW - 140 * Scale, H = 46 * Scale;
    Lettering(GWText::Get(TEXT("Menu.Settings")), X + PW * .5f, Y + 20 * Scale, 40 * Scale, Ink, 21);
    if (const auto* G = GetWorld()->GetAuthGameMode<AGWGameMode>(); G && G->LocalPause && S && S->InMatch)
    {
        Tape(FVector2D(X + PW - 140 * Scale, Y + 34 * Scale), 200 * Scale, 30 * Scale, 4, Marigold);
        Key(TEXT("Settings.Paused"), X + PW - 140 * Scale, Y + 24 * Scale, 15 * Scale, Ink, true, 180 * Scale, EFace::Bold);
    }
    Key(TEXT("Settings.Language"), Left, Y + 100 * Scale, 22 * Scale, Ink, false, Span * .3f, EFace::Bold);
    const TCHAR* Names[] = {TEXT("简体中文"), TEXT("English"), TEXT("日本語"), TEXT("Deutsch")};
    const TCHAR* Codes[] = {TEXT("zh-Hans"), TEXT("en"), TEXT("ja"), TEXT("de")};
    const float LangW = (Span - 3 * 12 * Scale) / 4;
    for (int I = 0; I < 4; ++I)
        Button(HudAction::Language + I, FText::FromString(Names[I]), Left + I * (LangW + 12 * Scale), Y + 138 * Scale, LangW, H, true,
               GWText::Culture() == Codes[I], Cobalt);
    auto* U = UGWUserSettings::Get();
    if (!U)
        return;
    const TCHAR* Labels[] = {TEXT("Settings.Music"), TEXT("Settings.Effects"), TEXT("Settings.Quality"), TEXT("Settings.Resolution"),
                             TEXT("Settings.Window")};
    for (int I = 0; I < 5; ++I)
    {
        const float RowY = Y + 222 * Scale + I * 64 * Scale;
        Key(Labels[I], Left, RowY + 10 * Scale, 21 * Scale, Ink, false, Span * .3f, EFace::Bold);
        const float ValueX = Left + Span * .44f, ValueW = Span * .38f;
        if (I < 2)
        {
            const float Value = I == 0 ? U->MusicVolume : U->EffectsVolume;
            Meter(ValueX, RowY + 14 * Scale, ValueW, 20 * Scale, Value, I == 0 ? Teal : Marigold, 400 + I);
            Label(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100))), ValueX + ValueW + 54 * Scale,
                  RowY + 12 * Scale, 18 * Scale, InkSoft, true, 0, EFace::Bold);
        }
        else
        {
            const FText Value =
                I == 2 ? GWText::Get(FString::Printf(TEXT("Settings.Quality.%d"), FMath::Clamp(U->GetOverallScalabilityLevel(), 0, 3)))
                : I == 3 ? FText::FromString(FString::Printf(TEXT("%d × %d"), U->GetScreenResolution().X, U->GetScreenResolution().Y))
                         : GWText::Get(U->GetFullscreenMode() == EWindowMode::Windowed ? TEXT("Settings.Windowed") : TEXT("Settings.Fullscreen"));
            Highlight(ValueX + ValueW * .12f, RowY + 8 * Scale, ValueW * .76f, 30 * Scale, Cobalt.CopyWithNewOpacity(.18f), 410 + I);
            Label(Value, ValueX + ValueW * .5f, RowY + 9 * Scale, 21 * Scale, Cobalt, true, ValueW, EFace::Bold);
        }
        Button(HudAction::Adjust + I * 2, FText::FromString(TEXT("−")), ValueX - 58 * Scale, RowY, 46 * Scale, H);
        Button(HudAction::Adjust + I * 2 + 1, FText::FromString(TEXT("+")), ValueX + ValueW + 10 * Scale + (I < 2 ? 76 * Scale : 0), RowY,
               46 * Scale, H);
    }
    // Readability switches for busy eight-player pages, and this player's own permission to be thrown.
    {
        const float RowY = Y + 222 * Scale + 5 * 64 * Scale;
        Key(TEXT("Settings.Comfort"), Left, RowY + 10 * Scale, 21 * Scale, Ink, false, Span * .3f, EFace::Bold);
        const float ToggleX = Left + Span * .32f, ToggleW = (Span * .68f - 3 * 10 * Scale) / 4;
        const struct
        {
            int32 Id;
            const TCHAR* Text;
            bool On;
        } Toggles[] = {{GWDefenseAction::Shake, TEXT("Settings.Shake"), U->ScreenShake},
                       {GWDefenseAction::Flash, TEXT("Settings.Flash"), U->HitFlash},
                       {GWDefenseAction::Numbers, TEXT("Settings.Numbers"), U->DamageNumbers},
                       {GWDefenseAction::Throwable, TEXT("Settings.Throwable"), U->Throwable}};
        for (int32 I = 0; I < 4; ++I)
            Button(Toggles[I].Id, GWText::Get(Toggles[I].Text), ToggleX + I * (ToggleW + 10 * Scale), RowY, ToggleW, H, true, Toggles[I].On,
                   I == 3 ? Coral : Teal);
    }
    if (InRun)
    {
        // Saving: only the authority owns the run, so co-op guests see why the slots are closed to them.
        const float RowY = Y + 222 * Scale + 6 * 64 * Scale + 6 * Scale;
        auto* P = PlayerOwner->GetPlayerState<AGWPlayerState>();
        const bool CanSave = P && P->Identity == S->HostIdentity;
        auto* GI = GetGameInstance<UGWGameInstance>();
        Key(TEXT("Save.Title"), Left, RowY + 10 * Scale, 21 * Scale, Ink, false, Span * .3f, EFace::Bold);
        const float SlotX = Left + Span * .32f, SlotW = (Span * .68f - 2 * 12 * Scale) / 3;
        if (CanSave)
            for (int32 Slot = 0; Slot < UGWRunSave::SlotCount; ++Slot)
            {
                FText Caption = GWText::Number(TEXT("Saves.Slot"), TEXT("Slot"), Slot + 1);
                if (GI && GI->Saves.IsValidIndex(Slot) && GI->Saves[Slot].Used)
                    Caption = FText::FromString(FString::Printf(TEXT("%s · Lv.%d"), *Caption.ToString(), GI->Saves[Slot].Level));
                Button(HudAction::SaveTo + Slot, Caption, SlotX + Slot * (SlotW + 12 * Scale), RowY, SlotW, H, true, false, Teal);
            }
        else
            Key(TEXT("Save.HostOnly"), SlotX, RowY + 12 * Scale, 17 * Scale, InkSoft, false, Span * .68f, EFace::Bold);
    }
    Button(HudAction::Resume, GWText::Get(TEXT("Menu.Continue")), Left, Y + PH - 86 * Scale, (Span - 20 * Scale) * .5f, 56 * Scale, true,
           false, Teal);
    // The way out depends on where the player is: a guest goes home, the host gives up the mission or closes the
    // room from its hub, the tutorial is skipped.
    Button(HudAction::Leave, GWText::Get(LeaveLabel(S)), Left + (Span + 20 * Scale) * .5f, Y + PH - 86 * Scale, (Span - 20 * Scale) * .5f,
           56 * Scale, true, false, Coral);
}

void AGWHUD::Activate(int32 Id)
{
    auto* C = Cast<AGWController>(PlayerOwner);
    auto* GI = GetGameInstance<UGWGameInstance>();
    if (!C || !GI)
        return;
    C->NoticeKey.Empty();
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    const int32 Pages = static_cast<int32>(gwgame::defenseLevels().size());
    if (Id == GWDefenseAction::Start)
    {
        // One road for solo and co-op (design doc): a new save learns first, everyone else goes to their own hub.
        if (GI->TutorialPending())
            C->ServerAction(20, 0);
        else
            GI->OpenHub();
    }
    else if (Id >= GWDefenseAction::Level && Id < GWDefenseAction::Level + Pages)
    {
        BoardLevel = Id - GWDefenseAction::Level;
        // Everyone in the room sees which page the host is looking at.
        if (S && S->Hub() && LocalHost(S))
            C->ServerAction(19, BoardLevel);
    }
    else if (Id == GWDefenseAction::Deploy)
        C->ServerAction(17, BoardLevel | (BoardBots << 4));
    else if (Id >= GWDefenseAction::Bots && Id < GWDefenseAction::Bots + 4)
        BoardBots = Id - GWDefenseAction::Bots;
    else if (Id == GWDefenseAction::Profiles)
        SetPage(EPage::Saves);
    else if (Id >= GWDefenseAction::ProfileUse && Id < GWDefenseAction::ProfileUse + UGWProfileSave::SlotCount)
    {
        GI->SelectProfile(Id - GWDefenseAction::ProfileUse);
        BoardLevel = 0;
    }
    else if (Id >= GWDefenseAction::ProfileDelete && Id < GWDefenseAction::ProfileDelete + UGWProfileSave::SlotCount)
        ConfirmDelete = Id - GWDefenseAction::ProfileDelete;
    else if (Id >= GWDefenseAction::ProfileConfirm && Id < GWDefenseAction::ProfileConfirm + UGWProfileSave::SlotCount)
    {
        GI->DeleteProfile(Id - GWDefenseAction::ProfileConfirm);
        ConfirmDelete = -1;
    }
    else if (Id >= GWDefenseAction::ProfileCancel && Id < GWDefenseAction::ProfileCancel + UGWProfileSave::SlotCount)
        ConfirmDelete = -1;
    else if (Id == GWDefenseAction::CallWave)
        C->ServerAction(12, 0);
    else if (Id == GWDefenseAction::Retry && S)
        C->ServerAction(13, S->Level | (BoardBots << 4));
    else if (Id == GWDefenseAction::Next && S)
    {
        BoardLevel = FMath::Min(S->Level + 1, Pages - 1);
        C->ServerAction(13, BoardLevel | (BoardBots << 4));
    }
    else if (Id >= GWDefenseAction::Prank && Id < GWDefenseAction::Prank + 3)
    {
        // The host's choice is remembered for the next room they open.
        if (auto* U = UGWUserSettings::Get())
        {
            U->PrankLevel = Id - GWDefenseAction::Prank;
            U->SaveSettings();
        }
        C->ServerAction(15, Id - GWDefenseAction::Prank);
    }
    else if (Id == GWDefenseAction::Throwable)
    {
        if (auto* U = UGWUserSettings::Get())
        {
            U->Throwable = !U->Throwable;
            U->SaveSettings();
            C->SendThrowable(U->Throwable);
        }
    }
    else if (Id >= GWDefenseAction::Kick && Id < GWDefenseAction::Kick + 8)
    {
        if (S && S->PlayerArray.IsValidIndex(Id - GWDefenseAction::Kick))
            C->ServerAction(16, S->PlayerArray[Id - GWDefenseAction::Kick]->GetPlayerId());
    }
    else if (Id == GWDefenseAction::Shake || Id == GWDefenseAction::Flash || Id == GWDefenseAction::Numbers)
    {
        if (auto* U = UGWUserSettings::Get())
        {
            bool& Switch = Id == GWDefenseAction::Shake ? U->ScreenShake : Id == GWDefenseAction::Flash ? U->HitFlash : U->DamageNumbers;
            Switch = !Switch;
            U->SaveSettings();
        }
    }
    else if (Id == GWDefenseAction::PanelClose)
        Panel = EPanel::None;
    else if (Id == GWDefenseAction::DeployCancel)
        C->ServerAction(18, 0);
    else if (Id == GWDefenseAction::Invite)
    {
        if (!GI->ShowInvite())
            C->NoticeKey = TEXT("Hub.InviteUnavailable");
    }
    else if (Id == GWDefenseAction::Reconnect)
    {
        Panel = EPanel::None;
        GI->OpenHub();
    }
    else if (Id == GWDefenseAction::Tutorial)
    {
        Panel = EPanel::None;
        C->ServerAction(20, 0);
    }
    else if (Id == GWDefenseAction::TutorialSkip || Id == GWDefenseAction::TutorialExit)
        LeaveTutorial();
    else if (Id == GWDefenseAction::Abandon)
        C->ServerAction(21, 0);
    else if (Id == GWDefenseAction::LeaveHome)
    {
        if (Settings)
            ToggleSettings();
        GI->LeaveRoom(true);
    }
    else if (Id >= GWDefenseAction::Mode && Id < GWDefenseAction::Mode + 3)
    {
        if (Id != GWDefenseAction::Mode)
            C->NoticeKey = TEXT("Board.ModeSoon");
    }
    else if (Id == HudAction::Single)
        C->ServerAction(4, 0);
    else if (Id == HudAction::SaveBook)
        SetPage(EPage::Saves);
    else if (Id == HudAction::Notebook)
    {
        if (!Settings)
            ToggleSettings();
    }
    else if (Id == HudAction::Guide)
        SetPage(EPage::Intro);
    else if (Id == HudAction::Quit)
        UKismetSystemLibrary::QuitGame(this, PlayerOwner, EQuitPreference::Quit, false);
    else if (Id == HudAction::Back)
        SetPage(EPage::Main);
    else if (Id >= HudAction::SlotContinue && Id < HudAction::SlotContinue + UGWRunSave::SlotCount)
    {
        const int32 Slot = Id - HudAction::SlotContinue;
        if (GI->Saves.IsValidIndex(Slot) && GI->Saves[Slot].Online)
            GI->HostSave(Slot);
        else
            C->ServerAction(10, Slot);
    }
    else if (Id >= HudAction::SlotDelete && Id < HudAction::SlotDelete + UGWRunSave::SlotCount)
        ConfirmDelete = Id - HudAction::SlotDelete;
    else if (Id >= HudAction::SlotConfirm && Id < HudAction::SlotConfirm + UGWRunSave::SlotCount)
    {
        GI->DeleteSave(Id - HudAction::SlotConfirm);
        ConfirmDelete = -1;
    }
    else if (Id >= HudAction::SlotCancel && Id < HudAction::SlotCancel + UGWRunSave::SlotCount)
        ConfirmDelete = -1;
    else if (Id == HudAction::Leave)
    {
        if (Settings)
            ToggleSettings();
        LeaveNow(S);
    }
    else if (Id == HudAction::Lobby)
        C->ServerAction(8, 0);
    else if (Id >= HudAction::Upgrade && Id < HudAction::Upgrade + 3)
        ChooseUpgrade(Id - HudAction::Upgrade);
    else if (Id >= HudAction::Language && Id <= HudAction::Language + 3)
    {
        const TCHAR* Codes[] = {TEXT("zh-Hans"), TEXT("en"), TEXT("ja"), TEXT("de")};
        GI->SetLanguage(Codes[Id - HudAction::Language]);
        WrappedText.Reset();
    }
    else if (Id == HudAction::Resume)
    {
        if (Settings)
            ToggleSettings();
    }
    else if (Id >= HudAction::SaveTo && Id < HudAction::SaveTo + UGWRunSave::SlotCount)
    {
        C->ServerAction(9, Id - HudAction::SaveTo);
        GI->RefreshSaves();
    }
    else if (Id >= HudAction::Adjust && Id <= HudAction::Adjust + 9)
    {
        auto* U = UGWUserSettings::Get();
        if (!U)
            return;
        int Row = (Id - HudAction::Adjust) / 2, Sign = Id % 2 ? 1 : -1;
        if (Row == 0)
            U->MusicVolume = FMath::Clamp(U->MusicVolume + Sign * .1f, 0.f, 1.f);
        if (Row == 1)
            U->EffectsVolume = FMath::Clamp(U->EffectsVolume + Sign * .1f, 0.f, 1.f);
        if (Row == 2)
            U->SetOverallScalabilityLevel(FMath::Clamp(U->GetOverallScalabilityLevel() + Sign, 0, 3));
        if (Row == 3)
        {
            const FIntPoint Resolutions[] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
            // Step from the nearest listed size so a custom resolution never jumps to the smallest one.
            int Index = 0;
            for (int I = 0; I < 4; ++I)
                if (U->GetScreenResolution().X >= Resolutions[I].X)
                    Index = I;
            U->SetScreenResolution(Resolutions[FMath::Clamp(Index + Sign, 0, 3)]);
        }
        if (Row == 4)
            U->SetFullscreenMode(U->GetFullscreenMode() == EWindowMode::Windowed ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
        U->ApplySettings(false);
        U->SaveSettings();
    }
}

void AGWHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    Super::EndPlay(Reason);
}
