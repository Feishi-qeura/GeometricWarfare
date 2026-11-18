#include "GWGameState.h"
#include "GWPalette.h"
#include "GWWorld.h"

// The expedition's bestiary on the page (design v2.1 chapter 7): every enemy, elite, boss and summon drawn from the
// diorama's card-stock solids in its pen's colours, its act shown on the body (a wind-up trembles and glows, a phantom
// fades to a dashed outline, a bug swells before it bursts, a clicker wears its current ink), a boss's erasable tools
// ringed in gold dashes while they can still be rubbed out; then the foes' own shots, and the floor's hazards, terrain
// and warnings (Core/Foes.h, Core/FoeRules.inl).

namespace GWFoeColors
{
const FLinearColor FoeInk(.012f, .014f, .022f);
const FLinearColor Lead = GWPalette::Hex(0x5E6678), Steel = GWPalette::Hex(0xC5CCD6), Brass = GWPalette::Hex(0xD9A441),
                   Cinnabar = GWPalette::Hex(0xC23B2A), Wash = GWPalette::Hex(0x22A9BE), Indigo = GWPalette::Hex(0x3B2F8F),
                   Blue = GWPalette::Hex(0x2F6BD8), Eraser = GWPalette::Hex(0xF08A9A), Wood = GWPalette::Hex(0xE4B36A),
                   Cyan = GWPalette::Hex(0x2BC4D8);
// The rainbow's seven colours, for the boss and its palette.
FLinearColor RainbowAt(float T)
{
    static const FLinearColor Colors[7] = {GWPalette::Hex(0xE23A33), GWPalette::Hex(0xFF8A2B), GWPalette::Hex(0xFFD84A), GWPalette::Hex(0x4DBA5C),
                                           GWPalette::Hex(0x2BC4D8), GWPalette::Hex(0x2F6BD8), GWPalette::Hex(0x8E5CD9)};
    const float W = FMath::Fmod(FMath::Max(0.f, T), 7.f);
    const int32 A = FMath::FloorToInt(W) % 7;
    return FMath::Lerp(Colors[A], Colors[(A + 1) % 7], W - FMath::FloorToFloat(W));
}
} // namespace GWFoeColors

