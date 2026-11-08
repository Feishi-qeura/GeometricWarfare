#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GWHUD.h"
#include "GWPalette.h"
#include "GWText.h"
#include "TextureResource.h"

// Sketchbook drawing toolkit shared by every HUD page. Paper never moves; ink lines re-seed with
// the HUD "boil" counter so outlines shimmer like a hand-animated drawing.
namespace
{
float Noise(int32 Seed, int32 Index)
{
    uint32 H = static_cast<uint32>(Seed) * 747796405u + static_cast<uint32>(Index) * 2891336453u + 12345u;
    H = ((H >> ((H >> 28) + 4u)) ^ H) * 277803737u;
    H = (H >> 22) ^ H;
    return (H & 0xffffff) / 8388607.5f - 1.f;
}
bool IsCJK(TCHAR C)
{
    return (C >= 0x2E80 && C <= 0x9FFF) || (C >= 0xF900 && C <= 0xFAFF) || (C >= 0xFF00 && C <= 0xFFEF) ||
           (C >= 0xAC00 && C <= 0xD7AF);
}
bool NoLineStart(TCHAR C)
{
    return FCString::Strchr(TEXT("，。、；：！？）」』】》…・ー,.;:!?)%"), C) != nullptr;
}
} // namespace

UFont* AGWHUD::FaceFont(EFace Face) const
{
    UFont* Chosen = Face == EFace::Heavy ? HeavyFont.Get() : Face == EFace::Bold ? BoldFont.Get() : Font.Get();
    return Chosen ? Chosen : Font.Get();
}

void AGWHUD::Line(FVector2D A, FVector2D B, FLinearColor Color, float Thickness)
{
    FCanvasLineItem Item(A, B);
    Item.SetColor(Color);
    Item.LineThickness = Thickness;
    Canvas->DrawItem(Item);
}

void AGWHUD::Pen(FVector2D A, FVector2D B, FLinearColor Color, float Thickness, int32 Seed, float Wobble)
{
    const FVector2D D = B - A;
    const float Length = D.Size();
    if (Length < .5f)
        return;
    const FVector2D Dir = D / Length, Perp(-Dir.Y, Dir.X);
    const int32 S = Seed * 31 + Boil;
    const float Over = FMath::Min(Length * .05f, 3.5f * Scale) * Wobble;
    const float Bow = FMath::Clamp(Length * .012f, .3f, 2.6f * Scale) * Wobble * Noise(S, 3);
    const FVector2D Start = A - Dir * Over * (.35f + .65f * FMath::Abs(Noise(S, 1)));
    const FVector2D End = B + Dir * Over * (.35f + .65f * FMath::Abs(Noise(S, 2)));
    const int32 Segments = FMath::Clamp(FMath::CeilToInt(Length / (26 * Scale)), 1, 8);
    FVector2D Previous = Start;
    for (int32 I = 1; I <= Segments; ++I)
    {
        const float T = I / float(Segments);
        const FVector2D Point = FMath::Lerp(Start, End, T) + Perp * FMath::Sin(PI * T) * Bow;
        Line(Previous, Point, Color, Thickness);
        Previous = Point;
    }
    if (Thickness >= 1.2f && Wobble > 0)
    {
        // The faint second pass of a pencil that went over the line twice.
        const FVector2D Offset = Perp * Noise(S, 4) * 1.1f * Scale;
        Line(A + Offset + Dir * Over * .4f, B + Offset - Dir * Over * .2f, Color * FLinearColor(1, 1, 1, .32f),
             FMath::Max(.8f, Thickness * .55f));
    }
}

void AGWHUD::Fill(const TArray<FVector2D>& Points, FLinearColor Color)
{
    if (Points.Num() < 3 || Color.A <= 0)
        return;
    TArray<FCanvasUVTri> Triangles;
    for (int32 I = 1; I + 1 < Points.Num(); ++I)
    {
        FCanvasUVTri T;
        T.V0_Pos = Points[0];
        T.V1_Pos = Points[I];
        T.V2_Pos = Points[I + 1];
        T.V0_UV = T.V1_UV = T.V2_UV = FVector2D::ZeroVector;
        T.V0_Color = T.V1_Color = T.V2_Color = Color;
        Triangles.Add(T);
    }
    FCanvasTriangleItem Item(Triangles, GWhiteTexture);
    Item.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Item);
}

void AGWHUD::Box(float X, float Y, float W, float H, FLinearColor Color)
{
    FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), Color);
    Tile.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Tile);
}

