#pragma once
#include "CoreMinimal.h"

// One sketchbook palette for the paper HUD, the 3D Evolution diorama and Niagara cues.
// Values are authored as sRGB hex (how an illustrator picks them) and converted to linear.
namespace GWPalette
{
inline FLinearColor Hex(uint32 RGB, float Alpha = 1)
{
    return FLinearColor(FColor((RGB >> 16) & 255, (RGB >> 8) & 255, RGB & 255)).CopyWithNewOpacity(Alpha);
}
inline const FLinearColor Paper = Hex(0xF6F0E1), PaperShade = Hex(0xEADFC8), PaperDeep = Hex(0xD9CAAC);
inline const FLinearColor Ink = Hex(0x1F2633), InkSoft = Hex(0x4A5262), Pencil = Hex(0x8C909A), Grid = Hex(0x9DB5D3);
inline const FLinearColor Coral = Hex(0xE8553F), Cobalt = Hex(0x2F6BD8), Marigold = Hex(0xF2A93B), Teal = Hex(0x26A796);
inline const FLinearColor Mint = Hex(0x9AD8CB), Plum = Hex(0x4B3F72), Magenta = Hex(0xB23A7A), Night = Hex(0x2A2140);
inline const FLinearColor Violet = Hex(0x8E5CD9), Frost = Hex(0x5EC8E8), Fire = Hex(0xFF6A2B), Volt = Hex(0x6E6BFF);
inline const FLinearColor Spark = Hex(0xFFD84A), Leaf = Hex(0x4DBA5C), Kraft = Hex(0xD6BC90), Danger = Hex(0xE23A33);
// The box's platform is a steel ruler laid round the page: cool grey card with ink ticks, clear of kraft walls and
// graphite rock.
inline const FLinearColor Ruler = Hex(0xC5CCD6), RulerEdge = Hex(0x8E98A8);

inline FLinearColor Team(int32 Team)
{
    return Team == 1 ? Coral : Team == 2 ? Cobalt : Marigold;
}
// Eight seat colours for up to eight players (design doc: 8 人同屏的可读性): distinct hues at marker
// strength on cream paper, kept clear of the dark purples every enemy wears. Seat 0 is the host's coral.
inline FLinearColor Seat(int32 Index)
{
    static const uint32 Colors[8] = {0xE8553F, 0x2F6BD8, 0xD9A10E, 0x3A9E45, 0xE0559B, 0x17A3BF, 0x8E5B2E, 0x94B81A};
    return Index < 0 ? InkSoft : Hex(Colors[Index % 8]);
}
// The expedition's five pen biomes (gwgame::PenBiome): pencil graphite, a watercolour wash, fountain-pen indigo,
// ballpoint blue and the brush's vermilion seal. Floors, rooms and HUD badges of a stage wear its pen's colour.
inline FLinearColor Biome(int32 Pen)
{
    static const uint32 Colors[5] = {0x6B7280, 0x22A9BE, 0x3B2F8F, 0x2F6BD8, 0xC23B2A};
    return Hex(Colors[Pen < 0 ? 0 : Pen > 4 ? 4 : Pen]);
}
// The expedition's five qualities (gwgame::Quality, 2026-11-16): green, blue, purple, orange and red as game players
// read them, at marker strength on cream paper; 0 (an armoury gun, a look) is plain graphite.
inline FLinearColor Grade(int32 Quality)
{
    static const uint32 Colors[6] = {0x8C909A, 0x3FA34D, 0x2F6BD8, 0x8E5CD9, 0xF08A24, 0xE23A33};
    return Hex(Colors[Quality < 0 ? 0 : Quality > 5 ? 5 : Quality]);
}
// The expedition's coins: brass hexagon tokens.
inline const FLinearColor CoinBrass = Hex(0xC99A2E), CoinLight = Hex(0xF2CF63);
// Five upgrade families: ballistics, charge, fire, ice and shield.
inline FLinearColor SkillFamily(int32 Skill)
{
    if (Skill < 0)
        return Leaf;
    switch (Skill / 4)
    {
    case 0:
        return Hex(0xF59E2E);
    case 1:
        return Volt;
    case 2:
        return Fire;
    case 3:
        return Hex(0x4FB9E3);
    default:
        return Teal;
    }
}
} // namespace GWPalette
