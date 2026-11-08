#pragma once
#include "CoreMinimal.h"
#include "Core/FlatView.h"
#include "GameFramework/HUD.h"
#include "GWHUD.generated.h"
class AGWGameState;
class UTextureRenderTarget2D;
class UTexture2D;
struct FGWUnitView;

// Hit-box actions of the line-defense pages, shared by the HUD source files (the original menu actions
// are numbered in GWHUD.cpp; everything here stays below the text-field and room-list ranges).
namespace GWDefenseAction
{
constexpr int32 Start = 8, Level = 600, Deploy = 620, Bots = 630, Friends = 640, Profiles = 641, ProfileUse = 650,
                ProfileDelete = 660, ProfileConfirm = 670, ProfileCancel = 680, CallWave = 700, Retry = 710, Next = 711,
                RoomLevel = 720;
// Phase 2: the room's trolling level (+0..2), the player's own throw permission, kicking a lobby row (+0..7)
// and the readability switches of the settings notebook.
constexpr int32 Prank = 730, Throwable = 740, Kick = 750, Shake = 760, Flash = 761, Numbers = 762;
// Phase 3, the hub and the tutorial: closing a station panel, cancelling a deploy, the invite dialog, reopening
// the hub with Steam, replaying, skipping and leaving the tutorial, giving up a mission, and the board's modes
// (+0..2).
constexpr int32 PanelClose = 770, DeployCancel = 771, Invite = 772, Reconnect = 773, Tutorial = 774, TutorialSkip = 775,
                TutorialExit = 776, Abandon = 777, LeaveHome = 778, Mode = 780;
// Phase 4, meta progression: the shop's tabs (+0..2), buying or using a weapon (+0..5), a pen (+0..2), buying an item
// (+0..5), bringing one (+0..5), dropping an item slot (+0..1), raising an attribute (+0..5), buying or wearing a hat
// (+0..7), wearing a title (+0..10), and the board's difficulty tier (+1..5).
constexpr int32 ShopTab = 800, BuyWeapon = 810, BuyPen = 820, BuyItem = 830, BringItem = 840, DropItem = 850, Research = 860,
                BuyHat = 870, WearTitle = 880, Tier = 900;
}

// The whole interface is drawn on Canvas as a sketchbook: paper cards, washi tape, wobbly ink,
// marker highlights and geometric doodles. Line "boil" (6 fps re-seeding) keeps it hand animated.
UCLASS()
class GEOMETRICWARFARE_API AGWHUD : public AHUD
{
    GENERATED_BODY()
public:
    // Title-page sections. The settings notebook is an overlay that can open above any of them.
    enum class EPage : uint8
    {
        Main,
        Saves,
        Intro
    };
    // A hub station's sheet, opened with E in front of it (the order follows gwgame::HubStation, plus None).
    enum class EPanel : uint8
    {
        None,
        Board,
        Achievements,
        Shop,
        Research,
        Wardrobe,
        Door,
        Training
    };
    virtual void BeginPlay() override;
    virtual void DrawHUD() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool Click(float X, float Y);
    void ToggleSettings();
    bool Back();
    void ChooseUpgrade(int32 Index);
    bool OverlayOpen() const;
    bool SettingsOpen() const
    {
        return Settings;
    }
    void SetPage(EPage Next);
    EPage CurrentPage() const
    {
        return Page;
    }
    // E in the hub: opens the station the local player stands at (false when there is none in reach).
    bool Interact();
    void OpenPanel(EPanel Next);
    // The shop's tab (0 weapons, 1 pens, 2 items), for tests and screenshots.
    void SetShopPage(int32 Tab)
    {
        ShopPage = FMath::Clamp(Tab, 0, 2);
    }
    EPanel CurrentPanel() const
    {
        return Panel;
    }
    // The tutorial's way out (its skip and finish buttons): marks the save, then returns to the hub it was
    // replayed from or opens the player's own hub.
    void LeaveTutorial();

    enum class EFace : uint8
    {
        Body,
        Bold,
        Heavy
    };

private:
    struct Hit
    {
        FBox2D Bounds;
        int32 Action;
    };
    TArray<Hit> Hits;
    UPROPERTY() TObjectPtr<UFont> Font;
    UPROPERTY() TObjectPtr<UFont> BoldFont;
    UPROPERTY() TObjectPtr<UFont> HeavyFont;
    UPROPERTY() TObjectPtr<UTexture2D> Grain;
    UPROPERTY() TObjectPtr<UCanvas> MenuBackgroundCanvas;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> MenuBackgroundTarget;
    EPage Page = EPage::Main;
    EPanel Panel = EPanel::None;
    int32 ConfirmDelete = -1;
    // The tutorial step the step card last showed (a change flashes the "done" banner).
    int32 SeenStep = -1;
    double StepBanner = -10;
    // Mission board choices, and what the defense HUD has already announced.
    int32 BoardLevel = 0, BoardBots = 0, SeenWave = 0, SeenCleared = 0;
    uint32 SeenDenied = 0, BannerSeed = 0;
    double WaveBanner = -10, ClearBanner = -10;
    // Pages that keep a column on the left push transient notices to the right of it.
    float NoticeMinX = 0;
    bool Settings = false;
    int32 Boil = 0;
    double Clock = 0, NoticeTime = 0, LevelBanner = -10;
    int32 SeenLevel = 1;
    FString SeenNotice;
    gwgame::MenuScene BackgroundBattle;
    float Width = 0, Height = 0, Scale = 1;
    TMap<FString, TArray<FString>> WrappedText;
    TMap<uint32, double> DamageBirth;
    uint32 DamageSeed = 0;

