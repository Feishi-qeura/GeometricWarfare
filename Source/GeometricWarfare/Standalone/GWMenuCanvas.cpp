#include "GWHUD.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "GWPalette.h"

// The title page's animated background: a top-down brawl on graph paper drawn with the same cut-paper
// pens as the HUD. Coral players and their companions hold off magenta monsters and one elite block.
using namespace GWPalette;
namespace
{
FVector2D V(gwgame::Vec2 P)
{
    return {P.x, P.y};
}
} // namespace

void AGWHUD::FlatLine(FVector2D A, FVector2D B, FLinearColor Color, float Thickness)
{
    FCanvasLineItem Item(A, B);
    Item.SetColor(Color);
    Item.LineThickness = Thickness;
    Canvas->DrawItem(Item);
}

void AGWHUD::FlatBody(uint8 Shape, FVector2D P, float R, float Angle, FLinearColor Color, bool Monster, int32 Seed)
{
    const float S = R / 22;
    const float Stroke = FMath::Clamp(R * .12f, 1.6f, 3.4f);
    const FVector2D Drop = FVector2D(.14f, .22f) * R;
    const FLinearColor Body = FMath::Lerp(Paper, Color, .86f);
    const FLinearColor Shine = FMath::Lerp(Color, FLinearColor::White, .55f).CopyWithNewOpacity(.4f);
    if (Shape == 0)
    {
        Disc(P + Drop, R, Ink.CopyWithNewOpacity(.18f), 20);
        Disc(P, R, Body, 24);
        Disc(P + FVector2D(-.3f, -.32f) * R, R * .36f, Shine, 14);
        Circle(P, R, Ink, Stroke, Seed);
    }
    else
    {
        const TArray<FVector2D> Local = Shape == 1   ? TArray<FVector2D>{{-21, -21}, {21, -21}, {21, 21}, {-21, 21}}
                                        : Shape == 2 ? TArray<FVector2D>{{-30, -15}, {30, -15}, {30, 15}, {-30, 15}}
                                                     : TArray<FVector2D>{{0, -29}, {25.1147367, 14.5}, {-25.1147367, 14.5}};
        const float Cos = FMath::Cos(Angle), Sin = FMath::Sin(Angle);
        TArray<FVector2D> Points, Shadow, Inset;
        for (const FVector2D& L : Local)
        {
            const FVector2D Q = P + FVector2D(L.X * Cos - L.Y * Sin, L.X * Sin + L.Y * Cos) * S;
            Points.Add(Q);
            Shadow.Add(Q + Drop);
            Inset.Add(P + (Q - P) * .46f + FVector2D(-.16f, -.2f) * R);
        }
        Fill(Shadow, Ink.CopyWithNewOpacity(.18f));
        Fill(Points, Body);
        Fill(Inset, Shine);
        for (int32 I = 0; I < Points.Num(); ++I)
            Pen(Points[I], Points[(I + 1) % Points.Num()], Ink, Stroke, Seed + I, .6f);
    }
    if (Monster)
    {
        // Two ink eyes give every monster a facing, as in the 3D arena.
        const FVector2D Aim(FMath::Cos(Angle), FMath::Sin(Angle)), Side(-Aim.Y, Aim.X);
        for (float Sign : {-1.f, 1.f})
        {
            const FVector2D Eye = P + Aim * R * .45f + Side * Sign * R * .32f;
            Disc(Eye, R * .2f, Paper, 10);
            Disc(Eye + Aim * R * .06f, R * .09f, Ink, 8);
        }
    }
}

