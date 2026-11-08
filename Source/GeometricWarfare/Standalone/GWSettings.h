#pragma once
#include "Core/Types.h"
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameFramework/GameUserSettings.h"
#include "GWSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Geometric Warfare"))
class GEOMETRICWARFARE_API UGWRuntimeSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category = "Match", meta = (ClampMin = "120")) float MatchSeconds = 600;
    UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = "50", ClampMax = "1000"))
    float PlayerSpeed = 300;
    UPROPERTY(Config, EditAnywhere, Category = "Companions", meta = (ClampMin = "60", ClampMax = "400"))
    float RecruitDistance = 165;
    UPROPERTY(Config, EditAnywhere, Category = "Companions", meta = (ClampMin = "50", ClampMax = "4000"))
    float LaunchSpeed = 1550;
    UPROPERTY(Config, EditAnywhere, Category = "Companions", meta = (ClampMin = "1", ClampMax = "1000"))
    float LaunchDamage = 100;
    UPROPERTY(Config, EditAnywhere, Category = "Evolution", meta = (ClampMin = "1")) float RecruitInterval = 5;
    UPROPERTY(Config, EditAnywhere, Category = "Evolution", meta = (ClampMin = "1", ClampMax = "300"))
    int32 MaxMonsters = 160;
    gwgame::BattleConfig Rules() const;
    virtual FName GetCategoryName() const override
    {
        return TEXT("Game");
    }
};
UCLASS(Config = GameUserSettings)
class GEOMETRICWARFARE_API UGWUserSettings : public UGameUserSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config) FString Language;
    UPROPERTY(Config) float MusicVolume = .45f;
    UPROPERTY(Config) float EffectsVolume = .7f;
    UPROPERTY(Config) float CameraDistance = 1650;
    // The player profile (save slot) in use; profiles live in Saved/SaveGames/GWProfile<N>.sav.
    UPROPERTY(Config) int32 ProfileSlot = 0;
    // Throwing teammates: whether this player lets others throw them in Friendly rooms, and the trolling
    // level (gwgame::PrankLevel) this player's rooms open with when hosting.
    UPROPERTY(Config) bool Throwable = false;
    UPROPERTY(Config) int32 PrankLevel = 1;
    // The difficulty tier this player's rooms open with when hosting (capped by what their profile has opened).
    UPROPERTY(Config) int32 Tier = 1;
    // Readability switches (design doc: 8 人同屏的可读性).
    UPROPERTY(Config) bool ScreenShake = true;
    UPROPERTY(Config) bool HitFlash = true;
    UPROPERTY(Config) bool DamageNumbers = true;
    virtual void SetToDefaults() override;
    virtual void ValidateSettings() override;
    static UGWUserSettings* Get();
};
