#include "Engine/Canvas.h"
#include "GWController.h"
#include "GWGameInstance.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWPalette.h"
#include "GWPlayerState.h"
#include "GWProfile.h"
#include "GWSettings.h"
#include "GWText.h"

// Meta progression on the sketchbook HUD (phase 4): the shop, the research desk, the wardrobe and the achievement
// wall in the hub's stations, the board's difficulty tier, achievement toasts, and the hand-drawn icons they use
// (items, hats, pens, attributes). Every sheet shows only the local player's own profile (design doc: 每个人打开的
// 界面只显示自己的贴纸和解锁); the wall and the tier are the room's.
using namespace GWPalette;

namespace
{
// Icon kinds for MetaIcon: items 0-5, hats 10-17, pens 20-22, attributes 30-35.
constexpr int32 HatIcon = 10, PenIcon = 20, AttributeIcon = 30;
FLinearColor AttributeColor(int32 A)
{
    const FLinearColor Colors[] = {Coral, Fire, Cobalt, Teal, Marigold, Leaf};
    return Colors[FMath::Clamp(A, 0, 5)];
}
FLinearColor PenColor(int32 P)
{
    return P == 1 ? Hex(0x8A6A43) : P == 2 ? Hex(0xB5E655) : Kraft;
}
FLinearColor ItemColor(int32 I)
{
    const FLinearColor Colors[] = {Cobalt, Marigold, Kraft, Hex(0xF2C9A0), Coral, Plum};
    return Colors[FMath::Clamp(I, 0, 5)];
}
// Each achievement's sticker: a glyph (GWSketch.cpp Glyph kinds) and a marker colour.
struct FBadge
{
    int32 Glyph;
    FLinearColor Color;
};
FBadge Badge(int32 Index)
{
    static const FBadge Badges[gwgame::AchievementCount] = {
        {1, Kraft},  {2, Kraft},     {8, Coral},    {8, Cobalt}, {5, Marigold}, {5, Coral},    {11, Marigold}, {11, Coral},
        {18, Marigold}, {15, Teal},  {15, Coral},   {15, Cobalt}, {0, Coral},   {0, Plum},     {10, Coral},    {10, Marigold},
        {23, Cobalt}, {14, Teal},    {14, Cobalt},  {18, Teal},  {20, Kraft},   {6, Leaf},     {22, Plum},     {5, Leaf},
        {17, Danger}, {5, Fire},     {17, Pencil},  {4, Teal},   {4, Coral},    {3, Cobalt},   {21, Leaf},     {20, Teal}};
    return Badges[FMath::Clamp(Index, 0, gwgame::AchievementCount - 1)];
}
// The hat (or title) an achievement opens, for "Achievement: <name>" lines.
int32 AchievementFor(bool Hat, int32 Id)
{
    for (int32 I = 0; I < gwgame::AchievementCount; ++I)
    {
        const gwgame::AchievementInfo& Info = gwgame::achievementInfo(I);
        if ((Hat ? Info.hat : Info.title) == Id)
            return I;
    }
    return -1;
}
FText Multiplier(double Value)
{
    FNumberFormattingOptions Options;
    Options.MinimumFractionalDigits = 1;
    Options.MaximumFractionalDigits = 2;
    return FText::AsNumber(Value, &Options);
}
} // namespace

