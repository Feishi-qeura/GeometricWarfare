#pragma once
#include <algorithm>

namespace gwgame
{
struct IntroTimeline
{
    bool active = false;
    double alpha = 0;
    bool stopping = false;
    double started = 0, progressed = 0, position = -1, stopped = 0, stopAlpha = 0;

    static double smooth(double value)
    {
        const double x = std::clamp(value, 0.0, 1.0);
        return x * x * (3 - 2 * x);
    }
    void begin(double now)
    {
        *this = {};
        active = true;
        started = progressed = now;
    }
    void update(double now, double time, double duration, bool ready)
    {
        if (!active)
            return;
        if (stopping)
        {
            alpha = stopAlpha * (1 - smooth((now - stopped) / .28));
            active = now - stopped < .28;
            return;
        }
        if (ready && time > position + .001)
        {
            position = time;
            progressed = now;
        }
        if (now - progressed >= 8 || (ready && duration > 0 && time >= duration))
        {
            active = false;
            alpha = 0;
            return;
        }
        alpha = ready ? smooth(time / .5) : 0;
        if (duration > 0)
            alpha *= smooth((duration - time) / .5);
    }
    void skip(double now)
    {
        if (!active || stopping)
            return;
        stopping = true;
        stopped = now;
        stopAlpha = alpha;
    }
};
} // namespace gwgame
