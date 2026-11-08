#include "GWSettings.h"
#include "Engine/Engine.h"
gwgame::BattleConfig UGWRuntimeSettings::Rules() const
{
    gwgame::BattleConfig C;
    C.duration = FMath::Clamp(MatchSeconds, 120.f, 3600.f);
    C.playerSpeed = FMath::Clamp(PlayerSpeed, 50.f, 1000.f);
    C.recruitRange = FMath::Clamp(RecruitDistance, 60.f, 400.f);
    C.launchSpeed = FMath::Clamp(LaunchSpeed, 50.f, 4000.f);
    C.launchDamage = FMath::Clamp(LaunchDamage, 1.f, 1000.f);
    C.recruitInterval = FMath::Max(1.f, RecruitInterval);
    C.maxMonsters = FMath::Clamp(MaxMonsters, 1, 300);
    return C;
}
UGWUserSettings* UGWUserSettings::Get()
{
    return GEngine ? Cast<UGWUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}
void UGWUserSettings::SetToDefaults()
{
    Super::SetToDefaults();
    MusicVolume = .45f;
    EffectsVolume = .7f;
    CameraDistance = 1650;
    Language.Empty();
    Throwable = false;
    PrankLevel = 1;
    Tier = 1;
    ScreenShake = HitFlash = DamageNumbers = true;
}
void UGWUserSettings::ValidateSettings()
{
    Super::ValidateSettings();
    MusicVolume = FMath::IsFinite(MusicVolume) ? FMath::Clamp(MusicVolume, 0.f, 1.f) : .45f;
    EffectsVolume = FMath::IsFinite(EffectsVolume) ? FMath::Clamp(EffectsVolume, 0.f, 1.f) : .7f;
    CameraDistance = FMath::IsFinite(CameraDistance) ? FMath::Clamp(CameraDistance, 900.f, 2600.f) : 1650;
    ProfileSlot = FMath::Clamp(ProfileSlot, 0, 2);
    PrankLevel = FMath::Clamp(PrankLevel, 0, 2);
    Tier = FMath::Clamp(Tier, 1, 5);
}
