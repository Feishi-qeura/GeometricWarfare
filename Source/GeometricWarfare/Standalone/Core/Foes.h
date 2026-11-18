#pragma once
#include "Expedition.h"
namespace gwgame
{
// The expedition's bestiary (design v2.1 chapter 7): five enemies for each pen biome (25), two elites for each biome
// and two that turn up anywhere (12), one boss per biome (5), and what the bosses bring along (the rainbow's three
// spirits, the sage's written guards). An enemy unit's breed is its Foe. The rules are FoeRules.inl; GWWorld draws each
// one from its own parts.
enum class Foe : uint8_t
{
    // A. Pencil: scribble ball, dashed phantom, shaving darter, draft shield, pencil stub.
    Scribble,
    Phantom,
    Shaver,
    Shield,
    Stub,
    // B. Watercolor: drop slime, two-tone striker, paint sprayer, bubble zapper, sponge.
    Slime,
    Striker,
    Sprayer,
    Zapper,
    Sponge,
    // C. Fountain pen: nib lancer, ink bug, hatch crab, inkwell sniper, paper saw.
    Lancer,
    InkBug,
    Crab,
    Inkwell,
    Saw,
    // D. Ballpoint: bead shooter, red marker, carbon twin, roller tank, abacus gunner.
    Beader,
    Marker,
    Twin,
    Roller,
    Abacus,
    // E. Brush: ink wisp, parasol, seal stone, brush column, pinwheel.
    Wisp,
    Parasol,
    Seal,
    Column,
    Pinwheel,
    // Elites EL01-EL12 (two per biome in biome order, then the sticker thief and the mash-up that turn up anywhere).
    Leadcannon,
    Crumbler,
    Palette,
    Eel,
    Swordsaint,
    Inspector,
    Clicker,
    Mirror,
    Wildscript,
    Judge,
    Thief,
    Mashup,
    // The biome bosses.
    Draftsman,
    Rainbow,
    Editor,
    Warlord,
    Sage,
    // Summons: the rainbow's red, yellow and blue spirits; the sage's written soldier (兵) and shield (盾).
    SpiritRed,
    SpiritYellow,
    SpiritBlue,
    GuardBing,
    GuardDun,
    Count
};
constexpr int FoeCount = static_cast<int>(Foe::Count), FoeNormals = 25, FoeElites = 12, FoeBosses = 5;
enum class FoeRank : uint8_t
{
    Normal,
    Elite,
    Boss,
    Summon
};
struct FoeInfo
{
    // The biome it belongs to (PenBiome; -1 anywhere), its rank, the shape it is hit as, its size, health before the
    // stage and tier scale it, pace, the bite of its body, and a boss's erasable parts (a bit each) as the fight begins.
    int biome;
    FoeRank rank;
    Shape shape;
    double radius, health, speed, melee;
    uint8_t parts;
};
inline const FoeInfo& foeInfo(int foe)
{
    using R = FoeRank;
    using S = Shape;
    static const FoeInfo Infos[FoeCount] = {
        {0, R::Normal, S::Circle, 22, 45, 270, 9, 0},
        {0, R::Normal, S::Triangle, 24, 55, 230, 12, 0},
        {0, R::Normal, S::Hexagon, 24, 70, 150, 7, 0},
        {0, R::Normal, S::Square, 30, 150, 115, 14, 0},
        {0, R::Normal, S::Rectangle, 17, 40, 95, 4, 0},
        {1, R::Normal, S::Circle, 26, 80, 200, 10, 0},
        {1, R::Normal, S::Square, 22, 60, 330, 11, 0},
        {1, R::Normal, S::Trapezoid, 26, 75, 140, 7, 0},
        {1, R::Normal, S::Circle, 24, 55, 170, 6, 0},
        {1, R::Normal, S::Square, 30, 140, 105, 10, 0},
        {2, R::Normal, S::Triangle, 22, 65, 190, 9, 0},
        {2, R::Normal, S::Pentagon, 26, 70, 105, 8, 0},
        {2, R::Normal, S::Trapezoid, 30, 160, 135, 12, 0},
        {2, R::Normal, S::Hexagon, 26, 80, 95, 6, 0},
        {2, R::Normal, S::Circle, 22, 60, 380, 14, 0},
        {3, R::Normal, S::Circle, 24, 70, 200, 7, 0},
        {3, R::Normal, S::Pentagon, 26, 75, 165, 7, 0},
        {3, R::Normal, S::Triangle, 24, 90, 185, 10, 0},
        {3, R::Normal, S::Rectangle, 32, 180, 110, 18, 0},
        {3, R::Normal, S::Rectangle, 28, 100, 120, 8, 0},
        {4, R::Normal, S::Circle, 24, 65, 215, 10, 0},
        {4, R::Normal, S::Triangle, 28, 120, 120, 12, 0},
        {4, R::Normal, S::Square, 34, 220, 80, 16, 0},
        {4, R::Normal, S::Rectangle, 24, 90, 100, 8, 0},
        {4, R::Normal, S::Square, 28, 110, 85, 9, 0},
        {0, R::Elite, S::Hexagon, 46, 420, 105, 16, 0},
        {0, R::Elite, S::Square, 52, 560, 80, 22, 0},
        {1, R::Elite, S::Circle, 48, 460, 90, 14, 0},
        {1, R::Elite, S::Circle, 34, 400, 270, 14, 0},
        {2, R::Elite, S::Triangle, 42, 440, 170, 18, 0},
        {2, R::Elite, S::Hexagon, 46, 520, 80, 16, 0},
        {3, R::Elite, S::Circle, 46, 480, 90, 14, 0},
        {3, R::Elite, S::Hexagon, 44, 420, 115, 12, 0},
        {4, R::Elite, S::Pentagon, 50, 520, 120, 18, 0},
        {4, R::Elite, S::Square, 46, 480, 100, 16, 0},
        {-1, R::Elite, S::Triangle, 30, 300, 330, 0, 0},
        {-1, R::Elite, S::Pentagon, 48, 500, 140, 16, 0},
        {0, R::Boss, S::Rectangle, 112, 3400, 100, 34, 3},
        {1, R::Boss, S::Circle, 110, 3600, 110, 30, 0},
        {2, R::Boss, S::Pentagon, 105, 3500, 125, 38, 3},
        {3, R::Boss, S::Hexagon, 112, 3700, 105, 34, 3},
        {4, R::Boss, S::Triangle, 108, 4300, 110, 36, 1},
        {1, R::Summon, S::Circle, 30, 260, 260, 12, 0},
        {1, R::Summon, S::Circle, 30, 260, 120, 8, 0},
        {1, R::Summon, S::Circle, 30, 260, 140, 8, 0},
        {4, R::Summon, S::Square, 26, 120, 170, 12, 0},
        {4, R::Summon, S::Square, 30, 220, 110, 10, 0}};
    static const FoeInfo None{-1, R::Normal, S::Circle, 24, 60, 200, 10, 0};
    return foe >= 0 && foe < FoeCount ? Infos[foe] : None;
}
inline bool bossFoe(int foe)
{
    return foe >= 0 && foe < FoeCount && foeInfo(foe).rank == FoeRank::Boss;
}
inline bool eliteFoe(int foe)
{
    return foe >= 0 && foe < FoeCount && foeInfo(foe).rank == FoeRank::Elite;
}
// A biome's five enemies, its two elites and its boss.
inline Foe biomeFoe(PenBiome biome, int index)
{
    return static_cast<Foe>(std::clamp(static_cast<int>(biome), 0, BiomeCount - 1) * 5 + std::clamp(index, 0, 4));
}
inline Foe biomeElite(PenBiome biome, int index)
{
    return static_cast<Foe>(FoeNormals + std::clamp(static_cast<int>(biome), 0, BiomeCount - 1) * 2 + std::clamp(index, 0, 1));
}
inline Foe biomeBoss(PenBiome biome)
{
    return static_cast<Foe>(FoeNormals + FoeElites + std::clamp(static_cast<int>(biome), 0, BiomeCount - 1));
}

// What a foe is doing (Unit::act). Clients draw it: a wind-up glows and shakes, a phantom fades, a fuse swells, a
// clicker wears its ink colour (Blue..Black).
enum class FoeAct : uint8_t
{
    Idle,
    Windup,
    Dash,
    Hidden,
    Fuse,
    Spikes,
    Jam,
    Parry,
    Spin,
    Guard,
    Hop,
    Leap,
    Split,
    Broken,
    Aim,
    Write,
    Open,
    Blue = 20,
    Red,
    Green,
    Black
};
// Enemy shots by look (Projectile::weapon; the client draws each its own way): an ink dart, a pencil lead, a water drop,
// a pen nib, a ballpoint bead, a brush blot, a boss's heavy round, and a lobbed blob that bursts where it lands.
constexpr int DartShot = 9, LeadShot = 13, DropShot = 14, NibShot = 15, BeadShot = 16, BlotShot = 17, HeavyShot = 18, FoeLob = 19;
inline bool foeShot(int weapon)
{
    return weapon == DartShot || (weapon >= LeadShot && weapon <= FoeLob);
}
// What an enemy shot leaves on a player it hits (Projectile::effect); a lobbed blob's effect is what it leaves where it
// lands (a puddle, a paint pool of either colour, a burst of eight blots).
enum class ShotEffect : uint8_t
{
    None,
    Bleed,
    Slow,
    Smear,
    Stun,
    Puddle,
    PaintA,
    PaintB,
    Burst8,
    Rock
};
constexpr double FoeLobBlast = 120, BleedSeconds = 3, CircleSeconds = 5, SealSeconds = 4, SmearSeconds = 2, ThiefSeconds = 15;
// A boss's erasable parts, each at a place on its body (forward and sideways in radii; sideways along the aim turned a
// quarter left, (-aim.y, aim.x)): the draftsman's pencil gatling and gold compass, the editor's revolver and paper
// blade, the warlord's four-colour gatling and carbon shield, the sage's brass paperweight seal over its head.
struct FoePart
{
    double forward, side;
};
inline FoePart foePart(int foe, int bit)
{
    switch (static_cast<Foe>(foe))
    {
    case Foe::Draftsman:
    case Foe::Editor:
        return bit == 0 ? FoePart{.3, -1.05} : FoePart{.3, 1.05};
    case Foe::Warlord:
        return bit == 0 ? FoePart{.25, 1.05} : FoePart{.55, -.95};
    case Foe::Sage:
        return {0, 0};
    default:
        return {0, 0};
    }
}
inline Vec2 foePartAt(int foe, int bit, Vec2 position, Vec2 aim, double radius)
{
    const FoePart p = foePart(foe, bit);
    const Vec2 a = aim.normalized(), side{-a.y, a.x};
    return position + a * (p.forward * radius) + side * (p.side * radius);
}
// A boss's parts byte on the wire: the parts it still has (bits 0-1), its second form (bit 7: the sage's dragon) and a
// split body (bit 6: the rainbow while its spirits are out).
constexpr uint8_t PartsSecondForm = 0x80, PartsSplit = 0x40;
} // namespace gwgame
