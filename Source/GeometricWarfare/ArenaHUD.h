#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Simulation/ArenaView.h"
#include "ArenaHUD.generated.h"
class SWidget;
class AArenaGameMode;
UCLASS()
class GEOMETRICWARFARE_API AArenaHUD : public AHUD {
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void DrawHUD() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void SelectAtCursor(float X,float Y);
    bool BeginPointer(float X,float Y);
    void UpdatePointer(float X,float Y);
    void EndPointer(float X,float Y);
    void CancelPointer();
    void ZoomAtCursor(float X,float Y,float Factor);
private:
    friend struct FArenaHUDInputAccess;
    TSharedPtr<SWidget> Controls;
    UPROPERTY(Transient) TObjectPtr<class UFont> TextFont;
    float ArenaX=0,ArenaY=0,ArenaSide=0,WorldScale=1;
    FVector2D ViewOrigin;
    // Render-only camera motion. Never written back to the view or pointer input.
    FVector2D ArenaShakeOffset=FVector2D::ZeroVector;
    float ViewportWidth=0,ViewportHeight=0;
    gw::PointerDrag PointerDrag;
    enum class EPointerArea { None,Arena,Minimap,Rank };
    EPointerArea PointerArea=EPointerArea::None;
    FBox2D MinimapBounds=FBox2D(ForceInit);
    struct FRankHitRegion { FBox2D Bounds; int32 Id=-1; };
    TArray<FRankHitRegion> RankRegions;
    FBox2D RankBounds=FBox2D(ForceInit);
    int32 RankOffset=0,RankVisibleRows=20,RankTotalRows=0;
    int32 PressedRankId=-1;
    FBox2D PressedRankBounds=FBox2D(ForceInit);
    std::array<uint8,64*64> MinimapCells{};
    float MinimapRefreshAt=-1;
    int32 ResultsRound=-1;
    float ResultsSeenAt=0;
    void Text(const FString& Value,float X,float Y,int32 Size,FLinearColor Color,bool Center=false);
    FString FitText(const FString& Value,int32 Size,float MaxWidth,bool Ellipsis=true) const;
    void Rect(float X,float Y,float Width,float Height,FLinearColor Color);
    void Line(FVector2D A,FVector2D B,FLinearColor Color,float Width=1);
    void Polygon(const TArray<FVector2D>& Points,FLinearColor Fill);
    void Portrait(UTexture2D* Texture,float X,float Y,float Size);
    void ArenaPortrait(UTexture2D* Texture,float X,float Y,float Size);
    void ArenaRect(float X,float Y,float Width,float Height,FLinearColor Color);
    FBox2D ClipArenaRect(const FBox2D& Bounds) const;
    void DrawArena(AArenaGameMode* Game);
    void DrawResults(AArenaGameMode* Game);
    void DrawMinimap(AArenaGameMode* Game);
    void DrawFocusCard(AArenaGameMode* Game,float X,float Y,float Width);
    void DrawDamage(AArenaGameMode* Game);
    void DrawCombatGround(AArenaGameMode* Game);
    void DrawCombatActors(AArenaGameMode* Game);
    void DrawSupplyStatus(FVector2D Center,float Radius,double Hp,double MaxHp,double HitFlash,const FString& Label);
    void DrawGunfire(AArenaGameMode* Game);
    void DrawBossBar(AArenaGameMode* Game);
    void DrawGiftNotice(AArenaGameMode* Game);
    void DrawRankTooltip(AArenaGameMode* Game,int32 FighterIndex,float X,float Y,float Width);
    void UpdateArenaShake(AArenaGameMode* Game);
    void DrawStatusAura(FVector2D Center,float HalfSize,bool Evolution,bool BossBuff,float Time,bool ClipToArena,bool Hero=false);
    void ArenaLine(FVector2D A,FVector2D B,FLinearColor Color,float Width=1);
    void ArenaPolygon(const TArray<FVector2D>& Points,FLinearColor Color);
    void ArenaRing(FVector2D Center,float Radius,FLinearColor Color,float Width=1,int32 Segments=32);
    void HealthBar(float X,float Y,float Width,float Height,double Hp,double MaxHp,FLinearColor Color,int32 FontSize);
    FVector2D Project(double X,double Y) const;
    bool InArena(FVector2D Point,float Margin=0) const;
};
