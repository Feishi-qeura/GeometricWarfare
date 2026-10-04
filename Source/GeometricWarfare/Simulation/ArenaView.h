#pragma once
#include "ArenaPhysics.h"

namespace gw {
struct ArenaView {
    double zoom=1;
    Vec center{4000,4000};
    ArenaView(double initialZoom=1,Vec initialCenter={4000,4000}) : zoom(initialZoom),center(initialCenter) { clamp(); }
    void clamp() {
        zoom=std::isfinite(zoom)?std::clamp(zoom,1.0,12.0):1.0;
        const double half=visibleSize()*.5;
        if(!std::isfinite(center.x)) center.x=World::Size*.5;
        if(!std::isfinite(center.y)) center.y=World::Size*.5;
        center.x=std::clamp(center.x,half,World::Size-half);
        center.y=std::clamp(center.y,half,World::Size-half);
    }
    double visibleSize() const { return World::Size/(std::isfinite(zoom)?std::clamp(zoom,1.0,12.0):1.0); }
    // u/v are relative to the rendered square arena, not the entire application.
    Vec screenToWorld(double u,double v) const {
        if(!std::isfinite(u) || !std::isfinite(v)) return center;
        return center+Vec{u-.5,v-.5}*visibleSize();
    }
    // A following camera only changes magnification. Free cameras preserve the
    // point under the cursor unless world-edge clamping intervenes.
    void zoomAt(double u,double v,double factor,bool keepCenter=false) {
        if(!std::isfinite(u) || !std::isfinite(v) || !std::isfinite(factor) || factor<=0) return;
        if(keepCenter) {
            const double current=std::isfinite(zoom)?std::clamp(zoom,1.0,12.0):1.0;
            zoom=std::clamp(current*factor,1.0,12.0); return;
        }
        clamp(); u=std::clamp(u,0.0,1.0); v=std::clamp(v,0.0,1.0);
        const Vec anchor=screenToWorld(u,v);
        zoom=std::clamp(zoom*factor,1.0,12.0);
        center=anchor-Vec{u-.5,v-.5}*visibleSize();
        clamp();
    }
    // Drag the map with the cursor: a positive pixel delta moves its center left/up.
    void panPixels(double dx,double dy,double arenaSide) {
        if(!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(arenaSide) || arenaSide<=0) return;
        clamp(); center+=Vec{-dx,-dy}*(visibleSize()/arenaSide); clamp();
    }
    // The minimap always represents the complete world, regardless of current zoom.
    void jumpNormalized(double u,double v) {
        if(!std::isfinite(u) || !std::isfinite(v)) return;
        center={std::clamp(u,0.0,1.0)*World::Size,std::clamp(v,0.0,1.0)*World::Size};
        clamp();
    }
};
struct PointerDrag {
    bool down=false,dragging=false;
    void begin(double x,double y) {
        cancel();
        if(!std::isfinite(x) || !std::isfinite(y)) return;
        down=true; origin=last={x,y};
    }
    // Apply the release position through update() before finish(). A gesture that
    // ever crossed the 5px threshold stays a drag even if it returns to its origin.
    Vec update(double x,double y) {
        if(!down) return {};
        if(!std::isfinite(x) || !std::isfinite(y)) { cancel(); return {}; }
        const Vec position{x,y},total=position-origin;
        if(!dragging) {
            if(total.dot(total)<25) return {};
            dragging=true; last=position; return total;
        }
        const Vec delta=position-last; last=position; return delta;
    }
    bool finish() { const bool clicked=down&&!dragging; cancel(); return clicked; }
    void cancel() { down=false; dragging=false; origin=last={}; }
private:
    Vec origin{},last{};
};
}
