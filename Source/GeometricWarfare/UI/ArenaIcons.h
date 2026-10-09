#pragma once

#include "CoreMinimal.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "GlobalRenderResources.h"

namespace gwui {

enum class Icon {
    Circle, Square, Rectangle, Triangle, Pistol, Bullet, Heart, Skull,
    Score, People, Crosshair, Shield, Reload, Trophy, Clock, Shotgun, Rifle, Lightning,
    Sniper, MachineGun, RocketLauncher, BossBuff, EvolutionBuff, Hero, WeaponCrate, Base, Revive, Boss
};

namespace detail {
// Unit-circle samples keep small HUD icons cheap and consistent. No font,
// per-icon texture, persistent object, or dynamically generated path is needed.
inline const FVector2D RingPoints[] = {
    {1,0}, {.923880,.382683}, {.707107,.707107}, {.382683,.923880},
    {0,1}, {-.382683,.923880}, {-.707107,.707107}, {-.923880,.382683},
    {-1,0}, {-.923880,-.382683}, {-.707107,-.707107}, {-.382683,-.923880},
    {0,-1}, {.382683,-.923880}, {.707107,-.707107}, {.923880,-.382683}
};

struct IconPainter {
    UCanvas* Canvas;
    FVector2D Center;
    float Scale;
    float Stroke;
    FLinearColor Color;

    FVector2D Point(FVector2D P) const { return Center+P*Scale; }

    void Line(FVector2D A,FVector2D B) const {
        FCanvasLineItem Item(Point(A),Point(B));
        Item.SetColor(Color);
        Item.LineThickness=Stroke;
        Item.BlendMode=SE_BLEND_Translucent;
        Canvas->DrawItem(Item);
    }

    template<int N> void Outline(const FVector2D (&Points)[N],bool Closed=true) const {
        for(int i=1;i<N;++i) Line(Points[i-1],Points[i]);
        if(Closed) Line(Points[N-1],Points[0]);
    }

    void Ring(FVector2D Origin,float Radius,int Stride=1) const {
        for(int i=0;i<16;i+=Stride)
            Line(Origin+RingPoints[i]*Radius,Origin+RingPoints[(i+Stride)%16]*Radius);
    }

    void Triangle(FVector2D A,FVector2D B,FVector2D C) const {
        FCanvasTriangleItem Item(Point(A),Point(B),Point(C),GWhiteTexture);
        Item.SetColor(Color);
        Item.BlendMode=SE_BLEND_Translucent;
        Canvas->DrawItem(Item);
    }

