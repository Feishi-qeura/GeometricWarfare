#include "DouyinHostTransport.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

bool FDouyinHostTransport::Launch(const FString& Executable,const FString& EnvironmentArgs)
{
    void* WriteChild=nullptr;void* ReadChild=nullptr;
    if(!FPlatformProcess::CreatePipe(ReadParent,WriteChild) || !FPlatformProcess::CreatePipe(ReadChild,WriteParent,true)) {
        FPlatformProcess::ClosePipe(ReadParent,WriteChild);FPlatformProcess::ClosePipe(ReadChild,WriteParent);
        ReadParent=WriteParent=nullptr;return false;
    }
    // Credentials are sent in the first pipe record, never in process arguments.
    // Unity redirects its standard output with logFile; '-' preserves the
    // inherited pipe. Discard every diagnostic line without our protocol prefix.
    const FString SessionDirectory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("LivePlatform/HostSessions")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*SessionDirectory,true);
    const FString Arguments=TEXT("-batchmode -nographics -logFile - -screen-fullscreen 0")+EnvironmentArgs;
    Process=FPlatformProcess::CreateProc(*Executable,*Arguments,false,true,true,nullptr,0,
        *SessionDirectory,WriteChild,ReadChild,WriteChild);
    FPlatformProcess::ClosePipe(ReadChild,WriteChild);
    if(!Process.IsValid()) {FPlatformProcess::ClosePipe(ReadParent,WriteParent);ReadParent=WriteParent=nullptr;return false;}
    Thread.Reset(FRunnableThread::Create(this,TEXT("DouyinSdkPipe")));
    if(!Thread) {Shutdown();return false;}
    return true;
}
bool FDouyinHostTransport::Enqueue(const FString& Json)
{
    const int64 MemoryBytes=static_cast<int64>(Json.Len())*sizeof(TCHAR);
    if(bStop.Load() || bFailed.Load() || Json.Len()>4*1024*1024 || OutgoingCount.Load()>=1024||OutgoingBytes.Load()+MemoryBytes>16*1024*1024)return false;
    // Match the host's byte limit for UTF-8, including non-ASCII identities.
    const FTCHARToUTF8 Utf8(*Json);
    if(Utf8.Length()>4*1024*1024)return false;
    ++OutgoingCount;OutgoingBytes+=MemoryBytes;Outgoing.Enqueue(Json);return true;
}
bool FDouyinHostTransport::Dequeue(FString& Json)
{
    if(!Incoming.Dequeue(Json))return false;
    --IncomingCount;IncomingBytes-=static_cast<int64>(Json.Len())*sizeof(TCHAR);return true;
}
uint32 FDouyinHostTransport::Run()
{
    TArray<uint8> Pending;
    while(!bStop.Load()) {
        if(!FPlatformProcess::IsProcRunning(Process)) {bFailed.Store(true);break;}
        TArray<uint8> Bytes;
        if(FPlatformProcess::ReadPipeToArray(ReadParent,Bytes)) {
            Pending.Append(Bytes);
            int32 Start=0;
            for(int32 I=0;I<Pending.Num();++I) if(Pending[I]=='\n') {
                if(I-Start>4*1024*1024) {bFailed.Store(true);return 0;}
                // Decode only complete UTF-8 records; pipe chunks can split Chinese text.
                FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Pending.GetData()+Start),I-Start);
                FString Line(Text.Length(),Text.Get());Start=I+1;
                if(Line.StartsWith(TEXT("GWSDK/1 "))) {
                    FString Frame=Line.Mid(8).TrimEnd();const int64 MemoryBytes=static_cast<int64>(Frame.Len())*sizeof(TCHAR);
                    if(IncomingCount.Load()>=4096||IncomingBytes.Load()+MemoryBytes>16*1024*1024) {bFailed.Store(true);return 0;}
                    ++IncomingCount;IncomingBytes+=MemoryBytes;Incoming.Enqueue(MoveTemp(Frame));
                }
            }
            if(Start)Pending.RemoveAt(0,Start,EAllowShrinking::No);
            if(Pending.Num()>4*1024*1024) {bFailed.Store(true);break;}
        }
        FString Command;
        // Blocking pipe writes stay off the game thread. Killing our child during
        // shutdown releases any pending write if the host no longer drains stdin.
        for(int32 Batch=0;Batch<64&&Outgoing.Dequeue(Command);++Batch) {
            --OutgoingCount;OutgoingBytes-=static_cast<int64>(Command.Len())*sizeof(TCHAR);
            if(!FPlatformProcess::WritePipe(WriteParent,Command)) {bFailed.Store(true);return 0;}
            Command.Reset();
        }
        FPlatformProcess::Sleep(.005f);
    }
    return 0;
}
void FDouyinHostTransport::Shutdown()
{
    if(Thread && Process.IsValid() && !bFailed.Load() && FPlatformProcess::IsProcRunning(Process)) {
        Enqueue(TEXT("{\"op\":\"stop\",\"id\":\"shutdown\"}"));
        const double Deadline=FPlatformTime::Seconds()+2;
        // Worker keeps draining output while Unity shuts down.
        while(FPlatformProcess::IsProcRunning(Process) && FPlatformTime::Seconds()<Deadline)FPlatformProcess::Sleep(.01f);
    }
    bStop.Store(true);
    if(Process.IsValid()) {if(FPlatformProcess::IsProcRunning(Process))FPlatformProcess::TerminateProc(Process,false);}
    if(Thread) {Thread->WaitForCompletion();Thread.Reset();}
    if(Process.IsValid())FPlatformProcess::CloseProc(Process);
    FPlatformProcess::ClosePipe(ReadParent,WriteParent);ReadParent=WriteParent=nullptr;
    FString Discard;while(Outgoing.Dequeue(Discard))Discard.Reset();while(Incoming.Dequeue(Discard))Discard.Reset();
}