void AGWHUD::MetaIcon(int32 Kind, FVector2D C, float Size, FLinearColor Color, int32 Seed)
{
    const float R = Size * .5f, W = FMath::Max(1.4f, Size * .06f);
    auto P = [&](float X, float Y) { return C + FVector2D(X, Y) * R; };
    switch (Kind)
    {
    case 0:
        // Ink bottle: a squat bottle of cobalt ink with a paper label and a cork.
        Polygon({P(-.62f, -.15f), P(-.3f, -.42f), P(-.3f, -.72f), P(.3f, -.72f), P(.3f, -.42f), P(.62f, -.15f), P(.62f, .95f), P(-.62f, .95f)}, Color, Ink, W,
                Seed);
        Polygon({P(-.62f, .2f), P(.62f, .2f), P(.62f, .58f), P(-.62f, .58f)}, Paper, Ink, W * .8f, Seed + 1);
        Polygon({P(-.34f, -1.f), P(.34f, -1.f), P(.3f, -.72f), P(-.3f, -.72f)}, Kraft, Ink, W * .8f, Seed + 2);
        break;
    case 1:
        // Tape roll: a washi ring with its loose end.
        Polygon({P(.2f, .9f), P(1.f, .75f), P(1.f, 1.f), P(.2f, 1.f)}, Color.CopyWithNewOpacity(.85f), Ink, W * .7f, Seed + 3);
        Disc(C, R * .92f, Color);
        Circle(C, R * .92f, Ink, W, Seed);
        Disc(C, R * .42f, Paper);
        Circle(C, R * .42f, Ink, W * .8f, Seed + 1);
        break;
    case 2:
        // Triangle pouch: a kraft bag tied at the neck with a teal triangle peeking out.
        Polygon({P(-.35f, -.95f), P(.05f, -.55f), P(.4f, -.98f)}, Teal, Ink, W * .8f, Seed + 4);
        Polygon({P(-.45f, -.55f), P(.45f, -.55f), P(.85f, .3f), P(.6f, .95f), P(-.6f, .95f), P(-.85f, .3f)}, Color, Ink, W, Seed);
        Pen(P(-.5f, -.42f), P(.5f, -.42f), Ink, W * 1.3f, Seed + 1, .3f);
        break;
    case 3:
    {
        // Plaster: a tilted strip with a pale pad and breathing holes.
        const FVector2D A(FMath::Cos(-.6f), FMath::Sin(-.6f)), N(-A.Y, A.X);
        auto Q = [&](float U, float V) { return C + (A * U + N * V) * R; };
        Polygon({Q(-1.f, -.36f), Q(1.f, -.36f), Q(1.f, .36f), Q(-1.f, .36f)}, Color, Ink, W, Seed);
        Polygon({Q(-.36f, -.3f), Q(.36f, -.3f), Q(.36f, .3f), Q(-.36f, .3f)}, Paper, Ink, W * .7f, Seed + 1);
        for (float U : {-.7f, .7f})
            Disc(Q(U, 0), R * .06f, Ink, 6);
        break;
    }
    case 4:
        // Magnet: a coral horseshoe with paper tips.
        Arc(C + FVector2D(0, -R * .1f), R * .55f, PI, 2 * PI, Color, R * .42f);
        for (float Sign : {-1.f, 1.f})
        {
            Polygon({P(Sign * .76f, -.12f), P(Sign * .34f, -.12f), P(Sign * .34f, .55f), P(Sign * .76f, .55f)}, Color, Ink, W, Seed + 2);
            Polygon({P(Sign * .76f, .55f), P(Sign * .34f, .55f), P(Sign * .34f, .9f), P(Sign * .76f, .9f)}, Paper, Ink, W, Seed + 3);
        }
        Arc(C + FVector2D(0, -R * .1f), R * .76f, PI, 2 * PI, Ink, W);
        Arc(C + FVector2D(0, -R * .1f), R * .34f, PI, 2 * PI, Ink, W);
        break;
    case 5:
        // Carbon paper: a plum carbon sheet under a ruled white one.
        Polygon({P(-.55f, -.95f), P(.85f, -.75f), P(.65f, .95f), P(-.75f, .75f)}, Color, Ink, W, Seed);
        Polygon({P(-.85f, -.75f), P(.55f, -.85f), P(.65f, .85f), P(-.75f, .95f)}, Paper, Ink, W, Seed + 1);
        for (int32 L = 0; L < 3; ++L)
            Pen(P(-.55f, -.35f + L * .4f), P(.35f, -.42f + L * .4f), Pencil, W * .8f, Seed + 2 + L, .3f);
        break;
    case HatIcon:
        // Bare head: a dashed ring with a stroke through it.
        Circle(C, R * .7f, Pencil, W, Seed);
        Pen(P(-.55f, .55f), P(.55f, -.55f), Pencil, W, Seed + 1, .3f);
        break;
    case HatIcon + 1:
        // Paper hat: a folded newspaper triangle with print lines.
        Polygon({P(-1.f, .65f), P(0, -.95f), P(1.f, .65f)}, Hex(0xE9E2D0), Ink, W, Seed);
        Polygon({P(-1.f, .65f), P(1.f, .65f), P(1.f, .9f), P(-1.f, .9f)}, Hex(0xD8D0BC), Ink, W * .8f, Seed + 1);
        for (int32 L = 0; L < 3; ++L)
            Pen(P(-.35f + L * .1f, -.2f + L * .25f), P(.25f + L * .12f, -.2f + L * .25f), Pencil, W * .7f, Seed + 2 + L, .2f);
        break;
    case HatIcon + 2:
    {
        // Beanie: a red knitted dome, a rolled band and a pom-pom.
        TArray<FVector2D> Dome;
        for (int32 I = 0; I <= 10; ++I)
        {
            const float A = PI + PI * I / 10.f;
            Dome.Add(C + FVector2D(FMath::Cos(A) * .85f, FMath::Sin(A) * .8f + .25f) * R);
        }
        Polygon(Dome, Hex(0xD9534F), Ink, W, Seed);
        Polygon({P(-.92f, .2f), P(.92f, .2f), P(.92f, .55f), P(-.92f, .55f)}, Hex(0xB8403C), Ink, W, Seed + 1);
        Disc(P(0, -.62f), R * .24f, Paper);
        Circle(P(0, -.62f), R * .24f, Ink, W * .8f, Seed + 2);
        break;
    }
    case HatIcon + 3:
    {
        // Cap: a teal dome and its brim.
        TArray<FVector2D> Dome;
        for (int32 I = 0; I <= 10; ++I)
        {
            const float A = PI + PI * I / 10.f;
            Dome.Add(C + FVector2D(FMath::Cos(A) * .7f - .2f, FMath::Sin(A) * .7f + .3f) * R);
        }
        Polygon(Dome, Teal, Ink, W, Seed);
        Polygon({P(.4f, .22f), P(1.f, .3f), P(.95f, .45f), P(.4f, .42f)}, Teal * .8f, Ink, W, Seed + 1);
        Disc(P(-.2f, -.4f), R * .1f, Ink, 8);
        break;
    }
    case HatIcon + 4:
        // Top hat: brim, tall crown and a coral band.
        Polygon({P(-.5f, -.95f), P(.5f, -.95f), P(.5f, .55f), P(-.5f, .55f)}, Ink, Ink, W, Seed);
        Polygon({P(-.5f, .25f), P(.5f, .25f), P(.5f, .5f), P(-.5f, .5f)}, Coral, Ink, W * .7f, Seed + 1);
        Polygon({P(-.95f, .55f), P(.95f, .55f), P(.95f, .75f), P(-.95f, .75f)}, Ink, Ink, W, Seed + 2);
        break;
    case HatIcon + 5:
        // Antenna: a stalk on a base with a marigold ball.
        Polygon({P(-.5f, .55f), P(.5f, .55f), P(.4f, .85f), P(-.4f, .85f)}, InkSoft, Ink, W, Seed);
        Pen(P(0, .55f), P(0, -.45f), Ink, W * 1.6f, Seed + 1, .2f);
        Disc(P(0, -.62f), R * .3f, Marigold);
        Circle(P(0, -.62f), R * .3f, Ink, W, Seed + 2);
        break;
    case HatIcon + 6:
    {
        // Pencil: a yellow hexagonal body, a kraft cone and a graphite tip, drawn on a slant.
        const FVector2D A = FVector2D(.55f, -.83f), N(-A.Y, A.X);
        auto Q = [&](float U, float V) { return C + (A * U + N * V) * R; };
        Polygon({Q(-.95f, -.24f), Q(.35f, -.24f), Q(.35f, .24f), Q(-.95f, .24f)}, Hex(0xF2C230), Ink, W, Seed);
        Polygon({Q(-.95f, -.24f), Q(-.75f, -.24f), Q(-.75f, .24f), Q(-.95f, .24f)}, Coral, Ink, W * .7f, Seed + 1);
        Polygon({Q(.35f, -.24f), Q(.85f, 0), Q(.35f, .24f)}, Kraft, Ink, W, Seed + 2);
        Polygon({Q(.68f, -.08f), Q(.85f, 0), Q(.68f, .08f)}, Ink, Ink, W * .5f, Seed + 3);
        break;
    }
    case HatIcon + 7:
        // Crown.
        Glyph(18, C, Size, Marigold, Seed);
        Disc(P(0, .2f), R * .14f, Coral, 8);
        break;
    case PenIcon:
    case PenIcon + 1:
    case PenIcon + 2:
    {
        // A pen on a slant over a swatch of the wall it draws.
        const int32 Tool = Kind - PenIcon;
        Polygon({P(-.95f, .35f), P(-.15f, .35f), P(-.15f, .95f), P(-.95f, .95f)}, PenColor(Tool), Ink, W, Seed);
        if (Tool == 1)
            Hatch(C.X - .95f * R, C.Y + .35f * R, .8f * R, .6f * R, Ink.CopyWithNewOpacity(.35f), 5 * Scale, 1, Seed);
        const FVector2D A = FVector2D(.7f, -.7f), N(-A.Y, A.X);
        auto Q = [&](float U, float V) { return C + FVector2D(.15f, -.1f) * R + (A * U + N * V) * R; };
        const float Thick = Tool == 1 ? .3f : Tool == 2 ? .26f : .17f;
        const FLinearColor Body = Tool == 0 ? Ink : Tool == 1 ? Hex(0x3B3B44) : Hex(0xB5E655);
        Polygon({Q(-.9f, -Thick), Q(.45f, -Thick), Q(.45f, Thick), Q(-.9f, Thick)}, Body, Ink, W, Seed + 1);
        if (Tool == 0)
            Polygon({Q(.45f, -Thick), Q(.95f, 0), Q(.45f, Thick)}, Marigold, Ink, W, Seed + 2);
        else
            Polygon({Q(.45f, -Thick), Q(.85f, -Thick * .2f), Q(.85f, Thick * .6f), Q(.45f, Thick)}, PenColor(Tool), Ink, W, Seed + 2);
        Pen(Q(-.55f, -Thick), Q(-.55f, Thick), Paper, W, Seed + 3, .2f);
        break;
    }
    case AttributeIcon + 4:
        // Speed: two marigold chevrons.
        for (int32 I = 0; I < 2; ++I)
        {
            const float X0 = -.55f + I * .55f;
            Pen(P(X0, -.6f), P(X0 + .45f, 0), Color, W * 2.6f, Seed + I, .2f);
            Pen(P(X0 + .45f, 0), P(X0, .6f), Color, W * 2.6f, Seed + I + 4, .2f);
        }
        break;
    case AttributeIcon + 5:
        // Pickup range: a dot inside two leaf-green arcs.
        Disc(C, R * .26f, Color);
        Circle(C, R * .26f, Ink, W, Seed);
        Arc(C, R * .58f, -PI * .8f, PI * .3f, Color, W * 2.2f);
        Arc(C, R * .9f, -PI * .7f, PI * .2f, Color, W * 2.2f);
        break;
    default:
        if (Kind >= AttributeIcon && Kind < AttributeIcon + 4)
        {
            const int32 Glyphs[] = {14, 11, 23, 15};
            Glyph(Glyphs[Kind - AttributeIcon], C, Size, Color, Seed);
        }
        break;
    }
}