void AGWHUD::Grained(float X, float Y, float W, float H, float)
{
    if (!Grain || !Grain->GetResource() || W <= 0 || H <= 0)
        return;
    const float Tile = 256 * FMath::Max(.75f, Scale);
    FCanvasTileItem Item(FVector2D(X, Y), Grain->GetResource(), FVector2D(W, H), FVector2D(X / Tile, Y / Tile),
                         FVector2D((X + W) / Tile, (Y + H) / Tile), FLinearColor::White);
    Item.BlendMode = SE_BLEND_Modulate;
    Canvas->DrawItem(Item);
}

void AGWHUD::Card(float X, float Y, float W, float H, FLinearColor PaperColor, FLinearColor Stroke, int32 Seed, float Lift,
                  float StrokeWidth)
{
    const float J = 1.4f * Scale;
    auto Corner = [&](float CX, float CY, int32 K) { return FVector2D(CX + Noise(Seed, K) * J, CY + Noise(Seed, K + 9) * J); };
    const FVector2D A = Corner(X, Y, 1), B = Corner(X + W, Y, 2), C = Corner(X + W, Y + H, 3), D = Corner(X, Y + H, 4);
    if (Lift > 0)
    {
        const FVector2D Drop(3.f * Lift * Scale, 5.f * Lift * Scale);
        Fill({A + Drop, B + Drop, C + Drop, D + Drop}, GWPalette::Ink.CopyWithNewOpacity(.12f));
        Fill({A + Drop * .45f, B + Drop * .45f, C + Drop * .45f, D + Drop * .45f}, GWPalette::Ink.CopyWithNewOpacity(.08f));
    }
    Fill({A, B, C, D}, PaperColor);
    if (PaperColor.A > .9f)
        Grained(X, Y, W, H);
    const float Thick = StrokeWidth > 0 ? StrokeWidth : 2.f * Scale;
    if (Stroke.A > 0)
    {
        Pen(A, B, Stroke, Thick, Seed + 1);
        Pen(B, C, Stroke, Thick, Seed + 2);
        Pen(C, D, Stroke, Thick, Seed + 3);
        Pen(D, A, Stroke, Thick, Seed + 4);
    }
}

void AGWHUD::Tape(FVector2D Center, float W, float H, float Degrees, FLinearColor Color)
{
    const FVector2D X = FVector2D(FMath::Cos(FMath::DegreesToRadians(Degrees)), FMath::Sin(FMath::DegreesToRadians(Degrees)));
    const FVector2D Y(-X.Y, X.X);
    TArray<FVector2D> Points;
    const int32 Teeth = 4;
    for (int32 I = 0; I <= Teeth; ++I)
        Points.Add(Center + X * (W * .5f + (I % 2 ? -2.5f : 0) * Scale) + Y * (-H * .5f + H * I / Teeth));
    for (int32 I = 0; I <= Teeth; ++I)
        Points.Add(Center - X * (W * .5f + (I % 2 ? -2.5f : 0) * Scale) + Y * (H * .5f - H * I / Teeth));
    // Fan from the centre keeps the zig-zag ends valid as a convex-ish fill.
    TArray<FVector2D> Fan = {Center};
    Fan.Append(Points);
    Fan.Add(Points[0]);
    Fill(Fan, Color.CopyWithNewOpacity(Color.A * .62f));
    Line(Center - X * W * .42f - Y * H * .5f, Center + X * W * .42f - Y * H * .5f, FLinearColor(1, 1, 1, .25f), 1);
}

void AGWHUD::Highlight(float X, float Y, float W, float H, FLinearColor Color, int32 Seed)
{
    TArray<FVector2D> Points;
    const int32 Steps = 6;
    const float Skew = H * .35f;
    for (int32 I = 0; I <= Steps; ++I)
        Points.Add(FVector2D(X + Skew + (W - Skew) * I / Steps, Y + Noise(Seed, I) * H * .12f));
    for (int32 I = Steps; I >= 0; --I)
        Points.Add(FVector2D(X + (W - Skew) * I / Steps, Y + H + Noise(Seed, I + 20) * H * .12f));
    TArray<FVector2D> Fan = {FVector2D(X + W * .5f, Y + H * .5f)};
    Fan.Append(Points);
    Fan.Add(Points[0]);
    Fill(Fan, Color);
}

void AGWHUD::Hatch(float X, float Y, float W, float H, FLinearColor Color, float Spacing, float Thickness, int32 Seed)
{
    if (W <= 0 || H <= 0)
        return;
    int32 Index = 0;
    for (float K = -H; K < W; K += Spacing, ++Index)
    {
        const float X0 = FMath::Max(X + K, X), X1 = FMath::Min(X + K + H, X + W);
        if (X0 >= X1)
            continue;
        const float J = Noise(Seed, Index) * .8f * Scale;
        Line(FVector2D(X0, Y + H - (X0 - X - K) + J), FVector2D(X1, Y + H - (X1 - X - K) - J), Color, Thickness);
    }
}

