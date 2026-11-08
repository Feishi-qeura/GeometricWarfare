#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "GWController.h"
#include "GWGameMode.h"
#include "GWGameState.h"
#include "GWPlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Network stress run for the design doc's gate 2 (8 players, a page full of enemies). One process hosts a
// listen room over direct IP and keeps the page topped up with enemies; up to seven headless processes join
// on autopilot (Scripts/Test-NetStress.ps1 starts them all). The host reports its upload per client and how
// often each connection was saturated; each client reports its download, how many snapshots arrived per
// second, the longest gap between them and how often a unit jumped. Reports land in -GWStressOut.
namespace
{
int32 Option(const TCHAR* Name, int32 Default)
{
    int32 Value = Default;
    FParse::Value(FCommandLine::Get(), Name, Value);
    return Value;
}
FString OutDir()
{
    FString Dir;
    if (!FParse::Value(FCommandLine::Get(), TEXT("GWStressOut="), Dir))
        Dir = FPaths::ProjectSavedDir() / TEXT("NetVerification");
    return Dir;
}
void WriteReport(const FString& Name, const TSharedRef<FJsonObject>& Report)
{
    FString Text;
    const auto Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(Report, Writer);
    const FString Path = OutDir() / Name;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
TArray<TSharedPtr<FJsonValue>> Numbers(const TArray<double>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    for (double V : Values)
        Out.Add(MakeShared<FJsonValueNumber>(V));
    return Out;
}
double Mean(const TArray<double>& Values)
{
    double Sum = 0;
    for (double V : Values)
        Sum += V;
    return Values.IsEmpty() ? 0 : Sum / Values.Num();
}
double Most(const TArray<double>& Values)
{
    double Top = 0;
    for (double V : Values)
        Top = FMath::Max(Top, V);
    return Top;
}

class FStressHost : public IAutomationLatentCommand
{
public:
    explicit FStressHost(FAutomationTestBase* T)
        : Test(T), Start(FPlatformTime::Seconds()), Clients(FMath::Clamp(Option(TEXT("GWStressClients="), 7), 1, 7)),
          Seconds(FMath::Clamp(Option(TEXT("GWStressSeconds="), 40), 5, 600)), Warmup(FMath::Clamp(Option(TEXT("GWStressWarmup="), 12), 0, 120)),
          Monsters(FMath::Clamp(Option(TEXT("GWStressMonsters="), 160), 0, 400))
    {
    }
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        auto* G = W ? W->GetAuthGameMode<AGWGameMode>() : nullptr;
        if (Now - Start > 420)
        {
            Test->AddError(TEXT("Network stress host timed out"));
            Finish(TEXT("timeout"));
            return true;
        }
        if (!C || !S)
            return false;
        if (Stage == 0)
        {
            if (!S->IsRoom)
            {
                if (Now >= Next && W->GetNetMode() != NM_ListenServer)
                {
                    UGameplayStatics::OpenLevel(W, FName(TEXT("/Engine/Maps/Entry")), true, TEXT("listen?GWRoom=1"));
                    Next = Now + 10;
                }
                return false;
            }
            Stage = 1;
            Waiting = Now;
        }
        if (Stage == 1)
        {
            int32 Arrived = 0;
            for (const auto& Item : S->PlayerArray)
                if (auto* Other = Cast<AGWPlayerState>(Item))
                    Arrived += Other->UnitId >= 0;
            // Everyone walks into the hub on joining; the host deploys once all of them are there.
            if (S->PlayerArray.Num() < Clients + 1 || Arrived < Clients + 1)
            {
                if (Now - Waiting > 300)
                {
                    Test->AddError(FString::Printf(TEXT("Only %d of %d clients joined"), S->PlayerArray.Num() - 1, Clients));
                    Finish(TEXT("clients missing"));
                    return true;
                }
                return false;
            }
            if (!G || !G->BeginDefense(C, 0, 0))
            {
                Test->AddError(TEXT("Stress host could not open page 1-1"));
                Finish(TEXT("no page"));
                return true;
            }
            auto& B = *S->Simulation;
            const auto* HostPlayer = C->GetPlayerState<AGWPlayerState>();
            const int32 Painter = HostPlayer ? HostPlayer->UnitId : -1;
            // A serpentine of drawn walls (as in GW.Defense.Scenes) so rams chew walls and the page changes.
            for (int32 Y = 1; Y <= 7; ++Y)
                B.paintWall(Painter, Y * B.maze.width + 5);
            for (int32 Y = 3; Y <= 9; ++Y)
                B.paintWall(Painter, Y * B.maze.width + 11);
            // A wave that never ends: a far-off order keeps the queue open and the core cannot fall.
            B.config.maxMonsters = Monsters;
            B.callWave(Painter);
            B.defense.queue.clear();
            gwgame::SpawnOrder Never;
            Never.at = 1e9;
            B.defense.queue.push_back(Never);
            B.defense.queued = 0;
            B.defense.coreHealth = B.defense.coreMaxHealth = 1e9;
            B.defense.waveHealth = 5;
            Stage = 2;
            Began = Now;
            Next = Now;
        }
        auto& B = *S->Simulation;
        if (Stage == 2)
        {
            // Keep the page full: a steady mix of runners, rams and shooters marching from both entrances.
            if (Now >= Refill)
            {
                Refill = Now + .25;
                int32 Alive = 0;
                for (const auto& U : B.units)
                    Alive += U.alive && U.kind == gwgame::UnitKind::Monster;
                for (int32 I = 0; I < 24 && Alive < Monsters; ++I, ++Alive)
                {
                    const uint32 Roll = Spawned++ % 10;
                    const gwgame::Shape Kind = Roll < 6 ? gwgame::Shape::Circle : Roll < 8 ? gwgame::Shape::Rectangle : gwgame::Shape::Triangle;
                    B.spawnWaveEnemy(Kind, Spawned % 13 == 0, static_cast<int>(Spawned % 2));
                }
            }
            UNetDriver* Driver = W->GetNetDriver();
            if (!Driver)
                return false;
            const auto& Connections = Driver->ClientConnections;
            if (Now - Began >= Warmup)
            {
                ++Frames;
                FrameMs.Add(FApp::GetDeltaTime() * 1000);
                Saturated.SetNum(FMath::Max(Saturated.Num(), Connections.Num()));
                for (int32 I = 0; I < Connections.Num(); ++I)
                    if (Connections[I] && !Connections[I]->IsNetReady())
                        ++Saturated[I];
            }
            if (Now >= Next)
            {
                Next = Now + 1;
                if (Now - Began >= Warmup)
                {
                    int32 Alive = 0, Allies = 0;
                    for (const auto& U : B.units)
                    {
                        Alive += U.alive && U.kind == gwgame::UnitKind::Monster;
                        Allies += U.alive && U.kind == gwgame::UnitKind::Companion;
                    }
                    MonstersAlive.Add(Alive);
                    AlliesAlive.Add(Allies);
                    UnitSlots.Add(S->Units.Num());
                    ShotCount.Add(S->Projectiles.Num());
                    EffectCount.Add(S->Effects.Num());
                    double Total = 0;
                    TArray<double> PerClient;
                    for (const auto& Connection : Connections)
                        if (Connection)
                        {
                            PerClient.Add(Connection->OutBytesPerSecond);
                            Total += Connection->OutBytesPerSecond;
                            Packets.Add(Connection->OutPacketsPerSecond);
                            Lag.Add(Connection->AvgLag * 1000);
                        }
                    ClientOut.Append(PerClient);
                    Uplink.Add(Total);
                }
                if (Now - Began >= Warmup + Seconds)
                {
                    // Report, then end the run so the clients stop measuring before this process closes.
                    Finish(TEXT("done"));
                    // Ended as a loss so no reward packet ever touches a real profile.
                    B.finished = true;
                    B.winner = 2;
                    S->Publish();
                    S->ForceNetUpdate();
                    Stage = 3;
                    Next = Now + 2;
                }
            }
        }
        return Stage == 3 && Now >= Next;
    }

private:
    void Finish(const TCHAR* Result)
    {
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("role"), TEXT("host"));
        Report->SetStringField(TEXT("result"), Result);
        Report->SetNumberField(TEXT("clients"), Clients);
        Report->SetNumberField(TEXT("monsterTarget"), Monsters);
        Report->SetNumberField(TEXT("seconds"), Seconds);
        Report->SetArrayField(TEXT("uplinkBytesPerSecond"), Numbers(Uplink));
        Report->SetArrayField(TEXT("clientBytesPerSecond"), Numbers(ClientOut));
        Report->SetArrayField(TEXT("monstersAlive"), Numbers(MonstersAlive));
        TArray<double> Shares;
        for (int32 Count : Saturated)
            Shares.Add(Frames ? Count / static_cast<double>(Frames) : 0);
        Report->SetArrayField(TEXT("saturatedShare"), Numbers(Shares));
        const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
        Summary->SetNumberField(TEXT("avgClientKBps"), Mean(ClientOut) / 1000);
        Summary->SetNumberField(TEXT("maxClientKBps"), Most(ClientOut) / 1000);
        Summary->SetNumberField(TEXT("avgUplinkKBps"), Mean(Uplink) / 1000);
        Summary->SetNumberField(TEXT("maxUplinkKBps"), Most(Uplink) / 1000);
        Summary->SetNumberField(TEXT("avgPacketsPerSecond"), Mean(Packets));
        Summary->SetNumberField(TEXT("avgLagMs"), Mean(Lag));
        Summary->SetNumberField(TEXT("saturatedShareMax"), Most(Shares));
        Summary->SetNumberField(TEXT("avgMonsters"), Mean(MonstersAlive));
        Summary->SetNumberField(TEXT("avgAllies"), Mean(AlliesAlive));
        Summary->SetNumberField(TEXT("unitSlots"), Most(UnitSlots));
        Summary->SetNumberField(TEXT("avgProjectiles"), Mean(ShotCount));
        Summary->SetNumberField(TEXT("avgEffects"), Mean(EffectCount));
        Summary->SetNumberField(TEXT("avgFrameMs"), Mean(FrameMs));
        Summary->SetNumberField(TEXT("maxFrameMs"), Most(FrameMs));
        Report->SetObjectField(TEXT("summary"), Summary);
        WriteReport(TEXT("stress-host.json"), Report);
    }
    FAutomationTestBase* Test;
    double Start = 0, Next = 0, Waiting = 0, Began = 0, Refill = 0;
    int32 Clients = 7, Seconds = 40, Warmup = 12, Monsters = 160, Stage = 0, Frames = 0;
    uint32 Spawned = 0;
    TArray<int32> Saturated;
    TArray<double> Uplink, ClientOut, Packets, Lag, MonstersAlive, AlliesAlive, UnitSlots, ShotCount, EffectCount, FrameMs;
};

