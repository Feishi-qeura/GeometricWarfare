#pragma once
#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreMiscDefines.h"

// The wall clock the sketchbook's visual timers read (page wipe, page intro, hit flashes, the jelly wobble). A promo
// capture runs the game in fixed steps far slower than real time and sets Override to its own frame clock, so those
// timers advance one capture frame at a time like everything else; otherwise it is the platform clock.
namespace GWClock
{
inline double& Override()
{
    static double Value = -1;
    return Value;
}
inline double Seconds()
{
    const double Forced = Override();
    return Forced >= 0 ? Forced : FPlatformTime::Seconds();
}
} // namespace GWClock

// Easing curves for the sketchbook's motion: cards pinned down with a little bounce, tape slapped on, stamps
// slammed, shapes snapping into place. T runs 0..1 and is clamped.
namespace GWEase
{
inline float Clamp01(float T)
{
    return FMath::Clamp(T, 0.f, 1.f);
}
inline float OutCubic(float T)
{
    T = 1 - Clamp01(T);
    return 1 - T * T * T;
}
inline float InCubic(float T)
{
    T = Clamp01(T);
    return T * T * T;
}
inline float InOutCubic(float T)
{
    T = Clamp01(T);
    return T < .5f ? 4 * T * T * T : 1 - FMath::Pow(-2 * T + 2, 3.f) * .5f;
}
// Overshoots past 1 and settles back (Amount 1.7 is the classic back curve).
inline float OutBack(float T, float Amount = 1.7f)
{
    T = Clamp01(T) - 1;
    return 1 + (Amount + 1) * T * T * T + Amount * T * T;
}
inline float InBack(float T, float Amount = 1.7f)
{
    T = Clamp01(T);
    return (Amount + 1) * T * T * T - Amount * T * T;
}
// A damped wobble after an impact: starts at 1, swings through zero and dies away.
inline float Wobble(float Seconds, float Frequency = 22, float Decay = 7)
{
    return Seconds < 0 ? 0.f : FMath::Cos(Seconds * Frequency) * FMath::Exp(-Seconds * Decay);
}
} // namespace GWEase

// The page wipe between scenes (title, hub, a mission): triangle tiles sweep over the screen, a title card names
// where the player is going, and the tiles flip away once the new scene is in place. It lives outside every
// world, so a cover drawn before a map change is still up when the next map's HUD starts, and the world's own
// intro (blocks rising off the page, the camera settling) waits for the reveal.
struct FGWTransition
{
    enum class EStage : uint8
    {
        Idle,
        Closing,
        Closed,
        Opening
    };
    // What the title card names: a defense page index (0+), or one of these.
    static constexpr int32 TitlePage = -1, HubPage = -2, TutorialPage = -3, ExpeditionPage = -4, PlaylistPage = -5, SurvivalPage = -6;
    static constexpr double CloseSeconds = .55, OpenSeconds = .75;

    EStage Stage = EStage::Idle;
    double StageStart = 0;
    int32 Title = TitlePage;
    // The cover went up for something that changes the scene (a deploy, leaving, a new page): it stays until the
    // scene changes, or until the wait runs out.
    bool AwaitChange = false;
    double AwaitUntil = 0;
    // Tests leave scenes still (no wipes, no world intro) unless a test asks for them.
    bool ForceInTests = false;

    static FGWTransition& Get()
    {
        static FGWTransition Instance;
        return Instance;
    }
    static double Now()
    {
        return GWClock::Seconds();
    }
    bool Enabled() const
    {
        return ForceInTests || !GIsAutomationTesting;
    }
    double Age() const
    {
        return Now() - StageStart;
    }
    void SetStage(EStage Next, double Backdate = 0)
    {
        Stage = Next;
        StageStart = Now() - Backdate;
    }
    // Starts covering the screen (an open page wipe turns round where it is).
    void Close(int32 InTitle, bool InAwait, double Patience = 10)
    {
        Title = InTitle;
        AwaitChange = InAwait;
        AwaitUntil = Now() + Patience;
        if (Stage == EStage::Closing || Stage == EStage::Closed)
            return;
        const double Done = Stage == EStage::Opening ? FMath::Clamp(1 - Age() / OpenSeconds, 0., 1.) : 0.;
        SetStage(EStage::Closing, Done * CloseSeconds);
    }
    // Seconds into the reveal (the world's intro clock); negative while the page is still covered.
    double RevealAge() const
    {
        if (Stage == EStage::Opening)
            return Age();
        if (Stage == EStage::Idle)
            return 1000;
        return -1;
    }
};