void AGWHUD::Circle(FVector2D Center, float Radius, FLinearColor Color, float Thickness, int32 Seed, float Turns)
{
    const int32 S = Seed * 17 + Boil;
    const int32 Count = FMath::Clamp(FMath::CeilToInt(Radius / (3 * Scale)) + 10, 14, 72);
    const float Start = Noise(S, 1) * PI, P1 = Noise(S, 2) * PI, P2 = Noise(S, 3) * PI;
    const float Wobble = FMath::Min(.05f, 2.2f * Scale / FMath::Max(Radius, 1.f));
    FVector2D Previous = Center;
    for (int32 I = 0; I <= Count; ++I)
    {
        const float T = I / float(Count), A = Start + T * Turns * 2 * PI;
        const float R = Radius * (1 + Wobble * (.6f * FMath::Sin(2 * A + P1) + .4f * FMath::Sin(3 * A + P2))) *
                        (1 + .03f * (T - .5f));
        const FVector2D Point = Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
        if (I)
            Line(Previous, Point, Color, Thickness);
        Previous = Point;
    }
}

void AGWHUD::Disc(FVector2D Center, float Radius, FLinearColor Color, int32 Sides)
{
    TArray<FVector2D> Points;
    for (int32 I = 0; I < Sides; ++I)
        Points.Add(Center + FVector2D(FMath::Cos(I * 2 * PI / Sides), FMath::Sin(I * 2 * PI / Sides)) * Radius);
    Fill(Points, Color);
}

void AGWHUD::Arc(FVector2D Center, float Radius, float From, float To, FLinearColor Color, float Thickness)
{
    const int32 Count = FMath::Clamp(FMath::CeilToInt(FMath::Abs(To - From) * Radius / (4 * Scale)), 4, 64);
    FVector2D Previous = Center + FVector2D(FMath::Cos(From), FMath::Sin(From)) * Radius;
    for (int32 I = 1; I <= Count; ++I)
    {
        const float A = FMath::Lerp(From, To, I / float(Count));
        const FVector2D Point = Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius;
        Line(Previous, Point, Color, Thickness);
        Previous = Point;
    }
}

void AGWHUD::Polygon(const TArray<FVector2D>& Points, FLinearColor FillColor, FLinearColor Stroke, float Thickness, int32 Seed)
{
    Fill(Points, FillColor);
    if (Thickness > 0)
        for (int32 I = 0; I < Points.Num(); ++I)
            Pen(Points[I], Points[(I + 1) % Points.Num()], Stroke, Thickness, Seed + I, .7f);
}

void AGWHUD::Meter(float X, float Y, float W, float H, float Value, FLinearColor Color, int32 Seed, bool Ticks)
{
    Value = FMath::Clamp(Value, 0.f, 1.f);
    Box(X, Y, W, H, GWPalette::PaperShade);
    if (Value > 0)
    {
        Box(X, Y, W * Value, H, Color.CopyWithNewOpacity(.88f));
        Hatch(X, Y, W * Value, H, (Color * .55f).CopyWithNewOpacity(.45f), 6 * Scale, FMath::Max(1.f, Scale), Seed);
        Line(FVector2D(X + 2 * Scale, Y + H * .3f), FVector2D(X + W * Value - 2 * Scale, Y + H * .3f), FLinearColor(1, 1, 1, .35f),
             FMath::Max(1.f, H * .14f));
    }
    if (Ticks)
        for (int32 I = 1; I < 10; ++I)
            Line(FVector2D(X + W * I / 10, Y + H * .65f), FVector2D(X + W * I / 10, Y + H), GWPalette::Ink.CopyWithNewOpacity(.35f), 1);
    const float S = 1.6f * Scale;
    Pen(FVector2D(X, Y), FVector2D(X + W, Y), GWPalette::Ink, S, Seed + 1, .6f);
    Pen(FVector2D(X + W, Y), FVector2D(X + W, Y + H), GWPalette::Ink, S, Seed + 2, .4f);
    Pen(FVector2D(X + W, Y + H), FVector2D(X, Y + H), GWPalette::Ink, S, Seed + 3, .6f);
    Pen(FVector2D(X, Y + H), FVector2D(X, Y), GWPalette::Ink, S, Seed + 4, .4f);
}

