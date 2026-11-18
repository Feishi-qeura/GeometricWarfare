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
#include "GWMotion.h"
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
    StartIntro();
}

bool AGWHUD::OverlayOpen() const
{
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    return IntroPlaying() || Settings || Panel != EPanel::None || KitSheet || ReviveSheet || !S || !S->InMatch || S->Finished || S->UpgradePaused;
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
    // Esc folds an open build ring away first.
    if (const auto* C = Cast<AGWController>(PlayerOwner); C && C->RingCell >= 0)
    {
        CloseRing();
        return true;
    }
    // Esc closes a station sheet before anything else (the expedition's revive sheet spends nothing when it is folded
    // away: design 5.2).
    if (!Settings && Panel != EPanel::None)
    {
        Panel = EPanel::None;
        return true;
    }
    if (!Settings && ReviveSheet)
    {
        CloseRevive();
        return true;
    }
    if (!Settings && KitSheet)
    {
        KitSheet = false;
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
    if (IntroPlaying())
        return true;
    // Nothing takes a click while the page wipe is on screen.
    if (FGWTransition::Get().Stage != FGWTransition::EStage::Idle && FGWTransition::Get().Enabled())
        return true;
    // An open build ring takes the clicks on it; one anywhere else folds it away and goes on (to a button or another
    // cell's ring, never to a shot).
    const auto* Ringer = Cast<AGWController>(PlayerOwner);
    const bool HadRing = Ringer && Ringer->RingCell >= 0;
    if (RingClick(X, Y))
        return true;
    for (int I = Hits.Num() - 1; I >= 0; --I)
        if (Hits[I].Bounds.IsInside({X, Y}))
        {
            // A pinch of paper confetti where the click landed (not on a sheet's backing or its drawing square).
            if (Hits[I].Action == GWDefenseAction::ReviveDraw)
            {
                ReviveClick(X, Y);
                return true;
            }
            if (Hits[I].Action != GWDefenseAction::Blocker)
                Confetti(FVector2D(X, Y), 7, 420, 5.5f);
            Activate(Hits[I].Action);
            return true;
        }
    // A click on the open floor of a build phase pops the build ring there (on someone it is a shot); with the
    // expedition's revive kit in hand it opens the revive sheet.
    if (RingFromClick() || ReviveFromClick())
        return true;
    return HadRing || OverlayOpen();
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
    ++HudFrame;
    FrameDelta = LastFrame > 0 ? FMath::Clamp(static_cast<float>(Clock - LastFrame), 0.f, .1f) : 0.f;
    LastFrame = Clock;
    Boil = FMath::FloorToInt(Clock * 6);
    Hits.Reset();
    ClearMotions();
    NoticeMinX = 0;
    if (IntroPlaying())
    {
        DrawIntro();
        return;
    }
    {
        const FGWTransition& Wipe = FGWTransition::Get();
        Covered = Wipe.Enabled() && !FGWCinematic::Get().Clean &&
                  (Wipe.Stage == FGWTransition::EStage::Closed ||
                   (Wipe.Stage == FGWTransition::EStage::Closing && Wipe.Age() > FGWTransition::CloseSeconds * .7));
    }
    // Appear timers of things no longer drawn are forgotten now and then.
    if (Boil % 30 == 0)
        for (auto It = SeenAt.CreateIterator(); It; ++It)
            if (Clock - It.Value().Last > 4)
                It.RemoveCurrent();
    if (EasedValues.Num() > 4000)
        EasedValues.Reset();
    if (Bumps.Num() > 1000)
        Bumps.Reset();
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
    ClearMotions();
    DrawBits();
    if (!FGWCinematic::Get().Clean)
        Transition(S);
    if (CustomCursor() && FGWTransition::Get().Stage == FGWTransition::EStage::Idle)
        DrawCursor(S);
}

bool AGWHUD::CustomCursor() const
{
    const auto* S = GetWorld() ? GetWorld()->GetGameState<AGWGameState>() : nullptr;
    return S && S->InMatch && !S->Finished && !S->UpgradePaused && !Settings && Panel == EPanel::None && !OverlayOpen() && !FGWCinematic::Get().Clean;
}

void AGWHUD::DrawCursor(AGWGameState* S)
{
    float MX = 0, MY = 0;
    if (!PlayerOwner || !PlayerOwner->GetMousePosition(MX, MY))
        return;
    const FVector2D At(MX, MY);
    if (S->Building())
    {
        // A small hand-drawn hammer, its head's face on the point: building is on.
        const float K = Scale, A = FMath::DegreesToRadians(-38.f + 6.f * FMath::Sin(Clock * 3));
        const FVector2D Along(FMath::Cos(A), FMath::Sin(A)), Across(-Along.Y, Along.X);
        const FVector2D Face = At + FVector2D(10, 4) * K;
        const FVector2D Grip = Face + Across * 34 * K;
        Pen(Face + Across * 4 * K, Grip, Ink, 7 * K, 4101, .4f);
        Pen(Face + Across * 4 * K, Grip, Kraft, 4 * K, 4102, .4f);
        TArray<FVector2D> Head = {Face - Along * 11 * K - Across * 5 * K, Face + Along * 13 * K - Across * 5 * K, Face + Along * 13 * K + Across * 5 * K,
                                  Face - Along * 11 * K + Across * 5 * K};
        Polygon(Head, Hex(0x5E6678), Ink, 1.8f * K, 4103);
        Disc(At, 2.2f * K, Coral);
        return;
    }
    // A reticle in ink with a coral dot: aiming.
    const float R = 13 * Scale;
    Circle(At, R, Ink, 1.8f * Scale, 4110);
    for (int32 I = 0; I < 4; ++I)
    {
        const float A = HALF_PI * I + .05f * FMath::Sin(Clock * 2 + I);
        const FVector2D D(FMath::Cos(A), FMath::Sin(A));
        Pen(At + D * (R - 4 * Scale), At + D * (R + 7 * Scale), Ink, 2 * Scale, 4111 + I, .3f);
    }
    Disc(At, 2.6f * Scale, Coral);
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
    // On an expedition the hotbar and its line take the bottom middle: notices sit above them.
    const float X = FMath::Max((Width - W) * .5f, NoticeMinX),
                Y = InMatch ? (S->Expedition() ? Height - 262 * Scale : Height - 132 * Scale) : Height - 84 * Scale;
    // A sticky note slid up from the bottom edge, slid away again as it expires.
    const float Age = static_cast<float>(Clock - NoticeTime), Leaving = Working ? 0.f : GWEase::InCubic((Age - 5.6f) / .4f);
    const auto Motion = SlideIn(FVector2D(X + W * .5f, Y), Age, FVector2D(0, 90));
    PushMotion(FVector2D(X, Y), FVector2D(0, Leaving * 100 * Scale), 0, 1, 1 - Leaving);
    const FMotionGuard Away(this);
    Card(X, Y, W, H, Hex(0xFFF2B0), Ink, 77, 1.3f);
    Tape(FVector2D(X + W * .5f, Y + 2 * Scale), 80 * Scale, 20 * Scale, -4, Marigold);
    Glyph(Working ? 10 : 6, FVector2D(X + 30 * Scale, Y + H * .5f), 22 * Scale, Marigold, 5);
    Label(GWText::Get(Current), X + W * .5f + 12 * Scale, Y + 14 * Scale, 18 * Scale, Ink, true, W - 86 * Scale, EFace::Bold);
}

void AGWHUD::Menu(AGWGameState* S)
{
    // The battle itself plays in the 3D world behind the HUD (AGWWorld's showcase); a clean capture shows only it and
    // its level-up cards.
    const bool Clean = FGWCinematic::Get().Clean;
    if (!Clean)
        MenuWash();
    ShowcaseCards();
    if (Clean)
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
    const float W = Measure(Title, Size, EFace::Heavy).X, Age = Since(TitleKey);
    // The marker stroke is drawn across first, then the letters drop onto it and two shapes pop up beside them.
    const float Stroke = GWEase::OutCubic(Age / .35f);
    Highlight(X - 8 * Scale, Y + Size * .74f, (W + 16 * Scale) * Stroke, 16 * Scale, Coral.CopyWithNewOpacity(.45f), 5);
    Lettering(Title, X + W * .5f, Y, Size, Ink, 9, Age);
    {
        const auto Pop = PopIn(FVector2D(X + W + 38 * Scale, Y + 24 * Scale), Age, .25f);
        Glyph(3, FVector2D(X + W + 38 * Scale, Y + 24 * Scale), 30 * Scale, Coral, 15);
    }
    {
        const auto Pop = PopIn(FVector2D(X + W + 70 * Scale, Y + 44 * Scale), Age, .33f);
        Glyph(0, FVector2D(X + W + 70 * Scale, Y + 44 * Scale), 20 * Scale, Cobalt, 16);
    }
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
    // The hover eases in: the line slides right, a marker stroke sweeps under it, the arrow swings in and the
    // glyph turns a little.
    const float Lift = Eased(HashCombine(static_cast<uint32>(Id), 0x3A1u), Hover ? 1.f : 0.f, 14);
    const float Shift = 12 * Scale * Lift, Size = 28 * Scale;
    if (Lift > .02f)
    {
        Highlight(X + 54 * Scale + Shift, Y + H * .22f, (W - 64 * Scale) * Lift, H * .58f, Accent.CopyWithNewOpacity(.32f), Id);
        const FVector2D Tip(X - 4 * Scale + Shift - (1 - Lift) * 14 * Scale, Y + H * .5f);
        Polygon({Tip, Tip + FVector2D(-14 * Scale, -9 * Scale), Tip + FVector2D(-14 * Scale, 9 * Scale)}, Accent.CopyWithNewOpacity(Lift), Ink.CopyWithNewOpacity(Lift),
                1.4f * Scale, Id + 7);
    }
    {
        const FVector2D Spot(X + 24 * Scale + Shift, Y + H * .5f);
        PushMotion(Spot, FVector2D::ZeroVector, Lift * 12 * FMath::Sin(static_cast<float>(Clock) * 3), 1 + .12f * Lift);
        const FMotionGuard Turned(this);
        Glyph(GlyphKind, Spot, 28 * Scale, FMath::Lerp(PaperDeep, Accent, Lift), Id * 3);
    }
    Label(Text, X + 60 * Scale + Shift, Y + (H - Size * 1.3f) * .5f, Size, FMath::Lerp(InkSoft, Ink, Lift), false, W - 76 * Scale, EFace::Heavy);
    Pen(FVector2D(X + 54 * Scale, Y + H - 2 * Scale), FVector2D(X + W, Y + H - 2 * Scale), Pencil.CopyWithNewOpacity(.35f), 1, Id * 5, .4f);
    AddHit(FBox2D(FVector2D(X - 20 * Scale, Y), FVector2D(X + W, Y + H)), Id);
}

AGWHUD::FTitleLayout AGWHUD::MainTitle(const FText& Title, float CanvasW, float CanvasH) const
{
    FTitleLayout Out;
    const float K = FMath::Max(.5f, FMath::Min(CanvasW / 1600, CanvasH / 900));
    Out.X = FMath::Max(48 * K, CanvasW * .06f);
    Out.Y = CanvasH * .09f;
    // The three shapes bob up to 112 units right of the first line; the name and its shapes keep the page's margin.
    const float Full = 86 * K, Shapes = 120 * K, Room = FMath::Max(CanvasW - 2 * Out.X - Shapes, 160 * K);
    const FString Name = Title.ToString();
    TArray<FString> Lines = {Name};
    float Widest = LetteringWidth(Name, Full);
    if (Widest > Room)
    {
        // A name too long for one line breaks at the space or middle dot that leaves its two lines most even; the
        // break mark itself goes (a hyphen stays at the end of the first line).
        float Best = TNumericLimits<float>::Max();
        for (int32 I = 1; I < Name.Len() - 1; ++I)
        {
            const TCHAR Mark = Name[I];
            const bool Hyphen = Mark == TEXT('-');
            if (!Hyphen && Mark != TEXT(' ') && Mark != TCHAR(0x30FB) && Mark != TCHAR(0x00B7) && Mark != TCHAR(0xFF65))
                continue;
            const FString First = Name.Left(Hyphen ? I + 1 : I).TrimEnd(), Second = Name.Mid(I + 1).TrimStart();
            if (First.IsEmpty() || Second.IsEmpty())
                continue;
            const float Wide = FMath::Max(LetteringWidth(First, Full), LetteringWidth(Second, Full));
            if (Wide < Best)
            {
                Best = Wide;
                Lines = {First, Second};
            }
        }
        if (Lines.Num() > 1)
            Widest = Best;
    }
    // Slate sets whole point sizes, so a shrunk name keeps a little slack.
    Out.Size = Widest > Room ? Full * Room / Widest * .98f : Full;
    for (const FString& Line : Lines)
    {
        Out.Lines.Add(FText::FromString(Line));
        Out.Widths.Add(LetteringWidth(Line, Out.Size));
    }
    const float LineH = Measure(Title, Out.Size, EFace::Heavy).Y;
    Out.Step = LineH * .94f;
    Out.Bottom = Out.Y + (Lines.Num() - 1) * Out.Step + LineH;
    Out.Right = Out.X + Out.Widths[0] + 112 * K;
    for (float Wide : Out.Widths)
        Out.Right = FMath::Max(Out.Right, Out.X + Wide);
    return Out;
}

void AGWHUD::MainPage()
{
    // A conventional title screen: the logo top-left, one vertical menu in the lower-left, the brawl behind.
    const FTitleLayout Title = MainTitle(GWText::Get(TEXT("Game.Title")), Width, Height);
    const float X = Title.X, TitleSize = Title.Size, TitleY = Title.Y, TitleW = Title.Widths[0];
    float Widest = 0;
    for (float Wide : Title.Widths)
        Widest = FMath::Max(Widest, Wide);
    // The logo: the marker stroke draws across, the letters drop onto it one by one, and the three shapes of the
    // game pop up beside it and keep bobbing on the page. A name set on two lines gets a stroke under each, and its
    // second line's letters drop after the first's.
    const float Age = Since(TEXT("Main.Logo"));
    {
        const float Doodled = GWEase::Clamp01((Age - .5f) / .6f);
        const float Keep = Fade;
        Fade *= Doodled;
        Doodles(FBox2D(FVector2D(X - 20 * Scale, TitleY - 30 * Scale),
                       FVector2D(FMath::Min(X + Widest + 160 * Scale, Width - 8 * Scale), TitleY + (Title.Lines.Num() - 1) * Title.Step + TitleSize * 1.9f)),
                3, .3f);
        Fade = Keep;
    }
    int32 Dropped = 0;
    for (int32 L = 0; L < Title.Lines.Num(); ++L)
    {
        const float LineY = TitleY + L * Title.Step, LineW = Title.Widths[L];
        Highlight(X - 10 * Scale, LineY + TitleSize * .74f, (LineW + 20 * Scale) * GWEase::OutCubic((Age - .15f * L) / .45f), TitleSize * 22 / 86,
                  Coral.CopyWithNewOpacity(.5f), 4 + 9 * L);
        Lettering(Title.Lines[L], X + LineW * .5f, LineY, TitleSize, Ink, 7 + 13 * L, Age - .1f - Dropped * .045f);
        Dropped += Title.Lines[L].ToString().Len();
    }
    // The shapes bob beside the first line.
    const struct
    {
        int32 Kind;
        FVector2D Offset;
        float Size;
        FLinearColor Color;
    } Shapes[] = {{3, FVector2D(48, 30), 42, Coral}, {0, FVector2D(96, 58), 30, Cobalt}, {1, FVector2D(66, 100), 22, Marigold}};
    for (int32 I = 0; I < 3; ++I)
    {
        const FVector2D Spot(X + TitleW + Shapes[I].Offset.X * Scale, TitleY + Shapes[I].Offset.Y * Scale + 4 * Scale * FMath::Sin(static_cast<float>(Clock) * 1.6f + I * 2));
        const auto Pop = PopIn(Spot, Age, .55f + I * .12f);
        PushMotion(Spot, FVector2D::ZeroVector, 8 * FMath::Sin(static_cast<float>(Clock) * (1.1f + I * .3f) + I));
        const FMotionGuard Bob(this);
        Glyph(Shapes[I].Kind, Spot, Shapes[I].Size * Scale, Shapes[I].Color, 11 + I);
    }
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
    // The list sits on an index card with a red margin, like the first page of the sketchbook; the card is pinned
    // down, the tape slapped on, then the lines slide in one after another.
    {
        const auto Pin = DropIn(FVector2D(X + ItemW * .5f, Top + StackH * .5f), Age, .2f, 60);
        Card(X - 30 * Scale, Top - 30 * Scale, ItemW + 60 * Scale, StackH + 58 * Scale, Paper.CopyWithNewOpacity(.94f), Ink, 140, 1.6f,
             2.2f * Scale);
        {
            const auto Slap = PopIn(FVector2D(X + ItemW * .5f, Top - 28 * Scale), Age, .5f);
            Tape(FVector2D(X + ItemW * .5f, Top - 28 * Scale), 120 * Scale, 26 * Scale, -3, Teal.CopyWithNewOpacity(.85f));
        }
        const float Margin = GWEase::OutCubic((Age - .4f) / .4f);
        Line(FVector2D(X + 46 * Scale, Top - 18 * Scale), FVector2D(X + 46 * Scale, Top - 18 * Scale + (StackH + 38 * Scale) * Margin),
             Coral.CopyWithNewOpacity(.45f), 1.5f);
        for (int32 I = 0; I < Count; ++I)
        {
            const float RowY = Top + I * (ItemH + Gap);
            const auto Slide = SlideIn(FVector2D(X, RowY), Age, FVector2D(-70, 0), .45f + I * .08f);
            MenuItem(Items[I].Id, Items[I].GlyphKind, GWText::Get(Items[I].Text), X, RowY, ItemW, ItemH, Items[I].Accent);
        }
    }
    NoticeMinX = X + ItemW + 60 * Scale;
    {
        const auto Corner = PopIn(FVector2D(Width - 120 * Scale, Height - 34 * Scale), Age, .9f);
        SteamLine();
    }
}

void AGWHUD::SteamLine()
{
    // Whether Start opens a room friends can join: the Steam account this copy runs as, or that it will play offline.
    // Development builds add the app id Steam was started with (480 is Valve's shared test app).
    auto* GI = GetGameInstance<UGWGameInstance>();
    if (!GI)
        return;
    if (Clock - SteamCheckedAt > 1)
    {
        SteamCheckedAt = Clock;
        const bool Up = GI->PlatformReady(EFU_OnlineProvider::Steam);
        SteamName = Up ? GI->PlatformAccount(EFU_OnlineProvider::Steam) : FString();
        SteamApp = Up ? GI->SteamAppId() : 0;
    }
    const bool Up = !SteamName.IsEmpty();
    FText Line = GWText::Get(TEXT("Menu.SteamOff"));
    if (Up)
    {
        FFormatNamedArguments Args;
        Args.Add(TEXT("Name"), FText::FromString(SteamName));
        Line = FText::Format(GWText::Get(TEXT("Menu.Steam")), Args);
    }
#if !UE_BUILD_SHIPPING
    if (Up && SteamApp > 0)
        Line = FText::FromString(FString::Printf(TEXT("%s  ·  AppID %d"), *Line.ToString(), SteamApp));
#endif
    const float Size = 15 * Scale, LineW = FMath::Min(Measure(Line, Size, EFace::Bold).X, Width * .4f);
    const float LX = Width - 26 * Scale - LineW, LY = Height - 42 * Scale;
    Highlight(LX - 34 * Scale, LY + 2 * Scale, LineW + 44 * Scale, 22 * Scale, (Up ? Teal : Pencil).CopyWithNewOpacity(.18f), 31);
    Glyph(Up ? 16 : 17, FVector2D(LX - 16 * Scale, LY + 12 * Scale), 16 * Scale, Up ? Teal : Pencil, 32);
    Label(Line, LX, LY, Size, Up ? InkSoft : Pencil, false, Width * .4f, EFace::Bold);
}

void AGWHUD::SavesPage()
{
    // A save is one player profile (design doc): stickers, stars and opened pages, three slots.
    auto* GI = GetGameInstance<UGWGameInstance>();
    if (!GI)
        return;
    PageHeader(TEXT("Menu.Saves"), nullptr);
    const float Left = Width * .06f, Span = Width * .88f, Gap = 26 * Scale;
    const int32 Slots = UGWProfileSave::SlotCount;
    const float CardW = (Span - (Slots - 1) * Gap) / Slots, Top = Height * .2f, CardH = Height * .6f;
    const float Age = Since(TEXT("Saves.Cards"));
    for (int32 Slot = 0; Slot < Slots && GI->Profiles.IsValidIndex(Slot); ++Slot)
    {
        const FGWProfileSummary& Profile = GI->Profiles[Slot];
        const bool Active = GI->ProfileSlot() == Slot;
        const float X = Left + Slot * (CardW + Gap), Inner = CardW - 48 * Scale;
        // The three cards are dealt onto the table one after another.
        const auto Deal = DropIn(FVector2D(X + CardW * .5f, Top + CardH * .5f), Age, .1f + Slot * .09f, 70);
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
    {
        const auto Slide = SlideIn(FVector2D(Left, Height - 86 * Scale), Age, FVector2D(0, 80), .35f);
        Button(HudAction::Back, GWText::Get(TEXT("Menu.Back")), Left, Height - 86 * Scale, 220 * Scale, 56 * Scale, !GI->Busy);
    }
    NoticeMinX = Left + 250 * Scale;
}

void AGWHUD::IntroPage()
{
    PageHeader(TEXT("Menu.Intro"), nullptr);
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
    // The notebook flips open over a dimmed page.
    ClearMotions();
    const float Age = Since(TEXT("Settings.Notebook"));
    Box(0, 0, Width, Height, Ink.CopyWithNewOpacity(.55f * GWEase::Clamp01(Age / .2f)));
    Hits.Reset();
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    // Only Survival runs are saved mid-run; a defense page keeps its progress in each profile.
    const bool InRun = S && S->InMatch && !S->Finished && !S->Defense();
    const float PW = FMath::Min(1000 * Scale, Width * .84f), PH = FMath::Min((InRun ? 844 : 784) * Scale, Height * .92f);
    const float X = (Width - PW) * .5f, Y = (Height - PH) * .5f;
    const auto Open = DropIn(FVector2D(Width * .5f, Height * .5f), Age, 0, 110);
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
        // The page wipe covers the title before either opens.
        TWeakObjectPtr<AGWController> Weak(C);
        TWeakObjectPtr<UGWGameInstance> WeakGI(GI);
        const bool Lesson = GI->TutorialPending();
        CoverThen(Lesson ? FGWTransition::TutorialPage : FGWTransition::HubPage,
                  [Weak, WeakGI, Lesson]
                  {
                      if (Lesson && Weak.IsValid())
                          Weak->ServerAction(20, 0);
                      else if (!Lesson && WeakGI.IsValid())
                          WeakGI->OpenHub();
                  });
    }
    else if (Id >= GWDefenseAction::Level && Id < GWDefenseAction::Level + Pages)
    {
        BoardLevel = Id - GWDefenseAction::Level;
        // Everyone in the room sees which page the host is looking at.
        if (S && S->Hub() && LocalHost(S))
            C->ServerAction(19, BoardLevel);
    }
    else if (Id == GWDefenseAction::Deploy)
    {
        // A page, the expedition, or the party playlist's roll (the host's own board mode).
        const int32 Target = BoardMode == 1 ? AGWGameMode::ExpeditionLevel : BoardMode == 2 ? AGWGameMode::PlaylistLevel : BoardLevel;
        C->ServerAction(17, Target | (BoardBots << 4));
    }
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
    {
        TWeakObjectPtr<AGWController> Weak(C);
        const int32 Arg = S->Level | (BoardBots << 4);
        CoverThen(S->Level, [Weak, Arg] { if (Weak.IsValid()) Weak->ServerAction(13, Arg); });
    }
    else if (Id == GWDefenseAction::Next && S)
    {
        BoardLevel = FMath::Min(S->Level + 1, Pages - 1);
        TWeakObjectPtr<AGWController> Weak(C);
        const int32 Arg = BoardLevel | (BoardBots << 4);
        CoverThen(BoardLevel, [Weak, Arg] { if (Weak.IsValid()) Weak->ServerAction(13, Arg); });
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
    else if (Id >= GWDefenseAction::FriendJoin && Id < GWDefenseAction::FriendJoin + 8)
    {
        // A friend's room from the door's list: Steam looks the room up, then the join runs like an accepted invite.
        Panel = EPanel::None;
        GI->JoinFriendRoom(Id - GWDefenseAction::FriendJoin);
    }
    else if (Id == GWDefenseAction::Reconnect)
    {
        Panel = EPanel::None;
        TWeakObjectPtr<UGWGameInstance> WeakGI(GI);
        CoverThen(FGWTransition::HubPage, [WeakGI] { if (WeakGI.IsValid()) WeakGI->OpenHub(); });
    }
    else if (Id == GWDefenseAction::Tutorial)
    {
        Panel = EPanel::None;
        TWeakObjectPtr<AGWController> Weak(C);
        CoverThen(FGWTransition::TutorialPage, [Weak] { if (Weak.IsValid()) Weak->ServerAction(20, 0); });
    }
    else if (Id == GWDefenseAction::TutorialSkip || Id == GWDefenseAction::TutorialExit)
    {
        TWeakObjectPtr<AGWHUD> Self(this);
        CoverThen(FGWTransition::HubPage, [Self] { if (Self.IsValid()) Self->LeaveTutorial(); });
    }
    else if (Id == GWDefenseAction::Abandon)
    {
        TWeakObjectPtr<AGWController> Weak(C);
        CoverThen(FGWTransition::HubPage, [Weak] { if (Weak.IsValid()) Weak->ServerAction(21, 0); });
    }
    else if (Id == GWDefenseAction::LeaveHome)
    {
        if (Settings)
            ToggleSettings();
        TWeakObjectPtr<UGWGameInstance> WeakGI(GI);
        CoverThen(FGWTransition::HubPage, [WeakGI] { if (WeakGI.IsValid()) WeakGI->LeaveRoom(true); });
    }
    // Phase 6: the board's chapter tabs; the first open page of that chapter becomes the host's pick.
    else if (Id >= GWDefenseAction::Chapter && Id < GWDefenseAction::Chapter + 3)
    {
        BoardChapter = Id - GWDefenseAction::Chapter;
        const auto& Levels = gwgame::defenseLevels();
        UGWProfileSave* Profile = GI->ActiveProfile();
        for (int32 I = 0; I < static_cast<int32>(Levels.size()); ++I)
            if (Levels[I].id[0] - '1' == BoardChapter && (!Profile || Profile->Unlocked(I)) && S && S->Hub() && LocalHost(S))
            {
                BoardLevel = I;
                C->ServerAction(19, BoardLevel);
                break;
            }
    }
    // Phase 4's stations: every purchase is this player's own profile; buying a weapon, pen or hat also puts it to use.
    else if (Id >= GWDefenseAction::ShopTab && Id < GWDefenseAction::ShopTab + 3)
        ShopPage = Id - GWDefenseAction::ShopTab;
    else if (Id >= GWDefenseAction::WallTab && Id < GWDefenseAction::WallTab + 2)
        WallPage = Id - GWDefenseAction::WallTab;
    else if (Id >= GWDefenseAction::BuyWeapon && Id < GWDefenseAction::BuyWeapon + gwgame::WeaponCount)
    {
        const int32 Weapon = Id - GWDefenseAction::BuyWeapon;
        if (!GI->EquipWeapon(Weapon))
            GI->BuyWeapon(Weapon);
    }
    else if (Id >= GWDefenseAction::BuyPen && Id < GWDefenseAction::BuyPen + gwgame::PenCount)
    {
        const int32 Pen = Id - GWDefenseAction::BuyPen;
        if (!GI->EquipPen(Pen))
            GI->BuyPen(Pen);
    }
    else if (Id >= GWDefenseAction::BuyItem && Id < GWDefenseAction::BuyItem + gwgame::ItemCount)
        GI->BuyItem(Id - GWDefenseAction::BuyItem);
    else if (Id >= GWDefenseAction::BringItem && Id < GWDefenseAction::BringItem + gwgame::ItemCount)
        GI->BringItem(Id - GWDefenseAction::BringItem);
    else if (Id >= GWDefenseAction::DropItem && Id < GWDefenseAction::DropItem + 2)
        GI->DropItem(Id - GWDefenseAction::DropItem);
    else if (Id >= GWDefenseAction::Research && Id < GWDefenseAction::Research + gwgame::AttributeCount)
        GI->Research(Id - GWDefenseAction::Research);
    else if (Id >= GWDefenseAction::BuyHat && Id < GWDefenseAction::BuyHat + gwgame::HatCount)
    {
        const int32 Hat = Id - GWDefenseAction::BuyHat;
        if (!GI->EquipHat(Hat))
            GI->BuyHat(Hat);
    }
    else if (Id >= GWDefenseAction::WearTitle && Id < GWDefenseAction::WearTitle + gwgame::TitleCount)
        GI->EquipTitle(Id - GWDefenseAction::WearTitle);
    else if (Id > GWDefenseAction::Tier && Id <= GWDefenseAction::Tier + gwgame::MaxTier)
        C->ServerAction(22, Id - GWDefenseAction::Tier);
    else if (Id >= GWDefenseAction::Mode && Id < GWDefenseAction::Mode + 3)
        BoardMode = Id - GWDefenseAction::Mode;
    // The expedition's sheets and hotbar.
    else if (Id >= GWDefenseAction::KitDrop && Id < GWDefenseAction::KitDrop + 8)
        C->DropFromKit(Id - GWDefenseAction::KitDrop);
    else if (Id == GWDefenseAction::KitClose)
        KitSheet = false;
    else if (Id >= GWDefenseAction::KitSlot && Id < GWDefenseAction::KitSlot + gwgame::KitSlots)
    {
        // The first click picks a slot, a second on another moves it there (a pair or a stack merges), on itself lets go.
        const int32 Slot = Id - GWDefenseAction::KitSlot;
        if (KitPick < 0)
            KitPick = Slot;
        else if (KitPick == Slot)
            KitPick = -1;
        else
        {
            C->ServerAction(28, KitPick | (Slot << 8));
            KitPick = -1;
        }
    }
    else if (Id == GWDefenseAction::KitDropPick && KitPick >= 0)
    {
        C->ServerAction(29, KitPick);
        KitPick = -1;
    }
    else if (Id == GWDefenseAction::KitSellPick && KitPick >= 0)
    {
        C->ServerAction(30, KitPick);
        KitPick = -1;
    }
    else if (Id >= GWDefenseAction::KitCoins && Id < GWDefenseAction::KitCoins + 2)
        C->DropFromKit(Id == GWDefenseAction::KitCoins ? 10 : 11);
    else if (Id >= GWDefenseAction::KitTabs && Id < GWDefenseAction::KitTabs + 2)
    {
        KitTab = Id - GWDefenseAction::KitTabs;
        KitPick = -1;
    }
    else if (Id >= GWDefenseAction::HotbarPick && Id < GWDefenseAction::HotbarPick + 5)
        C->HotbarKey(Id - GWDefenseAction::HotbarPick);
    else if (Id >= GWDefenseAction::ReviveWho && Id < GWDefenseAction::ReviveWho + 8 && S)
    {
        // The sheet lists the ghosts by unit id, as it drew them.
        TArray<int32> Ghosts;
        const auto* P = C->GetPlayerState<AGWPlayerState>();
        for (const auto& U : S->Units)
            if (U.Human && !U.Alive && U.Ghost && !U.Extracted && (!P || U.Id != P->UnitId))
                Ghosts.Add(U.Id);
        Ghosts.Sort();
        if (Ghosts.IsValidIndex(Id - GWDefenseAction::ReviveWho))
            ReviveTarget = Ghosts[Id - GWDefenseAction::ReviveWho];
    }
    else if (Id >= GWDefenseAction::RevivePen && Id < GWDefenseAction::RevivePen + gwgame::BiomeCount)
        RevivePenPick = Id - GWDefenseAction::RevivePen;
    else if (Id == GWDefenseAction::ReviveSubmit)
        SubmitRevive();
    else if (Id == GWDefenseAction::ReviveRedraw)
    {
        ReviveStroke.Reset();
        ReviveDrawing = false;
        ReviveMeasured = -1;
    }
    else if (Id == GWDefenseAction::ReviveCancel)
        CloseRevive();
    else if (Id == GWDefenseAction::Blocker)
    {
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
        // Leaving goes home (the hub) or to the title; the wipe names where.
        const bool ToTitle = !S || !S->InMatch || (S->Hub() && LocalHost(S));
        TWeakObjectPtr<AGWHUD> Self(this);
        TWeakObjectPtr<AGWGameState> WeakState(S);
        CoverThen(ToTitle ? FGWTransition::TitlePage : FGWTransition::HubPage,
                  [Self, WeakState]
                  {
                      if (Self.IsValid())
                          Self->LeaveNow(WeakState.Get());
                  });
    }
    else if (Id == HudAction::Lobby)
    {
        TWeakObjectPtr<AGWController> Weak(C);
        CoverThen(FGWTransition::HubPage, [Weak] { if (Weak.IsValid()) Weak->ServerAction(8, 0); });
    }
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
    StopIntro();
    Super::EndPlay(Reason);
}