    // Sketch toolkit (GWSketch.cpp).
    UFont* FaceFont(EFace Face) const;
    void Line(FVector2D A, FVector2D B, FLinearColor Color, float Thickness = 1);
    void Pen(FVector2D A, FVector2D B, FLinearColor Color, float Thickness, int32 Seed, float Wobble = 1);
    void Fill(const TArray<FVector2D>& Points, FLinearColor Color);
    void Card(float X, float Y, float W, float H, FLinearColor Paper, FLinearColor Stroke, int32 Seed, float Lift = 1,
              float StrokeWidth = 0);
    void Tape(FVector2D Center, float W, float H, float Degrees, FLinearColor Color);
    void Highlight(float X, float Y, float W, float H, FLinearColor Color, int32 Seed);
    void Hatch(float X, float Y, float W, float H, FLinearColor Color, float Spacing, float Thickness, int32 Seed);
    void Circle(FVector2D Center, float Radius, FLinearColor Color, float Thickness, int32 Seed, float Turns = 1.06f);
    void Disc(FVector2D Center, float Radius, FLinearColor Color, int32 Sides = 28);
    void Arc(FVector2D Center, float Radius, float From, float To, FLinearColor Color, float Thickness);
    void Polygon(const TArray<FVector2D>& Points, FLinearColor FillColor, FLinearColor Stroke, float Thickness, int32 Seed);
    void Meter(float X, float Y, float W, float H, float Value, FLinearColor Color, int32 Seed, bool Ticks = true);
    void Glyph(int32 Kind, FVector2D Center, float Size, FLinearColor Color, int32 Seed);
    void SkillIcon(int32 Skill, FVector2D Center, float Size, int32 Seed);
    void Keycap(const FText& Key, float X, float Y, float Size, FLinearColor Color);
    void Doodles(FBox2D Area, int32 Seed, float Alpha);
    void Grained(float X, float Y, float W, float H, float Strength = 1);
    FVector2D Measure(const FText& Text, float Size, EFace Face = EFace::Body) const;

    void Label(const FText& Text, float X, float Y, float Size, FLinearColor Color, bool Center = false, float MaxWidth = 0,
               EFace Face = EFace::Body);
    void Key(const TCHAR* Id, float X, float Y, float Size, FLinearColor Color, bool Center = false, float MaxWidth = 0,
             EFace Face = EFace::Body);
    void Lettering(const FText& Text, float X, float Y, float Size, FLinearColor Color, int32 Seed);
    void Box(float X, float Y, float W, float H, FLinearColor Color);
    void Button(int32 Id, const FText& Text, float X, float Y, float W, float H, bool Enabled = true, bool Selected = false,
                FLinearColor Accent = FLinearColor::Transparent);
    bool Hovered(float X, float Y, float W, float H) const;
    // Wraps and draws Text; returns the height it used.
    float Paragraph(const FText& Text, float X, float Y, float Size, float MaxWidth, FLinearColor Color);

    void Activate(int32 Id);
    void MenuItem(int32 Id, int32 GlyphKind, const FText& Text, float X, float Y, float W, float H, FLinearColor Accent);
    void PageHeader(const TCHAR* TitleKey, const TCHAR* SubtitleKey);
    void Menu(AGWGameState* State);
    void MainPage();
    void SavesPage();
    void IntroPage();
    // A room on its way in (the hub replicates a moment after the map): a card instead of a stale title page.
    void EnteringCard();
    void Combat(AGWGameState* State);
    void TopBoard(AGWGameState* State);
    void PlayerCard(AGWGameState* State, const FGWUnitView& Unit);
    void SquadCard(AGWGameState* State, const FGWUnitView& Unit);
    void TeamCard(AGWGameState* State, const FGWUnitView& Unit);
    void WorldTags(AGWGameState* State, const FGWUnitView* Local);
    void Controls();
    void DrawUpgradeChoices(AGWGameState* State, const FGWUnitView& Unit);
    void DrawResults(AGWGameState* State);
    void DrawSettings();

