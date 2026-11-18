#include "Engine/Canvas.h"
#include "GWCinematic.h"
#include "GWController.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWMotion.h"
#include "GWPalette.h"
#include "GWPlayerState.h"
#include "GWText.h"
#include "GWWorld.h"
#include "InputCoreTypes.h"

// The expedition on the sketchbook HUD (design v2.1, the HUD row of chapter 10): a floor board pinned at the top (stage
// and floor, the stage's pen, the room under way), a minimap of the floor's rooms in the top-right corner, the five-slot
// hotbar at the bottom, the pouch card (health, shields, ink and the stickers not banked yet), the teammates' kits,
// prompts over whatever E would take, buy, work or open, the room banners, the ghost's card, Tab's kit sheet, the revive
// sheet and the result card.
using namespace GWPalette;

namespace
{
constexpr int32 HatIconBase = 10;
constexpr uint8 WeaponSlot = static_cast<uint8>(gwgame::SlotKind::Weapon), ItemSlot = static_cast<uint8>(gwgame::SlotKind::Item);
constexpr uint8 LootStickers = static_cast<uint8>(gwgame::LootKind::Stickers), LootWeapon = static_cast<uint8>(gwgame::LootKind::Weapon),
                LootItem = static_cast<uint8>(gwgame::LootKind::Item), LootLook = static_cast<uint8>(gwgame::LootKind::Look),
                Machine = static_cast<uint8>(gwgame::ServiceLoot);
constexpr uint8 RoomStart = static_cast<uint8>(gwgame::RoomKind::Start), RoomCombat = static_cast<uint8>(gwgame::RoomKind::Combat),
                RoomPortal = static_cast<uint8>(gwgame::RoomKind::Portal), RoomBoss = static_cast<uint8>(gwgame::RoomKind::Boss),
                RoomShop = static_cast<uint8>(gwgame::RoomKind::Shop), RoomTreasure = static_cast<uint8>(gwgame::RoomKind::Treasure),
                RoomEventKind = static_cast<uint8>(gwgame::RoomKind::Event);
constexpr uint8 StateArmed = static_cast<uint8>(gwgame::RoomState::Armed), StateLocked = static_cast<uint8>(gwgame::RoomState::Locked),
                StateCleared = static_cast<uint8>(gwgame::RoomState::Cleared);
constexpr int32 ReviveKitGear = static_cast<int32>(gwgame::Gear::ReviveKit);
// How close E must be to a fallen teammate's outline to open the revive sheet on them (2026-11-17).
constexpr float GhostReach = 170;

// Named arguments for a localised line: FArgs()(TEXT("Name"), Value).Text(TEXT("Key")).
struct FArgs
{
    FFormatNamedArguments Values;
    FArgs& operator()(const TCHAR* Name, const FFormatArgumentValue& Value)
    {
        Values.Add(Name, Value);
        return *this;
    }
    FText Text(const TCHAR* Key) const
    {
        return FText::Format(GWText::Get(Key), Values);
    }
};
FText WeaponName(int32 Weapon)
{
    return Weapon == gwgame::FistWeapon ? GWText::Get(TEXT("Weapon.Fists")) : GWText::Get(FString::Printf(TEXT("Weapon.%d"), FMath::Clamp(Weapon, 0, 5)));
}
FText GearName(int32 Gear)
{
    return GWText::Get(FString::Printf(TEXT("Gear.%d"), FMath::Clamp(Gear, 0, gwgame::GearCount - 1)));
}
FText GearNote(int32 Gear)
{
    return GWText::Get(FString::Printf(TEXT("Gear.%d.Note"), FMath::Clamp(Gear, 0, gwgame::GearCount - 1)));
}
FText LookName(int32 Look)
{
    const int32 Category = gwgame::lookCategory(Look), Index = gwgame::lookIndex(Look);
    if (Category == 0)
        return GWText::Get(FString::Printf(TEXT("Hat.%d"), FMath::Clamp(Index, 0, 8)));
    if (Category == 5)
        return GWText::Get(FString::Printf(TEXT("Title.%d"), FMath::Clamp(Index, 0, 10)));
    return GWText::Get(FString::Printf(TEXT("Look.%d.%d"), FMath::Clamp(Category, 1, 4), FMath::Clamp(Index, 1, 3)));
}
// A quality gun's own name (Gun.N), else the armoury gun's.
FText GunName(int32 Weapon, int32 Variant)
{
    if (Variant >= 0 && Variant < gwgame::GunVariantCount && gwgame::validVariant(Weapon, Variant))
        return GWText::Get(FString::Printf(TEXT("Gun.%d"), Variant));
    return WeaponName(Weapon);
}
FText SlotName(uint8 Kind, int32 What, int32 Count, int32 Variant = -1)
{
    if (Kind == WeaponSlot)
        return Count >= 2 ? FArgs()(TEXT("Name"), GunName(What, Variant)).Text(TEXT("Expedition.Pair")) : GunName(What, Variant);
    if (Kind == ItemSlot)
        return Count > 1 ? FText::FromString(FString::Printf(TEXT("%s ×%d"), *GearName(What).ToString(), Count)) : GearName(What);
    return WeaponName(gwgame::FistWeapon);
}
FText PieceName(uint8 Kind, int32 What, int32 Variant)
{
    if (Kind == LootWeapon)
        return GunName(What, Variant);
    if (Kind == LootItem)
        return GearName(What);
    return LookName(What);
}
// Quality as a number (0 none, 1 green .. 5 red) of a piece and of a kit slot, and its name.
int32 PieceQuality(uint8 Kind, int32 What, int32 Variant)
{
    return static_cast<int32>(gwgame::pieceQuality(static_cast<gwgame::LootKind>(Kind), What, Variant));
}
int32 SlotQuality(const FGWUnitView::FSlot& Slot)
{
    return Slot.Kind == WeaponSlot ? PieceQuality(LootWeapon, Slot.What, Slot.Variant) : Slot.Kind == ItemSlot ? PieceQuality(LootItem, Slot.What, -1) : 0;
}
FText QualityName(int32 Quality)
{
    return GWText::Get(FString::Printf(TEXT("Quality.%d"), FMath::Clamp(Quality, 0, 5)));
}
// What the shop's buyer pays for a slot (gwgame::sellCoins).
int32 SlotCoins(const FGWUnitView::FSlot& Slot)
{
    gwgame::HotSlot H;
    H.kind = static_cast<gwgame::SlotKind>(Slot.Kind);
    H.what = Slot.What;
    H.count = Slot.Count;
    H.variant = Slot.Variant;
    return Slot.Kind ? gwgame::sellCoins(H) : 0;
}
// What a player can spend, in stickers (the pouch and every ten coins).
int32 Purse(const FGWUnitView& U)
{
    return U.Pouch + U.Coins / gwgame::CoinsPerSticker;
}
bool IsTrader(const FGWOfferNet& O)
{
    return O.Kind == Machine && O.What == static_cast<uint8>(gwgame::Service::Trade);
}
FText ServiceName(int32 Service)
{
    return GWText::Get(FString::Printf(TEXT("Service.%d"), FMath::Clamp(Service, 0, static_cast<int32>(gwgame::Service::Count) - 1)));
}
FText MachineName(int32 What, int32 Extra);
FText WareName(uint8 Kind, int32 What, int32 Extra = -1)
{
    if (Kind == Machine)
        return MachineName(What, Extra);
    if (Kind == LootWeapon)
        return GunName(What, Extra);
    if (Kind == LootLook)
        return LookName(What);
    return GearName(What);
}
FText RoomName(const FGWRoomNet& R)
{
    if (R.Kind == RoomEventKind && R.Event < gwgame::RoomEventCount)
        return GWText::Get(FString::Printf(TEXT("Event.%d"), R.Event));
    return GWText::Get(FString::Printf(TEXT("Room.%d"), FMath::Min<int32>(R.Kind, 6)));
}
FText BiomeName(int32 Pen)
{
    return GWText::Get(FString::Printf(TEXT("Biome.%d"), FMath::Clamp(Pen, 0, gwgame::BiomeCount - 1)));
}
FText PrankName(int32 Prank)
{
    return GWText::Get(FString::Printf(TEXT("Prank.%d"), FMath::Clamp(Prank, 0, gwgame::FloorPrankCount - 1)));
}
FText MachineName(int32 What, int32 Extra)
{
    if (What == static_cast<int32>(gwgame::Service::Vote) && Extra >= 0)
        return FArgs()(TEXT("Name"), PrankName(Extra)).Text(TEXT("Service.VoteFor"));
    if (What == static_cast<int32>(gwgame::Service::Borrow) && Extra >= 0)
        return FArgs()(TEXT("Name"), BiomeName(Extra)).Text(TEXT("Service.BorrowPen"));
    return ServiceName(What);
}
FText FoeName(int32 Foe)
{
    return GWText::Get(FString::Printf(TEXT("Foe.%d"), FMath::Clamp(Foe, 0, gwgame::FoeCount - 1)));
}
FLinearColor GearColor(int32 Gear)
{
    static const uint32 Colors[gwgame::GearCount] = {0xF6F0E1, 0xD6BC90, 0xF7D84A, 0x2B3A67, 0xFFFFFF, 0xE8553F, 0xE23A33, 0x9AD8CB, 0xF2A93B,
                                                     0xC5CCD6, 0xF2C9A0, 0xF2A93B, 0x4B3F72, 0x5EC8E8, 0x9AD8CB, 0xC5CCD6, 0x2F6BD8, 0xB23A7A,
                                                     0x2F6BD8, 0xD6BC90, 0x3A9E45, 0xFFD84A, 0xE8553F, 0xFF6A2B, 0xD6BC90};
    return Hex(Colors[FMath::Clamp(Gear, 0, gwgame::GearCount - 1)]);
}
int32 BodyGlyph(uint8 Shape)
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
FVector Unpack(uint16 X, uint16 Y)
{
    return FVector(GWNet::UnpackCoord(X), GWNet::UnpackCoord(Y), 0);
}
// A ghost is a fallen player waiting for a drawing; the extracted have left the run.
bool IsGhost(const FGWUnitView& U)
{
    return U.Human && !U.Alive && U.Ghost && !U.Extracted;
}
int32 KitSlotOf(const FGWUnitView& U, int32 Gear)
{
    for (int32 I = 0; I < 5; ++I)
        if (U.Hotbar[I].Kind == ItemSlot && U.Hotbar[I].What == Gear)
            return I;
    return -1;
}
// A revive drawing's ink as the host charges it (Battle::reviveDrawn): twice as much without paper & pen in the hotbar.
int32 ReviveCost(const FGWUnitView& U, double Share)
{
    return static_cast<int32>(FMath::RoundToInt(gwgame::reviveInk(Share) * (KitSlotOf(U, ReviveKitGear) >= 0 ? 1. : gwgame::KitlessReviveInk)));
}
// The same, anywhere in the kit (the bag counts): the kit index, -1 none.
int32 KitIndexOf(const FGWUnitView& U, int32 Gear)
{
    for (int32 I = 0; I < 5 + FMath::Clamp(U.PackSize, 0, 10); ++I)
        if (U.KitSlot(I).Kind == ItemSlot && U.KitSlot(I).What == Gear)
            return I;
    return -1;
}
// A long gun takes both hands (2026-11-17: everything but the pistol).
bool IsLongGun(const FGWUnitView::FSlot& Slot)
{
    return Slot.Kind == WeaponSlot && gwgame::twoHanded(Slot.What);
}
// The guns in hand by name: a long gun's or a pistol's, or two pistols ("dual X", or both names when they differ).
FText HandsName(const FGWUnitView& U)
{
    const auto &L = U.Hotbar[0], &R = U.Hotbar[1];
    if (L.Kind == WeaponSlot && R.Kind == WeaponSlot)
        return L.What == R.What && L.Variant == R.Variant ? FArgs()(TEXT("Name"), GunName(L.What, L.Variant)).Text(TEXT("Expedition.Pair"))
                                                          : FArgs()(TEXT("Left"), GunName(L.What, L.Variant))(TEXT("Right"), GunName(R.What, R.Variant)).Text(TEXT("Expedition.PairOf"));
    if (L.Kind == WeaponSlot)
        return GunName(L.What, L.Variant);
    if (R.Kind == WeaponSlot)
        return GunName(R.What, R.Variant);
    return WeaponName(gwgame::FistWeapon);
}
// Where a piece on the floor would go if picked up (gwgame::planStow, the host's own rule).
gwgame::StowPlan PlanFor(const FGWUnitView& U, uint8 LootKind, int32 What)
{
    std::array<gwgame::HotSlot, gwgame::KitSlots> Kit{};
    const int32 Open = FMath::Clamp(U.PackSize, 0, gwgame::PackMax);
    for (int32 I = 0; I < gwgame::HotbarSlots + Open; ++I)
    {
        const auto& Slot = U.KitSlot(I);
        Kit[static_cast<size_t>(I)].kind = static_cast<gwgame::SlotKind>(Slot.Kind);
        Kit[static_cast<size_t>(I)].what = Slot.What;
        Kit[static_cast<size_t>(I)].count = Slot.Count;
        Kit[static_cast<size_t>(I)].variant = Slot.Variant;
    }
    const bool Gun = LootKind == LootWeapon;
    return gwgame::planStow([&](int I) -> const gwgame::HotSlot& { return Kit[static_cast<size_t>(I)]; }, Open, Gun, What,
                            !Gun && gwgame::gearOneTime(What));
}
} // namespace

// ---- Icons ----

void AGWHUD::GearIcon(int32 Gear, FVector2D C, float Size, int32 Seed)
{
    const float R = Size * .5f, W = FMath::Max(1.2f, Size * .055f);
    const FLinearColor Tint = GearColor(Gear);
    auto P = [&](float X, float Y) { return C + FVector2D(X, Y) * R; };
    // Points along a slanted axis (U along it, V across), for the long tools.
    auto Slant = [&](float Degrees)
    {
        const float A = FMath::DegreesToRadians(Degrees);
        const FVector2D Along(FMath::Cos(A), FMath::Sin(A)), Across(-Along.Y, Along.X);
        const FVector2D Middle = C;
        const float Radius = R;
        return [Middle, Radius, Along, Across](float U, float V) { return Middle + (Along * U + Across * V) * Radius; };
    };
    switch (static_cast<gwgame::Gear>(FMath::Clamp(Gear, 0, gwgame::GearCount - 1)))
    {
    case gwgame::Gear::Eraser:
    {
        // A 2B eraser: white rubber in its blue sleeve.
        const auto Q = Slant(-24);
        Polygon({Q(-.95f, -.42f), Q(.95f, -.42f), Q(.95f, .42f), Q(-.95f, .42f)}, Paper, Ink, W, Seed);
        Polygon({Q(-.05f, -.46f), Q(.72f, -.46f), Q(.72f, .46f), Q(-.05f, .46f)}, Cobalt, Ink, W, Seed + 1);
        Pen(Q(.1f, -.1f), Q(.58f, -.1f), Paper, W, Seed + 2, .2f);
        break;
    }
    case gwgame::Gear::ReviveKit:
    {
        // A sheet with a loop drawn on it, and the pencil that drew it.
        Polygon({P(-.75f, -.85f), P(.55f, -.95f), P(.72f, .85f), P(-.6f, .95f)}, Paper, Ink, W, Seed);
        Circle(P(-.05f, -.05f), R * .36f, Coral, W * 1.2f, Seed + 1);
        const auto Q = Slant(-50);
        const FVector2D Shift = FVector2D(.28f, .3f) * R;
        Polygon({Q(-.75f, -.16f) + Shift, Q(.35f, -.16f) + Shift, Q(.35f, .16f) + Shift, Q(-.75f, .16f) + Shift}, Hex(0xF2C230), Ink, W, Seed + 2);
        Polygon({Q(.35f, -.16f) + Shift, Q(.7f, 0) + Shift, Q(.35f, .16f) + Shift}, Kraft, Ink, W, Seed + 3);
        break;
    }
    case gwgame::Gear::StickyNote:
        Polygon({P(-.8f, -.8f), P(.8f, -.8f), P(.8f, .35f), P(.35f, .8f), P(-.8f, .8f)}, Tint, Ink, W, Seed);
        Polygon({P(.8f, .35f), P(.35f, .35f), P(.35f, .8f)}, Tint * FLinearColor(.82f, .82f, .7f, 1), Ink, W * .8f, Seed + 1);
        Pen(P(-.55f, -.35f), P(.5f, -.35f), InkSoft, W * .7f, Seed + 2, .3f);
        Pen(P(-.55f, 0), P(.2f, 0), InkSoft, W * .7f, Seed + 3, .3f);
        break;
    case gwgame::Gear::InkCapsule:
    {
        // A capsule, half ink and half paper.
        const auto Q = Slant(-30);
        TArray<FVector2D> Dark, Light;
        Dark.Add(Q(0, -.42f));
        Light.Add(Q(0, .42f));
        for (int32 K = 0; K <= 8; ++K)
        {
            const float A = PI * .5f + PI * K / 8.f;
            Dark.Add(Q(-.5f + FMath::Cos(A) * .42f, FMath::Sin(A) * -.42f));
            Light.Add(Q(.5f - FMath::Cos(A) * .42f, FMath::Sin(A) * .42f));
        }
        Dark.Add(Q(0, .42f));
        Light.Add(Q(0, -.42f));
        Polygon(Dark, Tint, Ink, W, Seed);
        Polygon(Light, Paper, Ink, W, Seed + 1);
        Disc(Q(-.5f, -.12f), R * .1f, Paper.CopyWithNewOpacity(.7f), 8);
        break;
    }
    case gwgame::Gear::Correction:
        Polygon({P(-.4f, -.3f), P(.4f, -.3f), P(.46f, .92f), P(-.46f, .92f)}, Paper, Ink, W, Seed);
        Polygon({P(-.26f, -.78f), P(.26f, -.78f), P(.26f, -.3f), P(-.26f, -.3f)}, Teal, Ink, W, Seed + 1);
        Polygon({P(-.1f, -.98f), P(.1f, -.98f), P(.12f, -.78f), P(-.12f, -.78f)}, Ink, Ink, W * .6f, Seed + 2);
        Pen(P(-.25f, .15f), P(.25f, .15f), Teal, W, Seed + 3, .2f);
        break;
    case gwgame::Gear::Crane:
        // An origami crane: the body, its raised wing, the neck and tail.
        Polygon({P(-1.f, -.5f), P(-.35f, .25f), P(.35f, .25f), P(1.f, -.62f), P(.5f, .55f), P(-.5f, .55f)}, Tint, Ink, W, Seed);
        Polygon({P(-.25f, .22f), P(.12f, -.95f), P(.32f, .22f)}, Tint * FLinearColor(1.15f, 1.15f, 1.15f, 1), Ink, W, Seed + 1);
        break;
    case gwgame::Gear::Stamp:
        Disc(P(0, -.68f), R * .26f, Kraft);
        Circle(P(0, -.68f), R * .26f, Ink, W, Seed);
        Polygon({P(-.16f, -.46f), P(.16f, -.46f), P(.2f, -.05f), P(-.2f, -.05f)}, Kraft, Ink, W, Seed + 1);
        Polygon({P(-.82f, -.05f), P(.82f, -.05f), P(.82f, .48f), P(-.82f, .48f)}, Tint, Ink, W, Seed + 2);
        Polygon({P(-.72f, .48f), P(.72f, .48f), P(.72f, .7f), P(-.72f, .7f)}, Ink, Ink, W * .6f, Seed + 3);
        Glyph(17, P(0, .2f), Size * .3f, Paper, Seed + 4);
        break;
    case gwgame::Gear::TapeTrap:
        MetaIcon(1, C, Size * .9f, Tint, Seed);
        Pen(P(-.55f, -.05f), P(.55f, -.05f), Ink.CopyWithNewOpacity(.5f), W * .7f, Seed + 7, .5f);
        break;
    case gwgame::Gear::Dryer:
        Disc(P(-.28f, -.22f), R * .5f, Tint);
        Circle(P(-.28f, -.22f), R * .5f, Ink, W, Seed);
        Circle(P(-.28f, -.22f), R * .22f, Ink, W * .8f, Seed + 1);
        Polygon({P(.12f, -.48f), P(.95f, -.38f), P(.95f, .02f), P(.12f, .08f)}, Tint, Ink, W, Seed + 2);
        Polygon({P(-.45f, .18f), P(-.12f, .18f), P(.02f, .95f), P(-.32f, .95f)}, InkSoft, Ink, W, Seed + 3);
        break;
    case gwgame::Gear::Scissors:
        Polygon({P(-.22f, .22f), P(.95f, -.88f), P(.1f, .1f)}, Ruler, Ink, W, Seed);
        Polygon({P(.22f, .22f), P(-.95f, -.88f), P(-.1f, .1f)}, Ruler, Ink, W, Seed + 1);
        Circle(P(-.45f, .58f), R * .3f, Coral, W * 1.6f, Seed + 2);
        Circle(P(.45f, .58f), R * .3f, Coral, W * 1.6f, Seed + 3);
        Disc(P(0, .16f), R * .08f, Ink, 8);
        break;
    case gwgame::Gear::Bandage:
        MetaIcon(3, C, Size, Tint, Seed);
        break;
    case gwgame::Gear::GoldDust:
        Polygon({P(-.55f, -.35f), P(.55f, -.35f), P(.6f, .92f), P(-.6f, .92f)}, Paper, Ink, W, Seed);
        Polygon({P(-.58f, .3f), P(.58f, .3f), P(.6f, .92f), P(-.6f, .92f)}, Marigold, Ink, W * .7f, Seed + 1);
        Polygon({P(-.45f, -.68f), P(.45f, -.68f), P(.45f, -.35f), P(-.45f, -.35f)}, Ink, Ink, W, Seed + 2);
        Glyph(5, P(.55f, -.72f), Size * .42f, Spark, Seed + 3);
        break;
    case gwgame::Gear::CarbonPaper:
        MetaIcon(5, C, Size, Tint, Seed);
        break;
    case gwgame::Gear::Paperclip:
        // A paperclip: three runs of wire and the bends between them.
        Pen(P(-.12f, .4f), P(-.12f, -.55f), Tint, W * 1.7f, Seed, .2f);
        Arc(P(.06f, -.55f), R * .18f, PI, 2 * PI, Tint, W * 1.7f);
        Pen(P(.24f, -.55f), P(.24f, .62f), Tint, W * 1.7f, Seed + 1, .2f);
        Arc(P(-.03f, .62f), R * .27f, 0, PI, Tint, W * 1.7f);
        Pen(P(-.3f, .62f), P(-.3f, -.72f), Tint, W * 1.7f, Seed + 2, .2f);
        Arc(P(.03f, -.72f), R * .33f, PI, 2 * PI, Tint, W * 1.7f);
        Pen(P(.36f, -.72f), P(.36f, .3f), Tint, W * 1.7f, Seed + 3, .2f);
        break;
    case gwgame::Gear::TapeRoll:
        MetaIcon(1, C, Size, Frost, Seed);
        break;
    case gwgame::Gear::Ruler:
    {
        const auto Q = Slant(-35);
        Polygon({Q(-1.f, -.3f), Q(1.f, -.3f), Q(1.f, .3f), Q(-1.f, .3f)}, Ruler, Ink, W, Seed);
        for (int32 K = 0; K < 9; ++K)
            Pen(Q(-.84f + K * .21f, -.3f), Q(-.84f + K * .21f, -.3f + (K % 2 ? .16f : .3f)), Ink, W * .7f, Seed + 1 + K, .1f);
        break;
    }
    case gwgame::Gear::Refill:
    {
        const auto Q = Slant(-40);
        Polygon({Q(-.95f, -.11f), Q(.5f, -.11f), Q(.5f, .11f), Q(-.95f, .11f)}, Paper, Ink, W, Seed);
        Pen(Q(-.9f, 0), Q(.45f, 0), Tint, W * 1.4f, Seed + 1, .1f);
        Polygon({Q(.5f, -.11f), Q(.95f, 0), Q(.5f, .11f)}, Ink, Ink, W * .6f, Seed + 2);
        for (int32 K = 0; K < 5; ++K)
            Pen(Q(-.62f + K * .1f, -.24f), Q(-.57f + K * .1f, .24f), InkSoft, W * .8f, Seed + 3 + K, .1f);
        break;
    }
    case gwgame::Gear::Sharpener:
        Polygon({P(-.72f, -.5f), P(.72f, -.5f), P(.72f, .72f), P(-.72f, .72f)}, Tint, Ink, W, Seed);
        Disc(P(0, .12f), R * .3f, Paper);
        Circle(P(0, .12f), R * .3f, Ink, W, Seed + 1);
        Disc(P(0, .12f), R * .08f, Ink, 8);
        Pen(P(-.55f, -.25f), P(.55f, -.25f), Ruler, W * 1.4f, Seed + 2, .2f);
        break;
    case gwgame::Gear::InkBottle:
        MetaIcon(0, C, Size, Tint, Seed);
        break;
    case gwgame::Gear::KraftLiner:
        Polygon({P(-.75f, -.82f), P(.75f, -.82f), P(.75f, .05f), P(0, .95f), P(-.75f, .05f)}, Tint, Ink, W, Seed);
        Pen(P(0, -.82f), P(0, .9f), Ink.CopyWithNewOpacity(.6f), W * .7f, Seed + 1, .3f);
        Pen(P(-.75f, -.3f), P(.75f, -.3f), Ink.CopyWithNewOpacity(.6f), W * .7f, Seed + 2, .3f);
        break;
    case gwgame::Gear::CuttingMat:
        Polygon({P(-.88f, -.72f), P(.88f, -.72f), P(.88f, .72f), P(-.88f, .72f)}, Tint, Ink, W, Seed);
        for (int32 K = 1; K < 4; ++K)
            Pen(P(-.88f + K * .44f, -.72f), P(-.88f + K * .44f, .72f), Paper.CopyWithNewOpacity(.55f), W * .6f, Seed + K, .1f);
        for (int32 K = 1; K < 3; ++K)
            Pen(P(-.88f, -.72f + K * .48f), P(.88f, -.72f + K * .48f), Paper.CopyWithNewOpacity(.55f), W * .6f, Seed + 5 + K, .1f);
        Pen(P(-.7f, .6f), P(.7f, -.6f), Paper, W, Seed + 9, .2f);
        break;
    case gwgame::Gear::Highlighter:
    {
        Box(C.X - .9f * R, C.Y + .62f * R, 1.8f * R, .26f * R, Tint.CopyWithNewOpacity(.6f));
        const auto Q = Slant(-32);
        Polygon({Q(-.95f, -.3f), Q(.38f, -.3f), Q(.38f, .3f), Q(-.95f, .3f)}, Tint, Ink, W, Seed);
        Polygon({Q(.38f, -.22f), Q(.92f, -.06f), Q(.92f, .2f), Q(.38f, .22f)}, Tint * FLinearColor(.8f, .8f, .55f, 1), Ink, W, Seed + 1);
        Pen(Q(-.6f, -.3f), Q(-.6f, .3f), Ink.CopyWithNewOpacity(.5f), W * .7f, Seed + 2, .2f);
        break;
    }
    case gwgame::Gear::Album:
        Polygon({P(-.8f, -.85f), P(.75f, -.85f), P(.75f, .85f), P(-.8f, .85f)}, Tint, Ink, W, Seed);
        Polygon({P(-.8f, -.85f), P(-.56f, -.85f), P(-.56f, .85f), P(-.8f, .85f)}, Tint * FLinearColor(.7f, .7f, .7f, 1), Ink, W * .8f, Seed + 1);
        Glyph(5, P(.1f, -.05f), Size * .56f, Marigold, Seed + 2);
        break;
    case gwgame::Gear::Phoenix:
        Polygon({P(-.1f, -.2f), P(.38f, -.62f), P(.28f, .1f), P(-.04f, .45f), P(-.45f, .3f)}, Tint, Ink, W, Seed);
        Polygon({P(-.1f, -.1f), P(-.95f, -.78f), P(-.45f, .15f)}, Marigold, Ink, W, Seed + 1);
        Polygon({P(-.04f, .45f), P(-.36f, .95f), P(.1f, .62f), P(.32f, .95f), P(.26f, .25f)}, Danger, Ink, W, Seed + 2);
        Disc(P(.26f, -.44f), R * .07f, Ink, 6);
        break;
    case gwgame::Gear::StandIn:
    default:
    {
        // A paper doll cut out of kraft paper.
        Disc(P(0, -.62f), R * .27f, Tint);
        Circle(P(0, -.62f), R * .27f, Ink, W, Seed);
        Polygon({P(-.25f, -.32f), P(-.9f, -.05f), P(-.85f, .14f), P(-.3f, .02f), P(-.42f, .92f), P(-.14f, .92f), P(0, .42f), P(.14f, .92f),
                 P(.42f, .92f), P(.3f, .02f), P(.85f, .14f), P(.9f, -.05f), P(.25f, -.32f)},
                Tint, Ink, W, Seed + 1);
        break;
    }
    }
}

