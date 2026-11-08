#pragma once
#include "Types.h"
namespace gwgame
{
// A replicated position is a sample, not a render position. Extrapolate only
// a small network gap; smooth position corrections without changing authority.
struct ProjectilePresentation
{
    bool valid = false;
    double sample = -1, age = 0;
    Vec2 position{}, velocity{}, correction{}, visual{};
    Vec2 present(double time, Vec2 p, Vec2 v, double remaining, double dt, bool paused)
    {
        if (!valid || time < sample - 1e-6 || time > sample + 1e-6)
        {
            bool smooth =
                valid && time > sample && time - sample <= .15 && velocity.dot(v) > 0 && (visual - p).length() < 160;
            correction = smooth ? visual - p : Vec2{};
            position = p;
            velocity = v;
            sample = time;
            age = 0;
            if (!valid || !smooth)
                visual = p;
            valid = true;
        }
        if (paused)
            return visual;
        // Advance at render frequency, capped to 100 ms and authoritative lifetime.
        age += std::max(0., dt);
        double advance = std::min({age, .1, std::max(0., remaining)});
        visual = position + velocity * advance + correction * std::max(0., 1 - age / .05);
        return visual;
    }
};
} // namespace gwgame