// Geometric pictograms: 0 circle, 1 square, 2 rectangle, 3 triangle, 4 hexagon, 5 star, 6 diamond, 7 plus,
// 8 flag, 9 lock, 10 gear, 11 bolt, 12 flame, 13 snowflake, 14 shield, 15 piercing arrow, 16 check,
// 17 cross, 18 crown, 19 maze, 20 cube, 21 pentagon, 22 trapezoid, 23 ink drop.
void AGWHUD::Glyph(int32 Kind, FVector2D C, float Size, FLinearColor Color, int32 Seed)
{
    const FLinearColor Inked = GWPalette::Ink;
    const float R = Size * .5f, W = FMath::Max(1.4f, Size * .06f);
    auto Ring = [&](int32 Sides, float Radius, float Spin)
    {
        TArray<FVector2D> P;
        for (int32 I = 0; I < Sides; ++I)
        {
            const float A = FMath::DegreesToRadians(Spin + I * 360.f / Sides);
            P.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
        }
        return P;
    };
    auto Star = [&](int32 Points, float Outer, float Inner, float Spin)
    {
        TArray<FVector2D> P;
        for (int32 I = 0; I < Points * 2; ++I)
        {
            const float A = FMath::DegreesToRadians(Spin + I * 180.f / Points);
            P.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * (I % 2 ? Inner : Outer));
        }
        return P;
    };
    switch (Kind)
    {
    case 0:
        Disc(C, R * .9f, Color);
        Circle(C, R * .9f, Inked, W, Seed);
        break;
    case 1:
        Polygon(Ring(4, R * 1.15f, 45), Color, Inked, W, Seed);
        break;
    case 2:
        Polygon({C + FVector2D(-R, -R * .55f), C + FVector2D(R, -R * .55f), C + FVector2D(R, R * .55f), C + FVector2D(-R, R * .55f)},
                Color, Inked, W, Seed);
        break;
    case 3:
        Polygon(Ring(3, R, -90), Color, Inked, W, Seed);
        break;
    case 4:
        Polygon(Ring(6, R, -90), Color, Inked, W, Seed);
        break;
    case 5:
    {
        // Concave outlines are filled as a fan from the centre.
        auto P = Star(5, R, R * .45f, -90);
        TArray<FVector2D> Fan = {C};
        Fan.Append(P);
        Fan.Add(P[0]);
        Fill(Fan, Color);
        for (int32 I = 0; I < P.Num(); ++I)
            Pen(P[I], P[(I + 1) % P.Num()], Inked, W, Seed + I, .5f);
        break;
    }
    case 6:
        Polygon({C + FVector2D(0, -R), C + FVector2D(R * .7f, 0), C + FVector2D(0, R), C + FVector2D(-R * .7f, 0)}, Color, Inked, W,
                Seed);
        break;
    case 7:
    {
        const float T = R * .32f;
        Box(C.X - R, C.Y - T, 2 * R, 2 * T, Color);
        Box(C.X - T, C.Y - R, 2 * T, 2 * R, Color);
        Pen(C + FVector2D(-R, 0), C + FVector2D(R, 0), Inked, W, Seed, .4f);
        Pen(C + FVector2D(0, -R), C + FVector2D(0, R), Inked, W, Seed + 1, .4f);
        break;
    }
    case 8:
        Pen(C + FVector2D(-R * .5f, R), C + FVector2D(-R * .5f, -R), Inked, W * 1.2f, Seed, .4f);
        Polygon({C + FVector2D(-R * .45f, -R), C + FVector2D(R, -R * .55f), C + FVector2D(-R * .45f, -R * .1f)}, Color, Inked, W,
                Seed + 2);
        break;
    case 9:
        Polygon({C + FVector2D(-R * .7f, -R * .05f), C + FVector2D(R * .7f, -R * .05f), C + FVector2D(R * .7f, R),
                 C + FVector2D(-R * .7f, R)},
                Color, Inked, W, Seed);
        Arc(C + FVector2D(0, -R * .05f), R * .45f, PI, 2 * PI, Inked, W * 1.4f);
        Disc(C + FVector2D(0, R * .45f), R * .14f, Inked, 10);
        break;
    case 10:
    {
        for (int32 I = 0; I < 8; ++I)
        {
            const float A = I * PI / 4;
            Pen(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R * .6f, C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R, Inked,
                W * 2.2f, Seed + I, .2f);
        }
        Disc(C, R * .68f, Color);
        Circle(C, R * .68f, Inked, W, Seed);
        Disc(C, R * .25f, GWPalette::Paper);
        Circle(C, R * .25f, Inked, W * .8f, Seed + 3);
        break;
    }
    case 11:
        Polygon({C + FVector2D(R * .15f, -R), C + FVector2D(-R * .55f, R * .1f), C + FVector2D(-R * .02f, R * .1f),
                 C + FVector2D(-R * .2f, R), C + FVector2D(R * .55f, -R * .15f), C + FVector2D(R * .02f, -R * .15f)},
                Color, Inked, W, Seed);
        break;
    case 12:
    {
        TArray<FVector2D> P;
        for (int32 I = 0; I < 14; ++I)
        {
            const float T = I / 13.f, A = PI * .5f + T * 2 * PI;
            const float Rad = R * (.55f + .45f * FMath::Pow(FMath::Abs(FMath::Sin(A * .5f - PI * .25f)), 3.f));
            P.Add(C + FVector2D(FMath::Cos(A) * .75f, -FMath::Sin(A)) * Rad + FVector2D(0, R * .2f));
        }
        P.Add(C + FVector2D(R * .1f, -R));
        TArray<FVector2D> Fan = {C + FVector2D(0, R * .25f)};
        Fan.Append(P);
        Fan.Add(P[0]);
        Fill(Fan, Color);
        for (int32 I = 0; I < P.Num(); ++I)
            Pen(P[I], P[(I + 1) % P.Num()], Inked, W, Seed + I, .3f);
        break;
    }
    case 13:
        for (int32 I = 0; I < 3; ++I)
        {
            const FVector2D D(FMath::Cos(I * PI / 3 + PI / 2), FMath::Sin(I * PI / 3 + PI / 2));
            Pen(C - D * R, C + D * R, Color, W * 2.2f, Seed + I, .3f);
            Pen(C - D * R, C + D * R, Inked, W * .8f, Seed + I + 9, .3f);
            for (float Sign : {-1.f, 1.f})
            {
                const FVector2D Tip = C + D * R * .6f * Sign;
                const FVector2D N(-D.Y, D.X);
                Pen(Tip, Tip + (D * Sign * .25f + N * .3f) * R, Inked, W, Seed + 20, .2f);
                Pen(Tip, Tip + (D * Sign * .25f - N * .3f) * R, Inked, W, Seed + 21, .2f);
            }
        }
        break;
    case 14:
        Polygon(Ring(6, R, -90), Color, Inked, W, Seed);
        Pen(C + FVector2D(-R * .4f, 0), C + FVector2D(R * .4f, 0), GWPalette::Paper, W * 2, Seed + 7, .2f);
        Pen(C + FVector2D(0, -R * .4f), C + FVector2D(0, R * .4f), GWPalette::Paper, W * 2, Seed + 8, .2f);
        break;
    case 15:
        Polygon(Ring(3, R * .55f, 0), Color, Inked, W, Seed);
        Pen(C + FVector2D(-R, R * .35f), C + FVector2D(R * .2f, -R * .1f), Inked, W * 1.3f, Seed + 4, .3f);
        Polygon({C + FVector2D(R, -R * .35f), C + FVector2D(R * .55f, -R * .45f), C + FVector2D(R * .7f, -R * .05f)}, Inked, Inked, 0,
                Seed);
        break;
    case 16:
        Pen(C + FVector2D(-R * .7f, 0), C + FVector2D(-R * .15f, R * .55f), Color, W * 2.4f, Seed, .5f);
        Pen(C + FVector2D(-R * .15f, R * .55f), C + FVector2D(R * .8f, -R * .7f), Color, W * 2.4f, Seed + 1, .5f);
        break;
    case 17:
        Pen(C + FVector2D(-R * .6f, -R * .6f), C + FVector2D(R * .6f, R * .6f), Color, W * 2.2f, Seed, .5f);
        Pen(C + FVector2D(R * .6f, -R * .6f), C + FVector2D(-R * .6f, R * .6f), Color, W * 2.2f, Seed + 1, .5f);
        break;
    case 18:
        Polygon({C + FVector2D(-R, R * .5f), C + FVector2D(-R, -R * .4f), C + FVector2D(-R * .45f, R * .05f), C + FVector2D(0, -R * .7f),
                 C + FVector2D(R * .45f, R * .05f), C + FVector2D(R, -R * .4f), C + FVector2D(R, R * .5f)},
                Color, Inked, W, Seed);
        break;
    case 19:
        Card(C.X - R, C.Y - R, 2 * R, 2 * R, Color, Inked, Seed, 0, W);
        Pen(C + FVector2D(-R * .5f, -R), C + FVector2D(-R * .5f, R * .4f), Inked, W, Seed + 3, .3f);
        Pen(C + FVector2D(-R * .5f, R * .4f), C + FVector2D(R * .2f, R * .4f), Inked, W, Seed + 4, .3f);
        Pen(C + FVector2D(R * .5f, R), C + FVector2D(R * .5f, -R * .3f), Inked, W, Seed + 5, .3f);
        Pen(C + FVector2D(R * .5f, -R * .3f), C + FVector2D(-R * .1f, -R * .3f), Inked, W, Seed + 6, .3f);
        break;
    case 20:
    {
        const FVector2D Top(0, -R), Right(R * .87f, -R * .5f), Left(-R * .87f, -R * .5f);
        Polygon({C, C + Left, C + Top, C + Right}, Color * FLinearColor(1.15f, 1.15f, 1.15f, 1), Inked, W, Seed);
        Polygon({C, C + Right, C + FVector2D(R * .87f, R * .5f), C + FVector2D(0, R)}, Color * FLinearColor(.75f, .75f, .75f, 1), Inked,
                W, Seed + 5);
        Polygon({C, C + FVector2D(0, R), C + FVector2D(-R * .87f, R * .5f), C + Left}, Color, Inked, W, Seed + 9);
        break;
    }
    case 21:
        Polygon(Ring(5, R, -90), Color, Inked, W, Seed);
        break;
    case 22:
        Polygon({C + FVector2D(-R, R * .7f), C + FVector2D(-R * .55f, -R * .7f), C + FVector2D(R * .55f, -R * .7f), C + FVector2D(R, R * .7f)},
                Color, Inked, W, Seed);
        break;
    case 23:
    {
        TArray<FVector2D> P = {C + FVector2D(0, -R)};
        for (int32 I = 0; I <= 10; ++I)
        {
            const float A = PI * (-.15f + 1.3f * I / 10.f);
            P.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R * .62f + FVector2D(0, R * .3f));
        }
        Polygon(P, Color, Inked, W, Seed);
        break;
    }
    default:
        break;
    }
}