void AGWHUD::LookIcon(int32 Look, FVector2D C, float Size, FLinearColor Color, int32 Seed)
{
    const int32 Category = gwgame::lookCategory(Look), Index = FMath::Max(1, gwgame::lookIndex(Look));
    const float R = Size * .5f, W = FMath::Max(1.2f, Size * .055f);
    auto P = [&](float X, float Y) { return C + FVector2D(X, Y) * R; };
    switch (Category)
    {
    case 0:
        if (Index < 8)
            MetaIcon(HatIconBase + Index, C, Size, Color, Seed);
        else
        {
            // The expedition's own hat: a folded newspaper boat.
            Polygon({P(-1.f, .12f), P(-.5f, .12f), P(0, -.88f), P(.5f, .12f), P(1.f, .12f), P(.66f, .62f), P(-.66f, .62f)}, Hex(0xE9E2D0), Ink, W, Seed);
            for (int32 L = 0; L < 2; ++L)
                Pen(P(-.12f, -.3f + L * .22f), P(.16f, -.3f + L * .22f), Pencil, W * .7f, Seed + 1 + L, .2f);
        }
        break;
    case 1:
        // Masks: red and blue paper glasses, an eye patch, scribbled-in sunglasses.
        if (Index == 1)
        {
            Polygon({P(-.95f, -.32f), P(-.08f, -.32f), P(-.12f, .3f), P(-.9f, .3f)}, Danger.CopyWithNewOpacity(.85f), Ink, W, Seed);
            Polygon({P(.08f, -.32f), P(.95f, -.32f), P(.9f, .3f), P(.12f, .3f)}, Cobalt.CopyWithNewOpacity(.85f), Ink, W, Seed + 1);
            Pen(P(-.08f, -.1f), P(.08f, -.1f), Ink, W, Seed + 2, .2f);
        }
        else if (Index == 2)
        {
            Pen(P(-.95f, -.55f), P(.95f, .25f), Ink, W * 1.2f, Seed, .3f);
            Disc(P(.3f, .05f), R * .42f, Ink, 16);
        }
        else
        {
            Disc(P(-.45f, 0), R * .4f, Ink, 14);
            Disc(P(.45f, 0), R * .4f, Ink, 14);
            Pen(P(-.1f, -.12f), P(.1f, -.12f), Ink, W * 1.4f, Seed, .2f);
            Pen(P(-.7f, -.15f), P(-.25f, .12f), Paper.CopyWithNewOpacity(.5f), W, Seed + 1, .6f);
        }
        break;
    case 2:
        // Capes: paper-crane wings, a red scarf, notebook-spiral wings.
        if (Index == 1)
        {
            Polygon({P(-.1f, .1f), P(-.98f, -.75f), P(-.6f, .55f)}, Paper, Ink, W, Seed);
            Polygon({P(.1f, .1f), P(.98f, -.75f), P(.6f, .55f)}, Paper, Ink, W, Seed + 1);
            Disc(C, R * .14f, Coral, 8);
        }
        else if (Index == 2)
        {
            Polygon({P(-.8f, -.55f), P(.8f, -.55f), P(.7f, -.2f), P(-.7f, -.2f)}, Danger, Ink, W, Seed);
            Polygon({P(.2f, -.25f), P(.55f, -.25f), P(.75f, .9f), P(.4f, .82f)}, Danger, Ink, W, Seed + 1);
        }
        else
        {
            Polygon({P(-.12f, -.6f), P(-.95f, -.4f), P(-.8f, .7f), P(-.12f, .45f)}, Paper, Ink, W, Seed);
            Polygon({P(.12f, -.6f), P(.95f, -.4f), P(.8f, .7f), P(.12f, .45f)}, Paper, Ink, W, Seed + 1);
            for (int32 K = 0; K < 4; ++K)
                Circle(P(0, -.45f + K * .28f), R * .1f, InkSoft, W * .8f, Seed + 2 + K);
        }
        break;
    case 3:
        // Charms on a string: a small eraser, a paperclip chain, a tiny compass.
        Pen(P(0, -.95f), P(0, -.35f), Pencil, W, Seed, .3f);
        if (Index == 1)
        {
            Polygon({P(-.4f, -.35f), P(.4f, -.35f), P(.4f, .6f), P(-.4f, .6f)}, Paper, Ink, W, Seed + 1);
            Polygon({P(-.42f, -.35f), P(.42f, -.35f), P(.42f, .05f), P(-.42f, .05f)}, Cobalt, Ink, W, Seed + 2);
        }
        else if (Index == 2)
            for (int32 K = 0; K < 3; ++K)
                Circle(P(K % 2 ? .12f : -.12f, -.2f + K * .38f), R * .2f, Frost, W * 1.4f, Seed + 1 + K);
        else
        {
            Disc(P(0, -.3f), R * .12f, Ink, 8);
            Pen(P(0, -.3f), P(-.42f, .85f), Ink, W * 1.3f, Seed + 1, .2f);
            Pen(P(0, -.3f), P(.42f, .85f), Ink, W * 1.3f, Seed + 2, .2f);
            Arc(P(0, -.3f), R * .7f, PI * .38f, PI * .62f, Pencil, W);
        }
        break;
    case 4:
        // Trails: watercolour footprints, a pencil's scratches, the brush's bamboo steps.
        if (Index == 1)
        {
            const FLinearColor Steps[3] = {Coral, Marigold, Teal};
            for (int32 K = 0; K < 3; ++K)
            {
                const FVector2D At = P(-.6f + K * .6f, (K % 2 ? -.35f : .35f));
                Disc(At, R * .24f, Steps[K].CopyWithNewOpacity(.75f), 10);
                Disc(At + FVector2D(0, -.32f) * R, R * .1f, Steps[K].CopyWithNewOpacity(.75f), 8);
            }
        }
        else if (Index == 2)
            for (int32 K = 0; K < 3; ++K)
                Pen(P(-.9f, -.5f + K * .45f), P(.9f, -.62f + K * .45f), Pencil, W * 1.2f, Seed + K, 1.2f);
        else
            for (int32 K = 0; K < 3; ++K)
            {
                Pen(P(-.55f + K * .55f, .9f), P(-.55f + K * .55f, -.9f), Ink, W * 2.2f, Seed + K, .3f);
                Pen(P(-.7f + K * .55f, -.1f), P(-.4f + K * .55f, -.1f), Ink, W, Seed + 4 + K, .2f);
            }
        break;
    default:
        // A title: a ribbon banner.
        Polygon({P(-1.f, -.35f), P(1.f, -.35f), P(.75f, .05f), P(1.f, .45f), P(-1.f, .45f), P(-.75f, .05f)}, Color, Ink, W, Seed);
        Glyph(5, C + FVector2D(0, .05f) * R, Size * .34f, Paper, Seed + 1);
        break;
    }
}

void AGWHUD::GunIcon(int32 Weapon, FVector2D C, float Size, int32 Seed, FLinearColor Accent)
{
    const float R = Size * .5f, W = FMath::Max(1.2f, Size * .05f);
    const float A = FMath::DegreesToRadians(-14.f);
    const FVector2D Along(FMath::Cos(A), FMath::Sin(A)), Across(-Along.Y, Along.X);
    auto Q = [&](float U, float V) { return C + (Along * U + Across * V) * R; };
    auto Bar = [&](float U0, float V0, float U1, float V1, const FLinearColor& Fill, int32 Key)
    { Polygon({Q(U0, V0), Q(U1, V0), Q(U1, V1), Q(U0, V1)}, Fill, Ink, W, Seed + Key); };
    const FLinearColor Steel = Hex(0x3A4150), Body = Accent.A > 0 ? Accent : Hex(0x5B6372);
    switch (FMath::Clamp(Weapon, 0, 5))
    {
    case 0:
        // Pistol: a slide over a short barrel, the grip below.
        Polygon({Q(-.45f, .02f), Q(-.08f, .02f), Q(-.18f, .7f), Q(-.58f, .7f)}, Kraft, Ink, W, Seed);
        Bar(-.55f, -.36f, .62f, .04f, Body, 1);
        Bar(.45f, -.28f, .82f, -.08f, Steel, 2);
        Arc(Q(-.02f, .1f), R * .16f, 0, PI, Ink, W);
        break;
    case 1:
        // Rifle: stock, body, magazine and a long barrel.
        Polygon({Q(-1.f, -.18f), Q(-.45f, -.26f), Q(-.45f, .08f), Q(-.95f, .26f)}, Kraft, Ink, W, Seed);
        Bar(-.47f, -.3f, .35f, .08f, Body, 1);
        Polygon({Q(-.05f, .06f), Q(.17f, .06f), Q(.24f, .52f), Q(.02f, .52f)}, Steel, Ink, W, Seed + 2);
        Bar(.33f, -.2f, 1.f, -.08f, Steel, 3);
        break;
    case 2:
        // Shotgun: two barrels side by side, the pump under them, a kraft stock.
        Polygon({Q(-1.f, -.18f), Q(-.52f, -.28f), Q(-.52f, .08f), Q(-.95f, .3f)}, Kraft, Ink, W, Seed);
        Bar(-.54f, -.32f, -.1f, .08f, Body, 1);
        Bar(-.12f, -.32f, 1.f, -.18f, Steel, 2);
        Bar(-.12f, -.18f, 1.f, -.04f, Steel, 3);
        Bar(.22f, -.04f, .58f, .12f, Kraft, 4);
        break;
    case 3:
        // Sniper: a long thin barrel and a scope on top.
        Polygon({Q(-1.f, -.1f), Q(-.45f, -.18f), Q(-.45f, .1f), Q(-.98f, .24f)}, Kraft, Ink, W, Seed);
        Bar(-.47f, -.2f, .02f, .08f, Body, 1);
        Bar(0, -.13f, 1.05f, -.05f, Steel, 2);
        Bar(-.3f, -.44f, .22f, -.26f, Steel, 3);
        Disc(Q(.22f, -.35f), R * .09f, Frost, 8);
        break;
    case 4:
        // Machine gun: a heavy body, a ribbed barrel and its drum.
        Bar(-.65f, -.3f, .3f, .12f, Body, 1);
        Bar(.28f, -.18f, 1.f, -.04f, Steel, 2);
        for (int32 K = 0; K < 3; ++K)
            Pen(Q(.45f + K * .17f, -.24f), Q(.45f + K * .17f, .02f), Ink, W, Seed + 3 + K, .1f);
        Disc(Q(-.15f, .36f), R * .26f, Steel, 14);
        Circle(Q(-.15f, .36f), R * .26f, Ink, W, Seed + 7);
        break;
    default:
        // Rocket launcher: a fat green tube with its warhead poking out.
        Bar(-.92f, -.22f, .55f, .2f, Leaf, 1);
        Polygon({Q(.55f, -.3f), Q(1.f, -.01f), Q(.55f, .28f)}, Coral, Ink, W, Seed + 2);
        Bar(-.12f, .2f, .1f, .52f, Kraft, 3);
        Pen(Q(-.6f, -.22f), Q(-.6f, .2f), Ink, W, Seed + 4, .1f);
        break;
    }
}

void AGWHUD::SlotIcon(int32 Kind, int32 What, int32 Count, FVector2D C, float Size, int32 Seed, int32 Variant)
{
    if (Kind == ItemSlot)
        return GearIcon(What, C, Size, Seed);
    if (Kind == WeaponSlot && What >= 0 && What <= 5)
    {
        // A quality gun wears its quality's colour, with a spark over it from purple up.
        const int32 Quality = PieceQuality(LootWeapon, What, Variant);
        const FLinearColor Accent = Quality > 0 ? Grade(Quality) : FLinearColor::Transparent;
        // A pair: the second gun behind the first, a little up and to the left.
        if (Count >= 2)
            GunIcon(What, C + FVector2D(-.12f, -.2f) * Size, Size * .9f, Seed + 40, Accent);
        GunIcon(What, C + (Count >= 2 ? FVector2D(.06f, .1f) * Size : FVector2D::ZeroVector), Size, Seed, Accent);
        if (Quality >= 3)
            Glyph(5, C + FVector2D(-.38f, -.36f) * Size, Size * .3f, Grade(Quality), Seed + 50);
        return;
    }
    // Bare hands: a fist.
    const float R = Size * .5f, W = FMath::Max(1.2f, Size * .055f);
    Disc(C, R * .5f, Kraft, 16);
    Circle(C, R * .5f, Ink, W, Seed);
    for (int32 K = 0; K < 3; ++K)
        Pen(C + FVector2D(-.25f + K * .25f, -.48f) * R, C + FVector2D(-.25f + K * .25f, -.12f) * R, Ink, W * .8f, Seed + 1 + K, .2f);
}

void AGWHUD::QualityMark(FVector2D C, float Size, int32 Quality, int32 Seed)
{
    if (Quality <= 0)
        return;
    const float R = Size * .5f;
    Polygon({C + FVector2D(0, -R), C + FVector2D(R * .8f, 0), C + FVector2D(0, R), C + FVector2D(-R * .8f, 0)}, Grade(Quality), Ink,
            FMath::Max(1.f, Size * .08f), Seed);
    Pen(C + FVector2D(-R * .3f, -R * .25f), C + FVector2D(R * .05f, -R * .55f), Paper.CopyWithNewOpacity(.85f), FMath::Max(1.f, Size * .08f), Seed + 1, .1f);
}

void AGWHUD::CoinIcon(FVector2D C, float Size, int32 Seed)
{
    const float R = Size * .5f;
    TArray<FVector2D> Outer, Inner;
    for (int32 K = 0; K < 6; ++K)
    {
        const float A = PI / 6 + K * PI / 3;
        Outer.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
        Inner.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R * .55f);
    }
    Polygon(Outer, CoinBrass, Ink, FMath::Max(1.f, Size * .07f), Seed);
    Polygon(Inner, CoinLight, Ink.CopyWithNewOpacity(.6f), FMath::Max(1.f, Size * .05f), Seed + 1);
}

