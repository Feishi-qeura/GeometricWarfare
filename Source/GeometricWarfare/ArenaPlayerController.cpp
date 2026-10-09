#include "ArenaPlayerController.h"
#include "ArenaGameMode.h"
#include "ArenaHUD.h"
#include "Components/InputComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"
void AArenaPlayerController::BeginPlay() {
    Super::BeginPlay(); bShowMouseCursor=true;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
}
void AArenaPlayerController::GWComment(const FString& UserId,const FString& Nickname,const FString& Content) {
    if(auto* Game=GetWorld()->GetAuthGameMode<AArenaGameMode>()) Game->GetBridge()->SimulateComment(UserId,Nickname,Content);
}
void AArenaPlayerController::GWConnectRelay(const FString& Url,const FString& RoomId) {
    if(!ULiveInteractionSubsystem::IsValidRelayEndpoint(Url,RoomId)) return;
    if(auto* Game=GetWorld()->GetAuthGameMode<AArenaGameMode>()) {
        if(!Game->GetBridge()->IsLocalTestMode()) return;
        Game->GetBridge()->ConnectRelay(Url,RoomId);
    }
}
void AArenaPlayerController::GWDisconnect() { if(auto* Game=GetWorld()->GetAuthGameMode<AArenaGameMode>()) Game->GetBridge()->DisconnectRelay(); }
void AArenaPlayerController::GWAdd(int32 Count) { if(auto* Game=GetWorld()->GetAuthGameMode<AArenaGameMode>()) Game->AddMockUsers(FMath::Clamp(Count,1,gw::Match::ViewerCapacity)); }
void AArenaPlayerController::GWAction(const FString& Action) { if(auto* Game=GetWorld()->GetAuthGameMode<AArenaGameMode>()) Game->DemoAction(Action); }
void AArenaPlayerController::GWSpeed(float Speed) { if(auto* Game=GetWorld()->GetAuthGameMode<AArenaGameMode>()) Game->SetDemoSpeed(Speed); }
void AArenaPlayerController::GWFocus(int32 Id) { if(auto* Game=GetWorld()->GetAuthGameMode<AArenaGameMode>()) Game->FocusViewer(Id); }
void AArenaPlayerController::SetupInputComponent() {
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::MouseScrollUp,IE_Pressed,this,&AArenaPlayerController::ZoomIn);
    InputComponent->BindKey(EKeys::MouseScrollDown,IE_Pressed,this,&AArenaPlayerController::ZoomOut);
    InputComponent->BindKey(EKeys::A,IE_Pressed,this,&AArenaPlayerController::CameraLeft);
    InputComponent->BindKey(EKeys::D,IE_Pressed,this,&AArenaPlayerController::CameraRight);
    InputComponent->BindKey(EKeys::W,IE_Pressed,this,&AArenaPlayerController::CameraUp);
    InputComponent->BindKey(EKeys::S,IE_Pressed,this,&AArenaPlayerController::CameraDown);
    InputComponent->BindKey(EKeys::LeftMouseButton,IE_Pressed,this,&AArenaPlayerController::PointerPressed);
    InputComponent->BindKey(EKeys::LeftMouseButton,IE_Released,this,&AArenaPlayerController::PointerReleased);
    InputComponent->BindKey(EKeys::RightMouseButton,IE_Pressed,this,&AArenaPlayerController::UnlockCamera);
    InputComponent->BindKey(EKeys::Home,IE_Pressed,this,&AArenaPlayerController::ShowOverview);
    InputComponent->BindKey(EKeys::Escape,IE_Pressed,this,&AArenaPlayerController::ToggleSettings);
    InputComponent->BindKey(EKeys::G,IE_Pressed,this,&AArenaPlayerController::ToggleGM);
}
bool AArenaPlayerController::SettingsOpen() const {const auto* H=Cast<AArenaHUD>(GetHUD());return H && H->IsOverlayOpen();}
void AArenaPlayerController::ToggleSettings(){CancelPointer();if(auto* H=Cast<AArenaHUD>(GetHUD()))H->ToggleSettings();}
void AArenaPlayerController::ToggleGM(){CancelPointer();if(auto* H=Cast<AArenaHUD>(GetHUD()))H->ToggleGM();}
void AArenaPlayerController::ZoomIn(){float X,Y;if(ReadPointer(X,Y))if(auto* H=Cast<AArenaHUD>(GetHUD()))H->ZoomAtCursor(X,Y,1.25f);}
void AArenaPlayerController::ZoomOut(){float X,Y;if(ReadPointer(X,Y))if(auto* H=Cast<AArenaHUD>(GetHUD()))H->ZoomAtCursor(X,Y,.8f);}
void AArenaPlayerController::CameraLeft(){if(SettingsOpen())return;if(auto* G=GetWorld()->GetAuthGameMode<AArenaGameMode>())if(G->FocusBodyId<0)G->MoveCamera(-1,0);}
void AArenaPlayerController::CameraRight(){if(SettingsOpen())return;if(auto* G=GetWorld()->GetAuthGameMode<AArenaGameMode>())if(G->FocusBodyId<0)G->MoveCamera(1,0);}
void AArenaPlayerController::CameraUp(){if(SettingsOpen())return;if(auto* G=GetWorld()->GetAuthGameMode<AArenaGameMode>())if(G->FocusBodyId<0)G->MoveCamera(0,-1);}
void AArenaPlayerController::CameraDown(){if(SettingsOpen())return;if(auto* G=GetWorld()->GetAuthGameMode<AArenaGameMode>())if(G->FocusBodyId<0)G->MoveCamera(0,1);}
bool AArenaPlayerController::ReadPointer(float& X,float& Y) const {
    const auto* ViewportClient=GetWorld()?GetWorld()->GetGameViewport():nullptr;
    if(!ViewportClient || !ViewportClient->Viewport || !GetMousePosition(X,Y)) return false;
    if(!FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsActive()) return false;
    // Viewport::HasFocus only checks keyboard focus, which may legitimately be
    // held by an overlay control while the pointer is back above the map.
    const TSharedPtr<SWindow> Window=ViewportClient->GetWindow();
    if(Window.IsValid() && !Window->IsActive()) return false;
    const FIntPoint Size=ViewportClient->Viewport->GetSizeXY();
    return FMath::IsFinite(X) && FMath::IsFinite(Y) && X>=0 && Y>=0 && X<Size.X && Y<Size.Y;
}
void AArenaPlayerController::PointerPressed() {
    CancelPointer();
    float X,Y;
    if(!ReadPointer(X,Y)) return;
    if(auto* H=Cast<AArenaHUD>(GetHUD())) if(H->BeginPointer(X,Y)) {
        PointerHUD=H; bPointerActive=true;
    }
}
void AArenaPlayerController::PointerReleased() {
    if(!bPointerActive) return;
    float X,Y;
    if(!PointerHUD.IsValid() || !ReadPointer(X,Y)) { CancelPointer(); return; }
    // EndPointer updates the final position before deciding between click and drag.
    PointerHUD->EndPointer(X,Y);
    bPointerActive=false; PointerHUD.Reset();
}
void AArenaPlayerController::PlayerTick(float DeltaTime) {
    Super::PlayerTick(DeltaTime);
    if(!bPointerActive) return;
    float X,Y;
    if(!PointerHUD.IsValid() || !ReadPointer(X,Y) || !IsInputKeyDown(EKeys::LeftMouseButton)) { CancelPointer(); return; }
    PointerHUD->UpdatePointer(X,Y);
}
void AArenaPlayerController::CancelPointer() {
    if(PointerHUD.IsValid()) PointerHUD->CancelPointer();
    bPointerActive=false; PointerHUD.Reset();
}
void AArenaPlayerController::UnlockCamera() {
    if(SettingsOpen())return;
    CancelPointer();
    if(auto* H=Cast<AArenaHUD>(GetHUD())) H->CancelPointer();
    if(auto* G=GetWorld()->GetAuthGameMode<AArenaGameMode>()) G->FocusBodyId=-1;
}
void AArenaPlayerController::EndPlay(const EEndPlayReason::Type Reason) { CancelPointer(); Super::EndPlay(Reason); }
void AArenaPlayerController::ShowOverview(){if(SettingsOpen())return;if(auto* G=GetWorld()->GetAuthGameMode<AArenaGameMode>())if(G->FocusBodyId<0)G->Overview();}
