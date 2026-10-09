#include "ArenaLiveRoundOutbox.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaRoundOutboxTest,"GeometricWarfare.Arena.RoundOutbox",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaRoundOutboxTest::RunTest(const FString&) {
    FLiveSession Session;Session.PlatformId=TEXT("outbox-test-")+FGuid::NewGuid().ToString(EGuidFormats::Digits);Session.AppId=TEXT("app");Session.RoomId=TEXT("room");
    auto Payload=MakeShared<FJsonObject>();Payload->SetStringField(TEXT("round_id"),TEXT("123"));
    TArray<FArenaRoundReportStep> Steps={{TEXT("backend_round"),Payload},{TEXT("complete"),Payload}};FString Path;
    TestTrue(TEXT("Frozen round steps are atomically persisted"),FArenaLiveRoundOutbox::Save(Session,123,100,1,Steps,Path));
    ON_SCOPE_EXIT {FArenaLiveRoundOutbox::Remove(Path);};
    FArenaStoredRound Loaded;FString Error;
    TestTrue(TEXT("Same app and room can recover the complete snapshot"),FArenaLiveRoundOutbox::LoadOldest(Session,Loaded,Error));
    TestEqual(TEXT("Initial cursor starts before any receipt"),Loaded.Cursor,0);TestEqual(TEXT("Recovered step count retained"),Loaded.Steps.Num(),2);
    auto Receipt=MakeShared<FJsonObject>();Receipt->SetBoolField(TEXT("accepted"),true);Receipt->SetStringField(TEXT("round_id"),TEXT("123"));Receipt->SetStringField(TEXT("world_rank_version"),TEXT("week-v1"));
    TestTrue(TEXT("Backend response and cursor are durably recorded together"),FArenaLiveRoundOutbox::AppendReceipt(Path,1,TEXT("backend_round"),Receipt));
    TestTrue(TEXT("Restart reads confirmed cursor"),FArenaLiveRoundOutbox::LoadOldest(Session,Loaded,Error));
    TestEqual(TEXT("Confirmed command is not replayed"),Loaded.Cursor,1);if(!TestTrue(TEXT("Committed backend response exists"),Loaded.BackendReceipt.IsValid()))return false;
    TestEqual(TEXT("Committed backend response is retained"),Loaded.BackendReceipt->GetStringField(TEXT("world_rank_version")),FString(TEXT("week-v1")));
    auto Other=Session;Other.RoomId=TEXT("other-room");TestFalse(TEXT("Cross-room session cannot inject a saved snapshot"),FArenaLiveRoundOutbox::LoadOldest(Other,Loaded,Error));TestTrue(TEXT("Cross-room absence is not corruption"),Error.IsEmpty());
    Other=Session;Other.AppId=TEXT("other-app");TestFalse(TEXT("Cross-application outbox is isolated"),FArenaLiveRoundOutbox::LoadOldest(Other,Loaded,Error));
    FString Journal=Path+TEXT(".journal");
    FFileHelper::SaveStringToFile(TEXT("{\"cursor\":2"),*Journal,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    TestTrue(TEXT("A torn final journal record does not consume a receipt"),FArenaLiveRoundOutbox::LoadOldest(Session,Loaded,Error));TestEqual(TEXT("Cursor remains at last complete record"),Loaded.Cursor,1);
    TestTrue(TEXT("New receipt safely replaces torn tail"),FArenaLiveRoundOutbox::AppendReceipt(Path,2,TEXT("complete")));
    TestFalse(TEXT("Completed job is not replayed after restart"),FArenaLiveRoundOutbox::LoadOldest(Session,Loaded,Error));TestTrue(TEXT("Completed outbox is cleanly drained"),Error.IsEmpty());
    Payload->SetStringField(TEXT("session_token"),TEXT("must-never-persist"));
    TestFalse(TEXT("Credential fields cannot enter a snapshot"),FArenaLiveRoundOutbox::Save(Session,124,100,1,Steps,Path));
    return true;
}
#endif