void AGWHUD::BiomeIcon(int32 PenIndex, FVector2D C, float Size, int32 Seed)
{
    const float R = Size * .5f, W = FMath::Max(1.2f, Size * .055f);
    const FLinearColor Tone = Biome(PenIndex);
    auto P = [&](float X, float Y) { return C + FVector2D(X, Y) * R; };
    const float A = FMath::DegreesToRadians(-45.f);
    const FVector2D Along(FMath::Cos(A), FMath::Sin(A)), Across(-Along.Y, Along.X);
    auto Q = [&](float U, float V) { return C + (Along * U + Across * V) * R; };
    switch (static_cast<gwgame::PenBiome>(FMath::Clamp(PenIndex, 0, gwgame::BiomeCount - 1)))
    {
    case gwgame::PenBiome::Pencil:
        MetaIcon(HatIconBase + 6, C, Size, Tone, Seed);
        break;
    case gwgame::PenBiome::Watercolor:
        // A round brush over a wet wash.
        Disc(P(.25f, .3f), R * .55f, Tone.CopyWithNewOpacity(.45f), 18);
        Disc(P(.05f, .45f), R * .35f, Magenta.CopyWithNewOpacity(.3f), 14);
        Polygon({Q(-.95f, -.09f), Q(.2f, -.09f), Q(.2f, .09f), Q(-.95f, .09f)}, Kraft, Ink, W, Seed);
        Polygon({Q(.2f, -.16f), Q(.55f, -.2f), Q(.85f, 0), Q(.55f, .2f), Q(.2f, .16f)}, Tone, Ink, W, Seed + 1);
        break;
    case gwgame::PenBiome::Fountain:
        // A fountain pen's nib.
        Polygon({P(0, -1.f), P(.52f, .08f), P(.28f, .88f), P(-.28f, .88f), P(-.52f, .08f)}, Tone, Ink, W, Seed);
        Pen(P(0, -.62f), P(0, .25f), Paper, W, Seed + 1, .1f);
        Disc(P(0, .3f), R * .11f, Paper, 8);
        break;
    case gwgame::PenBiome::Ballpoint:
        // A clicky ballpoint.
        Polygon({Q(-.95f, -.17f), Q(.55f, -.17f), Q(.55f, .17f), Q(-.95f, .17f)}, Tone, Ink, W, Seed);
        Polygon({Q(.55f, -.17f), Q(.95f, 0), Q(.55f, .17f)}, Paper, Ink, W, Seed + 1);
        Pen(Q(-.78f, -.27f), Q(-.2f, -.27f), Ink, W * 1.2f, Seed + 2, .1f);
        Polygon({Q(-1.f, -.09f), Q(-.95f, -.09f), Q(-.95f, .09f), Q(-1.f, .09f)}, Ink, Ink, W, Seed + 3);
        break;
    case gwgame::PenBiome::Brush:
    default:
        // A calligraphy brush and its vermilion seal.
        Polygon({Q(-.95f, -.12f), Q(.2f, -.12f), Q(.2f, .12f), Q(-.95f, .12f)}, Kraft, Ink, W, Seed);
        Polygon({Q(.2f, -.22f), Q(.95f, 0), Q(.2f, .22f)}, Ink, Ink, W, Seed + 1);
        Box(C.X + .3f * R, C.Y + .38f * R, .5f * R, .5f * R, Tone);
        break;
    }
}

void AGWHUD::RoomIcon(int32 Kind, int32 Event, FVector2D C, float Size, FLinearColor Color, int32 Seed)
{
    const float R = Size * .5f, W = FMath::Max(1.2f, Size * .07f);
    auto P = [&](float X, float Y) { return C + FVector2D(X, Y) * R; };
    switch (Kind)
    {
    case RoomStart:
        Glyph(8, C, Size, Color, Seed);
        break;
    case RoomCombat:
        Glyph(17, C, Size * .8f, Color, Seed);
        break;
    case RoomPortal:
        Circle(C, R * .85f, Color, W * 1.4f, Seed);
        Circle(C, R * .5f, Color, W * 1.2f, Seed + 1);
        Disc(C, R * .16f, Color, 8);
        break;
    case RoomBoss:
        Glyph(18, C, Size, Color, Seed);
        break;
    case RoomShop:
        // A price tag.
        Polygon({P(-.75f, -.55f), P(.3f, -.55f), P(.85f, 0), P(.3f, .55f), P(-.75f, .55f)}, Color, Ink, W, Seed);
        Disc(P(.32f, 0), R * .12f, Paper, 8);
        break;
    case RoomTreasure:
        // A chest: its box, lid and lock.
        Polygon({P(-.8f, -.1f), P(.8f, -.1f), P(.8f, .7f), P(-.8f, .7f)}, Color, Ink, W, Seed);
        Polygon({P(-.8f, -.1f), P(-.7f, -.55f), P(.7f, -.55f), P(.8f, -.1f)}, Color * FLinearColor(1.2f, 1.2f, 1.2f, 1), Ink, W, Seed + 1);
        Box(C.X - .12f * R, C.Y - .2f * R, .24f * R, .32f * R, Ink);
        break;
    default:
        if (Event == static_cast<int32>(gwgame::RoomEvent::Exit))
        {
            // The early exit: a door with an arrow out of it.
            Polygon({P(-.55f, -.85f), P(.35f, -.85f), P(.35f, .85f), P(-.55f, .85f)}, Paper, Color, W * 1.4f, Seed);
            Pen(P(-.15f, 0), P(.95f, 0), Color, W * 1.6f, Seed + 1, .2f);
            Pen(P(.95f, 0), P(.65f, -.3f), Color, W * 1.6f, Seed + 2, .2f);
            Pen(P(.95f, 0), P(.65f, .3f), Color, W * 1.6f, Seed + 3, .2f);
        }
        else
            Label(FText::FromString(TEXT("?")), C.X, C.Y - Size * .62f, Size * 1.05f, Color, true, 0, EFace::Heavy);
        break;
    }
}

// ---- What E does here ----

bool AGWHUD::ExpeditionReach(AGWGameState* S, const FGWUnitView& U, FReach& Out) const
{
    if (!S || !S->Expedition() || S->Finished || !U.Alive || U.Extracted)
        return false;
    const FVector At = U.Position;
    // 1. A gun, an item or a look on the floor, nearest first (Battle::interact's order).
    const FGWLootView* Best = nullptr;
    float Nearest = static_cast<float>(gwgame::PickupReach) + U.Radius;
    for (const auto& L : S->Loot)
        // A chest's toss still in the air cannot be taken yet.
        if ((L.Kind == LootWeapon || L.Kind == LootItem || L.Kind == LootLook) && L.Hop <= 0)
        {
            const float D = FVector::Dist2D(L.Position, At);
            if (D < Nearest)
            {
                Nearest = D;
                Best = &L;
            }
        }
    if (Best)
    {
        Out.World = Best->Position;
        if (Best->Kind == LootLook)
        {
            const int32 Category = gwgame::lookCategory(Best->What);
            const bool Worn = Category >= 0 && Category < 6 && U.Wear[Category] > 0;
            Out.Text = Worn ? FArgs()(TEXT("Name"), LookName(Best->What))(TEXT("Worn"), LookName(gwgame::lookId(Category, U.Wear[Category]))).Text(TEXT("Expedition.SwapLook"))
                            : FArgs()(TEXT("Name"), LookName(Best->What)).Text(TEXT("Expedition.WearLook"));
            return true;
        }
        // Where it would go: the hands (a second pistol is the dual wield), a stack, the belt or the bag.
        const gwgame::StowPlan Plan = PlanFor(U, Best->Kind, Best->What);
        const FText Name = PieceName(Best->Kind, Best->What, Best->Variant);
        Out.Refused = Plan.place == gwgame::StowPlace::None;
        const TCHAR* Line = Out.Refused                                  ? TEXT("Expedition.Full")
                            : Plan.place == gwgame::StowPlace::Pair  ? TEXT("Expedition.PickPair")
                            : Plan.place == gwgame::StowPlace::Stack ? TEXT("Expedition.PickStack")
                            : Plan.place == gwgame::StowPlace::Bag   ? TEXT("Expedition.PickBag")
                                                                     : TEXT("Expedition.Pick");
        Out.Text = FArgs()(TEXT("Name"), Name).Text(Line);
        return true;
    }
    // 2. A shelf's ware or an event machine.
    const FGWOfferNet* Offer = nullptr;
    Nearest = 1e9f;
    for (const auto& O : S->Offers)
        if (!O.Sold)
        {
            const float D = FVector::Dist2D(Unpack(O.X, O.Y), At);
            if (D < static_cast<float>(IsTrader(O) ? gwgame::TradeReach : gwgame::ShelfReach) + U.Radius && D < Nearest)
            {
                Nearest = D;
                Offer = &O;
            }
        }
    if (Offer && IsTrader(*Offer))
    {
        Out.World = Unpack(Offer->X, Offer->Y);
        Out.Text = GWText::Get(TEXT("Expedition.Trade"));
        Out.Trader = true;
        return true;
    }
    if (Offer)
    {
        Out.World = Unpack(Offer->X, Offer->Y);
        Out.Refused = Purse(U) < Offer->Price;
        const FText Name = WareName(Offer->Kind, Offer->What, Offer->Extra);
        const int32 Price = Offer->Price;
        if (Offer->Kind == Machine)
            Out.Text = Price > 0 ? FArgs()(TEXT("Name"), Name)(TEXT("Price"), Price).Text(TEXT("Expedition.Machine"))
                                 : FArgs()(TEXT("Name"), Name).Text(TEXT("Expedition.MachineFree"));
        else
            Out.Text = FArgs()(TEXT("Name"), Name)(TEXT("Price"), Price).Text(TEXT("Expedition.Buy"));
        return true;
    }
    // 3. A chest (the chained one is held open instead).
    for (const auto& Chest : S->Chests)
        if (!Chest.Opened && Chest.Grade != 2 && FVector::Dist2D(Unpack(Chest.X, Chest.Y), At) < gwgame::ChestReach + U.Radius)
        {
            Out.World = Unpack(Chest.X, Chest.Y);
            Out.Text = FArgs()(TEXT("Quality"), QualityName(Chest.Quality)).Text(TEXT("Expedition.OpenChestQ"));
            return true;
        }
    // 4. Held: an early exit door (EV13) and the chained chest's ring (EV05).
    for (const auto& Exit : S->Exits)
        if (!Exit.Final && FVector::Dist2D(Unpack(Exit.X, Exit.Y), At) < gwgame::ExitRadius)
        {
            Out.World = Unpack(Exit.X, Exit.Y);
            Out.Hold = true;
            Out.Progress = U.Hold;
            Out.Text = FArgs()(TEXT("Count"), U.Pouch).Text(TEXT("Expedition.ExitHold"));
            return true;
        }
    for (const auto& Chest : S->Chests)
        if (!Chest.Opened && Chest.Grade == 2 && FVector::Dist2D(Unpack(Chest.X, Chest.Y), At) < 210)
        {
            Out.World = Unpack(Chest.X, Chest.Y);
            Out.Hold = true;
            Out.Progress = S->Rooms.IsValidIndex(Chest.Room) ? FMath::Clamp(S->Rooms[Chest.Room].Clock, 0.f, 1.f) : 0.f;
            Out.Text = GWText::Get(TEXT("Expedition.ChainHold"));
            return true;
        }
    // 5. A fallen teammate's outline (2026-11-17): E opens the revive sheet on them, paper & pen in the hotbar or not
    // (without them the drawing costs gwgame::KitlessReviveInk times the ink).
    const FGWUnitView* Fallen = nullptr;
    Nearest = GhostReach + U.Radius;
    for (const auto& Other : S->Units)
        if (IsGhost(Other) && Other.Id != U.Id && FVector::Dist2D(Other.Position, At) < Nearest)
        {
            Nearest = FVector::Dist2D(Other.Position, At);
            Fallen = &Other;
        }
    if (Fallen)
    {
        Out.World = Fallen->Position;
        Out.Ghost = Fallen->Id;
        Out.Text = FArgs()(TEXT("Name"), TeammateName(S, *Fallen)).Text(KitSlotOf(U, ReviveKitGear) >= 0 ? TEXT("Revive.Prompt") : TEXT("Revive.PromptNoKit"));
        return true;
    }
    return false;
}

// ---- The screen ----

void AGWHUD::ExpeditionScreen(AGWGameState* S, const FGWUnitView* Local, float Entry)
{
    if (!Local)
        return;
    const bool Furniture = !FGWCinematic::Get().Promo;
    if (Local->Alive && !Local->Extracted)
    {
        {
            const auto Slide = SlideIn(FVector2D(210 * Scale, Height - 140 * Scale), Entry, FVector2D(-160, 40), .2f);
            PouchCard(S, *Local);
        }
        DodgeBar(S, *Local);
        if (Furniture)
        {
            const auto Pop = PopIn(FVector2D(Width * .5f, Height - 110 * Scale), Entry, .35f);
            Hotbar(S, *Local);
        }
        Afflictions(S, *Local);
        ExpeditionPrompts(S, Local);
    }
    else if (!S->Finished)
    {
        GhostCard(S, *Local);
        // Whose view it is, under the floor board (notices take the space over the ghost's card).
        if (!Local->Extracted)
            SpectateTag(S, *Local, 108 * Scale);
    }
    if (Furniture)
    {
        const auto Slide = SlideIn(FVector2D(150 * Scale, 150 * Scale), Entry, FVector2D(-160, 0), .32f);
        ExpeditionTeam(S, *Local);
    }
}

void AGWHUD::Afflictions(AGWGameState* S, const FGWUnitView& U)
{
    (void)S;
    // (The smear's blots over the field are drawn under the cards, in Combat.)
    struct FChip
    {
        uint8 Bit;
        const TCHAR* Key;
        int32 Mark;
        FLinearColor Tone;
    };
    static const FChip Chips[] = {{EGWAfflict::Bleeding, TEXT("Afflict.Bleeding"), 23, Hex(0xC23B2A)}, {EGWAfflict::Circled, TEXT("Afflict.Circled"), 0, Hex(0xE23A33)},
                                  {EGWAfflict::Sealed, TEXT("Afflict.Sealed"), 17, Hex(0xC23B2A)},   {EGWAfflict::Smeared, TEXT("Afflict.Smeared"), 23, Hex(0x1F2633)},
                                  {EGWAfflict::Dragged, TEXT("Afflict.Dragged"), 7, Hex(0x5EC8E8)},  {EGWAfflict::Fogged, TEXT("Afflict.Fogged"), 13, Hex(0x8C909A)},
                                  {EGWAfflict::Streak, TEXT("Afflict.Streak"), 5, Hex(0xF2A93B)},    {EGWAfflict::DryBrush, TEXT("Afflict.DryBrush"), 18, Hex(0x1F2633)}};
    // Chips in a row over the hotbar's left end, newest on the right.
    const float SlotSize = 64 * Scale, Total = 5 * SlotSize + 4 * 10 * Scale;
    float X = Width * .5f - Total * .5f;
    const float Y = Height - SlotSize - 76 * Scale - 46 * Scale;
    int32 ChipSeed = 4950;
    for (const FChip& Chip : Chips)
    {
        if (!(U.Act & Chip.Bit))
            continue;
        const FText Name = GWText::Get(Chip.Key);
        const float ChipW = Measure(Name, 12 * Scale, EFace::Bold).X + 32 * Scale;
        Card(X, Y, ChipW, 24 * Scale, Paper, Chip.Tone, ChipSeed++, .5f);
        Glyph(Chip.Mark, FVector2D(X + 13 * Scale, Y + 12 * Scale), 14 * Scale, Chip.Tone, ChipSeed++);
        Label(Name, X + 24 * Scale, Y + 4 * Scale, 12 * Scale, Ink, false, 0, EFace::Bold);
        X += ChipW + 6 * Scale;
    }
}

void AGWHUD::ExpeditionBoard(AGWGameState* S, const FGWUnitView* Local)
{
    const int32 PenKind = static_cast<int32>(S->Biome());
    const FLinearColor Accent = Biome(PenKind);
    const float CX = Width * .5f, Y = 16 * Scale, H = 76 * Scale;
    const float W = FMath::Clamp(Width - 620 * Scale, 420 * Scale, 640 * Scale), X = CX - W * .5f;
    Card(X, Y, W, H, Paper, Ink, 3601, 1.3f);
    Tape(FVector2D(X + 18 * Scale, Y + 10 * Scale), 60 * Scale, 18 * Scale, -38, Accent);
    // The stage's pen in a badge.
    const FVector2D Badge(X + 46 * Scale, Y + H * .5f);
    Disc(Badge, 29 * Scale, Accent.CopyWithNewOpacity(.16f));
    Circle(Badge, 29 * Scale, Ink, 2 * Scale, 3602);
    BiomeIcon(PenKind, Badge, 38 * Scale, 3603);
    // Stage-floor in big figures, the pen's name under it.
    const FText Code = FText::FromString(FString::Printf(TEXT("%d-%d"), S->ExpStage, S->ExpFloor));
    const float CodeSize = 30 * Scale, CodeX = X + 84 * Scale;
    {
        const FVector2D Spot(CodeX + Measure(Code, CodeSize, EFace::Heavy).X * .5f, Y + 24 * Scale);
        PushMotion(Spot, FVector2D::ZeroVector, 0, Bump(0xE3B0u, static_cast<float>(S->ExpStage * 64 + S->ExpFloor)));
        const FMotionGuard Pulse(this);
        Label(Code, CodeX, Y + 6 * Scale, CodeSize, Ink, false, 0, EFace::Heavy);
    }
    const FText PenName = BiomeName(PenKind);
    Label(PenName, CodeX, Y + 46 * Scale, 13 * Scale, InkSoft, false, 150 * Scale, EFace::Bold);
    // The stage's floors as dots (the boss floor crowned), and the four stages under them in their pens' colours.
    const int32 Floors = gwgame::stageFloors(FMath::Clamp<int32>(S->ExpStage, 1, 4));
    const float DotsX = CodeX + FMath::Max(Measure(Code, CodeSize, EFace::Heavy).X, FMath::Min(150 * Scale, Measure(PenName, 13 * Scale, EFace::Bold).X)) + 22 * Scale;
    for (int32 F = 1; F <= Floors; ++F)
    {
        const FVector2D At(DotsX + (F - 1) * 21 * Scale, Y + 26 * Scale);
        const bool Done = F < S->ExpFloor, Here = F == S->ExpFloor;
        if (F == Floors)
            Glyph(18, At, 18 * Scale, Done || Here ? Danger : Pencil, 3610);
        else if (Done)
            Disc(At, 6 * Scale, Accent);
        else
            Circle(At, 6 * Scale, Here ? Accent : Pencil, 1.6f * Scale, 3611 + F);
        if (Here)
            Circle(At, 11 * Scale, Coral, 1.8f * Scale, 3620);
    }
    for (int32 Stage = 1; Stage <= gwgame::ExpeditionStages; ++Stage)
    {
        const float SX = DotsX + (Stage - 1) * 19 * Scale - 5 * Scale, SY = Y + 46 * Scale;
        const FLinearColor Tone = Biome(static_cast<int32>(S->Biome(Stage)));
        if (Stage < S->ExpStage)
            Box(SX, SY, 12 * Scale, 12 * Scale, Tone);
        else if (Stage == S->ExpStage)
        {
            Box(SX, SY, 12 * Scale, 12 * Scale, Tone.CopyWithNewOpacity(.35f));
            Pen(FVector2D(SX, SY + 14 * Scale), FVector2D(SX + 12 * Scale, SY + 14 * Scale), Coral, 2 * Scale, 3625, .2f);
        }
        else
            Box(SX, SY, 12 * Scale, 12 * Scale, PaperShade);
    }
    // The room under way: what it is, and how its fight stands.
    int32 Room = Local ? S->RoomAt(Local->Position) : -1;
    if (Room < 0)
        Room = S->ExpCurrent;
    if (S->Rooms.IsValidIndex(Room))
    {
        const FGWRoomNet& R = S->Rooms[Room];
        FText Status;
        FLinearColor Tone = Ink;
        const bool Fights = R.Kind == RoomCombat || R.Kind == RoomPortal || R.Kind == RoomBoss;
        if (Fights && R.State == StateArmed)
        {
            Status = FArgs()(TEXT("Seconds"), FMath::Max(1, FMath::CeilToInt(R.Clock))).Text(TEXT("Expedition.Arming"));
            Tone = Danger;
        }
        else if (Fights && R.State == StateLocked)
        {
            Status = FArgs()(TEXT("Wave"), FMath::Max(1, static_cast<int32>(R.Wave)))(TEXT("Waves"), FMath::Max(1, static_cast<int32>(R.Waves))).Text(TEXT("Expedition.Wave"));
            Tone = Coral;
        }
        else if (Fights && R.State == StateCleared)
        {
            Status = R.Kind == RoomPortal && S->PortalOpen ? GWText::Get(TEXT("Expedition.PortalOpen")) : GWText::Get(TEXT("Expedition.Cleared"));
            Tone = Teal;
        }
        else
            Status = RoomName(R);
        const float Size = 15 * Scale, ChipW = FMath::Min(Measure(Status, Size, EFace::Bold).X + 52 * Scale, W * .36f), ChipH = 44 * Scale;
        const float ChipX = X + W - ChipW - 14 * Scale, ChipY = Y + (H - ChipH) * .5f;
        if (DotsX + Floors * 21 * Scale < ChipX)
        {
            const float Pulse = R.State == StateArmed ? Bump(HashCombine(0xA4D0u, static_cast<uint32>(Room)), FMath::CeilToFloat(R.Clock)) : 1.f;
            PushMotion(FVector2D(ChipX + ChipW * .5f, ChipY + ChipH * .5f), FVector2D::ZeroVector, 0, Pulse);
            const FMotionGuard Grow(this);
            Card(ChipX, ChipY, ChipW, ChipH, Tone == Ink ? PaperShade : Tone.CopyWithNewOpacity(.14f), Tone == Ink ? Ink : Tone, 3630, .8f);
            if (Fights && R.State == StateCleared && !(R.Kind == RoomPortal && S->PortalOpen))
                Glyph(16, FVector2D(ChipX + 20 * Scale, ChipY + ChipH * .5f), 20 * Scale, Tone, 3631);
            else
                RoomIcon(R.Kind, R.Event == 255 ? -1 : R.Event, FVector2D(ChipX + 20 * Scale, ChipY + ChipH * .5f), 20 * Scale, Tone == Ink ? InkSoft : Tone, 3631);
            Label(Status, ChipX + 36 * Scale, ChipY + (ChipH - Size * 1.3f) * .5f, Size, Ink, false, ChipW - 44 * Scale, EFace::Bold);
        }
    }
    // The boss's bar under the board while it stands.
    if (S->BossActive && !S->Finished)
    {
        const float BW = FMath::Min(Width * .44f, 640 * Scale), BH = 48 * Scale, BX = CX - BW * .5f, BY = Y + H + 12 * Scale;
        Card(BX, BY, BW, BH, Paper, Ink, 3640, 1);
        Glyph(18, FVector2D(BX + 34 * Scale, BY + BH * .5f), 30 * Scale, Accent, 3641);
        const FGWUnitView* Boss = S->ExpBoss >= 0 ? S->FindUnit(S->ExpBoss) : nullptr;
        const bool Dragon = Boss && (Boss->Parts & gwgame::PartsSecondForm);
        float NameW = BW * .6f;
        // Its tools while it has them: a gold chip with the eraser for each the eraser can still take, struck through once gone.
        if (Boss && Boss->Breed >= 0 && Boss->Breed < gwgame::FoeCount && gwgame::foeInfo(Boss->Breed).parts && !Dragon)
        {
            float TX = BX + BW - 14 * Scale;
            const uint8 Tools = gwgame::foeInfo(Boss->Breed).parts;
            for (int32 Bit = 1; Bit >= 0; --Bit)
            {
                if (!((Tools >> Bit) & 1))
                    continue;
                const bool Has = ((Boss->Parts >> Bit) & 1) != 0;
                const FText Part = GWText::Get(FString::Printf(TEXT("Part.%d.%d"), Boss->Breed, Bit));
                const float PW = Measure(Part, 11 * Scale, EFace::Bold).X + 30 * Scale;
                TX -= PW;
                Card(TX, BY + 4 * Scale, PW, 20 * Scale, Has ? Hex(0xFFF2B0) : PaperShade, Has ? Marigold : Pencil, 3650 + Bit, .5f);
                GearIcon(static_cast<int32>(gwgame::Gear::Eraser), FVector2D(TX + 12 * Scale, BY + 14 * Scale), 14 * Scale, 3652 + Bit);
                Label(Part, TX + 22 * Scale, BY + 6 * Scale, 11 * Scale, Has ? Ink : Pencil, false, 0, EFace::Bold);
                if (!Has)
                    Line(FVector2D(TX + 4 * Scale, BY + 14 * Scale), FVector2D(TX + PW - 4 * Scale, BY + 14 * Scale), Danger, 2 * Scale);
                TX -= 6 * Scale;
            }
            NameW = FMath::Max(80 * Scale, TX - (BX + 62 * Scale) - 8 * Scale);
        }
        Label(GWText::Get(Dragon ? FString(TEXT("Boss.Dragon")) : FString::Printf(TEXT("Boss.%d"), PenKind)), BX + 62 * Scale, BY + 6 * Scale, 15 * Scale, Ink, false, NameW,
              EFace::Bold);
        Meter(BX + 62 * Scale, BY + 27 * Scale, BW - 82 * Scale, 13 * Scale, S->BossHP, Danger, 3642, false);
    }
    // The floor's prank once a ballot chose it (EV04), or the open ballot's count while it runs.
    const float ChipY = Y + H + 12 * Scale + (S->BossActive && !S->Finished ? 58 * Scale : 0);
    if (S->ExpPrank >= 0 && !S->Finished)
    {
        const FText Text = FArgs()(TEXT("Name"), PrankName(S->ExpPrank)).Text(TEXT("Expedition.PrankChip"));
        const float TW = Measure(Text, 13 * Scale, EFace::Bold).X + 44 * Scale;
        Card(CX - TW * .5f, ChipY, TW, 26 * Scale, Hex(0xF7D6E6), Magenta, 3660, .6f);
        Glyph(17, FVector2D(CX - TW * .5f + 16 * Scale, ChipY + 13 * Scale), 16 * Scale, Magenta, 3661);
        Label(Text, CX - TW * .5f + 30 * Scale, ChipY + 5 * Scale, 13 * Scale, Ink, false, 0, EFace::Bold);
    }
    else if (S->ExpVoteClock > 0 && !S->Finished)
    {
        // Each prank with a dot per vote in the voter's colour, and the seconds left.
        int32 Tally[gwgame::FloorPrankCount] = {};
        TArray<int32> Seats[gwgame::FloorPrankCount];
        for (const auto& U : S->Units)
            if (U.Human && U.Alive && U.Vote >= 0 && U.Vote < gwgame::FloorPrankCount)
            {
                ++Tally[U.Vote];
                Seats[U.Vote].Add(U.Seat);
            }
        const float BW = FMath::Min(Width * .5f, 660 * Scale), BH = 84 * Scale, BX = CX - BW * .5f;
        Card(BX, ChipY, BW, BH, Paper, Magenta, 3662, 1);
        Label(FArgs()(TEXT("Seconds"), FMath::CeilToInt(S->ExpVoteClock)).Text(TEXT("Expedition.Ballot")), CX, ChipY + 4 * Scale, 13 * Scale, Magenta, true, BW - 20 * Scale,
              EFace::Bold);
        const float ColW = (BW - 20 * Scale) / gwgame::FloorPrankCount;
        for (int32 K = 0; K < gwgame::FloorPrankCount; ++K)
        {
            const float KX = BX + 10 * Scale + K * ColW;
            Label(PrankName(K), KX + ColW * .5f, ChipY + 24 * Scale, 13 * Scale, Ink, true, ColW - 8 * Scale, EFace::Bold);
            Label(GWText::Get(FString::Printf(TEXT("Prank.%d.Note"), K)), KX + ColW * .5f, ChipY + 42 * Scale, 10 * Scale, InkSoft, true, ColW - 8 * Scale);
            for (int32 V = 0; V < Seats[K].Num(); ++V)
                Glyph(0, FVector2D(KX + ColW * .5f + (V - (Seats[K].Num() - 1) * .5f) * 14 * Scale, ChipY + 70 * Scale), 11 * Scale, Seat(Seats[K][V]), 3670 + K * 8 + V);
        }
    }
}

