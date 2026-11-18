// Native runner: compile with /DGW_INTRO_TIMELINE_TEST_MAIN (not part of the game executable).
#if defined(GW_INTRO_TIMELINE_TEST_MAIN)
#include "../Core/IntroTimeline.h"
#include <cmath>
#include <iostream>

int main()
{
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    gwgame::IntroTimeline t;
    t.begin(10);
    check(t.active && t.alpha == 0, "opening covers the title without displaying a prompt");
    t.update(10.1, 0, 6, true);
    check(t.active && t.alpha == 0, "first frame starts transparent");
    t.update(10.35, .25, 6, true);
    check(std::abs(t.alpha - .5) < .001, "smooth half-opacity at quarter second");
    t.update(10.6, .5, 6, true);
    check(t.alpha == 1, "fully visible after half second");
    t.update(15.85, 5.75, 6, true);
    check(std::abs(t.alpha - .5) < .001, "prompt and video fade at the natural end");
    t.update(16.1, 6, 6, true);
    check(!t.active, "completed video releases the title");

    t.begin(20);
    t.update(20.2, .2, 6, true);
    const double before = t.alpha;
    t.skip(20.2);
    t.update(20.34, .34, 6, true);
    check(t.active && std::abs(t.alpha - before * .5) < .001, "early skip fades from current opacity");
    t.skip(20.34);
    t.update(20.49, .49, 6, true);
    check(!t.active, "repeated Space does not restart skip fade");

    t.begin(30);
    t.skip(30.1);
    t.update(30.4, 0, 0, false);
    check(!t.active, "Space can skip before decoder opens");
    t.begin(40);
    t.update(48.01, 0, 0, false);
    check(!t.active, "failed decoder cannot strand the player");
    t.begin(50);
    t.update(50.1, .1, 6, true);
    t.update(58.2, .1, 6, true);
    check(!t.active, "stalled playback cannot strand the player");
    std::cout << "Intro timeline: " << failures << " failures\n";
    return failures ? 1 : 0;
}
#endif
