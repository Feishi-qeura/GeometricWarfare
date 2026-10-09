#pragma once

namespace gwui {
struct CompactActorDetail { bool markerOnly=false,showLabel=false,showBars=false; };
inline bool IsFullMapOverview(float Zoom) { return Zoom<=1.01f; }
inline CompactActorDetail ResolveCompactActorDetail(float PixelScale,float Zoom,bool Priority) {
    const bool Overview=IsFullMapOverview(Zoom);
    // Keep readable silhouettes across the compact arena. Details belong to
    // followed/special actors in overview; the crowd cannot cover one another.
    return {PixelScale<.08f&&!Priority,Priority||(!Overview&&PixelScale>.38f),Priority||(!Overview&&PixelScale>.26f)};
}
}