void AGWHUD::Minimap(AGWGameState* S, const FGWUnitView* Local)
{
    const int32 Count = S->Rooms.Num();
    if (Count == 0 || S->CellSize <= 0)
        return;
    // The party knows the rooms it has walked into and the ones their corridors lead to.
    TArray<int32> SlotX, SlotY;
    TArray<bool> Seen;
    for (const auto& R : S->Rooms)
    {
        SlotX.Add((R.X - 1) / gwgame::DungeonPitch);
        SlotY.Add((R.Y - 1) / gwgame::DungeonPitch);
        Seen.Add(R.Explored);
    }
    auto Find = [&](int32 X, int32 Y)
    {
        for (int32 I = 0; I < Count; ++I)
            if (SlotX[I] == X && SlotY[I] == Y)
                return I;
        return -1;
    };
    auto Neighbour = [&](int32 I, int32 Side) { return Find(SlotX[I] + gwgame::detail::StepX[Side], SlotY[I] + gwgame::detail::StepY[Side]); };
    if (S->Rooms.IsValidIndex(S->ExpCurrent))
        Seen[S->ExpCurrent] = true;
    for (int32 I = 0; I < Count; ++I)
        if (S->Rooms[I].Explored)
            for (int32 Side = 0; Side < 4; ++Side)
                if (S->Rooms[I].Links & (1 << Side))
                    if (const int32 N = Neighbour(I, Side); N >= 0)
                        Seen[N] = true;
    int32 MinX = 99, MinY = 99, MaxX = -1, MaxY = -1;
    for (int32 I = 0; I < Count; ++I)
        if (Seen[I])
        {
            MinX = FMath::Min(MinX, SlotX[I]);
            MinY = FMath::Min(MinY, SlotY[I]);
            MaxX = FMath::Max(MaxX, SlotX[I]);
            MaxY = FMath::Max(MaxY, SlotY[I]);
        }
    if (MaxX < 0)
        return;
    const float MapW = 232 * Scale, MapH = 196 * Scale, MX = Width - MapW - 22 * Scale, MY = 16 * Scale;
    Card(MX, MY, MapW, MapH, Paper.CopyWithNewOpacity(.96f), Ink, 3650, 1.2f);
    Tape(FVector2D(MX + MapW - 26 * Scale, MY + 8 * Scale), 58 * Scale, 16 * Scale, 34, Biome(static_cast<int32>(S->Biome())));
    // Faint squares of graph paper under the map.
    for (float GX = MX + 14 * Scale; GX < MX + MapW - 8 * Scale; GX += 18 * Scale)
        Line(FVector2D(GX, MY + 8 * Scale), FVector2D(GX, MY + MapH - 8 * Scale), Grid.CopyWithNewOpacity(.18f), 1);
    for (float GY = MY + 14 * Scale; GY < MY + MapH - 8 * Scale; GY += 18 * Scale)
        Line(FVector2D(MX + 8 * Scale, GY), FVector2D(MX + MapW - 8 * Scale, GY), Grid.CopyWithNewOpacity(.18f), 1);
    const int32 Cols = MaxX - MinX + 1, Rows = MaxY - MinY + 1;
    const float Pad = 18 * Scale, Pitch = FMath::Min(52 * Scale, FMath::Min((MapW - 2 * Pad) / Cols, (MapH - 2 * Pad) / Rows));
    const FVector2D Origin(MX + MapW * .5f - Cols * Pitch * .5f, MY + MapH * .5f - Rows * Pitch * .5f);
    // Cells (floor units / CellSize) onto the map: each slot is DungeonPitch cells, offset by the grid's border.
    auto CellToMap = [&](float CellX, float CellY)
    { return Origin + FVector2D(((CellX - 1) / gwgame::DungeonPitch - MinX) * Pitch, ((CellY - 1) / gwgame::DungeonPitch - MinY) * Pitch); };
    auto WorldToMap = [&](FVector World) { return CellToMap(World.X / S->CellSize, World.Y / S->CellSize); };
    auto Middle = [&](int32 I)
    {
        const FGWRoomNet& R = S->Rooms[I];
        return CellToMap(R.X + R.W * .5f, R.Y + R.H * .5f);
    };
    // Corridors first, rooms over them.
    for (int32 I = 0; I < Count; ++I)
        if (Seen[I])
            for (int32 Side = 0; Side < 4; ++Side)
                if (S->Rooms[I].Links & (1 << Side))
                {
                    const int32 N = Neighbour(I, Side);
                    if (N > I && Seen[N])
                        Pen(Middle(I), Middle(N), InkSoft, 3.f * Scale, 3655 + I * 4 + Side, .3f);
                    else if (N >= 0 && !Seen[N])
                        Pen(Middle(I), Middle(I) + FVector2D(gwgame::detail::StepX[Side], gwgame::detail::StepY[Side]) * Pitch * .45f, Pencil, 2 * Scale,
                            3656 + I * 4 + Side, .3f);
                }
    int32 Here = Local ? S->RoomAt(Local->Position) : -1;
    if (Here < 0)
        Here = S->ExpCurrent;
    for (int32 I = 0; I < Count; ++I)
    {
        if (!Seen[I])
            continue;
        const FGWRoomNet& R = S->Rooms[I];
        const FVector2D A = CellToMap(R.X, R.Y), B = CellToMap(R.X + R.W, R.Y + R.H), Mid = (A + B) * .5f;
        FLinearColor Face = R.Explored ? Paper : PaperShade.CopyWithNewOpacity(.7f);
        if (R.State == StateLocked)
            Face = Hex(0xFBD3CB);
        if (I == Here)
            Face = Hex(0xFFE9A8);
        Polygon({A, FVector2D(B.X, A.Y), B, FVector2D(A.X, B.Y)}, Face, R.Explored ? Ink : Pencil, (I == Here ? 2.4f : 1.5f) * Scale, 3700 + I);
        const float Icon = FMath::Min(B.X - A.X, B.Y - A.Y) * .62f;
        if (!R.Explored)
            Label(FText::FromString(TEXT("?")), Mid.X, Mid.Y - Icon * .6f, Icon, Pencil, true, 0, EFace::Heavy);
        else if (R.Kind == RoomCombat)
        {
            if (R.State == StateCleared)
                Glyph(16, Mid, Icon * .8f, Teal, 3720 + I);
            else if (R.State == StateLocked || R.State == StateArmed)
                Glyph(17, Mid, Icon * .7f, Danger, 3720 + I);
        }
        else
            RoomIcon(R.Kind, R.Event == 255 ? -1 : R.Event, Mid, Icon,
                     R.Kind == RoomBoss ? Danger : R.Kind == RoomPortal ? Cobalt : R.Kind == RoomShop ? Marigold : R.Kind == RoomTreasure ? Kraft : InkSoft, 3730 + I);
    }
    // The portal and the exits, then everyone on the floor: seat-coloured dots, hollow for ghosts.
    if (S->PortalOpen)
        Circle(WorldToMap(S->PortalAt), (4.f + 2.f * FMath::Sin(static_cast<float>(Clock) * 5)) * Scale, Cobalt, 2 * Scale, 3760);
    // Exit doors in the rooms the party has walked into.
    for (const auto& Exit : S->Exits)
        if (S->Rooms.IsValidIndex(Exit.Room) && S->Rooms[Exit.Room].Explored)
            RoomIcon(RoomEventKind, static_cast<int32>(gwgame::RoomEvent::Exit), WorldToMap(Unpack(Exit.X, Exit.Y)), 14 * Scale, Exit.Final ? Teal : Leaf, 3761);
    for (const auto& U : S->Units)
    {
        if (!U.Human || U.Extracted || (!U.Alive && !U.Ghost))
            continue;
        const FVector2D Dot = WorldToMap(U.Position);
        const bool Me = Local && U.Id == Local->Id;
        if (U.Alive)
        {
            Disc(Dot, (Me ? 5.f : 4.f) * Scale, Seat(U.Seat), 10);
            Circle(Dot, (Me ? 5.f : 4.f) * Scale, Ink, 1.2f * Scale, 3770 + U.Id % 50);
        }
        else
            Circle(Dot, 4 * Scale, Seat(U.Seat), 1.6f * Scale, 3780 + U.Id % 50);
        if (Me)
            Circle(Dot, (8.f + 2.f * FMath::Sin(static_cast<float>(Clock) * 4)) * Scale, Seat(U.Seat).CopyWithNewOpacity(.6f), 1.4f * Scale, 3790);
    }
}

void AGWHUD::Hotbar(AGWGameState* S, const FGWUnitView& U)
{
    auto* C = Cast<AGWController>(PlayerOwner);
    // Two groups (2026-11-17): the hands (slots 1-2, guns only; a long gun spans both) and the belt (slots 3-5, items).
    const float SlotSize = 64 * Scale, Gap = 10 * Scale, GroupGap = 28 * Scale, Total = 5 * SlotSize + 3 * Gap + GroupGap;
    const float X0 = Width * .5f - Total * .5f, Y0 = Height - SlotSize - 76 * Scale;
    auto SlotX = [&](int32 I) { return X0 + I * (SlotSize + Gap) + (I >= gwgame::HandSlots ? GroupGap - Gap : 0.f); };
    // The slot asked for shows at once; the host's answer follows a moment later. Both hand keys hold the guns.
    int32 Held = C && C->AskedSlot >= 0 && GetWorld()->GetTimeSeconds() - C->AskedAt < .35 ? C->AskedSlot : FMath::Clamp(U.Selected, 0, 4);
    if (Held < gwgame::HandSlots)
        Held = 0;
    const bool HandsUp = Held == 0;
    const bool Long = IsLongGun(U.Hotbar[0]);
    auto DrawCard = [&](int32 I, float SX, float SW, bool On)
    {
        const auto& Item = U.Hotbar[I];
        const float Lift = Eased(HashCombine(0x407Bu, static_cast<uint32>(I < gwgame::HandSlots ? 0 : I)), On ? 1.f : 0.f, 16);
        const float SY = Y0 - Lift * 10 * Scale;
        const bool Hover = Hovered(SX, SY, SW, SlotSize);
        const int32 Quality = SlotQuality(Item);
        Card(SX, SY, SW, SlotSize, Item.Kind ? Paper : PaperShade.CopyWithNewOpacity(.85f), On ? Seat(U.Seat) : Quality > 0 ? Grade(Quality) : Ink, 3800 + I,
             1.f + Lift + (Hover ? .4f : 0.f), (On ? 3.2f : Quality > 0 ? 2.4f : 1.6f) * Scale);
        const FVector2D Center(SX + SW * .5f, SY + SlotSize * .5f);
        if (Quality > 0)
            Box(SX + 5 * Scale, SY + SlotSize - 7 * Scale, SW - 10 * Scale, 3 * Scale, Grade(Quality).CopyWithNewOpacity(.75f));
        if (Item.Kind)
        {
            PushMotion(Center, FVector2D::ZeroVector, 0, Bump(HashCombine(0x407Cu, static_cast<uint32>(I)), static_cast<float>(Item.What * 16 + Item.Count)));
            const FMotionGuard Pulse(this);
            SlotIcon(Item.Kind, Item.What, Item.Count, Center, SlotSize * (SW > SlotSize ? .78f : .64f), 3810 + I * 7, Item.Variant);
        }
        QualityMark(FVector2D(SX + 9 * Scale, SY + SlotSize - 12 * Scale), 13 * Scale, Quality, 3880 + I);
        // A stack of one-time items.
        if (Item.Kind == ItemSlot && Item.Count > 1)
        {
            const FText Badge = FText::FromString(FString::Printf(TEXT("×%d"), Item.Count));
            const float BW = Measure(Badge, 13 * Scale, EFace::Heavy).X + 10 * Scale;
            Card(SX + SW - BW - 2 * Scale, SY + SlotSize - 20 * Scale, BW, 18 * Scale, Marigold, Ink, 3850 + I, .5f);
            Label(Badge, SX + SW - BW * .5f - 2 * Scale, SY + SlotSize - 20 * Scale, 13 * Scale, Ink, true, 0, EFace::Heavy);
        }
        if (Item.Kind == WeaponSlot && U.Nibs[I] >= 0)
            Polygon({FVector2D(SX + SW - 4 * Scale, SY + 4 * Scale), FVector2D(SX + SW - 4 * Scale, SY + 22 * Scale), FVector2D(SX + SW - 22 * Scale, SY + 4 * Scale)},
                    Biome(U.Nibs[I]), Ink, 1.4f * Scale, 3860 + I);
        AddHit(FBox2D(FVector2D(SX, SY), FVector2D(SX + SW, SY + SlotSize)), GWDefenseAction::HotbarPick + I);
    };
    // The hands: a long gun across both, else a hand each (bare hands show a fist while they are up).
    if (Long)
        DrawCard(0, SlotX(0), 2 * SlotSize + Gap, HandsUp);
    else
        for (int32 I = 0; I < gwgame::HandSlots; ++I)
            DrawCard(I, SlotX(I), SlotSize, HandsUp);
    if (HandsUp && U.Hotbar[0].Kind == 0 && U.Hotbar[1].Kind == 0)
        SlotIcon(0, gwgame::FistWeapon, 1, FVector2D(SlotX(0) + SlotSize * .5f, Y0 - 10 * Scale + SlotSize * .5f), SlotSize * .5f, 3810);
    for (int32 I = gwgame::HandSlots; I < 5; ++I)
        DrawCard(I, SlotX(I), SlotSize, I == Held);
    for (int32 I = 0; I < 5; ++I)
        if (!(Long && I == 1))
            Keycap(FText::AsNumber(I + 1), SlotX(I) - 7 * Scale, Y0 - (I < gwgame::HandSlots ? (HandsUp ? 10 : 0) : I == Held ? 10 : 0) * Scale - 9 * Scale, 22 * Scale,
                   (I < gwgame::HandSlots ? HandsUp : I == Held) ? Ink : InkSoft);
    // The guns in hand: their magazine (or their reload) under the hands.
    if (HandsUp && (U.Hotbar[0].Kind == WeaponSlot || U.Hotbar[1].Kind == WeaponSlot))
    {
        const float BarX = SlotX(0) + 4 * Scale, BarW = 2 * SlotSize + Gap - 8 * Scale, BarY = Y0 + SlotSize + 5 * Scale - 10 * Scale;
        if (U.Reload > 0)
        {
            float Full = .05f;
            for (int32 I = 0; I < gwgame::HandSlots; ++I)
                if (U.Hotbar[I].Kind == WeaponSlot)
                    Full = FMath::Max(Full, static_cast<float>(gwgame::gunStats(U.Hotbar[I].What, U.Hotbar[I].Variant).reload));
            Meter(BarX, BarY, BarW, 7 * Scale, 1 - U.Reload / (Full * (U.Dual ? static_cast<float>(gwgame::DualReload) : 1.f)), Marigold, 3860, false);
        }
        else
            Meter(BarX, BarY, BarW, 7 * Scale, U.Clip > 0 ? static_cast<float>(U.Ammo) / U.Clip : 0, Ink, 3861, false);
    }
    // What is in hand, and how it is used.
    const auto& Hand = HandsUp ? (U.Hotbar[0].Kind ? U.Hotbar[0] : U.Hotbar[1]) : U.Hotbar[Held];
    FText Name = HandsUp ? HandsName(U) : SlotName(Hand.Kind, Hand.What, Hand.Count, Hand.Variant), Detail;
    if (Hand.Kind == WeaponSlot)
        Detail = U.Reload > 0 ? GWText::Get(TEXT("Combat.Reload")) : FText::FromString(FString::Printf(TEXT("%d / %d"), U.Ammo, FMath::Max(U.Clip, 1)));
    else if (Hand.Kind == ItemSlot)
    {
        const gwgame::GearInfo& Info = gwgame::gearInfo(Hand.What);
        Detail = Info.use == gwgame::GearUse::Active ? FArgs()(TEXT("Seconds"), FMath::RoundToInt(Info.cooldown)).Text(TEXT("GearUse.2"))
                                                     : GWText::Get(FString::Printf(TEXT("GearUse.%d"), static_cast<int32>(Info.use)));
        if (Hand.What == ReviveKitGear)
            Detail = GWText::Get(TEXT("GearUse.Revive"));
    }
    else
        Detail = GWText::Get(TEXT("GearUse.Fists"));
    const float NameSize = 17 * Scale, DetailSize = 13 * Scale;
    const float NameW = Measure(Name, NameSize, EFace::Heavy).X, DetailW = Measure(Detail, DetailSize, EFace::Bold).X;
    const float LineW = NameW + DetailW + 18 * Scale, LineY = Y0 - 40 * Scale;
    int32 HandQuality = SlotQuality(Hand);
    if (HandsUp)
        HandQuality = FMath::Max(SlotQuality(U.Hotbar[0]), SlotQuality(U.Hotbar[1]));
    Highlight(Width * .5f - LineW * .5f - 8 * Scale, LineY + 4 * Scale, LineW + 16 * Scale, 22 * Scale,
              (HandQuality > 0 ? Grade(HandQuality) : Seat(U.Seat)).CopyWithNewOpacity(HandQuality > 0 ? .35f : .28f), 3870);
    Label(Name, Width * .5f - LineW * .5f, LineY, NameSize, Ink, false, 0, EFace::Heavy);
    Label(Detail, Width * .5f - LineW * .5f + NameW + 18 * Scale, LineY + 3 * Scale, DetailSize, InkSoft, false, 0, EFace::Bold);
    // G and Tab beside the bar.
    const float KeysX = X0 + Total + 22 * Scale;
    Keycap(FText::FromString(TEXT("G")), KeysX, Y0 + 6 * Scale, 24 * Scale, Ink);
    Key(TEXT("Key.Drop"), KeysX + 32 * Scale, Y0 + 9 * Scale, 13 * Scale, InkSoft, false, 120 * Scale);
    Keycap(FText::FromString(TEXT("Tab")), KeysX, Y0 + 36 * Scale, 24 * Scale, Ink);
    Key(TEXT("Key.Kit"), KeysX + 52 * Scale, Y0 + 39 * Scale, 13 * Scale, InkSoft, false, 110 * Scale);
    // Pouring ink into wounds (R): how many share it (everyone hurt in the circle, this player too).
    if (U.Mending >= 0)
    {
        int32 Sharing = 0;
        const FGWUnitView* Other = nullptr;
        for (const auto& V : S->Units)
            if (V.Alive && V.Human && V.Team == U.Team && V.Deck == U.Deck && !V.Ghost && !V.Extracted && V.HP < V.MaxHP - .5f &&
                FVector::Dist2D(V.Position, U.Position) <= gwgame::MendReach + V.Radius)
            {
                ++Sharing;
                if (V.Id != U.Id)
                    Other = &V;
            }
        const FText Line = Sharing >= 2 ? FArgs()(TEXT("Count"), Sharing).Text(TEXT("Mend.Area"))
                           : Other      ? FArgs()(TEXT("Name"), TeammateName(S, *Other)).Text(TEXT("Mend.Mate"))
                                        : GWText::Get(TEXT("Mend.Self"));
        const float MW = Measure(Line, 14 * Scale, EFace::Bold).X + 44 * Scale, MY = LineY - 30 * Scale;
        Tape(FVector2D(Width * .5f, MY + 10 * Scale), MW, 26 * Scale, 1, Teal.CopyWithNewOpacity(.8f));
        Glyph(23, FVector2D(Width * .5f - MW * .5f + 16 * Scale, MY + 10 * Scale), 14 * Scale, Ink, 3890);
        Label(Line, Width * .5f + 6 * Scale, MY + 1 * Scale, 14 * Scale, Ink, true, 0, EFace::Bold);
    }
    // A ghost: say which slot's paper & pen draws them back, or (with none, 2026-11-17) that E at the outline does.
    const int32 KitSlot = KitSlotOf(U, ReviveKitGear);
    const FGWUnitView* Fallen = nullptr;
    for (const auto& Other : S->Units)
        if (IsGhost(Other) && Other.Id != U.Id)
            Fallen = &Other;
    const bool InBag = KitSlot < 0 && KitIndexOf(U, ReviveKitGear) >= 5;
    const bool NoKit = KitSlot < 0 && !InBag;
    if ((KitSlot >= 0 && Fallen && Held != KitSlot) || (InBag && Fallen) || (NoKit && Fallen))
    {
        const FText Hint = NoKit   ? FArgs()(TEXT("Name"), TeammateName(S, *Fallen)).Text(TEXT("Revive.Hint.NoKit"))
                           : InBag ? FArgs()(TEXT("Name"), TeammateName(S, *Fallen)).Text(TEXT("Revive.Hint.Bag"))
                                   : FArgs()(TEXT("Name"), TeammateName(S, *Fallen))(TEXT("Slot"), KitSlot + 1).Text(TEXT("Revive.Hint.Slot"));
        const float HW = Measure(Hint, 14 * Scale, EFace::Bold).X + 40 * Scale;
        Tape(FVector2D(Width * .5f, LineY - 24 * Scale - (U.Mending >= 0 ? 34 * Scale : 0.f)), HW, 28 * Scale, -1, Coral.CopyWithNewOpacity(.85f));
        Label(Hint, Width * .5f, LineY - 34 * Scale - (U.Mending >= 0 ? 34 * Scale : 0.f), 14 * Scale, Ink, true, 0, EFace::Bold);
    }
}