void AGWHUD::AchievementIcon(int32 Index, FVector2D C, float Size, bool Earned, int32 Seed)
{
    const FBadge B = Badge(Index);
    if (Earned)
        Glyph(B.Glyph, C, Size, B.Color, Seed);
    else
        Circle(C, Size * .42f, Pencil.CopyWithNewOpacity(.6f), FMath::Max(1.f, Size * .05f), Seed);
}

void AGWHUD::StickerChip(int32 Count, float Right, float Y)
{
    const FText Text = GWText::Number(TEXT("Board.Stickers"), TEXT("Count"), Count);
    const float Size = 18 * Scale, W = Measure(Text, Size, EFace::Heavy).X + 54 * Scale, H = 36 * Scale, X = Right - W;
    Card(X, Y, W, H, Hex(0xFFF2B0), Ink, 2100, 1.1f);
    Glyph(5, FVector2D(X + 20 * Scale, Y + H * .5f), 22 * Scale, Marigold, 2101);
    Label(Text, X + 38 * Scale, Y + (H - Size * 1.3f) * .5f, Size, Ink, false, 0, EFace::Heavy);
}

void AGWHUD::ItemRow(uint8 Items, float X, float Y, float Size)
{
    for (int32 I = 0, Shown = 0; I < gwgame::ItemCount; ++I)
        if ((Items >> I) & 1)
            MetaIcon(I, FVector2D(X + Size * .5f + Shown++ * (Size + 6 * Scale), Y + Size * .5f), Size, ItemColor(I), 2110 + I);
}

