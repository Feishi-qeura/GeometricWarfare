#include "LiveInteractionProvider.h"
namespace {
TMap<FString, FLiveInteractionProviderRegistry::FFactory>& Factories() {
    static TMap<FString, FLiveInteractionProviderRegistry::FFactory> Value;
    return Value;
}
}
bool FLiveInteractionProviderRegistry::Register(const FString& PlatformId, FFactory Factory) {
    check(IsInGameThread());
    if(PlatformId.IsEmpty() || PlatformId.Len()>64 || PlatformId!=PlatformId.TrimStartAndEnd() || !Factory || Factories().Contains(PlatformId)) return false;
    Factories().Add(PlatformId,MoveTemp(Factory)); return true;
}
void FLiveInteractionProviderRegistry::Unregister(const FString& PlatformId) {
    check(IsInGameThread()); Factories().Remove(PlatformId);
}
TSharedPtr<ILiveInteractionProvider> FLiveInteractionProviderRegistry::Create(const FString& PlatformId) {
    check(IsInGameThread());
    const FFactory* Factory=Factories().Find(PlatformId);
    return Factory?(*Factory)():nullptr;
}
