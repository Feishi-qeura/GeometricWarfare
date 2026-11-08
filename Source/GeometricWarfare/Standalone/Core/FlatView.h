#pragma once
#include "Types.h"
namespace gwgame
{
// Shared by the flat renderer and mouse input. World Y points down on screen.
struct FlatView
{
    Vec2 center{};
    double width = 1600, height = 900, scale = .7;
    Vec2 screen(Vec2 p) const
    {
        return (p - center) * scale + Vec2{width * .5, height * .5};
    }
    Vec2 world(Vec2 p) const
    {
        return center + (p - Vec2{width * .5, height * .5}) * (1 / std::max(.01, scale));
    }
};
// Decorative main-menu brawl, deliberately independent of network match state.
// Retains the released paper scene's 32-second pan / combat / boss close-up cycle.
struct MenuScene
{
    struct Fighter
    {
        Vec2 position, velocity;
        double angle = 0;
        int shape = 0, weapon = 0, team = 1;
    };
    struct Shot
    {
        Vec2 position, velocity;
        double life = 0;
        int team = 1;
    };
    std::vector<Fighter> fighters;
    std::vector<Shot> shots;
    Vec2 boss{4480, 3630};
    double age = 0, fireTime = 0;
    uint64_t seed = 125;
    MenuScene()
    {
        reset();
    }
    void reset()
    {
        age = fireTime = 0;
        fighters.clear();
        shots.clear();
        for (int i = 0; i < 100; ++i)
        {
            double a = random01(seed) * Pi * 2;
            fighters.push_back({{2600 + (i % 10) * 330., 2600 + (i / 10) * 330.},
                                {std::cos(a) * 175, std::sin(a) * 175},
                                a,
                                i % 4,
                                i % 6,
                                i % 2 + 1});
        }
    }
    void step(double dt)
    {
        dt = std::clamp(dt, 0., .1);
        age += dt;
        if (age >= 32)
            reset();
        for (auto& f : fighters)
        {
            f.position += f.velocity * dt;
            if (f.position.x < 1900 || f.position.x > 6300)
                f.velocity.x = -f.velocity.x;
            if (f.position.y < 1900 || f.position.y > 6300)
                f.velocity.y = -f.velocity.y;
            f.angle = std::atan2(boss.y - f.position.y, boss.x - f.position.x) + std::sin(age + f.shape) * .6;
        }
        fireTime -= dt;
        if (fireTime <= 0)
        {
            fireTime = .18;
            for (size_t i = 0; i < fighters.size(); i += 3)
            {
                auto& f = fighters[i];
                shots.push_back({f.position, {std::cos(f.angle) * 1100, std::sin(f.angle) * 1100}, 1.3, f.team});
            }
        }
        for (auto& s : shots)
        {
            s.position += s.velocity * dt;
            s.life -= dt;
        }
        shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Shot& s) { return s.life <= 0; }), shots.end());
    }
    FlatView view(double w, double h) const
    {
        auto smooth = [](double t)
        {
            t = std::clamp(t, 0., 1.);
            return t * t * (3 - 2 * t);
        };
        double track = smooth(age / 2) * (1 - smooth((age - 7) / 2));
        double focus = smooth((age - 9.35) / 1.25) * (1 - smooth((age - 13.8) / 2.2));
        double zoom = 1 + track * .2;
        Vec2 center{4096 + std::sin(age * .27) * 245, 4096 + std::cos(age * .21) * 180};
        if (focus > 0)
        {
            zoom = 1 + (w < h ? 1.1 : 1.7) * focus;
            double s = std::min(w / 2000, h / 2400) * zoom;
            Vec2 target = boss - Vec2{w / s * (w < h ? .35 : .32), -h / s * (w < h ? .435 : .34)};
            center = center * (1 - focus) + target * focus;
        }
        return {center, w, h, std::min(w / 2000, h / 2400) * zoom};
    }
};
} // namespace gwgame
