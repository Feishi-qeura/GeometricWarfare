#pragma once
#include "Defense.h"
#include <map>
#include <string>
namespace gwgame
{
// Meta progression (局外养成, design doc phase 4): what a player keeps between missions. Unlocks (weapons, pens,
// items) and research ranks travel into every room whole (each player's own ranks count for that player, whatever the
// room's tier: the user's call of 2026-11-16 lifted the old tier cap); hats, titles and achievements are for show. Everything here is plain data and rules shared by the profile (written only by
// its own player's machine) and the host (which checks what a guest reports, caps it and computes rewards).

// ---- Research desk (研究台): seven attributes, five ranks each ----
enum class Attribute : uint8_t
{
    Health,
    Damage,
    Ink,
    Throw,
    Speed,
    Reach,
    // The expedition's backpack (背包扩容, 2026-11-16): one more bag slot per rank (Expedition.h packSlots).
    Pack,
    // The dodge (2026-11-17): its cooldown shorter (冲刺冷却) and its kick stronger (冲刺速度), rank by rank.
    DashCooldown,
    DashSpeed,
    Count
};
constexpr int AttributeCount = static_cast<int>(Attribute::Count);
constexpr int MaxRank = 5;
using Ranks = std::array<uint8_t, AttributeCount>;
// Per rank (design doc table): health +5%, damage +4%, ink capacity +6%, throw power +5%, speed +2%, pickup +6%; the
// backpack is counted in slots, not percent (attributeStep 0: attributeScale leaves it at 1); the dodge's cooldown -8%
// (a negative step: the scale shrinks) and its kick +6%.
inline double attributeStep(Attribute a)
{
    static const double Steps[AttributeCount] = {.05, .04, .06, .05, .02, .06, 0, -.08, .06};
    const int i = static_cast<int>(a);
    return i >= 0 && i < AttributeCount ? Steps[i] : 0;
}
// A rank outside 0-5 from a client counts as rank 0 (design doc: 不合法的项按 0 级处理).
inline int sanitizeRank(int rank)
{
    return rank >= 0 && rank <= MaxRank ? rank : 0;
}
inline double attributeScale(Attribute a, int rank)
{
    return 1 + attributeStep(a) * sanitizeRank(rank);
}
// Stickers that buy the next rank (current 0 buys rank 1); -1 once the attribute is full. 1180 per attribute.
inline int researchCost(int current)
{
    static const int Costs[MaxRank] = {60, 120, 200, 320, 480};
    return current >= 0 && current < MaxRank ? Costs[current] : -1;
}

// ---- Difficulty tiers (难度档) ----
constexpr int MinTier = 1, MaxTier = 5;
struct TierInfo
{
    // reward: the sticker multiplier; enemyHealth / enemyDamage: how much tougher and harder-hitting enemies are;
    // elite: the extra share of elites.
    double reward, enemyHealth, enemyDamage, elite;
};
inline int clampTier(int tier)
{
    return std::clamp(tier, MinTier, MaxTier);
}
inline const TierInfo& tierInfo(int tier)
{
    static const TierInfo Tiers[MaxTier] = {
        {1.0, 1.0, 1.0, 0}, {1.3, 1.25, 1.1, .02}, {1.7, 1.55, 1.2, .04}, {2.2, 1.9, 1.35, .06}, {3.0, 2.3, 1.5, .08}};
    return Tiers[clampTier(tier) - 1];
}
// The rank that counts in a room (the design doc's min(rank, tier) cap was lifted on 2026-11-16 at the user's request:
// research is the player's own, in every room and in every party). The tier stays in the signature for callers.
inline int effectiveRank(int rank, int tier)
{
    (void)tier;
    return sanitizeRank(rank);
}
// Stickers per cleared wave and for a page's first clear, both at the room's tier multiplier. The expedition refactor
// (design v2.1, 2.1) halves line defense to a steady starter's income: (4 + wave) a wave, about 95-130 for a page of ten.
inline int waveStickers(int wave, int tier)
{
    return static_cast<int>(std::lround((4 + std::max(0, wave)) * tierInfo(tier).reward));
}
inline int firstClearStickers(int tier)
{
    return static_cast<int>(std::lround(50 * tierInfo(tier).reward));
}
// The expedition is where stickers come from (high risk, high return): each player banks what their pouch holds when
// they walk out alive, plus this bonus through the last boss's exit door. A wipe banks nothing.
constexpr double ExpeditionClearBase = 150;
inline int expeditionClearBonus(int tier)
{
    return static_cast<int>(std::lround(ExpeditionClearBase * tierInfo(tier).reward));
}
// The party playlist is the light, silly option: its missions pay 55% of what they would (the stickers they settle
// are cut by 45%), and the room's MVP gets 20 on top.
constexpr double PlaylistShare = .55;
constexpr int PlaylistMvpBonus = 20;
inline int playlistStickers(int stickers)
{
    return static_cast<int>(std::lround(std::max(0, stickers) * PlaylistShare));
}

// ---- Shop (商店): weapons, pens and single-mission items ----
// weaponStats() kinds 0-5; the pistol (0) is everyone's from the start. Unlocking a weapon lets a player start
// every mission holding it (crates still hand out any weapon during a run).
constexpr int WeaponCount = 6;
inline int weaponPrice(int weapon)
{
    static const int Prices[WeaponCount] = {0, 150, 200, 250, 300, 400};
    return weapon >= 0 && weapon < WeaponCount ? Prices[weapon] : -1;
}
// A pen draws one wall type (design doc 墙型): the fountain pen's plain wall, the marker's thick wall, the
// highlighter's glossy bounce wall. A pen's ink is per cell, relative to the plain wall's 10.
constexpr int PenCount = 3;
inline int penPrice(int pen)
{
    static const int Prices[PenCount] = {0, 250, 350};
    return pen >= 0 && pen < PenCount ? Prices[pen] : -1;
}
inline double penInk(int pen)
{
    static const double Ink[PenCount] = {1., 1.6, 1.3};
    return pen >= 0 && pen < PenCount ? Ink[pen] : 1.;
}
// Single-mission items: bought with stickers, at most two brought into a mission, used up when it starts. All of
// them work by themselves, so they need no key (design doc: 新模式只换规则，不加新按键).
enum class Item : uint8_t
{
    InkBottle, // ink capacity +40% for the mission
    Tape,      // the core gets +150 health
    Pouch,     // the tower kit: the first tower costs no ink
    Plaster,   // the first fall: back up on the spot after 2 s at half health
    Magnet,    // kills pay a quarter more ink, and ink lying around is picked up from three times as far
    Carbon,    // walls cost a quarter less ink
    Count
};
constexpr int ItemCount = static_cast<int>(Item::Count), ItemSlots = 2, MaxItemStack = 9;
inline int itemPrice(int item)
{
    static const int Prices[ItemCount] = {40, 60, 50, 70, 40, 50};
    return item >= 0 && item < ItemCount ? Prices[item] : -1;
}
constexpr double BottleInk = 1.4, TapeCore = 150, MagnetReach = 3, MagnetInk = 1.25, CarbonInk = .75, PlasterDelay = 2;
using ItemSlotsArray = std::array<int, ItemSlots>;

// ---- Wardrobe (衣柜): hats and titles, looks only ----
// 0 bare head, 1 paper hat, 2 beanie, 3 cap, 4 top hat, 5 antenna, 6 pencil, 7 crown (an achievement's prize).
constexpr int HatCount = 8;
inline int hatPrice(int hat)
{
    static const int Prices[HatCount] = {0, 100, 150, 150, 250, 200, 300, -1};
    return hat >= 0 && hat < HatCount ? Prices[hat] : -1;
}
// The end-of-mission titles (design doc 颁奖); winning one opens it as a title in the wardrobe.
enum class Award : uint8_t
{
    MVP,
    Bait,
    Cannonball,
    Walls,
    Scapegoat,
    Count
};
constexpr int AwardCount = static_cast<int>(Award::Count);
// Titles: 0 none; 1-5 the five awards; 6 sharpshooter, 7 doctor, 8 margin keeper, 9 sticker tycoon, 10 ink veteran
// come with achievements.
constexpr int TitleCount = 11;
inline int awardTitle(Award a)
{
    return 1 + static_cast<int>(a);
}
// Who wins each award: the player with the highest score above zero (the first in order on a tie); at most four are
// handed out, in the order MVP (damage), best bait (held), human cannonball (thrown), wall artist (walls), scapegoat
// (shoves). scores[i][a] is player i's score for award a; the result names each award's winner (index or -1). The
// result card draws exactly these, and the host grants the matching titles.
using AwardScores = std::array<int, AwardCount>;
inline AwardScores awardWinners(const std::vector<AwardScores>& scores)
{
    AwardScores winners;
    winners.fill(-1);
    int given = 0;
    for (int a = 0; a < AwardCount; ++a)
    {
        int best = 0;
        for (size_t i = 0; i < scores.size(); ++i)
            if (scores[i][a] > best)
            {
                best = scores[i][a];
                winners[a] = static_cast<int>(i);
            }
        if (winners[a] >= 0 && given++ >= 4)
            winners[a] = -1;
    }
    return winners;
}

// ---- The expedition's feats: one-off moments of a run, a bit each (PlayerStats::feats; Counters keeps every one ever) ----
enum class Feat : uint8_t
{
    FloorCleared, // a floor of the expedition behind you
    FinalExit,    // out through the last boss's door
    EarlyRich,    // out through an early exit door with 500 stickers or more
    BigBank,      // 1000 stickers or more banked in one run
    Dragon,       // the sage's dragon felled
    Disarm,       // a boss's part rubbed out with an eraser
    Toolless,     // the editor's last weapon rubbed out
    HalfErase,    // half a skill boss rubbed out in one swipe
    Revive,       // a teammate drawn back
    TinyRevive,   // a teammate drawn back with a sliver of a shape (under a tenth of the sheet)
    BorrowedPen,  // a teammate drawn back with a borrowed pen
    Pair,         // a pistol in each hand (2026-11-17: long guns take both hands)
    StaplerPair,  // a stapler pistol in each hand (was two rocket launchers, no longer possible)
    AllLooks,     // a look in every one of the six categories at once
    Thief,        // the sticker thief caught
    Arena,        // the thirty-second arena beaten
    Chained,      // a chained chest held open
    CopyEraser,   // an eraser copied
    BigSpin,      // the big wheel spun
    Reforge2,     // two nibs reforged in one run
    Vote4,        // a prank ballot with four players or more
    Flawless,     // a boss felled with nobody falling in the fight
    TeamExit,     // out through the last door with four players or more in the run
    Tier3Exit,    // out through the last door on tier 3 or higher
    WipedRich,    // wiped out with 300 stickers or more in the pouch
    Count
};
constexpr int FeatCount = static_cast<int>(Feat::Count);
constexpr uint64_t featBit(Feat f)
{
    return uint64_t(1) << static_cast<int>(f);
}

// ---- Achievements (成就墙): a 64-bit map per player; 0-31 the pages and the room, 32-63 the expedition ----
enum class Achievement : uint8_t
{
    FirstStroke,    // draw a wall
    WallApprentice, // draw 300 walls
    FirstPage,      // clear page 1-1
    FirstNotebook,  // clear every page of chapter 1
    FullMarks,      // three stars on a page
    NineStars,      // every star of chapter 1
    Harder,         // clear a page on tier 2 or higher
    Tough,          // clear a page on tier 3 or higher
    Veteran,        // clear a page on tier 5
    LiftOff,        // be thrown by a teammate
    Cannonball,     // be thrown 25 times
    Pitcher,        // throw teammates 25 times
    Bait,           // hold 10 enemies as bait
    ProBait,        // hold 200 enemies as bait
    Bullseye,       // hit the training bullseye
    Sharpshooter,   // 25 bullseyes
    GoodMate,       // pour 300 ink to teammates
    FirstStudy,     // buy a research rank
    Specialist,     // an attribute at rank 5
    Doctorate,      // every attribute at rank 5
    Armory,         // own every weapon
    Stationery,     // own every pen
    HatCollector,   // own four hats
    Tycoon,         // earn 5000 stickers in all
    Thousand,       // defeat 1000 enemies
    MVP,            // win the MVP award
    Scapegoat,      // win the scapegoat award
    Party,          // play a mission with four or more players
    FullHouse,      // play a mission with eight players
    LoneWolf,       // clear a page alone, without AI teammates
    Untouched,      // clear chapter 1's last page with the core untouched
    Supplied,       // bring two items into a mission
    // The expedition (design v2.1 9.1: the 32 spare bits).
    ExpFirstFloor,  // clear an expedition floor
    ExpPencilBoss,  // fell the drafting table (the pencil's boss)
    ExpWalkOut,     // walk out through the last boss's door
    ExpEarlyRich,   // leave by an early exit door with 500 stickers
    ExpBigBank,     // bank 1000 stickers in one run
    ExpAllBosses,   // fell all five biome bosses
    ExpDragon,      // fell the sage's dragon
    ExpDisarm,      // rub out a boss's part
    ExpBareHands,   // leave the editor empty-handed
    ExpHalfErase,   // rub out half a skill boss in one swipe
    ExpEraser10,    // rub out 10 enemies with erasers
    ExpRevive,      // draw a teammate back
    ExpTinyRevive,  // draw a teammate back with a sliver of a shape
    ExpGeometry,    // earn the circle, triangle and rectangle bonuses
    ExpBorrowed,    // draw a teammate back with a borrowed pen
    ExpPair,        // hold the same gun in both hands
    ExpStaplerPair, // hold a stapler pistol in each hand
    ExpAllLooks,    // wear a look in all six categories at once
    ExpElites,      // fell 25 elites
    ExpThief,       // catch the sticker thief
    ExpArena,       // beat the thirty-second arena
    ExpChained,     // hold a chained chest open
    ExpCopier,      // copy an eraser
    ExpBigSpin,     // spin the big wheel
    ExpReforge,     // reforge two nibs in one run
    ExpVote,        // finish a prank ballot with four or more
    ExpGenerous,    // drop 200 stickers that teammates pick up
    ExpFlawless,    // fell a boss with nobody falling
    ExpTeamExit,    // walk out of the last door with four or more in the run
    ExpTier3,       // walk out of the last door on tier 3 or higher
    ExpRich,        // bank 10000 stickers from expeditions in all
    ExpWiped,       // lose 300 stickers in a wipe
    Count
};
constexpr int AchievementCount = static_cast<int>(Achievement::Count);
static_assert(AchievementCount <= 64, "the room's achievement map is 64 bits");
constexpr uint64_t AchievementMask = AchievementCount >= 64 ? ~uint64_t(0) : (uint64_t(1) << (AchievementCount % 64)) - 1;
struct AchievementInfo
{
    // Stickers it pays once (design doc: 贴纸 come from waves, first clears and achievements); the hat and the
    // title it opens (0 = none).
    int stickers, hat, title;
};
inline const AchievementInfo& achievementInfo(int index)
{
    static const AchievementInfo Infos[AchievementCount] = {
        {25, 0, 0},  {50, 0, 0},  {25, 0, 0},  {100, 0, 8}, {50, 0, 0},  {100, 0, 0}, {50, 0, 0},  {100, 0, 0},
        {200, 7, 10}, {25, 0, 0}, {50, 0, 0},  {50, 0, 0},  {25, 0, 0},  {100, 0, 0}, {25, 0, 0},  {50, 0, 6},
        {50, 0, 0},  {25, 0, 0},  {50, 0, 0},  {200, 0, 7}, {100, 0, 0}, {50, 0, 0},  {50, 0, 0},  {100, 0, 9},
        {100, 0, 0}, {50, 0, 0},  {50, 0, 0},  {50, 0, 0},  {100, 0, 0}, {50, 0, 0},  {50, 0, 0},  {25, 0, 0},
        // The expedition's.
        {25, 0, 0},  {50, 0, 0},  {150, 0, 0}, {75, 0, 0},  {100, 0, 0}, {250, 0, 0}, {150, 0, 0}, {50, 0, 0},
        {100, 0, 0}, {75, 0, 0},  {75, 0, 0},  {25, 0, 0},  {50, 0, 0},  {100, 0, 0}, {50, 0, 0},  {25, 0, 0},
        {75, 0, 0},  {50, 0, 0},  {100, 0, 0}, {75, 0, 0},  {50, 0, 0},  {50, 0, 0},  {50, 0, 0},  {25, 0, 0},
        {50, 0, 0},  {50, 0, 0},  {75, 0, 0},  {100, 0, 0}, {100, 0, 0}, {150, 0, 0}, {200, 0, 0}, {25, 0, 0}};
    static const AchievementInfo None{0, 0, 0};
    return index >= 0 && index < AchievementCount ? Infos[index] : None;
}

// ---- Difficulty unlocks ----
// Three notebooks of three pages (27 stars). The doc names 12 stars across chapters 1-3 and chapter 3's last page;
// the gates (Claude's call, 2026-11-11): six stars anywhere open tier 2, twelve open tier 3, the last page (3-3)
// cleared on tier 3 opens tier 4, and cleared on tier 4 opens tier 5.
// The expedition keeps one record of its own, beside the pages.
constexpr const char* ExpeditionId = "EX";
inline bool knownPage(const std::string& page)
{
    if (page == ExpeditionId)
        return true;
    for (const auto& level : defenseLevels())
        if (page == level.id)
            return true;
    return false;
}
inline std::string lastPage(const std::string& chapter)
{
    std::string last;
    for (const auto& level : defenseLevels())
        if (std::string(level.id).rfind(chapter, 0) == 0)
            last = level.id;
    return last;
}

// A page record: bits 0-2 the best stars, bits 3-5 the highest tier it was cleared on, bit 7 cleared. Records
// written before tiers existed carry no tier and count as cleared on tier 1.
constexpr uint8_t ClearedBit = 0x80;
// The three star bits (core untouched, few walls lost, no AI teammates), and how many of them are set.
inline int recordStars(uint8_t bits)
{
    return bits & 7;
}
inline int countStars(uint8_t bits)
{
    return (bits & 1) + ((bits >> 1) & 1) + ((bits >> 2) & 1);
}
inline int recordTier(uint8_t bits)
{
    return (bits & ClearedBit) ? std::max(1, (bits >> 3) & 7) : 0;
}

// What a player tells a room about their profile (the doc's 档案摘要): it is all the host ever sees of it.
struct Loadout
{
    Ranks ranks{};
    int weapon = 0, pen = 0, hat = 0, title = 0;
    ItemSlotsArray items{{-1, -1}};
    uint64_t achievements = 0;
    // The weapons the player owns (a bit per kind; the pistol always): the expedition's loot favours them.
    uint32_t weapons = 1;
};
// The host's check on a guest's report (进房校验): anything outside the game's lists counts as nothing.
inline Loadout sanitize(const Loadout& in)
{
    Loadout out = in;
    for (auto& rank : out.ranks)
        rank = static_cast<uint8_t>(sanitizeRank(rank));
    if (out.weapon < 0 || out.weapon >= WeaponCount)
        out.weapon = 0;
    if (out.pen < 0 || out.pen >= PenCount)
        out.pen = 0;
    if (out.hat < 0 || out.hat >= HatCount)
        out.hat = 0;
    if (out.title < 0 || out.title >= TitleCount)
        out.title = 0;
    for (int& item : out.items)
        if (item < 0 || item >= ItemCount)
            item = -1;
    out.achievements &= AchievementMask;
    out.weapons = (out.weapons & ((1u << WeaponCount) - 1)) | 1u;
    if (!((out.weapons >> out.weapon) & 1u))
        out.weapons |= 1u << out.weapon;
    return out;
}

// Lifetime numbers the achievements read. The host sends each player their own share of every mission.
struct Counters
{
    int64_t kills = 0, damage = 0, walls = 0, thrown = 0, throws = 0, held = 0, inkGiven = 0, bullseyes = 0;
    int64_t missions = 0, wins = 0, loneWins = 0;
    int maxParty = 0, maxItems = 0;
    std::array<int, AwardCount> awards{};
    // The expedition: every feat ever earned (a bit each), enemies erased, elites felled, stickers dropped for teammates,
    // stickers banked on the way out, the biome bosses felled and the revive shape bonuses drawn (a bit each).
    uint64_t feats = 0;
    int64_t erased = 0, elites = 0, given = 0, banked = 0;
    int bosses = 0, shapes = 0;
};
// The most an expedition's exit may settle in one packet (a whole run's pouch at once).
constexpr int ExpeditionRewardCap = 5000;
// One player's share of a mission since the host's last packet (a wave, the end, a bullseye in the hub).
struct Tally
{
    int kills = 0, damage = 0, walls = 0, thrown = 0, throws = 0, held = 0, inkGiven = 0, bullseyes = 0;
    // The end of a mission: how it went, who was there, what this player brought and won.
    bool final = false, won = false, bots = false;
    int players = 0, items = 0;
    uint8_t awards = 0;
    // The expedition's share (PlayerStats): feats, erased, elites, stickers given and banked, bosses and shape bits.
    uint64_t feats = 0;
    int erased = 0, elites = 0, given = 0, banked = 0, bosses = 0, shapes = 0;
};
// What one packet may carry at most; a packet beyond any bound is dropped whole (design doc: 奖励包上限).
inline bool plausible(const Tally& t)
{
    auto in = [](int value, int most) { return value >= 0 && value <= most; };
    return in(t.kills, 3000) && in(t.damage, 3000000) && in(t.walls, 3000) && in(t.thrown, 500) && in(t.throws, 500) && in(t.held, 3000) &&
           in(t.inkGiven, 200000) && in(t.bullseyes, 500) && in(t.players, 8) && in(t.items, ItemSlots) && t.awards < (1 << AwardCount) &&
           t.feats < (uint64_t(1) << FeatCount) && in(t.erased, 500) && in(t.elites, 500) && in(t.given, 100000) &&
           in(t.banked, ExpeditionRewardCap) && in(t.bosses, 31) && in(t.shapes, 7);
}

// A player's profile as rules see it (the UE save mirrors these fields).
struct MetaProfile
{
    // A reward packet above its cap is dropped whole (design doc: 奖励包上限). An expedition's exit settles a whole run's
    // pouch at once (400-1500 and more on high tiers), so its record has a cap of its own.
    static constexpr int MaxStickersPerReward = 400, MaxExpeditionStickersPerClear = ExpeditionRewardCap;
    static int rewardCap(const std::string& page)
    {
        return page == ExpeditionId ? MaxExpeditionStickersPerClear : MaxStickersPerReward;
    }
    int stickers = 0, stickersTotal = 0, wavesCleared = 0, levelsCleared = 0;
    std::map<std::string, uint8_t> results;
    Ranks ranks{};
    // Bit per weapon, pen, hat and title owned (each list's 0 is always owned); item stocks.
    uint32_t weapons = 1, pens = 1, hats = 1, titles = 1;
    std::array<uint8_t, ItemCount> items{};
    uint64_t achievements = 0;
    // What the player wears and brings.
    int weapon = 0, pen = 0, hat = 0, title = 0;
    ItemSlotsArray loadout{{-1, -1}};
    Counters counters;