void AGWHUD::ShopPanel(AGWGameState* S, float X, float Y, float W, float H)
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    const UGWProfileSave* Save = GI ? GI->ActiveProfile() : nullptr;
    if (!Save)
        return;
    const gwgame::MetaProfile P = Save->ToCore();
    const float Gap = 16 * Scale, TabH = 42 * Scale, TabW = FMath::Min(170 * Scale, W * .17f);
    for (int32 I = 0; I < 3; ++I)
        Button(GWDefenseAction::ShopTab + I, GWText::Get(FString::Printf(TEXT("Shop.Tab.%d"), I)), X + I * (TabW + 10 * Scale), Y, TabW, TabH, true,
               ShopPage == I, Teal);
    StickerChip(P.stickers, X + W, Y + 3 * Scale);
    const float GridX = X, GridY = Y + TabH + Gap, GridW = W * .67f, GridH = Y + H - GridY;
    const int32 Count = ShopPage == 0 ? gwgame::WeaponCount : ShopPage == 1 ? gwgame::PenCount : gwgame::ItemCount;
    const int32 Rows = Count > 3 ? 2 : 1;
    const float CardW = (GridW - 2 * Gap) / 3, CardH = (GridH - (Rows - 1) * Gap) / Rows;
    for (int32 I = 0; I < Count; ++I)
    {
        const float CX = GridX + (I % 3) * (CardW + Gap), CY = GridY + (I / 3) * (CardH + Gap);
        bool Owned = false, Active = false;
        int32 Price = 0, Action = 0;
        FText Name, Note;
        FLinearColor Accent = Teal;
        if (ShopPage == 0)
        {
            Owned = gwgame::MetaProfile::has(P.weapons, I);
            Active = P.weapon == I;
            Price = gwgame::weaponPrice(I);
            Action = GWDefenseAction::BuyWeapon + I;
            Name = GWText::Get(FString::Printf(TEXT("Weapon.%d"), I));
            Note = GWText::Get(FString::Printf(TEXT("Weapon.%d.Note"), I));
            Accent = Coral;
        }
        else if (ShopPage == 1)
        {
            Owned = gwgame::MetaProfile::has(P.pens, I);
            Active = P.pen == I;
            Price = gwgame::penPrice(I);
            Action = GWDefenseAction::BuyPen + I;
            Name = FText::FromString(GWText::Get(FString::Printf(TEXT("Pen.%d"), I)).ToString() + TEXT("  ·  ") +
                                     GWText::Get(FString::Printf(TEXT("Pen.%d.Wall"), I)).ToString());
            Note = GWText::Get(FString::Printf(TEXT("Pen.%d.Note"), I));
            Accent = Cobalt;
        }
        else
        {
            Owned = P.items[I] > 0;
            Active = P.copies(I) > 0;
            Price = gwgame::itemPrice(I);
            Action = GWDefenseAction::BuyItem + I;
            Name = GWText::Get(FString::Printf(TEXT("Item.%d"), I));
            Note = GWText::Get(FString::Printf(TEXT("Item.%d.Note"), I));
            Accent = Marigold;
        }
        Card(CX, CY, CardW, CardH, Active ? Hex(0xFFF7D6) : Paper, Ink, 2200 + I, Active ? 1.5f : 1.f);
        if (Active)
            Tape(FVector2D(CX + CardW * .5f, CY + 3 * Scale), 70 * Scale, 18 * Scale, (I % 3 - 1) * 3.f, Accent.CopyWithNewOpacity(.9f));
        const float Icon = FMath::Min(58 * Scale, CardH * .3f);
        const FVector2D IconAt(CX + 18 * Scale + Icon * .5f, CY + 18 * Scale + Icon * .5f);
        if (ShopPage == 0)
        {
            // The player's own coin body with that gun in hand.
            Disc(IconAt - FVector2D(Icon * .25f, 0), Icon * .3f, Paper);
            Circle(IconAt - FVector2D(Icon * .25f, 0), Icon * .3f, Ink, 1.6f * Scale, 2210 + I);
            FlatWeapon(static_cast<uint8>(I), IconAt - FVector2D(Icon * .25f, 0), 0, Icon / 90.f);
        }
        else
            MetaIcon(ShopPage == 1 ? PenIcon + I : I, IconAt, Icon, ShopPage == 1 ? PenColor(I) : ItemColor(I), 2220 + I);
        const float TextX = CX + 30 * Scale + Icon, TextW = CX + CardW - 14 * Scale - TextX;
        Label(Name, TextX, CY + 16 * Scale, 17 * Scale, Ink, false, TextW, EFace::Heavy);
        if (ShopPage == 2)
            Label(GWText::Number(TEXT("Shop.Stock"), TEXT("Count"), P.items[I]), TextX, CY + 42 * Scale, 14 * Scale, Owned ? Teal : Pencil, false, TextW,
                  EFace::Bold);
        const float NoteY = CY + 26 * Scale + Icon;
        const float ButtonH = FMath::Min(42 * Scale, CardH * .22f), ButtonY = CY + CardH - ButtonH - 12 * Scale;
        Paragraph(Note, CX + 16 * Scale, NoteY, 14 * Scale, CardW - 32 * Scale, InkSoft);
        // Weapons and pens: buy once, then put to use. Items: buy as many as nine, then bring one along.
        if (ShopPage < 2)
        {
            FText Caption = Active ? GWText::Get(TEXT("Meta.InUse")) : Owned ? GWText::Get(TEXT("Meta.Use")) : GWText::Number(TEXT("Meta.Buy"), TEXT("Price"), Price);
            Button(Action, Caption, CX + 14 * Scale, ButtonY, CardW - 28 * Scale, ButtonH, !Active && (Owned || P.stickers >= Price), Active, Accent);
        }
        else
        {
            const float Half = (CardW - 28 * Scale - 8 * Scale) * .5f;
            Button(Action, GWText::Number(TEXT("Meta.Buy"), TEXT("Price"), Price), CX + 14 * Scale, ButtonY, Half, ButtonH,
                   P.stickers >= Price && P.items[I] < gwgame::MaxItemStack, false, Accent);
            const bool Free = P.loadout[0] < 0 || P.loadout[1] < 0;
            Button(GWDefenseAction::BringItem + I, GWText::Get(TEXT("Shop.Bring")), CX + 22 * Scale + Half, ButtonY, Half, ButtonH,
                   Free && P.copies(I) < P.items[I], false, Teal);
        }
    }
    // What goes into the next mission: the starting weapon, the pen and two item slots.
    const float LX = X + GridW + 26 * Scale, LW = X + W - LX, LY = GridY, LH = GridH;
    Card(LX, LY, LW, LH, PaperShade, Ink, 2300, 1.2f);
    Tape(FVector2D(LX + LW * .5f, LY + 3 * Scale), 90 * Scale, 20 * Scale, 2, Teal);
    Key(TEXT("Shop.Loadout"), LX + 18 * Scale, LY + 16 * Scale, 18 * Scale, Ink, false, LW - 36 * Scale, EFace::Heavy);
    float Cursor = LY + 54 * Scale;
    const float RowH = 56 * Scale, IconSize = 40 * Scale;
    auto Row = [&](const TCHAR* Caption, TFunctionRef<void(FVector2D)> DrawIcon, const FText& Name, int32 DropAction)
    {
        Card(LX + 12 * Scale, Cursor, LW - 24 * Scale, RowH - 8 * Scale, Paper, Ink, 2310 + DropAction % 7, .9f);
        DrawIcon(FVector2D(LX + 16 * Scale + IconSize * .5f + 6 * Scale, Cursor + (RowH - 8 * Scale) * .5f));
        const float TX = LX + 30 * Scale + IconSize, TW = LX + LW - 24 * Scale - TX - (DropAction ? 40 * Scale : 0);
        Key(Caption, TX, Cursor + 5 * Scale, 12 * Scale, InkSoft, false, TW, EFace::Bold);
        Label(Name, TX, Cursor + 20 * Scale, 16 * Scale, Ink, false, TW, EFace::Heavy);
        if (DropAction)
            Button(DropAction, FText::FromString(TEXT("×")), LX + LW - 54 * Scale, Cursor + 8 * Scale, 34 * Scale, 32 * Scale, true, false, Pencil);
        Cursor += RowH;
    };
    Row(TEXT("Shop.WeaponSlot"),
        [&](FVector2D At)
        {
            Disc(At - FVector2D(IconSize * .25f, 0), IconSize * .3f, Paper);
            Circle(At - FVector2D(IconSize * .25f, 0), IconSize * .3f, Ink, 1.4f * Scale, 2320);
            FlatWeapon(static_cast<uint8>(P.weapon), At - FVector2D(IconSize * .25f, 0), 0, IconSize / 90.f);
        },
        GWText::Get(FString::Printf(TEXT("Weapon.%d"), P.weapon)), 0);
    Row(TEXT("Shop.PenSlot"), [&](FVector2D At) { MetaIcon(PenIcon + P.pen, At, IconSize, PenColor(P.pen), 2321); },
        GWText::Get(FString::Printf(TEXT("Pen.%d.Wall"), P.pen)), 0);
    for (int32 Slot = 0; Slot < gwgame::ItemSlots; ++Slot)
    {
        const int32 Item = P.loadout[Slot];
        Row(TEXT("Shop.ItemSlot"),
            [&](FVector2D At)
            {
                if (Item >= 0)
                    MetaIcon(Item, At, IconSize, ItemColor(Item), 2322 + Slot);
                else
                    Circle(At, IconSize * .38f, Pencil, 1.4f * Scale, 2324 + Slot);
            },
            Item >= 0 ? GWText::Get(FString::Printf(TEXT("Item.%d"), Item)) : GWText::Get(TEXT("Shop.Empty")), Item >= 0 ? GWDefenseAction::DropItem + Slot : 0);
    }
    if (Cursor + 40 * Scale < LY + LH)
        Paragraph(GWText::Get(TEXT("Shop.ItemsNote")), LX + 18 * Scale, Cursor + 6 * Scale, 13 * Scale, LW - 36 * Scale, InkSoft);
    (void)S;
}

