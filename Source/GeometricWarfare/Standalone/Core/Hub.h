#pragma once
#include "Defense.h"
#include <cstring>
namespace gwgame
{
// The hub (大厅) is the room everyone waits in: one sheet of graph paper with the stations built into its
// walls. Every player sees the same sheet, so the layout is static data shared by the host and the clients.
// '#' wall, ',' floor, '.' the training mat, 'P' where players arrive, 't' a training target, 'o' the
// bullseye throws aim for. Station letters sit in the wall ring and face into the room: B mission board,
// A achievement wall, S shop, R research desk, W wardrobe, D the door (invites), T the training sign.
// Players arrive just below the board in the middle of the far wall, so the first view of the room is the
// board with the achievement wall and the shop on either side; tags on the screen edge point to the rest.
inline const std::vector<const char*>& hubRows()
{
    static const std::vector<const char*> Rows = {"#AAA#BBB#SS##", "#,,,,,,,,,,,#", "#,,,,,P,,,,,R",
                                                  "D,,,,,,,,,,,R", "D,,,,,,,,,,,#", "#,,,,,,,,,,,W",
                                                  "T..o..,,,,,,W", "Tt..t.,,,,,,#", "#############"};
    return Rows;
}
enum class HubStation : uint8_t
{
    Board,
    Achievements,
    Shop,
    Research,
    Wardrobe,
    Door,
    Training,
    Count
};
inline int hubStationOf(char code)
{
    switch (code)
    {
    case 'B':
        return static_cast<int>(HubStation::Board);
    case 'A':
        return static_cast<int>(HubStation::Achievements);
    case 'S':
        return static_cast<int>(HubStation::Shop);
    case 'R':
        return static_cast<int>(HubStation::Research);
    case 'W':
        return static_cast<int>(HubStation::Wardrobe);
    case 'D':
        return static_cast<int>(HubStation::Door);
    case 'T':
        return static_cast<int>(HubStation::Training);
    default:
        return -1;
    }
}
// A station's cells, the way it faces into the room, and the spot in front of it a player walks up to.
struct HubSpot
{
    HubStation kind = HubStation::Board;
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    Vec2 facing{0, 1}, front{}, center{};
};
// The hub's cells are smaller than a mission page's (400) so more of the room fits the follow camera.
constexpr double HubCell = 300;
// How close to a station's front a player must stand to use it (E).
constexpr double HubReach = 260;
// A throw that comes to rest this close to the bullseye's centre scores for the thrower.
constexpr double BullseyeRadius = 140;
inline const std::vector<HubSpot>& hubSpots()
{
    static const std::vector<HubSpot> Spots = []
    {
        std::vector<HubSpot> out(static_cast<size_t>(HubStation::Count));
        std::vector<uint8_t> seen(out.size(), 0);
        const auto& rows = hubRows();
        const int h = static_cast<int>(rows.size()), w = static_cast<int>(std::strlen(rows[0]));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const int kind = hubStationOf(rows[y][x]);
                if (kind < 0)
                    continue;
                HubSpot& s = out[kind];
                if (!seen[kind])
                {
                    s.kind = static_cast<HubStation>(kind);
                    s.minX = s.maxX = x;
                    s.minY = s.maxY = y;
                    seen[kind] = 1;
                }
                s.minX = std::min(s.minX, x);
                s.maxX = std::max(s.maxX, x);
                s.minY = std::min(s.minY, y);
                s.maxY = std::max(s.maxY, y);
            }
        for (auto& s : out)
        {
            // Stations stand in the wall ring; they face whichever way the room lies.
            s.facing = s.minY == 0 ? Vec2{0, 1} : s.maxY == h - 1 ? Vec2{0, -1} : s.minX == 0 ? Vec2{1, 0} : Vec2{-1, 0};
            s.center = Vec2{(s.minX + s.maxX + 1) * .5 * HubCell, (s.minY + s.maxY + 1) * .5 * HubCell};
            // The inner face of the station plus a step into the room.
            const Vec2 half{(s.maxX - s.minX + 1) * .5 * HubCell, (s.maxY - s.minY + 1) * .5 * HubCell};
            s.front = s.center + Vec2{s.facing.x * (half.x + 120), s.facing.y * (half.y + 120)};
        }
        return out;
    }();
    return Spots;
}
// The station a player standing at p can use, or -1.
inline int hubStationNear(Vec2 p)
{
    int best = -1;
    double nearest = HubReach * HubReach;
    const auto& spots = hubSpots();
    for (size_t i = 0; i < spots.size(); ++i)
    {
        const double d2 = (spots[i].front - p).lengthSquared();
        if (d2 < nearest)
        {
            nearest = d2;
            best = static_cast<int>(i);
        }
    }
    return best;
}
// Cells of the hub sheet with one letter (spawn, targets, bullseye).
inline std::vector<int> hubCells(char code)
{
    std::vector<int> cells;
    const auto& rows = hubRows();
    const int w = static_cast<int>(std::strlen(rows[0]));
    for (int y = 0; y < static_cast<int>(rows.size()); ++y)
        for (int x = 0; x < w; ++x)
            if (rows[y][x] == code)
                cells.push_back(y * w + x);
    return cells;
}
struct HubState
{
    Vec2 spawn{}, bullseye{};
    double supplyClock = 0;
    int bullseyes = 0;
};

// The tutorial (新手引导) is a small defense page with a script: seven steps, each finished by doing it once.
enum class TutorialStep : uint8_t
{
    Move,
    Shoot,
    Recruit,
    Merge,
    Turret,
    Draw,
    Defend,
    Done
};
constexpr int TutorialStepCount = 7;
struct TutorialPage
{
    DefenseLevel page;
    // Where the first step sends the player, the two paper targets, and the three cells the drawing step
    // suggests (a short wall across the road; the road bends around it).
    int moveX, moveY;
    std::array<std::pair<int, int>, 2> targets;
    std::array<std::pair<int, int>, 3> wall;
    int waveSize;
};
inline const TutorialPage& tutorialPage()
{
    static const TutorialPage Page = {{"0-1",
                                       1,
                                       1.0,
                                       {"#############", "#...........#", "#...........#", "#,,.......,,#", "S,,.......,C#",
                                        "#,,.......,,#", "#...........#", "#...........#", "#############"}},
                                      7,
                                      2,
                                      {{{4, 1}, {4, 3}}},
                                      {{{6, 3}, {6, 4}, {6, 5}}},
                                      6};
    return Page;
}
// How far the current step has come, as done / needed (what the step card counts).
struct TutorialProgress
{
    int done = 0, needed = 1;
};
} // namespace gwgame
