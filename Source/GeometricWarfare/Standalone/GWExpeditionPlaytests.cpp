#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GWController.h"
#include "GWGameInstance.h"
#include "GWGameMode.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWPlayerState.h"
#include "GWProfile.h"
#include "GWSettings.h"
#include "GWText.h"
#include "GWWorld.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// The expedition's rendered acceptance scenes (design v2.1). GW.Expedition.Scenes (one process, rendered): 1-1's start
// room with the hotbar, the minimap and the newcomer's vending machine; a fight with its doors shut; the room cleared,
// its chests and loot on the floor; the portal room's ring of chests clear of the portal; a shop's shelf with the E
// prompt; the shop's buyer and its trade sheet (zh, de); Tab's bag sheet with quality pieces in the bag (zh, de); a
// fallen teammate's ghost and the revive
// sheet with a circle drawn on it (submitted: the ghost stands up again); the local player's own ghost card; the last
// boss; its final door; and the result card. The second player is a simulation seat without a machine of its own.
// Screenshots land in Saved/ExpeditionVerification; real profiles wait as *.sav.playtest meanwhile.
namespace
{
using namespace gwgame;
class FExpeditionScenes : public IAutomationLatentCommand
{
public:
    explicit FExpeditionScenes(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds())
    {
    }
    bool Update() override
    {
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        auto* GI = W ? W->GetGameInstance<UGWGameInstance>() : nullptr;
        auto* H = C ? Cast<AGWHUD>(C->GetHUD()) : nullptr;
        const double Now = FPlatformTime::Seconds();
        if (Now - Start > 180)
        {
            Test->AddError(TEXT("Expedition scene timeout"));
            RestoreProfiles(GI);
            return true;
        }
        Battle* B = S && S->InMatch ? S->Simulation.Get() : nullptr;
        // The local player is kept standing between the scenes that need it down (the floor's enemies hit for real).
        if (B && P && KeepAlive && B->expeditionMode())
            if (Unit* Me = B->find(P->UnitId); Me && Me->alive)
                Me->hp = Me->maxHp;
        if (!C || !G || !S || !P || !GI || !H || !C->WorldView || Now < Next)
            return false;
        auto Capture = [&](const TCHAR* Name)
        {
            const FString Path = FPaths::ProjectSavedDir() / TEXT("ExpeditionVerification") / (FString(Name) + TEXT(".png"));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
            FScreenshotRequest::RequestScreenshot(Path, false, false);
            ++Shots;
        };
        auto Wait = [&](double Seconds)
        {
            Next = Now + Seconds;
            ++Stage;
            return false;
        };
        auto Me = [&]() { return B ? B->find(P->UnitId) : nullptr; };
        auto Mate = [&]() { return B ? B->find(MateId) : nullptr; };
        auto RoomOf = [&](RoomKind Kind)
        {
            for (int32 I = 0; B && I < static_cast<int32>(B->expedition.rooms.size()); ++I)
                if (B->expedition.rooms[static_cast<size_t>(I)].kind == Kind)
                    return I;
            return -1;
        };
        auto Middle = [&](int32 Room)
        {
            const DungeonRoom& R = B->expedition.rooms[static_cast<size_t>(Room)];
            return Vec2{(R.x + R.w * .5) * B->maze.cellSize, (R.y + R.h * .5) * B->maze.cellSize};
        };
        auto Run = [&](double Seconds)
        {
            for (int32 K = 0; K < FMath::CeilToInt(Seconds * 60); ++K)
            {
                B->advance(1. / 60);
                if (Unit* U = Me(); U && U->alive && KeepAlive)
                    U->hp = U->maxHp;
            }
            S->Publish();
        };
        // Fells every enemy in a room, wave after wave, until it is cleared (the local player gets the kills).
        auto ClearRoom = [&](int32 Room)
        {
            for (int32 Round = 0; Round < 40 && B->expedition.rooms[static_cast<size_t>(Room)].state != RoomState::Cleared; ++Round)
            {
                TArray<int32> Targets;
                for (const Unit& U : B->units)
                    if (Battle::enemy(U) && U.room == Room)
                        Targets.Add(U.id);
                for (int32 Id : Targets)
                    B->damageUnit(P->UnitId, Id, 1e12);
                Run(1.2);
            }
            return B->expedition.rooms[static_cast<size_t>(Room)].state == RoomState::Cleared;
        };
        // Brings a player down for good (a pencil trait or a note would catch the first blow).
        auto Fell = [&](int32 Id)
        {
            for (int32 K = 0; K < 6; ++K)
                if (Unit* U = B->find(Id); U && U->alive)
                {
                    U->dashTime = 0;
                    U->kit.note = false;
                    B->damageUnit(-1, Id, 1e12);
                    B->advance(1. / 60);
                }
            S->Publish();
            const Unit* U = B->find(Id);
            return U && !U->alive && U->kit.ghost;
        };
        // A circle on the emergency sheet: 64 points round its middle, a fifth of the sheet across.
        auto Circle = []()
        {
            TArray<FVector2D> Points;
            for (int32 K = 0; K < 64; ++K)
                Points.Add(FVector2D(.5f + .3f * FMath::Cos(K * 2 * PI / 64), .5f + .3f * FMath::Sin(K * 2 * PI / 64)));
            return Points;
        };
        switch (Stage)
        {
        case 0:
            GWText::SetCulture(TEXT("zh-Hans"));
            if (S->InMatch)
            {
                S->Finished = true;
                G->ReturnToLobby(C);
            }
            for (int32 Slot = 0; Slot < UGWProfileSave::SlotCount; ++Slot)
            {
                const FString Path = UGWProfileSave::SlotPath(Slot), Backup = Path + TEXT(".playtest");
                if (IFileManager::Get().FileExists(*Backup))
                    UGWProfileSave::Delete(Slot); // left over from an interrupted playtest
                else if (IFileManager::Get().FileExists(*Path))
                    IFileManager::Get().Move(*Backup, *Path, true);
            }
            Parked = true;
            OriginalSlot = GI->ProfileSlot();
            GI->SelectProfile(0);
            return Wait(1.5);
        case 1:
        {
            Test->TestTrue(TEXT("An expedition opens"), G->BeginExpedition(C, 0));
            B = S->Simulation.Get();
            Test->TestTrue(TEXT("It is replicated on 1-1"), S->Expedition() && S->ExpStage == 1 && S->ExpFloor == 1 && !S->Defense());
            Test->TestTrue(TEXT("The starting gun sits in slot 1"), Me() && Me()->kit.slots[0].kind == SlotKind::Weapon && Me()->kit.pouch == 0);
            Test->TestEqual(TEXT("1-1 is start, one fight and the portal"), static_cast<int32>(B->expedition.rooms.size()), 3);
            // A friend whose machine is not in this process: a seat in the simulation.
            MateId = B->joinHuman(9001);
            Test->TestTrue(TEXT("A second player joins the run"), Mate() != nullptr && Mate()->human);
            KeepAlive = true;
            S->Publish();
            return Wait(2.5);
        }
        case 2:
            Capture(TEXT("expedition-start-zh"));
            return Wait(.4);
        case 3:
        {
            // Both walk into the fight: everyone is in, so the doors come down at once.
            FightRoom = RoomOf(RoomKind::Combat);
            Test->TestTrue(TEXT("1-1 has its fight"), FightRoom >= 0);
            if (FightRoom < 0)
                return Wait(.05);
            const Vec2 Mid = Middle(FightRoom);
            Me()->position = Mid + Vec2{-90, 40};
            Mate()->position = Mid + Vec2{90, 60};
            Run(1.4);
            Test->TestTrue(TEXT("The fight shuts its doors"), B->expedition.rooms[static_cast<size_t>(FightRoom)].state == RoomState::Locked);
            return Wait(1.3);
        }
        case 4:
            Capture(TEXT("expedition-fight-zh"));
            return Wait(.4);
        case 5:
        {
            Test->TestTrue(TEXT("The room clears"), FightRoom >= 0 && ClearRoom(FightRoom));
            Test->TestTrue(TEXT("Its loot lies on the floor"), !B->expedition.loot.empty());
            int32 Chests = 0;
            for (const Chest& Ch : B->expedition.chests)
                Chests += Ch.room == FightRoom && Ch.grade == static_cast<int>(ChestGrade::Clear);
            // One per player the floor began with (the friend here joined after 1-1 was laid out).
            Test->TestEqual(TEXT("A cleared fight leaves a chest for each player"), Chests, B->expedition.party);
            return Wait(1.2);
        }
        case 6:
            Capture(TEXT("expedition-clear-zh"));
            return Wait(.4);
        case 7:
        {
            // The portal room: its chests stand in a ring well clear of the portal.
            PortalRoom = RoomOf(RoomKind::Portal);
            Test->TestTrue(TEXT("1-1 ends in the portal room"), PortalRoom >= 0);
            if (PortalRoom < 0)
                return Wait(.05);
            const Vec2 Mid = Middle(PortalRoom);
            Me()->position = Mid + Vec2{-260, 380};
            Mate()->position = Mid + Vec2{260, 380};
            Run(1.4);
            Test->TestTrue(TEXT("The portal room clears"), ClearRoom(PortalRoom));
            Test->TestTrue(TEXT("Its portal opens"), B->expedition.portalOpen);
            bool Clear = true;
            for (const Chest& Ch : B->expedition.chests)
                if (Ch.room == PortalRoom)
                    Clear &= (Ch.position - B->expedition.portal).length() > PortalRadius + 150;
            Test->TestTrue(TEXT("No chest stands on the portal"), Clear);
            Me()->position = Mid + Vec2{-300, 430};
            Mate()->position = Mid + Vec2{300, 430};
            Me()->kit.coins = 128;
            Run(.6);
            return Wait(1.2);
        }
        case 8:
            Test->TestEqual(TEXT("Nobody stepped through"), static_cast<int32>(S->ExpFloor), 1);
            Capture(TEXT("expedition-portal-zh"));
            return Wait(.4);
        case 9:
        {
            // 2026-11-17: by the portal room's chest (once the portal's banner has gone), a foe already bleeding from
            // thumbtacks across the floor.
            Chest* Box = nullptr;
            for (Chest& Ch : B->expedition.chests)
                if (Ch.room == PortalRoom && !Ch.opened)
                    Box = &Ch;
            Test->TestTrue(TEXT("The portal room keeps a chest to open"), Box != nullptr);
            if (!Box)
                return Wait(.05);
            ChestAt = Box->position;
            B->expedition.loot.erase(std::remove_if(B->expedition.loot.begin(), B->expedition.loot.end(),
                                                    [](const GroundLoot& L) { return !walkOverLoot(L.kind); }),
                                     B->expedition.loot.end());
            Me()->position = ChestAt + Vec2{0, 100};
            Mate()->position = ChestAt + Vec2{-330, 140};
            Unit Target;
            Target.id = 98000;
            Target.team = 2;
            Target.kind = UnitKind::Monster;
            Target.breed = static_cast<int>(Foe::Shield);
            Target.room = PortalRoom;
            Target.shape = foeInfo(Target.breed).shape;
            Target.radius = foeInfo(Target.breed).radius;
            Target.hp = Target.maxHp = 1e7;
            Target.position = B->maze.unstick(ChestAt + Vec2{-330, -170}, Target.radius + 4);
            Target.aim = {0, 1};
            Target.stun = 30;
            B->units.push_back(Target);
            S->Publish();
            return Wait(1.8);
        }
        case 10:
        {
            // E opens the chest: its contents fly out on arcs; the friend's thumbtacks (spiked rounds) fly at the foe.
            Chest* Box = nullptr;
            for (Chest& Ch : B->expedition.chests)
                if (Ch.room == PortalRoom && !Ch.opened && (Ch.position - ChestAt).length() < 1)
                    Box = &Ch;
            Me()->position = ChestAt + Vec2{0, 100};
            const int32 Opened = Box ? B->interact(P->UnitId) : -1;
            Test->TestTrue(TEXT("E opens the chest"), Box && Opened > 0 && Box->opened);
            int32 Up = 0;
            for (const GroundLoot& L : B->expedition.loot)
                Up += L.airborne();
            Test->TestTrue(TEXT("Its contents fly out on arcs"), Up >= 3);
            if (Unit* Target = B->find(98000))
            {
                Target->bleedTime = 8;
                Target->bleedOwner = MateId;
                Kit& M = Mate()->kit;
                M.slots[0] = {SlotKind::Weapon, 2, 1, 8, 0};
                M.slots[0].variant = 1;
                M.slots[1] = HotSlot{};
                B->syncHand(*Mate());
                Mate()->aim = (Target->position - Mate()->position).normalized();
                Mate()->cooldown = Mate()->reload = 0;
                B->fire(*Mate());
            }
            S->Publish();
            return Wait(.3);
        }
        case 11:
            Capture(TEXT("expedition-toss-zh"));
            return Wait(.4);
        case 12:
        {
            // The hands: a wax-seal launcher takes both; its aiming preview while the trigger is held; R pours ink into
            // the hurt friend standing by.
            B->units.erase(std::remove_if(B->units.begin(), B->units.end(), [](const Unit& U) { return U.id == 98000; }), B->units.end());
            Kit& K = Me()->kit;
            K.slots[0] = {SlotKind::Weapon, RocketWeapon, 1, 4, 0};
            K.slots[0].variant = 6;
            K.slots[1] = HotSlot{};
            K.slots[2] = {SlotKind::Item, static_cast<int>(Gear::Bandage), 2, 0, 0};
            B->selectSlot(P->UnitId, 0);
            Test->TestTrue(TEXT("A long gun takes both hands"), Me()->weapon == RocketWeapon && Me()->kit.slots[1].empty() && !Me()->dual);
            Me()->ink = Me()->maxInk;
            Me()->aim = {1, .25};
            Mate()->position = Me()->position + Vec2{-210, 30};
            Mate()->hp = Mate()->maxHp * .4;
            C->RocketOverride = .12f;
            C->MendOverride = true;
            // A heart on the floor nearby (2026-11-17), left there by a player at full health.
            Me()->hp = Me()->maxHp;
            B->scatterLoot(LootKind::Heart, 0, static_cast<int>(HeartHeal), Me()->position + Vec2{150, 150}, -1, 0);
            S->Publish();
            return Wait(1.1);
        }
        case 13:
            Test->TestTrue(TEXT("R pours ink into the friend in the circle"), Me()->mendOn == Me()->id && Mate()->hp > Mate()->maxHp * .4 && Mate()->mendSlow > 0);
            Capture(TEXT("expedition-aim-zh"));
            return Wait(.4);
        case 14:
            C->RocketOverride = -1;
            C->MendOverride = false;
            Me()->kit.slots[0] = {SlotKind::Weapon, 0, 1, 10, 0};
            Me()->kit.slots[2] = HotSlot{};
            B->syncHand(*Me());
            return Wait(.3);
        case 15:
        {
            // A floor with a shop (a boss floor always has one): the local player at its shelf with a full pouch.
            const int32 Floors[][2] = {{1, 3}, {2, 4}, {3, 5}, {4, 6}};
            ShopRoom = -1;
            for (const auto& At : Floors)
                if (ShopRoom < 0 && B->expeditionJump(At[0], At[1]))
                    ShopRoom = RoomOf(RoomKind::Shop);
            Test->TestTrue(TEXT("A shop stands on a boss floor"), ShopRoom >= 0);
            if (ShopRoom < 0)
                return Wait(.05);
            int32 Wares = 0, Buyers = 0;
            const Offer* First = nullptr;
            for (const Offer& O : B->expedition.offers)
                if (O.room == ShopRoom)
                {
                    if (O.kind == ServiceLoot && O.what == static_cast<int>(Service::Trade))
                    {
                        ++Buyers;
                        BuyerAt = O.position;
                        continue;
                    }
                    ++Wares;
                    First = First ? First : &O;
                }
            Test->TestEqual(TEXT("Its shelf holds four wares"), Wares, 4);
            Test->TestEqual(TEXT("And its buyer stands at a counter"), Buyers, 1);
            Me()->kit.pouch = 60;
            Me()->kit.coins = 1240;
            // In front of the first ware, on the room's side of the shelf.
            if (First)
            {
                const Vec2 Toward = Middle(ShopRoom) - First->position;
                Me()->position = First->position + (Toward.length() > 1 ? Toward.normalized() * 100 : Vec2{0, 100});
            }
            Mate()->position = Middle(ShopRoom) + Vec2{-160, 120};
            S->Publish();
            return Wait(1.2);
        }
        case 16:
            Capture(TEXT("expedition-shop-zh"));
            return Wait(.4);
        case 17:
        {
            // A kit worth selling, hotbar and bag: then E at the buyer opens the trade sheet, the bag's sniper picked.
            Kit& K = Me()->kit;
            K.slots[0] = {SlotKind::Weapon, 0, 1, 14, 0};
            K.slots[0].variant = StaplerVariant;
            K.slots[1] = {SlotKind::Weapon, 0, 1, 10, 0};
            K.slots[2] = {SlotKind::Item, static_cast<int>(Gear::Bandage), 2, 0, 0};
            K.slots[3] = {SlotKind::Item, static_cast<int>(Gear::Highlighter), 1, 0, 0};
            K.pack[0] = {SlotKind::Weapon, 3, 1, 6, 0};
            K.pack[0].variant = 4;
            K.pack[1] = {SlotKind::Item, static_cast<int>(Gear::Phoenix), 1, 0, 0};
            K.pack[2] = {SlotKind::Item, static_cast<int>(Gear::Refill), 0, 0, 0};
            K.pack[3] = {SlotKind::Weapon, 4, 1, 60, 0};
            K.pack[3].variant = 3;
            B->syncHand(*Me());
            const Vec2 Toward = (Middle(ShopRoom) - BuyerAt).normalized();
            Me()->position = BuyerAt + (Toward.lengthSquared() > .5 ? Toward : Vec2{0, -1}) * (TraderRadius + 75);
            Mate()->position = Me()->position + Vec2{-140, 40};
            S->Publish();
            return Wait(.8);
        }
        case 18:
            // The buyer at its counter, and E's prompt over the player.
            Capture(TEXT("expedition-buyer-zh"));
            return Wait(.4);
        case 19:
            Test->TestTrue(TEXT("E at the buyer opens the trade sheet"), H->OpenTrade() && H->TradeOpen());
            H->PickKitSlot(5);
            return Wait(1.2);
        case 20:
            Capture(TEXT("expedition-trade-zh"));
            return Wait(.4);
        case 21:
            GWText::SetCulture(TEXT("de"));
            return Wait(1.2);
        case 22:
            Capture(TEXT("expedition-trade-de"));
            return Wait(.4);
        case 23:
        {
            GWText::SetCulture(TEXT("zh-Hans"));
            // Selling the picked sniper pays coins, never stickers.
            H->Press(GWDefenseAction::KitSellPick);
            return Wait(.6);
        }
        case 24:
        {
            Test->TestTrue(TEXT("The sale paid coins"), Me()->kit.coins > 1240 && Me()->kit.pouch == 60 && Me()->kit.pack[0].kind == SlotKind::Empty);
            H->ToggleKit();
            // A full kit: the thumbtack shotgun in both hands, the revive kit, the eraser, three sticky notes; three looks
            // worn; the bag.
            Kit& K = Me()->kit;
            K.slots[0] = {SlotKind::Weapon, 2, 1, gunMagazine(2, 1, false), 0};
            K.slots[0].variant = 1;
            K.slots[1] = HotSlot{};
            K.slots[2] = {SlotKind::Item, static_cast<int>(Gear::ReviveKit), 1, 0, 0};
            K.slots[3] = {SlotKind::Item, static_cast<int>(Gear::Eraser), 1, 0, 0};
            K.slots[4] = {SlotKind::Item, static_cast<int>(Gear::StickyNote), 3, 0, 0};
            K.pack[0] = {SlotKind::Weapon, 4, 1, 75, 0};
            K.pack[0].variant = 7;
            K.pack[4] = {SlotKind::Weapon, 5, 1, 4, 0};
            K.pack[4].variant = 6;
            K.wear[0] = 8;
            K.wear[1] = 1;
            K.wear[2] = 2;
            B->syncHand(*Me());
            S->Publish();
            H->ToggleKit();
            H->PickKitSlot(5);
            return Wait(1);
        }
        case 25:
            Test->TestTrue(TEXT("Tab's kit sheet is open"), H->KitOpen() && !H->TradeOpen());
            Capture(TEXT("expedition-kit-zh"));
            return Wait(.4);
        case 26:
            // The same sheet in German, the longest of the four languages.
            GWText::SetCulture(TEXT("de"));
            return Wait(1.2);
        case 27:
            Capture(TEXT("expedition-kit-de"));
            return Wait(.4);
        case 28:
            GWText::SetCulture(TEXT("zh-Hans"));
            return Wait(.8);
        case 29:
        {
            H->ToggleKit();
            // The friend falls in the quiet start room (the shop's shelves would take E first): a ghost where they
            // stood, their kit on the floor round it.
            Mate()->position = Middle(0) + Vec2{110, 0};
            Test->TestTrue(TEXT("A fall leaves a ghost"), Fell(MateId));
            Me()->ink = Me()->maxInk;
            // Paper & pen put aside for now (2026-11-17): E at the outline opens the sheet anyway, for twice the ink.
            RevKit = Me()->kit.slots[2];
            Me()->kit.slots[2] = HotSlot{};
            Me()->position = Mate()->position + Vec2{-110, 0};
            // The friend's spilled kit swept aside, so E reaches the outline (things on the floor come first).
            B->expedition.loot.clear();
            B->syncHand(*Me());
            S->Publish();
            return Wait(.6);
        }
        case 30:
        {
            Test->TestTrue(TEXT("E at a fallen friend's outline opens the sheet without paper & pen"), H->Interact() && H->ReviveOpen());
            H->DrawReviveStroke(Circle());
            return Wait(1.2);
        }
        case 31:
            Capture(TEXT("expedition-revive-zh"));
            return Wait(.4);
        case 32:
            // The same sheet in German, the longest of the four languages.
            GWText::SetCulture(TEXT("de"));
            return Wait(1.2);
        case 33:
            Capture(TEXT("expedition-revive-de"));
            return Wait(.4);
        case 34:
            GWText::SetCulture(TEXT("zh-Hans"));
            // Paper & pen back in the hotbar: the drawing is sent at the kit's price.
            Me()->kit.slots[2] = RevKit;
            S->Publish();
            return Wait(.8);
        case 35:
        {
            InkBefore = Me()->ink;
            H->Press(GWDefenseAction::ReviveSubmit);
            return Wait(.6);
        }
        case 36:
        {
            Test->TestTrue(TEXT("The drawing brings the ghost back"), Mate() && Mate()->alive && !Mate()->kit.ghost);
            Test->TestTrue(TEXT("It cost ink and the revive kit"), Me()->ink < InkBefore && Me()->kit.slots[2].kind == SlotKind::Empty);
            Test->TestTrue(TEXT("The body is bare-handed, with the pen's trait"), Mate() && Mate()->weapon == FistWeapon && Mate()->kit.trait >= 0);
            Test->TestFalse(TEXT("The sheet folds away once it is sent"), H->ReviveOpen());
            // Now the local player falls (the friend stands): the ghost's card.
            KeepAlive = false;
            Test->TestTrue(TEXT("The local player can fall too"), Fell(P->UnitId));
            return Wait(1.4);
        }
        case 37:
            Test->TestTrue(TEXT("Down, the camera follows the friend still standing"), C->SpectateId == MateId && C->SpectateCount == 1);
            Capture(TEXT("expedition-ghost-zh"));
            return Wait(.4);
        case 38:
        {
            // The friend draws the local player back, and the party jumps to the last boss's floor.
            Mate()->kit.slots[2] = {SlotKind::Item, static_cast<int>(Gear::ReviveKit), 1, 0, 0};
            Mate()->ink = Mate()->maxInk;
            std::vector<Vec2> Drawing;
            for (const FVector2D& Point : Circle())
                Drawing.push_back({Point.X, Point.Y});
            const uint32_t Pens = B->revivePens(MateId);
            int32 Pen = 0;
            while (Pen < BiomeCount && !((Pens >> Pen) & 1))
                ++Pen;
            Test->TestTrue(TEXT("A friend's drawing brings the local player back"), B->reviveDrawn(MateId, P->UnitId, Drawing, Pen) && Me()->alive);
            KeepAlive = true;
            Test->TestTrue(TEXT("The last floor opens"), B->expeditionJump(4, 6));
            BossRoom = RoomOf(RoomKind::Boss);
            Test->TestTrue(TEXT("It ends in the boss's room"), BossRoom >= 0);
            if (BossRoom < 0)
                return Wait(.05);
            const Vec2 Mid = Middle(BossRoom);
            Me()->position = Mid + Vec2{-150, 420};
            Mate()->position = Mid + Vec2{150, 420};
            Run(2.5);
            Test->TestTrue(TEXT("The last boss stands"), B->expedition.bossId > 0 && B->find(B->expedition.bossId) && B->find(B->expedition.bossId)->alive);
            return Wait(1.5);
        }
        case 39:
            Capture(TEXT("expedition-boss-zh"));
            return Wait(.4);
        case 40:
        {
            Test->TestTrue(TEXT("Felling the last boss clears its room"), BossRoom >= 0 && ClearRoom(BossRoom));
            bool Final = false;
            for (const auto& Door : B->expedition.exits)
                Final |= Door.final;
            Test->TestTrue(TEXT("The final door opens"), Final);
            int32 BossChests = 0, Best = 0;
            for (const Chest& Ch : B->expedition.chests)
                if (Ch.grade == static_cast<int>(ChestGrade::Boss))
                {
                    ++BossChests;
                    Best = FMath::Max(Best, Ch.quality);
                }
            Test->TestTrue(TEXT("The boss leaves a chest per player, purple or better"), BossChests == 2 && Best >= 3);
            Me()->kit.pouch += 240;
            S->Publish();
            return Wait(1.5);
        }
        case 41:
            Capture(TEXT("expedition-final-zh"));
            return Wait(.4);
        case 42:
        {
            // Out through the final door; the friend falls behind, and the run is over.
            for (const auto& Door : B->expedition.exits)
                if (Door.final)
                    Me()->position = Door.at;
            Run(.3);
            Test->TestTrue(TEXT("Walking through the final door banks the pouch and the clear bonus"), Me()->kit.extracted && Me()->kit.finalExit && Me()->kit.banked > 240);
            Test->TestEqual(TEXT("Coins stay in the dungeon"), Me()->kit.coins, 0);
            Banked = Me()->kit.banked;
            Fell(MateId);
            Run(.1);
            G->Tick(.01f);
            S->Publish();
            return Wait(1.6);
        }
        case 43:
            Test->TestTrue(TEXT("The run ends with someone out"), S->Finished && S->Winner == 1);
            Test->TestTrue(TEXT("The host paid out what was banked"), S->StickersEarned >= Banked && Banked > 0);
            Capture(TEXT("expedition-result-zh"));
            return Wait(.4);
        default:
            S->Finished = true;
            G->ReturnToLobby(C);
            H->SetPage(AGWHUD::EPage::Main);
            RestoreProfiles(GI);
            Test->TestEqual(TEXT("Captured every expedition scene"), Shots, 18);
            return true;
        }
    }

private:
    void RestoreProfiles(UGWGameInstance* GI)
    {
        if (!Parked)
            return;
        Parked = false;
        for (int32 Slot = 0; Slot < UGWProfileSave::SlotCount; ++Slot)
        {
            const FString Path = UGWProfileSave::SlotPath(Slot), Backup = Path + TEXT(".playtest");
            UGWProfileSave::Delete(Slot);
            if (IFileManager::Get().FileExists(*Backup))
                IFileManager::Get().Move(*Path, *Backup, true);
        }
        if (auto* Settings = UGWUserSettings::Get())
        {
            Settings->ProfileSlot = OriginalSlot;
            Settings->SaveSettings();
        }
        if (GI)
            GI->ReloadProfile();
    }
    FAutomationTestBase* Test;
    double Start = 0, Next = 0, InkBefore = 0;
    int32 Stage = 0, Shots = 0, OriginalSlot = 0, MateId = -1, FightRoom = -1, ShopRoom = -1, BossRoom = -1, PortalRoom = -1, Banked = 0;
    gwgame::Vec2 ChestAt{};
    bool Parked = false, KeepAlive = false;
    Vec2 BuyerAt{};
    HotSlot RevKit{};
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWExpeditionScenes, "GW.Expedition.Scenes", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWExpeditionScenes::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FExpeditionScenes(this));
    return true;
}