    static bool has(uint32_t bits, int index)
    {
        return index >= 0 && index < 32 && ((bits >> index) & 1u);
    }
    bool earned(Achievement a) const
    {
        return (achievements >> static_cast<int>(a)) & 1;
    }
    bool cleared(const std::string& page) const
    {
        const auto it = results.find(page);
        return it != results.end() && (it->second & ClearedBit);
    }
    // The page's star bits (recordStars).
    int stars(const std::string& page) const
    {
        const auto it = results.find(page);
        return it != results.end() ? recordStars(it->second) : 0;
    }
    int bestTier(const std::string& page) const
    {
        const auto it = results.find(page);
        return it != results.end() ? recordTier(it->second) : 0;
    }
    // Stars of every page whose id starts with prefix ("1-" = chapter 1, "" = all).
    int starCount(const std::string& prefix = "") const
    {
        int count = 0;
        for (const auto& [page, bits] : results)
            if (page.rfind(prefix, 0) == 0)
                count += countStars(bits);
        return count;
    }
    int highestClearTier() const
    {
        int best = 0;
        for (const auto& [page, bits] : results)
            best = std::max(best, recordTier(bits));
        return best;
    }
    int unlockedTier() const
    {
        const int allStars = starCount("");
        const std::string last = lastPage("3-");
        int tier = 1;
        if (allStars >= 6)
            tier = 2;
        if (allStars >= 12)
            tier = 3;
        if (tier >= 3 && bestTier(last) >= 3)
            tier = 4;
        if (tier >= 4 && bestTier(last) >= 4)
            tier = 5;
        return tier;
    }
    // ---- Spending (each returns the stickers spent, or -1 when refused) ----
    int research(Attribute a)
    {
        const int i = static_cast<int>(a);
        if (i < 0 || i >= AttributeCount)
            return -1;
        const int cost = researchCost(ranks[i]);
        if (cost < 0 || stickers < cost)
            return -1;
        stickers -= cost;
        ++ranks[i];
        return cost;
    }
    // Buying a weapon, pen or hat also puts it to use.
    int buyWeapon(int w)
    {
        const int price = weaponPrice(w);
        if (price < 0 || has(weapons, w) || stickers < price)
            return -1;
        stickers -= price;
        weapons |= 1u << w;
        weapon = w;
        return price;
    }
    int buyPen(int p)
    {
        const int price = penPrice(p);
        if (price < 0 || has(pens, p) || stickers < price)
            return -1;
        stickers -= price;
        pens |= 1u << p;
        pen = p;
        return price;
    }
    int buyHat(int h)
    {
        const int price = hatPrice(h);
        if (price < 0 || has(hats, h) || stickers < price)
            return -1;
        stickers -= price;
        hats |= 1u << h;
        hat = h;
        return price;
    }
    // A bought item goes into a free slot when there is one.
    int buyItem(int i)
    {
        const int price = itemPrice(i);
        if (price < 0 || items[i] >= MaxItemStack || stickers < price)
            return -1;
        stickers -= price;
        ++items[i];
        bring(i);
        return price;
    }
    bool equipWeapon(int w)
    {
        if (!has(weapons, w) || w >= WeaponCount)
            return false;
        weapon = w;
        return true;
    }
    bool equipPen(int p)
    {
        if (!has(pens, p) || p >= PenCount)
            return false;
        pen = p;
        return true;
    }
    bool equipHat(int h)
    {
        if (!has(hats, h) || h >= HatCount)
            return false;
        hat = h;
        return true;
    }
    bool equipTitle(int t)
    {
        if (!has(titles, t) || t >= TitleCount)
            return false;
        title = t;
        return true;
    }
    int copies(int item) const
    {
        int n = 0;
        for (int slot : loadout)
            n += slot == item;
        return n;
    }
    // Puts one more of an item into a free slot (needs a copy in stock for every slot it fills).
    bool bring(int item)
    {
        if (item < 0 || item >= ItemCount || copies(item) >= items[item])
            return false;
        for (int& slot : loadout)
            if (slot < 0)
            {
                slot = item;
                return true;
            }
        return false;
    }
    bool drop(int slot)
    {
        if (slot < 0 || slot >= ItemSlots || loadout[slot] < 0)
            return false;
        loadout[slot] = -1;
        return true;
    }
    // The host has applied these items to a mission: they leave the stock; slots stay filled while stock lasts
    // (the last slot empties first).
    void consume(const ItemSlotsArray& used)
    {
        for (int item : used)
            if (item >= 0 && item < ItemCount && items[item] > 0)
                --items[item];
        for (int i = ItemSlots - 1; i >= 0; --i)
            if (loadout[i] >= 0 && copies(loadout[i]) > items[loadout[i]])
                loadout[i] = -1;
    }
    // ---- Earning ----
    // One reward packet from the host: a wave's stickers, or the page's clear with its stars. Unknown pages and
    // packets above the cap are refused (-1). The first clear of a page pays a bonus at the room's tier, which only
    // this profile can know; firstClear reports it.
    int reward(const std::string& page, int gain, int starBits, bool clear, int tier, int* firstClear = nullptr)
    {
        if (firstClear)
            *firstClear = 0;
        if (!knownPage(page) || gain < 0 || gain > rewardCap(page))
            return -1;
        tier = clampTier(tier);
        const int bonus = clear && !cleared(page) ? firstClearStickers(tier) : 0;
        stickers += gain + bonus;
        stickersTotal += gain + bonus;
        if (firstClear)
            *firstClear = bonus;
        if (clear)
        {
            uint8_t& best = results[page];
            if (!(best & ClearedBit))
                ++levelsCleared;
            const int topTier = std::max(recordTier(best), tier);
            best = static_cast<uint8_t>(ClearedBit | (topTier << 3) | (recordStars(best) | (starBits & 7)));
        }
        else if (gain > 0)
            ++wavesCleared;
        return gain + bonus;
    }
    // One packet's share of a mission. Implausible packets change nothing (false).
    bool tally(const Tally& t)
    {
        if (!plausible(t))
            return false;
        counters.kills += t.kills;
        counters.damage += t.damage;
        counters.walls += t.walls;
        counters.thrown += t.thrown;
        counters.throws += t.throws;
        counters.held += t.held;
        counters.inkGiven += t.inkGiven;
        counters.bullseyes += t.bullseyes;
        counters.feats |= t.feats;
        counters.erased += t.erased;
        counters.elites += t.elites;
        counters.given += t.given;
        counters.bosses |= t.bosses;
        counters.shapes |= t.shapes;
        counters.banked += t.banked;
        if (t.final)
        {
            ++counters.missions;
            if (t.won)
            {
                ++counters.wins;
                if (t.players <= 1 && !t.bots)
                    ++counters.loneWins;
            }
            counters.maxParty = std::max(counters.maxParty, t.players);
            counters.maxItems = std::max(counters.maxItems, t.items);
            for (int i = 0; i < AwardCount; ++i)
                if ((t.awards >> i) & 1)
                {
                    ++counters.awards[i];
                    titles |= 1u << awardTitle(static_cast<Award>(i));
                }
        }
        return true;
    }
    bool met(Achievement a) const
    {
        const Counters& c = counters;
        int full = 0, owned = 0;
        for (uint8_t rank : ranks)
            full += rank >= MaxRank;
        for (int h = 1; h < HatCount; ++h)
            owned += has(hats, h);
        auto all = [](uint32_t bits, int count)
        {
            for (int i = 0; i < count; ++i)
                if (!has(bits, i))
                    return false;
            return true;
        };
        bool chapter = true;
        for (const auto& level : defenseLevels())
            chapter &= std::string(level.id).rfind("1-", 0) != 0 || cleared(level.id);
        switch (a)
        {
        case Achievement::FirstStroke:
            return c.walls >= 1;
        case Achievement::WallApprentice:
            return c.walls >= 300;
        case Achievement::FirstPage:
            return cleared("1-1");
        case Achievement::FirstNotebook:
            return chapter;
        case Achievement::FullMarks:
        {
            for (const auto& [page, bits] : results)
                if (recordStars(bits) == 7)
                    return true;
            return false;
        }
        case Achievement::NineStars:
            return starCount("1-") >= 9;
        case Achievement::Harder:
            return highestClearTier() >= 2;
        case Achievement::Tough:
            return highestClearTier() >= 3;
        case Achievement::Veteran:
            return highestClearTier() >= 5;
        case Achievement::LiftOff:
            return c.thrown >= 1;
        case Achievement::Cannonball:
            return c.thrown >= 25;
        case Achievement::Pitcher:
            return c.throws >= 25;
        case Achievement::Bait:
            return c.held >= 10;
        case Achievement::ProBait:
            return c.held >= 200;
        case Achievement::Bullseye:
            return c.bullseyes >= 1;
        case Achievement::Sharpshooter:
            return c.bullseyes >= 25;
        case Achievement::GoodMate:
            return c.inkGiven >= 300;
        case Achievement::FirstStudy:
            for (uint8_t rank : ranks)
                if (rank > 0)
                    return true;
            return false;
        case Achievement::Specialist:
            return full >= 1;
        case Achievement::Doctorate:
            return full >= AttributeCount;
        case Achievement::Armory:
            return all(weapons, WeaponCount);
        case Achievement::Stationery:
            return all(pens, PenCount);
        case Achievement::HatCollector:
            return owned >= 4;
        case Achievement::Tycoon:
            return stickersTotal >= 5000;
        case Achievement::Thousand:
            return c.kills >= 1000;
        case Achievement::MVP:
            return c.awards[static_cast<int>(Award::MVP)] >= 1;
        case Achievement::Scapegoat:
            return c.awards[static_cast<int>(Award::Scapegoat)] >= 1;
        case Achievement::Party:
            return c.maxParty >= 4;
        case Achievement::FullHouse:
            return c.maxParty >= 8;
        case Achievement::LoneWolf:
            return c.loneWins >= 1;
        case Achievement::Untouched:
            return (stars(lastPage("1-")) & 1) != 0;
        case Achievement::Supplied:
            return c.maxItems >= ItemSlots;
        default:
            break;
        }
        auto feat = [&](Feat f) { return (c.feats & featBit(f)) != 0; };
        switch (a)
        {
        case Achievement::ExpFirstFloor:
            return feat(Feat::FloorCleared);
        case Achievement::ExpPencilBoss:
            return (c.bosses & 1) != 0;
        case Achievement::ExpWalkOut:
            return feat(Feat::FinalExit);
        case Achievement::ExpEarlyRich:
            return feat(Feat::EarlyRich);
        case Achievement::ExpBigBank:
            return feat(Feat::BigBank);
        case Achievement::ExpAllBosses:
            return (c.bosses & 31) == 31;
        case Achievement::ExpDragon:
            return feat(Feat::Dragon);
        case Achievement::ExpDisarm:
            return feat(Feat::Disarm);
        case Achievement::ExpBareHands:
            return feat(Feat::Toolless);
        case Achievement::ExpHalfErase:
            return feat(Feat::HalfErase);
        case Achievement::ExpEraser10:
            return c.erased >= 10;
        case Achievement::ExpRevive:
            return feat(Feat::Revive);
        case Achievement::ExpTinyRevive:
            return feat(Feat::TinyRevive);
        case Achievement::ExpGeometry:
            return (c.shapes & 7) == 7;
        case Achievement::ExpBorrowed:
            return feat(Feat::BorrowedPen);
        case Achievement::ExpPair:
            return feat(Feat::Pair);
        case Achievement::ExpStaplerPair:
            return feat(Feat::StaplerPair);
        case Achievement::ExpAllLooks:
            return feat(Feat::AllLooks);
        case Achievement::ExpElites:
            return c.elites >= 25;
        case Achievement::ExpThief:
            return feat(Feat::Thief);
        case Achievement::ExpArena:
            return feat(Feat::Arena);
        case Achievement::ExpChained:
            return feat(Feat::Chained);
        case Achievement::ExpCopier:
            return feat(Feat::CopyEraser);
        case Achievement::ExpBigSpin:
            return feat(Feat::BigSpin);
        case Achievement::ExpReforge:
            return feat(Feat::Reforge2);
        case Achievement::ExpVote:
            return feat(Feat::Vote4);
        case Achievement::ExpGenerous:
            return c.given >= 200;
        case Achievement::ExpFlawless:
            return feat(Feat::Flawless);
        case Achievement::ExpTeamExit:
            return feat(Feat::TeamExit);
        case Achievement::ExpTier3:
            return feat(Feat::Tier3Exit);
        case Achievement::ExpRich:
            return c.banked >= 10000;
        case Achievement::ExpWiped:
            return feat(Feat::WipedRich);
        default:
            return false;
        }
    }
    // Marks every achievement newly met: each pays its stickers once and opens its hat or title. Earning one can
    // earn another (the sticker total), so it runs until nothing changes. Returns the new ones in order.
    std::vector<int> settle()
    {
        std::vector<int> fresh;
        for (bool again = true; again;)
        {
            again = false;
            for (int i = 0; i < AchievementCount; ++i)
                if (!((achievements >> i) & 1) && met(static_cast<Achievement>(i)))
                {
                    achievements |= uint64_t(1) << i;
                    const AchievementInfo& info = achievementInfo(i);
                    stickers += info.stickers;
                    stickersTotal += info.stickers;
                    if (info.hat > 0)
                        hats |= 1u << info.hat;
                    if (info.title > 0)
                        titles |= 1u << info.title;
                    fresh.push_back(i);
                    again = true;
                }
        }
        return fresh;
    }
    // What this player reports to a room: only what the profile really owns, the rest as nothing.
    Loadout report() const
    {
        Loadout out;
        out.ranks = ranks;
        out.weapon = has(weapons, weapon) ? weapon : 0;
        out.pen = has(pens, pen) ? pen : 0;
        out.hat = has(hats, hat) ? hat : 0;
        out.title = has(titles, title) ? title : 0;
        for (int i = 0; i < ItemSlots; ++i)
        {
            const int item = loadout[i];
            int before = 0;
            for (int j = 0; j < i; ++j)
                before += loadout[j] == item;
            out.items[i] = item >= 0 && item < ItemCount && before < items[item] ? item : -1;
        }
        out.achievements = achievements & AchievementMask;
        out.weapons = weapons;
        return sanitize(out);
    }
};
} // namespace gwgame
