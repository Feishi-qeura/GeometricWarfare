#include "GWGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "FU_OnlineSessionSubsystem.h"
#include "GWController.h"
#include "GWMeta.h"
#include "GWProfile.h"
#include "GWSaveGame.h"
#include "GWSettings.h"
#include "GWText.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"
DEFINE_LOG_CATEGORY_STATIC(LogGWRoom, Log, All);
namespace
{
FName SubsystemOf(EFU_OnlineProvider Provider)
{
    return Provider == EFU_OnlineProvider::Eos ? EOS_SUBSYSTEM : STEAM_SUBSYSTEM;
}
const TCHAR* EntryMap = TEXT("/Engine/Maps/Entry");
} // namespace
void UGWGameInstance::Init()
{
    Super::Init();
    auto* S = GetSubsystem<UFU_OnlineSessionSubsystem>();
    S->OnCreateSessionCompleteV2.AddDynamic(this, &UGWGameInstance::Created);
    S->OnJoinSessionCompleteV2.AddDynamic(this, &UGWGameInstance::Joined);
    S->OnDestroySessionComplete.AddDynamic(this, &UGWGameInstance::Destroyed);
    S->OnOnlineConnectionFailure.AddDynamic(this, &UGWGameInstance::ConnectionFailed);
    // Hubs are friends-only rooms (design doc: 默认只有好友和受邀者能进).
    S->bSteamFriendsOnly = true;
    MapLoadedHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UGWGameInstance::MapLoaded);
    if (GEngine)
        NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UGWGameInstance::NetworkFailure);
    GWText::Initialize();
    RefreshSaves();
    RefreshProfiles();
}
void UGWGameInstance::Shutdown()
{
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(MapLoadedHandle);
    if (GEngine)
        GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
    if (const IOnlineSessionPtr Sessions = InviteSessions.Pin())
        Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteHandle);
    if (EpicLoginHandle.IsValid())
        if (auto* Eos = Online::GetSubsystem(GetWorld(), EOS_SUBSYSTEM))
            if (auto Identity = Eos->GetIdentityInterface())
                Identity->ClearOnLoginCompleteDelegate_Handle(0, EpicLoginHandle);
    Super::Shutdown();
}
void UGWGameInstance::MapLoaded(UWorld* World)
{
    HookInvites();
    // A guest's machine has loaded the friend's hub: Gate 3 measures this from the accepted invite.
    if (World && World->GetNetMode() == NM_Client)
    {
        JoiningRoom = false;
        if (InviteAcceptedAt > 0 && ArrivedAt <= 0)
        {
            ArrivedAt = FPlatformTime::Seconds();
            UE_LOG(LogGWRoom, Display, TEXT("Reached the friend's hub %.1f s after accepting the invite"), ArrivedAt - InviteAcceptedAt);
        }
    }
    if (Leaving)
    {
        // The room's session closes before anything else opens; Destroyed() continues the trip.
        if (HasSession(ActiveProvider))
        {
            auto* S = GetSubsystem<UFU_OnlineSessionSubsystem>();
            if (ActiveProvider == EFU_OnlineProvider::Eos)
                S->DestroyEosSession();
            else
                S->DestroySteamSession();
            return;
        }
        Leaving = false;
        Busy = false;
        SteamRoom = false;
    }
    if (ReturnHub)
    {
        ReturnHub = false;
        // The map that just loaded finishes starting first; the hub opens on the next frame.
        GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UGWGameInstance::OpenHub));
    }
}
void UGWGameInstance::HookInvites()
{
    // Steam invites and "Join game" from the friends list arrive on the session interface the rooms use. A
    // game launched by an invite (+connect_lobby) is held by Steam until this delegate is bound.
    IOnlineSubsystem* Steam = Online::GetSubsystem(GetWorld(), STEAM_SUBSYSTEM);
    const IOnlineSessionPtr Sessions = Steam ? Steam->GetSessionInterface() : nullptr;
    if (!Sessions.IsValid() || InviteSessions.Pin() == Sessions)
        return;
    if (const IOnlineSessionPtr Old = InviteSessions.Pin())
        Old->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteHandle);
    InviteHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(
        FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &UGWGameInstance::InviteAccepted));
    InviteSessions = Sessions;
}
void UGWGameInstance::InviteAccepted(bool bWasSuccessful, int32, FUniqueNetIdPtr, const FOnlineSessionSearchResult& Result)
{
    if (!bWasSuccessful || !Result.IsValid())
    {
        StatusKey = TEXT("Network.InviteFailed");
        return;
    }
    // An invited player skips the tutorial and goes straight to the friend's hub (design doc flow); the room
    // this player had open closes on the way.
    InviteAcceptedAt = FPlatformTime::Seconds();
    ArrivedAt = 0;
    JoiningRoom = true;
    ReturnHub = false;
    Busy = true;
    StatusKey = TEXT("Network.JoiningFriend");
    ActiveProvider = EFU_OnlineProvider::Steam;
    RoomName = Result.Session.OwningUserName;
    UE_LOG(LogGWRoom, Display, TEXT("Accepted an invite to %s's hub"), *RoomName);
    GetSubsystem<UFU_OnlineSessionSubsystem>()->JoinSteamSessionFromResult(Result);
}
void UGWGameInstance::NetworkFailure(UWorld*, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
    // A host never goes anywhere when a guest drops; only a guest (or a join on its way) goes home.
    if (!Driver || Driver->GetNetMode() != NM_Client ||
        (Driver->NetDriverName != NAME_GameNetDriver && Driver->NetDriverName != NAME_PendingNetDriver))
        return;
    const bool Pending = Type == ENetworkFailure::PendingConnectionFailure || Driver->NetDriverName == NAME_PendingNetDriver;
    if (StatusKey != TEXT("Network.Kicked"))
        StatusKey = Error.Contains(TEXT("ROOM_BUSY"))   ? TEXT("Network.RoomBusy")
                    : Error.Contains(TEXT("ROOM_FULL")) ? TEXT("Network.RoomFull")
                    : Pending                           ? TEXT("Network.JoinFailed")
                                                        : TEXT("Network.HostLeft");
    Busy = false;
    if (Pending)
    {
        // A refused join leaves this machine where it was. A Steam join had already closed this player's own
        // room, so they go home rather than stay in a hub nobody can enter.
        if (JoiningRoom)
        {
            JoiningRoom = false;
            GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this] { LeaveRoom(true); }));
        }
        return;
    }
    // The engine takes a disconnected guest back to the entry map; from there they go to their own hub.
    ReturnHub = true;
    Leaving = true;
}
bool UGWGameInstance::HasSession(EFU_OnlineProvider Provider) const
{
    // EOS logs errors on every touch while unconfigured, so it is only asked once it can exist.
    if (Provider == EFU_OnlineProvider::Eos && !EpicConfigured())
        return false;
    IOnlineSubsystem* Platform = Online::GetSubsystem(GetWorld(), SubsystemOf(Provider));
    const IOnlineSessionPtr Sessions = Platform ? Platform->GetSessionInterface() : nullptr;
    return Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr;
}
EGWRoomKind UGWGameInstance::RoomKind() const
{
    const UWorld* W = GetWorld();
    if (!W || W->GetNetMode() == NM_Standalone)
        return EGWRoomKind::Offline;
    return SteamRoom ? EGWRoomKind::Steam : EGWRoomKind::Direct;
}
bool UGWGameInstance::ShowInvite()
{
    if (RoomKind() != EGWRoomKind::Steam)
        return false;
    IOnlineSubsystem* Steam = Online::GetSubsystem(GetWorld(), STEAM_SUBSYSTEM);
    const IOnlineExternalUIPtr Overlay = Steam ? Steam->GetExternalUIInterface() : nullptr;
    return Overlay.IsValid() && Overlay->ShowInviteUI(0, NAME_GameSession);
}
void UGWGameInstance::SetRoomJoinable(bool Joinable)
{
    const UWorld* W = GetWorld();
    if (!SteamRoom || !W || W->GetNetMode() != NM_ListenServer)
        return;
    IOnlineSubsystem* Steam = Online::GetSubsystem(GetWorld(), STEAM_SUBSYSTEM);
    const IOnlineSessionPtr Sessions = Steam ? Steam->GetSessionInterface() : nullptr;
    FOnlineSessionSettings* Current = Sessions.IsValid() ? Sessions->GetSessionSettings(NAME_GameSession) : nullptr;
    if (!Current || Current->bAllowJoinInProgress == Joinable)
        return;
    // A closed Steam lobby turns "Join game" away until the room is back in its hub.
    FOnlineSessionSettings Updated = *Current;
    Updated.bAllowJoinInProgress = Joinable;
    Sessions->UpdateSession(NAME_GameSession, Updated, true);
}
void UGWGameInstance::OpenHub()
{
    ReturnHub = false;
    if (Busy)
        return;
    if (PlatformReady(EFU_OnlineProvider::Steam))
    {
        CreateRoom(EFU_OnlineProvider::Steam, PlatformAccount(EFU_OnlineProvider::Steam), FString());
        return;
    }
    OpenOfflineHub();
}
void UGWGameInstance::OpenOfflineHub()
{
    // Without Steam the hub is the same sheet, only nobody can come in.
    SteamRoom = false;
    UGameplayStatics::OpenLevel(this, FName(EntryMap), true, TEXT("GWHub=1"));
}
void UGWGameInstance::ReturnHome(const TCHAR* NoticeKey)
{
    StatusKey = NoticeKey;
    ReturnHub = true;
    Leaving = true;
}
bool UGWGameInstance::EpicConfigured() const
{
    // EOS cannot start without product credentials and logs errors on every attempt, so the artifact in
    // the config is checked (once) before the EOS subsystem is ever touched.
    if (EpicConfig < 0)
    {
        const TCHAR* Section = TEXT("/Script/OnlineSubsystemEOS.EOSSettings");
        FString Artifact;
        if (!FParse::Value(FCommandLine::Get(), TEXT("EpicApp="), Artifact))
            GConfig->GetString(Section, TEXT("DefaultArtifactName"), Artifact, GEngineIni);
        TArray<FString> Artifacts;
        GConfig->GetArray(Section, TEXT("Artifacts"), Artifacts, GEngineIni);
        EpicConfig = 0;
        for (const FString& Entry : Artifacts)
        {
            FString Name, Client, Product, Sandbox, Deployment;
            FParse::Value(*Entry, TEXT("ArtifactName="), Name);
            FParse::Value(*Entry, TEXT("ClientId="), Client);
            FParse::Value(*Entry, TEXT("ProductId="), Product);
            FParse::Value(*Entry, TEXT("SandboxId="), Sandbox);
            FParse::Value(*Entry, TEXT("DeploymentId="), Deployment);
            if (Name == Artifact && !Client.IsEmpty() && !Product.IsEmpty() && !Sandbox.IsEmpty() && !Deployment.IsEmpty())
                EpicConfig = 1;
        }
    }
    return EpicConfig > 0;
}
bool UGWGameInstance::PlatformReady(EFU_OnlineProvider Provider) const
{
    if (Provider == EFU_OnlineProvider::Eos && !EpicConfigured())
        return false;
    const auto* S = GetSubsystem<UFU_OnlineSessionSubsystem>();
    return S && (Provider == EFU_OnlineProvider::Eos ? S->CheckEosProviderStatus() : S->CheckSteamProviderStatus()).bIsReady;
}
FString UGWGameInstance::PlatformAccount(EFU_OnlineProvider Provider) const
{
    if (Provider == EFU_OnlineProvider::Eos && !EpicConfigured())
        return FString();
    if (auto* Platform = Online::GetSubsystem(GetWorld(), SubsystemOf(Provider)))
        if (auto Identity = Platform->GetIdentityInterface(); Identity && Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
            return Identity->GetPlayerNickname(0);
    return FString();
}
FString UGWGameInstance::PlatformStatusKey(EFU_OnlineProvider Provider) const
{
    const auto* S = GetSubsystem<UFU_OnlineSessionSubsystem>();
    if (!S)
        return TEXT("Platform.Unavailable");
    if (Provider == EFU_OnlineProvider::Eos && !EpicConfigured())
        return TEXT("Platform.EpicNotConfigured");
    const FFU_OnlineProviderStatus Status =
        Provider == EFU_OnlineProvider::Eos ? S->CheckEosProviderStatus() : S->CheckSteamProviderStatus();
    if (Status.bIsReady)
        return TEXT("Platform.Ready");
    if (Provider == EFU_OnlineProvider::Eos)
    {
        if (EpicSigningIn)
            return TEXT("Platform.EpicSigningIn");
        if (Status.StatusCode == EFU_OnlineProviderStatusCode::NotLoggedIn)
            return TEXT("Platform.EpicSignIn");
        // Credentials are present but the EOS platform still failed to start (wrong ids, no SDK binaries).
        if (!Status.bSubsystemAvailable)
            return TEXT("Platform.EpicNotConfigured");
        return TEXT("Platform.Unavailable");
    }
    return Status.StatusCode == EFU_OnlineProviderStatusCode::NotLoggedIn || !Status.bSubsystemAvailable
               ? TEXT("Platform.SteamOffline")
               : TEXT("Platform.Unavailable");
}
void UGWGameInstance::LoginEpic()
{
    if (EpicSigningIn || PlatformReady(EFU_OnlineProvider::Eos))
        return;
    if (!EpicConfigured())
    {
        StatusKey = TEXT("Platform.EpicNotConfigured");
        return;
    }
    auto* Eos = Online::GetSubsystem(GetWorld(), EOS_SUBSYSTEM);
    auto Identity = Eos ? Eos->GetIdentityInterface() : nullptr;
    if (!Identity)
    {
        StatusKey = TEXT("Platform.EpicNotConfigured");
        return;
    }
    if (Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
        return;
    if (!EpicLoginHandle.IsValid())
        EpicLoginHandle = Identity->AddOnLoginCompleteDelegate_Handle(
            0, FOnLoginCompleteDelegate::CreateUObject(this, &UGWGameInstance::EpicLoginComplete));
    EpicSigningIn = true;
    StatusKey = TEXT("Platform.EpicSigningIn");
    // Started by the Epic Games Launcher (exchange code) or the DevAuthTool, the command line already
    // carries credentials; otherwise try the remembered token first and the account portal second.
    FString AuthType;
    if (FParse::Value(FCommandLine::Get(), TEXT("AUTH_TYPE="), AuthType) && !AuthType.IsEmpty())
        EpicSigningIn = Identity->AutoLogin(0);
    else
    {
        EpicPortalTried = false;
        EpicSigningIn = Identity->Login(0, FOnlineAccountCredentials(TEXT("persistentauth"), FString(), FString()));
    }
    if (!EpicSigningIn)
        StatusKey = TEXT("Platform.EpicFailed");
}
void UGWGameInstance::EpicLoginComplete(int32, bool bWasSuccessful, const FUniqueNetId&, const FString&)
{
    if (!bWasSuccessful && !EpicPortalTried)
    {
        EpicPortalTried = true;
        if (auto* Eos = Online::GetSubsystem(GetWorld(), EOS_SUBSYSTEM))
            if (auto Identity = Eos->GetIdentityInterface();
                Identity && Identity->Login(0, FOnlineAccountCredentials(TEXT("accountportal"), FString(), FString())))
                return;
    }
    EpicSigningIn = false;
    StatusKey = bWasSuccessful ? TEXT("Platform.EpicSignedIn") : TEXT("Platform.EpicFailed");
}
void UGWGameInstance::CreateRoom(EFU_OnlineProvider Provider, const FString& Name, const FString& Password, int32 SaveSlot)
{
    if (Busy)
        return;
    if (!PlatformReady(Provider))
    {
        StatusKey = PlatformStatusKey(Provider);
        if (StatusKey == TEXT("Platform.EpicSignIn"))
            LoginEpic();
        return;
    }
    auto* S = GetSubsystem<UFU_OnlineSessionSubsystem>();
    Busy = true;
    StatusKey = TEXT("Network.Creating");
    ActiveProvider = Provider;
    PendingSaveSlot = SaveSlot;
    RoomName = Name.TrimStartAndEnd().Left(48);
    if (RoomName.IsEmpty())
        RoomName = TEXT("Geometric Warfare");
    if (Provider == EFU_OnlineProvider::Eos)
        S->CreateEosSession(RoomCapacity, RoomName, Password);
    else
        S->CreateSteamSession(RoomCapacity, RoomName, Password);
}
void UGWGameInstance::Created(EFU_OnlineProvider P, bool Success)
{
    if (P != ActiveProvider)
        return;
    Busy = false;
    if (!Success)
    {
        // The hub still opens, only without the room: nobody is ever stuck on the title.
        StatusKey = TEXT("Network.CreateFailed");
        PendingSaveSlot = -1;
        OpenOfflineHub();
        return;
    }
    SteamRoom = P == EFU_OnlineProvider::Steam;
    StatusKey.Empty();
    // Every room opens into its hub; a continued co-op save waits there for its run to be started.
    const FString Options = PendingSaveSlot >= 0 ? FString::Printf(TEXT("listen?GWRoom=1?GWHub=1?GWSave=%d"), PendingSaveSlot)
                                                 : FString(TEXT("listen?GWRoom=1?GWHub=1"));
    PendingSaveSlot = -1;
    UGameplayStatics::OpenLevel(this, FName(EntryMap), true, Options);
}
void UGWGameInstance::Joined(EFU_OnlineProvider P, EFU_JoinSessionResult Result)
{
    if (P != ActiveProvider)
        return;
    Busy = false;
    if (Result == EFU_JoinSessionResult::Success)
    {
        // The plugin travels to the host; MapLoaded sees the friend's hub arrive.
        SteamRoom = P == EFU_OnlineProvider::Steam;
        StatusKey.Empty();
        return;
    }
    StatusKey = Result == EFU_JoinSessionResult::InvalidPassword ? TEXT("Network.BadPassword") : TEXT("Network.JoinFailed");
    // Joining closes this player's own room first; a failed join must not leave them in a hub nobody can enter.
    if (JoiningRoom)
    {
        JoiningRoom = false;
        const UWorld* W = GetWorld();
        if (W && W->GetNetMode() == NM_ListenServer && !HasSession(P))
            LeaveRoom(true);
    }
}
void UGWGameInstance::Destroyed(EFU_OnlineProvider P, bool Success)
{
    if (P != ActiveProvider)
        return;
    Busy = false;
    Leaving = false;
    SteamRoom = false;
    // A notice explaining why the player left (kicked, host gone) outlives the session it belonged to.
    if (!Success && StatusKey.IsEmpty())
        StatusKey = TEXT("Network.LeaveFailed");
    if (ReturnHub)
    {
        ReturnHub = false;
        GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UGWGameInstance::OpenHub));
    }
}
void UGWGameInstance::ConnectionFailed(EFU_OnlineProvider P, EFU_OnlineConnectionFailureType, const FString&)
{
    if (P != ActiveProvider)
        return;
    StatusKey = TEXT("Network.ConnectionLost");
    LeaveRoom(true);
}
void UGWGameInstance::LeaveRoom(bool ToOwnHub)
{
    Busy = true;
    Leaving = true;
    ReturnHub = ToOwnHub;
    JoiningRoom = false;
    UGameplayStatics::OpenLevel(this, FName(EntryMap), true);
}
void UGWGameInstance::SetLanguage(const FString& Culture)
{
    GWText::SetCulture(Culture);
}
void UGWGameInstance::RefreshSaves()
{
    Saves.SetNum(UGWRunSave::SlotCount);
    for (int32 Slot = 0; Slot < UGWRunSave::SlotCount; ++Slot)
    {
        FGWSaveSummary Summary;
        if (const UGWRunSave* Save = UGWRunSave::ReadSlot(Slot))
        {
            Summary.Used = true;
            Summary.Online = Save->Origin != EGWSaveOrigin::Offline;
            Summary.Provider = Save->Origin == EGWSaveOrigin::Epic ? EFU_OnlineProvider::Eos : EFU_OnlineProvider::Steam;
            Summary.SavedAt = Save->SavedAt;
            Summary.Level = Save->TeamLevel;
            Summary.Elapsed = Save->Elapsed;
            Summary.Players = Save->Players.Num();
            Summary.Bots = Save->Bots;
            Summary.RoomName = Save->RoomName;
        }
        Saves[Slot] = Summary;
    }
}
void UGWGameInstance::DeleteSave(int32 Slot)
{
    UGWRunSave::DeleteSlot(Slot);
    RefreshSaves();
}
void UGWGameInstance::HostSave(int32 Slot)
{
    if (!Saves.IsValidIndex(Slot) || !Saves[Slot].Used || !Saves[Slot].Online)
        return;
    CreateRoom(Saves[Slot].Provider, Saves[Slot].RoomName, FString(), Slot);
}
int32 UGWGameInstance::ProfileSlot() const
{
    const auto* Settings = UGWUserSettings::Get();
    return Settings ? FMath::Clamp(Settings->ProfileSlot, 0, UGWProfileSave::SlotCount - 1) : 0;
}
UGWProfileSave* UGWGameInstance::ActiveProfile()
{
    if (!Profile)
    {
        Profile = UGWProfileSave::ReadOrCreate(ProfileSlot());
        // Achievements a profile already qualifies for (one written before phase 4) are earned as it loads.
        gwgame::MetaProfile Core = Profile->ToCore();
        const std::vector<int> Fresh = Core.settle();
        if (!Fresh.empty())
        {
            for (int Index : Fresh)
                PendingToasts.Add(Index);
            Profile->FromCore(Core);
            Profile->Write(ProfileSlot());
        }
    }
    return Profile;
}
void UGWGameInstance::SelectProfile(int32 Slot)
{
    if (Slot < 0 || Slot >= UGWProfileSave::SlotCount)
        return;
    if (auto* Settings = UGWUserSettings::Get())
    {
        Settings->ProfileSlot = Slot;
        Settings->SaveSettings();
    }
    Profile = nullptr;
    ActiveProfile()->Write(Slot);
    RefreshProfiles();
    PushProfile();
}
void UGWGameInstance::ReloadProfile()
{
    Profile = nullptr;
    RefreshProfiles();
    PushProfile();
}
void UGWGameInstance::DeleteProfile(int32 Slot)
{
    UGWProfileSave::Delete(Slot);
    if (Slot == ProfileSlot())
        Profile = nullptr;
    RefreshProfiles();
    PushProfile();
}
FGWProfileDigest UGWGameInstance::ProfileDigest()
{
    const UGWProfileSave* Active = ActiveProfile();
    return Active ? GWMeta::ToDigest(Active->ToCore().report()) : FGWProfileDigest();
}
void UGWGameInstance::PushProfile()
{
    if (auto* C = Cast<AGWController>(GetFirstLocalPlayerController()))
        C->SendProfile();
}
bool UGWGameInstance::EditProfile(TFunctionRef<bool(gwgame::MetaProfile&)> Edit)
{
    UGWProfileSave* Active = ActiveProfile();
    if (!Active)
        return false;
    gwgame::MetaProfile Core = Active->ToCore();
    if (!Edit(Core))
        return false;
    for (int Index : Core.settle())
        PendingToasts.Add(Index);
    Active->FromCore(Core);
    Active->Write(ProfileSlot());
    RefreshProfiles();
    PushProfile();
    return true;
}
void UGWGameInstance::ApplyProgress(const FGWProgressPacket& Packet)
{
    UGWProfileSave* Active = ActiveProfile();
    if (!Active)
        return;
    gwgame::MetaProfile Core = Active->ToCore();
    const gwgame::Tally Share = GWMeta::ToTally(Packet);
    // A packet beyond any cap changes nothing at all (design doc: 奖励包上限).
    if (!gwgame::plausible(Share) || Packet.Stickers < 0 || Packet.Stickers > gwgame::MetaProfile::MaxStickersPerReward)
        return;
    if (Packet.Stickers > 0 || Packet.Clear)
    {
        // The first clear of a page is worth a bonus at the room's tier; only this profile knows it is the first.
        int Bonus = 0;
        if (Core.reward(TCHAR_TO_UTF8(*Packet.Level), Packet.Stickers, Packet.Stars, Packet.Clear, Packet.Tier, &Bonus) < 0)
            return;
        if (Packet.Clear)
        {
            LastClearLevel = Packet.Level;
            LastClearBonus = Bonus;
        }
    }
    Core.tally(Share);
    const uint64 Before = Core.achievements;
    for (int Index : Core.settle())
        PendingToasts.Add(Index);
    Active->FromCore(Core);
    Active->Write(ProfileSlot());
    RefreshProfiles();
    // The room's achievement wall shows each new one as it happens.
    if (Core.achievements != Before)
        PushProfile();
}
void UGWGameInstance::ConsumeItems(int32 ItemA, int32 ItemB)
{
    EditProfile([&](gwgame::MetaProfile& Core)
    {
        Core.consume({{ItemA, ItemB}});
        return true;
    });
}
bool UGWGameInstance::Research(int32 Attribute)
{
    return EditProfile([&](gwgame::MetaProfile& Core) { return Core.research(static_cast<gwgame::Attribute>(FMath::Clamp(Attribute, 0, 255))) >= 0; });
}
bool UGWGameInstance::BuyWeapon(int32 Weapon)
{
    return EditProfile([&](gwgame::MetaProfile& Core) { return Core.buyWeapon(Weapon) >= 0; });
}
bool UGWGameInstance::BuyPen(int32 Pen)
{
    return EditProfile([&](gwgame::MetaProfile& Core) { return Core.buyPen(Pen) >= 0; });
}
bool UGWGameInstance::BuyItem(int32 Item)
{
    return EditProfile([&](gwgame::MetaProfile& Core) { return Core.buyItem(Item) >= 0; });
}
bool UGWGameInstance::BuyHat(int32 Hat)
{
    return EditProfile([&](gwgame::MetaProfile& Core) { return Core.buyHat(Hat) >= 0; });
}
bool UGWGameInstance::EquipWeapon(int32 Weapon)
{
    return EditProfile([&](gwgame::MetaProfile& Core) { return Core.equipWeapon(Weapon); });
}
bool UGWGameInstance::EquipPen(int32 Pen)
{
    return EditProfile([&](gwgame::MetaProfile& Core) { return Core.equipPen(Pen); });
}
bool UGWGameInstance::EquipHat(int32 Hat)
{
    return EditProfile([&](gwgame::MetaProfile& Core) { return Core.equipHat(Hat); });
}
bool UGWGameInstance::EquipTitle(int32 Title)
{
    return EditProfile([&](gwgame::MetaProfile& Core) { return Core.equipTitle(Title); });
}
bool UGWGameInstance::BringItem(int32 Item)
{
    return EditProfile([&](gwgame::MetaProfile& Core) { return Core.bring(Item); });
}
bool UGWGameInstance::DropItem(int32 Slot)
{
    return EditProfile([&](gwgame::MetaProfile& Core) { return Core.drop(Slot); });
}
void UGWGameInstance::GrantStickers(int32 Count)
{
    EditProfile([&](gwgame::MetaProfile& Core)
    {
        Core.stickers = FMath::Clamp(Core.stickers + FMath::Clamp(Count, 0, 100000), 0, 10000000);
        return true;
    });
}
void UGWGameInstance::RefreshProfiles()
{
    Profiles.SetNum(UGWProfileSave::SlotCount);
    for (int32 Slot = 0; Slot < UGWProfileSave::SlotCount; ++Slot)
    {
        FGWProfileSummary Summary;
        if (const UGWProfileSave* Saved = UGWProfileSave::Read(Slot))
        {
            Summary.Used = true;
            Summary.PlayedAt = Saved->PlayedAt;
            Summary.Stickers = Saved->Stickers;
            Summary.Stars = Saved->StarCount();
            Summary.LevelsCleared = Saved->LevelsCleared;
        }
        Profiles[Slot] = Summary;
    }
}
bool UGWGameInstance::TutorialPending()
{
    const UGWProfileSave* Active = ActiveProfile();
    return Active && !Active->TutorialDone;
}
bool UGWGameInstance::StationSeen(int32 Station)
{
    const UGWProfileSave* Active = ActiveProfile();
    return Active && Station >= 0 && Station < 31 && (Active->StationsSeen & (1 << Station)) != 0;
}
void UGWGameInstance::MarkStationSeen(int32 Station)
{
    UGWProfileSave* Active = ActiveProfile();
    if (!Active || Station < 0 || Station >= 31 || (Active->StationsSeen & (1 << Station)))
        return;
    Active->StationsSeen |= 1 << Station;
    Active->Write(ProfileSlot());
}
void UGWGameInstance::MarkTutorialDone()
{
    UGWProfileSave* Active = ActiveProfile();
    if (!Active || Active->TutorialDone)
        return;
    Active->TutorialDone = true;
    Active->Write(ProfileSlot());
    RefreshProfiles();
}