void AGWHUD::PouchCard(AGWGameState* S, const FGWUnitView& U)
{
    const float W = 372 * Scale, H = 150 * Scale, X = 24 * Scale, Y = Height - H - 70 * Scale;
    const FLinearColor Tone = U.Seat >= 0 ? Seat(U.Seat) : Coral;
    Card(X, Y, W, H, Paper, Ink, 3900, 1.4f);
    Tape(FVector2D(X + 10 * Scale, Y + 8 * Scale), 60 * Scale, 18 * Scale, -40, Tone);
    // The body as drawn (a revive's shape keeps its glyph), its pen trait and its shape's bonus.
    const FVector2D Face(X + 50 * Scale, Y + 52 * Scale);
    Disc(Face, 34 * Scale, Tone.CopyWithNewOpacity(.16f));
    Circle(Face, 34 * Scale, Ink, 2 * Scale, 3901);
    Glyph(BodyGlyph(U.Shape), Face, 40 * Scale, Tone, 3902);
    if (U.Trait >= 0)
    {
        const FVector2D Spot = Face + FVector2D(26, 26) * Scale;
        Disc(Spot, 13 * Scale, Paper);
        Circle(Spot, 13 * Scale, Biome(U.Trait), 2 * Scale, 3903);
        BiomeIcon(U.Trait, Spot, 18 * Scale, 3904);
    }
    if (U.Bonus > 0)
    {
        const int32 Shapes[] = {0, 0, 3, 2};
        const FVector2D Spot = Face + FVector2D(-26, 26) * Scale;
        Disc(Spot, 12 * Scale, Paper);
        Glyph(Shapes[FMath::Clamp(U.Bonus, 0, 3)], Spot, 15 * Scale, Marigold, 3905);
    }
    if (U.Note)
    {
        // A sticky note on the card: the next big hit is eaten.
        const FVector2D Spot = Face + FVector2D(26, -28) * Scale;
        GearIcon(static_cast<int32>(gwgame::Gear::StickyNote), Spot, 22 * Scale, 3906);
    }
    const float BarX = X + 98 * Scale, BarW = W - 116 * Scale;
    Glyph(7, FVector2D(BarX + 8 * Scale, Y + 19 * Scale), 15 * Scale, U.HP < U.MaxHP * .3f ? Danger : Tone, 3907);
    const FText Health = FText::FromString(FString::Printf(TEXT("%.0f / %.0f"), U.HP, U.MaxHP));
    {
        const float HealthW = Measure(Health, 15 * Scale, EFace::Bold).X;
        const FVector2D Spot(BarX + BarW - HealthW * .5f, Y + 19 * Scale);
        PushMotion(Spot, FVector2D::ZeroVector, 0, Bump(HashCombine(static_cast<uint32>(U.Id), 0x4E42u), FMath::CeilToFloat(U.HP)));
        const FMotionGuard Pulse(this);
        Label(Health, BarX + BarW - HealthW, Y + 9 * Scale, 15 * Scale, Ink, false, 0, EFace::Bold);
    }
    Meter(BarX, Y + 32 * Scale, BarW, 16 * Scale, U.MaxHP > 0 ? U.HP / U.MaxHP : 0, U.HP < U.MaxHP * .3f ? Danger : Tone, 3908);
    // Tape and graphite shields as a thin frost line over the health bar.
    if (U.Shield > 0)
        Meter(BarX, Y + 50 * Scale, BarW, 5 * Scale, FMath::Clamp(U.Shield / FMath::Max(60.f, U.MaxHP * .25f), 0.f, 1.f), Frost, 3909, false);
    InkMeter(U, BarX, Y + 60 * Scale, BarW);
    // The pouch: stickers picked up and not banked yet (only those who walk out keep them).
    const FVector2D Star(BarX + 12 * Scale, Y + 100 * Scale);
    Glyph(5, Star, 26 * Scale, Marigold, 3910);
    const FText Pouch = FText::AsNumber(U.Pouch);
    {
        const float PouchW = Measure(Pouch, 24 * Scale, EFace::Heavy).X;
        PushMotion(FVector2D(BarX + 30 * Scale + PouchW * .5f, Star.Y), FVector2D::ZeroVector, 0, Bump(HashCombine(static_cast<uint32>(U.Id), 0x9C4Bu), static_cast<float>(U.Pouch)));
        const FMotionGuard Pulse(this);
        Label(Pouch, BarX + 30 * Scale, Star.Y - 16 * Scale, 24 * Scale, Ink, false, 0, EFace::Heavy);
    }
    const float PouchW = Measure(Pouch, 24 * Scale, EFace::Heavy).X;
    // The run's coins beside it (spent first at a shelf; they stay in the dungeon).
    const float CoinX = BarX + 46 * Scale + PouchW;
    CoinIcon(FVector2D(CoinX + 10 * Scale, Star.Y), 22 * Scale, 3915);
    const FText Coins = FText::AsNumber(U.Coins);
    {
        const float CoinsW = Measure(Coins, 20 * Scale, EFace::Heavy).X;
        PushMotion(FVector2D(CoinX + 26 * Scale + CoinsW * .5f, Star.Y), FVector2D::ZeroVector, 0, Bump(HashCombine(static_cast<uint32>(U.Id), 0xC014u), static_cast<float>(U.Coins)));
        const FMotionGuard Pulse(this);
        Label(Coins, CoinX + 26 * Scale, Star.Y - 13 * Scale, 20 * Scale, Ink, false, 0, EFace::Heavy);
    }
    // The note under both: too long for one line, it breaks at the " · " when both halves fit (zh would otherwise leave
    // "算数" alone on the second line), else wraps normally (de's second half is too long to stand alone).
    const FText Note = GWText::Get(TEXT("Expedition.Pouch"));
    const float NoteX = BarX, NoteY = Star.Y + 17 * Scale, NoteW = BarW;
    FString Head, Tail;
    if (Measure(Note, 11 * Scale).X > NoteW && Note.ToString().Split(TEXT(" · "), &Head, &Tail) &&
        FMath::Max(Measure(FText::FromString(Head), 11 * Scale).X, Measure(FText::FromString(Tail), 11 * Scale).X) <= NoteW)
    {
        Label(FText::FromString(Head), NoteX, NoteY, 11 * Scale, InkSoft);
        Label(FText::FromString(Tail), NoteX, NoteY + 11 * Scale * 1.45f, 11 * Scale, InkSoft);
    }
    else
        Paragraph(Note, NoteX, NoteY, 11 * Scale, NoteW, InkSoft);
}

void AGWHUD::ExpeditionTeam(AGWGameState* S, const FGWUnitView& Local)
{
    TArray<const FGWUnitView*> Mates;
    for (const auto& Other : S->Units)
        if (Other.Id != Local.Id && Other.Human && Other.Kind == 0)
            Mates.Add(&Other);
    if (Mates.IsEmpty())
        return;
    Mates.Sort([](const FGWUnitView& A, const FGWUnitView& B) { return A.Seat < B.Seat; });
    const float X = 22 * Scale, Y = 92 * Scale, W = 316 * Scale, RowH = 50 * Scale;
    const int32 Rows = FMath::Min(Mates.Num(), 7);
    Card(X, Y, W, 38 * Scale + Rows * RowH, Paper.CopyWithNewOpacity(.95f), Ink, 3950, 1.1f);
    Tape(FVector2D(X + W - 22 * Scale, Y + 6 * Scale), 54 * Scale, 16 * Scale, 34, Coral);
    Key(TEXT("Combat.Team"), X + 16 * Scale, Y + 8 * Scale, 15 * Scale, InkSoft, false, W * .6f, EFace::Bold);
    for (int32 I = 0; I < Rows; ++I)
    {
        const FGWUnitView& Mate = *Mates[I];
        const float RY = Y + 34 * Scale + I * RowH;
        const FLinearColor Tone = Seat(Mate.Seat);
        const bool Ghost = IsGhost(Mate);
        if (Mate.Extracted)
            Glyph(16, FVector2D(X + 24 * Scale, RY + 15 * Scale), 20 * Scale, Teal, 3951 + I);
        else if (Ghost)
        {
            for (int32 K = 0; K < 8; K += 2)
                Arc(FVector2D(X + 24 * Scale, RY + 15 * Scale), 10 * Scale, K * PI / 4, (K + 1) * PI / 4, Tone, 2 * Scale);
        }
        else
            Glyph(BodyGlyph(Mate.Shape), FVector2D(X + 24 * Scale, RY + 15 * Scale), 22 * Scale, Tone, 3951 + I);
        // The pouch (or what was banked on the way out) on the right.
        const FText Purse = FText::AsNumber(Mate.Extracted ? Mate.Banked : Mate.Pouch);
        const float PurseW = Measure(Purse, 14 * Scale, EFace::Heavy).X;
        Label(Purse, X + W - 14 * Scale - PurseW, RY + 2 * Scale, 14 * Scale, Mate.Extracted ? Teal : Ink, false, 0, EFace::Heavy);
        Glyph(5, FVector2D(X + W - 26 * Scale - PurseW, RY + 11 * Scale), 15 * Scale, Marigold, 3960 + I);
        Label(TeammateName(S, Mate), X + 44 * Scale, RY + 2 * Scale, 15 * Scale, Mate.Alive || Mate.Extracted ? Ink : Pencil, false, W - 100 * Scale - PurseW, EFace::Bold);
        const float LineY = RY + 26 * Scale;
        if (Mate.Extracted || Ghost)
        {
            const FText Tag = GWText::Get(Mate.Extracted ? (Mate.FinalExit ? TEXT("Expedition.Tag.Out") : TEXT("Expedition.Tag.Early")) : TEXT("Expedition.Tag.Ghost"));
            const float TagW = Measure(Tag, 12 * Scale, EFace::Heavy).X + 12 * Scale;
            Highlight(X + 44 * Scale, LineY - 2 * Scale, TagW, 17 * Scale, (Mate.Extracted ? Teal : Pencil).CopyWithNewOpacity(.4f), 3970 + I);
            Label(Tag, X + 44 * Scale + TagW * .5f, LineY - 2 * Scale, 12 * Scale, Ink, true, 0, EFace::Heavy);
            continue;
        }
        Meter(X + 44 * Scale, LineY, 92 * Scale, 7 * Scale, Mate.MaxHP > 0 ? Mate.HP / Mate.MaxHP : 0, Mate.HP < Mate.MaxHP * .3f ? Danger : Tone, 3980 + I, false);
        // The five slots in small: who carries the revive kit, the spare gun, the eraser.
        for (int32 K = 0; K < 5; ++K)
        {
            const FVector2D At(X + 156 * Scale + K * 30 * Scale, LineY + 3 * Scale);
            const auto& Item = Mate.Hotbar[K];
            if (K == Mate.Selected)
                Disc(At, 13 * Scale, Tone.CopyWithNewOpacity(.22f), 14);
            if (Item.Kind)
                SlotIcon(Item.Kind, Item.What, Item.Count, At, 22 * Scale, 3990 + I * 5 + K, Item.Variant);
            else
                Disc(At, 2 * Scale, Pencil, 6);
        }
    }
}

void AGWHUD::ExpeditionPrompts(AGWGameState* S, const FGWUnitView* Local)
{
    auto* C = Cast<AGWController>(PlayerOwner);
    if (!C || !C->WorldView || !Local || !Local->Alive || Local->Extracted || Settings || S->Finished || KitSheet || ReviveSheet)
        return;
    FReach Reach;
    if (!ExpeditionReach(S, *Local, Reach))
        return;
    FVector World = Local->Position;
    if (!C->PredictedPosition(Local->Id, World))
        C->WorldView->VisualPosition(Local->Id, World);
    FVector2D P;
    if (!WorldToScreen(S, World + FVector(0, 0, Local->Radius * 2.2f + 30), P))
        return;
    const FText KeyText = FText::FromString(TEXT("E"));
    const float TextSize = 16 * Scale, W = Measure(Reach.Text, TextSize, EFace::Bold).X + 66 * Scale, H = 40 * Scale;
    const float X = P.X - W * .5f, Y = P.Y - H - 18 * Scale - 3 * Scale * FMath::Sin(Clock * 5);
    Card(X, Y, W, H, Reach.Refused ? PaperShade : Paper, Reach.Refused ? Danger : Ink, 4001, 1.2f);
    Polygon({FVector2D(P.X - 9 * Scale, Y + H - 1), FVector2D(P.X + 9 * Scale, Y + H - 1), FVector2D(P.X, Y + H + 14 * Scale)}, Reach.Refused ? PaperShade : Paper,
            Reach.Refused ? Danger : Ink, 1.6f * Scale, 4002);
    Keycap(KeyText, X + 8 * Scale, Y + 6 * Scale, 28 * Scale, Reach.Refused ? Pencil : Ink);
    if (Reach.Hold)
    {
        // Held prompts fill a ring round the key as E is held.
        const FVector2D Ring(X + 22 * Scale, Y + 20 * Scale);
        Arc(Ring, 19 * Scale, -PI * .5f, -PI * .5f + 2 * PI, PaperDeep, 3 * Scale);
        if (Reach.Progress > 0)
            Arc(Ring, 19 * Scale, -PI * .5f, -PI * .5f + Reach.Progress * 2 * PI, Teal, 4 * Scale);
    }
    Label(Reach.Text, X + 46 * Scale, Y + 10 * Scale, TextSize, Reach.Refused ? Danger : Ink, false, 0, EFace::Bold);
}

