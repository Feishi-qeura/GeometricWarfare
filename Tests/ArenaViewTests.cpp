#include "../Source/GeometricWarfare/Simulation/ArenaView.h"
#include "../Source/GeometricWarfare/UI/ArenaHealthDisplay.h"
#include <cmath>
#include <iostream>

static int assertions=0,failures=0;
static void check(bool ok,const char* name) { ++assertions; if(!ok) { ++failures; std::cerr<<"FAIL: "<<name<<'\n'; } }
static bool close(double a,double b) { return std::abs(a-b)<1e-8; }
static bool close(gw::Vec a,gw::Vec b) { return close(a.x,b.x)&&close(a.y,b.y); }
static void rectangularViews() {
    const double World=gw::World::Size;
    for(const double aspect:{2.0,.5,16.0/9,9.0/16,4.0/3,20.0/17,19.0/9}) {
        const double width=1000*aspect,height=1000,scale=std::max(width,height)/(World/2);
        auto v=gw::ArenaView(2,{World*.5,World*.5},aspect);
        const gw::Vec span{width/scale,height/scale};
        check(close(v.visibleExtent(),span),"rectangle extent uses one common physical scale on both axes");
        check(close(v.screenToWorld(0,0),{World*.5-span.x*.5,World*.5-span.y*.5})&&close(v.screenToWorld(1,1),{World*.5+span.x*.5,World*.5+span.y*.5}),"rectangular corners map to independently expected world coordinates");
        const auto origin=v.screenToWorld(.5,.5);
        check(close((v.screenToWorld(.5+20/width,.5)-origin).length(),(v.screenToWorld(.5,.5+20/height)-origin).length()),"equal pixel distances cannot stretch circles across axes");
        const auto click=v.screenToWorld(.23,.71),northwest=v.screenToWorld(0,0);
        check(close((click.x-northwest.x)*scale,width*.23)&&close((click.y-northwest.y)*scale,height*.71),"click and rendering projection round trip in a rectangular viewport");
        v.panPixels(40,-25,width,height);
        check(close(v.center,{World*.5-40/scale,World*.5+25/scale}),"rectangular drag preserves common world units per pixel");
        const auto anchor=v.screenToWorld(.2,.7);
        v.zoomAt(.2,.7,2);
        check(close(v.screenToWorld(.2,.7),anchor),"rectangular cursor zoom preserves its world anchor away from boundaries");
        const auto half=gw::Vec{span.x*.25,span.y*.25};
        v.jumpNormalized(1,0);
        check(close(v.center,{World-half.x,half.y}),"rectangular minimap jump clamps each camera half extent separately");
        v.panPixels(-1e9,1e9,width,height);
        check(close(v.screenToWorld(1,0),{World,0}),"rectangular pan reaches actual north east world edges without overshooting");
        const auto fixed=v.center;v.zoomAt(.1,.9,2,true);
        check(close(v.center,fixed)&&close(v.zoom,8),"rectangular following zoom retains the tracked center");
        auto overview=gw::ArenaView(1,{World*.5,World*.5},aspect);
        const auto overviewSpan=gw::Vec{span.x*2,span.y*2};
        overview.jumpNormalized(0,1);
        check(close(overview.center,{overviewSpan.x*.5,World-overviewSpan.y*.5}),"minimum zoom still permits panning on the cropped rectangle axis");
        const auto a=overview.screenToWorld(0,0),b=overview.screenToWorld(1,1);
        check(a.x>=-1e-8&&a.y>=-1e-8&&b.x<=World+1e-8&&b.y<=World+1e-8,"minimum rectangular zoom keeps its entire viewport within the world");
        const auto previous=overview.center;
        overview.panPixels(10,20,0,height);overview.panPixels(10,20,width,0);overview.panPixels(10,20,NAN,height);overview.panPixels(10,20,width,INFINITY);
        check(close(overview.center,previous),"invalid rectangle dimensions cannot mutate the camera");
    }
    for(const double aspect:{0.0,-1.0,static_cast<double>(NAN),static_cast<double>(INFINITY)}) {
        const auto v=gw::ArenaView(2,{World*.5,World*.5},aspect);
        check(close(v.visibleExtent(),{World*.5,World*.5}),"invalid aspect falls back to a finite square view");
    }
}
int main() {
    check(gwui::HealthDisplay(250.01)=="251" && gwui::HealthDisplay(1002500)=="1002500","ordinary and 1000-gift HP retain exact positive rounded display");
    const auto LargeHp=gwui::HealthDisplay(2147483648.0);
    check(LargeHp.size()<16 && std::stod(LargeHp)>2.14e9 && std::stod(LargeHp)<2.16e9,"HP above int32 remains compact and positive without overflow");
    const auto LongGiftHp=gwui::HealthDisplay(1000.0*static_cast<double>(INT64_MAX));
    check(LongGiftHp.size()<16 && std::stod(LongGiftHp)>9e21,"maximum int64 gift HP formats without integer narrowing or oversized text");
    gw::ArenaView v;
    check(close(v.screenToWorld(0,0),{0,0}) && close(v.screenToWorld(1,1),{(gw::World::Size*1),(gw::World::Size*1)}),"overview covers the entire world");
    const auto anchor=v.screenToWorld(.25,.75);
    v.zoomAt(.25,.75,2);
    check(close(v.zoom,2) && close(v.center,{(gw::World::Size*0.375),(gw::World::Size*0.625)}),"off-center zoom moves the camera toward its cursor anchor");
    check(close(v.screenToWorld(.25,.75),anchor),"zoom preserves the world point below the cursor");
    v.zoomAt(.25,.75,.5);
    check(close(v.zoom,1) && close(v.center,{(gw::World::Size*0.5),(gw::World::Size*0.5)}),"zoom out returns to full-world overview");
    v.zoomAt(.5,.5,100);
    check(close(v.zoom,12),"zoom cannot exceed twelve times");
    v.zoomAt(.1,.9,.00001);
    check(close(v.zoom,1) && close(v.center,{(gw::World::Size*0.5),(gw::World::Size*0.5)}),"minimum zoom clamps to a centered overview");
    v=gw::ArenaView(4,{(gw::World::Size*0.5),(gw::World::Size*0.5)});
    v.panPixels(100,-50,800);
    check(close(v.center,{(gw::World::Size*0.46875),(gw::World::Size*0.515625)}),"drag moves world content with the mouse using viewport scale");
    v.panPixels(1e6,-1e6,800);
    check(close(v.center,{(gw::World::Size*0.125),(gw::World::Size*0.875)}),"panning clamps the whole visible viewport inside world edges");
    v.jumpNormalized(1,0);
    check(close(v.center,{(gw::World::Size*0.875),(gw::World::Size*0.125)}),"minimap corner jump obeys camera edge bounds");
    v.jumpNormalized(.25,.75);
    check(close(v.center,{(gw::World::Size*0.25),(gw::World::Size*0.75)}),"minimap maps its normalized coordinates over the full world");
    v.jumpNormalized(-3,4);
    check(close(v.center,{(gw::World::Size*0.125),(gw::World::Size*0.875)}),"minimap dragging beyond bounds clamps to its edges");
    const auto before=v.center; const auto zoom=v.zoom;
    v.zoomAt(.5,.5,NAN); v.zoomAt(.5,.5,0); v.panPixels(50,20,0); v.panPixels(NAN,0,800); v.jumpNormalized(NAN,.5);
    check(close(v.center,before) && close(v.zoom,zoom),"invalid pointer math inputs leave the view unchanged");
    v=gw::ArenaView(0,{-100,9000});
    check(close(v.zoom,1) && close(v.center,{(gw::World::Size*0.5),(gw::World::Size*0.5)}),"loading an out-of-range camera state is safe");
    v=gw::ArenaView(12,{NAN,INFINITY});
    check(close(v.center,{(gw::World::Size*0.5),(gw::World::Size*0.5)}),"non-finite loaded camera centers recover to arena center");
    v=gw::ArenaView(2,{(gw::World::Size*0.5),(gw::World::Size*0.5)});
    v.zoomAt(.25,.75,2,true);
    check(close(v.zoom,4) && close(v.center,{(gw::World::Size*0.5),(gw::World::Size*0.5)}),"following zoom changes magnification without moving the tracked center");
    v.zoomAt(.9,.1,.5,true);
    check(close(v.zoom,2) && close(v.center,{(gw::World::Size*0.5),(gw::World::Size*0.5)}),"following zoom out also preserves the tracked center");

    rectangularViews();
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