void AGWWorld::FoeBody(const FGWUnitView& U, FVector Base, float R, float Lift, float Flash, float Time)
{
    using gwgame::Foe;
    using gwgame::FoeAct;
    using namespace GWPalette;
    using namespace GWFoeColors;
    const Foe Kind = static_cast<Foe>(FMath::Clamp(U.Breed, 0, gwgame::FoeCount - 1));
    const gwgame::FoeInfo& Info = gwgame::foeInfo(U.Breed);
    const FLinearColor Tone = Biome(Info.biome < 0 ? 0 : Info.biome);
    const float Yaw = U.Angle;
    const FVector F = FRotator(0, Yaw, 0).Vector(), S(-F.Y, F.X, 0), Z(0, 0, 1);
    const FoeAct Act = static_cast<FoeAct>(U.Act);
    const bool Winding = Act == FoeAct::Windup || Act == FoeAct::Aim || Act == FoeAct::Leap || Act == FoeAct::Fuse || Act == FoeAct::Write;
    // A wind-up trembles and glows where it stands.
    const FVector O = Base + (Winding ? S * (FMath::Sin(Time * 70 + U.Id) * 3.f) : FVector::ZeroVector) + Z * Lift;
    const float H = R * 1.25f;
    const float InkW = R >= 100 ? 6.f : R >= 40 ? 4.5f : 3.5f;
    const float Id = static_cast<float>(U.Id);
    auto At = [&](float Fwd, float Side, float Up) { return O + F * (Fwd * R) + S * (Side * R) + Z * Up; };
    auto Part = [&](int32 Mesh, FVector Center, FVector Size, float PartYaw, const FLinearColor& Color, float Edge = -1.f)
    { Solid(Mesh, Center, Size, PartYaw, Color, Flash, Edge < 0 ? InkW : Edge); };
    auto Tilt = [&](int32 Mesh, FVector Center, FVector Size, FRotator Turn, const FLinearColor& Color, float Edge = 2.5f)
    { Rotated(Mesh, Center, Size, Turn, Color, Edge); };
    auto Eyes = [&](float Up, float Ahead = .78f, float Apart = .34f)
    {
        const float Eye = FMath::Clamp(R * .22f, 6.f, 26.f);
        for (const float Sign : {-1.f, 1.f})
            Solid(MeshSphere, At(Ahead, Apart * Sign, Up), FVector(Eye), 0, Paper, 0, Eye * .35f);
    };
    // The dashed gold ring round a boss's tool that can still be rubbed out (design 5.1: 粗虚线高亮), on the floor below it.
    auto Tool = [&](int32 Bit)
    {
        if (!((U.Parts >> Bit) & 1))
            return;
        const gwgame::FoePart P = gwgame::foePart(U.Breed, Bit);
        const FVector Floor = Base + F * (P.forward * R) + S * (P.side * R);
        Decal(DecalCompass, FVector(Floor.X, Floor.Y, 2.4f), FVector2D(R * 1.15f), Time * 70, Marigold.CopyWithNewOpacity(.75f + .2f * FMath::Sin(Time * 6)));
    };
    if (Winding)
        Decal(DecalGlow, FVector(Base.X, Base.Y, 1.5f), FVector2D(R * 3.4f), 0, Danger.CopyWithNewOpacity(.3f + .2f * FMath::Sin(Time * 14)));
    if (Act == FoeAct::Dash)
        Decal(DecalStreak, FVector(Base.X, Base.Y, 1.6f) - F * R * 1.6f, FVector2D(R * 3.2f, R * 1.4f), Yaw, Tone.CopyWithNewOpacity(.55f));
    switch (Kind)
    {
    // ---- Pencil ----
    case Foe::Scribble:
    {
        // A ball of tangled graphite strokes, rolling.
        Part(MeshSphere, At(0, 0, R), FVector(R * 1.7f), 0, Lead);
        for (int32 K = 0; K < 3; ++K)
            Tilt(MeshCoin, At(0, 0, R), FVector(R * 2.2f, R * 2.2f, 4), FRotator(35 + K * 50, Time * 220 + K * 60, K * 30), Pencil, 1.5f);
        Eyes(R * 1.3f, .6f);
        break;
    }
    case Foe::Phantom:
        // A dashed pencil triangle; hidden, it is only its dashes.
        if (Act == FoeAct::Hidden)
        {
            Decal(DecalTriRing, FVector(Base.X, Base.Y, 2.f), FVector2D(R * 2.6f), Yaw - 90, Pencil.CopyWithNewOpacity(.45f + .15f * FMath::Sin(Time * 9)));
            break;
        }
        Part(MeshTri, At(0, 0, H * .5f), FVector(R * 2.2f, R * 2.f, H * .8f), Yaw, FMath::Lerp(Paper, Pencil, .35f));
        Decal(DecalTriRing, FVector(Base.X, Base.Y, 2.f), FVector2D(R * 2.8f), Yaw - 90, Lead.CopyWithNewOpacity(.6f));
        Eyes(H * .85f, .45f, .28f);
        break;
    case Foe::Shaver:
        // A pencil's hexagonal cut end: wood round a graphite core that spits leads.
        Part(MeshHex, At(0, 0, H * .5f), FVector(R * 2.2f, R * 2.2f, H), Yaw, Wood);
        Part(MeshCoin, At(0, 0, H + 3), FVector(R * .8f, R * .8f, 8), 0, Lead, 2);
        Part(MeshCoin, At(.95f, 0, H * .55f), FVector(R * .5f, R * .5f, R * .5f), Yaw, Lead, 2);
        Eyes(H * .8f, .7f, .45f);
        break;
    case Foe::Shield:
        // A block shaded in heavy 2B, a hatched card shield in front (cracked off once broken).
        Part(MeshBlock, At(0, 0, H * .5f), FVector(R * 1.8f, R * 1.8f, H), Yaw, Lead);
        if (Act == FoeAct::Broken)
            for (const float Sign : {-1.f, 1.f})
                Tilt(MeshBlock, At(1.3f, .6f * Sign, 6), FVector(R * .25f, R * .9f, R * .9f), FRotator(70, Yaw + 20 * Sign, 0), Pencil, 2);
        else
        {
            Part(MeshBlock, At(1.05f, 0, H * .55f), FVector(R * .3f, R * 2.3f, H * 1.1f), Yaw, Pencil);
            Decal(DecalHatch, At(1.05f, 0, H * 1.12f), FVector2D(R * 2.2f, R * .5f), Yaw, FoeInk.CopyWithNewOpacity(.5f));
        }
        Eyes(H * .8f, .55f);
        break;
    case Foe::Stub:
        // A snapped-off yellow pencil: graphite point ahead, a metal band and a pink eraser behind.
        Part(MeshBlock, At(0, 0, R * .55f), FVector(R * 2.2f, R * 1.1f, R * 1.1f), Yaw, Marigold);
        Part(MeshDiamond, At(1.4f, 0, R * .55f), FVector(R * 1.f, R * .7f, R * .7f), Yaw, Wood, 2);
        Part(MeshCoin, At(-1.2f, 0, R * .55f), FVector(R * 1.2f, R * 1.2f, R * .3f), Yaw, Steel, 2);
        Part(MeshSphere, At(-1.55f, 0, R * .55f), FVector(R * 1.f), 0, Eraser, 2);
        Eyes(R * 1.05f, .4f, .25f);
        break;
    // ---- Watercolor ----
    case Foe::Slime:
    {
        // A drop of bleeding wash; mid-hop it stretches tall.
        const bool Hop = Act == FoeAct::Hop;
        const FVector Size = Hop ? FVector(R * 1.6f, R * 1.6f, R * 2.2f) : FVector(R * 2.1f, R * 2.1f, R * 1.3f);
        Part(MeshSphere, At(0, 0, Size.Z * .5f + (Hop ? R * .6f : 0)), Size, 0, FMath::Lerp(Wash, Frost, .4f));
        Decal(DecalDot, FVector(Base.X, Base.Y, 1.3f), FVector2D(R * 2.6f), Id * 40, Wash.CopyWithNewOpacity(.3f));
        Eyes(Size.Z * .75f + (Hop ? R * .6f : 0), .55f);
        break;
    }
    case Foe::Striker:
        // Two diamonds of magenta and cyan bleeding into each other.
        Part(MeshDiamond, At(.1f, -.35f, H * .5f), FVector(R * 2.4f, R * 1.2f, H * .8f), Yaw + 20, Magenta);
        Part(MeshDiamond, At(-.1f, .35f, H * .5f), FVector(R * 2.4f, R * 1.2f, H * .8f), Yaw - 20, Cyan);
        Eyes(H * .85f, .7f, .25f);
        break;
    case Foe::Sprayer:
        // An upturned watercolour bottle with a spray head.
        Tilt(MeshTrap, At(0, 0, H * .5f), FVector(R * 2.1f, R * 2.f, H), FRotator(0, Yaw, 180), Wash);
        Part(MeshCoin, At(.3f, 0, H + 8), FVector(R * .6f, R * .6f, 16), 0, Steel, 2);
        Part(MeshSphere, At(.75f, 0, H + 6), FVector(R * .4f), 0, Magenta, 1.5f);
        Eyes(H * .7f, .7f);
        break;
    case Foe::Zapper:
        // A fluorescent bubble in a ring: charged, it crackles.
        Part(MeshSphere, At(0, 0, R * 1.05f), FVector(R * 1.6f), 0, Spark);
        Tilt(MeshCoin, At(0, 0, R * 1.05f), FVector(R * 2.6f, R * 2.6f, 5), FRotator(15, Time * 120, 0), Frost, 2);
        if (Winding)
            for (int32 K = 0; K < 4; ++K)
                Part(MeshDiamond, At(0, 0, R * 1.05f) + FRotator(0, Time * 400 + K * 90, 0).Vector() * R * 1.4f, FVector(16, 6, 6), Time * 400 + K * 90, Spark, 1);
        Eyes(R * 1.5f, .55f);
        break;
    case Foe::Sponge:
        // A pale washed sponge, pocked with holes, swelling with each puddle it drinks.
        Part(MeshBlock, At(0, 0, H * .5f), FVector(R * 1.9f, R * 1.9f, H * .9f), Yaw, GWPalette::Hex(0xF1E3A0));
        for (int32 K = 0; K < 5; ++K)
            Decal(DecalDot, At(-.5f + K * .25f, (K % 2 ? .4f : -.35f), H * .95f + 1), FVector2D(R * .35f), 0, GWPalette::Hex(0xC9B76A).CopyWithNewOpacity(.9f));
        Eyes(H * .75f, .7f);
        break;
    // ---- Fountain pen ----
    case Foe::Lancer:
        // A long cracked iridium point, gold-tipped.
        Part(MeshDiamond, At(0, 0, H * .45f), FVector(R * 3.4f, R * .9f, R * .9f), Yaw, Night);
        Part(MeshDiamond, At(1.6f, 0, H * .45f), FVector(R * 1.f, R * .5f, R * .5f), Yaw, Brass, 2);
        Decal(DecalStreak, At(0, 0, H * .45f + R * .5f), FVector2D(R * 2.4f, 4), Yaw, Paper.CopyWithNewOpacity(.8f));
        Eyes(H * .7f, .2f, .22f);
        break;
    case Foe::InkBug:
    {
        // A swollen black ink sac under a pentagon shell; a fuse swells it.
        const float Swell = Act == FoeAct::Fuse ? 1.f + .35f * (.5f + .5f * FMath::Sin(Time * 30)) : 1.f;
        Part(MeshSphere, At(0, 0, R * .9f * Swell), FVector(R * 1.9f * Swell), 0, Night);
        Part(MeshPent, At(-.2f, 0, R * 1.6f * Swell), FVector(R * 1.4f, R * 1.4f, R * .5f), Yaw, Plum);
        Eyes(R * 1.2f * Swell, .75f);
        break;
    }
    case Foe::Crab:
        // A broad trapezoid covered in cross-hatching, claws out; struck often, it bristles with thorns.
        Part(MeshTrap, At(0, 0, H * .4f), FVector(R * 2.8f, R * 1.7f, H * .7f), Yaw, Plum);
        Decal(DecalHatch, At(0, 0, H * .75f + 1), FVector2D(R * 2.4f, R * 1.4f), Yaw + 45, FoeInk.CopyWithNewOpacity(.55f));
        for (const float Sign : {-1.f, 1.f})
            Part(MeshDiamond, At(.9f, 1.25f * Sign, H * .4f), FVector(R * .9f, R * .5f, R * .5f), Yaw + 30 * Sign, Night, 2);
        if (Act == FoeAct::Spikes)
            for (int32 K = 0; K < 8; ++K)
            {
                const float A = K * 45.f + Yaw;
                Part(MeshDiamond, O + FRotator(0, A, 0).Vector() * R * 1.5f + Z * H * .4f, FVector(R * .9f, 8, 8), A, Night, 1.5f);
            }
        Eyes(H * .8f, .55f, .4f);
        break;
    case Foe::Inkwell:
        // A squat old hexagonal ink bottle, corked.
        Part(MeshHex, At(0, 0, H * .45f), FVector(R * 2.1f, R * 2.1f, H * .9f), Yaw, Night);
        Part(MeshCoin, At(0, 0, H * .95f), FVector(R * .9f, R * .9f, R * .4f), 0, Indigo, 2);
        Part(MeshCoin, At(0, 0, H * 1.2f), FVector(R * .7f, R * .7f, R * .35f), 0, Kraft, 2);
        Eyes(H * .7f, .75f);
        break;
    case Foe::Saw:
        // A spinning eight-point star of black steel wire.
        Part(MeshStar, At(0, 0, R * .5f), FVector(R * 2.4f, R * 2.4f, R * .4f), Time * 900 + Id * 13, FoeInk);
        Part(MeshStar, At(0, 0, R * .5f + 6), FVector(R * 1.7f, R * 1.7f, R * .3f), Time * 900 + Id * 13 + 22.5f, Steel, 1.5f);
        Part(MeshCoin, At(0, 0, R * .7f + 6), FVector(R * .5f, R * .5f, 6), 0, Danger, 1.5f);
        break;
    // ---- Ballpoint ----
    case Foe::Beader:
        // A blue ball in a steel ring, a bead at its lip.
        Part(MeshSphere, At(0, 0, R * 1.f), FVector(R * 1.8f), 0, Blue);
        Tilt(MeshCoin, At(0, 0, R * 1.f), FVector(R * 2.5f, R * 2.5f, 5), FRotator(0, Yaw, 90), Steel, 2);
        Part(MeshSphere, At(1.f, 0, R * 1.f), FVector(R * .4f), 0, Steel, 1.5f);
        Eyes(R * 1.6f, .45f);
        break;
    case Foe::Marker:
        // A red correction box: a pentagon frame round a blank of paper.
        Part(MeshPent, At(0, 0, H * .45f), FVector(R * 2.2f, R * 2.2f, H * .9f), Yaw, Danger);
        Part(MeshPent, At(0, 0, H * .9f + 2), FVector(R * 1.4f, R * 1.4f, 4), Yaw, Paper, 1.5f);
        Decal(DecalPlus, At(0, 0, H * .9f + 5), FVector2D(R * .9f), Yaw + 45, Danger.CopyWithNewOpacity(.85f));
        Eyes(H * .7f, .7f);
        break;
    case Foe::Twin:
        // A blue triangle and its violet carbon copy, offset.
        Part(MeshTri, At(-.15f, .35f, H * .45f), FVector(R * 2.f, R * 1.8f, H * .7f), Yaw, FMath::Lerp(Violet, Paper, .35f), 2);
        Part(MeshTri, At(.1f, -.15f, H * .5f), FVector(R * 2.1f, R * 1.9f, H * .8f), Yaw, Blue);
        Eyes(H * .85f, .45f, .25f);
        break;
    case Foe::Roller:
        // A long ballpoint tank: a turning gear on top, a click button at the back, the ball at its nose.
        Part(MeshBlock, At(0, 0, H * .45f), FVector(R * 2.7f, R * 1.3f, H * .8f), Yaw, Blue);
        Part(MeshStar, At(.1f, 0, H * .9f), FVector(R * 1.1f, R * 1.1f, 10), Time * (Act == FoeAct::Dash ? 600 : 90), Steel, 2);
        Part(MeshCoin, At(-1.2f, 0, H * .95f), FVector(R * .5f, R * .5f, Act == FoeAct::Windup ? 6.f : 20.f), 0, Danger, 2);
        Part(MeshSphere, At(1.45f, 0, H * .45f), FVector(R * .6f), 0, Steel, 2);
        Eyes(H * .75f, .9f, .3f);
        break;
    case Foe::Abacus:
        // Four beads on a rod, two blue and two red.
        Part(MeshBlock, At(0, 0, H * .5f), FVector(R * .4f, R * 3.4f, R * .3f), Yaw, Kraft, 2);
        for (int32 K = 0; K < 4; ++K)
            Part(MeshSphere, At(0, -1.2f + K * .8f, H * .5f), FVector(R * .85f), 0, K % 2 ? Danger : Blue, 2);
        Eyes(H * .85f, .45f, .4f);
        break;
    // ---- Brush ----
    case Foe::Wisp:
    {
        // An ink tadpole: a round head and a dry-brush tail that wags.
        Part(MeshSphere, At(0, 0, R * .9f), FVector(R * 1.7f), 0, FoeInk);
        for (int32 K = 1; K <= 3; ++K)
        {
            const float Wag = FMath::Sin(Time * 8 + K + Id) * .35f * K;
            Part(MeshSphere, At(-.8f * K, Wag, R * .8f), FVector(R * (1.3f - .3f * K)), 0, FMath::Lerp(FoeInk, Pencil, K * .2f), 1.5f);
        }
        Eyes(R * 1.3f, .55f);
        break;
    }
    case Foe::Parasol:
        // A triangle behind a heavy ink fan; spinning, the fan sweeps round.
        Part(MeshTri, At(-.2f, 0, H * .45f), FVector(R * 1.9f, R * 1.8f, H * .8f), Yaw, Kraft);
        Tilt(MeshCoin, At(.85f, 0, H * .6f), FVector(R * 2.4f, R * 2.4f, 5), FRotator(0, Act == FoeAct::Spin ? Time * 720 : Yaw, 90), Paper, 3);
        Decal(DecalHatch, At(.85f, 0, H * 1.25f), FVector2D(R * 1.8f, R * .4f), Yaw + 90, FoeInk.CopyWithNewOpacity(.6f));
        Eyes(H * .75f, .3f);
        break;
    case Foe::Seal:
    {
        // A squat cinnabar seal stone, its carved face on top; it leaps before it stamps.
        const float Up = Act == FoeAct::Leap ? 90 * FMath::Sin(FMath::Clamp(Time * 4 - FMath::FloorToFloat(Time * 4), 0.f, 1.f) * PI) : 0;
        Part(MeshBlock, At(0, 0, H * .5f + Up), FVector(R * 1.9f, R * 1.9f, H), Yaw, Cinnabar);
        Decal(DecalSquare, At(0, 0, H + Up + 1), FVector2D(R * 1.3f), Yaw, Paper.CopyWithNewOpacity(.85f));
        Decal(DecalPlus, At(0, 0, H + Up + 2), FVector2D(R * .9f), Yaw, Cinnabar.CopyWithNewOpacity(.9f));
        Eyes(H * .7f + Up, .75f);
        break;
    }
    case Foe::Column:
        // A tall brush standing on its tip, leaning into its sweep.
        Tilt(MeshCoin, At(0, 0, R * 1.5f), FVector(R * .9f, R * .9f, R * 3.f), FRotator(Act == FoeAct::Aim ? -18.f : 0.f, Yaw, 0), FoeInk);
        Part(MeshDiamond, At(0, 0, R * 3.2f), FVector(R * .7f, R * .7f, R * 1.f), Yaw, Wood, 2);
        Eyes(R * 2.f, .45f, .25f);
        break;
    case Foe::Pinwheel:
        // Four strokes of dry brush turning round a cinnabar dot.
        for (int32 K = 0; K < 4; ++K)
        {
            const float A = (Act == FoeAct::Spin ? Time * 260 : 0) + K * 90 + Id * 7;
            Part(MeshBlock, O + FRotator(0, A, 0).Vector() * R * .8f + Z * R * .6f, FVector(R * 1.6f, R * .45f, R * .3f), A + 20, FoeInk, 2);
        }
        Part(MeshCoin, At(0, 0, R * .9f), FVector(R * .6f, R * .6f, 10), 0, Cinnabar, 2);
        break;
    // ---- Elites ----
    case Foe::Leadcannon:
        // A gold-banded mechanical pencil barrel with six spare leads turning round it; jammed, it smokes.
        Part(MeshHex, At(0, 0, H * .5f), FVector(R * 1.9f, R * 1.9f, H), Yaw, Lead);
        Part(MeshCoin, At(0, 0, H * .75f), FVector(R * 2.f, R * 2.f, R * .25f), 0, Brass, 2);
        for (int32 K = 0; K < 6; ++K)
        {
            const float A = Time * (Act == FoeAct::Jam ? 0 : 140) + K * 60;
            Part(MeshCoin, O + FRotator(0, A, 0).Vector() * R * 1.15f + Z * H * .55f, FVector(R * .35f, R * .35f, H * .8f), 0, FoeInk, 1.5f);
        }
        if (Act == FoeAct::Jam)
            for (int32 K = 0; K < 3; ++K)
                Part(MeshSphere, At(0, 0, H + 20 + 25 * K + 15 * FMath::Sin(Time * 3 + K)), FVector(R * (.5f + .15f * K)), 0, Pencil, 0);
        Eyes(H * .85f, .8f);
        break;
    case Foe::Crumbler:
        // A huge half-rubbed pink-grey eraser block, crumbs falling off it.
        Part(MeshBlock, At(0, 0, H * .45f), FVector(R * 2.f, R * 1.7f, H * .9f), Yaw + 6, FMath::Lerp(Eraser, Pencil, .45f));
        Part(MeshBlock, At(-.3f, .2f, H * .95f), FVector(R * 1.2f, R * 1.f, R * .4f), Yaw - 9, FMath::Lerp(Eraser, Paper, .3f), 2);
        for (int32 K = 0; K < 4; ++K)
            Part(MeshBlock, At(1.1f - K * .7f, (K % 2 ? 1.1f : -1.1f), 8), FVector(12, 10, 10), Id * 30 + K * 40, FMath::Lerp(Eraser, Pencil, .5f), 1);
        Eyes(H * .7f, .9f, .35f);
        break;
    case Foe::Palette:
        // A great round palette turning slowly, its seven wells of colour round the rim.
        Part(MeshCoin, At(0, 0, R * .5f), FVector(R * 2.3f, R * 2.3f, R * .5f), 0, PaperShade);
        for (int32 K = 0; K < 7; ++K)
        {
            const float A = Time * 40 + K * 360.f / 7;
            Part(MeshCoin, O + FRotator(0, A, 0).Vector() * R * .8f + Z * (R * .5f + 8), FVector(R * .45f, R * .45f, 8), 0, RainbowAt(K), 1.5f);
        }
        Eyes(R * .9f, .3f);
        break;
    case Foe::Eel:
    {
        // A string of glowing wash beads: the head, and the body beads it has left, along its own wake.
        Part(MeshSphere, At(0, 0, R * .9f), FVector(R * 1.9f), 0, Cyan);
        TArray<FVector>& Wake = FoeTrails.FindOrAdd(U.Id);
        if (Wake.IsEmpty() || FVector::Dist2D(Wake.Last(), Base) > R * .9f)
        {
            Wake.Add(Base);
            if (Wake.Num() > 12)
                Wake.RemoveAt(0);
        }
        const int32 Beads = FMath::Clamp(static_cast<int32>(U.Parts) - 1, 0, 4);
        for (int32 K = 0; K < Beads && K < Wake.Num() - 1; ++K)
        {
            const FVector Bead = Wake[Wake.Num() - 2 - K];
            Part(MeshSphere, FVector(Bead.X, Bead.Y, R * .8f), FVector(R * (1.6f - .2f * K)), 0, FMath::Lerp(Cyan, Frost, .3f + .15f * K), 2);
        }
        Eyes(R * 1.4f, .6f);
        break;
    }
    case Foe::Swordsaint:
        // Two gold-tipped nibs crossed into one great cross-blade.
        for (const float Turn : {45.f, -45.f})
        {
            Part(MeshDiamond, At(0, 0, H * .5f), FVector(R * 3.f, R * .7f, R * .7f), Yaw + Turn, Night);
            Part(MeshDiamond, O + FRotator(0, Yaw + Turn, 0).Vector() * R * 1.45f + Z * H * .5f, FVector(R * .8f, R * .45f, R * .45f), Yaw + Turn, Brass, 2);
        }
        if (Act == FoeAct::Parry)
            Decal(DecalCompass, FVector(Base.X, Base.Y, 2.f), FVector2D(R * 3.4f), Time * 300, Marigold.CopyWithNewOpacity(.9f));
        Eyes(H * .85f, .3f);
        break;
    case Foe::Inspector:
        // An iron-framed hexagonal ink bottle; its crew near it hit twice as hard (the ring shows how near).
        Part(MeshHex, At(0, 0, H * .5f), FVector(R * 2.1f, R * 2.1f, H), Yaw, Night);
        for (const float Up : {.25f, .75f})
            Part(MeshHex, At(0, 0, H * Up), FVector(R * 2.25f, R * 2.25f, R * .18f), Yaw, Steel, 2);
        Part(MeshCoin, At(0, 0, H * 1.1f), FVector(R * .8f, R * .8f, R * .5f), 0, Steel, 2);
        Decal(DecalRing, FVector(Base.X, Base.Y, 1.4f), FVector2D(800), Time * 15, Danger.CopyWithNewOpacity(.25f));
        Eyes(H * .75f, .8f);
        break;
    case Foe::Clicker:
    {
        // A barrel with four click buttons: the colour it is firing stands up and glows.
        const int32 InkIndex = FMath::Clamp(U.Act - static_cast<int32>(FoeAct::Blue), 0, 3);
        static const FLinearColor Inks[4] = {Blue, Danger, Leaf, FoeInk};
        Part(MeshCoin, At(0, 0, H * .5f), FVector(R * 2.f, R * 2.f, H), 0, FMath::Lerp(Blue, Steel, .5f));
        for (int32 K = 0; K < 4; ++K)
            Part(MeshCoin, At(-.4f + (K % 2) * .8f, -.4f + (K / 2) * .8f, H + (K == InkIndex ? 26.f : 8.f)), FVector(R * .45f, R * .45f, K == InkIndex ? 30.f : 14.f), 0, Inks[K], 2);
        Decal(DecalGlow, FVector(Base.X, Base.Y, 1.4f), FVector2D(R * 2.8f), 0, Inks[InkIndex].CopyWithNewOpacity(.35f));
        Eyes(H * .7f, .8f);
        break;
    }
    case Foe::Mirror:
        // A carbon-blue octagon with a steel mirror face; turned open, the mirror goes dull.
        Part(MeshBlock, At(0, 0, H * .5f), FVector(R * 1.8f, R * 1.8f, H), Yaw, Indigo);
        Part(MeshBlock, At(0, 0, H * .5f), FVector(R * 1.8f, R * 1.8f, H), Yaw + 45, Indigo);
        Part(MeshBlock, At(.95f, 0, H * .55f), FVector(R * .15f, R * 1.4f, H * .8f), Yaw, Act == FoeAct::Open ? Pencil : Steel, 2);
        if (Act != FoeAct::Open)
            Decal(DecalGlow, At(1.1f, 0, 2), FVector2D(R * 1.6f), 0, Frost.CopyWithNewOpacity(.4f));
        Eyes(H * .85f, .4f);
        break;
    case Foe::Wildscript:
        // A heap of wild cursive strokes writhing round itself.
        for (int32 K = 0; K < 6; ++K)
        {
            const float A = Time * (40 + 20 * K) * (K % 2 ? 1 : -1) + K * 60;
            Tilt(MeshBlock, At(0, 0, H * (.3f + .1f * K)), FVector(R * 2.4f, R * .3f, R * .3f), FRotator(15 * (K % 3), A, 0), K == 2 ? Cinnabar : FoeInk, 2);
        }
        Eyes(H * .9f, .5f);
        break;
    case Foe::Judge:
        // A black-robed block between two hovering seals, red on its left and black on its right.
        Part(MeshBlock, At(0, 0, H * .6f), FVector(R * 1.5f, R * 1.4f, H * 1.2f), Yaw, FoeInk);
        Part(MeshBlock, At(0, -1.35f, H * .9f + 12 * FMath::Sin(Time * 3)), FVector(R * .9f, R * .9f, R * .9f), Yaw, Cinnabar, 2.5f);
        Part(MeshBlock, At(0, 1.35f, H * .9f + 12 * FMath::Sin(Time * 3 + 1.5f)), FVector(R * .9f, R * .9f, R * .9f), Yaw, Night, 2.5f);
        Eyes(H * 1.f, .6f);
        break;
    case Foe::Thief:
        // A flickering paper triangle with a bulging sack of stickers on its back.
        Part(MeshTri, At(.1f, 0, H * .45f), FVector(R * 2.1f, R * 1.9f, H * .8f), Yaw, FMath::Sin(Time * 16) > 0 ? Marigold : Spark);
        Part(MeshSphere, At(-.9f, 0, H * .8f), FVector(R * 1.5f), 0, Kraft, 2.5f);
        Solid(MeshStar, At(-.9f, 0, H * .8f + R * .8f), FVector(R * .6f, R * .6f, 8), Time * 90, Coral, 0, 1.5f);
        Eyes(H * .8f, .55f, .25f);
        break;
    case Foe::Mashup:
        // A spoiled draft: shapes of every pen jammed together, turning against each other.
        Part(MeshHex, At(0, 0, H * .45f), FVector(R * 1.9f, R * 1.9f, H * .8f), Time * 30, Lead);
        Part(MeshTri, At(.2f, .3f, H * .75f), FVector(R * 1.4f, R * 1.3f, H * .5f), -Time * 50, Wash, 2);
        Part(MeshPent, At(-.3f, -.2f, H * .95f), FVector(R * 1.f, R * 1.f, H * .4f), Time * 70, Cinnabar, 2);
        Part(MeshDiamond, At(.5f, -.4f, H * 1.1f), FVector(R * .9f, R * .4f, R * .4f), Time * 90, Indigo, 1.5f);
        Eyes(H * .9f, .6f);
        break;
    // ---- Bosses ----
    case Foe::Draftsman:
    {
        // A drafting table on four legs, a sheet pinned on it; the pencil gatling on its left arm, the gold compass on its
        // right (each ringed while it can be rubbed out).
        const float Top = R * .9f;
        Part(MeshBlock, At(0, 0, Top), FVector(R * 2.1f, R * 1.7f, R * .25f), Yaw, Wood);
        Part(MeshBlock, At(0, 0, Top + R * .15f), FVector(R * 1.6f, R * 1.2f, 6), Yaw + 4, Paper, 2);
        for (int32 K = 0; K < 4; ++K)
            Part(MeshBlock, At(K < 2 ? .8f : -.8f, K % 2 ? .65f : -.65f, Top * .5f), FVector(R * .18f, R * .18f, Top), Yaw, Lead, 2);
        Eyes(Top + R * .35f, .7f, .3f);
        if (U.Parts & 1)
        {
            const gwgame::FoePart P = gwgame::foePart(U.Breed, 0);
            const FVector Arm = At(P.forward, P.side, Top);
            Part(MeshHex, Arm, FVector(R * .7f, R * .7f, R * .9f), Yaw, Marigold);
            Tilt(MeshStar, Arm + F * R * .5f, FVector(R * .7f, R * .7f, R * .3f), FRotator(90, Yaw, Time * (Act == FoeAct::Spin ? 1200 : 90)), Lead, 2);
            Part(MeshDiamond, Arm + F * R * .8f, FVector(R * .5f, R * .25f, R * .25f), Yaw, FoeInk, 1.5f);
        }
        if (U.Parts & 2)
        {
            const gwgame::FoePart P = gwgame::foePart(U.Breed, 1);
            const FVector Hinge = At(P.forward, P.side, Top + R * .7f);
            for (const float Sign : {-1.f, 1.f})
                Tilt(MeshBlock, Hinge - Z * R * .45f + F * (Sign * R * .2f), FVector(R * .12f, R * .12f, R * 1.f), FRotator(0, Yaw, Sign * 18), Brass, 1.5f);
            Part(MeshSphere, Hinge, FVector(R * .25f), 0, Brass, 1.5f);
        }
        Tool(0);
        Tool(1);
        break;
    }
    case Foe::Rainbow:
    {
        // A great blob of running colour; split, it is a faint outline while its spirits are out.
        const bool Split = (U.Parts & gwgame::PartsSplit) != 0;
        const FLinearColor Now = RainbowAt(Time * .8f);
        if (Split)
            Decal(DecalLoop, FVector(Base.X, Base.Y, 2.f), FVector2D(R * 2.4f), Time * 60, Now.CopyWithNewOpacity(.6f));
        else
        {
            Part(MeshSphere, At(0, 0, R * .95f), FVector(R * 2.f, R * 2.f, R * 1.7f), 0, Now);
            for (int32 K = 0; K < 6; ++K)
            {
                const float A = Time * 70 + K * 60;
                Part(MeshSphere, O + FRotator(0, A, 0).Vector() * R * 1.25f + Z * (R * .6f + 20 * FMath::Sin(Time * 2 + K)), FVector(R * .45f), 0,
                     RainbowAt(K + Time), 2);
            }
            Eyes(R * 1.3f, .7f, .3f);
        }
        break;
    }
    case Foe::Editor:
    {
        // A tall hatched knight; the gilded revolver raised in its left hand, the long paper blade in its right.
        Part(MeshPent, At(0, 0, H * .7f), FVector(R * 1.7f, R * 1.7f, H * 1.4f), Yaw, Night);
        Decal(DecalHatch, At(0, 0, H * 1.4f + 1), FVector2D(R * 1.5f), Yaw + 45, Steel.CopyWithNewOpacity(.6f));
        Part(MeshTri, At(.1f, 0, H * 1.65f), FVector(R * 1.f, R * .9f, R * .6f), Yaw, Steel, 2.5f);
        Eyes(H * 1.5f, .45f, .25f);
        if (U.Parts & 1)
        {
            const gwgame::FoePart P = gwgame::foePart(U.Breed, 0);
            const FVector Hand = At(P.forward, P.side, H * .95f);
            Part(MeshBlock, Hand + F * R * .35f, FVector(R * .9f, R * .22f, R * .22f), Yaw, FoeInk, 2);
            Tilt(MeshCoin, Hand, FVector(R * .45f, R * .45f, R * .35f), FRotator(90, Yaw, 0), Brass, 2);
        }
        if (U.Parts & 2)
        {
            const gwgame::FoePart P = gwgame::foePart(U.Breed, 1);
            const FVector Hand = At(P.forward, P.side, H * .9f);
            Part(MeshBlock, Hand + F * R * .9f, FVector(R * 2.f, R * .12f, R * .4f), Yaw, Steel, 2);
            Part(MeshBlock, Hand, FVector(R * .3f, R * .5f, R * .2f), Yaw, Brass, 1.5f);
        }
        Tool(0);
        Tool(1);
        break;
    }
    case Foe::Warlord:
    {
        // A hexagonal mech under four sprung click caps; the four-colour gatling on its right shoulder, the carbon shield
        // on its left arm (raised square in front while it guards).
        Part(MeshHex, At(0, 0, H * .55f), FVector(R * 2.f, R * 2.f, H * 1.1f), Yaw, Blue);
        static const FLinearColor Caps[4] = {Blue, Danger, Leaf, FoeInk};
        for (int32 K = 0; K < 4; ++K)
        {
            const float Bounce = Act == FoeAct::Dash ? 20 * FMath::Abs(FMath::Sin(Time * 9 + K)) : 0;
            Part(MeshCoin, At(-.35f + (K % 2) * .7f, -.35f + (K / 2) * .7f, H * 1.25f + Bounce), FVector(R * .4f, R * .4f, R * .3f), 0, Caps[K], 2);
        }
        Eyes(H * .9f, .8f, .3f);
        if (U.Parts & 1)
        {
            const gwgame::FoePart P = gwgame::foePart(U.Breed, 0);
            const FVector Gun = At(P.forward, P.side, H * 1.05f);
            const int32 InkIndex = FMath::Clamp(U.Act - static_cast<int32>(FoeAct::Blue), 0, 3);
            Tilt(MeshCoin, Gun + F * R * .3f, FVector(R * .6f, R * .6f, R * 1.3f), FRotator(90, Yaw, Time * (U.Act >= static_cast<int32>(FoeAct::Blue) ? 900 : 60)), Steel, 2);
            Tilt(MeshCoin, Gun + F * R * .3f, FVector(R * .7f, R * .7f, R * .2f), FRotator(90, Yaw, 0), Caps[InkIndex], 2);
        }
        if (U.Parts & 2)
        {
            const gwgame::FoePart P = gwgame::foePart(U.Breed, 1);
            const bool Guard = Act == FoeAct::Guard;
            const FVector Arm = Guard ? At(1.15f, 0, H * .6f) : At(P.forward, P.side, H * .6f);
            Part(MeshBlock, Arm, FVector(R * .2f, R * (Guard ? 2.2f : 1.3f), H * (Guard ? 1.1f : .9f)), Yaw, Indigo, 3);
            Decal(DecalHatch, Arm + Z * H * .55f, FVector2D(R * .3f, R * 1.2f), Yaw, Frost.CopyWithNewOpacity(.7f));
        }
        Tool(0);
        Tool(1);
        break;
    }
    case Foe::Sage:
    {
        if (U.Parts & gwgame::PartsSecondForm)
        {
            // The dragon of wild cursive: a pointed head and its body strung out along its wake.
            TArray<FVector>& Wake = FoeTrails.FindOrAdd(U.Id);
            if (Wake.IsEmpty() || FVector::Dist2D(Wake.Last(), Base) > R * .55f)
            {
                Wake.Add(Base);
                if (Wake.Num() > 16)
                    Wake.RemoveAt(0);
            }
            Part(MeshDiamond, At(.4f, 0, R * .8f), FVector(R * 2.2f, R * 1.1f, R * .9f), Yaw, FoeInk);
            for (const float Sign : {-1.f, 1.f})
                Part(MeshDiamond, At(-.1f, .45f * Sign, R * 1.3f), FVector(R * .9f, R * .2f, R * .2f), Yaw + 150 * Sign, Cinnabar, 1.5f);
            for (int32 K = 0; K < 10 && K < Wake.Num() - 1; ++K)
            {
                const FVector Seg = Wake[Wake.Num() - 2 - K];
                const float Wave = 20 * FMath::Sin(Time * 6 + K);
                Part(MeshSphere, FVector(Seg.X, Seg.Y, R * .7f + Wave), FVector(R * (1.5f - .1f * K)), 0, K % 3 == 1 ? Cinnabar : FMath::Lerp(FoeInk, Pencil, K * .05f), 2);
            }
            Eyes(R * 1.1f, 1.f, .3f);
            break;
        }
        // The white-browed sage in a dry-brush robe, the brass paperweight seal hovering over its head.
        Part(MeshTri, At(0, 0, H * .7f), FVector(R * 2.f, R * 1.9f, H * 1.4f), Yaw, Paper);
        Decal(DecalHatch, At(0, 0, H * 1.4f + 1), FVector2D(R * 1.6f), Yaw, FoeInk.CopyWithNewOpacity(.55f));
        Part(MeshSphere, At(.15f, 0, H * 1.55f), FVector(R * .75f), 0, PaperShade, 3);
        for (const float Sign : {-1.f, 1.f})
            Part(MeshBlock, At(.45f, .2f * Sign, H * 1.7f), FVector(R * .35f, R * .12f, 6), Yaw + 25 * Sign, Paper, 1.5f);
        Eyes(H * 1.6f, .5f, .2f);
        if (U.Parts & 1)
        {
            const float Hover = Act == FoeAct::Leap ? 120 : 30 * FMath::Sin(Time * 2);
            Part(MeshBlock, At(0, 0, H * 2.3f + Hover), FVector(R * 1.1f, R * 1.1f, R * .6f), Yaw + Time * 20, Brass, 3);
            Decal(DecalSquare, At(0, 0, H * 2.3f + Hover + R * .31f), FVector2D(R * .8f), Yaw + Time * 20, Cinnabar.CopyWithNewOpacity(.85f));
            Decal(DecalCompass, FVector(Base.X, Base.Y, 2.4f), FVector2D(R * 1.3f), Time * 70, Marigold.CopyWithNewOpacity(.75f + .2f * FMath::Sin(Time * 6)));
        }
        break;
    }
    // ---- Summons ----
    case Foe::SpiritRed:
    case Foe::SpiritYellow:
    case Foe::SpiritBlue:
    {
        const FLinearColor Glow = Kind == Foe::SpiritRed ? Danger : Kind == Foe::SpiritYellow ? Spark : Cobalt;
        Part(MeshSphere, At(0, 0, R * 1.1f + 8 * FMath::Sin(Time * 4 + Id)), FVector(R * 1.7f), 0, Glow, 2.5f);
        Decal(DecalGlow, FVector(Base.X, Base.Y, 1.4f), FVector2D(R * 3.f), 0, Glow.CopyWithNewOpacity(.45f));
        Eyes(R * 1.5f, .55f);
        break;
    }
    case Foe::GuardBing:
    case Foe::GuardDun:
        // A soldier (兵) and a shield (盾) written in ink: paper squares with a brush mark, the shield's red front.
        Part(MeshBlock, At(0, 0, H * .5f), FVector(R * 1.7f, R * 1.7f, H), Yaw, Paper);
        Decal(Kind == Foe::GuardBing ? DecalPlus : DecalSquare, At(0, 0, H + 1), FVector2D(R * 1.3f), Yaw + 45, FoeInk.CopyWithNewOpacity(.85f));
        if (Kind == Foe::GuardDun)
            Part(MeshBlock, At(1.05f, 0, H * .55f), FVector(R * .25f, R * 2.1f, H * 1.1f), Yaw, Cinnabar);
        Eyes(H * .75f, .6f);
        break;
    default:
        Part(MeshCoin, At(0, 0, H * .5f), FVector(R * 2.f, R * 2.f, H), Yaw, Tone);
        Eyes(H * .7f);
        break;
    }
    // Elites: a gold star over the head and a violet-and-gold ring (design 7.2: 金/紫双色外描边光环 + 顶部精英星标).
    if (Info.rank == gwgame::FoeRank::Elite)
    {
        Solid(MeshStar, O + Z * (H * 1.55f + 16 + 6 * FMath::Sin(Time * 3 + Id)), FVector(R * .55f, R * .55f, 10), Time * 60, Marigold, 0, 2);
        Decal(DecalHexRing, FVector(Base.X, Base.Y, 1.6f), FVector2D(R * 3.4f), Time * 20, Violet.CopyWithNewOpacity(.8f));
        Decal(DecalRing, FVector(Base.X, Base.Y, 1.7f), FVector2D(R * 3.9f), -Time * 15, Marigold.CopyWithNewOpacity(.7f));
    }
}