void AGWHUD::ExpeditionTags(AGWGameState* S, const FGWUnitView* Local)
{
    auto* C = Cast<AGWController>(PlayerOwner);
    if (!C || !C->WorldView || S->Finished || FGWCinematic::Get().Clean)
        return;
    const FVector Me = Local ? Local->Position : FVector::ZeroVector;
    auto OnScreen = [&](FVector World, FVector2D& P) { return WorldToScreen(S, World, P) && P.X > -80 && P.Y > -60 && P.X < Width + 80 && P.Y < Height + 60; };
    auto PriceChip = [&](FVector2D At, int32 Price, bool Short, int32 Seed)
    {
        const FText Text = FText::AsNumber(Price);
        const float TW = Measure(Text, 13 * Scale, EFace::Heavy).X, W = TW + 30 * Scale, H = 20 * Scale;
        Card(At.X - W * .5f, At.Y, W, H, Short ? Hex(0xFBD3CB) : Hex(0xFFF2B0), Short ? Danger : Ink, Seed, .6f);
        Glyph(5, FVector2D(At.X - W * .5f + 11 * Scale, At.Y + H * .5f), 13 * Scale, Marigold, Seed + 1);
        Label(Text, At.X - W * .5f + 20 * Scale, At.Y + 2 * Scale, 13 * Scale, Short ? Danger : Ink, false, 0, EFace::Heavy);
    };
    const int32 Pouch = Local ? Purse(*Local) : 0;
    // The shelves: each ware's price over it; a machine names what it does.
    for (int32 I = 0; I < S->Offers.Num(); ++I)
    {
        const FGWOfferNet& O = S->Offers[I];
        if (O.Sold)
            continue;
        const FVector At = Unpack(O.X, O.Y);
        FVector2D P;
        if ((Local && FVector::Dist2D(At, Me) > 1600) || !OnScreen(At + FVector(0, 0, IsTrader(O) ? 250 : 130), P))
            continue;
        if (IsTrader(O))
        {
            // The buyer: a speech bubble with a coin in it.
            const FText Name = MachineName(O.What, O.Extra);
            const float NW = Measure(Name, 14 * Scale, EFace::Heavy).X + 46 * Scale;
            Card(P.X - NW * .5f, P.Y - 30 * Scale, NW, 26 * Scale, Hex(0xFFF2B0), Ink, 4100 + I, .8f);
            CoinIcon(FVector2D(P.X - NW * .5f + 16 * Scale, P.Y - 17 * Scale), 16 * Scale, 4140 + I);
            Label(Name, P.X - NW * .5f + 30 * Scale, P.Y - 28 * Scale, 14 * Scale, Ink, false, 0, EFace::Heavy);
            continue;
        }
        if (O.Kind == Machine)
        {
            const FText Name = MachineName(O.What, O.Extra);
            const float NW = Measure(Name, 13 * Scale, EFace::Bold).X + 16 * Scale;
            Highlight(P.X - NW * .5f, P.Y - 24 * Scale, NW, 18 * Scale, Magenta.CopyWithNewOpacity(.3f), 4100 + I);
            Label(Name, P.X, P.Y - 24 * Scale, 13 * Scale, Ink, true, 0, EFace::Bold);
            if (O.Price > 0)
                PriceChip(P, O.Price, Pouch < O.Price, 4150 + I * 2);
            continue;
        }
        QualityMark(P + FVector2D(-26 * Scale, -38 * Scale), 12 * Scale, PieceQuality(O.Kind, O.What, O.Kind == LootWeapon ? O.Extra : -1), 4190 + I);
        if (O.Kind == LootWeapon)
            SlotIcon(WeaponSlot, O.What, 1, P - FVector2D(0, 22 * Scale), 34 * Scale, 4200 + I, O.Extra);
        else if (O.Kind == LootItem)
            GearIcon(O.What, P - FVector2D(0, 22 * Scale), 30 * Scale, 4200 + I);
        else
            LookIcon(O.What, P - FVector2D(0, 22 * Scale), 30 * Scale, Marigold, 4200 + I);
        PriceChip(P, O.Price, Pouch < O.Price, 4250 + I * 2);
    }
    // Guns, items and looks on the floor near the player, nearest first: what they are (a look says whose it is). A
    // fallen kit lies in a heap, so a label that would cover one already written is left out.
    TArray<const FGWLootView*> Near;
    for (const auto& L : S->Loot)
        if ((L.Kind == LootWeapon || L.Kind == LootItem || L.Kind == LootLook) && (!Local || FVector::Dist2D(L.Position, Me) < 700))
            Near.Add(&L);
    Near.Sort([&](const FGWLootView& A, const FGWLootView& B) { return FVector::DistSquared2D(A.Position, Me) < FVector::DistSquared2D(B.Position, Me); });
    TArray<FBox2D> Written;
    for (const FGWLootView* Item : Near)
    {
        const FGWLootView& L = *Item;
        FVector2D P;
        if (!OnScreen(L.Position + FVector(0, 0, 60), P))
            continue;
        FText Name = PieceName(L.Kind, L.What, L.Variant);
        const int32 Quality = L.Kind == LootLook ? 0 : PieceQuality(L.Kind, L.What, L.Variant);
        FLinearColor Mark = Quality > 0 ? Grade(Quality) : L.Kind == LootWeapon ? Coral : L.Kind == LootItem ? Teal : Marigold;
        if (L.Kind == LootLook && L.Owner >= 0 && (!Local || L.Owner != Local->Seat))
        {
            for (const auto& U : S->Units)
                if (U.Human && U.Seat == L.Owner)
                    Name = FArgs()(TEXT("Name"), Name)(TEXT("Owner"), TeammateName(S, U)).Text(TEXT("Expedition.LookOf"));
            Mark = Seat(L.Owner);
        }
        const float NW = Measure(Name, 12 * Scale, EFace::Bold).X + 12 * Scale;
        const FBox2D Area(FVector2D(P.X - NW * .5f, P.Y - 19 * Scale), FVector2D(P.X + NW * .5f, P.Y - 1 * Scale));
        bool Overlaps = false;
        for (const FBox2D& Other : Written)
            Overlaps |= Area.Intersect(Other);
        if (Overlaps)
            continue;
        Written.Add(Area);
        Highlight(P.X - NW * .5f, P.Y - 18 * Scale, NW, 16 * Scale, Mark.CopyWithNewOpacity(Quality > 0 ? .45f : .35f), 4300 + L.Id % 100);
        Label(Name, P.X, P.Y - 18 * Scale, 12 * Scale, Ink, true, 0, EFace::Bold);
    }
    // Unopened chests near the player say their quality.
    for (int32 I = 0; I < S->Chests.Num(); ++I)
    {
        const FGWChestNet& Chest = S->Chests[I];
        const FVector At = Unpack(Chest.X, Chest.Y);
        FVector2D P;
        if (Chest.Opened || (Local && FVector::Dist2D(At, Me) > 900) || !OnScreen(At + FVector(0, 0, 150), P))
            continue;
        const FText Name = FArgs()(TEXT("Quality"), QualityName(Chest.Quality)).Text(TEXT("Expedition.ChestTag"));
        const float NW = Measure(Name, 12 * Scale, EFace::Bold).X + 30 * Scale;
        Highlight(P.X - NW * .5f, P.Y - 18 * Scale, NW, 16 * Scale, Grade(Chest.Quality).CopyWithNewOpacity(.45f), 4350 + I % 40);
        QualityMark(FVector2D(P.X - NW * .5f + 10 * Scale, P.Y - 10 * Scale), 11 * Scale, Chest.Quality, 4390 + I % 40);
        Label(Name, P.X + 8 * Scale, P.Y - 18 * Scale, 12 * Scale, Ink, true, 0, EFace::Bold);
    }
    // Exits and the portal: a tag over them on screen, pointed at from the edge of the page while off it.
    auto Visible = [&](FVector World)
    {
        FVector2D Q;
        return WorldToScreen(S, World, Q) && Q.X > 0 && Q.Y > 0 && Q.X < Width && Q.Y < Height;
    };
    for (int32 I = 0; I < S->Exits.Num(); ++I)
    {
        const FGWExitNet& Exit = S->Exits[I];
        const FVector At = Unpack(Exit.X, Exit.Y);
        const FText Name = GWText::Get(Exit.Final ? TEXT("Expedition.FinalDoor") : TEXT("Expedition.EarlyDoor"));
        // An early door is pointed at only once its room has been found.
        const bool Known = Exit.Final || (S->Rooms.IsValidIndex(Exit.Room) && S->Rooms[Exit.Room].Explored);
        if (!Visible(At))
        {
            if (Known)
                EdgePointer(S, At, 16, Name, Exit.Final ? Teal : Leaf, 4400 + I, 110 * Scale);
            continue;
        }
        FVector2D P;
        if (!OnScreen(At + FVector(0, 0, 230), P))
            continue;
        const float NW = Measure(Name, 15 * Scale, EFace::Heavy).X + 34 * Scale;
        Tape(FVector2D(P.X, P.Y), NW, 28 * Scale, -2, (Exit.Final ? Teal : Leaf).CopyWithNewOpacity(.85f));
        Label(Name, P.X, P.Y - 11 * Scale, 15 * Scale, Ink, true, 0, EFace::Heavy);
    }
    if (S->PortalOpen)
    {
        int32 In = 0, Living = 0;
        for (const auto& U : S->Units)
            if (U.Human && U.Alive && !U.Extracted)
            {
                ++Living;
                In += FVector::Dist2D(U.Position, S->PortalAt) < gwgame::PortalRadius;
            }
        FText Name = FArgs()(TEXT("In"), In)(TEXT("Count"), FMath::Max(1, Living)).Text(TEXT("Expedition.PortalCount"));
        if (In > 0 && S->PortalWait > 0)
            Name = FText::FromString(Name.ToString() + TEXT("  ·  ") +
                                     FArgs()(TEXT("Seconds"), FMath::Max(1, FMath::CeilToInt(gwgame::PortalWait - S->PortalWait))).Text(TEXT("Expedition.PortalLeaves")).ToString());
        if (!Visible(S->PortalAt))
            EdgePointer(S, S->PortalAt, 4, GWText::Get(TEXT("Expedition.Portal")), Cobalt, 4410, 110 * Scale);
        else
        {
            FVector2D P;
            if (OnScreen(S->PortalAt + FVector(0, 0, 240), P))
            {
                const float NW = Measure(Name, 15 * Scale, EFace::Heavy).X + 34 * Scale;
                Tape(FVector2D(P.X, P.Y), NW, 28 * Scale, 2, Cobalt.CopyWithNewOpacity(.55f));
                Label(Name, P.X, P.Y - 11 * Scale, 15 * Scale, Ink, true, 0, EFace::Heavy);
            }
        }
    }
    // Ghosts wait where they fell, named, with the revive's way in.
    for (const auto& U : S->Units)
    {
        if (!IsGhost(U) || (Local && U.Id == Local->Id))
            continue;
        FVector2D P;
        if (!OnScreen(U.Position + FVector(0, 0, 90), P))
            continue;
        const FText Name = FArgs()(TEXT("Name"), TeammateName(S, U)).Text(TEXT("Expedition.GhostTag"));
        const float NW = Measure(Name, 14 * Scale, EFace::Bold).X + 14 * Scale;
        Highlight(P.X - NW * .5f, P.Y - 22 * Scale, NW, 18 * Scale, Seat(U.Seat).CopyWithNewOpacity(.35f), 4420 + U.Id % 50);
        Label(Name, P.X, P.Y - 22 * Scale, 14 * Scale, Ink, true, 0, EFace::Bold);
    }
    // Elites wear a bar and every enemy the marks the run's items leave on it.
    for (const auto& U : S->Units)
    {
        if (!U.Alive || U.Kind != 2 || U.Radius >= 100 || (!U.Elite && !U.Marks))
            continue;
        FVector World = U.Position;
        C->WorldView->VisualPosition(U.Id, World);
        FVector2D P;
        if (!OnScreen(World + FVector(0, 0, U.Radius * 2.2f + 20), P))
            continue;
        if (U.Elite)
        {
            const float BarW = FMath::Clamp(U.Radius * 1.8f, 50.f, 120.f) * Scale;
            Meter(P.X - BarW * .5f, P.Y, BarW, 7 * Scale, U.HP / FMath::Max(1.f, U.MaxHP), Magenta, U.Id, false);
            if (U.Breed >= 0 && U.Breed < gwgame::FoeCount)
                Label(FoeName(U.Breed), P.X, P.Y + 9 * Scale, 11 * Scale, Magenta, true, 160 * Scale, EFace::Bold);
            Glyph(18, FVector2D(P.X - BarW * .5f - 10 * Scale, P.Y + 3 * Scale), 14 * Scale, Marigold, 4500 + U.Id % 50);
        }
        float MarkX = P.X - 30 * Scale;
        auto Mark = [&](uint8 Bit, int32 GlyphKind, FLinearColor Tone)
        {
            if (!(U.Marks & Bit))
                return;
            Glyph(GlyphKind, FVector2D(MarkX, P.Y - 12 * Scale), 13 * Scale, Tone, 4550 + GlyphKind);
            MarkX += 15 * Scale;
        };
        Mark(EGWMark::Stunned, 5, Spark);
        Mark(EGWMark::Stamped, 17, Danger);
        Mark(EGWMark::Gilded, 6, Marigold);
        Mark(EGWMark::Highlighted, 11, Spark);
        Mark(EGWMark::Wet, 23, Frost);
    }
}

void AGWHUD::ExpeditionBanners(AGWGameState* S, const FGWUnitView* Local)
{
    const uint32 FloorKey = HashCombine(static_cast<uint32>(S->ExpStage * 64 + S->ExpFloor), S->Seed);
    if (FloorKey != ExpSeenFloor)
    {
        ExpSeenFloor = FloorKey;
        ExpRoomStates.Reset();
        ExpRoomWaves.Reset();
        ExpSeenPortal = S->PortalOpen;
        DoorBanner = RoomClearBanner = PortalBanner = WaveCall = -10;
    }
    // The floor's own banner once the page wipe has opened (Since holds at zero while the wipe covers the screen).
    const float FloorAge = Since(HashCombine(FloorKey, 0xF100u));
    int32 Room = Local ? S->RoomAt(Local->Position) : -1;
    if (Room < 0)
        Room = S->ExpCurrent;
    // Each room's state against the last frame's: the doors coming down, a new wave and the clear are announced for the
    // room this player is in.
    for (int32 I = 0; I < S->Rooms.Num(); ++I)
    {
        const FGWRoomNet& R = S->Rooms[I];
        if (!ExpRoomStates.IsValidIndex(I))
        {
            ExpRoomStates.Add(R.State);
            ExpRoomWaves.Add(R.Wave);
            continue;
        }
        if (I == Room)
        {
            if (R.State == StateLocked && ExpRoomStates[I] != StateLocked)
                DoorBanner = Clock;
            if (R.State == StateCleared && ExpRoomStates[I] == StateLocked && !FGWCinematic::Get().QuietClears)
            {
                RoomClearBanner = Clock;
                Confetti(FVector2D(Width * .5f, Height * .36f), 30, 950, 7);
            }
            if (R.State == StateLocked && R.Wave > ExpRoomWaves[I] && R.Wave > 1)
            {
                WaveCall = Clock;
                ExpSeenWave = R.Wave;
            }
        }
        ExpRoomStates[I] = R.State;
        ExpRoomWaves[I] = R.Wave;
    }
    if (S->PortalOpen && !ExpSeenPortal)
        PortalBanner = Clock;
    ExpSeenPortal = S->PortalOpen;
    bool FinalDoor = false;
    for (const auto& Exit : S->Exits)
        FinalDoor |= Exit.Final;
    if (FinalDoor && !ExpSeenFinal)
        PortalBanner = Clock;
    ExpSeenFinal = FinalDoor;
    // A refused E tells only the player who pressed it why (a full hotbar, a short pouch...).
    auto* C = Cast<AGWController>(PlayerOwner);
    for (const auto& E : S->Effects)
        if (static_cast<gwgame::EffectStyle>(E.Style) == gwgame::EffectStyle::Denied && E.Id > ExpSeenDenied)
        {
            ExpSeenDenied = E.Id;
            if (C && Local && E.Subject == Local->Id)
                C->NoticeKey = FString::Printf(TEXT("Expedition.Denied.%d"), FMath::Clamp(FMath::RoundToInt(E.Radius), 20, 30));
        }
    if (S->ExpPrank != ExpSeenPrank)
    {
        if (S->ExpPrank >= 0)
            PrankBanner = Clock;
        ExpSeenPrank = S->ExpPrank;
    }
    for (const auto& E : S->Effects)
        if (static_cast<gwgame::EffectStyle>(E.Style) == gwgame::EffectStyle::BossPhase && E.Id > ExpSeenPhase)
        {
            ExpSeenPhase = E.Id;
            const FGWUnitView* Boss = S->ExpBoss >= 0 ? S->FindUnit(S->ExpBoss) : nullptr;
            const int32 Kind = FMath::RoundToInt(E.Radius);
            PhaseKey = Kind == static_cast<int32>(gwgame::Foe::Sage)                        ? TEXT("BossPhase.Dragon")
                       : Boss && (Boss->Parts & gwgame::PartsSplit)                        ? TEXT("BossPhase.Split")
                       : Kind == static_cast<int32>(gwgame::Foe::Rainbow)                  ? TEXT("BossPhase.Merge")
                                                                                            : TEXT("BossPhase.Split");
            PhaseBanner = Clock;
        }
    if (S->Finished)
        return;
    auto Banner = [&](float Age, const FText& Text, FLinearColor Color, int32 Seed, float Row, float Life = 2.4f)
    {
        if (Age < 0 || Age >= Life)
            return;
        const float Out = GWEase::InCubic((Age - (Life - .4f)) / .4f), Y = Height * Row - Out * 40 * Scale;
        const float W = FMath::Max(420 * Scale, Measure(Text, 40 * Scale, EFace::Heavy).X + 120 * Scale);
        const auto Pop = PopIn(FVector2D(Width * .5f, Y + 30 * Scale), Age);
        PushMotion(FVector2D(Width * .5f, Y + 30 * Scale), FVector2D::ZeroVector, Out * 3, 1, 1 - Out);
        const FMotionGuard Lift(this);
        Tape(FVector2D(Width * .5f, Y + 30 * Scale), W * GWEase::OutBack(Age / .3f), 62 * Scale, -2, Color);
        Lettering(Text, Width * .5f, Y + 5 * Scale, 40 * Scale, Ink, Seed, Age - .08f);
    };
    const int32 PenKind = static_cast<int32>(S->Biome());
    const int32 Floors = gwgame::stageFloors(FMath::Clamp<int32>(S->ExpStage, 1, 4));
    const bool BossFloor = S->ExpFloor >= Floors;
    Banner(FloorAge - .25f,
           FArgs()(TEXT("Stage"), static_cast<int32>(S->ExpStage))(TEXT("Floor"), static_cast<int32>(S->ExpFloor))(TEXT("Pen"), BiomeName(PenKind))
               .Text(BossFloor ? TEXT("Expedition.FloorBoss") : TEXT("Expedition.Floor")),
           (BossFloor ? Danger : Biome(PenKind)).CopyWithNewOpacity(.8f), 4601, .2f, 2.6f);
    Banner(Clock - DoorBanner, GWText::Get(TEXT("Expedition.DoorsShut")), Coral.CopyWithNewOpacity(.85f), 4602, .27f);
    Banner(Clock - WaveCall, FArgs()(TEXT("Wave"), FMath::Max(1, ExpSeenWave)).Text(TEXT("Expedition.WaveCall")), Marigold, 4603, .27f, 1.8f);
    Banner(Clock - RoomClearBanner, GWText::Get(TEXT("Expedition.RoomClear")), Teal.CopyWithNewOpacity(.85f), 4604, .34f);
    Banner(Clock - PortalBanner, GWText::Get(FinalDoor ? TEXT("Expedition.WayOut") : TEXT("Expedition.PortalBanner")), (FinalDoor ? Teal : Cobalt).CopyWithNewOpacity(.6f), 4605,
           .41f, 3.f);
    if (S->ExpPrank >= 0)
        Banner(Clock - PrankBanner, FArgs()(TEXT("Name"), PrankName(S->ExpPrank)).Text(TEXT("Expedition.PrankBanner")), Magenta.CopyWithNewOpacity(.55f), 4606, .3f, 2.8f);
    if (!PhaseKey.IsEmpty())
        Banner(Clock - PhaseBanner, GWText::Get(*PhaseKey), Danger.CopyWithNewOpacity(.6f), 4607, .3f, 2.2f);
}

void AGWHUD::GhostCard(AGWGameState* S, const FGWUnitView& U)
{
    const float Down = Since(HashCombine(static_cast<uint32>(U.Id), U.Extracted ? 0xE5C9u : 0x6057u));
    if (!U.Extracted)
        Box(0, 0, Width, Height, Ink.CopyWithNewOpacity(.14f * GWEase::Clamp01(Down / .3f)));
    // Where the hotbar would be (a ghost has none): notices go above it.
    const float W = FMath::Min(600 * Scale, Width - 80 * Scale), H = 124 * Scale, X = (Width - W) * .5f, Y = Height - H - 70 * Scale;
    const auto Drop = DropIn(FVector2D(Width * .5f, Y + H * .5f), Down, .1f, 60);
    Card(X, Y, W, H, Paper, Ink, 4700, 1.6f);
    Tape(FVector2D(X + 40 * Scale, Y + 6 * Scale), 70 * Scale, 20 * Scale, -30, U.Extracted ? Teal : Pencil);
    const FVector2D Face(X + 62 * Scale, Y + H * .5f);
    if (U.Extracted)
        Glyph(16, Face, 46 * Scale, Teal, 4701);
    else
        for (int32 K = 0; K < 10; K += 2)
            Arc(Face, 24 * Scale, K * PI / 5, (K + 1) * PI / 5, Seat(U.Seat), 3 * Scale);
    int32 Standing = 0;
    for (const auto& Other : S->Units)
        Standing += Other.Human && Other.Alive && !Other.Extracted;
    FText Title, Note;
    if (U.Extracted)
    {
        Title = FArgs()(TEXT("Count"), U.Banked).Text(TEXT("Expedition.OutTitle"));
        Note = GWText::Get(Standing > 0 ? TEXT("Expedition.OutNote") : TEXT("Expedition.OutLast"));
    }
    else
    {
        Title = GWText::Get(TEXT("Expedition.GhostTitle"));
        Note = GWText::Get(Standing > 0 ? TEXT("Expedition.GhostNote") : TEXT("Expedition.GhostAlone"));
    }
    Label(Title, X + 112 * Scale, Y + 18 * Scale, 22 * Scale, Ink, false, W - 130 * Scale, EFace::Heavy);
    Paragraph(Note, X + 112 * Scale, Y + 54 * Scale, 15 * Scale, W - 130 * Scale, InkSoft);
}

