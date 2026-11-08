#pragma once
#include "CoreMinimal.h"

// Scripted camera and clean frames for promotional capture (GW.Trailer.Capture). Nothing turns this on
// during play, so the follow camera, HUD and menu behave exactly as before unless a capture drives it.
struct FGWCinematic
{
    // Camera looks at Focus from Distance (same units as the zoom setting) at Pitch/Yaw degrees.
    bool Active = false;
    // Match pages draw the battlefield and combat feedback only; the title page draws only its brawl.
    bool Clean = false;
    FVector Focus = FVector::ZeroVector;
    float Distance = 1650, Pitch = 55, Yaw = -90;
    static FGWCinematic& Get()
    {
        static FGWCinematic Instance;
        return Instance;
    }
};
