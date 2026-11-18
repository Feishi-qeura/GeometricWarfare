#include "GWHUD.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "GWController.h"
#include "GWMotion.h"
#include "GWPalette.h"
#include "GWText.h"
#include "GWWorld.h"

// The title page's wash over the 3D battle behind it, the battle's level-up cards, and the shop's flat gun icons drawn
// with the HUD's pens.
using namespace GWPalette;

void AGWHUD::FlatLine(FVector2D A, FVector2D B, FLinearColor Color, float Thickness)
{
    FCanvasLineItem Item(A, B);
    Item.SetColor(Color);
    Item.LineThickness = Thickness;
    Canvas->DrawItem(Item);
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

void AGWHUD::MenuWash()
{
    // The title page sits over the brawl (AGWWorld's showcase): a paper wash from the left edge keeps the title and
    // the menu clear and fades out before the middle; a pale band along the bottom carries the notices.
    const int32 Strips = 32;
    const float Reach = Width * .58f;
    for (int32 I = 0; I < Strips; ++I)
    {
        const float U = (I + .5f) / Strips;
        Box(Reach * I / Strips, 0, Reach / Strips + 1, Height, Paper.CopyWithNewOpacity(.82f * FMath::Square(1 - U)));
    }
    // The title reaches further right than the menu: a second wash behind it, fading right and down.
    const int32 Columns = 20, Rows = 10;
    const float Across = Width * .72f, Down = Height * .34f;
    for (int32 X = 0; X < Columns; ++X)
        for (int32 Y = 0; Y < Rows; ++Y)
        {
            const float U = (X + .5f) / Columns, V = (Y + .5f) / Rows;
            Box(Across * X / Columns, Down * Y / Rows, Across / Columns + 1, Down / Rows + 1,
                Paper.CopyWithNewOpacity(.5f * FMath::Square(1 - V) * (1 - U * U)));
        }
    for (int32 I = 0; I < 12; ++I)
        Box(0, Height - (I + 1) * 10 * Scale, Width, 10 * Scale + 1, Paper.CopyWithNewOpacity(.035f * (12 - I)));
}

void AGWHUD::MiniCard(FVector2D Middle, float CardW, float CardH, int32 Skill, bool Lit, int32 Seed)
{
    // The match's upgrade card in miniature: a paper card with its family's band and the skill's icon.
    const FLinearColor Family = SkillFamily(Skill);
    const float Left = Middle.X - CardW * .5f, Top = Middle.Y - CardH * .5f;
    Card(Left, Top, CardW, CardH, Paper, Lit ? Family : Ink, Seed, Lit ? 2.4f : 1.3f, (Lit ? 2.6f : 1.6f) * Scale);
    Box(Left + 4 * Scale, Top + 4 * Scale, CardW - 8 * Scale, 15 * Scale, Family.CopyWithNewOpacity(.92f));
    Hatch(Left + 4 * Scale, Top + 4 * Scale, CardW - 8 * Scale, 15 * Scale, (Family * .5f).CopyWithNewOpacity(.4f), 6 * Scale, Scale, Seed);
    SkillIcon(Skill, FVector2D(Middle.X, Middle.Y + 8 * Scale), 36 * Scale, Seed + 5);
}

void AGWHUD::ShowcaseCards()
{
    const auto* Viewer = Cast<AGWController>(PlayerOwner);
    if (!Viewer || !Viewer->WorldView)
        return;
    for (const FGWShowcasePick& Pick : Viewer->WorldView->ShowcasePicks)
    {
        const FVector HeadAt = Project(Pick.Head), BodyAt = Project(Pick.Body);
        const FVector2D Head(HeadAt.X, HeadAt.Y), Body(BodyAt.X, BodyAt.Y);
        const float T = Pick.Age, CardW = 64 * Scale, CardH = 88 * Scale, Break = 1.62f;
        // Where the picked card stands once it has been picked: centred over the head.
        const FVector2D Picked = Head + FVector2D(0, -(CardH * .62f + 26 * Scale));
        // The roulette: a light runs along the cards, slowing down, and stops on the one picked.
        const int32 Steps = 7 + Pick.Chosen;
        int32 Lit = -1;
        if (T >= .35f && T < 1.2f)
            for (int32 K = 0; K < Steps; ++K)
                if (T >= .35f + .8f * FMath::Pow(static_cast<float>(K) / Steps, 1.6f))
                    Lit = K % 3;
        // Three cards dealt out of the head in a fan; the other two drop away once one is picked.
        for (int32 I = 0; I < 3; ++I)
        {
            const bool Chosen = I == Pick.Chosen;
            if (Chosen && T >= Break)
                continue;
            const float Slot = I - 1.f, Out = GWEase::OutBack(GWEase::Clamp01((T - I * .06f) / .32f), 1.5f);
            if (Out <= .01f)
                continue;
            FVector2D Middle = Head + FVector2D(Slot * (CardW + 12 * Scale) * Out, (-(CardH * .5f + 18 * Scale) + FMath::Abs(Slot) * 12 * Scale) * Out);
            float Turn = Slot * 10 * Out, Grow = Out, Alpha = 1;
            if (Lit == I)
                Middle.Y -= 12 * Scale;
            if (T >= 1.2f)
            {
                if (Chosen)
                {
                    const float K = GWEase::Clamp01((T - 1.2f) / .3f);
                    Middle = FMath::Lerp(Middle, Picked, static_cast<double>(GWEase::OutCubic(K)));
                    Turn *= 1 - K;
                    Grow = 1 + .3f * FMath::Sin(K * PI) + .12f * K;
                    // It shivers before it breaks.
                    if (T >= 1.45f)
                        Middle += FVector2D(FMath::Sin(T * 97), FMath::Cos(T * 83)) * 2.5f * Scale;
                }
                else
                {
                    const float K = GWEase::Clamp01((T - 1.2f) / .4f);
                    Middle.Y += K * K * 70 * Scale;
                    Turn += Slot * 50 * K;
                    Alpha = 1 - K;
                }
            }
            if (Alpha <= .01f)
                continue;
            PushMotion(Middle, FVector2D::ZeroVector, Turn, Grow, Alpha);
            const FMotionGuard Deal(this);
            MiniCard(Middle, CardW, CardH, Pick.Skills[I], Lit == I || (Chosen && T >= 1.2f), 900 + I * 17);
        }
        const int32 Skill = Pick.Skills[FMath::Clamp(Pick.Chosen, 0, 2)];
        const FLinearColor Family = SkillFamily(Skill);
        // The pick's name, taped under its card while it is shown.
        if (T >= 1.25f && T < Break + .2f)
        {
            const float In = GWEase::OutBack(GWEase::Clamp01((T - 1.25f) / .2f)), Gone = GWEase::Clamp01((T - Break) / .2f);
            const FVector2D Under = Picked + FVector2D(0, CardH * .62f + 4 * Scale);
            const FText Title = GWText::Get(FString::Printf(TEXT("Skill.%d"), Skill));
            const float Wide = Measure(Title, 15 * Scale, EFace::Heavy).X + 24 * Scale;
            PushMotion(Under, FVector2D::ZeroVector, -3, In, 1 - Gone);
            const FMotionGuard Deal(this);
            Tape(Under, Wide, 26 * Scale, -3, Family);
            Label(Title, Under.X, Under.Y - 10 * Scale, 15 * Scale, Paper, true, 0, EFace::Heavy);
        }
        // The card breaks into triangular shards that fly along curves into the player, one after another.
        if (T >= Break)
        {
            const float HalfW = CardW * .56f, HalfH = CardH * .56f;
            int32 Index = 0;
            for (int32 Row = 0; Row < 3; ++Row)
                for (int32 Col = 0; Col < 2; ++Col)
                {
                    const float X0 = -HalfW + Col * HalfW, X1 = X0 + HalfW, Y0 = -HalfH + Row * HalfH * 2 / 3, Y1 = Y0 + HalfH * 2 / 3;
                    const FVector2D Ca(X0, Y0), Cb(X1, Y0), Cc(X1, Y1), Cd(X0, Y1);
                    const bool Even = (Row + Col) % 2 == 0;
                    const FVector2D Pieces[2][3] = {{Ca, Cb, Even ? Cc : Cd}, {Even ? Ca : Cb, Cc, Cd}};
                    for (const auto& Piece : Pieces)
                    {
                        const int32 N = Index++;
                        const FVector2D Mid = (Piece[0] + Piece[1] + Piece[2]) / 3;
                        const float S = GWEase::Clamp01((T - Break - .025f * (N % 6) - .012f * (N / 6)) / .5f);
                        if (S >= 1)
                            continue;
                        const FVector2D Away = Mid.GetSafeNormal();
                        const FVector2D P0 = Picked + Mid + Away * 10 * Scale * GWEase::Clamp01((T - Break) / .08f);
                        const FVector2D P1 = P0 + (Away * 95 + FVector2D(0, -55)) * Scale;
                        const FVector2D P2 = Body + FVector2D(FMath::Sin(N * 2.3f) * 120, -100 - 50 * FMath::Cos(N * 1.7f)) * Scale;
                        auto Curve = [&](float U)
                        {
                            const float V = 1 - U;
                            return P0 * (V * V * V) + P1 * (3 * V * V * U) + P2 * (3 * V * U * U) + Body * (U * U * U);
                        };
                        // A trail of dots behind it along its curve.
                        for (int32 K = 1; K <= 3; ++K)
                            if (const float Behind = S - K * .055f; Behind > 0)
                                Disc(Curve(GWEase::InOutCubic(Behind)), (3.4f - .7f * K) * Scale, Family.CopyWithNewOpacity(.55f - .13f * K), 8);
                        const float E = GWEase::InOutCubic(S);
                        const FVector2D At = Curve(E);
                        PushMotion(At, FVector2D::ZeroVector, N * 37 + E * 300 * (N % 2 ? 1 : -1), 1 - .65f * E, 1 - GWEase::Clamp01((E - .88f) / .12f));
                        const FMotionGuard Deal(this);
                        Polygon({At + (Piece[0] - Mid), At + (Piece[1] - Mid), At + (Piece[2] - Mid)}, Family, Ink, 1.2f * Scale, 930 + N);
                    }
                }
        }
        // Taken in: a ring bursts round the player and rays fly off it.
        if (T >= 2.1f && T < 2.7f)
        {
            const float K = GWEase::Clamp01((T - 2.1f) / .55f);
            Circle(Body, (12 + 64 * GWEase::OutCubic(K)) * Scale, Family.CopyWithNewOpacity(1 - K), 3.2f * Scale * (1 - K) + .5f, 960, 1);
            for (int32 Ray = 0; Ray < 8; ++Ray)
            {
                const FVector2D Way = FVector2D(1, 0).GetRotated(Ray * 45 + 22.5f);
                Line(Body + Way * (20 + 50 * K) * Scale, Body + Way * (30 + 70 * K) * Scale, Family.CopyWithNewOpacity(1 - K), 2.5f * Scale);
            }
        }
    }
}