// The bestiary (M4). GW.Expedition.Bestiary (one process, rendered): each pen's paper and terrain in a combat room of
// that pen's stage, with its boss (tools and all), its five foes and its two elites lined up still (the AI is off): the
// pencil sketchbook adds the thief and the mash-up, the watercolour the rainbow's three spirits, the rice paper the
// sage's two guards. Then the ink sage's second form, the cursive dragon, drawn along its wake. Screenshots land in
// Saved/ExpeditionVerification as bestiary-<pen>.png and bestiary-dragon.png.
namespace
{
using namespace gwgame;
class FExpeditionBestiary : public IAutomationLatentCommand
{
public:
    explicit FExpeditionBestiary(FAutomationTestBase* T) : Test(T), Start(FPlatformTime::Seconds())
    {
    }
    bool Update() override
    {
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        auto* GI = W ? W->GetGameInstance<UGWGameInstance>() : nullptr;
        auto* H = C ? Cast<AGWHUD>(C->GetHUD()) : nullptr;
        const double Now = FPlatformTime::Seconds();
        if (Now - Start > 150)
        {
            Test->AddError(TEXT("Bestiary timeout"));
            Restore(GI);
            return true;
        }
        Battle* B = S && S->InMatch ? S->Simulation.Get() : nullptr;
        if (B && P && B->expeditionMode())
            if (Unit* Me = B->find(P->UnitId); Me && Me->alive)
                Me->hp = Me->maxHp;
        if (!C || !G || !S || !P || !GI || !H || !C->WorldView || Now < Next)
            return false;
        auto Wait = [&](double Seconds)
        {
            Next = Now + Seconds;
            ++Stage;
            return false;
        };
        const double Cell = B ? B->maze.cellSize : DungeonCell;
        auto Middle = [&](int32 Room)
        {
            const DungeonRoom& R = B->expedition.rooms[static_cast<size_t>(Room)];
            return Vec2{(R.x + R.w * .5) * Cell, (R.y + R.h * .5) * Cell};
        };
        // The nearest open spot to a slot (the pencil's pillars and the fountain's fixed ones stand about the room).
        auto Free = [&](Vec2 At, double Radius)
        {
            for (int32 Ring = 0; Ring < 8; ++Ring)
                for (int32 K = 0; K < (Ring ? 8 : 1); ++K)
                {
                    const double A = K * Pi / 4;
                    const Vec2 Try = At + Vec2{std::cos(A), std::sin(A)} * (Ring * 45.);
                    if (B->maze.walkable(Try, Radius + 10))
                        return Try;
                }
            return At;
        };
        auto Place = [&](Foe Kind, Vec2 At)
        {
            const FoeInfo& Info = foeInfo(static_cast<int>(Kind));
            Unit M;
            M.id = 90000 + NextFoe++;
            M.team = 2;
            M.kind = UnitKind::Monster;
            M.breed = static_cast<int>(Kind);
            M.room = Room;
            M.shape = Info.shape;
            M.elite = Info.rank == FoeRank::Elite;
            M.radius = Info.radius;
            M.parts = Info.parts;
            M.hp = M.maxHp = Info.health;
            M.position = Free(At, Info.radius);
            // Facing the camera (the bottom of the page).
            M.aim = M.actAt = {0, 1};
            if (Kind == Foe::Eel)
                M.acts = 5;
            if (Kind == Foe::Thief)
                M.actClock = ThiefSeconds;
            if (Kind == Foe::Mirror)
                M.act = static_cast<int>(FoeAct::Guard);
            if (Kind == Foe::Pinwheel)
                M.act = static_cast<int>(FoeAct::Spin);
            B->units.push_back(M);
            return M.id;
        };
        auto Sweep = [&]()
        {
            B->units.erase(std::remove_if(B->units.begin(), B->units.end(), [](const Unit& U) { return U.team == 2; }), B->units.end());
            B->projectiles.clear();
        };
        // Every placed foe still on the page (decoded from what the game state replicates).
        auto OnPage = [&]()
        {
            int32 Count = 0;
            for (const FGWUnitView& U : S->Units)
                Count += U.Kind == 2 && U.Breed >= 0 && U.Breed < FoeCount && U.Id >= 90000;
            return Count;
        };
        static const TCHAR* PenNames[BiomeCount] = {TEXT("pencil"), TEXT("watercolor"), TEXT("fountain"), TEXT("ballpoint"), TEXT("brush")};
        if (Stage == 0)
        {
            GWText::SetCulture(TEXT("zh-Hans"));
            if (S->InMatch)
            {
                S->Finished = true;
                G->ReturnToLobby(C);
            }
            for (int32 Slot = 0; Slot < UGWProfileSave::SlotCount; ++Slot)
            {
                const FString Path = UGWProfileSave::SlotPath(Slot), Backup = Path + TEXT(".playtest");
                if (IFileManager::Get().FileExists(*Backup))
                    UGWProfileSave::Delete(Slot);
                else if (IFileManager::Get().FileExists(*Path))
                    IFileManager::Get().Move(*Backup, *Path, true);
            }
            Parked = true;
            OriginalSlot = GI->ProfileSlot();
            GI->SelectProfile(0);
            return Wait(1.5);
        }
        if (Stage == 1)
        {
            Test->TestTrue(TEXT("An expedition opens"), G->BeginExpedition(C, 0));
            B = S->Simulation.Get();
            // Still lifes: nothing moves, fires or spawns on its own.
            B->config.aiEnabled = false;
            return Wait(1.2);
        }
        // Three stages a pen: lay its page out, capture it, then (the brush) the dragon.
        const int32 Pen = (Stage - 2) / 3, Step = (Stage - 2) % 3;
        if (Pen < BiomeCount && Step == 0)
        {
            Sweep();
            B->expedition.biomes[0] = static_cast<PenBiome>(Pen);
            Test->TestTrue(TEXT("The pen's page opens"), B->expeditionJump(1, 2));
            Sweep();
            Room = -1;
            for (int32 I = 0; I < static_cast<int32>(B->expedition.rooms.size()) && Room < 0; ++I)
                if (B->expedition.rooms[static_cast<size_t>(I)].kind == RoomKind::Combat)
                    Room = I;
            Test->TestTrue(TEXT("The page has a fight room"), Room >= 0);
            if (Room < 0)
                return Wait(.05);
            // Cleared beforehand: walking in shuts no doors and calls no waves (its terrain stays).
            DungeonRoom& R = B->expedition.rooms[static_cast<size_t>(Room)];
            R.state = RoomState::Cleared;
            R.explored = true;
            const Vec2 Mid = Middle(Room);
            const double Across = R.w * Cell, Down = R.h * Cell;
            Place(biomeBoss(static_cast<PenBiome>(Pen)), Mid + Vec2{0, -Down * .3});
            for (int32 I = 0; I < 5; ++I)
                Place(biomeFoe(static_cast<PenBiome>(Pen), I), Mid + Vec2{(I - 2) * Across * .18, -Down * .02});
            TArray<Foe> Row = {biomeElite(static_cast<PenBiome>(Pen), 0), biomeElite(static_cast<PenBiome>(Pen), 1)};
            if (Pen == static_cast<int32>(PenBiome::Pencil))
                Row.Append({Foe::Thief, Foe::Mashup});
            else if (Pen == static_cast<int32>(PenBiome::Watercolor))
                Row.Append({Foe::SpiritRed, Foe::SpiritYellow, Foe::SpiritBlue});
            else if (Pen == BiomeCount - 1)
                Row.Append({Foe::GuardBing, Foe::GuardDun});
            for (int32 I = 0; I < Row.Num(); ++I)
                Place(Row[I], Mid + Vec2{(I - (Row.Num() - 1) * .5) * Across * .18, Down * .2});
            Expected = 1 + 5 + Row.Num();
            if (Unit* Me = B->find(P->UnitId))
            {
                Me->position = Free(Mid + Vec2{-Across * .3, Down * .38}, Me->radius);
                Me->aim = Vec2{1, -1}.normalized();
            }
            S->Publish();
            Walk = 0;
            return Wait(1.6);
        }
        if (Pen < BiomeCount && Step == 1)
        {
            // The eel's beads follow its wake: it swims its last few lengths into its slot first.
            for (Unit& U : B->units)
                if (U.breed == static_cast<int>(Foe::Eel) && Walk < 12)
                {
                    if (Walk == 0)
                        EelSlot = U.position;
                    U.position = EelSlot + Vec2{(11 - Walk) * U.radius * 1.1, 30 * std::sin(Walk * .9)};
                }
            if (Walk++ < 12 && Pen == static_cast<int32>(PenBiome::Watercolor))
            {
                S->Publish();
                Next = Now + 1. / 20;
                return false;
            }
            Walk = 0;
            Test->TestEqual(*FString::Printf(TEXT("Every foe of the %s page is on it"), PenNames[Pen]), OnPage(), Expected);
            Test->TestEqual(TEXT("The page is the pen's paper"), static_cast<int32>(S->Biome()), Pen);
            const FString Path = FPaths::ProjectSavedDir() / TEXT("ExpeditionVerification") / FString::Printf(TEXT("bestiary-%s.png"), PenNames[Pen]);
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
            FScreenshotRequest::RequestScreenshot(Path, false, false);
            ++Shots;
            return Wait(.5);
        }
        if (Pen < BiomeCount && Step == 2)
        {
            if (Pen < BiomeCount - 1)
                return Wait(.05);
            // The sage's second form: the dragon strung out along its wake, swept in an S across the room.
            Unit* Sage = nullptr;
            for (Unit& U : B->units)
                if (U.breed == static_cast<int>(Foe::Sage))
                    Sage = &U;
            if (!Sage || Room < 0)
            {
                Test->AddError(TEXT("The sage is not on its page"));
                return Wait(.05);
            }
            if (Walk == 0)
            {
                B->units.erase(std::remove_if(B->units.begin(), B->units.end(), [](const Unit& U) { return U.team == 2 && U.breed != static_cast<int>(Foe::Sage); }), B->units.end());
                for (Unit& U : B->units)
                    if (U.breed == static_cast<int>(Foe::Sage))
                        Sage = &U;
                Sage->parts = PartsSecondForm;
                Sage->hp = Sage->maxHp * .45;
            }
            const DungeonRoom& R = B->expedition.rooms[static_cast<size_t>(Room)];
            const Vec2 Mid = Middle(Room);
            const double T = FMath::Min(1., Walk / 26.);
            Sage->position = Free(Mid + Vec2{(T - .5) * R.w * Cell * .7, std::sin(T * 2 * Pi) * R.h * Cell * .16}, 40);
            Sage->aim = Vec2{1, std::cos(T * 2 * Pi) * .8}.normalized();
            S->Publish();
            if (++Walk <= 26)
            {
                Next = Now + 1. / 20;
                return false;
            }
            Walk = 0;
            return Wait(.6);
        }
        if (Stage == 2 + 3 * BiomeCount)
        {
            const FString Path = FPaths::ProjectSavedDir() / TEXT("ExpeditionVerification") / TEXT("bestiary-dragon.png");
            FScreenshotRequest::RequestScreenshot(Path, false, false);
            ++Shots;
            return Wait(.5);
        }
        S->Finished = true;
        G->ReturnToLobby(C);
        H->SetPage(AGWHUD::EPage::Main);
        Restore(GI);
        Test->TestEqual(TEXT("Captured every bestiary page"), Shots, BiomeCount + 1);
        return true;
    }

private:
    void Restore(UGWGameInstance* GI)
    {
        if (!Parked)
            return;
        Parked = false;
        for (int32 Slot = 0; Slot < UGWProfileSave::SlotCount; ++Slot)
        {
            const FString Path = UGWProfileSave::SlotPath(Slot), Backup = Path + TEXT(".playtest");
            UGWProfileSave::Delete(Slot);
            if (IFileManager::Get().FileExists(*Backup))
                IFileManager::Get().Move(*Path, *Backup, true);
        }
        if (auto* Settings = UGWUserSettings::Get())
        {
            Settings->ProfileSlot = OriginalSlot;
            Settings->SaveSettings();
        }
        if (GI)
            GI->ReloadProfile();
    }
    FAutomationTestBase* Test;
    double Start = 0, Next = 0;
    Vec2 EelSlot;
    int32 Stage = 0, Shots = 0, OriginalSlot = 0, Room = -1, NextFoe = 0, Expected = 0, Walk = 0;
    bool Parked = false;
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWExpeditionBestiary, "GW.Expedition.Bestiary", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWExpeditionBestiary::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FExpeditionBestiary(this));
    return true;
}
#endif