void AGWHUD::FlatWeapon(uint8 K, FVector2D P, float A, float S)
{
    const FVector2D Aim(FMath::Cos(A), FMath::Sin(A)), Side(-Aim.Y, Aim.X);
    const float Barrel = K == 0 ? 44 : K == 3 ? 76 : K == 4 ? 65 : 56;
    FlatLine(P + Aim * 17 * S, P + Aim * Barrel * S, Ink, FMath::Max(1.5f, (K == 5 ? 12 : 5) * S));
    if (K == 2)
        FlatLine(P + Aim * 20 * S + Side * 4 * S, P + Aim * Barrel * S + Side * 4 * S, Ink, FMath::Max(1.f, 2 * S));
    if (K == 1)
        FlatLine(P + Aim * 28 * S, P + Aim * 23 * S + Side * 9 * S, Ink, FMath::Max(1.5f, 4 * S));
    if (K == 3)
    {
        FlatLine(P + Aim * 32 * S - Side * 7 * S, P + Aim * 48 * S - Side * 7 * S, Ink, FMath::Max(1.5f, 5 * S));
        FlatLine(P + Aim * 68 * S - Side * 4 * S, P + Aim * 68 * S + Side * 4 * S, Ink, 2);
    }
    if (K == 4)
    {
        FlatLine(P + Aim * 52 * S - Side * 4 * S, P + Aim * Barrel * S - Side * 4 * S, Ink, FMath::Max(1.f, 2 * S));
        FlatLine(P + Aim * 21 * S + Side * 8 * S, P + Aim * 46 * S + Side * 8 * S, Ink, FMath::Max(1.5f, 7 * S));
    }
    if (K == 5)
        FlatLine(P + Aim * Barrel * S - Side * 9 * S, P + Aim * Barrel * S + Side * 9 * S, Ink, FMath::Max(1.5f, 4 * S));
    // Marker tip on the muzzle.
    Disc(P + Aim * Barrel * S, FMath::Max(1.5f, 3.2f * S), Marigold, 8);
}

void AGWHUD::DrawPaperGround(const gwgame::FlatView& View)
{
    // An endless graph-paper page anchored to the world: 100-unit squares and a stronger rule every 400.
    Box(0, 0, Width, Height, Paper);
    const auto WLo = View.world({0, 0}), WHi = View.world({Width, Height});
    for (int64 G = FMath::CeilToInt64(WLo.x / 100); G * 100 <= WHi.x; ++G)
    {
        const float SX = View.screen({G * 100., 0}).x;
        Line(FVector2D(SX, 0), FVector2D(SX, Height), Grid.CopyWithNewOpacity(G % 4 ? .28f : .6f), G % 4 ? 1.f : 1.5f);
    }
    for (int64 G = FMath::CeilToInt64(WLo.y / 100); G * 100 <= WHi.y; ++G)
    {
        const float SY = View.screen({0, G * 100.}).y;
        Line(FVector2D(0, SY), FVector2D(Width, SY), Grid.CopyWithNewOpacity(G % 4 ? .28f : .6f), G % 4 ? 1.f : 1.5f);
    }
    if (Grain && Grain->GetResource())
    {
        // Grain is pinned to the page too, so it scrolls with the battlefield instead of sitting on the lens.
        const FVector2D Origin = V(View.screen({0, 0}));
        const float Tile = FMath::Max(64.f, 900 * static_cast<float>(View.scale));
        FCanvasTileItem Item(FVector2D(0, 0), Grain->GetResource(), FVector2D(Width, Height), FVector2D(-Origin.X / Tile, -Origin.Y / Tile),
                             FVector2D((Width - Origin.X) / Tile, (Height - Origin.Y) / Tile), FLinearColor::White);
        Item.BlendMode = SE_BLEND_Modulate;
        Canvas->DrawItem(Item);
    }
}

