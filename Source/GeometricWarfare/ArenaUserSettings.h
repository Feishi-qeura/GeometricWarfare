#pragma once
#include "GameFramework/GameUserSettings.h"
#include "UI/ArenaPresentationBudget.h"
#include "ArenaUserSettings.generated.h"

UCLASS(Config=GameUserSettings)
class GEOMETRICWARFARE_API UArenaUserSettings : public UGameUserSettings {
    GENERATED_BODY()
public:
    static UArenaUserSettings* Get();
    UPROPERTY(Config) float BgmVolume=.55f;
    UPROPERTY(Config) float SfxVolume=.70f;
    virtual void SetToDefaults() override;
    virtual void ValidateSettings() override;
    // 0 automatic, 1 full, 2 reduced.
    UPROPERTY(Config) int32 EffectMode=0;
    UPROPERTY(Config) int32 BattlefieldLabels=180;
    UPROPERTY(Config) int32 FloatingNumbers=128;
    // 0 automatic, 1 PC, 2 mobile. Aspect: 0 viewport; 1..5 the advertised presets.
    UPROPERTY(Config) int32 LayoutDevice=0;
    UPROPERTY(Config) int32 LayoutAspect=0;
    gwui::PresentationBudget Budget(int32 Participants) const;
    void ApplyPresentationSettings();
};