void AGWHUD::DrawKitSheet(AGWGameState* S, const FGWUnitView* Local)
{
    if (!Local || !Local->Alive || Local->Extracted)
    {
        KitSheet = false;
        return;
    }
    const FGWUnitView& U = *Local;
    // The buyer's sheet folds away once its player walks off from the counter.
    const FGWOfferNet* Buyer = nullptr;
    for (const auto& O : S->Offers)
        if (IsTrader(O) && FVector::Dist2D(Unpack(O.X, O.Y), U.Position) < gwgame::TradeReach + U.Radius + 90)
            Buyer = &O;
    if (KitTrade && !Buyer)
    {
        KitSheet = KitTrade = false;
        KitPick = -1;
        return;
    }
    const int32 Open = 5 + FMath::Clamp(U.PackSize, 0, 10);
    if (KitPick >= Open || (KitPick >= 0 && U.KitSlot(KitPick).Kind == 0))
        KitPick = -1;
    const bool Looks = KitTab == 1 && !KitTrade;
    Box(0, 0, Width, Height, Ink.CopyWithNewOpacity(.3f));
    // Nothing under the sheet takes a click while it is open.
    AddHit(FBox2D(FVector2D(0, 0), FVector2D(Width, Height)), GWDefenseAction::Blocker);
    const float W = FMath::Min(1020 * Scale, Width - 60 * Scale), H = FMath::Min(580 * Scale, Height - 110 * Scale);
    const float X = (Width - W) * .5f, Y = (Height - H) * .45f;
    const auto Pop = PopIn(FVector2D(Width * .5f, Y + H * .5f), Since(KitTrade ? TEXT("Kit.Trade") : TEXT("Kit.Sheet")));
    Card(X, Y, W, H, Paper, Ink, 4800, 2.2f, 2.2f * Scale);
    Tape(FVector2D(X + W * .5f, Y + 4 * Scale), 120 * Scale, 24 * Scale, -2, (KitTrade ? CoinBrass : Seat(U.Seat)).CopyWithNewOpacity(.85f));
    Key(KitTrade ? TEXT("Trade.Title") : TEXT("Kit.Title"), X + 28 * Scale, Y + 20 * Scale, 26 * Scale, Ink, false, W * .45f, EFace::Heavy);
    Paragraph(GWText::Get(KitTrade ? TEXT("Trade.Note") : Looks ? TEXT("Kit.Note") : TEXT("Kit.BagNote")), X + 28 * Scale, Y + 58 * Scale, 14 * Scale,
              W - (KitTrade ? 110 : 330) * Scale, InkSoft);
    Button(GWDefenseAction::KitClose, FText::FromString(TEXT("×")), X + W - 62 * Scale, Y + 18 * Scale, 42 * Scale, 38 * Scale, true, false, Coral);
    if (!KitTrade)
    {
        // Two tabs: the bag and the looks worn.
        const float TW = 118 * Scale;
        Button(GWDefenseAction::KitTabs, GWText::Get(TEXT("Kit.TabBag")), X + W - 82 * Scale - 2 * TW, Y + 20 * Scale, TW - 8 * Scale, 34 * Scale, true, !Looks, Teal);
        Button(GWDefenseAction::KitTabs + 1, GWText::Get(TEXT("Kit.TabLooks")), X + W - 82 * Scale - TW, Y + 20 * Scale, TW - 8 * Scale, 34 * Scale, true, Looks,
               Marigold);
    }
    const float Top = Y + 112 * Scale, Gap = 22 * Scale, Inner = W - 56 * Scale - 2 * Gap, Bottom = Y + H - 22 * Scale;
    const float LeftCol = Inner * .24f, GridCol = Inner * .47f, InfoCol = Inner - LeftCol - GridCol;
    // The pouch and the coins, and the drops for a teammate.
    {
        // Each money with its count and its own pair of drops right under it.
        const float CX = X + 28 * Scale, ColW = LeftCol, BW = (ColW - 8 * Scale) * .5f, BH = 34 * Scale;
        Key(TEXT("Kit.Pouch"), CX, Top, 15 * Scale, InkSoft, false, ColW, EFace::Bold);
        Glyph(5, FVector2D(CX + 16 * Scale, Top + 40 * Scale), 28 * Scale, Marigold, 4801);
        Label(FText::AsNumber(U.Pouch), CX + 40 * Scale, Top + 23 * Scale, 28 * Scale, Ink, false, ColW - 40 * Scale, EFace::Heavy);
        Button(GWDefenseAction::KitDrop, GWText::Get(TEXT("Kit.Drop25")), CX, Top + 64 * Scale, BW, BH, U.Pouch > 0, false, Marigold);
        Button(GWDefenseAction::KitDrop + 1, GWText::Get(TEXT("Kit.DropAll")), CX + BW + 8 * Scale, Top + 64 * Scale, BW, BH, U.Pouch > 0, false, Coral);
        Key(TEXT("Kit.Coins"), CX, Top + 112 * Scale, 15 * Scale, InkSoft, false, ColW, EFace::Bold);
        CoinIcon(FVector2D(CX + 16 * Scale, Top + 152 * Scale), 26 * Scale, 4802);
        Label(FText::AsNumber(U.Coins), CX + 40 * Scale, Top + 135 * Scale, 28 * Scale, Ink, false, ColW - 40 * Scale, EFace::Heavy);
        Button(GWDefenseAction::KitCoins, GWText::Get(TEXT("Kit.Coins100")), CX, Top + 176 * Scale, BW, BH, U.Coins > 0, false, CoinBrass);
        Button(GWDefenseAction::KitCoins + 1, GWText::Get(TEXT("Kit.CoinsAll")), CX + BW + 8 * Scale, Top + 176 * Scale, BW, BH, U.Coins > 0, false, Coral);
        const float NoteH = Paragraph(GWText::Get(TEXT("Kit.CoinNote")), CX, Top + 222 * Scale, 12 * Scale, ColW, InkSoft);
        // The revive trait this body was drawn with.
        if (U.Trait >= 0 && Top + 236 * Scale + NoteH + 26 * Scale < Bottom)
        {
            const float TY = Top + 232 * Scale + NoteH;
            BiomeIcon(U.Trait, FVector2D(CX + 14 * Scale, TY + 12 * Scale), 24 * Scale, 4850);
            Label(FArgs()(TEXT("Name"), GWText::Get(FString::Printf(TEXT("Trait.%d"), U.Trait))).Text(TEXT("Kit.Trait")), CX + 32 * Scale, TY + 2 * Scale, 13 * Scale, Ink,
                  false, ColW - 32 * Scale, EFace::Bold);
        }
    }
    if (Looks)
    {
        // The looks worn, one per category.
        const float CX = X + 28 * Scale + LeftCol + Gap, ColW = GridCol + Gap + InfoCol, RowH = FMath::Min(56 * Scale, (Bottom - Top - 30 * Scale) / 6);
        Key(TEXT("Kit.Looks"), CX, Top, 16 * Scale, InkSoft, false, ColW, EFace::Bold);
        for (int32 Category = 0; Category < 6; ++Category)
        {
            const float RY = Top + 28 * Scale + Category * RowH;
            const int32 Worn = U.Wear[Category];
            const FVector2D Icon(CX + 18 * Scale, RY + RowH * .45f);
            if (Worn > 0)
                LookIcon(gwgame::lookId(Category, Worn), Icon, 28 * Scale, Marigold, 4810 + Category);
            else
                Circle(Icon, 11 * Scale, Pencil, 1.4f * Scale, 4810 + Category);
            Key(*FString::Printf(TEXT("Wear.%d"), Category), CX + 40 * Scale, RY + 2 * Scale, 12 * Scale, InkSoft, false, ColW - 140 * Scale);
            Label(Worn > 0 ? LookName(gwgame::lookId(Category, Worn)) : FText::FromString(TEXT("—")), CX + 40 * Scale, RY + 17 * Scale, 14 * Scale,
                  Worn > 0 ? Ink : Pencil, false, ColW - 140 * Scale, EFace::Bold);
            if (Worn > 0)
                Button(GWDefenseAction::KitDrop + 2 + Category, GWText::Get(TEXT("Kit.DropLook")), CX + ColW - 96 * Scale, RY + 4 * Scale, 96 * Scale,
                       RowH - 12 * Scale, true, false, Pencil);
        }
        return;
    }
    // The grid: the hotbar's five over the bag's ten (the closed ones wait for the research desk's backpack).
    const float GX = X + 28 * Scale + LeftCol + Gap, SlotGap = 10 * Scale;
    const float Chip = KitTrade ? 20 * Scale : 0.f;
    const float SlotSize = FMath::Min((GridCol - 4 * SlotGap) / 5, (Bottom - Top - 2 * 34 * Scale - 3 * Chip - 3 * SlotGap - 40 * Scale) / 3);
    const FLinearColor Picked = Seat(U.Seat);
    auto DrawSlot = [&](int32 Index, float SX, float SY, float SW)
    {
        const bool Closed = Index >= Open;
        const auto& Item = U.KitSlot(Index);
        const int32 Quality = Closed ? 0 : SlotQuality(Item);
        const bool On = Index == KitPick, Hover = !Closed && Hovered(SX, SY, SW, SlotSize);
        if (Closed)
        {
            // A dashed pencil outline with a little lock: open it at the research desk.
            Card(SX, SY, SW, SlotSize, PaperShade.CopyWithNewOpacity(.5f), Pencil, 4900 + Index, .4f, 1.f * Scale);
            const FVector2D Lock(SX + SW * .5f, SY + SlotSize * .42f);
            Arc(Lock - FVector2D(0, 6 * Scale), 8 * Scale, PI, 2 * PI, Pencil, 2 * Scale);
            Box(Lock.X - 11 * Scale, Lock.Y - 6 * Scale, 22 * Scale, 17 * Scale, Pencil.CopyWithNewOpacity(.6f));
            Key(TEXT("Kit.Locked"), SX + SW * .5f, SY + SlotSize - 20 * Scale, 10 * Scale, Pencil, true, SW - 6 * Scale, EFace::Bold);
            return;
        }
        Card(SX, SY - (On ? 6 * Scale : 0.f), SW, SlotSize, Item.Kind ? Paper : PaperShade.CopyWithNewOpacity(.85f), On ? Picked : Quality > 0 ? Grade(Quality) : Ink,
             4900 + Index, 1.f + (On ? 1.f : 0.f) + (Hover ? .4f : 0.f), (On ? 3.4f : Quality > 0 ? 2.4f : 1.4f) * Scale);
        const float LiftY = SY - (On ? 6 * Scale : 0.f);
        if (Quality > 0)
            Box(SX + 5 * Scale, LiftY + SlotSize - 7 * Scale, SW - 10 * Scale, 3 * Scale, Grade(Quality).CopyWithNewOpacity(.75f));
        if (Item.Kind)
            SlotIcon(Item.Kind, Item.What, Item.Count, FVector2D(SX + SW * .5f, LiftY + SlotSize * .5f), SlotSize * (SW > SlotSize ? .74f : .62f), 4920 + Index * 7,
                     Item.Variant);
        else if (gwgame::handSlot(Index))
            // An empty hand says which hand it is.
            Key(Index == 0 ? TEXT("Kit.LeftHand") : TEXT("Kit.RightHand"), SX + SW * .5f, LiftY + SlotSize * .5f - 8 * Scale, 12 * Scale, Pencil, true, SW - 8 * Scale,
                EFace::Bold);
        QualityMark(FVector2D(SX + 9 * Scale, LiftY + SlotSize - 12 * Scale), 12 * Scale, Quality, 5030 + Index);
        if (Item.Kind == ItemSlot && Item.Count > 1)
        {
            const FText Badge = FText::FromString(FString::Printf(TEXT("×%d"), Item.Count));
            const float BW = Measure(Badge, 12 * Scale, EFace::Heavy).X + 10 * Scale;
            Card(SX + SW - BW - 2 * Scale, LiftY + SlotSize - 19 * Scale, BW, 17 * Scale, Marigold, Ink, 5050 + Index, .5f);
            Label(Badge, SX + SW - BW * .5f - 2 * Scale, LiftY + SlotSize - 19 * Scale, 12 * Scale, Ink, true, 0, EFace::Heavy);
        }
        if (Index < 5)
            Keycap(FText::AsNumber(Index + 1), SX - 6 * Scale, LiftY - 8 * Scale, 20 * Scale,
                   (gwgame::handSlot(Index) ? U.Selected == 0 : Index == U.Selected) ? Ink : InkSoft);
        if (Index < 5 && Item.Kind == WeaponSlot && U.Nibs[Index] >= 0)
            Polygon({FVector2D(SX + SW - 4 * Scale, LiftY + 4 * Scale), FVector2D(SX + SW - 4 * Scale, LiftY + 20 * Scale),
                     FVector2D(SX + SW - 20 * Scale, LiftY + 4 * Scale)},
                    Biome(U.Nibs[Index]), Ink, 1.4f * Scale, 5070 + Index);
        // At the buyer: what each slot fetches.
        if (KitTrade && Item.Kind)
        {
            const FText Price = FText::AsNumber(SlotCoins(Item));
            const float PW = Measure(Price, 12 * Scale, EFace::Heavy).X + 26 * Scale;
            Card(SX + (SW - PW) * .5f, SY + SlotSize + 3 * Scale, PW, 17 * Scale, Hex(0xFFF2B0), Ink, 5090 + Index, .5f);
            CoinIcon(FVector2D(SX + (SW - PW) * .5f + 10 * Scale, SY + SlotSize + 11.5f * Scale), 12 * Scale, 5110 + Index);
            Label(Price, SX + (SW - PW) * .5f + 19 * Scale, SY + SlotSize + 3 * Scale, 12 * Scale, Ink, false, 0, EFace::Heavy);
        }
        AddHit(FBox2D(FVector2D(SX, SY - 6 * Scale), FVector2D(SX + SW, SY + SlotSize)), GWDefenseAction::KitSlot + Index);
    };
    float RowY = Top;
    // The hotbar's two groups (2026-11-17): the hands (guns only; a long gun takes both) and the belt (items, which only
    // work from there).
    Key(TEXT("Kit.Hands"), GX, RowY, 15 * Scale, InkSoft, false, 2 * SlotSize + SlotGap, EFace::Bold);
    Key(TEXT("Kit.Belt"), GX + 2 * (SlotSize + SlotGap), RowY, 15 * Scale, InkSoft, false, 3 * SlotSize + 2 * SlotGap, EFace::Bold);
    RowY += 34 * Scale;
    if (IsLongGun(U.Hotbar[0]))
        DrawSlot(0, GX, RowY, 2 * SlotSize + SlotGap);
    else
        for (int32 I = 0; I < gwgame::HandSlots; ++I)
            DrawSlot(I, GX + I * (SlotSize + SlotGap), RowY, SlotSize);
    for (int32 I = gwgame::HandSlots; I < 5; ++I)
        DrawSlot(I, GX + I * (SlotSize + SlotGap), RowY, SlotSize);
    // A pencil bracket under the hands.
    Pen(FVector2D(GX + 4 * Scale, RowY + SlotSize + 4 * Scale), FVector2D(GX + 2 * SlotSize + SlotGap - 4 * Scale, RowY + SlotSize + 4 * Scale), Pencil, 1.4f * Scale,
        5140, .3f);
    RowY += SlotSize + Chip + SlotGap + 6 * Scale;
    Label(FArgs()(TEXT("Open"), Open - 5)(TEXT("Count"), gwgame::PackMax).Text(TEXT("Kit.Bag")), GX, RowY, 16 * Scale, InkSoft, false, GridCol, EFace::Bold);
    RowY += 34 * Scale;
    for (int32 I = 0; I < gwgame::PackMax; ++I)
        DrawSlot(5 + I, GX + (I % 5) * (SlotGap + SlotSize), RowY + (I / 5) * (SlotSize + Chip + SlotGap), SlotSize);
    RowY += 2 * (SlotSize + Chip + SlotGap);
    Paragraph(GWText::Get(KitPick >= 0 ? TEXT("Kit.MoveHint") : TEXT("Kit.PickHint")), GX, FMath::Min(RowY, Bottom - 30 * Scale), 12 * Scale, GridCol, InkSoft);
    // What the picked slot holds (or the one in hand), what it does, and what it fetches; drop it or sell it.
    {
        const float CX = GX + GridCol + Gap, ColW = InfoCol;
        const int32 Shown = KitPick >= 0                                                      ? KitPick
                            : U.Selected == 0 && U.Hotbar[0].Kind == 0 && U.Hotbar[1].Kind != 0 ? 1
                                                                                                 : FMath::Clamp(U.Selected, 0, 4);
        const auto& Item = U.KitSlot(Shown);
        Card(CX, Top, ColW, Bottom - Top, Hex(0xFBF7EC), Ink, 4860, .8f);
        Key(KitPick >= 0 ? TEXT("Kit.Picked") : TEXT("Kit.InHand"), CX + 14 * Scale, Top + 10 * Scale, 13 * Scale, InkSoft, false, ColW - 28 * Scale, EFace::Bold);
        if (!Item.Kind)
        {
            Paragraph(GWText::Get(TEXT("Kit.EmptySlot")), CX + 14 * Scale, Top + 40 * Scale, 14 * Scale, ColW - 28 * Scale, Pencil);
            return;
        }
        const int32 Quality = SlotQuality(Item);
        const FVector2D Icon(CX + ColW * .5f, Top + 82 * Scale);
        if (Quality > 0)
            Disc(Icon, 44 * Scale, Grade(Quality).CopyWithNewOpacity(.16f));
        SlotIcon(Item.Kind, Item.What, Item.Count, Icon, 70 * Scale, 4870, Item.Variant);
        float LineY = Top + 132 * Scale;
        const FText Name = SlotName(Item.Kind, Item.What, Item.Count, Item.Variant);
        Label(Name, CX + ColW * .5f, LineY, 17 * Scale, Ink, true, ColW - 24 * Scale, EFace::Heavy);
        LineY += 26 * Scale;
        {
            const FText Tag = Quality > 0 ? FArgs()(TEXT("Quality"), QualityName(Quality)).Text(TEXT("Kit.QualityTag")) : GWText::Get(TEXT("Kit.NoQuality"));
            const float TW = Measure(Tag, 12 * Scale, EFace::Heavy).X + 16 * Scale;
            Highlight(CX + (ColW - TW) * .5f, LineY, TW, 17 * Scale, (Quality > 0 ? Grade(Quality) : Pencil).CopyWithNewOpacity(.45f), 4875);
            Label(Tag, CX + ColW * .5f, LineY, 12 * Scale, Ink, true, 0, EFace::Heavy);
        }
        LineY += 26 * Scale;
        const FText Note = Item.Kind == ItemSlot                                                       ? GearNote(Item.What)
                           : Item.Kind == WeaponSlot && Item.Variant >= 0                            ? GWText::Get(FString::Printf(TEXT("Gun.%d.Note"), Item.Variant))
                           : Item.Kind == WeaponSlot && Shown < 5 && U.Nibs[Shown] >= 0               ? GWText::Get(FString::Printf(TEXT("Nib.%d"), FMath::Clamp(U.Nibs[Shown], 0, 4)))
                           : GWText::Get(gwgame::twoHanded(Item.What) ? TEXT("Kit.LongNote") : TEXT("Kit.GunNote"));
        LineY += Paragraph(Note, CX + 14 * Scale, LineY, 12 * Scale, ColW - 28 * Scale, Ink) + 8 * Scale;
        // In the bag a piece only waits (2026-11-17): guns work from the hands, items from the belt.
        if (Shown >= 5)
            LineY += Paragraph(GWText::Get(Item.Kind == ItemSlot ? TEXT("Kit.BagIdle") : TEXT("Kit.BagGun")), CX + 14 * Scale, LineY, 11 * Scale, ColW - 28 * Scale,
                               InkSoft) +
                     6 * Scale;
        const int32 Fetch = SlotCoins(Item);
        Label(FArgs()(TEXT("Coins"), Fetch).Text(TEXT("Kit.Fetch")), CX + 14 * Scale, LineY, 12 * Scale, InkSoft, false, ColW - 28 * Scale, EFace::Bold);
        const float BH = 40 * Scale, BW = ColW - 28 * Scale;
        if (KitPick >= 0)
        {
            if (KitTrade)
                Button(GWDefenseAction::KitSellPick, FArgs()(TEXT("Coins"), Fetch).Text(TEXT("Trade.Sell")), CX + 14 * Scale, Bottom - 2 * BH - 18 * Scale, BW, BH, true, false,
                       CoinBrass);
            Button(GWDefenseAction::KitDropPick, GWText::Get(TEXT("Kit.DropSlot")), CX + 14 * Scale, Bottom - BH - 10 * Scale, BW, BH, true, false, Pencil);
        }
    }
    (void)S;
}

bool AGWHUD::OpenTrade()
{
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    auto* C = Cast<AGWController>(PlayerOwner);
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    const FGWUnitView* U = S && P ? S->FindUnit(P->UnitId) : nullptr;
    if (!S || !U || !S->Expedition() || S->Finished || !U->Alive || U->Extracted || ReviveSheet || Settings)
        return false;
    FReach Reach;
    if (!ExpeditionReach(S, *U, Reach) || !Reach.Trader)
        return false;
    KitSheet = KitTrade = true;
    KitPick = -1;
    KitTab = 0;
    return true;
}

// ---- The revive sheet (design 5.2) ----

bool AGWHUD::OpenRevive(bool Kitless)
{
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    auto* C = Cast<AGWController>(PlayerOwner);
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    const FGWUnitView* U = S && P ? S->FindUnit(P->UnitId) : nullptr;
    if (!S || !C || !S->Expedition() || S->Finished || !U || !U->Alive || U->Extracted || (!Kitless && KitSlotOf(*U, ReviveKitGear) < 0))
        return false;
    bool Any = false;
    for (const auto& Other : S->Units)
        Any |= IsGhost(Other) && Other.Id != U->Id;
    if (!Any)
    {
        C->NoticeKey = TEXT("Revive.NoGhost");
        return false;
    }
    KitSheet = false;
    ReviveSheet = true;
    ReviveStroke.Reset();
    ReviveDrawing = false;
    ReviveMeasured = -1;
    return true;
}

void AGWHUD::CloseRevive()
{
    ReviveSheet = false;
    ReviveDrawing = false;
    ReviveStroke.Reset();
    ReviveMeasured = -1;
}

void AGWHUD::ToggleKit()
{
    if (ReviveSheet)
        return;
    KitSheet = !KitSheet;
    KitTrade = false;
    KitPick = -1;
    KitTab = 0;
}

void AGWHUD::DrawReviveStroke(const TArray<FVector2D>& Points)
{
    ReviveStroke = Points;
    ReviveDrawing = false;
    ReviveMeasured = -1;
}

bool AGWHUD::ReviveFromClick()
{
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    auto* C = Cast<AGWController>(PlayerOwner);
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    const FGWUnitView* U = S && P ? S->FindUnit(P->UnitId) : nullptr;
    if (!S || !S->Expedition() || S->Finished || !U || !U->Alive || ReviveSheet || KitSheet || !C->GameplayInputAllowed())
        return false;
    const int32 Held = C->AskedSlot >= 0 && GetWorld()->GetTimeSeconds() - C->AskedAt < .35 ? C->AskedSlot : FMath::Clamp(U->Selected, 0, 4);
    if (U->Hotbar[Held].Kind != ItemSlot || U->Hotbar[Held].What != ReviveKitGear)
        return false;
    // The kit in hand: the click opens the sheet (or says there is nobody to draw back) instead of a shot.
    OpenRevive();
    return true;
}

bool AGWHUD::ReviveClick(float X, float Y)
{
    if (!ReviveSheet || !ReviveSquare.IsInside(FVector2D(X, Y)))
        return false;
    // A fresh stroke: the last one is forgotten, nothing has been spent.
    ReviveStroke.Reset();
    ReviveStroke.Add(FVector2D((X - ReviveSquare.Min.X) / ReviveSquare.GetSize().X, (Y - ReviveSquare.Min.Y) / ReviveSquare.GetSize().Y));
    ReviveDrawing = true;
    ReviveMeasured = -1;
    return true;
}

void AGWHUD::TrackReviveStroke()
{
    if (!ReviveDrawing || !PlayerOwner)
        return;
    float MX = 0, MY = 0;
    const FVector2D Size = ReviveSquare.GetSize();
    if (PlayerOwner->GetMousePosition(MX, MY) && Size.X > 1 && Size.Y > 1)
    {
        const FVector2D At(FMath::Clamp((MX - ReviveSquare.Min.X) / Size.X, 0.f, 1.f), FMath::Clamp((MY - ReviveSquare.Min.Y) / Size.Y, 0.f, 1.f));
        // Points at a fixed spacing (design 5.2: 以固定间距采样点列).
        if (ReviveStroke.Num() < gwgame::ReviveMaxPoints && (ReviveStroke.IsEmpty() || FVector2D::Distance(At, ReviveStroke.Last()) >= .012f))
        {
            ReviveStroke.Add(At);
            ReviveMeasured = -1;
        }
    }
    if (!PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton))
    {
        ReviveDrawing = false;
        ReviveMeasured = -1;
    }
}

