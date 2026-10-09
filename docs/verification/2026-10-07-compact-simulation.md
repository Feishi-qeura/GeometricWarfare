# Compact arena simulation verification

The approved compact rules are 500 registered spectators (red 200, blue 200, gray 100), plus the existing separate host seat, in a 2000 by 2000 arena. Default natural orbs are 120 and neutral NPCs are 24. Character geometry, weapon statistics, BOSS combat values, and round durations retain their previous numeric values.

## Regression evidence

`Scripts/Test-Compact.ps1` compiles and runs the real standalone simulation with MSVC C++20.

- Before production changes: 40 assertions, 24 expected failures against the previous 5000-spectator / 8000-unit rules. Log: `Saved/compact-red.log`.
- After the initial constant changes: 40 assertions, 3 failures. The original conservative 72-unit center spacing prevented the standalone physics world from filling all 500 ordinary body seats in the smaller arena. Log: `Saved/compact-first-green.log`.
- After the placement fallback correction and an additional late-match admission check: 41 assertions, 0 failures. Log: `Saved/compact-green.log`.

The test covers per-team rejection at 100/200/200; dead participants retaining their seats; rejecting the 501st spectator; admitting one host before and after all spectators; retaining all 501 participants through a round transition; physically bounded bodies while moving; whole-map spatial queries; unscaled ordinary geometry and host scale 2.5; resource counts and bounds; BOSS arrival at 150 seconds; admission after the BOSS, NPC, and pickup obstacles are active; preserved round durations and sample weapon/BOSS numeric values; and the default full-map camera.

## Placement behavior

The existing 80-unit physics cell size is preserved. The grid width is derived from arena size and is now 25 cells. The deterministic fallback sites are also derived from arena size, producing a 26 by 26 grid rather than scanning sites outside the map.

The first 128 random placement attempts retain the preferred 72-unit spacing. If that clearance is exhausted, fallback placement checks actual physical overlap against every active body and the existing arena obstacle test. This removes excess spawn clearance only in dense fallback admission; it preserves shape dimensions, movement, and the collision solver.

A separate diagnostic run admitted 500 bodies plus one additional body for each of the four homogeneous shape populations, with zero initial body penetration pairs. The actual Match keeps its existing gray spectator spawn-zone relocation; that relocation caused 101 to 103 initial penetration pairs with a full mixed crowd and host. After two seconds of simulation with automatic combat and collection disabled, both host-first and host-last diagnostic fixtures had zero penetration pairs. The compact change does not alter that spawn-zone rule.

This note verifies standalone simulation behavior. Unreal builds, presentation/HUD checks, and the adapted existing regression suites are recorded by the coordinating task separately.