void AGWHUD::ResearchPanel(AGWGameState* S, float X, float Y, float W, float H)
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    const UGWProfileSave* Save = GI ? GI->ActiveProfile() : nullptr;
    if (!Save)
        return;
    const gwgame::MetaProfile P = Save->ToCore();
    // The room's cap first (Gate 4: a veteran in a newcomer's room sees exactly how much of their research counts).
    const int32 Tier = FMath::Clamp(static_cast<int32>(S->Tier), gwgame::MinTier, gwgame::MaxTier);
    FFormatNamedArguments CapArgs;
    CapArgs.Add(TEXT("Tier"), Tier);
    const FText Cap = FText::Format(GWText::Get(Tier >= gwgame::MaxTier ? TEXT("Research.CapAll") : TEXT("Research.Cap")), CapArgs);
    const float CapW = FMath::Min(Measure(Cap, 17 * Scale, EFace::Bold).X + 70 * Scale, W * .66f);
    Card(X, Y, CapW, 40 * Scale, Hex(0xE6F0FB), Ink, 2400, 1.f);
    Polygon({FVector2D(X + 14 * Scale, Y + 8 * Scale), FVector2D(X + 40 * Scale, Y + 8 * Scale), FVector2D(X + 40 * Scale, Y + 32 * Scale),
             FVector2D(X + 14 * Scale, Y + 32 * Scale)},
            Cobalt, Ink, 1.4f * Scale, 2401);
    Label(FText::AsNumber(Tier), X + 27 * Scale, Y + 9 * Scale, 17 * Scale, Paper, true, 0, EFace::Heavy);
    Label(Cap, X + 52 * Scale, Y + 10 * Scale, 17 * Scale, Ink, false, CapW - 60 * Scale, EFace::Bold);
    StickerChip(P.stickers, X + W, Y + 2 * Scale);
    const float Top = Y + 56 * Scale, RowH = (Y + H - Top - 34 * Scale) / gwgame::AttributeCount;
    for (int32 A = 0; A < gwgame::AttributeCount; ++A)
    {
        const auto Attr = static_cast<gwgame::Attribute>(A);
        const int32 Rank = P.ranks[A], Counted = gwgame::effectiveRank(Rank, Tier);
        const float RY = Top + A * RowH, Mid = RY + (RowH - 6 * Scale) * .5f;
        Card(X, RY, W, RowH - 6 * Scale, A % 2 ? Paper : Hex(0xFBF7EC), Ink, 2410 + A, .7f);
        const FLinearColor Mark = AttributeColor(A);
        MetaIcon(AttributeIcon + A, FVector2D(X + 32 * Scale, Mid), FMath::Min(40 * Scale, RowH * .62f), Mark, 2420 + A);
        const int32 Step = FMath::RoundToInt(gwgame::attributeStep(Attr) * 100);
        Label(GWText::Get(FString::Printf(TEXT("Attr.%d"), A)), X + 64 * Scale, Mid - 20 * Scale, 19 * Scale, Ink, false, W * .2f, EFace::Heavy);
        Label(GWText::Number(TEXT("Attr.Step"), TEXT("Percent"), Step), X + 64 * Scale, Mid + 4 * Scale, 14 * Scale, InkSoft, false, W * .2f, EFace::Bold);
        // Five pips: earned and counted here (solid), earned but above this room's tier (hatched), not yet earned.
        const float PipX = X + W * .3f, Pip = FMath::Min(30 * Scale, RowH * .48f);
        for (int32 K = 0; K < gwgame::MaxRank; ++K)
        {
            const FVector2D C(PipX + K * (Pip + 10 * Scale) + Pip * .5f, Mid);
            const TArray<FVector2D> Gem = {C + FVector2D(0, -Pip * .55f), C + FVector2D(Pip * .55f, 0), C + FVector2D(0, Pip * .55f), C + FVector2D(-Pip * .55f, 0)};
            if (K < Counted)
                Polygon(Gem, Mark, Ink, 1.5f * Scale, 2430 + A * 5 + K);
            else if (K < Rank)
            {
                Polygon(Gem, Mark.CopyWithNewOpacity(.28f), Ink, 1.5f * Scale, 2430 + A * 5 + K);
                Pen(C + FVector2D(-Pip * .3f, Pip * .3f), C + FVector2D(Pip * .3f, -Pip * .3f), Ink.CopyWithNewOpacity(.55f), 1.5f * Scale, 2460 + K, .2f);
            }
            else
                Polygon(Gem, Paper, Pencil, 1.2f * Scale, 2430 + A * 5 + K);
        }
        // What the ranks are worth: in all, and in this room when the tier holds some back.
        const float InfoX = PipX + gwgame::MaxRank * (Pip + 10 * Scale) + 14 * Scale, InfoW = X + W - 220 * Scale - InfoX;
        Label(GWText::Number(TEXT("Research.Total"), TEXT("Percent"), Step * Rank), InfoX, Mid - 20 * Scale, 17 * Scale, Ink, false, InfoW, EFace::Heavy);
        if (Counted < Rank)
            Label(GWText::Number(TEXT("Research.Counted"), TEXT("Percent"), Step * Counted), InfoX, Mid + 4 * Scale, 14 * Scale, Cobalt, false, InfoW,
                  EFace::Bold);
        const int32 Cost = gwgame::researchCost(Rank);
        const float BW = 196 * Scale, BH = FMath::Min(44 * Scale, RowH * .64f);
        Button(GWDefenseAction::Research + A, Cost < 0 ? GWText::Get(TEXT("Research.Full")) : GWText::Number(TEXT("Research.Up"), TEXT("Price"), Cost),
               X + W - BW - 12 * Scale, Mid - BH * .5f, BW, BH, Cost >= 0 && P.stickers >= Cost, Cost < 0, Mark);
    }
    Paragraph(GWText::Get(TEXT("Research.Note")), X, Y + H - 26 * Scale, 14 * Scale, W, InkSoft);
}

