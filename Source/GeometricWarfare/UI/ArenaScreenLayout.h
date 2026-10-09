#pragma once
#include "CoreMinimal.h"

namespace gwui {
// One uniform scale for drawing and its inverse for input; never stretch X/Y separately.
struct ScreenLayout {
    FVector2D logicalSize=FVector2D::ZeroVector,offset=FVector2D::ZeroVector;
    float scale=1;
    bool mobile=false,portrait=false;
    FVector2D toScreen(FVector2D P) const {return offset+P*scale;}
    FVector2D toLogical(FVector2D P) const {return (P-offset)/scale;}
};
inline float LayoutAspect(int Preset,float Automatic) {
    const float Ratios[]={0,16.f/9,4.f/3,9.f/16,20.f/17,19.f/9};
    return Preset>=1 && Preset<=5?Ratios[Preset]:Automatic;
}
inline ScreenLayout ResolveScreenLayout(float Width,float Height,int Device,int Aspect) {
    if(!FMath::IsFinite(Width)||!FMath::IsFinite(Height)||Width<=0||Height<=0){Width=1920;Height=1080;}
    const float Ratio=LayoutAspect(Aspect,FMath::Clamp(Width/Height,.35f,4.f));
    const bool Portrait=Ratio<1;
    const bool Mobile=Device==2 || (Device==0 && Portrait);
    const float ReferenceHeight=Portrait?(Mobile?1280.f:1920.f):(Mobile?720.f:1080.f);
    const FVector2D Logical(Ratio*ReferenceHeight,ReferenceHeight);
    const float Scale=FMath::Min(Width/Logical.X,Height/Logical.Y);
    return {Logical,(FVector2D(Width,Height)-Logical*Scale)*.5f,Scale,Mobile,Portrait};
}
}
