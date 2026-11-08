#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "FU_OnlineSessionTypes.h"
#include "GWNetworkTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "GWGameInstance.generated.h"
class FUniqueNetId;
class UNetDriver;
class UGWRunSave;
class UGWProfileSave;
namespace gwgame
{
struct MetaProfile;
}

// What the profile page shows for one slot.
struct FGWProfileSummary
{
    bool Used = false;
    FDateTime PlayedAt;
    int32 Stickers = 0, Stars = 0, LevelsCleared = 0;
};

// What the save page shows for one slot without keeping the whole run in memory.
struct FGWSaveSummary
{
    bool Used = false;
    bool Online = false;
    EFU_OnlineProvider Provider = EFU_OnlineProvider::Steam;
    FDateTime SavedAt;
    int32 Level = 1, Players = 0, Bots = 0;
    float Elapsed = 0;
    FString RoomName;
};

// How this machine is in the room it is in: alone offline, through a Steam room, or by a direct address
// (development and the automated two-process tests).
enum class EGWRoomKind : uint8
{
    Offline,
    Steam,
    Direct
};

UCLASS()
class GEOMETRICWARFARE_API UGWGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    static constexpr int32 RoomCapacity = 8;
    virtual void Init() override;
    virtual void Shutdown() override;
    // The one road into play (design doc: 大厅即房间): this player's own hub. With Steam up it opens as a
    // friends-only Steam room for up to eight; without Steam (or if the room cannot be made) the same hub
    // opens offline.
    void OpenHub();
    // Online play goes through one of two platforms: Steam lobbies or Epic Online Services lobbies.
    // SaveSlot >= 0 opens the room to resume that co-op save. Every room opens into the hub.
    void CreateRoom(EFU_OnlineProvider Provider, const FString& Name, const FString& Password, int32 SaveSlot = -1);
    // Leaves the room this player is in: to their own hub (a guest going home) or to the title.
    void LeaveRoom(bool ToOwnHub = false);
    // A kicked guest, or one whose host left, goes back to their own hub with this notice.
    void ReturnHome(const TCHAR* NoticeKey);
    // Steam's invite dialog for this room (false when the room is not a Steam room).
    bool ShowInvite();
    EGWRoomKind RoomKind() const;
    // The host closes the Steam room while a mission runs and opens it again in the hub (players only ever
    // join in the hub).
    void SetRoomJoinable(bool Joinable);
    void SetLanguage(const FString& Culture);
    // Epic accounts sign in silently with a remembered token, or through the Epic account portal.
    void LoginEpic();
    bool PlatformReady(EFU_OnlineProvider Provider) const;
    // True once DefaultEngine.ini carries the Epic product credentials for the active artifact.
    bool EpicConfigured() const;
    FString PlatformAccount(EFU_OnlineProvider Provider) const;
    // Status line for a platform: ready, needs sign-in, or unavailable (and why).
    FString PlatformStatusKey(EFU_OnlineProvider Provider) const;
    void RefreshSaves();
    void DeleteSave(int32 Slot);
    // Continues an online save by hosting a room on the platform it was played on.
    void HostSave(int32 Slot);
    // Player profiles: the active one is always loaded (created on first use) and written after every reward.
    UGWProfileSave* ActiveProfile();
    int32 ProfileSlot() const;
    void SelectProfile(int32 Slot);
    void DeleteProfile(int32 Slot);
    // Forgets the loaded profile so the next use re-reads the active slot from disk (nothing is written).
    void ReloadProfile();
    void RefreshProfiles();
    // Meta progression (phase 4). What this player reports to the room they are in (gwgame::MetaProfile::report),
    // sent on arrival and after every change.
    FGWProfileDigest ProfileDigest();
    void PushProfile();
    // A host's packet for this profile: dropped whole beyond the caps (design doc: 奖励包上限), otherwise its
    // stickers, stars and run share are written, and any achievements it earns are marked.
    void ApplyProgress(const FGWProgressPacket& Packet);
    // The host applied these items to a mission (-1 = none): they leave the stock.
    void ConsumeItems(int32 ItemA, int32 ItemB);
    // The hub's stations, for this player alone: false when refused (too few stickers, not owned, full).
    bool Research(int32 Attribute);
    bool BuyWeapon(int32 Weapon);
    bool BuyPen(int32 Pen);
    bool BuyItem(int32 Item);
    bool BuyHat(int32 Hat);
    bool EquipWeapon(int32 Weapon);
    bool EquipPen(int32 Pen);
    bool EquipHat(int32 Hat);
    bool EquipTitle(int32 Title);
    bool BringItem(int32 Item);
    bool DropItem(int32 Slot);
    // Development builds: stickers for playtests (they never count toward achievements).
    void GrantStickers(int32 Count);
    // Achievements earned on this machine and not yet shown, oldest first (the HUD's sticker toasts).
    TArray<int32> PendingToasts;
    // A new save starts with the tutorial; finishing or skipping it marks the active profile.
    bool TutorialPending();
    void MarkTutorialDone();
    bool StationSeen(int32 Station);
    void MarkStationSeen(int32 Station);
    TArray<FGWProfileSummary> Profiles;
    // The last clear this player was rewarded for, shown on the result card.
    FString LastClearLevel;
    int32 LastClearBonus = 0;
    bool Busy = false;
    bool EpicSigningIn = false;
    FString StatusKey;
    TArray<FGWSaveSummary> Saves;
    EFU_OnlineProvider ActiveProvider = EFU_OnlineProvider::Steam;
    FString RoomName;
    // Gate 3 (an invited player reaches the friend's hub within a minute) is measured from these
    // (FPlatformTime seconds; ArrivedAt stays 0 until the friend's map has loaded).
    double InviteAcceptedAt = 0, ArrivedAt = 0;

private:
    UPROPERTY() TObjectPtr<UGWProfileSave> Profile;
    // Runs one change of the active profile through the shared rules; on success the achievements it earns are
    // marked, the profile is written and the room hears the new report.
    bool EditProfile(TFunctionRef<bool(gwgame::MetaProfile&)> Edit);
    bool Leaving = false;
    // The next loaded map opens this player's own hub (going home from someone else's room).
    bool ReturnHub = false;
    // This machine hosts or joined a Steam room (the invite dialog and joinability only exist then).
    bool SteamRoom = false;
    // A join through Steam is under way: if it fails, this player goes back to their own hub.
    bool JoiningRoom = false;
    bool EpicPortalTried = false;
    mutable int8 EpicConfig = -1;
    int32 PendingSaveSlot = -1;
    FDelegateHandle MapLoadedHandle, EpicLoginHandle, InviteHandle, NetworkFailureHandle;
    TWeakPtr<IOnlineSession, ESPMode::ThreadSafe> InviteSessions;
    void MapLoaded(UWorld* World);
    void HookInvites();
    void OpenOfflineHub();
    void InviteAccepted(bool bWasSuccessful, int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& Result);
    void NetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
    bool HasSession(EFU_OnlineProvider Provider) const;
    void EpicLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error);
    UFUNCTION() void Created(EFU_OnlineProvider Provider, bool Success);
    UFUNCTION() void Joined(EFU_OnlineProvider Provider, EFU_JoinSessionResult Result);
    UFUNCTION() void Destroyed(EFU_OnlineProvider Provider, bool Success);
    UFUNCTION()
    void ConnectionFailed(EFU_OnlineProvider Provider, EFU_OnlineConnectionFailureType Failure, const FString& Message);
};