class FStressClient : public IAutomationLatentCommand
{
public:
    explicit FStressClient(FAutomationTestBase* T)
        : Test(T), Start(FPlatformTime::Seconds()), Seconds(FMath::Clamp(Option(TEXT("GWStressSeconds="), 40), 5, 600)),
          Warmup(FMath::Clamp(Option(TEXT("GWStressWarmup="), 12), 0, 120))
    {
    }
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        auto* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* C = W ? Cast<AGWController>(W->GetFirstPlayerController()) : nullptr;
        auto* S = W ? W->GetGameState<AGWGameState>() : nullptr;
        if (Now - Start > 480)
        {
            Finish(TEXT("timeout"));
            return true;
        }
        if (!C || !S)
            return false;
        if (!InMatchSeen)
        {
            if (!S->IsRoom)
            {
                const ENetMode Mode = W->GetNetMode();
                if (Now >= Next && Mode != NM_Client)
                {
                    GEngine->Exec(W, TEXT("open 127.0.0.1"));
                    Next = Now + 5;
                }
                return false;
            }
            // The hub is a match of its own; the measured part starts with the mission.
            if (!S->InMatch || S->Hub())
                return false;
            InMatchSeen = true;
            Joined = Now;
            C->Autopilot = true;
            LastElapsed = S->Elapsed;
            LastArrival = Now;
        }
        // The host marks the run finished when its measurement ends (or closes, landing us on the entry map).
        if (!S->IsRoom || W->GetNetMode() != NM_Client || S->Finished || Now - LastArrival > 3 || Now - Joined > Warmup + Seconds + 40)
        {
            Finish(TEXT("done"));
            return true;
        }
        const bool Measuring = Now - Joined >= Warmup;
        ++FrameCount;
        // Each one-second sample starts when measuring does, so the first one is a whole second too.
        if (Measuring && !MeasureBegun)
        {
            MeasureBegun = true;
            NextSample = Now + 1;
            LastArrivals = Arrivals;
            LastFrameCount = FrameCount;
        }
        if (S->Elapsed != LastElapsed)
        {
            // A new snapshot arrived: how long since the last one, and did any unit jump.
            if (Measuring)
            {
                ++Arrivals;
                MaxGap = FMath::Max(MaxGap, Now - LastArrival);
                for (const auto& U : S->Units)
                {
                    FTrack& Track = Tracks.FindOrAdd(U.Id);
                    if (Track.Seen && Track.Alive && U.Alive && Track.Kind == U.Kind && U.State != 4 && Track.State != 4 &&
                        FVector::Dist2D(FVector(U.Position), Track.Position) > 250)
                        ++Jumps;
                    Track = {FVector(U.Position), U.Kind, U.State, U.Alive, true};
                }
            }
            LastElapsed = S->Elapsed;
            LastArrival = Now;
        }
        if (MeasureBegun && Now >= NextSample)
        {
            NextSample = Now + 1;
            if (const UNetConnection* Connection = C->GetNetConnection())
                Download.Add(Connection->InBytesPerSecond);
            Rates.Add(Arrivals - LastArrivals);
            LastArrivals = Arrivals;
            // A client drawing fewer frames than snapshots arrive counts several snapshots as one.
            Fps.Add(FrameCount - LastFrameCount);
            LastFrameCount = FrameCount;
        }
        Distance = C->AutopilotDistance;
        return false;
    }

