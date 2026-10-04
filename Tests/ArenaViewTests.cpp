#include "../Source/GeometricWarfare/Simulation/ArenaView.h"
#include <cmath>
#include <iostream>

static int assertions=0,failures=0;
static void check(bool ok,const char* name) { ++assertions; if(!ok) { ++failures; std::cerr<<"FAIL: "<<name<<'\n'; } }
static bool close(double a,double b) { return std::abs(a-b)<1e-8; }
static bool close(gw::Vec a,gw::Vec b) { return close(a.x,b.x)&&close(a.y,b.y); }
int main() {
    gw::ArenaView v;
    check(close(v.screenToWorld(0,0),{0,0}) && close(v.screenToWorld(1,1),{8000,8000}),"overview covers the entire world");
    const auto anchor=v.screenToWorld(.25,.75);
    v.zoomAt(.25,.75,2);
    check(close(v.zoom,2) && close(v.center,{3000,5000}),"off-center zoom moves the camera toward its cursor anchor");
    check(close(v.screenToWorld(.25,.75),anchor),"zoom preserves the world point below the cursor");
    v.zoomAt(.25,.75,.5);
    check(close(v.zoom,1) && close(v.center,{4000,4000}),"zoom out returns to full-world overview");
    v.zoomAt(.5,.5,100);
    check(close(v.zoom,12),"zoom cannot exceed twelve times");
    v.zoomAt(.1,.9,.00001);
    check(close(v.zoom,1) && close(v.center,{4000,4000}),"minimum zoom clamps to a centered overview");
    v=gw::ArenaView(4,{4000,4000});
    v.panPixels(100,-50,800);
    check(close(v.center,{3750,4125}),"drag moves world content with the mouse using viewport scale");
    v.panPixels(1e6,-1e6,800);
    check(close(v.center,{1000,7000}),"panning clamps the whole visible viewport inside world edges");
    v.jumpNormalized(1,0);
    check(close(v.center,{7000,1000}),"minimap corner jump obeys camera edge bounds");
    v.jumpNormalized(.25,.75);
    check(close(v.center,{2000,6000}),"minimap maps its normalized coordinates over the full world");
    v.jumpNormalized(-3,4);
    check(close(v.center,{1000,7000}),"minimap dragging beyond bounds clamps to its edges");
    const auto before=v.center; const auto zoom=v.zoom;
    v.zoomAt(.5,.5,NAN); v.zoomAt(.5,.5,0); v.panPixels(50,20,0); v.panPixels(NAN,0,800); v.jumpNormalized(NAN,.5);
    check(close(v.center,before) && close(v.zoom,zoom),"invalid pointer math inputs leave the view unchanged");
    v=gw::ArenaView(0,{-100,9000});
    check(close(v.zoom,1) && close(v.center,{4000,4000}),"loading an out-of-range camera state is safe");
    v=gw::ArenaView(12,{NAN,INFINITY});
    check(close(v.center,{4000,4000}),"non-finite loaded camera centers recover to arena center");
    v=gw::ArenaView(2,{4000,4000});
    v.zoomAt(.25,.75,2,true);
    check(close(v.zoom,4) && close(v.center,{4000,4000}),"following zoom changes magnification without moving the tracked center");
    v.zoomAt(.9,.1,.5,true);
    check(close(v.zoom,2) && close(v.center,{4000,4000}),"following zoom out also preserves the tracked center");

    gw::PointerDrag drag;
    check(!drag.finish(),"release without a press cannot click");
    drag.begin(100,100);
    check(close(drag.update(102,102),{0,0}) && !drag.dragging,"small click jitter does not pan");
    check(drag.finish() && !drag.down,"small pointer motion still permits a click on release");
    drag.begin(100,100);
    check(close(drag.update(103,104),{3,4}) && drag.dragging,"five pixel radial displacement begins a drag");
    check(close(drag.update(106,108),{3,4}),"subsequent drag updates return incremental motion");
    drag.update(100,100);
    check(!drag.finish(),"returning to the press point never turns a drag into a click");
    drag.begin(0,0); drag.update(9,0); drag.cancel();
    check(!drag.finish() && !drag.down,"losing capture cancels drag without a click");
    drag.begin(0,0); drag.update(NAN,1);
    check(!drag.finish(),"invalid pointer coordinates cancel selection");
    drag.begin(0,0); drag.update(5,0);
    check(!drag.finish(),"release-position update catches a drag before any intervening frame");
    std::cout<<"Arena view tests: "<<assertions<<" assertions, "<<failures<<" failures\n";
    return failures?1:0;
}