    // The few filled icons are star-shaped around Anchor. Submit their triangle
    // fan once, rather than creating a separate Canvas item for every triangle.
    template<int N> void Fill(const FVector2D (&Points)[N],FVector2D Anchor) const {
        FCanvasTriangleItem Item(Point(Anchor),Point(Points[0]),Point(Points[1]),GWhiteTexture);
        Item.TriangleList.Reserve(N);
        for(int i=1;i<N;++i) {
            FCanvasUVTri Tri{};
            Tri.V0_Pos=Point(Anchor);
            Tri.V1_Pos=Point(Points[i]);
            Tri.V2_Pos=Point(Points[(i+1)%N]);
            Tri.V0_UV=Tri.V1_UV=Tri.V2_UV=FVector2D::ZeroVector;
            Item.TriangleList.Add(Tri);
        }
        Item.SetColor(Color);
        Item.BlendMode=SE_BLEND_Translucent;
        Canvas->DrawItem(Item);
    }
};
} // namespace detail

// Size is the icon's outer square in screen pixels; Center is its midpoint.
// Geometry is inset by half the stroke so the whole icon stays within that box.
// Intended for compact HUD labels (typically 16-32 px), independent of DPI/fonts.
inline void DrawIcon(UCanvas* Canvas,Icon Which,FVector2D Center,float Size,FLinearColor Color) {
    if(!Canvas || !FMath::IsFinite(Size) || Size<=1 || Color.A<=0
        || !FMath::IsFinite(Center.X) || !FMath::IsFinite(Center.Y)) return;
    const float Stroke=FMath::Clamp(Size*.07f,1.f,3.f);
    const detail::IconPainter P{Canvas,Center,Size-Stroke,Stroke,Color};
    switch(Which) {
    case Icon::Base: {
        const FVector2D V[]={{-.44,.40},{-.44,-.25},{-.27,-.25},{-.27,-.43},{-.10,-.43},{-.10,-.25},{.10,-.25},{.10,-.43},{.27,-.43},{.27,-.25},{.44,-.25},{.44,.40}};
        P.Outline(V);P.Line({-.10,.40},{-.10,.10});P.Line({-.10,.10},{.10,.10});P.Line({.10,.10},{.10,.40});break;
    }
    case Icon::Revive:
        P.Ring({0,0},.42f);P.Line({0,-.25},{0,.25});P.Line({-.25,0},{.25,0});break;
    case Icon::Boss: {
        const FVector2D V[]={{-.30,-.40},{.30,-.40},{.47,.38},{-.47,.38}};
        P.Outline(V);P.Line({-.25,-.08},{-.10,.02});P.Line({.25,-.08},{.10,.02});P.Line({-.18,.23},{.18,.23});break;
    }
    case Icon::Circle:
        P.Ring({0,0},.43f);
        break;
    case Icon::Square: {
        const FVector2D V[]={{-.37,-.37},{.37,-.37},{.37,.37},{-.37,.37}};
        P.Outline(V); break;
    }
    case Icon::Rectangle: {
        const FVector2D V[]={{-.46,-.25},{.46,-.25},{.46,.25},{-.46,.25}};
        P.Outline(V); break;
    }
    case Icon::Triangle: {
        const FVector2D V[]={{0,-.45},{.43,.32},{-.43,.32}};
        P.Outline(V); break;
    }
    case Icon::Pistol: {
        const FVector2D Barrel[]={{-.43,-.24},{.44,-.24},{.44,-.025},{-.43,-.025}};
        const FVector2D Grip[]={{-.23,-.025},{.02,-.025},{-.08,.38},{-.29,.38}};
        P.Fill(Barrel,{0,-.13}); P.Fill(Grip,{-.14,.17});
        P.Line({.04,-.025},{.17,.12}); P.Line({.17,.12},{-.025,.12});
        P.Line({.32,-.31},{.32,-.24});
        break;
    }
    case Icon::Shotgun:
        P.Line({-.45,-.16},{.47,-.16});P.Line({-.45,-.05},{.47,-.05});
        P.Line({-.27,-.02},{-.40,.25});P.Line({-.41,.22},{-.48,.22});
        P.Line({.04,-.025},{.26,-.025});P.Line({.02,.04},{.25,.04});
        break;
    case Icon::Rifle:
        P.Line({-.47,-.08},{.47,-.08});P.Line({-.24,-.19},{.30,-.19});
        P.Line({-.43,-.08},{-.47,.15});P.Line({-.47,.15},{-.23,.01});
        P.Line({-.08,-.02},{-.14,.29});P.Line({.12,-.02},{.18,.23});
        P.Line({.18,.23},{.27,.23});P.Line({.07,-.29},{.22,-.29});
        break;
    case Icon::Sniper:
        P.Line({-.47,.01},{.48,.01});P.Line({.38,-.05},{.48,-.05});
        P.Line({-.46,.01},{-.43,.21});P.Line({-.43,.21},{-.21,.04});
        P.Line({-.08,-.04},{-.08,-.19});P.Line({.13,-.04},{.13,-.19});
        P.Line({-.19,-.22},{.22,-.22});P.Line({-.19,-.30},{.22,-.30});
        P.Line({-.19,-.30},{-.19,-.22});P.Line({.22,-.30},{.22,-.22});
        P.Line({.12,.03},{.02,.33});P.Line({.12,.03},{.30,.29});
        break;
    case Icon::MachineGun: {
        const FVector2D Body[]={{-.40,-.22},{.16,-.22},{.16,.04},{-.40,.04}};
        P.Fill(Body,{-.12,-.08});P.Line({.16,-.17},{.47,-.17});P.Line({.16,-.06},{.47,-.06});
        P.Line({-.40,-.07},{-.48,.14});P.Line({-.48,.14},{-.27,.06});
        P.Ring({-.04,.19},.17f,2);P.Line({.25,.04},{.22,.36});P.Line({.25,.04},{.43,.31});break;
    }
    case Icon::RocketLauncher: {
        const FVector2D Tube[]={{-.43,-.15},{.32,-.15},{.43,-.25},{.43,.15},{.32,.05},{-.43,.05}};
        P.Fill(Tube,{0,-.05});P.Line({-.43,-.23},{-.43,.13});
        P.Line({-.16,.05},{-.21,.31});P.Line({.11,.05},{.05,.30});
        P.Line({-.06,-.17},{-.06,-.34});P.Line({-.06,-.34},{.12,-.34});break;
    }
    case Icon::BossBuff:
        P.Ring({0,0},.44f,2);P.Line({-.23,-.11},{-.06,-.01});P.Line({.06,-.01},{.23,-.11});
        P.Line({-.16,.20},{.16,.20});P.Line({-.23,-.11},{-.20,-.22});P.Line({.23,-.11},{.20,-.22});break;
    case Icon::EvolutionBuff: {
        const FVector2D Box[]={{-.42,-.42},{.42,-.42},{.42,.42},{-.42,.42}};
        P.Outline(Box);P.Triangle({.10,-.31},{-.20,.05},{.07,.02});P.Triangle({-.06,-.02},{.20,-.05},{-.10,.31});break;
    }
    case Icon::Hero: {
        const FVector2D Crown[]={{-.44,-.22},{-.22,-.02},{0,-.40},{.22,-.02},{.44,-.22},{.33,.25},{-.33,.25}};
        P.Fill(Crown,{0,.02});P.Line({-.33,.38},{.33,.38});break;
    }
    case Icon::WeaponCrate: {
        const FVector2D Box[]={{-.40,-.35},{.40,-.35},{.40,.35},{-.40,.35}};
        P.Outline(Box);P.Line({-.40,-.16},{.40,-.16});P.Line({-.17,-.35},{-.17,.35});P.Line({.17,-.35},{.17,.35});
        P.Line({-.11,.06},{.11,.06});P.Line({0,-.05},{0,.17});break;
    }
    case Icon::Lightning: {
        const FVector2D V[]={{.12,-.47},{-.29,.06},{-.03,.06},{-.12,.47},{.30,-.10},{.03,-.10}};
        P.Fill(V,{0,0});break;
    }
    case Icon::Bullet: {
        const FVector2D V[]={{0,-.45},{.17,-.23},{.17,.37},{-.17,.37},{-.17,-.23}};
        P.Outline(V); P.Line({-.17,.23},{.17,.23});
        break;
    }
    case Icon::Heart: {
        const FVector2D V[]={{0,.43},{-.38,.04},{-.43,-.15},{-.30,-.35},{-.13,-.38},
            {0,-.22},{.13,-.38},{.30,-.35},{.43,-.15},{.38,.04}};
        P.Fill(V,{0,0}); break;
    }
    case Icon::Skull: {
        const FVector2D V[]={{-.27,.06},{-.39,-.07},{-.39,-.24},{-.23,-.41},
            {.23,-.41},{.39,-.24},{.39,-.07},{.27,.06},{.25,.35},{-.25,.35}};
        P.Outline(V);
        P.Line({-.23,-.12},{-.09,-.12}); P.Line({.09,-.12},{.23,-.12});
        P.Triangle({0,-.035},{-.055,.07},{.055,.07});
        P.Line({-.085,.20},{-.085,.35}); P.Line({.085,.20},{.085,.35});
        break;
    }
    case Icon::Score: {
        const FVector2D V[]={{0,-.45},{.126,-.173},{.428,-.139},{.204,.066},{.265,.364},
            {0,.214},{-.265,.364},{-.204,.066},{-.428,-.139},{-.126,-.173}};
        P.Fill(V,{0,0}); break;
    }
    case Icon::People: {
        P.Ring({-.14,-.23},.14f,2); P.Ring({.23,-.15},.105f,2);
        const FVector2D Front[]={{-.43,.35},{-.40,.15},{-.27,.03},{-.02,.03},{.12,.15},{.15,.35}};
        const FVector2D Back[]={{.19,.07},{.34,.10},{.44,.23},{.44,.35}};
        P.Outline(Front,false); P.Outline(Back,false);
        break;
    }
    case Icon::Crosshair:
        P.Ring({0,0},.25f,2);
        P.Line({-.46,0},{-.15,0}); P.Line({.15,0},{.46,0});
        P.Line({0,-.46},{0,-.15}); P.Line({0,.15},{0,.46});
        break;
    case Icon::Shield: {
        const FVector2D V[]={{0,-.44},{.37,-.32},{.31,.15},{0,.43},{-.31,.15},{-.37,-.32}};
        P.Outline(V); P.Line({-.16,-.01},{-.025,.13}); P.Line({-.025,.13},{.20,-.14});
        break;
    }
    case Icon::Reload:
        for(int i=2;i<15;++i) P.Line(detail::RingPoints[i]*.35,detail::RingPoints[i+1]*.35);
        P.Triangle({.46,.02},{.17,-.04},{.40,-.24});
        break;
    case Icon::Trophy: {
        const FVector2D Cup[]={{-.29,-.38},{.29,-.38},{.23,.06},{.12,.19},{-.12,.19},{-.23,.06}};
        const FVector2D Left[]={{-.29,-.26},{-.44,-.26},{-.41,-.03},{-.23,.06}};
        const FVector2D Right[]={{.29,-.26},{.44,-.26},{.41,-.03},{.23,.06}};
        P.Outline(Cup); P.Outline(Left,false); P.Outline(Right,false);
        P.Line({0,.19},{0,.40}); P.Line({-.25,.40},{.25,.40});
        break;
    }
    case Icon::Clock:
        P.Ring({0,0},.43f); P.Line({0,-.25},{0,0}); P.Line({0,0},{.20,.10});
        break;
    }
}

} // namespace gwui