void AGWHUD::SubmitRevive()
{
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    auto* C = Cast<AGWController>(PlayerOwner);
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    const FGWUnitView* U = S && P ? S->FindUnit(P->UnitId) : nullptr;
    const FGWUnitView* Target = S ? S->FindUnit(ReviveTarget) : nullptr;
    if (!U || !Target || !IsGhost(*Target) || ReviveDrawing || ReviveStroke.Num() < 3 || !ReviveClosed || ReviveShare < gwgame::ReviveMinShare ||
        U->Ink + 1e-3f < ReviveCost(*U, ReviveShare) || RevivePenPick < 0 || !((U->Pens >> RevivePenPick) & 1))
        return;
    TArray<uint32> Packed;
    for (const FVector2D& Point : ReviveStroke)
        Packed.Add((static_cast<uint32>(FMath::RoundToInt(FMath::Clamp(Point.X, 0.f, 1.f) * 65535)) << 16) |
                   static_cast<uint32>(FMath::RoundToInt(FMath::Clamp(Point.Y, 0.f, 1.f) * 65535)));
    C->ServerRevive(Target->Id, Packed, static_cast<uint8>(RevivePenPick));
    C->NoticeKey = TEXT("Revive.Sent");
    CloseRevive();
}

void AGWHUD::DrawReviveSheet(AGWGameState* S, const FGWUnitView* Local)
{
    // The sheet folds away if the reviver falls or leaves, or when nobody is left to draw back.
    TArray<const FGWUnitView*> Ghosts;
    for (const auto& Other : S->Units)
        if (IsGhost(Other) && (!Local || Other.Id != Local->Id))
            Ghosts.Add(&Other);
    if (!Local || !Local->Alive || Local->Extracted || Ghosts.IsEmpty())
    {
        CloseRevive();
        return;
    }
    Ghosts.Sort([](const FGWUnitView& A, const FGWUnitView& B) { return A.Id < B.Id; });
    const FGWUnitView& U = *Local;
    // The ghost and pen picked (or the nearest ghost and the first pen there is).
    bool TargetThere = false;
    for (const FGWUnitView* G : Ghosts)
        TargetThere |= G->Id == ReviveTarget;
    if (!TargetThere)
    {
        float Nearest = 1e12f;
        for (const FGWUnitView* G : Ghosts)
            if (FVector::DistSquared2D(G->Position, U.Position) < Nearest)
            {
                Nearest = FVector::DistSquared2D(G->Position, U.Position);
                ReviveTarget = G->Id;
            }
    }
    if (RevivePenPick < 0 || !((U.Pens >> RevivePenPick) & 1))
    {
        RevivePenPick = -1;
        for (int32 Tool = 0; Tool < gwgame::BiomeCount && RevivePenPick < 0; ++Tool)
            if ((U.Pens >> Tool) & 1)
                RevivePenPick = Tool;
    }
    // A see-through veil: the fight goes on behind the sheet, its edges glow red while enemies are close.
    Box(0, 0, Width, Height, Ink.CopyWithNewOpacity(.3f));
    int32 Close = 0;
    for (const auto& Other : S->Units)
        Close += Other.Alive && Other.Kind == 2 && FVector::Dist2D(Other.Position, U.Position) < 700;
    if (Close > 0 || U.HP < U.MaxHP * .3f)
    {
        const float Pulse = .25f + .2f * FMath::Sin(static_cast<float>(Clock) * 7);
        for (int32 I = 0; I < 5; ++I)
        {
            const float T = (5 - I) * 10 * Scale;
            const FLinearColor Edge = Danger.CopyWithNewOpacity(Pulse * .22f);
            Box(0, 0, Width, T, Edge);
            Box(0, Height - T, Width, T, Edge);
            Box(0, 0, T, Height, Edge);
            Box(Width - T, 0, T, Height, Edge);
        }
        if (Close > 0)
        {
            const FText Warning = FArgs()(TEXT("Count"), Close).Text(TEXT("Revive.Danger"));
            Tape(FVector2D(Width * .5f, 30 * Scale), Measure(Warning, 16 * Scale, EFace::Heavy).X + 40 * Scale, 30 * Scale, 1, Paper);
            Label(Warning, Width * .5f, 19 * Scale, 16 * Scale, Danger, true, 0, EFace::Heavy);
        }
    }
    AddHit(FBox2D(FVector2D(0, 0), FVector2D(Width, Height)), GWDefenseAction::Blocker);
    const float Side = FMath::Min3(Height * .56f, Width * .36f, 520 * Scale);
    const float SX = (Width - Side) * .5f, SY = Height * .17f;
    ReviveSquare = FBox2D(FVector2D(SX, SY), FVector2D(SX + Side, SY + Side));
    TrackReviveStroke();
    const float ColW = FMath::Min(230 * Scale, SX - 40 * Scale), LX = SX - ColW - 24 * Scale, RX = SX + Side + 24 * Scale;
    const float SheetW = Side + 2 * (ColW + 24 * Scale), SheetX = Width * .5f - SheetW * .5f;
    // The heading over the sheet.
    {
        const float HeadH = 74 * Scale, HeadY = SY - HeadH - 10 * Scale;
        Card(SheetX, HeadY, SheetW, HeadH, Paper, Ink, 4900, 1.4f);
        Tape(FVector2D(SheetX + 30 * Scale, HeadY + 8 * Scale), 70 * Scale, 20 * Scale, -30, Biome(FMath::Max(0, RevivePenPick)));
        Lettering(GWText::Get(TEXT("Revive.Title")), Width * .5f, HeadY + 6 * Scale, 28 * Scale, Ink, 4901);
        // Without paper & pen in the hotbar the sheet still works, for twice the ink (2026-11-17).
        const bool Kit = KitSlotOf(U, ReviveKitGear) >= 0;
        Label(GWText::Get(Kit ? TEXT("Revive.Note") : TEXT("Revive.NoteNoKit")), Width * .5f, HeadY + 44 * Scale, 13 * Scale, Kit ? InkSoft : Danger, true,
              SheetW - 40 * Scale, EFace::Bold);
    }
    // The sheet: graph paper, the stroke in the chosen pen's colour.
    Card(SX, SY, Side, Side, Paper, Ink, 4902, 2.4f, 2 * Scale);
    for (int32 K = 1; K < 12; ++K)
    {
        Line(FVector2D(SX + Side * K / 12.f, SY + 4 * Scale), FVector2D(SX + Side * K / 12.f, SY + Side - 4 * Scale), Grid.CopyWithNewOpacity(.35f), 1);
        Line(FVector2D(SX + 4 * Scale, SY + Side * K / 12.f), FVector2D(SX + Side - 4 * Scale, SY + Side * K / 12.f), Grid.CopyWithNewOpacity(.35f), 1);
    }
    AddHit(ReviveSquare, GWDefenseAction::ReviveDraw);
    const FLinearColor Stroke = Biome(FMath::Max(0, RevivePenPick));
    auto At = [&](const FVector2D& Unit) { return FVector2D(SX + Unit.X * Side, SY + Unit.Y * Side); };
    // Measure the stroke when it changes (the host measures it again on its own).
    if (ReviveMeasured != ReviveStroke.Num())
    {
        ReviveMeasured = ReviveStroke.Num();
        std::vector<gwgame::Vec2> Points;
        Points.reserve(ReviveStroke.Num());
        for (const FVector2D& Point : ReviveStroke)
            Points.push_back({Point.X, Point.Y});
        const gwgame::ReviveShape Measured = gwgame::measureDrawing(Points);
        ReviveClosed = Measured.closed;
        ReviveShare = static_cast<float>(Measured.share);
        ReviveBonus = Measured.bonus;
    }
    for (int32 I = 1; I < ReviveStroke.Num(); ++I)
        Pen(At(ReviveStroke[I - 1]), At(ReviveStroke[I]), Stroke, 4 * Scale, 4910 + I, .15f);
    if (ReviveClosed && !ReviveDrawing && ReviveStroke.Num() >= 3 && FVector2D::Distance(ReviveStroke.Last(), ReviveStroke[0]) <= gwgame::ReviveClose + .02f)
        Pen(At(ReviveStroke.Last()), At(ReviveStroke[0]), Stroke, 4 * Scale, 4909, .15f);
    if (ReviveStroke.Num() > 0)
        Disc(At(ReviveStroke[0]), 6 * Scale, Stroke.CopyWithNewOpacity(.6f), 12);
    if (ReviveStroke.IsEmpty())
        Label(GWText::Get(TEXT("Revive.Empty")), SX + Side * .5f, SY + Side * .46f, 18 * Scale, Pencil, true, Side - 40 * Scale, EFace::Bold);
    // Who: the ghosts, on the left.
    Card(LX, SY, ColW, Side, Paper, Ink, 4920, 1.4f);
    Key(TEXT("Revive.Who"), LX + 14 * Scale, SY + 12 * Scale, 15 * Scale, InkSoft, false, ColW - 28 * Scale, EFace::Bold);
    for (int32 I = 0; I < Ghosts.Num() && I < 8; ++I)
        Button(GWDefenseAction::ReviveWho + I, TeammateName(S, *Ghosts[I]), LX + 12 * Scale, SY + 44 * Scale + I * 50 * Scale, ColW - 24 * Scale, 42 * Scale, true,
               Ghosts[I]->Id == ReviveTarget, Seat(Ghosts[I]->Seat));
    // Which pen: the ones this player may draw with, and what each one gives the body.
    Card(RX, SY, ColW, Side, Paper, Ink, 4921, 1.4f);
    Key(TEXT("Revive.Pen"), RX + 14 * Scale, SY + 12 * Scale, 15 * Scale, InkSoft, false, ColW - 28 * Scale, EFace::Bold);
    float PenY = SY + 44 * Scale;
    for (int32 Tool = 0; Tool < gwgame::BiomeCount; ++Tool)
        if ((U.Pens >> Tool) & 1)
        {
            Button(GWDefenseAction::RevivePen + Tool, GWText::Get(FString::Printf(TEXT("Trait.%d"), Tool)), RX + 46 * Scale, PenY, ColW - 58 * Scale, 40 * Scale, true,
                   Tool == RevivePenPick, Biome(Tool));
            BiomeIcon(Tool, FVector2D(RX + 26 * Scale, PenY + 20 * Scale), 28 * Scale, 4930 + Tool);
            PenY += 48 * Scale;
        }
    if (RevivePenPick >= 0)
        Paragraph(GWText::Get(FString::Printf(TEXT("Trait.%d.Note"), RevivePenPick)), RX + 14 * Scale, PenY + 6 * Scale, 12 * Scale, ColW - 28 * Scale, InkSoft);
    // What the drawing makes: area, ink, size, health and speed (design 5.2's formulas), and the shape's bonus.
    const int32 Cost = ReviveCost(U, ReviveShare);
    FText Line1, Line2;
    FLinearColor Tone1 = Ink;
    bool Valid = false;
    if (ReviveStroke.Num() >= 3 && !ReviveDrawing && !ReviveClosed)
    {
        Line1 = GWText::Get(TEXT("Revive.Open"));
        Tone1 = Danger;
    }
    else if (ReviveStroke.Num() >= 3 && ReviveClosed)
    {
        const double Share = FMath::Clamp(static_cast<double>(ReviveShare), gwgame::ReviveMinShare, 1.);
        FNumberFormattingOptions Two;
        Two.MinimumFractionalDigits = 2;
        Two.MaximumFractionalDigits = 2;
        Line1 = FArgs()(TEXT("Percent"), FMath::RoundToInt(ReviveShare * 100))(TEXT("Ink"), Cost)(TEXT("Have"), FMath::FloorToInt(U.Ink)).Text(TEXT("Revive.Area"));
        Line2 = FArgs()(TEXT("Size"), FText::AsNumber(gwgame::reviveScale(Share), &Two))(TEXT("Health"), FText::AsNumber(gwgame::reviveHealth(Share), &Two))(
                    TEXT("Speed"), FText::AsNumber(gwgame::reviveSpeed(Share), &Two))
                    .Text(TEXT("Revive.Body"));
        if (ReviveShare < gwgame::ReviveMinShare)
        {
            Line1 = GWText::Get(TEXT("Revive.Small"));
            Tone1 = Danger;
        }
        else if (U.Ink + 1e-3f < Cost)
        {
            Line1 = FArgs()(TEXT("Ink"), Cost)(TEXT("Have"), FMath::FloorToInt(U.Ink)).Text(TEXT("Revive.NoInk"));
            Tone1 = Danger;
        }
        else
            Valid = !ReviveDrawing;
        if (ReviveBonus > 0)
            Line2 = FText::FromString(Line2.ToString() + TEXT("  ·  ") + GWText::Get(FString::Printf(TEXT("Revive.Bonus.%d"), FMath::Clamp(ReviveBonus, 1, 3))).ToString());
    }
    else
        Line1 = GWText::Get(TEXT("Revive.Hint"));
    const float InfoY = SY + Side + 12 * Scale, InfoH = 118 * Scale;
    Card(SheetX, InfoY, SheetW, InfoH, Paper, Ink, 4940, 1.4f);
    Label(Line1, Width * .5f, InfoY + 10 * Scale, 16 * Scale, Tone1, true, SheetW - 40 * Scale, EFace::Heavy);
    if (!Line2.IsEmpty())
        Label(Line2, Width * .5f, InfoY + 34 * Scale, 14 * Scale, InkSoft, true, SheetW - 40 * Scale, EFace::Bold);
    const float ButtonY = InfoY + 62 * Scale, ButtonW = FMath::Min(210 * Scale, (SheetW - 80 * Scale) / 3);
    Button(GWDefenseAction::ReviveSubmit, GWText::Get(TEXT("Revive.Submit")), Width * .5f - ButtonW * 1.5f - 16 * Scale, ButtonY, ButtonW, 44 * Scale, Valid && ReviveTarget >= 0,
           false, Teal);
    Button(GWDefenseAction::ReviveRedraw, GWText::Get(TEXT("Revive.Redraw")), Width * .5f - ButtonW * .5f, ButtonY, ButtonW, 44 * Scale, ReviveStroke.Num() > 0, false, Marigold);
    Button(GWDefenseAction::ReviveCancel, GWText::Get(TEXT("Revive.Cancel")), Width * .5f + ButtonW * .5f + 16 * Scale, ButtonY, ButtonW, 44 * Scale, true, false, Coral);
}

// ---- The result card ----

void AGWHUD::ExpeditionResults(AGWGameState* S)
{
    auto* P = PlayerOwner->GetPlayerState<AGWPlayerState>();
    const FGWUnitView* Me = P ? S->FindUnit(P->UnitId) : nullptr;
    const float Age = Since(HashCombine(S->Seed, 0xE4E5u));
    Box(0, 0, Width, Height, Paper.CopyWithNewOpacity(.9f * GWEase::Clamp01(Age / .25f)));
    if (Age > .25f)
        Grained(0, 0, Width, Height);
    const bool Out = Me && Me->Extracted;
    const FLinearColor Stamp = Out ? Teal : InkSoft;
    const float W = FMath::Min(Width * .7f, 940 * Scale), H = FMath::Min(Height * .82f, 660 * Scale);
    const float X = (Width - W) * .5f, Y = (Height - H) * .45f;
    const auto Drop = DropIn(FVector2D(Width * .5f, Height * .5f), Age, .05f, 90);
    Card(X, Y, W, H, Paper, Ink, 5000, 2.4f, 2.6f * Scale);
    Tape(FVector2D(X + 30 * Scale, Y + 18 * Scale), 90 * Scale, 26 * Scale, -38, Stamp);
    Tape(FVector2D(X + W - 30 * Scale, Y + 18 * Scale), 90 * Scale, 26 * Scale, 38, Stamp);
    if (Out)
        Doodles(FBox2D(FVector2D(X + 20 * Scale, Y + 20 * Scale), FVector2D(X + W - 20 * Scale, Y + H * .3f)), 7, .5f);
    // The stamp: out alive (through the last door or an early one), or lost on the way.
    const FText Result = GWText::Get(Out ? (Me->FinalExit ? TEXT("Expedition.Result.Final") : TEXT("Expedition.Result.Early")) : TEXT("Expedition.Result.Lost"));
    const float StampSize = 64 * Scale;
    const FVector2D Size = Measure(Result, StampSize, EFace::Heavy);
    const FVector2D Center(X + W * .5f, Y + 74 * Scale);
    TArray<FVector2D> Border;
    const float A = FMath::DegreesToRadians(-5.f);
    for (const FVector2D& Corner : {FVector2D(-1, -1), FVector2D(1, -1), FVector2D(1, 1), FVector2D(-1, 1)})
    {
        const FVector2D Local(Corner.X * (Size.X * .5f + 30 * Scale), Corner.Y * (Size.Y * .5f + 12 * Scale));
        Border.Add(Center + FVector2D(Local.X * FMath::Cos(A) - Local.Y * FMath::Sin(A), Local.X * FMath::Sin(A) + Local.Y * FMath::Cos(A)));
    }
    {
        const auto Slam = StampIn(Center, Age, .35f);
        Polygon(Border, Stamp.CopyWithNewOpacity(.08f), Stamp, 4 * Scale, 5001);
        Lettering(Result, Center.X, Center.Y - Size.Y * .5f, StampSize, Stamp, 5002);
    }
    if (Out && Age > .55f && Once(HashCombine(S->Seed, 0xE4E6u)))
        Confetti(Center, 46, 1100, 8);
    // What this player takes home, then the run (clear of the stamp's border).
    float RowY = Y + 158 * Scale;
    if (Me)
    {
        const FText Take = Out ? FArgs()(TEXT("Count"), Me->Banked).Text(TEXT("Expedition.Banked")) : GWText::Get(TEXT("Expedition.LostAll"));
        Glyph(5, FVector2D(X + W * .5f - Measure(Take, 26 * Scale, EFace::Heavy).X * .5f - 24 * Scale, RowY + 16 * Scale), 30 * Scale, Out ? Marigold : Pencil, 5003);
        Label(Take, X + W * .5f, RowY, 26 * Scale, Ink, true, W * .8f, EFace::Heavy);
        RowY += 44 * Scale;
    }
    const FText Reached = FArgs()(TEXT("Stage"), static_cast<int32>(S->ExpStage))(TEXT("Floor"), static_cast<int32>(S->ExpFloor))(TEXT("Floors"), static_cast<int32>(S->ExpFloorsCleared))
                              .Text(TEXT("Expedition.Reached"));
    Label(Reached, X + W * .5f, RowY, 17 * Scale, InkSoft, true, W * .8f, EFace::Bold);
    RowY += 30 * Scale;
    const FText Tally = FArgs()(TEXT("Kills"), S->ExpKills)(TEXT("Elites"), S->ExpElites)(TEXT("Chests"), S->ExpChests)(TEXT("Fights"), S->ExpFights).Text(TEXT("Expedition.Tally"));
    Label(Tally, X + W * .5f, RowY, 15 * Scale, InkSoft, true, W * .85f);
    RowY += 36 * Scale;
    // Everyone's outcome, one row each.
    TArray<const FGWUnitView*> Party;
    for (const auto& U : S->Units)
        if (U.Human && U.Kind == 0)
            Party.Add(&U);
    Party.Sort([](const FGWUnitView& L, const FGWUnitView& R) { return L.Seat < R.Seat; });
    const float ListW = FMath::Min(W - 80 * Scale, 620 * Scale), ListX = X + (W - ListW) * .5f;
    const float Buttons = Y + H - 86 * Scale, RowH = FMath::Min(38 * Scale, (Buttons - 16 * Scale - RowY) / FMath::Max(1, Party.Num()));
    for (int32 I = 0; I < Party.Num(); ++I)
    {
        const FGWUnitView& U = *Party[I];
        const float LY = RowY + I * RowH;
        if (LY + RowH > Buttons - 8 * Scale)
            break;
        if (I % 2 == 0)
            Box(ListX, LY, ListW, RowH, PaperShade.CopyWithNewOpacity(.5f));
        Glyph(BodyGlyph(U.Shape), FVector2D(ListX + 20 * Scale, LY + RowH * .5f), FMath::Min(22 * Scale, RowH * .7f), Seat(U.Seat), 5010 + I);
        Label(TeammateName(S, U), ListX + 42 * Scale, LY + (RowH - 20 * Scale) * .5f, 15 * Scale, Ink, false, ListW * .4f, EFace::Bold);
        const FText Fate = GWText::Get(U.Extracted ? (U.FinalExit ? TEXT("Expedition.Tag.Out") : TEXT("Expedition.Tag.Early")) : TEXT("Expedition.Tag.Lost"));
        Label(Fate, ListX + ListW * .52f, LY + (RowH - 18 * Scale) * .5f, 13 * Scale, U.Extracted ? Teal : Pencil, false, ListW * .25f, EFace::Heavy);
        const FText Count = FText::AsNumber(U.Extracted ? U.Banked : 0);
        const float CountW = Measure(Count, 16 * Scale, EFace::Heavy).X;
        Label(Count, ListX + ListW - 12 * Scale - CountW, LY + (RowH - 20 * Scale) * .5f, 16 * Scale, Ink, false, 0, EFace::Heavy);
        Glyph(5, FVector2D(ListX + ListW - 26 * Scale - CountW, LY + RowH * .5f), 15 * Scale, U.Extracted ? Marigold : Pencil, 5030 + I);
    }
    Button(220, GWText::Get(TEXT("Menu.Lobby")), X + W * .08f, Buttons, W * .4f, 58 * Scale, P && P->Identity == S->HostIdentity, false, Teal);
    Button(214, GWText::Get(TEXT("Menu.Leave")), X + W * .52f, Buttons, W * .4f, 58 * Scale, true, false, Coral);
}