private:
    struct FTrack
    {
        FVector Position = FVector::ZeroVector;
        uint8 Kind = 0, State = 0;
        bool Alive = false, Seen = false;
    };
    void Finish(const TCHAR* Result)
    {
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("role"), TEXT("client"));
        Report->SetStringField(TEXT("result"), Result);
        Report->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
        Report->SetBoolField(TEXT("played"), InMatchSeen);
        Report->SetArrayField(TEXT("downloadBytesPerSecond"), Numbers(Download));
        Report->SetArrayField(TEXT("snapshotsPerSecond"), Numbers(Rates));
        const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
        Summary->SetNumberField(TEXT("avgDownloadKBps"), Mean(Download) / 1000);
        Summary->SetNumberField(TEXT("avgSnapshotsPerSecond"), Mean(Rates));
        TArray<double> Lowest = Rates;
        Lowest.Sort();
        Summary->SetNumberField(TEXT("minSnapshotsPerSecond"), Lowest.IsEmpty() ? 0 : Lowest[0]);
        Summary->SetNumberField(TEXT("maxGapMs"), MaxGap * 1000);
        Summary->SetNumberField(TEXT("avgFps"), Mean(Fps));
        Summary->SetNumberField(TEXT("jumps"), Jumps);
        Summary->SetNumberField(TEXT("autopilotDistance"), Distance);
        Report->SetObjectField(TEXT("summary"), Summary);
        WriteReport(FString::Printf(TEXT("stress-client-%u.json"), FPlatformProcess::GetCurrentProcessId()), Report);
        if (!InMatchSeen)
            Test->AddError(TEXT("Stress client never reached the match"));
    }
    FAutomationTestBase* Test;
    double Start = 0, Next = 0, Joined = 0, NextSample = 0, LastArrival = 0, MaxGap = 0, Distance = 0;
    float LastElapsed = -1;
    int32 Seconds = 40, Warmup = 12, Arrivals = 0, LastArrivals = 0, Jumps = 0, FrameCount = 0, LastFrameCount = 0;
    bool InMatchSeen = false, MeasureBegun = false;
    TMap<int32, FTrack> Tracks;
    TArray<double> Download, Rates, Fps;
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWNetStressHost, "GW.Net.StressHost", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWNetStressHost::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStressHost(this));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGWNetStressClient, "GW.Net.StressClient", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGWNetStressClient::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStressClient(this));
    return true;
}
#endif