bool AGWWorld::FoeShotModel(uint8 Weapon, FVector Position, FVector Dir, float Yaw, float Time, int32 Id)
{
    using namespace GWPalette;
    using namespace GWFoeColors;
    switch (Weapon)
    {
    case gwgame::LeadShot:
        // A sharpened lead: a long graphite needle.
        Solid(MeshDiamond, Position, FVector(34, 8, 8), Yaw, Lead, 0, 1.5f);
        Decal(DecalStreak, Position - Dir * 40, FVector2D(70, 8), Yaw, Pencil.CopyWithNewOpacity(.7f));
        return true;
    case gwgame::DropShot:
        // A drop of wash with a wet trail.
        Solid(MeshSphere, Position, FVector(16), 0, Cyan, 0, 1.5f);
        Decal(DecalStreak, Position - Dir * 35, FVector2D(60, 14), Yaw, Wash.CopyWithNewOpacity(.6f));
        return true;
    case gwgame::NibShot:
        // A black nib, very sharp.
        Solid(MeshDiamond, Position, FVector(38, 10, 10), Yaw, Night, 0, 2);
        Solid(MeshDiamond, Position + Dir * 14, FVector(12, 6, 6), Yaw, Brass, 0, 1);
        Decal(DecalStreak, Position - Dir * 55, FVector2D(110, 10), Yaw, FoeInk.CopyWithNewOpacity(.75f));
        return true;
    case gwgame::BeadShot:
        // A ballpoint bead, small and quick.
        Solid(MeshSphere, Position, FVector(12), 0, Blue, 0, 1.5f);
        Decal(DecalStreak, Position - Dir * 30, FVector2D(55, 8), Yaw, Cobalt.CopyWithNewOpacity(.7f));
        return true;
    case gwgame::BlotShot:
        // A blot of brush ink, wobbling.
        Solid(MeshSphere, Position, FVector(20 + 3 * FMath::Sin(Time * 20 + Id), 18, 16), Yaw, FoeInk, 0, 1.5f);
        Decal(DecalDot, Position - Dir * 20 - FVector(0, 0, Position.Z - 2), FVector2D(26), Id * 30.f, FoeInk.CopyWithNewOpacity(.35f));
        return true;
    case gwgame::HeavyShot:
        // A boss's heavy round.
        Solid(MeshSphere, Position, FVector(26), 0, Night, 0, 2.5f);
        Decal(DecalStreak, Position - Dir * 60, FVector2D(120, 22), Yaw, Danger.CopyWithNewOpacity(.6f));
        return true;
    default:
        return false;
    }
}

