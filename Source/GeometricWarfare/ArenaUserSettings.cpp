#include "ArenaUserSettings.h"

UArenaUserSettings* UArenaUserSettings::Get() {return Cast<UArenaUserSettings>(UGameUserSettings::GetGameUserSettings());}
void UArenaUserSettings::SetToDefaults() {
    Super::SetToDefaults();BgmVolume=.55f;SfxVolume=.70f;SetVSyncEnabled(false);SetFrameRateLimit(60);
    EffectMode=0;BattlefieldLabels=180;FloatingNumbers=128;
    LayoutDevice=0;LayoutAspect=0;
}
void UArenaUserSettings::ValidateSettings() {
    Super::ValidateSettings();
    BgmVolume=FMath::IsFinite(BgmVolume)?FMath::Clamp(BgmVolume,0.f,1.f):.55f;
    SfxVolume=FMath::IsFinite(SfxVolume)?FMath::Clamp(SfxVolume,0.f,1.f):.70f;
    EffectMode=FMath::Clamp(EffectMode,0,2);
    BattlefieldLabels=FMath::Clamp(BattlefieldLabels,0,180);
    FloatingNumbers=FMath::Clamp(FloatingNumbers,0,128);
    LayoutDevice=FMath::Clamp(LayoutDevice,0,2);LayoutAspect=FMath::Clamp(LayoutAspect,0,5);
    const float FPS=GetFrameRateLimit();
    if(FPS!=0 && FPS!=30 && FPS!=60 && FPS!=90 && FPS!=120)SetFrameRateLimit(60);
}
gwui::PresentationBudget UArenaUserSettings::Budget(int32 Participants) const {
    return gwui::ResolvePresentationBudget(EffectMode,Participants,BattlefieldLabels,FloatingNumbers);
}
void UArenaUserSettings::ApplyPresentationSettings() {
    ValidateSettings();ApplyNonResolutionSettings();SaveSettings();
}
