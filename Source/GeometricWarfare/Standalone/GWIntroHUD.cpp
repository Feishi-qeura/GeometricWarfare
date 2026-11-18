#include "GWHUD.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "GWGameInstance.h"
#include "GWPalette.h"
#include "GWSettings.h"
#include "GWText.h"
#include "HAL/FileManager.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"
#include "MediaTexture.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogGWIntro, Log, All);

void AGWHUD::StartIntro()
{
    auto* GI = GetGameInstance<UGWGameInstance>();
    if (!GI || GI->StartupIntroAttempted || !PlayerOwner || !PlayerOwner->IsLocalController() || IsRunningDedicatedServer())
        return;
    GI->StartupIntroAttempted = true;
    if (FParse::Param(FCommandLine::Get(), TEXT("GWNoIntro")))
        return;
    const FString Movie = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Movies/OpenFrqush_Intro_1080p.mp4"));
    if (!IFileManager::Get().FileExists(*Movie))
    {
        UE_LOG(LogGWIntro, Warning, TEXT("Startup movie missing; continuing to title: %s"), *Movie);
        return;
    }
    IntroPlayer = NewObject<UMediaPlayer>(this);
    IntroPlayer->PlayOnOpen = true;
    IntroPlayer->SetLooping(false);
    IntroPlayer->OnEndReached.AddDynamic(this, &AGWHUD::StopIntro);
    IntroTexture = NewObject<UMediaTexture>(this);
    // Canvas samples a normal 2D texture, rather than an external-texture material.
    IntroTexture->NewStyleOutput = true;
    IntroTexture->AutoClear = false;
    IntroTexture->SetMediaPlayer(IntroPlayer);
    IntroTexture->UpdateResource();
    IntroSound = NewObject<UMediaSoundComponent>(this);
    IntroSound->bIsUISound = true;
    IntroSound->bAllowSpatialization = false;
    IntroSound->Channels = EMediaSoundChannels::Stereo;
    IntroSound->SetMediaPlayer(IntroPlayer);
    IntroSound->RegisterComponent();
    IntroSound->Start();
    Intro.begin(FPlatformTime::Seconds());
    if (!IntroPlayer->OpenFile(Movie))
    {
        UE_LOG(LogGWIntro, Warning, TEXT("Startup movie could not open; continuing to title"));
        StopIntro();
        return;
    }
    UE_LOG(LogGWIntro, Display, TEXT("Startup movie opening"));
}

void AGWHUD::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!IntroPlaying() || !IntroPlayer)
        return;
    const bool Ready = IntroPlayer->GetVideoTrackDimensions(INDEX_NONE, INDEX_NONE).X > 0;
    Intro.update(FPlatformTime::Seconds(), IntroPlayer->GetTime().GetTotalSeconds(),
                 IntroPlayer->GetDuration().GetTotalSeconds(), Ready);
    if (!Intro.active || IntroPlayer->HasError())
    {
        StopIntro();
        return;
    }
    if (IntroSound)
    {
        const auto* UserSettings = UGWUserSettings::Get();
        IntroSound->SetVolumeMultiplier((UserSettings ? UserSettings->MusicVolume : .45f) * Intro.alpha);
        IntroSound->UpdatePlayer();
    }
}

void AGWHUD::SkipIntro()
{
    if (IntroPlaying() && !Intro.stopping)
    {
        UE_LOG(LogGWIntro, Display, TEXT("Startup movie skipped with Space"));
        Intro.skip(FPlatformTime::Seconds());
    }
}

void AGWHUD::StopIntro()
{
    const bool HadPlayer = IntroPlayer != nullptr;
    Intro.active = false;
    Intro.alpha = 0;
    if (IntroSound)
    {
        IntroSound->Stop();
        IntroSound->SetMediaPlayer(nullptr);
        IntroSound->DestroyComponent();
        IntroSound = nullptr;
    }
    if (IntroPlayer)
    {
        IntroPlayer->OnEndReached.RemoveDynamic(this, &AGWHUD::StopIntro);
        IntroPlayer->Close();
        IntroPlayer = nullptr;
    }
    if (IntroTexture)
    {
        IntroTexture->SetMediaPlayer(nullptr);
        IntroTexture = nullptr;
    }
    if (HadPlayer)
        UE_LOG(LogGWIntro, Display, TEXT("Startup movie closed; title input restored"));
}

void AGWHUD::DrawIntro()
{
    using namespace GWPalette;
    // Opaque paper hides every menu hit target while the decoder opens and during fades.
    Box(0, 0, Width, Height, Paper);
    const float Alpha = static_cast<float>(Intro.alpha);
    if (Alpha <= .002f || !IntroTexture || !IntroTexture->GetResource())
        return;
    const FIntPoint Frame = IntroPlayer->GetVideoTrackDimensions(INDEX_NONE, INDEX_NONE);
    if (Frame.X <= 0 || Frame.Y <= 0)
        return;
    const float Fit = FMath::Min(Width / Frame.X, Height / Frame.Y);
    const FVector2D Size(Frame.X * Fit, Frame.Y * Fit);
    const FVector2D Origin((Width - Size.X) * .5f, (Height - Size.Y) * .5f);
    FCanvasTileItem Video(Origin, IntroTexture->GetResource(), Size, FLinearColor(1, 1, 1, Alpha));
    Video.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Video);

    const FText Hint = GWText::Get(TEXT("Intro.Skip"));
    const float FontSize = 16 * Scale;
    const float TextWidth = LetteringWidth(Hint.ToString(), FontSize);
    const float Right = Width - 30 * Scale, Top = 28 * Scale;
    const float Left = Right - TextWidth;
    // The same hand lettering as the notebook: irregular baselines, graphite shadow, CJK fallback.
    const float PreviousFade = Fade;
    Fade *= Alpha;
    Lettering(Hint, Left + TextWidth * .5f, Top, FontSize, InkSoft, 8301);
    Fade = PreviousFade;
    const FVector2D Mark(Left - 20 * Scale, Top + 10 * Scale);
    TArray<FVector2D> Hexagon;
    for (int32 I = 0; I < 6; ++I)
    {
        const float A = I * PI / 3 - PI / 6;
        Hexagon.Add(Mark + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 8 * Scale);
    }
    Polygon(Hexagon, Paper.CopyWithNewOpacity(0), Coral.CopyWithNewOpacity(Alpha * .8f), 1.2f * Scale, 8310);
    // A pencil spacebar mark and a short crooked underline give the little prompt its geometric character.
    Pen(Mark + FVector2D(-4, 0) * Scale, Mark + FVector2D(-4, 3) * Scale, InkSoft.CopyWithNewOpacity(Alpha), Scale, 8311, .2f);
    Pen(Mark + FVector2D(-4, 3) * Scale, Mark + FVector2D(4, 3) * Scale, InkSoft.CopyWithNewOpacity(Alpha), Scale, 8312, .2f);
    Pen(Mark + FVector2D(4, 3) * Scale, Mark + FVector2D(4, 0) * Scale, InkSoft.CopyWithNewOpacity(Alpha), Scale, 8313, .2f);
    Pen(FVector2D(Left, Top + 24 * Scale), FVector2D(Right, Top + 23 * Scale), Pencil.CopyWithNewOpacity(Alpha * .4f), Scale, 8314, .25f);
}