void AGWHUD::WardrobePanel(AGWGameState* S, float X, float Y, float W, float H)
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    const UGWProfileSave* Save = GI ? GI->ActiveProfile() : nullptr;
    if (!Save)
        return;
    const gwgame::MetaProfile P = Save->ToCore();
    StickerChip(P.stickers, X + W, Y);
    // Hats on the left, titles on the right.
    const float Gap = 14 * Scale, HatW = W * .55f, CardW = (HatW - 3 * Gap) / 4, Top = Y + 46 * Scale;
    Key(TEXT("Wardrobe.Hats"), X, Y + 6 * Scale, 20 * Scale, Ink, false, HatW, EFace::Heavy);
    const float CardH = (Y + H - Top - Gap) / 2;
    for (int32 I = 0; I < gwgame::HatCount; ++I)
    {
        const float CX = X + (I % 4) * (CardW + Gap), CY = Top + (I / 4) * (CardH + Gap);
        const bool Owned = gwgame::MetaProfile::has(P.hats, I), Worn = P.hat == I;
        Card(CX, CY, CardW, CardH, Worn ? Hex(0xFFF7D6) : Paper, Ink, 2500 + I, Worn ? 1.5f : 1.f);
        if (Worn)
            Tape(FVector2D(CX + CardW * .5f, CY + 3 * Scale), 56 * Scale, 16 * Scale, (I % 3 - 1) * 4.f, Plum.CopyWithNewOpacity(.85f));
        const float Icon = FMath::Min(CardW * .5f, CardH * .38f);
        MetaIcon(HatIcon + I, FVector2D(CX + CardW * .5f, CY + 16 * Scale + Icon * .5f), Icon, Plum, 2510 + I);
        Label(GWText::Get(FString::Printf(TEXT("Hat.%d"), I)), CX + CardW * .5f, CY + 22 * Scale + Icon, 15 * Scale, Ink, true, CardW - 16 * Scale, EFace::Heavy);
        const int32 Price = gwgame::hatPrice(I);
        const float BH = FMath::Min(38 * Scale, CardH * .24f), BY = CY + CardH - BH - 10 * Scale;
        if (Price < 0 && !Owned)
        {
            // The crown is an achievement's prize, never for sale.
            const int32 From = AchievementFor(true, I);
            FFormatNamedArguments Args;
            Args.Add(TEXT("Name"), GWText::Get(FString::Printf(TEXT("Ach.%d"), From)));
            Paragraph(FText::Format(GWText::Get(TEXT("Meta.FromAchievement")), Args), CX + 10 * Scale, BY - 4 * Scale, 12 * Scale, CardW - 20 * Scale, InkSoft);
        }
        else
            Button(GWDefenseAction::BuyHat + I,
                   Worn ? GWText::Get(TEXT("Meta.Wearing")) : Owned ? GWText::Get(TEXT("Meta.Wear")) : GWText::Number(TEXT("Meta.Buy"), TEXT("Price"), Price),
                   CX + 10 * Scale, BY, CardW - 20 * Scale, BH, !Worn && (Owned || P.stickers >= Price), Worn, Plum);
    }
    const float TX = X + HatW + 26 * Scale, TW = X + W - TX;
    Key(TEXT("Wardrobe.Titles"), TX, Y + 6 * Scale, 20 * Scale, Ink, false, TW * .6f, EFace::Heavy);
    const float RowH = (Y + H - Top) / gwgame::TitleCount;
    for (int32 T = 0; T < gwgame::TitleCount; ++T)
    {
        const float RY = Top + T * RowH;
        const bool Owned = gwgame::MetaProfile::has(P.titles, T), Worn = P.title == T;
        if (Worn)
            Highlight(TX - 4 * Scale, RY + 2 * Scale, TW + 8 * Scale, RowH - 4 * Scale, Plum.CopyWithNewOpacity(.16f), 2540 + T);
        const FText Name = GWText::Get(FString::Printf(TEXT("Title.%d"), T));
        Label(Name, TX + 6 * Scale, RY + (RowH - 16 * Scale * 1.3f) * .5f, 16 * Scale, Owned ? Ink : Pencil, false, TW * .42f, EFace::Heavy);
        const float BW = 96 * Scale, BH = FMath::Min(30 * Scale, RowH - 6 * Scale);
        if (Owned)
            Button(GWDefenseAction::WearTitle + T, GWText::Get(Worn ? TEXT("Meta.Wearing") : TEXT("Meta.Wear")), TX + TW - BW, RY + (RowH - BH) * .5f, BW, BH, !Worn,
                   Worn, Plum);
        else
        {
            // How to earn it: an award at the end of a run with friends, or an achievement.
            FText How;
            if (T >= 1 && T <= gwgame::AwardCount)
                How = GWText::Get(TEXT("Title.Award"));
            else
            {
                FFormatNamedArguments Args;
                Args.Add(TEXT("Name"), GWText::Get(FString::Printf(TEXT("Ach.%d"), AchievementFor(false, T))));
                How = FText::Format(GWText::Get(TEXT("Meta.FromAchievement")), Args);
            }
            Label(How, TX + TW * .45f, RY + (RowH - 13 * Scale * 1.3f) * .5f, 13 * Scale, InkSoft, false, TW * .55f, EFace::Body);
        }
        if (T + 1 < gwgame::TitleCount)
            Pen(FVector2D(TX, RY + RowH), FVector2D(TX + TW, RY + RowH), Pencil.CopyWithNewOpacity(.25f), 1, 2560 + T, .3f);
    }
    (void)S;
}