    // Line defense (GWDefenseHUD.cpp): the mission board's page cards, the wave board, hints, banners and the
    // result card. ViewOnly cards (a guest looking at the host's board) take no clicks and show every page open.
    void LevelCard(int32 Index, float X, float Y, float W, float H, bool ViewOnly = false);
    void StarRow(uint8 Stars, FVector2D Center, float Size, int32 Seed);
    void DefenseBoard(AGWGameState* State, const FGWUnitView* Local);
    void DefenseHints(AGWGameState* State, const FGWUnitView* Local);
    void DefenseBanners(AGWGameState* State);
    void DefenseResults(AGWGameState* State);
    // Up to four end-of-run titles (best bait, human cannonball, wall artist, scapegoat, MVP); returns whether
    // any were drawn.
    bool Awards(AGWGameState* State, float X, float Y, float W, float H);
    // Bait countdowns, the carried player's way out and the trolling level of the room (in-match).
    void PrankTags(AGWGameState* State, const FGWUnitView* Local);
    void InkMeter(const FGWUnitView& Unit, float X, float Y, float W);
    bool LocalHost(AGWGameState* State) const;

    // The hub (GWHubHUD.cpp): the room card, who is here, station tags and prompts, the deploy countdown and the
    // station panels. The tutorial: its step card, the skip button and the finished card.
    void HubScreen(AGWGameState* State, const FGWUnitView* Local);
    void RoomCard(AGWGameState* State);
    void Roster(AGWGameState* State, const FGWUnitView* Local);
    void StationTags(AGWGameState* State, const FGWUnitView* Local);
    void DeployBanner(AGWGameState* State);
    void HubPanel(AGWGameState* State);
    // A station sheet of at most Tall (unscaled) pixels; X/Y/W/H come back as the area under its title.
    void PanelFrame(const TCHAR* TitleKey, const TCHAR* SubtitleKey, FLinearColor Accent, float Tall, float& X, float& Y, float& W, float& H);
    void BoardPanel(AGWGameState* State, float X, float Y, float W, float H);
    void ComingPanel(AGWGameState* State, float X, float Y, float W, float H);
    void AchievementsPanel(AGWGameState* State, float X, float Y, float W, float H);
    // Meta progression (GWMetaHUD.cpp): the shop, research desk and wardrobe (each shows only this player's own
    // profile), the board's tier picker and its summary line, the sticker balance, achievement toasts, and icons for
    // items (0-5), hats (10-17), pens (20-22) and attributes (30-35).
    void ShopPanel(AGWGameState* State, float X, float Y, float W, float H);
    void ResearchPanel(AGWGameState* State, float X, float Y, float W, float H);
    void WardrobePanel(AGWGameState* State, float X, float Y, float W, float H);
    void TierPicker(AGWGameState* State, float X, float Y, float W, float H);
    void TierSummary(AGWGameState* State, float X, float Y, float W);
    void StickerChip(int32 Count, float Right, float Y);
    void MetaIcon(int32 Kind, FVector2D Center, float Size, FLinearColor Color, int32 Seed);
    void AchievementIcon(int32 Index, FVector2D Center, float Size, bool Earned, int32 Seed);
    // The items a player has in effect this mission, as a row of icons.
    void ItemRow(uint8 Items, float X, float Y, float Size);
    void Toasts();
    int32 ShopPage = 0;
    int32 ToastShowing = -1;
    double ToastStart = -10;
    void DoorPanel(AGWGameState* State, float X, float Y, float W, float H);
    void TrainingPanel(AGWGameState* State, float X, float Y, float W, float H);
    int32 NearStation(AGWGameState* State, const FGWUnitView* Local) const;
    // A tag on the edge of the play area pointing at a world spot the camera has off-screen (false if on screen).
    // TopEdge keeps it below whatever card fills the top-left corner (the roster, the tutorial step).
    bool EdgePointer(AGWGameState* State, FVector World, int32 GlyphKind, const FText& Caption, FLinearColor Color, int32 Seed, float TopEdge);
    void TutorialPointer(AGWGameState* State, const FGWUnitView* Local);
    void TutorialCard(AGWGameState* State, const FGWUnitView* Local);
    void TutorialFinished(AGWGameState* State);
    // The settings notebook's way out, which depends on where the player is and whether they host.
    const TCHAR* LeaveLabel(AGWGameState* State) const;
    void LeaveNow(AGWGameState* State);
    void Notice();
    bool WorldToScreen(AGWGameState* State, FVector World, FVector2D& Screen) const;
    FText TeammateName(AGWGameState* State, const FGWUnitView& Unit) const;

    // Combat feedback and the animated title-page background (GWCombatFeedback.cpp, GWMenuCanvas.cpp).
    void DrawCombatFeedback(AGWGameState* State);
    void DrawMenuBattle();
    void DrawPaperGround(const gwgame::FlatView& View);
    void FlatLine(FVector2D A, FVector2D B, FLinearColor Color, float Thickness = 1);
    void FlatBody(uint8 Shape, FVector2D Center, float Radius, float Angle, FLinearColor Color, bool Monster, int32 Seed);
    void FlatWeapon(uint8 Weapon, FVector2D Center, float Angle, float Size);
};
