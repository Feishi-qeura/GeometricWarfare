#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Core/IntroTimeline.h"
#include "GWHUD.generated.h"
class AGWGameState;
class UTexture2D;
class UMediaPlayer;
class UMediaTexture;
class UMediaSoundComponent;
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
// Phase 6: the board's chapter tabs (+0..2).
constexpr int32 Chapter = 790;
// The door's list of Steam friends in a room: joining one (+0..7).
constexpr int32 FriendJoin = 920;
// The expedition (design v2.1): the kit sheet's drops (+0 twenty-five stickers, +1 all of them, +2..7 a look by category)
// and its close button, a hotbar slot clicked (+0..4), the revive sheet's ghost (+0..7), pen (+0..4), submit, redraw,
// cancel and its drawing square, and the blocker under an open sheet (a click there does nothing).
constexpr int32 KitDrop = 940, KitClose = 948, HotbarPick = 950, ReviveWho = 960, RevivePen = 970, ReviveSubmit = 975,
                ReviveRedraw = 976, ReviveCancel = 977, ReviveDraw = 978, Blocker = 979;
// The achievement wall's two pages (+0 the pages and the room, +1 the expedition).
constexpr int32 WallTab = 990;
// The bag sheet (2026-11-16): a kit slot clicked (+0..14: the hotbar's five, then the bag's), dropping or selling the
// slot picked, dropping coins for a teammate (+0 a hundred, +1 all), and the sheet's tabs (+0 the bag, +1 the looks).
constexpr int32 KitSlot = 1000, KitDropPick = 1020, KitSellPick = 1021, KitCoins = 1022, KitTabs = 1024;
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
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool IntroPlaying() const { return Intro.active; }
    void SkipIntro();
    bool Click(float X, float Y);
    void ToggleSettings();
    bool Back();
    void ChooseUpgrade(int32 Index);
    bool OverlayOpen() const;
    // Whether play is on screen with nothing over it: the HUD then draws the cursor itself.
    bool CustomCursor() const;
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
    // The board's chapter tab (0-2) as if clicked, for tests and screenshots.
    void ShowMode(int32 Mode)
    {
        BoardMode = FMath::Clamp(Mode, 0, 2);
    }
    void ShowChapter(int32 Chapter)
    {
        Activate(GWDefenseAction::Chapter + FMath::Clamp(Chapter, 0, 2));
    }
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
    // A button's action as if it had been clicked (tests and screenshots).
    void Press(int32 Action)
    {
        Activate(Action);
    }
    // The game's name on the title page for a CanvasW x CanvasH canvas: one line at full size when it fits beside its
    // three shapes, else broken at the space or middle dot that leaves the two lines most even (ジオメトリック /
    // ウォーフェア), and shrunk only when a line still does not fit. Public for the layout test.
    struct FTitleLayout
    {
        TArray<FText> Lines;
        TArray<float> Widths;
        float X = 0, Y = 0, Size = 0, Step = 0;
        // The right edge of the lettering with its shapes, and the bottom of its last line.
        float Right = 0, Bottom = 0;
    };
    FTitleLayout MainTitle(const FText& Title, float CanvasW, float CanvasH) const;

    enum class EFace : uint8
    {
        Body,
        Bold,
        Heavy
    };

    // The build ring (GWBuildRing.cpp): a click on the open floor of a defense page in its build phase (or F) pops a
    // ring of slots round that cell: the four builds on empty floor, upgrade and rub out on a tower, rub out on wire.
    // A slot is a wedge of paper with its icon and ink price; the middle says what the hovered slot does.
    enum class ERingSlot : uint8
    {
        Bolt,
        Mortar,
        Tesla,
        Wire,
        Upgrade,
        Remove
    };
    struct FRingSlot
    {
        ERingSlot Kind = ERingSlot::Bolt;
        // Where the wedge sits (degrees, 0 = right, 90 = down) and its ink: the price, or what rubbing out pays back.
        float Angle = -90, Price = 0;
        // Why it cannot be taken (0: it can): 1 no ink, 2 it would seal the road, 3 someone stands there, 4 a
        // ladder's foot, 7 the tower is grown full (Ring.Denied.N; 1-4 also Defense.Denied.N).
        int32 Denied = 0;
        // Upgrade and rub out on a tower: its kind (gwgame::TowerKind) and the sides the upgrade grows it to.
        int32 Tower = -1, Next = 0;
    };
    // The slots a ring on Cell offers the local player now (empty: no ring there).
    TArray<FRingSlot> RingSlots(AGWGameState* State, const FGWUnitView& Local, int32 Cell) const;
    bool RingAllowed(AGWGameState* State, const FGWUnitView* Local) const;
    // Opens the ring on Cell (false when that cell takes nothing), toggles it, or folds it away.
    bool OpenRing(int32 Cell);
    void ToggleRing(int32 Cell);
    void CloseRing();
    // Takes a slot of the open ring (what a click on it does).
    void TakeRingSlot(int32 Index);
    // Tests point at a slot without a mouse (-1: the mouse decides).
    int32 RingHoverOverride = -1;

    // The expedition's sheets (GWExpeditionHUD.cpp): Tab's kit sheet (drop stickers or a look for a teammate), and the
    // revive sheet a click with the revive kit in hand opens (pick the ghost and the pen, draw a closed shape, submit).
    // Both sit over play like the station sheets; Esc folds them away without spending anything.
    void ToggleKit();
    bool KitOpen() const
    {
        return KitSheet;
    }
    // E at the shop's buyer opens the bag sheet in its selling mode (false when no buyer is in reach); tests pick a
    // slot on the sheet and switch its tab as a click would.
    bool OpenTrade();
    bool TradeOpen() const
    {
        return KitSheet && KitTrade;
    }
    void PickKitSlot(int32 Index)
    {
        KitPick = Index;
    }
    void ShowKitTab(int32 Tab)
    {
        KitTab = FMath::Clamp(Tab, 0, 1);
    }
    // Kitless: opened by E at a fallen teammate's outline, which needs no paper & pen (it costs twice the ink then).
    bool OpenRevive(bool Kitless = false);
    void CloseRevive();
    bool ReviveOpen() const
    {
        return ReviveSheet;
    }
    // Tests and screenshots: a stroke for the open revive sheet in its own unit square (as if drawn with the mouse), the
    // ghost and pen it is for, and the sheet's measurement of it.
    void DrawReviveStroke(const TArray<FVector2D>& Points);
    void PickRevive(int32 Target, int32 Pen)
    {
        ReviveTarget = Target;
        RevivePenPick = Pen;
    }

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FGWIntroCheck;
#endif
    gwgame::IntroTimeline Intro;
    UPROPERTY(Transient) TObjectPtr<UMediaPlayer> IntroPlayer;
    UPROPERTY(Transient) TObjectPtr<UMediaTexture> IntroTexture;
    UPROPERTY(Transient) TObjectPtr<UMediaSoundComponent> IntroSound;
    void StartIntro();
    void DrawIntro();
    UFUNCTION() void StopIntro();
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
    EPage Page = EPage::Main;
    EPanel Panel = EPanel::None;
    int32 ConfirmDelete = -1;
    // The tutorial step the step card last showed (a change flashes the "done" banner).
    int32 SeenStep = -1;
    double StepBanner = -10;
    // Mission board choices, and what the defense HUD has already announced.
    int32 BoardLevel = 0, BoardBots = 0, SeenWave = 0, SeenCleared = 0;
    // The chapter tab the host picked on the board (-1: the chapter of the selected page).
    int32 BoardChapter = -1;
    // The board's mode tab: 0 line defense, 1 expedition, 2 party playlist.
    int32 BoardMode = 0;
    uint32 SeenDenied = 0, BannerSeed = 0;
    double WaveBanner = -10, ClearBanner = -10;
    // Pages that keep a column on the left push transient notices to the right of it.
    float NoticeMinX = 0;
    // The title page's Steam line, read once a second: the signed-in name (empty: Steam is not up) and the app id.
    double SteamCheckedAt = -10;
    FString SteamName;
    int32 SteamApp = 0;
    void SteamLine();
    bool Settings = false;
    int32 Boil = 0;
    double Clock = 0, NoticeTime = 0, LevelBanner = -10;
    int32 SeenLevel = 1;
    FString SeenNotice;
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
    // Hand lettering; with Age (seconds since it appeared) the letters drop onto the line one after another.
    void Lettering(const FText& Text, float X, float Y, float Size, FLinearColor Color, int32 Seed, float Age = -1);
    // How wide Lettering sets Text at Size (glyph by glyph, as it advances).
    float LetteringWidth(const FString& Text, float Size) const;

    // Motion (GWMotion.cpp). Everything drawn inside a motion follows its 2D transform and fade (lines, fills,
    // text, hit boxes and hover tests), so a card can drop in, slide, pop or be stamped down and still take its
    // clicks where it shows. Motions nest; the guard pops its motion at the end of the scope.
    struct FAffine
    {
        float A = 1, B = 0, C = 0, D = 1, X = 0, Y = 0;
        FVector2D Apply(FVector2D P) const
        {
            return FVector2D(A * P.X + B * P.Y + X, C * P.X + D * P.Y + Y);
        }
    };
    struct FMotion
    {
        FAffine Matrix;
        float Fade = 1;
        bool Pushed = false;
    };
    struct FMotionGuard
    {
        AGWHUD* Hud;
        explicit FMotionGuard(AGWHUD* InHud) : Hud(InHud)
        {
        }
        FMotionGuard(const FMotionGuard&) = delete;
        FMotionGuard& operator=(const FMotionGuard&) = delete;
        ~FMotionGuard()
        {
            if (Hud)
                Hud->PopMotion();
        }
    };
    TArray<FMotion> Motions;
    float Fade = 1;
    void PushMotion(FVector2D Pivot, FVector2D Offset, float Turn = 0, float Grow = 1, float Alpha = 1);
    void PopMotion();
    void ClearMotions();
    // The mouse (or any screen point) in the coordinates of the motion being drawn.
    FVector2D FromScreen(FVector2D P) const;
    void AddHit(const FBox2D& Bounds, int32 Action);
    // Seconds since Id was first drawn in its current run of frames (not drawn for a moment: it starts over).
    struct FSeen
    {
        double First = 0, Last = -100;
        uint64 Frame = 0;
    };
    TMap<uint32, FSeen> SeenAt;
    // HUD frames drawn so far: "not drawn for a while" needs missed frames as well as missed time, so a hitch (a
    // shader compiling on a first effect) does not replay every card's entrance.
    uint64 HudFrame = 0;
    bool Absent(const FSeen& Entry) const
    {
        return Clock - Entry.Last > .25 && HudFrame - Entry.Frame > 3;
    }
    // While the page wipe covers the screen nothing has "appeared" yet (Since holds at zero until the reveal);
    // the wipe's own card uses SinceRaw.
    bool Covered = false;
    float Since(uint32 Id);
    float SinceRaw(uint32 Id);
    float Since(const TCHAR* Name)
    {
        return Since(FCrc::StrCrc32(Name));
    }
    // Card entrances, T seconds after the card appeared: pinned down from above, slid in, popped up, stamped.
    [[nodiscard]] FMotionGuard DropIn(FVector2D Pivot, float T, float Delay = 0, float Rise = 28);
    [[nodiscard]] FMotionGuard SlideIn(FVector2D Pivot, float T, FVector2D From, float Delay = 0);
    [[nodiscard]] FMotionGuard PopIn(FVector2D Pivot, float T, float Delay = 0);
    [[nodiscard]] FMotionGuard StampIn(FVector2D Pivot, float T, float Delay = 0);
    // A value easing toward Target (bars, counters); Ghost trails a drop so a hit reads as the chunk it took.
    TMap<uint32, FVector2D> EasedValues;
    float Eased(uint32 Id, float Target, float Rate = 10, float* Ghost = nullptr);
    // A short pulse (1 -> 1.3 -> 1) each time Value changes.
    TMap<uint32, FVector2D> Bumps;
    float Bump(uint32 Id, float Value);
    // Paper confetti on the HUD: little polygons that burst out, flutter, fall and fade.
    struct FBit
    {
        FVector2D Position = FVector2D::ZeroVector, Velocity = FVector2D::ZeroVector;
        float Angle = 0, Spin = 0, Size = 4, Age = 0, Life = 1;
        int32 Sides = 3;
        FLinearColor Color = FLinearColor::White;
    };
    TArray<FBit> Bits;
    void Confetti(FVector2D At, int32 Count, float Speed, float Size, float Direction = -1.5707963f, float Spread = 6.2831853f);
    // True the first time it is asked for Id (one burst of confetti per stamp, per star).
    TSet<uint32> Fired;
    bool Once(uint32 Id)
    {
        bool Already = false;
        Fired.Add(Id, &Already);
        if (Fired.Num() > 2000)
            Fired.Reset();
        return !Already;
    }
    // Seconds since the scene in play was revealed (the page wipe opening), for cards coming onto the page.
    float SceneAge(AGWGameState* State);
    // The fine print a station sheet shows beside the mouse (set while drawing the sheet, drawn on top of it).
    FText PanelTip;
    void DrawBits();
    // The page wipe between scenes and its title card (FGWTransition, GWMotion.h), drawn over everything.
    void Transition(AGWGameState* State);
    void TitleCard(AGWGameState* State, float Show);
    int32 SceneTitle(AGWGameState* State) const;
    // Covers the screen with the wipe, then does Action (which changes the scene; the new scene opens the wipe).
    void CoverThen(int32 Title, TFunction<void()> Action);
    TFunction<void()> PendingAction;
    uint32 SceneKey = MAX_uint32;
    double SceneArrived = 0;
    bool DeployCover = false;
    float FrameDelta = 0;
    double LastFrame = 0;
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
    // The cursor in play (a hammer while building, a reticle otherwise) and the dodge's cooldown over the local
    // player's head.
    void DrawCursor(AGWGameState* State);
    void DodgeBar(AGWGameState* State, const FGWUnitView& Local);
    void BuildRing(AGWGameState* State, const FGWUnitView* Local);
    // A click while the ring is open: true when the ring took it (a slot, its middle); a click outside folds the
    // ring away and goes on to whatever else is under it.
    bool RingClick(float X, float Y);
    // A click on the floor in the build phase opens a ring there (unless it lands on someone: that is a shot).
    bool RingFromClick();
    void RingIcon(const FRingSlot& Slot, FVector2D Center, float Size, FLinearColor Color, int32 Seed);
    // The build ring's state: when it opened, where it was last drawn (screen centre), the slots it showed, and its
    // fold-away after it closes (when, and the slot that was taken, -1 for none).
    double RingOpenedAt = -10, RingClosedAt = -10;
    FVector2D RingCenter = FVector2D::ZeroVector;
    TArray<FRingSlot> RingShown;
    int32 RingTaken = -1;
    // A refused slot shakes: which one and when.
    int32 RingShake = -1;
    double RingShakeAt = -10;
    void DrawRing(AGWGameState* State, float Open, float Fold);
    int32 RingSlotAt(FVector2D Point) const;
    // The party playlist's twist tag.
    void TwistTag(AGWGameState* State);
    // The expedition (GWExpeditionHUD.cpp, design v2.1 and its HUD row in chapter 10): the floor board (stage and floor,
    // the stage's pen, the room under way), the minimap of the floor's rooms, the five-slot hotbar, the pouch card (health,
    // shields, ink, the unsettled stickers), the teammates' kits, prompts over what E would take, buy, work or open, tags
    // on the floor's wares, machines, chests, doors and the portal, the room banners, the ghost's card, the kit and revive
    // sheets and the result card.
    void ExpeditionScreen(AGWGameState* State, const FGWUnitView* Local, float Entry);
    void ExpeditionBoard(AGWGameState* State, const FGWUnitView* Local);
    void Minimap(AGWGameState* State, const FGWUnitView* Local);
    void Hotbar(AGWGameState* State, const FGWUnitView& Local);
    void PouchCard(AGWGameState* State, const FGWUnitView& Local);
    // What the floor has left on the local player (bleeding, a red circle, the red seal, a streak...) as chips over the
    // hotbar, and an ink smear round the edges of the screen while it lasts.
    void Afflictions(AGWGameState* State, const FGWUnitView& Local);
    void ExpeditionTeam(AGWGameState* State, const FGWUnitView& Local);
    void ExpeditionPrompts(AGWGameState* State, const FGWUnitView* Local);
    void ExpeditionTags(AGWGameState* State, const FGWUnitView* Local);
    void ExpeditionBanners(AGWGameState* State, const FGWUnitView* Local);
    void GhostCard(AGWGameState* State, const FGWUnitView& Local);
    // Down and watching a teammate (2026-11-17): whose view it is and how to step to the others, its top at Y.
    void SpectateTag(AGWGameState* State, const FGWUnitView& Local, float Y);
    void DrawKitSheet(AGWGameState* State, const FGWUnitView* Local);
    void DrawReviveSheet(AGWGameState* State, const FGWUnitView* Local);
    void ExpeditionResults(AGWGameState* State);
    // Icons: an item (gwgame::Gear), a look (gwgame::lookId), a hotbar slot (kind, what, count), a stage's pen
    // (gwgame::PenBiome) and a room kind (gwgame::RoomKind).
    void GearIcon(int32 Gear, FVector2D Center, float Size, int32 Seed);
    // A gun (weapon kinds 0-5) as a chunky paper cut-out, nosing up and to the right (a quality gun's body in its
    // quality's colour).
    void GunIcon(int32 Weapon, FVector2D Center, float Size, int32 Seed, FLinearColor Accent = FLinearColor::Transparent);
    void LookIcon(int32 Look, FVector2D Center, float Size, FLinearColor Color, int32 Seed);
    void SlotIcon(int32 Kind, int32 What, int32 Count, FVector2D Center, float Size, int32 Seed, int32 Variant = -1);
    // A quality's gem (gwgame::Quality 1-5) and an expedition coin (a brass hexagon token).
    void QualityMark(FVector2D Center, float Size, int32 Quality, int32 Seed);
    void CoinIcon(FVector2D Center, float Size, int32 Seed);
    void BiomeIcon(int32 PenIndex, FVector2D Center, float Size, int32 Seed);
    void RoomIcon(int32 Kind, int32 Event, FVector2D Center, float Size, FLinearColor Color, int32 Seed);
    // What E does for the local player in the expedition right now (false: nothing in reach): the prompt, where it
    // floats, whether it would be refused (a full hotbar, a short pouch), and a hold's progress (Hold: E is held, 0-1).
    struct FReach
    {
        FText Text;
        FVector World = FVector::ZeroVector;
        bool Refused = false, Hold = false;
        float Progress = 0;
        // The shop's buyer: E opens the bag sheet to sell.
        bool Trader = false;
        // A fallen teammate's outline (their unit id): E opens the revive sheet on them.
        int32 Ghost = -1;
    };
    bool ExpeditionReach(AGWGameState* State, const FGWUnitView& Local, FReach& Out) const;
    // A click while the revive sheet is open (it takes every click), and one in play with the revive kit in hand.
    bool ReviveClick(float X, float Y);
    bool ReviveFromClick();
    // Follows the mouse while a revive stroke is being drawn (every frame the sheet is open).
    void TrackReviveStroke();
    void SubmitRevive();
    bool KitSheet = false, ReviveSheet = false;
    // The bag sheet: opened at the shop's buyer (selling), the slot picked (-1 none; a second click moves it there) and
    // the tab shown (0 the bag, 1 the looks).
    bool KitTrade = false;
    int32 KitPick = -1, KitTab = 0;
    // The revive sheet: the ghost (a unit id) and the pen picked (-1: the nearest ghost, the first pen there is), the stroke
    // in the sheet's unit square, whether the mouse is drawing it, where the drawing square was last drawn, and the stroke's
    // measurement (gwgame::measureDrawing, redone when the stroke changes: ReviveMeasured is the point count it was made at).
    int32 ReviveTarget = -1, RevivePenPick = -1;
    TArray<FVector2D> ReviveStroke;
    bool ReviveDrawing = false;
    FBox2D ReviveSquare = FBox2D(FVector2D::ZeroVector, FVector2D::ZeroVector);
    int32 ReviveMeasured = -1, ReviveBonus = 0;
    bool ReviveClosed = false;
    float ReviveShare = 0;
    // What the expedition's banners have already announced: the floor, every room's state and wave as last seen (a room
    // can arm and lock in the step the last player walks in), the wave called, the portal and the last boss's final door.
    uint32 ExpSeenFloor = 0;
    TArray<uint8> ExpRoomStates, ExpRoomWaves;
    int32 ExpSeenWave = 0;
    bool ExpSeenPortal = false, ExpSeenFinal = false;
    double FloorBanner = -10, DoorBanner = -10, RoomClearBanner = -10, PortalBanner = -10, WaveCall = -10;
    uint32 ExpSeenDenied = 0;
    // The floor's prank as last seen (EV04) and the last boss change announced, with their banners' start.
    int32 ExpSeenPrank = -1;
    uint32 ExpSeenPhase = 0;
    double PrankBanner = -10, PhaseBanner = -10;
    FString PhaseKey;
    void ModeCard(AGWGameState* State, float X, float Y, float W, float H);
    void PlayerCard(AGWGameState* State, const FGWUnitView& Unit);
    void SquadCard(AGWGameState* State, const FGWUnitView& Unit);
    void TeamCard(AGWGameState* State, const FGWUnitView& Unit);
    void WorldTags(AGWGameState* State, const FGWUnitView* Local);
    void Controls(float Entry);
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
    // The box: a tag beside the cursor saying what a click (or the right button) does on the cell under it: the build
    // ring on open floor or a tower, a ladder (or rubbing one out) on the platform's edge.
    void BoxCursor(AGWGameState* State, const FGWUnitView* Local);
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
    // The achievement wall's page (0 the first 32, 1 the expedition's 32).
    int32 WallPage = 0;
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

    // Combat feedback (GWCombatFeedback.cpp); the title page's paper wash over the brawl behind it (AGWWorld's
    // showcase) and the flat gun icons of the shop (GWMenuCanvas.cpp).
    void DrawCombatFeedback(AGWGameState* State);
    void MenuWash();
    // The title page battle's level-ups: skill cards over a player's head, one picked and broken into shards that fly
    // into it (AGWWorld::ShowcasePicks).
    void ShowcaseCards();
    void MiniCard(FVector2D Middle, float CardW, float CardH, int32 Skill, bool Lit, int32 Seed);
    void FlatLine(FVector2D A, FVector2D B, FLinearColor Color, float Thickness = 1);
    void FlatWeapon(uint8 Weapon, FVector2D Center, float Angle, float Size);
};