void AGWHUD::SkillIcon(int32 Skill, FVector2D Center, float Size, int32 Seed)
{
    const FLinearColor Color = GWPalette::SkillFamily(Skill);
    if (Skill < 0)
    {
        Glyph(7, Center, Size, Color, Seed);
        return;
    }
    static const int32 Families[5] = {15, 11, 12, 13, 14};
    Disc(Center, Size * .62f, GWPalette::Paper);
    Circle(Center, Size * .62f, Color, FMath::Max(1.5f, Size * .05f), Seed + 40);
    Glyph(Families[FMath::Clamp(Skill / 4, 0, 4)], Center, Size * .7f, Color, Seed);
    // Small dots tell the four skills of a family apart at a glance.
    for (int32 I = 0; I <= Skill % 4; ++I)
        Disc(Center + FVector2D((I - (Skill % 4) * .5f) * Size * .14f, Size * .5f), Size * .045f, GWPalette::Ink, 8);
}

void AGWHUD::Keycap(const FText& KeyText, float X, float Y, float Size, FLinearColor Color)
{
    const FVector2D Text = Measure(KeyText, Size * .55f, EFace::Bold);
    const float W = FMath::Max(Size, Text.X + Size * .55f), H = Size;
    Box(X + 1.5f * Scale, Y + 2.5f * Scale, W, H, GWPalette::Ink.CopyWithNewOpacity(.35f));
    Box(X, Y, W, H, GWPalette::Paper);
    Pen(FVector2D(X, Y), FVector2D(X + W, Y), GWPalette::Ink, 1.4f * Scale, int32(X), .3f);
    Pen(FVector2D(X + W, Y), FVector2D(X + W, Y + H), GWPalette::Ink, 1.4f * Scale, int32(X) + 1, .3f);
    Pen(FVector2D(X + W, Y + H), FVector2D(X, Y + H), GWPalette::Ink, 1.8f * Scale, int32(X) + 2, .3f);
    Pen(FVector2D(X, Y + H), FVector2D(X, Y), GWPalette::Ink, 1.4f * Scale, int32(X) + 3, .3f);
    Label(KeyText, X + W * .5f, Y + H * .18f, Size * .55f, Color, true, 0, EFace::Bold);
}

