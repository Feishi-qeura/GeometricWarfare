#include "GWHUD.h"
#include "GWController.h"
#include "GWGameState.h"
#include "GWPalette.h"
#include "GWPlayerState.h"
#include "GWSettings.h"
#include "GWWorld.h"

// Comic damage numbers that pop over the 3D arena; the aim diamond and charge ring live in the world.
using namespace GWPalette;

void AGWHUD::DrawCombatFeedback(AGWGameState* S)
{
    auto* C = Cast<AGWController>(PlayerOwner);
    auto* PS = C ? C->GetPlayerState<AGWPlayerState>() : nullptr;
    const auto* U = PS ? S->FindUnit(PS->UnitId) : nullptr;
    const double Now = GetWorld()->GetTimeSeconds();
    if (DamageSeed != S->Seed)
    {
        DamageBirth.Reset();
        DamageSeed = S->Seed;
    }
    if (const auto* Prefs = UGWUserSettings::Get(); Prefs && !Prefs->DamageNumbers)
        return;
    // Comic damage numbers: they pop in large, drift up and carry a paper outline; big hits get a burst.
    for (const auto& N : S->DamageNumbers)
    {
        double* Birth = DamageBirth.Find(N.Id);
        if (!Birth)
            Birth = &DamageBirth.Add(N.Id, Now - (.9f - N.Remaining));
        const float Age = Now - *Birth, Fade = FMath::Clamp((.9f - Age) / .25f, 0.f, 1.f);
        FVector2D P;
        if (Age > .9f || !PlayerOwner->ProjectWorldLocationToScreen(N.Position + FVector(0, 0, 50), P) || P.X < -80 ||
            P.Y < -80 || P.X > Width + 80 || P.Y > Height + 80)
            continue;
        P += FVector2D((static_cast<int32>(N.Id % 5) - 2) * 12 * Scale, (-40 - Age * 62) * Scale);
        const FText Number = FText::AsNumber(FMath::Max(1, FMath::RoundToInt(N.Amount)));
        const bool Big = N.Amount >= 100;
        const float Pop = 1 + .35f * FMath::Clamp(1 - Age / .12f, 0.f, 1.f), Size = (Big ? 26 : 18) * Scale * Pop;
        const FLinearColor Color =
            (N.Victim < 0 ? Marigold : U && N.Team == U->Team ? Danger : Hex(0xF07A1A)).CopyWithNewOpacity(Fade);
        if (Big)
        {
            const FVector2D Mid = P + FVector2D(0, Size * .62f);
            TArray<FVector2D> Burst = {Mid};
            for (int32 I = 0; I <= 16; ++I)
            {
                const float A = (I % 16) * PI / 8 + static_cast<float>(N.Id % 64);
                Burst.Add(Mid + FVector2D(FMath::Cos(A) * 1.35f, FMath::Sin(A)) * Size * (I % 2 ? .55f : .95f));
            }
            Fill(Burst, Spark.CopyWithNewOpacity(.9f * Fade));
            for (int32 I = 1; I < 17; ++I)
                Line(Burst[I], Burst[I + 1], Ink.CopyWithNewOpacity(Fade), 1.4f);
        }
        Label(Number, P.X + 2 * Scale, P.Y + 3 * Scale, Size, Ink.CopyWithNewOpacity(Fade * .7f), true, 0, EFace::Heavy);
        for (const FVector2D& O : {FVector2D(-2, 0), FVector2D(2, 0), FVector2D(0, -2), FVector2D(0, 2)})
            Label(Number, P.X + O.X * Scale, P.Y + O.Y * Scale, Size, Paper.CopyWithNewOpacity(Fade), true, 0, EFace::Heavy);
        Label(Number, P.X, P.Y, Size, Color, true, 0, EFace::Heavy);
    }
    for (auto It = DamageBirth.CreateIterator(); It; ++It)
        if (Now - It.Value() > 2)
            It.RemoveCurrent();
}