void AGWHUD::AchievementsPanel(AGWGameState* S, float X, float Y, float W, float H)
{
    // A sticker chart (design doc 成就墙): one row per achievement, one column per player in the room, a sticker in
    // the player's seat colour where it is earned and an empty ring where it is still missing.
    struct FColumn
    {
        const AGWPlayerState* State;
        int32 Seat;
        int32 Count;
    };
    TArray<FColumn> Columns;
    for (int32 I = 0; I < FMath::Min(S->PlayerArray.Num(), 8); ++I)
        if (const auto* PS = Cast<AGWPlayerState>(S->PlayerArray[I]))
        {
            const FGWUnitView* U = S->FindUnit(PS->UnitId);
            Columns.Add({PS, U && U->Seat >= 0 ? U->Seat : I, FMath::CountBits(PS->Achievements & gwgame::AchievementMask)});
        }
    const int32 Half = (gwgame::AchievementCount + 1) / 2;
    const float Gap = 26 * Scale, BlockW = (W - Gap) * .5f, Col = FMath::Min(30 * Scale, BlockW * .5f / FMath::Max(1, Columns.Num()));
    const float NameW = BlockW - Col * Columns.Num() - 8 * Scale, HeadH = 30 * Scale, LegendH = 54 * Scale;
    const float RowH = (H - HeadH - LegendH - 10 * Scale) / Half;
    int32 Hover = -1;
    float MX = 0, MY = 0;
    const bool Mouse = PlayerOwner && PlayerOwner->GetMousePosition(MX, MY);
    for (int32 Block = 0; Block < 2; ++Block)
    {
        const float BX = X + Block * (BlockW + Gap), ColX = BX + NameW + 8 * Scale;
        // Column heads: each player's seat colour with their number.
        for (int32 C = 0; C < Columns.Num(); ++C)
        {
            const FVector2D Head(ColX + C * Col + Col * .5f, Y + HeadH * .5f);
            Disc(Head, Col * .34f, Seat(Columns[C].Seat));
            Circle(Head, Col * .34f, Ink, 1.2f * Scale, 2600 + C);
            Label(FText::AsNumber(C + 1), Head.X, Head.Y - 7 * Scale, 11 * Scale, Paper, true, 0, EFace::Heavy);
        }
        for (int32 R = 0; R < Half; ++R)
        {
            const int32 Index = Block * Half + R;
            if (Index >= gwgame::AchievementCount)
                break;
            const float RY = Y + HeadH + R * RowH;
            const bool Over = Mouse && MX >= BX && MX <= BX + BlockW && MY >= RY && MY < RY + RowH;
            if (Over)
                Hover = Index;
            if (Over || R % 2 == 0)
                Box(BX, RY, BlockW, RowH, (Over ? Marigold.CopyWithNewOpacity(.18f) : PaperShade.CopyWithNewOpacity(.45f)));
            const float Icon = FMath::Min(22 * Scale, RowH * .8f);
            AchievementIcon(Index, FVector2D(BX + 6 * Scale + Icon * .5f, RY + RowH * .5f), Icon, true, 2620 + Index);
            Label(GWText::Get(FString::Printf(TEXT("Ach.%d"), Index)), BX + 14 * Scale + Icon, RY + (RowH - 14 * Scale * 1.3f) * .5f, 14 * Scale, Ink, false,
                  NameW - Icon - 18 * Scale, EFace::Bold);
            for (int32 C = 0; C < Columns.Num(); ++C)
            {
                const FVector2D At(ColX + C * Col + Col * .5f, RY + RowH * .5f);
                if ((Columns[C].State->Achievements >> Index) & 1)
                    Glyph(5, At, FMath::Min(Col * .8f, RowH * .8f), Seat(Columns[C].Seat), 2660 + Index * 8 + C);
                else
                    Circle(At, FMath::Min(Col, RowH) * .22f, Pencil.CopyWithNewOpacity(.5f), 1.f * Scale, 2660 + Index * 8 + C);
            }
        }
    }
    // The legend: who each column is and how many of the stickers they have.
    const float LY = Y + H - LegendH + 6 * Scale, ChipW = FMath::Min(260 * Scale, (W - 7 * 8 * Scale) / FMath::Max(4, Columns.Num()));
    for (int32 C = 0; C < Columns.Num(); ++C)
    {
        const float CX = X + C * (ChipW + 8 * Scale);
        Card(CX, LY, ChipW, LegendH - 12 * Scale, Paper, Ink, 2700 + C, .9f);
        Disc(FVector2D(CX + 18 * Scale, LY + (LegendH - 12 * Scale) * .5f), 10 * Scale, Seat(Columns[C].Seat));
        Label(FText::AsNumber(C + 1), CX + 18 * Scale, LY + (LegendH - 12 * Scale) * .5f - 7 * Scale, 11 * Scale, Paper, true, 0, EFace::Heavy);
        FString Name = Columns[C].State->GetPlayerName();
        const FString Tail = FString::Printf(TEXT("  %d/%d"), Columns[C].Count, gwgame::AchievementCount);
        const float Room = ChipW - 44 * Scale - Measure(FText::FromString(Tail), 14 * Scale, EFace::Heavy).X;
        if (Measure(FText::FromString(Name), 14 * Scale, EFace::Bold).X > Room)
        {
            while (Name.Len() > 1 && Measure(FText::FromString(Name + TEXT("…")), 14 * Scale, EFace::Bold).X > Room)
                Name.LeftChopInline(1);
            Name += TEXT("…");
        }
        Label(FText::FromString(Name), CX + 34 * Scale, LY + 4 * Scale, 14 * Scale, Ink, false, Room, EFace::Bold);
        Label(FText::FromString(Tail), CX + ChipW - 8 * Scale, LY + 4 * Scale, 14 * Scale, Teal, false, 0, EFace::Heavy);
        if (Columns[C].State->Title > 0)
            Label(GWText::Get(FString::Printf(TEXT("Title.%d"), Columns[C].State->Title)), CX + 34 * Scale, LY + 22 * Scale, 12 * Scale, Plum, false,
                  ChipW - 44 * Scale, EFace::Bold);
    }
    // The row under the mouse: what it asks for and what it pays.
    if (Hover >= 0)
    {
        const FText Name = GWText::Get(FString::Printf(TEXT("Ach.%d"), Hover));
        const FText Note = GWText::Get(FString::Printf(TEXT("Ach.%d.Note"), Hover));
        const FText Pay = GWText::Number(TEXT("Wall.Reward"), TEXT("Count"), gwgame::achievementInfo(Hover).stickers);
        const float TW = 360 * Scale, TH = 34 * Scale + Paragraph(Note, 0, -10000, 14 * Scale, TW - 28 * Scale, FLinearColor::Transparent) + 26 * Scale;
        const float TX = FMath::Clamp(MX + 18 * Scale, 8 * Scale, Width - TW - 8 * Scale), TY = FMath::Clamp(MY + 18 * Scale, 8 * Scale, Height - TH - 8 * Scale);
        Card(TX, TY, TW, TH, Hex(0xFFF7D6), Ink, 2750, 1.6f);
        AchievementIcon(Hover, FVector2D(TX + 24 * Scale, TY + 20 * Scale), 24 * Scale, true, 2751);
        Label(Name, TX + 44 * Scale, TY + 9 * Scale, 16 * Scale, Ink, false, TW - 56 * Scale, EFace::Heavy);
        const float Used = Paragraph(Note, TX + 14 * Scale, TY + 36 * Scale, 14 * Scale, TW - 28 * Scale, InkSoft);
        Label(Pay, TX + 14 * Scale, TY + 40 * Scale + Used, 13 * Scale, Teal, false, TW - 28 * Scale, EFace::Bold);
    }
}