void AGWHUD::Doodles(FBox2D Area, int32 Seed, float Alpha)
{
    const FLinearColor Pencil = GWPalette::Pencil.CopyWithNewOpacity(Alpha);
    for (int32 I = 0; I < 18; ++I)
    {
        const FVector2D P(FMath::Lerp(Area.Min.X, Area.Max.X, .5f + .5f * Noise(Seed, I)),
                          FMath::Lerp(Area.Min.Y, Area.Max.Y, .5f + .5f * Noise(Seed, I + 50)));
        const float S = (10 + 8 * FMath::Abs(Noise(Seed, I + 99))) * Scale;
        const int32 Kind = (I + Seed) % 5;
        if (Kind == 0)
            Circle(P, S * .5f, Pencil, 1.2f * Scale, Seed + I);
        else if (Kind == 1)
        {
            const FVector2D A = P + FVector2D(0, -S * .55f), B = P + FVector2D(S * .5f, S * .35f), C = P + FVector2D(-S * .5f, S * .35f);
            Pen(A, B, Pencil, 1.2f * Scale, Seed + I);
            Pen(B, C, Pencil, 1.2f * Scale, Seed + I + 1);
            Pen(C, A, Pencil, 1.2f * Scale, Seed + I + 2);
        }
        else if (Kind == 2)
        {
            const float H = S * .45f;
            Pen(P + FVector2D(-H, -H), P + FVector2D(H, -H), Pencil, 1.2f * Scale, Seed + I);
            Pen(P + FVector2D(H, -H), P + FVector2D(H, H), Pencil, 1.2f * Scale, Seed + I + 1);
            Pen(P + FVector2D(H, H), P + FVector2D(-H, H), Pencil, 1.2f * Scale, Seed + I + 2);
            Pen(P + FVector2D(-H, H), P + FVector2D(-H, -H), Pencil, 1.2f * Scale, Seed + I + 3);
        }
        else if (Kind == 3)
        {
            Pen(P + FVector2D(-S * .5f, 0), P + FVector2D(S * .5f, 0), Pencil, 1.2f * Scale, Seed + I);
            Pen(P + FVector2D(0, -S * .5f), P + FVector2D(0, S * .5f), Pencil, 1.2f * Scale, Seed + I + 1);
        }
        else
            Arc(P, S * .45f, 0, PI * 1.5f, Pencil, 1.2f * Scale);
    }
}

