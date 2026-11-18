#include "GWController.h"
#include "Components/InputComponent.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GWGameInstance.h"
#include "GWGameMode.h"
#include "GWGameState.h"
#include "GWHUD.h"
#include "GWPlayerState.h"
#include "GWSettings.h"
#include "GWWorld.h"
#include "InputKeyEventArgs.h"
void AGWController::BeginPlay()
{
    Super::BeginPlay();
    bShowMouseCursor = true;
    if (!IsLocalController())
        return;
    FInputModeGameAndUI Input;
    Input.SetHideCursorDuringCapture(false);
    Input.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
    SetInputMode(Input);
    WorldView = GetWorld()->SpawnActor<AGWWorld>();
    SetViewTarget(WorldView);
    // Every room learns this player's own throw permission and profile report as soon as they arrive.
    if (const auto* Settings = UGWUserSettings::Get())
        SendThrowable(Settings->Throwable);
    SendProfile();
}
void AGWController::SendProfile()
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    if (IsLocalController() && GI)
        ServerProfile(GI->ProfileDigest());
}
void AGWController::ServerProfile_Implementation(const FGWProfileDigest& Digest)
{
    if (auto* G = GetWorld()->GetAuthGameMode<AGWGameMode>())
        G->SetProfile(this, Digest);
}
void AGWController::ClientProgress_Implementation(const FGWProgressPacket& Packet)
{
    if (auto* GI = GetGameInstance<UGWGameInstance>())
        GI->ApplyProgress(Packet);
}
void AGWController::ClientConsume_Implementation(uint8 ItemA, uint8 ItemB)
{
    if (auto* GI = GetGameInstance<UGWGameInstance>())
        GI->ConsumeItems(ItemA == 255 ? -1 : ItemA, ItemB == 255 ? -1 : ItemB);
}
void AGWController::GWStickers(int32 Count)
{
#if !UE_BUILD_SHIPPING
    if (auto* GI = GetGameInstance<UGWGameInstance>())
        GI->GrantStickers(Count);
#else
    (void)Count;
#endif
}
void AGWController::SendThrowable(bool Allowed)
{
    ServerAction(14, Allowed ? 1 : 0);
}
void AGWController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AGWController::Click);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AGWController::PaintPressed);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AGWController::PaintReleased);
    InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AGWController::Merge);
    InputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &AGWController::Dodge);
    InputComponent->BindKey(EKeys::RightShift, IE_Pressed, this, &AGWController::Dodge);
    InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AGWController::CallWave);
    InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AGWController::Recruit);
    InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AGWController::Launch);
    InputComponent->BindKey(EKeys::Q, IE_Released, this, &AGWController::ReleaseLaunch);
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AGWController::Pause);
    InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AGWController::ZoomIn);
    InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AGWController::ZoomOut);
    InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AGWController::DropHeld);
    InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AGWController::KitPanel);
    // 1-5 belong to the expedition's hotbar alone (design v2.1 4.1: 按键职责清理). Upgrade cards are picked with the mouse,
    // towers in the build ring a click opens on the floor.
    const FKey Picks[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
                          EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive};
    for (int32 I = 0; I < static_cast<int32>(UE_ARRAY_COUNT(Picks)); ++I)
    {
        FInputKeyBinding Binding(FInputChord(Picks[I]), IE_Pressed);
        Binding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, Index = I % 5]() { HotbarKey(Index); });
        InputComponent->KeyBindings.Add(Binding);
    }
}
void AGWController::HotbarKey(int32 Slot)
{
    const auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (!S || !S->Expedition() || !GameplayInputAllowed())
        return;
    // Keys 1 and 2 both take up the guns (the two hands, 2026-11-17); an empty item slot has nothing to take up.
    const auto* P = GetPlayerState<AGWPlayerState>();
    const FGWUnitView* U = P ? S->FindUnit(P->UnitId) : nullptr;
    if (Slot < gwgame::HandSlots)
        Slot = 0;
    else if (U && U->Hotbar[FMath::Clamp(Slot, 0, 4)].Kind == 0)
        return;
    AskedSlot = Slot;
    AskedAt = GetWorld()->GetTimeSeconds();
    PickSlot(Slot);
}
void AGWController::CycleSlot(int32 Step)
{
    const auto* S = GetWorld()->GetGameState<AGWGameState>();
    const auto* P = GetPlayerState<AGWPlayerState>();
    const FGWUnitView* U = S && P ? S->FindUnit(P->UnitId) : nullptr;
    if (!U || !GameplayInputAllowed())
        return;
    // The wheel runs over the hands (one stop) and the item slots that hold something (design 4.1).
    const int32 From = AskedSlot >= 0 && GetWorld()->GetTimeSeconds() - AskedAt < .5 ? AskedSlot : U->Selected;
    const int32 Current = From < gwgame::HandSlots ? 0 : From;
    for (int32 K = 1; K <= 5; ++K)
    {
        const int32 Slot = ((Current + Step * K) % 5 + 5) % 5;
        if (Slot == 0 || (Slot >= gwgame::HandSlots && U->Hotbar[Slot].Kind != 0))
        {
            HotbarKey(Slot);
            return;
        }
    }
}
void AGWController::KitPanel()
{
    const auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (auto* H = Cast<AGWHUD>(GetHUD()); H && S && S->Expedition() && !S->Finished)
        H->ToggleKit();
}
bool AGWController::GameplayInputAllowed() const
{
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    auto* H = Cast<AGWHUD>(GetHUD());
    auto* P = GetPlayerState<AGWPlayerState>();
    const auto* U = S && P ? S->FindUnit(P->UnitId) : nullptr;
    return U && U->Alive && S && S->InMatch && !S->Finished && !S->UpgradePaused && H && !H->OverlayOpen() &&
           FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive();
}
bool AGWController::PredictedPosition(int32 UnitId, FVector& Position) const
{
    // A carried, flying or pinned player is placed by the host alone and drawn smoothed like everyone else.
    if (!Prediction.valid || UnitId != PredictedUnit || Pinned)
        return false;
    Position = FVector(Prediction.position.x, Prediction.position.y, 0);
    return true;
}
bool AGWController::InputKey(const FInputKeyEventArgs& Params)
{
    if (auto* H = Cast<AGWHUD>(GetHUD()); H && H->IntroPlaying())
    {
        if (Params.Key == EKeys::SpaceBar && Params.Event == IE_Pressed)
            H->SkipIntro();
        return true;
    }
    return Super::InputKey(Params);
}

