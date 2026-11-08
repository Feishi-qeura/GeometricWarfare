#include "GWGameMode.h"
#include "GameFramework/GameSession.h"
#include "GWController.h"
#include "GWGameInstance.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWMeta.h"
#include "GWPlayerState.h"
#include "GWProfile.h"
#include "GWSaveGame.h"
#include "GWSettings.h"
#include "Kismet/GameplayStatics.h"
AGWGameMode::AGWGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    GameStateClass = AGWGameState::StaticClass();
    PlayerStateClass = AGWPlayerState::StaticClass();
    PlayerControllerClass = AGWController::StaticClass();
    HUDClass = AGWHUD::StaticClass();
    DefaultPawnClass = nullptr;
}
void AGWGameMode::InitGame(const FString& Map, const FString& Options, FString& Error)
{
    Super::InitGame(Map, Options, Error);
    RequestedRoom = UGameplayStatics::ParseOption(Options, TEXT("GWRoom")) == TEXT("1");
    // Every room opens into its hub; an offline hub asks for one without the room.
    RequestedHub = RequestedRoom || UGameplayStatics::ParseOption(Options, TEXT("GWHub")) == TEXT("1");
    // A host continuing a co-op save opens its room with ?GWSave=<slot>; the run itself waits for Start.
    const FString Slot = UGameplayStatics::ParseOption(Options, TEXT("GWSave"));
    if (RequestedRoom && !Slot.IsEmpty())
        if (auto* Save = UGWRunSave::ReadSlot(FCString::Atoi(*Slot)); Save && Save->Origin != EGWSaveOrigin::Offline)
        {
            PendingSave = Save;
            for (const auto& Player : Save->Players)
                NameByAccount.Add(Player.Account, Player.Name);
        }
}
void AGWGameMode::InitGameState()
{
    Super::InitGameState();
    auto* S = GetGameState<AGWGameState>();
    S->IsRoom = RequestedRoom;
    // A room opens with its host's usual trolling level (Friendly unless they changed it).
    if (const auto* Settings = UGWUserSettings::Get(); Settings && RequestedRoom)
        S->Prank = static_cast<uint8>(FMath::Clamp(Settings->PrankLevel, 0, 2));
    // ...and the difficulty tier the host last played, as far as their profile has opened.
    S->MaxTier = static_cast<uint8>(HostTier());
    if (const auto* Settings = UGWUserSettings::Get())
        S->Tier = static_cast<uint8>(FMath::Clamp(Settings->Tier, 1, static_cast<int32>(S->MaxTier)));
    if (RequestedHub)
        BeginHub();
    RefreshSeats();
}
void AGWGameMode::ApplyPlayer(AGWGameState* S, const AGWPlayerState* P)
{
    if (!S || !P || !S->Simulation || P->UnitId < 0)
        return;
    S->Simulation->setThrowable(P->UnitId, P->Throwable);
    if (P->HasLoadout)
        S->Simulation->setLoadout(P->UnitId, P->Loadout);
}
void AGWGameMode::SetProfile(AGWController* C, const FGWProfileDigest& Digest)
{
    auto* S = GetGameState<AGWGameState>();
    auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    if (!S || !P)
        return;
    const gwgame::Loadout Clean = gwgame::sanitize(GWMeta::ToLoadout(Digest));
    P->Loadout = Clean;
    P->HasLoadout = true;
    P->Achievements = Clean.achievements;
    P->Hat = static_cast<uint8>(Clean.hat);
    P->Title = static_cast<uint8>(Clean.title);
    P->ForceNetUpdate();
    ApplyPlayer(S, P);
    if (S->Simulation)
        S->Publish();
}
int32 AGWGameMode::HostTier() const
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    const UGWProfileSave* Profile = GI ? GI->ActiveProfile() : nullptr;
    return Profile ? Profile->UnlockedTier() : 1;
}
bool AGWGameMode::SetTier(AGWController* C, int32 Tier)
{
    auto* S = GetGameState<AGWGameState>();
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    if (!S || !P || P->Identity != S->HostIdentity || (S->InMatch && !S->Finished && !S->Hub()))
        return false;
    S->MaxTier = static_cast<uint8>(HostTier());
    if (Tier < gwgame::MinTier || Tier > S->MaxTier)
        return false;
    S->Tier = static_cast<uint8>(Tier);
    // In the hub the new cap shows at once (speed, health); a finished run's next page opens on it.
    if (S->Simulation && S->Hub())
    {
        S->Simulation->setTier(Tier);
        S->Publish();
    }
    S->ForceNetUpdate();
    return true;
}
void AGWGameMode::JoinEveryone(AGWGameState* S)
{
    for (const auto& Item : S->PlayerArray)
        if (auto* Other = Cast<AGWPlayerState>(Item))
        {
            Other->UnitId = S->Simulation->joinHuman(Other->Identity);
            ApplyPlayer(S, Other);
        }
}
void AGWGameMode::SetJoinable(bool Joinable)
{
    if (auto* GI = GetGameInstance<UGWGameInstance>())
        GI->SetRoomJoinable(Joinable);
}
bool AGWGameMode::AloneInRoom() const
{
    int32 People = 0;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        People += It->IsValid();
    return People <= 1;
}
AGWController* AGWGameMode::HostController() const
{
    const auto* S = GetGameState<AGWGameState>();
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        if (auto* C = Cast<AGWController>(It->Get()))
            if (const auto* P = C->GetPlayerState<AGWPlayerState>(); P && S && P->Identity == S->HostIdentity)
                return C;
    return nullptr;
}
bool AGWGameMode::SetPrank(AGWController* C, int32 Level)
{
    auto* S = GetGameState<AGWGameState>();
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    if (!S || !P || P->Identity != S->HostIdentity || (S->InMatch && !S->Finished && !S->Hub()) || Level < 0 || Level > 2)
        return false;
    S->Prank = static_cast<uint8>(Level);
    // The hub plays by the room's level at once; a finished run's copy carries it into the next one.
    if (S->Simulation)
        S->Simulation->config.prank = static_cast<gwgame::PrankLevel>(Level);
    S->ForceNetUpdate();
    return true;
}
void AGWGameMode::SetThrowable(AGWController* C, bool Allowed)
{
    auto* S = GetGameState<AGWGameState>();
    auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    if (!S || !P)
        return;
    P->Throwable = Allowed;
    ApplyPlayer(S, P);
}
bool AGWGameMode::Kick(AGWController* C, int32 PlayerId)
{
    auto* S = GetGameState<AGWGameState>();
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    if (!S || !P || P->Identity != S->HostIdentity || !GameSession)
        return false;
    for (const auto& Item : S->PlayerArray)
        if (auto* Other = Cast<AGWPlayerState>(Item); Other && Other != P && Other->GetPlayerId() == PlayerId)
            if (auto* Target = Cast<APlayerController>(Other->GetOwner()))
                return GameSession->KickPlayer(Target, FText::FromString(TEXT("Network.Kicked")));
    return false;
}
FString AGWGameMode::AccountKey(const APlayerState* P) const
{
    if (GetNetMode() == NM_Standalone)
        return TEXT("local");
    const auto* GI = GetGameInstance<UGWGameInstance>();
    const TCHAR* Platform = GI && GI->ActiveProvider == EFU_OnlineProvider::Eos ? TEXT("EOS") : TEXT("Steam");
    if (P && P->GetUniqueId().IsValid())
        return FString::Printf(TEXT("%s:%s"), Platform, *P->GetUniqueId().ToString());
    return FString::Printf(TEXT("Guest:%d"), P ? P->GetPlayerId() : -1);
}
int32 AGWGameMode::IdentityFor(const FString& Account)
{
    if (const int32* Existing = IdentityMap.Find(Account))
        return *Existing;
    return IdentityMap.Add(Account, NextIdentity++);
}
void AGWGameMode::RefreshSeats()
{
    auto* S = GetGameState<AGWGameState>();
    if (!S)
        return;
    S->Resuming = PendingSave && S->IsRoom && (!S->InMatch || S->Hub());
    S->ResumeSeats.Reset();
    if (!S->Resuming)
        return;
    S->ResumeLevel = PendingSave->TeamLevel;
    S->ResumeElapsed = PendingSave->Elapsed;
    for (const auto& Player : PendingSave->Players)
    {
        FGWSeatView Seat;
        Seat.Name = Player.Name;
        for (const auto& State : S->PlayerArray)
            if (const auto* Other = Cast<AGWPlayerState>(State))
                Seat.Present |= Other->Account == Player.Account;
        S->ResumeSeats.Add(Seat);
    }
}
void AGWGameMode::PostLogin(APlayerController* Player)
{
    Super::PostLogin(Player);
    auto* P = Player->GetPlayerState<AGWPlayerState>();
    auto* S = GetGameState<AGWGameState>();
    if (!P || !S)
        return;
    FString Account = AccountKey(P);
    // Two connections presenting one platform id (two clients of one account on a test machine) must not
    // share a character: the later one plays as a guest.
    for (const auto& Item : S->PlayerArray)
        if (const auto* Other = Cast<AGWPlayerState>(Item); Other && Other != P && Other->Account == Account)
        {
            Account = FString::Printf(TEXT("Guest:%d"), P->GetPlayerId());
            break;
        }
    P->Account = Account;
    NameByAccount.Add(Account, P->GetPlayerName());
    P->Identity = IdentityFor(Account);
    if (S->HostIdentity < 0)
        S->HostIdentity = P->Identity;
    P->Team = 1;
    P->Ready = P->Identity == S->HostIdentity;
    // A newcomer walks into the hub (missions turn joins away in PreLogin).
    if (S->InMatch && S->Simulation)
    {
        P->UnitId = S->Simulation->joinHuman(P->Identity);
        if (P->UnitId < 0)
            if (auto* C = Cast<AGWController>(Player))
                C->ClientNotice(TEXT("Network.MatchFull"));
        ApplyPlayer(S, P);
        S->Publish();
    }
    RefreshSeats();
}
void AGWGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
                           FString& Error)
{
    Super::PreLogin(Options, Address, UniqueId, Error);
    if (!Error.IsEmpty())
        return;
    if (auto* S = GetGameState<AGWGameState>())
    {
        if (S->PlayerArray.Num() >= MaxPlayers)
            Error = TEXT("ROOM_FULL");
        // Players only ever join in the hub (design doc: 只在大厅加入); a mission has no late seats.
        else if (S->InMatch && !S->Hub())
            Error = TEXT("ROOM_BUSY");
    }
}
void AGWGameMode::Logout(AController* Player)
{
    if (auto* P = Player->GetPlayerState<AGWPlayerState>())
        if (auto* S = GetGameState<AGWGameState>())
            if (S->Simulation)
            {
                S->Simulation->leaveHuman(P->Identity);
                S->Publish();
            }
    Super::Logout(Player);
    RefreshSeats();
}
bool AGWGameMode::BeginHub()
{
    auto* S = GetGameState<AGWGameState>();
    if (!S)
        return false;
    CloseMission(S);
    auto Simulation = MakeUnique<gwgame::Battle>(GetDefault<UGWRuntimeSettings>()->Rules());
    Simulation->config.prank = static_cast<gwgame::PrankLevel>(S->Prank);
    // The hub plays by the room's tier too: a veteran's ranks show capped while everyone waits.
    Simulation->config.tier = S->Tier;
    const uint32 Seed = static_cast<uint32>(FPlatformTime::Cycles64()) | 1u;
    if (!Simulation->startHub(Seed))
        return false;
    S->Simulation = MoveTemp(Simulation);
    S->Seed = Seed;
    // The hub's sheet travels whole even when the last page had its size and version.
    S->WallVersion = MAX_uint32;
    S->Cells.Reset();
    S->BrokenWalls.Reset();
    S->WallPatches.Reset();
    S->ResetRings();
    S->StickersEarned = 0;
    S->DeployAt = 0;
    ResetProgress();
    JoinEveryone(S);
    FromHub = false;
    S->InMatch = true;
    S->Finished = false;
    RefreshSeats();
    S->Publish();
    S->ForceNetUpdate();
    SetJoinable(true);
    return true;
}
bool AGWGameMode::BeginRound(AGWController* C, bool Single, int32 LoadSlot)
{
    auto* S = GetGameState<AGWGameState>();
    auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    if (!P || !S || (S->InMatch && !S->Hub()) || P->Identity != S->HostIdentity)
        return false;
    const bool LeavingHub = S->Hub();
    UGWRunSave* Save = nullptr;
    if (Single)
    {
        if (GetNetMode() != NM_Standalone || S->IsRoom)
            return false;
        if (LoadSlot >= 0)
        {
            Save = UGWRunSave::ReadSlot(LoadSlot);
            if (!Save || Save->Origin != EGWSaveOrigin::Offline)
                return false;
        }
    }
    else
    {
        // The hub has no ready check: the host takes whoever is in the room.
        if (!S->IsRoom)
            return false;
        Save = PendingSave;
    }
    auto Simulation = MakeUnique<gwgame::Battle>(GetDefault<UGWRuntimeSettings>()->Rules());
    Simulation->config.prank = static_cast<gwgame::PrankLevel>(S->Prank);
    uint32 Seed = static_cast<uint32>(FPlatformTime::Cycles64()) | 1u;
    if (Save)
    {
        gwgame::RunSnapshot Run;
        if (!Save->Decode(Run))
            return false;
        // Saved identities belong to the session that wrote them; accounts map them onto this session's players.
        TMap<int32, int32> Remap;
        for (const auto& Player : Save->Players)
            if (Player.Identity >= 0 && !Player.Account.IsEmpty())
                Remap.Add(Player.Identity, IdentityFor(Player.Account));
        Run.units.erase(std::remove_if(Run.units.begin(), Run.units.end(),
                                       [&](const gwgame::SavedUnit& U) { return U.owner >= 0 && !Remap.Contains(U.owner); }),
                        Run.units.end());
        for (auto& U : Run.units)
            if (U.owner >= 0)
                U.owner = Remap[U.owner];
        if (!Simulation->restore(Run))
            return false;
        Seed = static_cast<uint32>(Run.seed);
    }
    else
        Simulation->start(Seed);
    CloseMission(S);
    ResetProgress();
    S->Simulation = MoveTemp(Simulation);
    S->Seed = Seed;
    S->WallVersion = 0;
    S->Cells.Reset();
    S->BrokenWalls.Reset();
    S->WallPatches.Reset();
    S->ResetRings();
    S->DeployAt = 0;
    JoinEveryone(S);
    if (Single && !Save)
        for (int32 I = 0; I < SinglePlayerBots; ++I)
            S->Simulation->addBot();
    PendingSave = nullptr;
    FromHub = LeavingHub;
    S->InMatch = true;
    S->Finished = false;
    RefreshSeats();
    S->Publish();
    S->ForceNetUpdate();
    SetJoinable(false);
    return true;
}
bool AGWGameMode::BeginDefense(AGWController* C, int32 Level, int32 Bots)
{
    auto* S = GetGameState<AGWGameState>();
    auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    // From the hub, from the title, or a restart from a result card.
    if (!P || !S || P->Identity != S->HostIdentity || (S->InMatch && !S->Finished && !S->Hub()))
        return false;
    if (!S->IsRoom && GetNetMode() != NM_Standalone)
        return false;
    const bool LeavingHub = S->Hub(), Restart = S->InMatch && !LeavingHub;
    CloseMission(S);
    auto Simulation = MakeUnique<gwgame::Battle>(GetDefault<UGWRuntimeSettings>()->Rules());
    Simulation->config.prank = static_cast<gwgame::PrankLevel>(S->Prank);
    // The page plays on the room's tier: ranks count up to it, enemies are tougher, stickers are worth more.
    Simulation->config.tier = FMath::Clamp(static_cast<int32>(S->Tier), gwgame::MinTier, gwgame::MaxTier);
    const uint32 Seed = static_cast<uint32>(FPlatformTime::Cycles64()) | 1u;
    if (!Simulation->startDefense(Seed, Level))
        return false;
    S->Simulation = MoveTemp(Simulation);
    S->Seed = Seed;
    // Force the whole grid to travel again even when the new page has the old page's size and version.
    S->WallVersion = MAX_uint32;
    S->Cells.Reset();
    S->BrokenWalls.Reset();
    S->WallPatches.Reset();
    S->ResetRings();
    S->StickersEarned = 0;
    S->DeployAt = 0;
    ResetProgress();
    JoinEveryone(S);
    // AI teammates come along only for a player alone (design doc: solo may add one to three).
    if (AloneInRoom())
        for (int32 I = 0; I < FMath::Clamp(Bots, 0, 3); ++I)
            S->Simulation->addBot();
    UseItems(S);
    PendingSave = nullptr;
    // A restart keeps where the run came from; otherwise it remembers whether it left the hub.
    if (!Restart)
        FromHub = LeavingHub;
    S->InMatch = true;
    S->Finished = false;
    RefreshSeats();
    S->Publish();
    S->ForceNetUpdate();
    SetJoinable(false);
    return true;
}
bool AGWGameMode::RequestDeploy(AGWController* C, int32 Level, int32 Bots)
{
    auto* S = GetGameState<AGWGameState>();
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    const int32 Pages = static_cast<int32>(gwgame::defenseLevels().size());
    if (!S || !P || P->Identity != S->HostIdentity || !S->Hub() || Level < 0 || Level >= Pages)
        return false;
    S->DeployLevel = static_cast<uint8>(Level);
    S->BoardLevel = static_cast<uint8>(Level);
    DeployBots = AloneInRoom() ? FMath::Clamp(Bots, 0, 3) : 0;
    S->DeployAt = GetWorld()->GetTimeSeconds() + DeploySeconds;
    S->ForceNetUpdate();
    return true;
}
bool AGWGameMode::CancelDeploy(AGWController* C)
{
    auto* S = GetGameState<AGWGameState>();
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    if (!S || !P || P->Identity != S->HostIdentity || S->DeployAt <= 0)
        return false;
    S->DeployAt = 0;
    S->ForceNetUpdate();
    return true;
}
bool AGWGameMode::PickBoard(AGWController* C, int32 Level)
{
    auto* S = GetGameState<AGWGameState>();
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    if (!S || !P || P->Identity != S->HostIdentity || Level < 0 || Level >= static_cast<int32>(gwgame::defenseLevels().size()))
        return false;
    S->BoardLevel = static_cast<uint8>(Level);
    return true;
}
bool AGWGameMode::BeginTutorial(AGWController* C)
{
    auto* S = GetGameState<AGWGameState>();
    auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    // A lesson for one: from the title, or from a hub (or a finished run) with nobody else around.
    if (!P || !S || P->Identity != S->HostIdentity || !AloneInRoom() || (S->InMatch && !S->Finished && !S->Hub()))
        return false;
    const bool LeavingHub = S->Hub();
    CloseMission(S);
    auto Simulation = MakeUnique<gwgame::Battle>(GetDefault<UGWRuntimeSettings>()->Rules());
    const uint32 Seed = static_cast<uint32>(FPlatformTime::Cycles64()) | 1u;
    if (!Simulation->startTutorial(Seed))
        return false;
    S->Simulation = MoveTemp(Simulation);
    S->Seed = Seed;
    S->WallVersion = MAX_uint32;
    S->Cells.Reset();
    S->BrokenWalls.Reset();
    S->WallPatches.Reset();
    S->ResetRings();
    S->StickersEarned = 0;
    S->DeployAt = 0;
    ResetProgress();
    JoinEveryone(S);
    PendingSave = nullptr;
    FromHub = LeavingHub;
    S->InMatch = true;
    S->Finished = false;
    RefreshSeats();
    S->Publish();
    S->ForceNetUpdate();
    SetJoinable(false);
    return true;
}
void AGWGameMode::ResetProgress()
{
    RewardedWaves = 0;
    RewardedFinish = false;
    BaitPaid.Reset();
    StatsSent.Reset();
    ItemsBrought.Reset();
}
void AGWGameMode::UseItems(AGWGameState* S)
{
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        if (auto* C = Cast<AGWController>(It->Get()))
            if (auto* P = C->GetPlayerState<AGWPlayerState>(); P && P->HasLoadout && P->UnitId >= 0)
            {
                const gwgame::ItemSlotsArray Used = S->Simulation->useItems(P->UnitId, P->Loadout.items);
                const int32 Count = (Used[0] >= 0 ? 1 : 0) + (Used[1] >= 0 ? 1 : 0);
                ItemsBrought.Add(P->Identity, Count);
                if (Count == 0)
                    continue;
                // Spent: the player's machine takes them out of its stock and reports what is left; until then the
                // room counts them as gone (a quick restart never uses the same item twice).
                P->Loadout.items = {{-1, -1}};
                C->ClientConsume(Used[0] < 0 ? 255 : static_cast<uint8>(Used[0]), Used[1] < 0 ? 255 : static_cast<uint8>(Used[1]));
            }
    S->Publish();
}
void AGWGameMode::SendProgress(AGWGameState* S, int32 Stickers, uint8 Stars, bool Clear, bool Final)
{
    const gwgame::Battle& B = *S->Simulation;
    const bool Page = B.defenseMode() && !B.tutorialMode();
    const FString LevelId = Page ? GWMeta::PageId(B.defense.level) : FString();
    // The awards of a run with friends: exactly the ones its result card draws (gwgame::awardWinners).
    std::vector<gwgame::AwardScores> Scores;
    std::vector<int32> Owners;
    for (const auto& U : B.units)
        if (U.kind == gwgame::UnitKind::Fighter && U.human)
        {
            Scores.push_back({{FMath::RoundToInt32(U.stats.damage), U.stats.held, U.stats.thrown, U.stats.walls, U.stats.pushes}});
            Owners.push_back(U.id);
        }
    const gwgame::AwardScores Winners = gwgame::awardWinners(Scores);
    const int32 Humans = static_cast<int32>(Owners.size());
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        auto* C = Cast<AGWController>(It->Get());
        const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
        if (!P)
            continue;
        FGWProgressPacket Packet;
        Packet.Level = LevelId;
        Packet.Tier = static_cast<uint8>(B.config.tier);
        Packet.Stickers = Stickers;
        Packet.Stars = Stars;
        Packet.Clear = Clear;
        if (const gwgame::Unit* U = B.find(P->UnitId); U && U->owner == P->Identity)
        {
            FSentStats& Sent = StatsSent.FindOrAdd(P->Identity);
            auto Delta = [](int32 Now, int32& Was)
            {
                const int32 New = FMath::Max(0, Now - Was);
                Was = FMath::Max(Was, Now);
                return New;
            };
            // Training targets in the hub count for nothing but bullseyes.
            Packet.Bullseyes = Delta(U->stats.bullseyes, Sent.Bullseyes);
            if (Page)
            {
                // Each packet also carries what that player's own bait earned since their last packet.
                const int32 Bait = FMath::Max(0, U->stats.baitBonus - BaitPaid.FindRef(P->Identity));
                BaitPaid.Add(P->Identity, U->stats.baitBonus);
                Packet.Stickers += Bait;
                Packet.Kills = Delta(U->kills, Sent.Kills);
                Packet.Damage = Delta(FMath::RoundToInt(U->stats.damage), Sent.Damage);
                Packet.Walls = Delta(U->stats.walls, Sent.Walls);
                Packet.Thrown = Delta(U->stats.thrown, Sent.Thrown);
                Packet.Throws = Delta(U->stats.throws, Sent.Throws);
                Packet.Held = Delta(U->stats.held, Sent.Held);
                Packet.InkGiven = Delta(FMath::RoundToInt(U->stats.inkGiven), Sent.InkGiven);
            }
        }
        if (Final)
        {
            Packet.Final = true;
            Packet.Won = B.finished && B.winner == 1;
            Packet.Bots = B.defense.botsUsed;
            Packet.Players = static_cast<uint8>(FMath::Clamp(Humans, 0, 8));
            Packet.Items = static_cast<uint8>(FMath::Clamp(ItemsBrought.FindRef(P->Identity), 0, gwgame::ItemSlots));
            for (int32 A = 0; A < gwgame::AwardCount; ++A)
                if (Humans >= 2 && Winners[A] >= 0 && Owners[Winners[A]] == P->UnitId)
                    Packet.Awards = static_cast<uint8>(Packet.Awards | (1 << A));
        }
        C->ClientProgress(Packet);
    }
}
void AGWGameMode::CloseMission(AGWGameState* S)
{
    if (!S || !S->Simulation || RewardedFinish)
        return;
    const gwgame::Battle& B = *S->Simulation;
    if (!B.defenseMode() || B.tutorialMode())
        return;
    RewardedFinish = true;
    SendProgress(S, 0, 0, false, true);
}
void AGWGameMode::GrantRewards(AGWGameState* S)
{
    const gwgame::Battle& B = *S->Simulation;
    // The hub pays nothing, but a throw onto the bullseye counts for the thrower's achievements.
    if (B.hubMode())
    {
        bool Hit = false;
        for (const auto& Item : S->PlayerArray)
            if (const auto* P = Cast<AGWPlayerState>(Item))
                if (const gwgame::Unit* U = B.find(P->UnitId); U && U->stats.bullseyes > StatsSent.FindRef(P->Identity).Bullseyes)
                    Hit = true;
        if (Hit)
            SendProgress(S, 0, 0, false, false);
        return;
    }
    // The tutorial pays nothing; defense pages pay per cleared wave (at the room's tier) and close with the clear.
    if (!B.defenseMode() || B.tutorialMode())
        return;
    while (RewardedWaves < B.defense.wavesCleared)
    {
        ++RewardedWaves;
        const int32 Gain = gwgame::waveStickers(RewardedWaves, B.config.tier);
        S->StickersEarned += Gain;
        SendProgress(S, Gain, 0, false, false);
    }
    if (B.finished && !RewardedFinish)
    {
        RewardedFinish = true;
        const bool Won = B.winner == 1;
        SendProgress(S, 0, Won ? B.defense.stars() : 0, Won, true);
    }
}
bool AGWGameMode::SaveRun(AGWController* C, int32 Slot, FString& Notice)
{
    auto* S = GetGameState<AGWGameState>();
    auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    Notice = TEXT("Save.Failed");
    if (!S || !P || !S->InMatch || !S->Simulation || S->Finished || Slot < 0 || Slot >= UGWRunSave::SlotCount)
        return false;
    // Only Survival runs are saved mid-run; a defense page keeps its progress in each player's profile.
    if (!S->Simulation->survivalMode())
        return false;
    if (P->Identity != S->HostIdentity)
    {
        Notice = TEXT("Save.HostOnly");
        return false;
    }
    const gwgame::RunSnapshot Run = S->Simulation->snapshot();
    auto* Save = NewObject<UGWRunSave>();
    Save->Encode(Run);
    Save->SavedAt = FDateTime::Now();
    auto* GI = GetGameInstance<UGWGameInstance>();
    Save->Origin = GetNetMode() == NM_Standalone ? EGWSaveOrigin::Offline
                   : GI && GI->ActiveProvider == EFU_OnlineProvider::Eos ? EGWSaveOrigin::Epic
                                                                         : EGWSaveOrigin::Steam;
    Save->RoomName = GI ? GI->RoomName : FString();
    Save->HostAccount = P->Account;
    Save->TeamLevel = Run.sharedLevel;
    Save->Elapsed = Run.elapsed;
    for (const auto& U : Run.units)
        Save->Bots += U.bot;
    // Every human of the run is stored, including friends who have not rejoined since a load.
    for (int32 RunOwner : Run.owners())
        for (const auto& Pair : IdentityMap)
            if (Pair.Value == RunOwner)
            {
                FGWSavedPlayer Player;
                Player.Account = Pair.Key;
                Player.Identity = RunOwner;
                Player.Name = NameByAccount.FindRef(Pair.Key);
                Save->Players.Add(Player);
                break;
            }
    if (!Save->WriteToSlot(Slot))
        return false;
    Notice = TEXT("Save.Saved");
    return true;
}
void AGWGameMode::ReturnToLobby(AGWController* C)
{
    auto* S = GetGameState<AGWGameState>();
    auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    if (!S || !P || P->Identity != S->HostIdentity || !S->Finished)
        return;
    // A hub world always goes back to its hub; a run started on the title goes back to the title.
    if (RunFromHub() && BeginHub())
        return;
    CloseMission(S);
    S->InMatch = false;
    S->Finished = false;
    S->DeployAt = 0;
    S->Simulation.Reset();
    S->ResetRings();
    FromHub = false;
    for (const auto& Item : S->PlayerArray)
        if (auto* Other = Cast<AGWPlayerState>(Item))
        {
            Other->UnitId = -1;
            Other->Ready = Other == P;
        }
    S->ForceNetUpdate();
}
bool AGWGameMode::AbandonRun(AGWController* C)
{
    auto* S = GetGameState<AGWGameState>();
    const auto* P = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    if (!S || !P || P->Identity != S->HostIdentity || !S->InMatch || S->Hub())
        return false;
    S->Finished = true;
    ReturnToLobby(C);
    return true;
}
void AGWGameMode::Tick(float Delta)
{
    Super::Tick(Delta);
    auto* S = GetGameState<AGWGameState>();
    if (!S)
        return;
    // The tiers the board offers follow the host's own profile (a clear may open the next one).
    if (const double Now = GetWorld()->GetTimeSeconds(); Now >= NextTierCheck && (!S->InMatch || S->Hub() || S->Finished))
    {
        NextTierCheck = Now + 1;
        const uint8 Top = static_cast<uint8>(HostTier());
        if (S->MaxTier != Top || S->Tier > Top)
        {
            S->MaxTier = Top;
            if (S->Tier > Top)
            {
                S->Tier = Top;
                if (S->Simulation && S->Hub())
                    S->Simulation->setTier(Top);
            }
            S->ForceNetUpdate();
        }
    }
    // The deploy countdown ends: everyone in the hub (including anyone who just walked in) leaves together.
    if (S->DeployAt > 0 && S->Hub() && GetWorld()->GetTimeSeconds() >= S->DeployAt)
    {
        S->DeployAt = 0;
        AGWController* Host = HostController();
        if (!Host || !BeginDefense(Host, S->DeployLevel, DeployBots))
            S->ForceNetUpdate();
        return;
    }
    if (!S->InMatch || !S->Simulation)
        return;
    // A player alone (offline, or a room nobody else is in yet) gets a real pause from the settings notebook;
    // with friends the shared world keeps running.
    if (LocalPause && (GetNetMode() == NM_Standalone || AloneInRoom()))
        return;
    S->Simulation->step(Delta);
    GrantRewards(S);
    // The wire arrays follow the simulation every frame and replication sends them at the game state's own
    // rate, so each send carries the newest state (a separate 20 Hz publish aliased sends down to ~15 Hz).
    S->Publish();
}