FVector2D AGWHUD::Measure(const FText& Text, float Size, EFace Face) const
{
    if (!FSlateApplication::IsInitialized())
        return FVector2D(Text.ToString().Len() * Size * .6f, Size);
    return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(
        Text, FSlateFontInfo(FaceFont(Face), FMath::Max(8, FMath::RoundToInt(Size))));
}

void AGWHUD::Label(const FText& Text, float X, float Y, float Size, FLinearColor Color, bool Center, float MaxWidth, EFace Face)
{
    if (MaxWidth > 0)
    {
        const FVector2D Extent = Measure(Text, Size, Face);
        if (Extent.X > MaxWidth)
            Size *= MaxWidth / Extent.X;
    }
    FCanvasTextItem Item(FVector2D(X, Y), Text, FSlateFontInfo(FaceFont(Face), FMath::Max(9, FMath::RoundToInt(Size))), Color);
    Item.bCentreX = Center;
    Canvas->DrawItem(Item);
}

void AGWHUD::Key(const TCHAR* Id, float X, float Y, float Size, FLinearColor Color, bool Center, float MaxWidth, EFace Face)
{
    Label(GWText::Get(Id), X, Y, Size, Color, Center, MaxWidth, Face);
}

void AGWHUD::Lettering(const FText& Text, float X, float Y, float Size, FLinearColor Color, int32 Seed)
{
    // Hand lettering: each glyph sits a little off the baseline with a soft graphite shadow.
    const FString Source = Text.ToString();
    TArray<float> Widths;
    float Total = 0;
    for (int32 I = 0; I < Source.Len(); ++I)
    {
        const float Glyph = Measure(FText::FromString(Source.Mid(I, 1)), Size, EFace::Heavy).X;
        Widths.Add(Glyph);
        Total += Glyph + Size * .02f;
    }
    float Cursor = X - Total * .5f;
    for (int32 I = 0; I < Source.Len(); ++I)
    {
        const FText Char = FText::FromString(Source.Mid(I, 1));
        const float Jitter = Noise(Seed + Boil / 2, I) * Size * .035f, Grow = 1 + Noise(Seed, I + 40) * .04f;
        const float Sz = Size * Grow;
        FCanvasTextItem Shadow(FVector2D(Cursor + 3 * Scale, Y + Jitter + 4 * Scale), Char,
                               FSlateFontInfo(FaceFont(EFace::Heavy), FMath::RoundToInt(Sz)), GWPalette::Ink.CopyWithNewOpacity(.16f));
        Canvas->DrawItem(Shadow);
        for (const FVector2D& Offset : {FVector2D(0, 0), FVector2D(.7f * Scale, .4f * Scale)})
        {
            FCanvasTextItem Item(FVector2D(Cursor, Y + Jitter) + Offset, Char, FSlateFontInfo(FaceFont(EFace::Heavy), FMath::RoundToInt(Sz)),
                                 Color);
            Canvas->DrawItem(Item);
        }
        Cursor += Widths[I] + Size * .02f;
    }
}

bool AGWHUD::Hovered(float X, float Y, float W, float H) const
{
    float MX = -1, MY = -1;
    return PlayerOwner && PlayerOwner->GetMousePosition(MX, MY) && MX >= X && MY >= Y && MX <= X + W && MY <= Y + H;
}