void AGWController::PlayerTick(float Delta)
{
    Super::PlayerTick(Delta);
    // In play the HUD draws its own cursor (a hammer while building, a reticle in a fight); menus keep the arrow.
    if (const auto* H = Cast<AGWHUD>(GetHUD()))
    {
        CurrentMouseCursor = (H->IntroPlaying() || H->CustomCursor()) ? EMouseCursor::None : EMouseCursor::Default;
        if (IsLocalController())
            UpdateMouse(H->CustomCursor() && !H->IntroPlaying());
    }
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    auto* P = GetPlayerState<AGWPlayerState>();
    if (HasAuthority() && FPlatformTime::Seconds() - LastInput > .3 && P && S && S->Simulation)
        S->Simulation->setInput(P->UnitId, {});
    if (!IsLocalController())
        return;
    UpdateSpectate();
    if (LocalCharging && !GameplayInputAllowed())
        CancelLaunch();
    if (LocalCharging && S && P)
    {
        bool HasTail = false;
        for (const auto& Ally : S->Units)
            HasTail |= Ally.Alive && Ally.FollowerOf == P->UnitId;
        if (!HasTail)
            CancelLaunch();
    }
    if (!IsInputKeyDown(EKeys::LeftMouseButton))
        UIConsumedMouse = false;
    // The rocket's charge meter starts when the trigger goes down with the launcher in hand.
    {
        const auto* Me = S && P ? S->FindUnit(P->UnitId) : nullptr;
        // It charges only once the launcher is loaded and its last shell's interval has passed (as the host counts), so
        // the aiming preview's landing spot is where the shell will come down.
        const double Now = GetWorld()->GetTimeSeconds();
        const bool Ready = Me && Me->Ammo > 0 && Me->Reload <= 0 && Now >= RocketReadyAt;
        const bool Held = IsInputKeyDown(EKeys::LeftMouseButton) && !UIConsumedMouse && Me && Me->Alive && Me->Weapon == gwgame::RocketWeapon &&
                          !Me->Climbing && GameplayInputAllowed();
        if (!Held)
        {
            if (RocketStarted >= 0 && Me)
                RocketReadyAt = Now + gwgame::gunStats(gwgame::RocketWeapon, S->Expedition() ? Me->Hotbar[0].Variant : -1).interval;
            RocketStarted = -1;
        }
        else if (RocketStarted < 0 && Ready)
            RocketStarted = Now;
    }
    HoverCell = HoverMate = -1;
    if (S && S->InMatch && S->Defense())
    {
        // The core is a solid the authority pushes players out of; prediction must agree.
        Prediction.obstacle = {S->CorePosition.X, S->CorePosition.Y};
        Prediction.obstacleRadius = gwgame::DefenseState::CoreRadius;
        CursorCell(HoverCell);
        HoverMate = MateUnderCursor();
        if (Painting && (!IsInputKeyDown(EKeys::RightMouseButton) || !GameplayInputAllowed()))
            Painting = false;
        if (Painting && HoverCell >= 0 && HoverCell != LastPaintCell)
            PaintTo(HoverCell);
        // Holding the right button on a teammate pours ink to them, a measure four times a second.
        if (PourTarget >= 0 && (!IsInputKeyDown(EKeys::RightMouseButton) || !GameplayInputAllowed() || !S->FindUnit(PourTarget)))
            PourTarget = -1;
        const double Now = GetWorld()->GetTimeSeconds();
        if (PourTarget >= 0 && Now >= NextPour)
        {
            ServerPour(PourTarget);
            NextPour = Now + .25;
        }
    }
    else
    {
        Prediction.obstacleRadius = 0;
        Painting = false;
        PourTarget = -1;
    }
    const auto* U = P && S ? S->FindUnit(P->UnitId) : nullptr;
    if (!S || !S->InMatch || S->Finished || !U || !U->Alive || !WorldView)
    {
        Prediction.valid = false;
        Pinned = false;
        InputTime = 0;
        return;
    }
    Pinned = U->State == static_cast<uint8>(gwgame::UnitState::Launched) || U->Carried || U->Bait > 0 || U->Climbing;
    // Up on the box's platform the player walks on platform cells only; the host says which level they are on.
    if (Prediction.deck != U->Deck)
    {
        Prediction.deck = U->Deck;
        Prediction.valid = false;
    }
    if (S->UpgradePaused)
    {
        Prediction.reset({U->Position.X, U->Position.Y}, U->AckSequence, {U->Impulse.X, U->Impulse.Y});
        InputTime = 0;
        return;
    }
    if (WorldView->Geometry.seed != S->Seed || WorldView->Geometry.version != S->WallVersion)
        return;
    FVector Authority = U->Position;
    // While pinned the commands still go up (aim and fire work) but nothing is walked locally.
    double Speed = Pinned ? 0 : S->PlayerSpeed * U->MovementScale;
    const gwgame::Vec2 Push{U->Impulse.X, U->Impulse.Y};
    if (!Prediction.valid || PredictedUnit != U->Id || PredictionSeed != S->Seed)
    {
        Prediction.reset({Authority.X, Authority.Y}, U->AckSequence, Push);
        PredictedUnit = U->Id;
        PredictionSeed = S->Seed;
    }
    else if (LastAuthorityAck != U->AckSequence || LastAuthorityPosition != Authority ||
             LastAuthorityImpulse != U->Impulse || PredictionWallVersion != S->WallVersion)
        Prediction.reconcile({Authority.X, Authority.Y}, U->AckSequence, WorldView->Geometry, Speed, U->Radius, Push);
    LastAuthorityAck = U->AckSequence;
    LastAuthorityPosition = Authority;
    LastAuthorityImpulse = U->Impulse;
    PredictionWallVersion = S->WallVersion;
    // The dodge as this player's research has it (the host counts the same ranks).
    Prediction.dashKick = gwgame::DashImpulse * gwgame::attributeScale(gwgame::Attribute::DashSpeed, U->KickRank);
    Prediction.dashRest = gwgame::DashCooldownSeconds * gwgame::attributeScale(gwgame::Attribute::DashCooldown, U->DashRank);
    gwgame::Input Input;
    double Angle = FMath::DegreesToRadians(U->Angle);
    Input.aim = {std::cos(Angle), std::sin(Angle)};
    // A headless test client has no focus to lose, so its driver comes before the keyboard.
    if (Autopilot && !S->UpgradePaused)
        DriveAutopilot(S, *U, Input);
    else if (GameplayInputAllowed())
    {
        Input.move = {static_cast<double>(IsInputKeyDown(EKeys::D)) - IsInputKeyDown(EKeys::A),
                      static_cast<double>(IsInputKeyDown(EKeys::S)) - IsInputKeyDown(EKeys::W)};
        FVector Hit;
        if (CursorWorld(Hit))
            Input.aim = {Hit.X - Prediction.position.x, Hit.Y - Prediction.position.y};
        Input.fire = IsInputKeyDown(EKeys::LeftMouseButton) && !UIConsumedMouse;
        // E held: the expedition's exit doors and chained chests are held open.
        Input.use = IsInputKeyDown(EKeys::E);
        // R held: ink poured into wounds, shared by every hurt player in the circle round this one (2026-11-17).
        Input.mend = IsInputKeyDown(EKeys::R);
        MouseAim = FVector2D(Input.aim.x, Input.aim.y).GetSafeNormal();
    }
    if (MendOverride)
        Input.mend = true;
    InputTime = FMath::Min(InputTime + Delta, .1f);
    bool Moved = false;
    // A Shift press rides on the next command only.
    Input.dodge = DodgeQueued && GameplayInputAllowed();
    while (InputTime + 1e-7f >= 1.f / 60)
    {
        InputTime -= 1.f / 60;
        Input.sequence = ++SequenceCounter;
        if (!Input.sequence)
            Input.sequence = ++SequenceCounter;
        Prediction.predict(Input, WorldView->Geometry, Speed, U->Radius);
        Input.dodge = false;
        DodgeQueued = false;
        Moved = true;
    }
    if (Moved)
    {
        TArray<FGWInputCommand> Commands;
        for (const auto& In : Prediction.commands())
        {
            FGWInputCommand Command;
            Command.Sequence = In.sequence;
            Command.Move = {In.move.x, In.move.y};
            Command.Aim = {In.aim.x, In.aim.y};
            Command.Fire = In.fire;
            Command.Dodge = In.dodge;
            Command.Use = In.use;
            Command.Mend = In.mend;
            Commands.Add(Command);
        }
        ServerInput(Commands);
    }
    if (Pinned)
        Prediction.reset({Authority.X, Authority.Y}, U->AckSequence, Push);
}
bool AGWController::CursorWorld(FVector& Hit) const
{
    FVector Ray, Direction;
    if (!DeprojectMousePositionToWorld(Ray, Direction) || FMath::Abs(Direction.Z) < .0001)
        return false;
    // The platform's top first: where the cursor is over it, it hides the floor of the box behind it.
    const auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (S && S->Defense() && S->GridWidth > 0 && S->CellSize > 0)
    {
        const float Top = static_cast<float>(gwgame::DeckHeight);
        const FVector High = Ray + Direction * ((Top - Ray.Z) / Direction.Z);
        const int32 X = FMath::FloorToInt(High.X / S->CellSize), Y = FMath::FloorToInt(High.Y / S->CellSize);
        const int32 Cell = Y * S->GridWidth + X;
        if (X >= 0 && Y >= 0 && X < S->GridWidth && Y < S->GridHeight && S->Cells.IsValidIndex(Cell) && (S->Cells[Cell] & EGWCell::Deck))
        {
            Hit = High;
            return true;
        }
    }
    Hit = Ray + Direction * (-Ray.Z / Direction.Z);
    return true;
}
float AGWController::RocketProgress() const
{
    if (RocketOverride >= 0)
        return FMath::Min(RocketOverride, 1.f);
    if (RocketStarted < 0)
        return -1;
    return FMath::Clamp(static_cast<float>((GetWorld()->GetTimeSeconds() - RocketStarted) / gwgame::RocketChargeSeconds), 0.f, 1.f);
}
bool AGWController::MendHeld() const
{
    return MendOverride || (GameplayInputAllowed() && IsInputKeyDown(EKeys::R));
}
// Spectating (2026-11-17 playtest: a fallen player could not look at anyone else): while the local player is down the
// camera follows a teammate still standing, and the left and right buttons step through them (players; with none left,
// any ally). The world reads SpectateId for its focus, the HUD for the name.
TArray<int32> AGWController::SpectateChoices() const
{
    TArray<int32> Ids;
    const auto* S = GetWorld()->GetGameState<AGWGameState>();
    const auto* P = GetPlayerState<AGWPlayerState>();
    const FGWUnitView* Me = S && P ? S->FindUnit(P->UnitId) : nullptr;
    if (!S || !Me || Me->Alive || !S->InMatch || S->Finished)
        return Ids;
    for (const auto& U : S->Units)
        if (U.Alive && U.Human && U.Id != Me->Id && U.Team == Me->Team && !U.Extracted)
            Ids.Add(U.Id);
    if (Ids.IsEmpty())
        for (const auto& U : S->Units)
            if (U.Alive && U.Id != Me->Id && U.Team == Me->Team && U.Kind != 2 && !U.Extracted)
                Ids.Add(U.Id);
    Ids.Sort();
    return Ids;
}
void AGWController::UpdateSpectate()
{
    const TArray<int32> Ids = SpectateChoices();
    SpectateCount = Ids.Num();
    if (Ids.IsEmpty())
        SpectateId = -1;
    else if (!Ids.Contains(SpectateId))
        SpectateId = Ids[0];
}
void AGWController::StepSpectate(int32 Step)
{
    const TArray<int32> Ids = SpectateChoices();
    if (Ids.Num() < 2)
        return;
    const int32 At = FMath::Max(0, Ids.IndexOfByKey(SpectateId));
    SpectateId = Ids[(At + Step % Ids.Num() + Ids.Num()) % Ids.Num()];
}
// The mouse (2026-11-17 playtest: windowed, Esc's notebook let the arrow out of the window but a click out there went
// nowhere): in play the arrow stays inside the window; with the notebook, a panel, a sheet or the results up it is free,
// and the window holds the mouse only while a button is down on it, so a click outside reaches whatever is there.
void AGWController::UpdateMouse(bool Play)
{
    UGameViewportClient* View = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    if (!View || !FSlateApplication::IsInitialized())
        return;
    const bool Active = FSlateApplication::Get().IsActive();
    // Applied again when the window comes back to the front (the system drops the lock with the focus).
    if (Active && (MouseMode != static_cast<int8>(Play) || !MouseActive))
    {
        MouseMode = static_cast<int8>(Play);
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(Play ? EMouseLockMode::LockAlways : EMouseLockMode::DoNotLock);
        SetInputMode(Mode);
    }
    MouseActive = Active;
    // Activating the window grabs the mouse too; out of play it is let go as soon as no button is down.
    if (!Play && View->Viewport && View->Viewport->HasMouseCapture() && !IsInputKeyDown(EKeys::LeftMouseButton) &&
        !IsInputKeyDown(EKeys::RightMouseButton) && !IsInputKeyDown(EKeys::MiddleMouseButton))
        FSlateApplication::Get().ReleaseAllPointerCapture();
}
int32 AGWController::MateUnderCursor() const
{
    const auto* S = GetWorld()->GetGameState<AGWGameState>();
    const auto* P = GetPlayerState<AGWPlayerState>();
    FVector Hit;
    if (!S || !P || !CursorWorld(Hit))
        return -1;
    const FGWUnitView* Self = S->FindUnit(P->UnitId);
    int32 Best = -1;
    double Nearest = 1e18;
    for (const auto& U : S->Units)
        if (U.Alive && U.Human && Self && U.Id != Self->Id && U.Team == Self->Team)
        {
            FVector At = U.Position;
            if (WorldView)
                WorldView->VisualPosition(U.Id, At);
            const double D = FVector::Dist2D(At, Hit);
            if (D < U.Radius + 45 && D < Nearest)
            {
                Nearest = D;
                Best = U.Id;
            }
        }
    return Best;
}
// Wanders around the core (or its own spot in Survival), shoots the nearest enemy and every few seconds
// recruits or throws, like a busy player. Deterministic per process so stress runs are comparable.
void AGWController::DriveAutopilot(const AGWGameState* S, const FGWUnitView& U, gwgame::Input& In)
{
    const double Now = GetWorld()->GetTimeSeconds();
    if (!AutopilotSeed)
        AutopilotSeed = (0x9E3779B97F4A7C15ull ^ (static_cast<uint64>(FPlatformProcess::GetCurrentProcessId()) * 1315423911ull)) | 1;
    const gwgame::Vec2 Here = Prediction.position;
    if (AutopilotLast.lengthSquared() > 0)
        AutopilotDistance += (Here - AutopilotLast).length();
    AutopilotLast = Here;
    const gwgame::Vec2 Home = S->Defense() ? gwgame::Vec2{S->CorePosition.X, S->CorePosition.Y} : Here;
    if (Now >= AutopilotTurn || (AutopilotGoal - Here).length() < 60)
    {
        const double Angle = gwgame::random01(AutopilotSeed) * 2 * PI, Reach = 350 + gwgame::random01(AutopilotSeed) * 900;
        AutopilotGoal = Home + gwgame::Vec2{std::cos(Angle), std::sin(Angle)} * Reach;
        // Up on the box's platform: walk along it, a few cells either way.
        if (U.Deck && WorldView)
        {
            const gwgame::Maze& M = WorldView->Geometry;
            const int32 From = M.cell(Here);
            int32 To = From;
            const int32 Steps = 2 + static_cast<int32>(gwgame::random01(AutopilotSeed) * 5);
            const bool Clockwise = gwgame::random01(AutopilotSeed) < .5;
            for (int32 I = 0; I < Steps && M.isDeck(To); ++I)
            {
                // Round the ring: the next platform cell that is not the one just left.
                const int32 X = To % M.width, Y = To / M.width;
                int32 Best = -1;
                for (const auto& D : {std::pair<int32, int32>{1, 0}, {0, 1}, {-1, 0}, {0, -1}})
                {
                    const int32 NX = X + (Clockwise ? D.first : -D.first), NY = Y + (Clockwise ? D.second : -D.second);
                    if (NX >= 0 && NY >= 0 && NX < M.width && NY < M.height && M.isDeck(NY * M.width + NX) && NY * M.width + NX != From)
                    {
                        Best = NY * M.width + NX;
                        break;
                    }
                }
                if (Best < 0)
                    break;
                To = Best;
            }
            AutopilotGoal = M.center(To);
        }
        AutopilotTurn = Now + 1.5 + gwgame::random01(AutopilotSeed) * 2;
    }
    In.move = (AutopilotGoal - Here).normalized();
    const FGWUnitView* Target = nullptr;
    double Best = 1e18;
    for (const auto& Other : S->Units)
        if (Other.Alive && Other.Team != U.Team)
        {
            const double D2 = FVector::DistSquared2D(FVector(Other.Position), FVector(Here.x, Here.y, 0));
            if (D2 < Best)
            {
                Best = D2;
                Target = &Other;
            }
        }
    In.aim = Target ? gwgame::Vec2{Target->Position.X - Here.x, Target->Position.Y - Here.y}.normalized() : In.move;
    if (In.aim.lengthSquared() < .5)
        In.aim = {1, 0};
    In.fire = true;
    MouseAim = FVector2D(In.aim.x, In.aim.y);
    if (Now >= AutopilotAct)
    {
        AutopilotAct = Now + 2 + gwgame::random01(AutopilotSeed) * 2;
        if (gwgame::random01(AutopilotSeed) < .6)
            ServerAction(5, 0);
        else
        {
            ServerCharge(0, MouseAim);
            ServerCharge(1, MouseAim);
        }
    }
}
void AGWController::ServerInput_Implementation(const TArray<FGWInputCommand>& Commands)
{
    double Now = FPlatformTime::Seconds();
    if (Now - LastInput < .005 || Commands.IsEmpty() || Commands.Num() > 8)
        return;
    auto* P = GetPlayerState<AGWPlayerState>();
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (!P || !S || !S->Simulation || S->Finished)
        return;
    auto* Unit = S->Simulation->find(P->UnitId);
    if (!Unit || !Unit->human || Unit->owner != P->Identity)
        return;
    bool Accepted = false;
    for (const auto& Command : Commands)
    {
        if (Command.Sequence == 0)
            continue;
        gwgame::Input In;
        In.sequence = Command.Sequence;
        In.move = {Command.Move.X, Command.Move.Y};
        In.aim = {Command.Aim.X, Command.Aim.Y};
        In.fire = Command.Fire;
        In.dodge = Command.Dodge;
        In.use = Command.Use;
        In.mend = Command.Mend;
        Accepted = S->Simulation->setInput(P->UnitId, In) || Accepted;
    }
    if (Accepted)
        LastInput = Now;
}
void AGWController::ServerAction_Implementation(uint8 Action, int32 Value)
{
    auto* G = GetWorld()->GetAuthGameMode<AGWGameMode>();
    auto* P = GetPlayerState<AGWPlayerState>();
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (!G || !P || !S)
        return;
    // A player's own permission arrives on joining, often right beside another action: never dropped.
    if (Action == 14)
    {
        G->SetThrowable(this, Value != 0);
        return;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - LastAction < .09f)
        return;
    LastAction = Now;
    if (Action == 15)
        G->SetPrank(this, Value);
    else if (Action == 16)
        G->Kick(this, Value);
    else if (Action == 1 && !S->InMatch)
        P->Ready = !P->Ready;
    else if (Action == 3)
    {
        if (!G->BeginRound(this, false))
            ClientNotice(TEXT("Network.NotReady"));
    }
    else if (Action == 4)
        G->BeginRound(this, true);
    else if (Action == 5 && S->Simulation)
        S->Simulation->recruit(P->UnitId);
    // Q has its own reliable press/release RPC. A generic action cannot bypass charge timing.
    else if (Action == 7 && S->Simulation)
        S->Simulation->chooseUpgrade(P->UnitId, Value);
    else if (Action == 8)
        G->ReturnToLobby(this);
    else if (Action == 9)
    {
        FString Notice;
        G->SaveRun(this, Value, Notice);
        ClientNotice(Notice);
    }
    else if (Action == 10)
    {
        if (!G->BeginRound(this, true, Value))
            ClientNotice(TEXT("Save.LoadFailed"));
    }
    else if (Action == 11 && S->Simulation)
        S->Simulation->merge(P->UnitId);
    // Calling the next wave early is the host's call, like starting the mission.
    else if (Action == 12 && S->Simulation && P->Identity == S->HostIdentity)
        S->Simulation->callWave(P->UnitId);
    else if (Action == 13)
    {
        if (!G->BeginDefense(this, Value & 15, (Value >> 4) & 3))
            ClientNotice(TEXT("Defense.StartFailed"));
    }
    // Phase 3, the hub: the host's deploy from the mission board (and its cancel), the page the host is looking
    // at, the tutorial for a player alone, and giving up the mission in progress.
    else if (Action == 17)
    {
        if (!G->RequestDeploy(this, Value & 15, (Value >> 4) & 3))
            ClientNotice(TEXT("Hub.DeployHostOnly"));
    }
    else if (Action == 18)
        G->CancelDeploy(this);
    else if (Action == 19)
        G->PickBoard(this, Value);
    else if (Action == 20)
    {
        if (!G->BeginTutorial(this))
            ClientNotice(TEXT("Tutorial.AloneOnly"));
    }
    else if (Action == 21)
        G->AbandonRun(this);
    // The expedition: a hotbar slot, dropping what is in hand, and the kit panel's drops (stickers, a look).
    else if (Action == 25 && S->Simulation)
        S->Simulation->selectSlot(P->UnitId, Value);
    else if (Action == 26 && S->Simulation)
        S->Simulation->dropSlot(P->UnitId);
    else if (Action == 27 && S->Simulation)
    {
        if (Value == 0 || Value == 1)
            S->Simulation->dropStickers(P->UnitId, Value == 0 ? 25 : 0);
        else if (Value == 10 || Value == 11)
            S->Simulation->dropCoins(P->UnitId, Value == 10 ? 100 : 0);
        else
            S->Simulation->dropLook(P->UnitId, Value - 2);
    }
    // The bag sheet (2026-11-16): a slot moved onto another, a slot dropped whole, a slot sold to the shop's buyer.
    else if (Action == 28 && S->Simulation)
        S->Simulation->moveSlot(P->UnitId, Value & 255, (Value >> 8) & 255);
    else if (Action == 29 && S->Simulation)
        S->Simulation->dropKitSlot(P->UnitId, Value);
    else if (Action == 30 && S->Simulation)
        S->Simulation->sellSlot(P->UnitId, Value);
    // Phase 4: the host's difficulty tier on the board.
    else if (Action == 22)
    {
        if (!G->SetTier(this, Value))
            ClientNotice(TEXT("Tier.HostOnly"));
    }
    // The box: a tower on the block in this cell, or one more side on the tower already there.
    else if (Action == 23 && S->Simulation)
    {
        if (const auto* U = S->Simulation->find(P->UnitId); U && U->human && U->owner == P->Identity)
            S->Simulation->buildTower(U->id, Value & 0xFFFF, (Value >> 16) & 7);
    }
    if (S->Simulation && (Action == 5 || Action == 7 || Action == 11 || Action == 12 || Action == 23 || (Action >= 25 && Action <= 27)))
        S->Publish();
    S->ForceNetUpdate();
}
void AGWController::ServerRevive_Implementation(int32 Target, const TArray<uint32>& Points, uint8 Pen)
{
    // A drawing is a handful of strokes a minute at most; anything far beyond that is dropped.
    const double Now = FPlatformTime::Seconds();
    if (Now - LastRevive < .4 || Points.Num() < 3 || Points.Num() > gwgame::ReviveMaxPoints)
        return;
    LastRevive = Now;
    auto* P = GetPlayerState<AGWPlayerState>();
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (!P || !S || !S->InMatch || !S->Simulation || S->Finished)
        return;
    const auto* U = S->Simulation->find(P->UnitId);
    if (!U || !U->human || U->owner != P->Identity)
        return;
    // The sheet's unit square, in 1/65535ths: the host measures the drawing itself (design 5.2: 闭合判定 and 鞋带公式).
    std::vector<gwgame::Vec2> Drawing;
    Drawing.reserve(Points.Num());
    for (uint32 Packed : Points)
        Drawing.push_back({(Packed >> 16) / 65535.0, (Packed & 0xFFFF) / 65535.0});
    S->Simulation->reviveDrawn(U->id, Target, Drawing, Pen);
    S->Publish();
    S->ForceNetUpdate();
}
void AGWController::ServerPaint_Implementation(int32 Cell, bool Erase)
{
    // A drag paints a handful of cells a second; anything far beyond that is dropped.
    const double Now = FPlatformTime::Seconds();
    PaintAllowance = FMath::Min(40.0, PaintAllowance + (Now - LastPaintTime) * 40);
    LastPaintTime = Now;
    if (PaintAllowance < 1)
        return;
    PaintAllowance -= 1;
    auto* P = GetPlayerState<AGWPlayerState>();
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (!P || !S || !S->InMatch || !S->Simulation || S->Finished)
        return;
    const auto* U = S->Simulation->find(P->UnitId);
    if (!U || !U->human || U->owner != P->Identity)
        return;
    const size_t Before = S->Simulation->effects.size();
    // On the box's platform the pen draws a ladder down the wall (or rubs one out); everywhere else a wall.
    const bool Ladder = S->Simulation->maze.isDeck(Cell);
    const bool Done = Ladder ? (Erase ? S->Simulation->eraseLadder(U->id, Cell) : S->Simulation->buildLadder(U->id, Cell))
                             : Erase ? S->Simulation->eraseWall(U->id, Cell)
                                     : S->Simulation->paintWall(U->id, Cell);
    if (Done || S->Simulation->effects.size() != Before)
    {
        S->Publish();
        S->ForceNetUpdate();
    }
}
void AGWController::ServerPour_Implementation(int32 Target)
{
    // A held button pours four measures a second; anything far beyond that is dropped.
    const double Now = FPlatformTime::Seconds();
    PourAllowance = FMath::Min(8.0, PourAllowance + (Now - LastPourTime) * 6);
    LastPourTime = Now;
    if (PourAllowance < 1)
        return;
    PourAllowance -= 1;
    auto* P = GetPlayerState<AGWPlayerState>();
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (!P || !S || !S->InMatch || !S->Simulation || S->Finished)
        return;
    const auto* U = S->Simulation->find(P->UnitId);
    if (U && U->human && U->owner == P->Identity && S->Simulation->pourInk(U->id, Target, 15) > 0)
        S->Publish();
}
bool AGWController::CursorCell(int32& Cell) const
{
    Cell = -1;
    const auto* S = GetWorld()->GetGameState<AGWGameState>();
    FVector Hit;
    if (!S || S->GridWidth <= 0 || S->CellSize <= 0 || !CursorWorld(Hit))
        return false;
    const int32 X = FMath::FloorToInt(Hit.X / S->CellSize), Y = FMath::FloorToInt(Hit.Y / S->CellSize);
    if (X < 0 || Y < 0 || X >= S->GridWidth || Y >= S->GridHeight)
        return false;
    Cell = Y * S->GridWidth + X;
    return true;
}
void AGWController::PaintPressed()
{
    // Down and watching a teammate: the right button steps back to the one before.
    if (SpectateId >= 0)
    {
        if (const auto* H = Cast<AGWHUD>(GetHUD()); H && !H->OverlayOpen() && !H->SettingsOpen())
            StepSpectate(-1);
        return;
    }
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    int32 Cell = -1;
    if (!S || !S->Defense() || !GameplayInputAllowed())
        return;
    // The right button first folds an open build ring away (and draws nothing).
    if (auto* H = Cast<AGWHUD>(GetHUD()); H && RingCell >= 0)
    {
        H->CloseRing();
        return;
    }
    // Pressing on a teammate pours ink to them instead of drawing.
    if (const int32 Mate = MateUnderCursor(); Mate >= 0)
    {
        PourTarget = Mate;
        NextPour = 0;
        return;
    }
    if (!CursorCell(Cell) || !S->Cells.IsValidIndex(Cell))
        return;
    // On the platform one click draws a ladder down the box wall, or rubs out the one there (no stroke).
    if (S->Cells[Cell] & EGWCell::Deck)
    {
        bool Hanging = false;
        for (const auto& L : S->Ladders)
            Hanging |= L.Deck == Cell;
        ServerPaint(Cell, Hanging);
        return;
    }
    // Pressing on a sound drawn wall starts an eraser stroke; on paper or a damaged wall, a pen stroke.
    const bool Wall = (S->Cells[Cell] & EGWCell::Wall) != 0;
    PaintErase = Wall && (!WorldView || WorldView->Geometry.integrity(Cell) > .95);
    Painting = true;
    LastPaintCell = Cell;
    ServerPaint(Cell, PaintErase);
}
void AGWController::PaintReleased()
{
    Painting = false;
    LastPaintCell = -1;
    PourTarget = -1;
}
// Fast drags skip cells; the stroke is filled along the line, turning each diagonal step into an L so the
// drawn wall is always edge-connected (corner-touching cells would leave a gap enemies walk through).
void AGWController::PaintTo(int32 Cell)
{
    const auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (!S || S->GridWidth <= 0 || LastPaintCell < 0)
    {
        LastPaintCell = Cell;
        return;
    }
    const int32 W = S->GridWidth;
    int32 X = LastPaintCell % W, Y = LastPaintCell / W;
    const int32 TX = Cell % W, TY = Cell / W;
    for (int32 Guard = 0; Guard < 64 && (X != TX || Y != TY); ++Guard)
    {
        const int32 DX = FMath::Sign(TX - X), DY = FMath::Sign(TY - Y);
        if (DX && DY && FMath::Abs(TX - X) >= FMath::Abs(TY - Y))
            X += DX;
        else if (DX && DY)
            Y += DY;
        else
        {
            X += DX;
            Y += DY;
        }
        // A stroke never spills onto the platform (ladders take a click of their own).
        if (S->Cells.IsValidIndex(Y * W + X) && (S->Cells[Y * W + X] & EGWCell::Deck))
            continue;
        ServerPaint(Y * W + X, PaintErase);
    }
    LastPaintCell = Cell;
}
void AGWController::Merge()
{
    if (!GameplayInputAllowed())
        return;
    const auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (S && S->InMatch && S->Defense())
    {
        // The build ring on the cell under the cursor (F again on the same cell closes it).
        int32 Cell = -1;
        if (auto* H = Cast<AGWHUD>(GetHUD()); H && CursorCell(Cell))
            H->ToggleRing(Cell);
        return;
    }
    ServerAction(11, 0);
}
void AGWController::Dodge()
{
    if (GameplayInputAllowed())
        DodgeQueued = true;
}
void AGWController::CallWave()
{
    const auto* S = GetWorld()->GetGameState<AGWGameState>();
    if (S && S->InMatch && S->Defense() && S->Phase == static_cast<uint8>(gwgame::DefensePhase::Build) && !S->Finished)
        ServerAction(12, 0);
}
void AGWController::ClientNotice_Implementation(const FString& Key)
{
    NoticeKey = Key;
}
void AGWController::Click()
{
    float X = 0, Y = 0;
    if (GetMousePosition(X, Y))
        if (auto* H = Cast<AGWHUD>(GetHUD()))
            if (H->Click(X, Y))
            {
                UIConsumedMouse = true;
                return;
            }
    // Down and watching a teammate: the left button steps on to the next one.
    if (SpectateId >= 0)
        if (const auto* H = Cast<AGWHUD>(GetHUD()); H && !H->OverlayOpen() && !H->SettingsOpen())
            StepSpectate(1);
}
void AGWController::Recruit()
{
    if (!GameplayInputAllowed())
        return;
    // In the hub, E at a station opens it; anywhere else it recruits.
    if (auto* H = Cast<AGWHUD>(GetHUD()); H && H->Interact())
        return;
    ServerAction(5, 0);
}
void AGWController::ClientWasKicked_Implementation(const FText& KickReason)
{
    Super::ClientWasKicked_Implementation(KickReason);
    if (auto* GI = GetGameInstance<UGWGameInstance>())
        GI->ReturnHome(TEXT("Network.Kicked"));
}
void AGWController::Launch()
{
    if (!GameplayInputAllowed() || LocalCharging)
        return;
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    auto* P = GetPlayerState<AGWPlayerState>();
    bool HasTail = false;
    if (S && P)
        for (const auto& U : S->Units)
            HasTail |= U.Alive && U.FollowerOf == P->UnitId;
    if (!HasTail)
        return;
    LocalCharging = true;
    ChargeStarted = GetWorld()->GetTimeSeconds();
    ServerCharge(0, ReadMouseAim());
}
FVector2D AGWController::ReadMouseAim() const
{
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    auto* P = GetPlayerState<AGWPlayerState>();
    const auto* U = S && P ? S->FindUnit(P->UnitId) : nullptr;
    if (!U)
        return MouseAim;
    FVector Position = U->Position;
    PredictedPosition(U->Id, Position);
    FVector Hit;
    if (CursorWorld(Hit))
        return FVector2D(Hit.X - Position.X, Hit.Y - Position.Y).GetSafeNormal();
    return MouseAim;
}
void AGWController::ReleaseLaunch()
{
    if (!LocalCharging)
        return;
    LocalCharging = false;
    ServerCharge(GameplayInputAllowed() ? 1 : 2, ReadMouseAim());
}
void AGWController::CancelLaunch()
{
    if (!LocalCharging)
        return;
    LocalCharging = false;
    ServerCharge(2, FVector2D::ZeroVector);
}
float AGWController::ChargeProgress() const
{
    if (LocalCharging)
        return FMath::Clamp((GetWorld()->GetTimeSeconds() - ChargeStarted) / 1.5, 0., 1.);
    const auto* S = GetWorld()->GetGameState<AGWGameState>();
    const auto* P = GetPlayerState<AGWPlayerState>();
    const auto* U = S && P ? S->FindUnit(P->UnitId) : nullptr;
    return U && U->Charging ? FMath::Clamp(U->ChargeTime / 1.5f, 0.f, 1.f) : -1;
}
void AGWController::ServerCharge_Implementation(uint8 Action, FVector2D Aim)
{
    auto* S = GetWorld()->GetGameState<AGWGameState>();
    auto* P = GetPlayerState<AGWPlayerState>();
    if (!S || !S->InMatch || S->Finished || !S->Simulation || !P)
        return;
    auto* U = S->Simulation->find(P->UnitId);
    if (!U || !U->human || U->owner != P->Identity)
        return;
    if (Action == 0)
        S->Simulation->beginCharge(U->id);
    else if (Action == 1)
        S->Simulation->releaseCharge(U->id, {Aim.X, Aim.Y});
    else if (Action == 2)
        S->Simulation->cancelCharge(U->id);
}
void AGWController::Pause()
{
    CancelLaunch();
    // Esc steps back out of a menu sub-page first, otherwise opens/closes the settings notebook.
    if (auto* H = Cast<AGWHUD>(GetHUD()))
        H->Back();
}
void AGWController::ZoomIn()
{
    // On the expedition the wheel runs over the hotbar (design 4.1) instead of zooming.
    if (const auto* G = GetWorld()->GetGameState<AGWGameState>(); G && G->Expedition() && !G->Finished)
        return CycleSlot(-1);
    if (auto* S = UGWUserSettings::Get())
        S->CameraDistance = FMath::Max(900.f, S->CameraDistance - 100);
}
void AGWController::ZoomOut()
{
    if (const auto* G = GetWorld()->GetGameState<AGWGameState>(); G && G->Expedition() && !G->Finished)
        return CycleSlot(1);
    if (auto* S = UGWUserSettings::Get())
        S->CameraDistance = FMath::Min(2600.f, S->CameraDistance + 100);
}
void AGWController::EndPlay(const EEndPlayReason::Type Reason)
{
    if (WorldView)
        WorldView->Destroy();
    Super::EndPlay(Reason);
}
