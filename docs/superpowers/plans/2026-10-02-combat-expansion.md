# Combat Expansion Implementation Plan

**Goal:** Deliver the user's eight camera, combat and live-interaction changes in the existing UE game.
**Architecture:** Match remains the deterministic rules owner; isolated Boss and evolution/weapon units expose bounded render state. The bridge normalizes Like/Share. GameMode routes identity events and host controls, Canvas renders effects without spawning per-hit widgets.
**Tech Stack:** UE 5.8, C++17 portable simulation, Canvas/Slate, PowerShell native test scripts.
**Spec:** `docs/superpowers/specs/2026-10-02-combat-expansion-design.md`

## Constraints and review focus

- 5000 viewers plus one host; red/blue/gray caps unchanged; dead viewers retain slots.
- Defense rounding, depletion and evolution expiry must not create health or retain buffs after death.
- Share duplicates/repeated shares cannot refill shells or override host rifle; Like count means delta, not cumulative totals.
- Boundary times 30/60/240/300 and round reset must not double-spawn BOSS/packs or retain beams.
- Base reward must not revive destroyed bases or multiply stolen scores; host cannot alter awards or tie-break scores.
- User correction during rendering review: BOSS stays at the center through every phase, with an in-place jump. Warning and firing beams extend from BOSS only in the target direction, with no beam behind it. Verify rear targets take no laser damage and recapture final visuals/performance after the correction.
- Fixed shot/effect budgets, spatial queries and frame-time sampling with new systems active.

## Work and interfaces

- [x] Core simulation (caps_damage): ArenaMatch/ArenaPhysics and new combat/evolution files. Expose `WeaponKind`, `weaponFor(const Fighter&)`, `healLike(id,count)`, `grantShotgun(id)`, `addHost(id,team)`, `damageEnvironment(id,amount)`; Fighter host/armor/weapon/evolution/hit state and bounded packs/waves. Tests in ArenaExpansionTests and existing Match suite. First demonstrate missing behavior with failing tests, then implement and rerun.
- [x] BOSS subsystem (full_review): ArenaBoss.h / ArenaBoss.inl, `BossState`, `Match::damageBoss`, `tickBoss`, `resetBoss`; coordinate declarations and targetKind 4 with core owner. ArenaBossTests verifies all timings/rewards/damage and reset through production Match code. User correction is covered by 444 passing assertions: stationary through every phase, in-place jump, forward-only warning/beam, and no damage to close rear targets.
- [x] Camera and bridge (physics5000): right-click release; wheel retains lock; tests update actual HUD handlers. Add Like/Share DTO/delegates, decoder, dedup and mocks in plugin with automation tests. No direct dependence on unreleased official SDK.
- [x] GameMode integration (root): bind social delegates, host team controls, mock Like/Share UI; integration tests for healing, weapon assignment, host authority and resets.
- [x] Combat presentation (root): bounded procedural muzzle/tracer/impact visuals, hit flash, armor bars and weapon icons; evolved ring/packs/sword waves; BOSS ground, telegraph, bullets, beam, slam and rage HUD. Inspected corrected full/compact screenshots and actual full-round capture frames; the cannon still image was obscured, so it is not claimed as clear in-flight projectile evidence.
- [x] Verify Editor/Game builds, native tests and UE automation; render full/compact states and sample 5000 viewers with new features active. Document exact results and limits in verification record and README. Final UI/award video was re-recorded and encoded successfully, with 4057 source/encoded frames confirmed by the new ffprobe metadata.

Latest user corrections are implemented: a spaced gold downward arrow above the followed player's name, plus winner-team MVP and loser-team FMVP by individual score (kills, then smaller stable ID break team-internal ties; host/gray excluded; empty eligible teams have no recipient). The existing score/kill/round-number team-victory tie-break is preserved. Final UI/award Editor and Game builds succeeded without warnings; the latest UE report has eight passing tests, and the final 4057-frame capture has reviewed arrow, correct team-award and next-round frames. The replacement `GeometricWarfare-CombatExpansion-1x.mp4` encoded successfully and passed ffprobe: H.264/yuv420p, 1920×1080, 12 fps, 4057 frames, 338.083008-second container duration. Metadata was created at `2026-10-02T15:42:06.5563459Z`. The isolated 5000-viewer-plus-host sample was captured after the BOSS correction but before the final UI/award changes; its documented scope remains unchanged. Real SDK/online fulfillment and Feishu updates remain external work, not claims of this completed local demo.

No Git repository is present. Work in the existing project and do not invent a commit/worktree workflow. The user confirmed automatic host combat and permanent immediate base-life reward. All other defaults are recorded in the spec and exposed in local documentation.