void AGWHUD::Button(int32 Id, const FText& Text, float X, float Y, float W, float H, bool Enabled, bool Selected,
                    FLinearColor Accent)
{
    const FLinearColor Mark = Accent.A > 0 ? Accent : GWPalette::Teal;
    const bool Hover = Enabled && Hovered(X, Y, W, H);
    const float Rise = Hover ? 2.5f * Scale : 0;
    const FLinearColor Face = !Enabled ? GWPalette::PaperShade : Selected ? Mark : GWPalette::Paper;
    Card(X, Y - Rise, W, H, Face, Enabled ? GWPalette::Ink : GWPalette::Pencil, Id * 13 + 5, Hover ? 1.6f : 1, 2 * Scale);
    if (Hover && !Selected)
        Highlight(X + W * .1f, Y - Rise + H * .22f, W * .8f, H * .56f, GWPalette::Marigold.CopyWithNewOpacity(.42f), Id);
    const float Size = FMath::Min(22 * Scale, H * .42f);
    if (!Enabled)
    {
        // The hatch marks the card as unavailable; a clear strip behind the caption keeps it readable.
        Hatch(X + 3 * Scale, Y + 3 * Scale, W - 6 * Scale, H - 6 * Scale, GWPalette::Pencil.CopyWithNewOpacity(.22f), 9 * Scale, 1,
              Id);
        const float Strip = FMath::Min(Measure(Text, Size, EFace::Bold).X, W - 26 * Scale) + 18 * Scale;
        Box(X + (W - Strip) * .5f, Y + (H - Size * 1.5f) * .5f, Strip, Size * 1.5f, GWPalette::PaperShade);
    }
    Label(Text, X + W * .5f, Y - Rise + (H - Size * 1.25f) * .5f, Size,
          !Enabled ? GWPalette::InkSoft : Selected ? GWPalette::Paper : GWPalette::Ink, true, W - 26 * Scale, EFace::Bold);
    if (Enabled)
        Hits.Add({FBox2D(FVector2D(X, Y - Rise), FVector2D(X + W, Y + H)), Id});
}

float AGWHUD::Paragraph(const FText& Text, float X, float Y, float Size, float MaxWidth, FLinearColor Color)
{
    const FString Source = Text.ToString();
    const FString CacheKey = FString::Printf(TEXT("%.1f:%.1f:"), Size, MaxWidth) + Source;
    auto* Cached = WrappedText.Find(CacheKey);
    if (!Cached)
    {
        TArray<FString> Lines;
        FString Line;
        // Breaks: Latin at spaces, CJK between characters, never before closing punctuation and
        // never between a number and its unit ("1 秒").
        auto BadBreak = [&](const FString& L, int32 I)
        {
            const TCHAR Here = L[I], Prev = L[I - 1];
            if (NoLineStart(Here))
                return true;
            if (Here == ' ' && FChar::IsDigit(Prev) && I + 1 < L.Len() && IsCJK(L[I + 1]))
                return true;
            if (Prev == ' ' && I >= 2 && FChar::IsDigit(L[I - 2]) && IsCJK(Here))
                return true;
            if (Here == ' ' && IsCJK(Prev) && I + 1 < L.Len() && FChar::IsDigit(L[I + 1]))
                return true;
            return false;
        };
        for (TCHAR Char : Source)
        {
            if (Char == '\n')
            {
                Lines.Add(Line);
                Line.Empty();
                continue;
            }
            Line.AppendChar(Char);
            if (Measure(FText::FromString(Line), Size).X > MaxWidth && Line.Len() > 1)
            {
                int32 Cut = INDEX_NONE;
                for (int32 I = Line.Len() - 1; I > 0 && Cut == INDEX_NONE; --I)
                    if ((Line[I] == ' ' || IsCJK(Line[I]) || IsCJK(Line[I - 1])) && !BadBreak(Line, I))
                        Cut = I;
                if (Cut == INDEX_NONE)
                    Cut = Line.Len() - 1;
                Lines.Add(Line.Left(Cut).TrimEnd());
                Line = Line.Mid(Cut).TrimStart();
            }
        }
        if (!Line.IsEmpty())
            Lines.Add(Line);
        if (WrappedText.Num() > 200)
            WrappedText.Reset();
        Cached = &WrappedText.Add(CacheKey, MoveTemp(Lines));
    }
    for (int32 I = 0; I < Cached->Num(); ++I)
        Label(FText::FromString((*Cached)[I]), X, Y + I * Size * 1.45f, Size, Color);
    return Cached->Num() * Size * 1.45f;
}
