#include "Engine/Texture2D.h"
#include "GWContentArt.h"
#include "UObject/Package.h"

// Procedural "pencil and marker" sprite sheets. Channel convention shared with M_GW_FXSprite / M_GW_Decal:
// R = marker fill (takes the particle colour), G = ink line, B = paper highlight, A = coverage.
namespace
{
enum EChan
{
    Fill,
    Ink,
    Light
};
struct FPainter
{
    int32 Size;
    TArray<float> Data[3];
    uint32 Seed;
    FPainter(int32 InSize, uint32 InSeed) : Size(InSize), Seed(InSeed * 2654435761u + 1u)
    {
        for (auto& Channel : Data)
            Channel.SetNumZeroed(Size * Size);
    }
    float Rand()
    {
        Seed = Seed * 1664525u + 1013904223u;
        return (Seed >> 8) * (1.f / 16777216.f);
    }
    float Rand(float A, float B)
    {
        return A + (B - A) * Rand();
    }
    static float Hash(int32 X, int32 Y)
    {
        uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u;
        H = (H ^ (H >> 13)) * 1274126177u;
        return ((H ^ (H >> 16)) & 0xffffff) / 16777215.f;
    }
    void Put(EChan C, int32 X, int32 Y, float V)
    {
        if (X < 0 || Y < 0 || X >= Size || Y >= Size)
            return;
        float& P = Data[C][Y * Size + X];
        P = FMath::Max(P, FMath::Clamp(V, 0.f, 1.f));
    }
    // Round-capped segment whose width varies from W0 to W1; Grain gives the pencil "tooth".
    void Segment(EChan C, FVector2f A, FVector2f B, float W0, float W1, float Strength, float Grain)
    {
        const float Pad = FMath::Max(W0, W1) * .5f + 2;
        const int32 X0 = FMath::FloorToInt(FMath::Min(A.X, B.X) - Pad), X1 = FMath::CeilToInt(FMath::Max(A.X, B.X) + Pad);
        const int32 Y0 = FMath::FloorToInt(FMath::Min(A.Y, B.Y) - Pad), Y1 = FMath::CeilToInt(FMath::Max(A.Y, B.Y) + Pad);
        const FVector2f D = B - A;
        const float L2 = D.SizeSquared();
        for (int32 Y = FMath::Max(0, Y0); Y <= FMath::Min(Size - 1, Y1); ++Y)
            for (int32 X = FMath::Max(0, X0); X <= FMath::Min(Size - 1, X1); ++X)
            {
                const FVector2f P(X + .5f, Y + .5f);
                const float T = L2 > 1e-6f ? FMath::Clamp(FVector2f::DotProduct(P - A, D) / L2, 0.f, 1.f) : 0.f;
                const float Half = FMath::Lerp(W0, W1, T) * .5f;
                const float Cover = FMath::Clamp(Half - FVector2f::Distance(P, A + D * T) + .5f, 0.f, 1.f);
                if (Cover > 0)
                    Put(C, X, Y, Cover * Strength * (1 - Grain * Hash(X, Y)));
            }
    }
    void Polyline(EChan C, const TArray<FVector2f>& P, float W, float Strength, float Grain, bool Closed, float Taper)
    {
        const int32 Segments = Closed ? P.Num() : P.Num() - 1;
        auto Width = [&](float T) { return W * (1 - Taper + Taper * FMath::Sin(PI * FMath::Clamp(T, 0.f, 1.f))); };
        for (int32 I = 0; I < Segments; ++I)
            Segment(C, P[I], P[(I + 1) % P.Num()], Width(I / float(Segments)), Width((I + 1) / float(Segments)), Strength,
                    Grain);
    }
    static bool Inside(const TArray<FVector2f>& Poly, FVector2f P)
    {
        bool In = false;
        for (int32 I = 0, J = Poly.Num() - 1; I < Poly.Num(); J = I++)
            if ((Poly[I].Y > P.Y) != (Poly[J].Y > P.Y) &&
                P.X < (Poly[J].X - Poly[I].X) * (P.Y - Poly[I].Y) / (Poly[J].Y - Poly[I].Y) + Poly[I].X)
                In = !In;
        return In;
    }
    void FillPolygon(EChan C, const TArray<FVector2f>& Poly, float Strength)
    {
        FBox2f Box(Poly);
        for (int32 Y = FMath::Max(0, FMath::FloorToInt(Box.Min.Y)); Y <= FMath::Min(Size - 1, FMath::CeilToInt(Box.Max.Y)); ++Y)
            for (int32 X = FMath::Max(0, FMath::FloorToInt(Box.Min.X)); X <= FMath::Min(Size - 1, FMath::CeilToInt(Box.Max.X));
                 ++X)
            {
                int32 Hits = 0;
                for (int32 S = 0; S < 16; ++S)
                    Hits += Inside(Poly, FVector2f(X + (S % 4 + .5f) / 4, Y + (S / 4 + .5f) / 4));
                if (Hits)
                    Put(C, X, Y, Hits / 16.f * Strength);
            }
    }
    void Disc(EChan C, FVector2f Center, float Radius, float Strength, float Soft = 1)
    {
        for (int32 Y = FMath::Max(0, FMath::FloorToInt(Center.Y - Radius - Soft - 1));
             Y <= FMath::Min(Size - 1, FMath::CeilToInt(Center.Y + Radius + Soft + 1)); ++Y)
            for (int32 X = FMath::Max(0, FMath::FloorToInt(Center.X - Radius - Soft - 1));
                 X <= FMath::Min(Size - 1, FMath::CeilToInt(Center.X + Radius + Soft + 1)); ++X)
            {
                const float D = FVector2f::Distance(FVector2f(X + .5f, Y + .5f), Center);
                Put(C, X, Y, FMath::Clamp((Radius - D) / Soft + .5f, 0.f, 1.f) * Strength);
            }
    }
    void Hatch(EChan C, const TArray<FVector2f>& Poly, float Spacing, float AngleDeg, float Width, float Strength)
    {
        const FVector2f N(FMath::Cos(FMath::DegreesToRadians(AngleDeg)), FMath::Sin(FMath::DegreesToRadians(AngleDeg)));
        FBox2f Box(Poly);
        for (int32 Y = FMath::Max(0, FMath::FloorToInt(Box.Min.Y)); Y <= FMath::Min(Size - 1, FMath::CeilToInt(Box.Max.Y)); ++Y)
            for (int32 X = FMath::Max(0, FMath::FloorToInt(Box.Min.X)); X <= FMath::Min(Size - 1, FMath::CeilToInt(Box.Max.X));
                 ++X)
            {
                const FVector2f P(X + .5f, Y + .5f);
                if (!Inside(Poly, P))
                    continue;
                const float S = FVector2f::DotProduct(P, N) / Spacing;
                const float D = FMath::Min(FMath::Frac(S), 1 - FMath::Frac(S)) * Spacing;
                Put(C, X, Y, FMath::Clamp(Width * .5f - D + .5f, 0.f, 1.f) * Strength * (1 - .3f * Hash(X, Y)));
            }
    }
    // Ink only where no marker fill is present: a coloured stroke with a dark pencil border.
    void Knockout()
    {
        for (int32 I = 0; I < Data[Ink].Num(); ++I)
            Data[Ink][I] *= 1 - Data[Fill][I];
    }
    TArray<FColor> Pixels() const
    {
        TArray<FColor> Out;
        Out.SetNumUninitialized(Size * Size);
        for (int32 I = 0; I < Out.Num(); ++I)
        {
            const float A = FMath::Max3(Data[Fill][I], Data[Ink][I], Data[Light][I] * .8f);
            Out[I] = FColor(FMath::RoundToInt(Data[Fill][I] * 255), FMath::RoundToInt(Data[Ink][I] * 255),
                            FMath::RoundToInt(Data[Light][I] * 255), FMath::RoundToInt(A * 255));
        }
        return Out;
    }
    // Hand drawn circle: wobbling radius, loose start/end overlap and slight drift so it never closes perfectly.
    TArray<FVector2f> Circle(FVector2f C, float R, float Wobble, float Turns, int32 N, float Drift)
    {
        TArray<FVector2f> Out;
        const float Start = Rand(0, 2 * PI), P1 = Rand(0, 2 * PI), P2 = Rand(0, 2 * PI);
        for (int32 I = 0; I <= N; ++I)
        {
            const float T = I / float(N), A = Start + T * Turns * 2 * PI;
            const float Rad = R * (1 + Wobble * (.6f * FMath::Sin(2 * A + P1) + .4f * FMath::Sin(3 * A + P2))) *
                              (1 + Drift * (T - .5f));
            Out.Add(C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * Rad);
        }
        return Out;
    }
    TArray<FVector2f> Bowed(FVector2f A, FVector2f B, float Bow, float Overshoot, int32 N = 8)
    {
        const FVector2f D = B - A;
        const float Len = D.Size();
        const FVector2f Dir = Len > 0 ? D / Len : FVector2f(1, 0), Perp(-Dir.Y, Dir.X);
        const FVector2f S = A - Dir * Overshoot * Rand(.2f, 1), E = B + Dir * Overshoot * Rand(.2f, 1);
        const float Amount = Bow * Len * Rand(-1, 1);
        TArray<FVector2f> Out;
        for (int32 I = 0; I <= N; ++I)
        {
            const float T = I / float(N);
            Out.Add(FMath::Lerp(S, E, T) + Perp * FMath::Sin(PI * T) * Amount);
        }
        return Out;
    }
    void SketchPolygon(EChan C, const TArray<FVector2f>& Poly, float W, float Strength, float Bow = .025f, float Over = 3)
    {
        for (int32 I = 0; I < Poly.Num(); ++I)
            Polyline(C, Bowed(Poly[I], Poly[(I + 1) % Poly.Num()], Bow, Over), W, Strength, .25f, false, .35f);
    }
};
FVector2f Rotate(FVector2f V, float Degrees)
{
    const float S = FMath::Sin(FMath::DegreesToRadians(Degrees)), C = FMath::Cos(FMath::DegreesToRadians(Degrees));
    return FVector2f(V.X * C - V.Y * S, V.X * S + V.Y * C);
}
TArray<FVector2f> Star(FPainter& P, FVector2f C, int32 Points, float Outer, float Inner, float Jitter, float Spin)
{
    TArray<FVector2f> Out;
    for (int32 I = 0; I < Points * 2; ++I)
    {
        const float R = (I % 2 ? Inner : Outer) * (1 + P.Rand(-Jitter, Jitter));
        const float A = FMath::DegreesToRadians(Spin + I * 180.f / Points);
        Out.Add(C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R);
    }
    return Out;
}
TArray<FVector2f> Regular(FVector2f C, int32 Sides, float R, float Spin)
{
    TArray<FVector2f> Out;
    for (int32 I = 0; I < Sides; ++I)
    {
        const float A = FMath::DegreesToRadians(Spin + I * 360.f / Sides);
        Out.Add(C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R);
    }
    return Out;
}
TArray<FVector2f> CatmullRom(const TArray<FVector2f>& Control, int32 Steps)
{
    TArray<FVector2f> Out;
    const int32 N = Control.Num();
    for (int32 I = 0; I < N; ++I)
        for (int32 S = 0; S < Steps; ++S)
        {
            const float T = S / float(Steps), T2 = T * T, T3 = T2 * T;
            const FVector2f P0 = Control[(I + N - 1) % N], P1 = Control[I], P2 = Control[(I + 1) % N],
                            P3 = Control[(I + 2) % N];
            Out.Add(.5f * ((2 * P1) + (-P0 + P2) * T + (2 * P0 - 5 * P1 + 4 * P2 - P3) * T2 + (-P0 + 3 * P1 - 3 * P2 + P3) * T3));
        }
    return Out;
}

UTexture2D* Save(const FString& Name, int32 Size, const TArray<FColor>& Pixels, bool Tile)
{
    UPackage* Package = GWContent::NewAssetPackage(TEXT("/Game/Sketch/Textures/") + Name);
    UTexture2D* Texture = NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone);
    Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
    Texture->SRGB = false;
    Texture->CompressionSettings = TC_BC7;
    Texture->LODGroup = TEXTUREGROUP_Effects;
    Texture->MipGenSettings = TMGS_FromTextureGroup;
    Texture->AddressX = Texture->AddressY = Tile ? TA_Wrap : TA_Clamp;
    Texture->PostEditChange();
    GWContent::SaveAsset(Texture);
    return Texture;
}
} // namespace