void AGWHUD::TierPicker(AGWGameState* S, float X, float Y, float W, float H)
{
    // The host picks among the tiers their own profile has opened; everyone sees the room's tier.
    const bool Host = LocalHost(S);
    const int32 Top = FMath::Clamp(static_cast<int32>(S->MaxTier), gwgame::MinTier, gwgame::MaxTier);
    const FText Title = GWText::Get(TEXT("Tier.Title"));
    const float TitleW = Measure(Title, 16 * Scale, EFace::Heavy).X + 12 * Scale;
    Label(Title, X, Y + (H - 16 * Scale * 1.3f) * .5f, 16 * Scale, InkSoft, false, 0, EFace::Heavy);
    const float BW = FMath::Min(46 * Scale, (W - TitleW - 4 * 6 * Scale) / 5);
    float MX = 0, MY = 0;
    const bool Mouse = PlayerOwner && PlayerOwner->GetMousePosition(MX, MY);
    for (int32 T = gwgame::MinTier; T <= gwgame::MaxTier; ++T)
    {
        const float BX = X + TitleW + (T - 1) * (BW + 6 * Scale);
        const bool Open = T <= Top;
        Button(GWDefenseAction::Tier + T, FText::AsNumber(T), BX, Y, BW, H, Host && Open, S->Tier == T, T >= 4 ? Coral : T >= 2 ? Marigold : Teal);
        if (!Open)
            Glyph(9, FVector2D(BX + BW - 8 * Scale, Y + 8 * Scale), 14 * Scale, Pencil, 2800 + T);
        // A locked tier says what opens it.
        if (!Open && Mouse && MX >= BX && MX <= BX + BW && MY >= Y && MY <= Y + H)
        {
            const FText How = GWText::Get(FString::Printf(TEXT("Tier.%d.How"), T));
            const float TW = Measure(How, 14 * Scale, EFace::Bold).X + 28 * Scale;
            Card(FMath::Min(MX, Width - TW - 8 * Scale), Y + H + 6 * Scale, TW, 34 * Scale, Hex(0xFFF7D6), Ink, 2810 + T, 1.3f);
            Label(How, FMath::Min(MX, Width - TW - 8 * Scale) + 14 * Scale, Y + H + 13 * Scale, 14 * Scale, Ink, false, 0, EFace::Bold);
        }
    }
}

void AGWHUD::TierSummary(AGWGameState* S, float X, float Y, float W)
{
    const int32 Tier = FMath::Clamp(static_cast<int32>(S->Tier), gwgame::MinTier, gwgame::MaxTier);
    const gwgame::TierInfo& Info = gwgame::tierInfo(Tier);
    FFormatNamedArguments Args;
    Args.Add(TEXT("Tier"), Tier);
    Args.Add(TEXT("Reward"), Multiplier(Info.reward));
    Args.Add(TEXT("Health"), Multiplier(Info.enemyHealth));
    Args.Add(TEXT("Damage"), Multiplier(Info.enemyDamage));
    Args.Add(TEXT("Cap"), Info.cap);
    const FText Text = FText::Format(GWText::Get(TEXT("Tier.Summary")), Args);
    Highlight(X - 4 * Scale, Y + 3 * Scale, FMath::Min(Measure(Text, 15 * Scale, EFace::Bold).X, W) + 8 * Scale, 20 * Scale,
              (Tier >= 4 ? Coral : Tier >= 2 ? Marigold : Teal).CopyWithNewOpacity(.25f), 2820);
    Label(Text, X, Y, 15 * Scale, Ink, false, W, EFace::Bold);
}

void AGWHUD::Toasts()
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    if (!GI)
        return;
    // One achievement at a time, each pinned up for a few seconds in the top-right corner.
    constexpr double Show = 3.4;
    if (ToastShowing >= 0 && Clock - ToastStart > Show)
        ToastShowing = -1;
    if (ToastShowing < 0 && !GI->PendingToasts.IsEmpty())
    {
        ToastShowing = GI->PendingToasts[0];
        GI->PendingToasts.RemoveAt(0);
        ToastStart = Clock;
    }
    if (ToastShowing < 0)
        return;
    const float Age = static_cast<float>(Clock - ToastStart);
    const float In = FMath::Clamp(Age / .25f, 0.f, 1.f), Out = FMath::Clamp((Show - Age) / .3f, 0.f, 1.f), Shown = FMath::Min(In, Out);
    const float W = 380 * Scale, H = 92 * Scale, X = Width - W - 22 * Scale + (1 - Shown) * 60 * Scale, Y = 128 * Scale;
    Card(X, Y, W, H, Hex(0xFFF7D6).CopyWithNewOpacity(Shown), Ink.CopyWithNewOpacity(Shown), 2900, 1.8f);
    Tape(FVector2D(X + W * .5f, Y + 3 * Scale), 100 * Scale, 22 * Scale, -3, Marigold.CopyWithNewOpacity(.9f * Shown));
    AchievementIcon(ToastShowing, FVector2D(X + 44 * Scale, Y + H * .5f), 50 * Scale, true, 2901);
    Key(TEXT("Toast.Title"), X + 82 * Scale, Y + 14 * Scale, 14 * Scale, Coral.CopyWithNewOpacity(Shown), false, W - 96 * Scale, EFace::Heavy);
    Label(GWText::Get(FString::Printf(TEXT("Ach.%d"), ToastShowing)), X + 82 * Scale, Y + 34 * Scale, 20 * Scale, Ink.CopyWithNewOpacity(Shown), false,
          W - 96 * Scale, EFace::Heavy);
    Label(GWText::Number(TEXT("Wall.Reward"), TEXT("Count"), gwgame::achievementInfo(ToastShowing).stickers), X + 82 * Scale, Y + 62 * Scale, 14 * Scale,
          Teal.CopyWithNewOpacity(Shown), false, W - 96 * Scale, EFace::Bold);
}
