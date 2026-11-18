#pragma once
#include "CoreMinimal.h"

// Scripted camera and clean frames for promotional capture (GW.Promo.Capture). Nothing turns this on during play, so
// the follow camera, HUD and menu behave exactly as before unless a capture drives it.
struct FGWCinematic
{
    // Camera looks at Focus from Distance (same units as the zoom setting) at Pitch/Yaw degrees.
    bool Active = false;
    // Match pages draw the battlefield and combat feedback only; the title page draws only its brawl.
    bool Clean = false;
    // A clean frame that still tells its story over the field: name tags, bait countdowns and the wave banners.
    bool Story = false;
    // The full HUD without its furniture (key bar, tips, settings gear, squad and team cards, the local speech bubble):
    // boards, banners, tags, the build ring, sheets and result cards stay.
    bool Promo = false;
    // The wave-cleared banner stays down (a capture that jumps a page ahead several waves at once).
    bool QuietClears = false;
    // A capture stages its friends' room in one offline process; the room card shows it as the Steam room it stands for.
    bool SteamRoom = false;
    FVector Focus = FVector::ZeroVector;
    float Distance = 1650, Pitch = 55, Yaw = -90;
    static FGWCinematic& Get()
    {
        static FGWCinematic Instance;
        return Instance;
    }
};