void GWContent::BuildTextures(TMap<FName, UTexture2D*>& Out)
{
    auto Add = [&](const TCHAR* Name, int32 Size, uint32 Seed, TFunctionRef<void(FPainter&, float, FVector2f)> Draw,
                   bool Tile = false)
    {
        FPainter P(Size, Seed);
        Draw(P, static_cast<float>(Size), FVector2f(Size * .5f, Size * .5f));
        Out.Add(Name, Save(Name, Size, P.Pixels(), Tile));
    };
    Add(TEXT("T_GW_Ring"), 256, 11,
        [](FPainter& P, float S, FVector2f C)
        {
            // A solid marker band between two ink rims: it must read as a shockwave at a glance.
            P.Polyline(Fill, P.Circle(C, .375f * S, .012f, 1, 120, 0), .15f * S, .92f, 0, true, 0);
            P.Polyline(Ink, P.Circle(C, .445f * S, .02f, 1.08f, 140, .03f), 7.5f, 1, .2f, false, .5f);
            P.Polyline(Ink, P.Circle(C, .452f * S, .025f, .7f, 100, .02f), 3, .6f, .3f, false, .6f);
            P.Polyline(Ink, P.Circle(C, .305f * S, .02f, 1.06f, 120, .03f), 5.5f, .95f, .2f, false, .5f);
        });
    Add(TEXT("T_GW_Loop"), 256, 12,
        [](FPainter& P, float S, FVector2f C)
        {
            const auto Path = P.Circle(C, .4f * S, .025f, 1.12f, 150, .04f);
            P.Polyline(Ink, Path, 24, 1, 0, false, .55f);
            P.Polyline(Fill, Path, 15, 1, 0, false, .55f);
            P.Knockout();
            P.Polyline(Light, Path, 4, .55f, .2f, false, .6f);
        });
    Add(TEXT("T_GW_Star"), 128, 13,
        [](FPainter& P, float S, FVector2f C)
        {
            const auto Shape = Star(P, C, 5, .44f * S, .19f * S, .07f, -90);
            P.FillPolygon(Fill, Shape, 1);
            P.SketchPolygon(Ink, Shape, 3.6f, 1);
            P.Segment(Light, C + FVector2f(-.14f, -.04f) * S, C + FVector2f(-.05f, -.16f) * S, 4, 2, .9f, 0);
        });
    Add(TEXT("T_GW_Burst"), 256, 14,
        [](FPainter& P, float S, FVector2f C)
        {
            TArray<FVector2f> Shape;
            for (int32 I = 0; I < 28; ++I)
            {
                const float A = I * 2 * PI / 28, R = (I % 2 ? P.Rand(.18f, .24f) : P.Rand(.34f, .47f)) * S;
                Shape.Add(C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R);
            }
            P.FillPolygon(Fill, Shape, 1);
            P.SketchPolygon(Ink, Shape, 4.5f, 1, .02f, 2);
            P.Disc(Light, C, .1f * S, .8f, 3);
        });
    Add(TEXT("T_GW_Spark"), 128, 15,
        [](FPainter& P, float S, FVector2f C)
        {
            const auto Shape = Star(P, C, 4, .46f * S, .09f * S, .05f, -90);
            P.FillPolygon(Fill, Shape, 1);
            P.SketchPolygon(Ink, Shape, 2.6f, 1, .02f, 2);
        });
    Add(TEXT("T_GW_Stroke"), 128, 16,
        [](FPainter& P, float S, FVector2f C)
        {
            const auto Path = P.Bowed(FVector2f(C.X, .07f * S), FVector2f(C.X, .93f * S), .06f, 0, 14);
            P.Polyline(Ink, Path, 46, 1, 0, false, .9f);
            P.Polyline(Fill, Path, 28, 1, 0, false, .9f);
            P.Knockout();
        });
    Add(TEXT("T_GW_Puff"), 128, 17,
        [](FPainter& P, float S, FVector2f C)
        {
            TArray<FVector3f> Circles = {FVector3f(C.X, C.Y + .04f * S, .22f * S)};
            for (int32 I = 0; I < 6; ++I)
            {
                const float A = I * PI / 3 + P.Rand(-.2f, .2f), D = P.Rand(.16f, .2f) * S;
                Circles.Add(FVector3f(C.X + FMath::Cos(A) * D, C.Y + FMath::Sin(A) * D, P.Rand(.12f, .17f) * S));
            }
            for (const auto& Circle : Circles)
                P.Disc(Fill, FVector2f(Circle.X, Circle.Y), Circle.Z, .92f);
            for (int32 I = 0; I < Circles.Num(); ++I)
            {
                TArray<FVector2f> Run;
                auto Flush = [&]()
                {
                    if (Run.Num() > 1)
                        P.Polyline(Ink, Run, 3.2f, 1, .3f, false, .3f);
                    Run.Reset();
                };
                for (int32 K = 0; K <= 72; ++K)
                {
                    const float A = K * 2 * PI / 72;
                    const FVector2f Pt =
                        FVector2f(Circles[I].X, Circles[I].Y) + FVector2f(FMath::Cos(A), FMath::Sin(A)) * Circles[I].Z;
                    bool Covered = false;
                    for (int32 J = 0; J < Circles.Num() && !Covered; ++J)
                        Covered = J != I && FVector2f::Distance(Pt, FVector2f(Circles[J].X, Circles[J].Y)) < Circles[J].Z - 1;
                    if (Covered)
                        Flush();
                    else
                        Run.Add(Pt);
                }
                Flush();
            }
            P.Polyline(Ink, P.Circle(C + FVector2f(.05f, .06f) * S, .07f * S, .05f, .55f, 18, 0), 2.4f, .7f, .3f, false, .5f);
            P.Disc(Light, C + FVector2f(-.11f, -.1f) * S, .06f * S, .75f, 2);
        });
    Add(TEXT("T_GW_Dot"), 64, 18,
        [](FPainter& P, float S, FVector2f C)
        {
            P.Disc(Fill, C, .4f * S, 1);
            P.Polyline(Ink, P.Circle(C, .4f * S, .02f, 1.05f, 48, .02f), 3, 1, .2f, false, .3f);
        });
    Add(TEXT("T_GW_Flame"), 128, 19,
        [](FPainter& P, float S, FVector2f C)
        {
            const TArray<FVector2f> Control = {{.53f, .05f}, {.70f, .33f}, {.77f, .58f}, {.70f, .80f}, {.54f, .92f},
                                               {.40f, .91f}, {.27f, .80f}, {.24f, .60f}, {.33f, .40f}, {.44f, .27f}};
            TArray<FVector2f> Scaled;
            for (auto V : Control)
                Scaled.Add(V * S);
            const auto Shape = CatmullRom(Scaled, 6);
            P.FillPolygon(Fill, Shape, 1);
            P.Polyline(Ink, Shape, 3.6f, 1, .25f, true, 0);
            TArray<FVector2f> Core;
            for (auto V : Shape)
                Core.Add(FVector2f(.5f, .8f) * S + (V - FVector2f(.5f, .8f) * S) * .48f);
            P.FillPolygon(Light, Core, 1);
        });
    Add(TEXT("T_GW_Snow"), 128, 20,
        [](FPainter& P, float S, FVector2f C)
        {
            for (int32 Pass = 0; Pass < 2; ++Pass)
            {
                const EChan Chan = Pass ? Fill : Ink;
                const float Scale = Pass ? .5f : 1.f;
                for (int32 K = 0; K < 6; ++K)
                {
                    const FVector2f Dir = Rotate(FVector2f(0, -1), K * 60.f);
                    P.Segment(Chan, C, C + Dir * .42f * S, 10 * Scale, 7 * Scale, 1, 0);
                    for (float T : {.5f, .74f})
                    {
                        const FVector2f Base = C + Dir * T * .42f * S;
                        P.Segment(Chan, Base, Base + Rotate(Dir, 52) * .13f * S, 7 * Scale, 5 * Scale, 1, 0);
                        P.Segment(Chan, Base, Base + Rotate(Dir, -52) * .13f * S, 7 * Scale, 5 * Scale, 1, 0);
                    }
                }
            }
            P.Knockout();
            P.Disc(Light, C, .06f * S, 1);
        });
    Add(TEXT("T_GW_Hex"), 128, 21,
        [](FPainter& P, float S, FVector2f C)
        {
            const auto Shape = Regular(C, 6, .4f * S, -90);
            P.Polyline(Fill, Shape, .09f * S, .7f, 0, true, 0);
            TArray<FVector2f> Outer, Inner;
            for (auto V : Shape)
            {
                Outer.Add(C + (V - C) * 1.11f);
                Inner.Add(C + (V - C) * .89f);
            }
            P.SketchPolygon(Ink, Outer, 3.2f, 1);
            P.SketchPolygon(Ink, Inner, 2.4f, .8f);
        });
    Add(TEXT("T_GW_Plus"), 64, 22,
        [](FPainter& P, float S, FVector2f C)
        {
            const float W = .15f * S, L = .42f * S;
            const TArray<FVector2f> Shape = {C + FVector2f(-W, -L), C + FVector2f(W, -L), C + FVector2f(W, -W),
                                             C + FVector2f(L, -W),  C + FVector2f(L, W),  C + FVector2f(W, W),
                                             C + FVector2f(W, L),   C + FVector2f(-W, L), C + FVector2f(-W, W),
                                             C + FVector2f(-L, W),  C + FVector2f(-L, -W), C + FVector2f(-W, -W)};
            P.FillPolygon(Fill, Shape, 1);
            P.SketchPolygon(Ink, Shape, 3, 1, .02f, 1.5f);
        });
    Add(TEXT("T_GW_Square"), 128, 23,
        [](FPainter& P, float S, FVector2f C)
        {
            const auto Shape = Regular(C, 4, .44f * S, 49);
            P.FillPolygon(Fill, Shape, .35f);
            P.Hatch(Fill, Shape, 9, 45, 2.6f, .9f);
            P.SketchPolygon(Ink, Shape, 4, 1);
        });
    Add(TEXT("T_GW_Triangle"), 128, 24,
        [](FPainter& P, float S, FVector2f C)
        {
            const auto Shape = Regular(C + FVector2f(0, .05f * S), 3, .43f * S, -90);
            P.FillPolygon(Fill, Shape, .35f);
            P.Hatch(Fill, Shape, 9, -35, 2.6f, .9f);
            P.SketchPolygon(Ink, Shape, 4, 1);
        });
    Add(TEXT("T_GW_Bolt"), 256, 25,
        [](FPainter& P, float S, FVector2f)
        {
            for (int32 Cell = 0; Cell < 4; ++Cell)
            {
                const FVector2f Origin((Cell % 2) * 128.f, (Cell / 2) * 128.f);
                TArray<FVector2f> Path;
                for (int32 I = 0; I <= 8; ++I)
                    Path.Add(Origin + FVector2f(64 + (I == 0 || I == 8 ? 0 : P.Rand(8, 24) * (I % 2 ? 1 : -1)), 5 + I * 14.75f));
                const FVector2f Fork = Path[3];
                const TArray<FVector2f> Branch = {Fork, Fork + FVector2f(P.Rand(-26, 26), 18), Fork + FVector2f(P.Rand(-34, 34), 34)};
                P.Polyline(Ink, Path, 22, 1, 0, false, .2f);
                P.Polyline(Ink, Branch, 12, 1, 0, false, .5f);
                P.Polyline(Fill, Path, 12, 1, 0, false, .2f);
                P.Polyline(Fill, Branch, 6.5f, 1, 0, false, .5f);
                P.Polyline(Light, Path, 4, 1, 0, false, .2f);
            }
            P.Knockout();
        });
    Add(TEXT("T_GW_Diamond"), 128, 26,
        [](FPainter& P, float S, FVector2f)
        {
            const TArray<FVector2f> Shape = {FVector2f(.5f, .05f) * S, FVector2f(.83f, .42f) * S, FVector2f(.5f, .95f) * S,
                                             FVector2f(.17f, .42f) * S};
            P.FillPolygon(Fill, Shape, 1);
            P.SketchPolygon(Ink, Shape, 3.6f, 1, .015f, 2);
            P.Segment(Ink, FVector2f(.19f, .42f) * S, FVector2f(.81f, .42f) * S, 2, 2, .6f, .2f);
            P.Segment(Ink, FVector2f(.5f, .07f) * S, FVector2f(.5f, .92f) * S, 1.8f, 1.8f, .45f, .2f);
            P.FillPolygon(Light, {FVector2f(.5f, .12f) * S, FVector2f(.29f, .4f) * S, FVector2f(.48f, .4f) * S}, .85f);
        });
    Add(TEXT("T_GW_Scribble"), 128, 27,
        [](FPainter& P, float S, FVector2f C)
        {
            P.Disc(Fill, C, .3f * S, .35f, 4);
            TArray<FVector2f> Path;
            for (int32 I = 0; I <= 260; ++I)
            {
                const float T = I / 260.f * 6 * PI;
                const float R = (.2f + .1f * FMath::Sin(1.7f * T + .4f)) * S;
                Path.Add(C + FVector2f(FMath::Cos(T * 1.31f), FMath::Sin(T * 1.31f)) * R +
                         FVector2f(FMath::Sin(T * .23f), FMath::Cos(T * .19f)) * .06f * S);
            }
            P.Polyline(Ink, Path, 2.6f, .85f, .35f, false, .3f);
        });
    Add(TEXT("T_GW_Shard"), 128, 28,
        [](FPainter& P, float S, FVector2f C)
        {
            const TArray<FVector2f> Shape = {C + FVector2f(-.38f, .3f) * S, C + FVector2f(.06f, -.42f) * S,
                                             C + FVector2f(.4f, .2f) * S, C + FVector2f(.1f, .36f) * S};
            P.FillPolygon(Fill, Shape, 1);
            P.SketchPolygon(Ink, Shape, 3.4f, 1, .02f, 2);
            P.Segment(Light, C + FVector2f(-.18f, .14f) * S, C + FVector2f(.02f, -.24f) * S, 3, 1.5f, .8f, 0);
        });
    Add(TEXT("T_GW_Spiral"), 128, 29,
        [](FPainter& P, float S, FVector2f C)
        {
            TArray<FVector2f> Path;
            for (int32 I = 0; I <= 160; ++I)
            {
                const float T = I / 160.f, A = T * 3.2f * 2 * PI;
                Path.Add(C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * (.04f + .4f * T) * S);
            }
            P.Polyline(Ink, Path, 11, 1, 0, false, .6f);
            P.Polyline(Fill, Path, 6, 1, 0, false, .6f);
            P.Knockout();
        });
    Add(TEXT("T_GW_Crack"), 256, 30,
        [](FPainter& P, float S, FVector2f C)
        {
            for (int32 K = 0; K < 8; ++K)
            {
                float A = K * 2 * PI / 8 + P.Rand(-.3f, .3f);
                FVector2f Pt = C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * .05f * S;
                const float Len = P.Rand(.3f, .46f) * S;
                const int32 Steps = 6;
                for (int32 I = 0; I < Steps; ++I)
                {
                    A += P.Rand(-.45f, .45f);
                    const FVector2f Next = Pt + FVector2f(FMath::Cos(A), FMath::Sin(A)) * (Len / Steps);
                    const float W = FMath::Lerp(5.f, 1.2f, I / float(Steps));
                    P.Segment(Ink, Pt, Next, W, W * .8f, 1, .3f);
                    if (I == 2 || I == 4)
                    {
                        const float B = A + P.Rand(.5f, 1.f) * (P.Rand() < .5f ? -1 : 1);
                        P.Segment(Ink, Next, Next + FVector2f(FMath::Cos(B), FMath::Sin(B)) * Len * .22f, W * .7f, 1, .9f, .3f);
                    }
                    Pt = Next;
                }
            }
            P.Disc(Fill, C, .09f * S, .8f, 6);
        });
    Add(TEXT("T_GW_Glow"), 128, 31,
        [](FPainter& P, float S, FVector2f C)
        {
            for (int32 Y = 0; Y < P.Size; ++Y)
                for (int32 X = 0; X < P.Size; ++X)
                {
                    const float D = FVector2f::Distance(FVector2f(X + .5f, Y + .5f), C) / (.5f * S);
                    const float V = FMath::Square(FMath::Clamp(1 - D, 0.f, 1.f));
                    P.Put(Fill, X, Y, V * .85f);
                    P.Put(Light, X, Y, FMath::Square(V) * .7f);
                }
        });
    Add(
        TEXT("T_GW_Hatch"), 128, 32,
        [](FPainter& P, float, FVector2f)
        {
            for (int32 Y = 0; Y < P.Size; ++Y)
                for (int32 X = 0; X < P.Size; ++X)
                {
                    const float S = (X + Y + 1) % 16;
                    const float D = FMath::Min(S, 16 - S);
                    P.Put(Ink, X, Y, FMath::Clamp(1.6f - D + .5f, 0.f, 1.f) * (1 - .35f * FPainter::Hash(X, Y)));
                }
        },
        true);
    Add(
        TEXT("T_GW_PaperGrain"), 256, 33,
        [](FPainter& P, float S, FVector2f)
        {
            // Tileable value noise: lattice wraps at the texture edge so the UI paper repeats seamlessly.
            auto Lattice = [&](int32 X, int32 Y, int32 Period)
            { return FPainter::Hash((X % Period + Period) % Period, (Y % Period + Period) % Period + Period * 977); };
            auto Noise = [&](float X, float Y, int32 Period)
            {
                const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
                const float FX = X - X0, FY = Y - Y0, UX = FX * FX * (3 - 2 * FX), UY = FY * FY * (3 - 2 * FY);
                return FMath::Lerp(FMath::Lerp(Lattice(X0, Y0, Period), Lattice(X0 + 1, Y0, Period), UX),
                                   FMath::Lerp(Lattice(X0, Y0 + 1, Period), Lattice(X0 + 1, Y0 + 1, Period), UX), UY);
            };
            for (int32 Y = 0; Y < P.Size; ++Y)
                for (int32 X = 0; X < P.Size; ++X)
                {
                    const float U = X / S, V = Y / S;
                    const float N = .45f * Noise(U * 8, V * 8, 8) + .3f * Noise(U * 32, V * 32, 32) +
                                    .25f * Noise(U * 128, V * 128, 128);
                    const float Fiber = FPainter::Hash(X, Y * 3) > .985f ? .08f : 0.f;
                    const float G = FMath::Clamp(.86f + .14f * N - Fiber, 0.f, 1.f);
                    P.Data[Fill][Y * P.Size + X] = G;
                    P.Data[Ink][Y * P.Size + X] = G;
                    P.Data[Light][Y * P.Size + X] = G;
                }
        },
        true);
    UE_LOG(LogGWContent, Display, TEXT("TEXTURES=%d"), Out.Num());
}
