#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GWGameMode.generated.h"
class AGWController;
class AGWGameState;
class UGWRunSave;
UCLASS()
class GEOMETRICWARFARE_API AGWGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    // Rooms hold up to eight players (design doc: 1-8 players, sized for streamers).
    static constexpr int32 MaxPlayers = 8;
    // Offline Survival runs fill the squad to four with AI teammates.
    static constexpr int32 SinglePlayerBots = 3;
    // Seconds between the host's deploy on the mission board and everyone leaving the hub.
    static constexpr float DeploySeconds = 3;
    AGWGameMode();
    virtual void InitGame(const FString& MapName, const FString& Options, FString& Error) override;
    virtual void InitGameState() override;
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
                          FString& Error) override;
    virtual void PostLogin(APlayerController* Player) override;
    virtual void Logout(AController* Player) override;
    virtual void Tick(float DeltaSeconds) override;
    // Opens (or reopens) the hub. Rooms always start in it (design doc: 大厅即房间) and every mission that left
    // it returns to it.
    bool BeginHub();
    // LoadSlot >= 0 continues an offline save; a room's host starts Survival for everyone from the hub.
    // Survival is kept for the coming expedition mode and its tests.
    bool BeginRound(AGWController* Player, bool Single, int32 LoadSlot = -1);
    // Opens a line-defense page for everyone, from the hub, the title or a result card. Up to three AI
    // teammates come along when the host is alone in the room.
    bool BeginDefense(AGWController* Player, int32 Level, int32 Bots = 0);
    // The host's deploy from the hub's mission board: everyone leaves after DeploySeconds unless the host
    // cancels; players can still walk in meanwhile and go along.
    bool RequestDeploy(AGWController* Player, int32 Level, int32 Bots);
    bool CancelDeploy(AGWController* Player);
    // The page the host is looking at on the mission board, shown to everyone in the room.
    bool PickBoard(AGWController* Player, int32 Level);
    // The scripted tutorial for a player alone: from the title for a new save, or replayed from a hub nobody
    // else is in.
    bool BeginTutorial(AGWController* Player);
    // From a result card: back to the hub the run started from (or the title, for a run started there).
    void ReturnToLobby(AGWController* Player);
    // The host gives up the mission in progress and everyone goes back (rewards already written stay written).
    bool AbandonRun(AGWController* Player);
    // Only the authority owns the run, so only single player or the room host may write a save.
    bool SaveRun(AGWController* Player, int32 Slot, FString& Notice);
    // The host sets the room's trolling level (in the hub or between runs); every player sets their own throw
    // permission.
    bool SetPrank(AGWController* Player, int32 Level);
    void SetThrowable(AGWController* Player, bool Allowed);
    // The host may remove a player from the room (design doc: 防止变成恶意).
    bool Kick(AGWController* Player, int32 PlayerId);
    // Nobody but the host is in the world: the settings notebook pauses it and AI teammates may come along.
    bool AloneInRoom() const;
    // Meta progression (phase 4). A player's report of their own profile: checked (design doc 进房校验: anything
    // outside the game's lists counts as nothing), kept on their player state for the room's wall, hats and
    // titles, and applied to their character.
    void SetProfile(AGWController* Player, const struct FGWProfileDigest& Digest);
    // The host picks the room's difficulty tier on the board, among the tiers their own profile has opened.
    bool SetTier(AGWController* Player, int32 Tier);
    // The highest tier the host's own profile has opened (the host plays on this machine).
    int32 HostTier() const;
    // Whether the run in progress began in the hub (its end goes back there rather than to the title).
    bool RunFromHub() const
    {
        return FromHub || RequestedHub;
    }
    bool LocalPause = false;

private:
    // Bait stickers already sent per player identity this run.
    TMap<int32, int32> BaitPaid;
    void ApplyPlayer(AGWGameState* State, const class AGWPlayerState* Player);
    void JoinEveryone(AGWGameState* State);
    void SetJoinable(bool Joinable);
    AGWController* HostController() const;
    bool RequestedRoom = false;
    // This world is a hub world (every room, and the offline hub): runs end back in its hub.
    bool RequestedHub = false;
    // The run in progress left the hub (a run started on the title returns to the title).
    bool FromHub = false;
    int32 DeployBots = 0;
    UPROPERTY() TObjectPtr<UGWRunSave> PendingSave;
    TMap<FString, int32> IdentityMap;
    TMap<FString, FString> NameByAccount;
    int32 NextIdentity = 1;
    // Rewards are handed out as each wave is cleared, so a host leaving early never voids what was earned.
    int32 RewardedWaves = 0;
    bool RewardedFinish = false;
    // Per player identity: their share of the run already sent (each packet carries what is new), and how many
    // items they brought into the mission.
    struct FSentStats
    {
        int32 Kills = 0;
        int32 Damage = 0;
        int32 Walls = 0;
        int32 Thrown = 0;
        int32 Throws = 0;
        int32 Held = 0;
        int32 InkGiven = 0;
        int32 Bullseyes = 0;
    };
    TMap<int32, FSentStats> StatsSent;
    TMap<int32, int32> ItemsBrought;
    double NextTierCheck = 0;
    FString AccountKey(const APlayerState* Player) const;
    int32 IdentityFor(const FString& Account);
    void RefreshSeats();
    void GrantRewards(AGWGameState* State);
    // One packet to every player's own profile: stickers (with their bait), stars, and their share of the run.
    void SendProgress(AGWGameState* State, int32 Stickers, uint8 Stars, bool Clear, bool Final);
    // A defense mission ending without a result (given up, or a new one opening): its last packet goes out once.
    void CloseMission(AGWGameState* State);
    // A new simulation: nothing of it has been sent yet.
    void ResetProgress();
    // Each player's two items, applied as a defense page opens and taken out of their stock by their machine.
    void UseItems(AGWGameState* State);
};
