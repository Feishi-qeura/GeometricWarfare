#include "ArenaLiveRoundOutbox.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/Archive.h"

namespace {
constexpr int64 MaxFileBytes=32*1024*1024;
bool KnownOperation(const FString& Op) {return Op==TEXT("round")||Op==TEXT("user_group")||Op==TEXT("backend_round")||Op==TEXT("user_results")||Op==TEXT("room_rank")||Op==TEXT("complete");}
bool Tokenless(const TSharedPtr<FJsonValue>& Value) {
    if(!Value)return false;
    if(Value->Type==EJson::Object)for(const auto& Pair:Value->AsObject()->Values) {
        const FString Key=FString(*Pair.Key).ToLower();
        if(Key.Contains(TEXT("token"))||Key==TEXT("authorization")||Key.Contains(TEXT("secret"))||!Tokenless(Pair.Value))return false;
    }
    else if(Value->Type==EJson::Array)for(const auto& Item:Value->AsArray())if(!Tokenless(Item))return false;
    return true;
}
bool Tokenless(const TSharedPtr<FJsonObject>& Object) {return Object&&Tokenless(MakeShared<FJsonValueObject>(Object));}
FString Encode(const TSharedRef<FJsonObject>& Object) {
    FString Text;FJsonSerializer::Serialize(Object,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));return Text;
}
FString Directory(const FLiveSession& Session) {
    const auto Part=[](const FString& V){return FString::FromInt(V.Len())+TEXT(":")+V;};
    const FString Scope=Part(Session.PlatformId)+Part(Session.AppId)+Part(Session.RoomId);FTCHARToUTF8 Utf8(*Scope);uint8 Hash[20];FSHA1::HashBuffer(Utf8.Get(),Utf8.Length(),Hash);
    return FPaths::ProjectSavedDir()/TEXT("LivePlatform/RoundOutbox")/BytesToHex(Hash,UE_ARRAY_COUNT(Hash));
}
bool AtomicWrite(const FString& Path,const FString& Text) {
    const FString Temp=Path+TEXT(".tmp");return FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)&&IFileManager::Get().Move(*Path,*Temp,true,true);
}
bool ReadBounded(const FString& Path,FString& Text) {
    const int64 Size=IFileManager::Get().FileSize(*Path);return Size>=0&&Size<=MaxFileBytes&&FFileHelper::LoadFileToString(Text,*Path);
}
bool Integer(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,int32& Value) {
    double Number=0;if(!Object->HasTypedField<EJson::Number>(Key)||!Object->TryGetNumberField(Key,Number)||!FMath::IsFinite(Number)||Number<0||Number>MAX_int32||FMath::FloorToDouble(Number)!=Number)return false;Value=static_cast<int32>(Number);return true;
}
}
bool FArenaLiveRoundOutbox::Save(const FLiveSession& Session,int64 RoundId,int64 StartedAt,int32 MatchRound,const TArray<FArenaRoundReportStep>& Steps,FString& Path) {
    Path.Reset();if(Session.bLocalTest||Session.PlatformId.IsEmpty()||Session.AppId.IsEmpty()||Session.RoomId.IsEmpty()||RoundId<=0||StartedAt<0||MatchRound<1||Steps.IsEmpty()||Steps.Num()>6000)return false;
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("schema"),1);Root->SetStringField(TEXT("platform"),Session.PlatformId);Root->SetStringField(TEXT("app_id"),Session.AppId);Root->SetStringField(TEXT("room_id"),Session.RoomId);
    Root->SetStringField(TEXT("round_id"),LexToString(RoundId));Root->SetStringField(TEXT("start_time"),LexToString(StartedAt));Root->SetNumberField(TEXT("match_round"),MatchRound);
    TArray<TSharedPtr<FJsonValue>> Values;
    for(const auto& Step:Steps){if(!KnownOperation(Step.Operation)||!Tokenless(Step.Payload))return false;auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("operation"),Step.Operation);Item->SetObjectField(TEXT("payload"),Step.Payload);Values.Add(MakeShared<FJsonValueObject>(Item));}
    Root->SetArrayField(TEXT("steps"),Values);const FString Text=Encode(Root);FTCHARToUTF8 Bytes(*Text);if(Bytes.Length()>MaxFileBytes)return false;
    const FString Dir=Directory(Session);IFileManager::Get().MakeDirectory(*Dir,true);const FString Target=Dir/(LexToString(RoundId)+TEXT(".json"));
    if(FPaths::FileExists(Target)){FString Existing;if(!ReadBounded(Target,Existing)||Existing!=Text)return false;}
    else if(!AtomicWrite(Target,Text))return false;
    Path=Target;return true;
}
bool FArenaLiveRoundOutbox::AppendReceipt(const FString& Path,int32 Cursor,const FString& Operation,TSharedPtr<FJsonObject> Data) {
    if(Path.IsEmpty()||Cursor<1||!KnownOperation(Operation)||(Data&&!Tokenless(Data)))return false;
    const FString Journal=Path+TEXT(".journal");
    if(FPaths::FileExists(Journal)){
        TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Journal));if(!Reader||Reader->TotalSize()>MaxFileBytes)return false;
        uint8 Last='\n';if(Reader->TotalSize()>0){Reader->Seek(Reader->TotalSize()-1);Reader->Serialize(&Last,1);}if(Reader->IsError())return false;Reader.Reset();
        // Normal appends inspect one byte; only a torn final record needs a full read.
        if(Last!='\n'){FString Existing;if(!ReadBounded(Journal,Existing))return false;const int32 End=Existing.Find(TEXT("\n"),ESearchCase::CaseSensitive,ESearchDir::FromEnd);if(!AtomicWrite(Journal,End==INDEX_NONE?FString():Existing.Left(End+1)))return false;}}
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("cursor"),Cursor);Root->SetStringField(TEXT("operation"),Operation);if(Data)Root->SetObjectField(TEXT("data"),Data);
    const FString Line=Encode(Root)+TEXT("\n");FTCHARToUTF8 Bytes(*Line);if(FMath::Max(int64(0),IFileManager::Get().FileSize(*Journal))+Bytes.Length()>MaxFileBytes)return false;
    return FFileHelper::SaveStringToFile(Line,*Journal,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
}
bool FArenaLiveRoundOutbox::LoadOldest(const FLiveSession& Session,FArenaStoredRound& Record,FString& Error) {
    Record=FArenaStoredRound();Error.Reset();TArray<FString> Files;const FString Dir=Directory(Session);IFileManager::Get().FindFiles(Files,*(Dir/TEXT("*.json")),true,false);
    Files.Sort([](const FString& A,const FString& B){return FCString::Atoi64(*A)<FCString::Atoi64(*B);});
    for(const auto& File:Files){FArenaStoredRound Candidate;Candidate.Path=Dir/File;FString Text;TSharedPtr<FJsonObject> Root;
        const auto Fail=[&](){Error=TEXT("历史对局上报记录不可读取，保留文件等待恢复");return false;};
        if(!ReadBounded(Candidate.Path,Text)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root||!Tokenless(Root))return Fail();
        FString Platform,App,Room,Round,Start;int32 Schema=0;
        if(!Integer(Root,TEXT("schema"),Schema)||Schema!=1||!Root->TryGetStringField(TEXT("platform"),Platform)||Platform!=Session.PlatformId||!Root->TryGetStringField(TEXT("app_id"),App)||App!=Session.AppId||!Root->TryGetStringField(TEXT("room_id"),Room)||Room!=Session.RoomId
            ||!Root->HasTypedField<EJson::String>(TEXT("round_id"))||!Root->TryGetStringField(TEXT("round_id"),Round)||!LexTryParseString(Candidate.RoundId,*Round)||Candidate.RoundId<=0||LexToString(Candidate.RoundId)!=Round||File!=Round+TEXT(".json")
            ||!Root->HasTypedField<EJson::String>(TEXT("start_time"))||!Root->TryGetStringField(TEXT("start_time"),Start)||!LexTryParseString(Candidate.StartedAt,*Start)||Candidate.StartedAt<0||LexToString(Candidate.StartedAt)!=Start||!Integer(Root,TEXT("match_round"),Candidate.MatchRound)||Candidate.MatchRound<1)return Fail();
        const TArray<TSharedPtr<FJsonValue>>* Steps=nullptr;if(!Root->TryGetArrayField(TEXT("steps"),Steps)||Steps->IsEmpty()||Steps->Num()>6000)return Fail();
        for(const auto& Value:*Steps){if(!Value||Value->Type!=EJson::Object)return Fail();auto Item=Value->AsObject();FString Op;const TSharedPtr<FJsonObject>* Payload=nullptr;if(!Item->TryGetStringField(TEXT("operation"),Op)||!KnownOperation(Op)||!Item->TryGetObjectField(TEXT("payload"),Payload)||!(*Payload))return Fail();Candidate.Steps.Add({Op,*Payload});}
        const FString Journal=Candidate.Path+TEXT(".journal");
        if(FPaths::FileExists(Journal)){if(!ReadBounded(Journal,Text))return Fail();const int32 Last=Text.Find(TEXT("\n"),ESearchCase::CaseSensitive,ESearchDir::FromEnd);Text=Last==INDEX_NONE?FString():Text.Left(Last+1);TArray<FString> Lines;Text.ParseIntoArrayLines(Lines,true);
            for(const auto& Line:Lines){TSharedPtr<FJsonObject> Receipt;int32 Cursor=0;FString Op;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line),Receipt)||!Receipt||!Tokenless(Receipt)||!Integer(Receipt,TEXT("cursor"),Cursor)||Cursor!=Candidate.Cursor+1||Cursor>Candidate.Steps.Num()||!Receipt->TryGetStringField(TEXT("operation"),Op)||Op!=Candidate.Steps[Candidate.Cursor].Operation)return Fail();
                if(Op==TEXT("backend_round")){const TSharedPtr<FJsonObject>* Data=nullptr;if(!Receipt->TryGetObjectField(TEXT("data"),Data)||!(*Data))return Fail();Candidate.BackendReceipt=*Data;}Candidate.Cursor=Cursor;}}
        if(Candidate.Cursor==Candidate.Steps.Num()){Remove(Candidate.Path);continue;}
        Record=MoveTemp(Candidate);return true;
    }
    return false;
}
bool FArenaLiveRoundOutbox::Remove(const FString& Path) {
    if(Path.IsEmpty())return true;bool Success=true;for(const FString& File:{Path,Path+TEXT(".journal"),Path+TEXT(".tmp"),Path+TEXT(".journal.tmp")})if(FPaths::FileExists(File))Success=IFileManager::Get().Delete(*File,false,true)&&Success;return Success;
}
