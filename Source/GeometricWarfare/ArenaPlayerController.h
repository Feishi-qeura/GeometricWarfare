#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ArenaPlayerController.generated.h"
class AArenaHUD;
UCLASS()
class GEOMETRICWARFARE_API AArenaPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(Exec) void GWComment(const FString& UserId,const FString& Nickname,const FString& Content);
    UFUNCTION(Exec) void GWConnectRelay(const FString& Url,const FString& RoomId);
    UFUNCTION(Exec) void GWDisconnect();
    UFUNCTION(Exec) void GWAdd(int32 Count=1);
    UFUNCTION(Exec) void GWAction(const FString& Action);
    UFUNCTION(Exec) void GWSpeed(float Speed=1);
    UFUNCTION(Exec) void GWFocus(int32 Id);
private:
    friend struct FArenaHUDInputAccess;
    void ZoomIn();
    void ZoomOut();
    void CameraLeft();
    void CameraRight();
    void CameraUp();
    void CameraDown();
    void PointerPressed();
    void PointerReleased();
    void UnlockCamera();
    void CancelPointer();
    bool ReadPointer(float& X,float& Y) const;
    void ShowOverview();
    void ToggleControls();
    bool bPointerActive=false;
    TWeakObjectPtr<AArenaHUD> PointerHUD;
};