void AGWHUD::DrawMenuBattle()
{
    BackgroundBattle.step(GetWorld()->GetDeltaSeconds());
    auto View = BackgroundBattle.view(Width, Height);
    float Z = View.scale;
    const int W = FMath::Max(1, FMath::RoundToInt(Width)), H = FMath::Max(1, FMath::RoundToInt(Height));
    if (!MenuBackgroundCanvas)
        MenuBackgroundCanvas = NewObject<UCanvas>(this);
    if (!MenuBackgroundTarget)
    {
        MenuBackgroundTarget = NewObject<UTextureRenderTarget2D>(this);
        MenuBackgroundTarget->RenderTargetFormat = RTF_RGBA8;
        MenuBackgroundTarget->TargetGamma = 1;
        MenuBackgroundTarget->InitCustomFormat(W, H, PF_B8G8R8A8, false);
        MenuBackgroundTarget->UpdateResourceImmediate(true);
    }
    else if (MenuBackgroundTarget->SizeX != W || MenuBackgroundTarget->SizeY != H)
        MenuBackgroundTarget->ResizeTarget(W, H);
    UCanvas* Previous = Canvas.Get();
    FCanvas Background(MenuBackgroundTarget->GameThread_GetRenderTargetResource(), nullptr, GetWorld(),
                       GetWorld()->GetFeatureLevel());
    Background.Clear(Paper);
    MenuBackgroundCanvas->Init(W, H, nullptr, &Background);
    Canvas = MenuBackgroundCanvas;
    DrawPaperGround(View);
    for (const auto& Shot : BackgroundBattle.shots)
    {
        auto P = V(View.screen(Shot.position));
        auto D = V(Shot.velocity.normalized());
        const FLinearColor Trail = Shot.team == 1 ? Marigold : Violet;
        Line(P - D * 22 * Z, P, Trail.CopyWithNewOpacity(.5f), FMath::Max(2.f, 5 * Z));
        Line(P - D * 8 * Z, P, Ink.CopyWithNewOpacity(.8f), FMath::Max(1.2f, 2 * Z));
    }
    if (BackgroundBattle.age >= 10)
    {
        // The elite: a big card-stock block wearing a gold star, inside its slam warning ring.
        auto P = V(View.screen(BackgroundBattle.boss));
        Circle(P, 320 * Z, Danger.CopyWithNewOpacity(.45f), 2, 77);
        FlatBody(2, P, 100 * Z, static_cast<float>(BackgroundBattle.age) * .15f, Danger, true, 78);
        Glyph(5, P - FVector2D(0, 120 * Z), 70 * Z, Marigold, 79);
    }
    for (int32 I = 0; I < static_cast<int32>(BackgroundBattle.fighters.size()); ++I)
    {
        const auto& F = BackgroundBattle.fighters[I];
        auto P = V(View.screen(F.position));
        // Every fifth friendly is a player (coral circle); the rest are recruited companions.
        const bool Monster = F.team == 2, Player = !Monster && I % 5 == 0;
        FlatBody(static_cast<uint8>(Player ? 0 : F.shape), P, 22 * Z, static_cast<float>(F.angle),
                 Monster ? Magenta : Player ? Coral : Teal, Monster, I * 7);
        if (!Monster)
            FlatWeapon(static_cast<uint8>(F.weapon), P, static_cast<float>(F.angle), Z);
    }
    // A paper veil pushes the brawl behind the title page.
    Box(0, 0, Width, Height, Paper.CopyWithNewOpacity(.3f));
    Background.Flush_GameThread();
    MenuBackgroundCanvas->Canvas = nullptr;
    Canvas = Previous;
    // Preserve the released menu's real 13-sample background blur; UI stays sharp.
    auto Smooth = [](double T)
    {
        T = FMath::Clamp(T, 0., 1.);
        return T * T * (3 - 2 * T);
    };
    double Track = Smooth(BackgroundBattle.age / 2) * (1 - Smooth((BackgroundBattle.age - 7) / 2));
    double Focus = Smooth((BackgroundBattle.age - 9.35f) / 1.25f) * (1 - Smooth((BackgroundBattle.age - 13.8f) / 2.2f));
    float Radius = .4f * (1 - .78f * Track) * (1 - Focus) * 14 * FMath::Min(Width / 720, Height / 1280);
    int Samples = Radius > .05f ? 13 : 1;
    for (int I = 0; I < Samples; ++I)
    {
        float A = (I - 1) * PI / 6;
        FVector2D Offset = I == 0 ? FVector2D::ZeroVector : FVector2D(FMath::Cos(A) * Radius / W, FMath::Sin(A) * Radius / H);
        FCanvasTileItem Tile({0, 0}, MenuBackgroundTarget->GetResource(), {Width, Height}, Offset, FVector2D(1, 1) + Offset,
                             FLinearColor(1, 1, 1, 1.f / (I + 1)));
        Tile.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(Tile);
    }
}