void AGWWorld::DrawHazards(const AGWGameState* S, float Time)
{
    using gwgame::ZoneKind;
    using namespace GWPalette;
    using namespace GWFoeColors;
    auto Spot = [](uint16 X, uint16 Y) { return FVector(GWNet::UnpackCoord(X), GWNet::UnpackCoord(Y), 0); };
    for (int32 I = 0; I < S->Zones.Num(); ++I)
    {
        const FGWZoneNet& Z = S->Zones[I];
        if (Z.Kind < 10)
            continue;
        const FVector From = Spot(Z.FromX, Z.FromY), To = Spot(Z.ToX, Z.ToY);
        const FVector Mid = (From + To) * .5f;
        const bool Band = FVector::DistSquared2D(From, To) > 1;
        const float Length = FMath::Max(40.f, static_cast<float>(FVector::Dist2D(From, To))), Along = (To - From).Rotation().Yaw;
        // Fresh hazards fade in, dying ones fade out.
        const float Fade = Z.Life > 1e5f ? 1.f : FMath::Clamp(FMath::Min(Z.Remaining / .5f, (Z.Life - Z.Remaining) / .25f + .2f), 0.f, 1.f);
        const float D = Z.Radius * 2;
        auto Lay = [&](int32 Kind, float Up, const FLinearColor& Color, float Width = -1.f, float Turn = 0)
        {
            if (Band)
                Decal(Kind == DecalDot || Kind == DecalGlow ? DecalStreak : Kind, Mid + FVector(0, 0, Up), FVector2D(Length + Z.Radius, Width > 0 ? Width : D), Along + Turn,
                      Color.CopyWithNewOpacity(Color.A * Fade));
            else
                Decal(Kind, From + FVector(0, 0, Up), FVector2D(Width > 0 ? Width : D), Turn, Color.CopyWithNewOpacity(Color.A * Fade));
        };
        switch (static_cast<ZoneKind>(Z.Kind))
        {
        case ZoneKind::Fog:
            Lay(DecalGlow, 1.1f, Pencil.CopyWithNewOpacity(.6f));
            for (int32 K = 0; K < 3; ++K)
                Decal(DecalDot, From + FRotator(0, Time * 25 + K * 120, 0).Vector() * Z.Radius * .45f + FVector(0, 0, 1.3f), FVector2D(Z.Radius * .9f), Time * 10,
                      Pencil.CopyWithNewOpacity(.35f * Fade));
            break;
        case ZoneKind::Dust:
            Lay(DecalHatch, 1.05f, Pencil.CopyWithNewOpacity(.45f));
            Lay(DecalStreak, 1.1f, Pencil.CopyWithNewOpacity(.25f));
            break;
        case ZoneKind::Puddle:
            Lay(DecalDot, 1.1f, Wash.CopyWithNewOpacity(.38f), -1, I * 37.f);
            Lay(DecalRing, 1.2f, Frost.CopyWithNewOpacity(.55f));
            if (Z.Charged)
                Decal(DecalCompass, From + FVector(0, 0, 1.4f), FVector2D(D * 1.05f), Time * 400, Spark.CopyWithNewOpacity(.6f + .35f * FMath::Sin(Time * 40)));
            break;
        case ZoneKind::Paint:
            Lay(DecalDot, 1.12f, (Z.What ? Cyan : Magenta).CopyWithNewOpacity(.45f), -1, I * 41.f);
            Lay(DecalRing, 1.22f, (Z.What ? Cyan : Magenta).CopyWithNewOpacity(.6f));
            break;
        case ZoneKind::Ribbon:
            Decal(DecalStreak, Mid + FVector(0, 0, 1.15f), FVector2D(Length + 20, Z.Radius * 2), Along, (Z.What ? Cyan : Magenta).CopyWithNewOpacity(.7f * Fade));
            break;
        case ZoneKind::Ink:
            Lay(DecalDot, 1.15f, FoeInk.CopyWithNewOpacity(Z.What ? .8f : .6f), -1, I * 29.f);
            if (Z.What)
                Lay(DecalHatch, 1.25f, Cinnabar.CopyWithNewOpacity(.5f));
            break;
        case ZoneKind::Crumbs:
            Lay(DecalDot, 1.1f, FMath::Lerp(Eraser, Pencil, .4f).CopyWithNewOpacity(.55f));
            for (int32 K = 0; K < 4; ++K)
                Solid(MeshBlock, From + FRotator(0, I * 50 + K * 90, 0).Vector() * Z.Radius * .5f + FVector(0, 0, 5), FVector(10, 8, 8), K * 40.f, FMath::Lerp(Eraser, Pencil, .5f), 0, 1);
            break;
        case ZoneKind::Track:
            Decal(DecalStreak, Mid + FVector(0, 0, 1.2f), FVector2D(Length, 16), Along, Cobalt.CopyWithNewOpacity(.55f * Fade));
            break;
        case ZoneKind::Disk:
            // The compass's black disk closing in: filled dark, ringed in ink, a danger compass turning on its edge.
            Decal(DecalDot, From + FVector(0, 0, 1.3f), FVector2D(D), 0, FoeInk.CopyWithNewOpacity(.42f));
            Decal(DecalRing, From + FVector(0, 0, 1.5f), FVector2D(D), 0, FoeInk.CopyWithNewOpacity(.95f));
            Decal(DecalCompass, From + FVector(0, 0, 1.7f), FVector2D(D * 1.02f), Time * 90, Danger.CopyWithNewOpacity(.7f));
            break;
        case ZoneKind::Seal:
            Decal(DecalSquare, From + FVector(0, 0, 1.4f), FVector2D(D * .8f), 45 + Time * 30, Cinnabar.CopyWithNewOpacity(.55f));
            Decal(DecalRing, From + FVector(0, 0, 1.5f), FVector2D(D), 0, Cinnabar.CopyWithNewOpacity(.85f));
            break;
        case ZoneKind::Spring:
            Decal(DecalGlow, From + FVector(0, 0, 1.2f), FVector2D(D), 0, Leaf.CopyWithNewOpacity(.5f * Fade));
            Decal(DecalRing, From + FVector(0, 0, 1.4f), FVector2D(D * (.7f + .3f * FMath::Frac(Time))), 0, Mint.CopyWithNewOpacity(.8f * Fade));
            break;
        case ZoneKind::Whirl:
            for (int32 K = 0; K < 3; ++K)
                Decal(DecalLoop, From + FVector(0, 0, 1.3f + K * .1f), FVector2D(D * (1 - K * .28f)), Time * (120 + 80 * K), Frost.CopyWithNewOpacity(.6f * Fade));
            Decal(DecalCompass, From + FVector(0, 0, 1.6f), FVector2D(D * .4f), -Time * 300, Cobalt.CopyWithNewOpacity(.8f * Fade));
            break;
        case ZoneKind::Clean:
            Decal(DecalDot, From + FVector(0, 0, 1.1f), FVector2D(D), 0, Mint.CopyWithNewOpacity(.4f));
            Decal(DecalLoop, From + FVector(0, 0, 1.25f), FVector2D(D), Time * 12, Paper.CopyWithNewOpacity(.9f));
            break;
        case ZoneKind::Sac:
            // A plump ink sac on the floor, quivering.
            Solid(MeshSphere, From + FVector(0, 0, Z.Radius * .8f + 3 * FMath::Sin(Time * 5 + I)), FVector(Z.Radius * 1.7f, Z.Radius * 1.7f, Z.Radius * 1.5f), 0, Night, 0,
                  3);
            Solid(MeshCoin, From + FVector(0, 0, Z.Radius * 1.6f), FVector(Z.Radius * .6f, Z.Radius * .6f, 10), 0, Brass, 0, 1.5f);
            Decal(DecalShadow, From + FVector(0, 0, .6f), FVector2D(Z.Radius * 2.6f), 0, FLinearColor(0, 0, 0, .5f));
            break;
        case ZoneKind::Carbon:
            Decal(DecalSquare, From + FVector(0, 0, 1.05f), FVector2D(D), 12, Cobalt.CopyWithNewOpacity(.3f));
            Decal(DecalHatch, From + FVector(0, 0, 1.1f), FVector2D(D * .9f), 12, Violet.CopyWithNewOpacity(.35f));
            break;
        case ZoneKind::Cinnabar:
            Decal(DecalRing, From + FVector(0, 0, 1.2f), FVector2D(D), 0, Cinnabar.CopyWithNewOpacity(.85f));
            Decal(DecalSquare, From + FVector(0, 0, 1.25f), FVector2D(D * .55f), Time * 8, Cinnabar.CopyWithNewOpacity(.4f + .15f * FMath::Sin(Time * 3)));
            break;
        case ZoneKind::DryBrush:
            Decal(DecalHatch, Mid + FVector(0, 0, 1.08f), FVector2D(Length + Z.Radius, D), Along, FoeInk.CopyWithNewOpacity(.4f));
            Decal(DecalStreak, Mid + FVector(0, 0, 1.12f), FVector2D(Length + Z.Radius, D * .5f), Along, FoeInk.CopyWithNewOpacity(.3f));
            break;
        case ZoneKind::Thorns:
            for (int32 K = 0; K < 10; ++K)
            {
                const float A = K * 36.f + I * 11;
                Solid(MeshDiamond, From + FRotator(0, A, 0).Vector() * (Z.Radius - 10) + FVector(0, 0, 14), FVector(30, 7, 7), A, Night, 0, 1);
            }
            break;
        default:
            break;
        }
    }
    // The foes' warnings: a ring for what lands round a point, a band for a charge, a line of aim or a sweep.
    for (const auto& E : S->Effects)
    {
        if (E.Style != static_cast<uint8>(gwgame::EffectStyle::FoeCue))
            continue;
        static const FLinearColor Cues[7] = {Danger, FoeInk, Cyan, Spark, Cinnabar, Pencil, Magenta};
        const FLinearColor C = Cues[FMath::Clamp(E.Subject, 0, 6)];
        const float Pulse = .55f + .3f * FMath::Sin(Time * 16);
        const FVector A(E.From.X, E.From.Y, 2.6f), B(E.To.X, E.To.Y, 2.6f);
        if (FVector::DistSquared2D(A, B) < 1)
        {
            Decal(DecalRing, A, FVector2D(E.Radius * 2), 0, C.CopyWithNewOpacity(Pulse));
            Decal(DecalCompass, A + FVector(0, 0, .1f), FVector2D(E.Radius * 2), Time * 120, C.CopyWithNewOpacity(.5f));
            Decal(DecalDot, A - FVector(0, 0, .2f), FVector2D(E.Radius * 2), 0, C.CopyWithNewOpacity(.12f));
        }
        else
        {
            const float Len = static_cast<float>(FVector::Dist2D(A, B));
            const float Turn = (B - A).Rotation().Yaw;
            Decal(DecalStreak, (A + B) * .5f, FVector2D(Len, FMath::Max(10.f, E.Radius * 2)), Turn, C.CopyWithNewOpacity(Pulse * .7f));
            Decal(DecalTriangle, B + FVector(0, 0, .1f), FVector2D(FMath::Max(40.f, E.Radius * 2)), Turn - 90, C.CopyWithNewOpacity(Pulse));
        }
    }
}
