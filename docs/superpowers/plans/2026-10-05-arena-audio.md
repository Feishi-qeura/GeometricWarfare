# Arena Audio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship original music, all requested combat audio and saved volume controls in a new Douyin debug package.
**Architecture:** Offline deterministic WAV synthesis imported as cooked SoundWave assets. A fixed simulation event aggregator feeds a bounded UE component pool; a separate wall-clock music controller handles transitions and host ducking. Existing live SDK and gameplay remain independent.
**Tech Stack:** UE 5.8 C++, native simulation tests, Python/numpy synthesis, Unreal Python import, PowerShell builds.
**Spec:** `docs/superpowers/specs/2026-10-05-arena-audio-design.md`

## Global Constraints
- 48 kHz 16-bit PCM: stereo music, mono SFX; peak <= -6 dBFS.
- Battle/Boss 112 BPM 32 bars; Sprint 140 BPM 32 bars; Results 112 BPM 8 bars; Assist 112 BPM 2 bars.
- Fixed 16 SFX components; max 4 starts/frame and 60 starts/second with burst 8; music max 3 components.
- BGM/SFX defaults .55/.70, gain squared, save on release; performance reset preserves volumes.
- No additional 5000-player scan; audio never changes scores, damage, SDK ACK or phases.
- Preserve the successful 1.1.7 archive; deliver 1.1.8 separately.

## Review Focus
- Broken/missing audio assets degrade silently without stopping formal live startup (task 3).
- Fast boss transitions within a simulation substep retain telegraphs; cancellation stops loop (tasks 2,3).
- Slider drag during player focus changes volume rather than camera; leaving viewport saves current value (task 4).
- Break-to-collect emits exactly one resource sound, armor-only damage still emits hit (task 2).
- Muting during assist/laser ends SFX while music timing survives; same-team assist never retriggers (task 3).

### Task 1: Original sound library
**Files:** `Tools/ArenaAudio/generate.py`, `test_assets.py`, `import_unreal.py`; `Assets/Audio/GeometricWarfare`; `Content/Audio/GeometricWarfare`.
**Interfaces:** `generate(output: Path)` emits manifest.json with name, loop, duration, category and WAV per asset. Import consumes manifest and writes `/Game/Audio/GeometricWarfare/<name>`.
- [ ] Write/run asset validation before assets exist: fail missing manifest. Validate actual PCM non-silence, peaks, channels, duration and seam.
- [ ] Compose shared original theme and distinct weapon, impact, pickup and boss motifs; deterministic seed. Generate WAV and pass validation.
- [ ] Import SoundWave with looping/ForceInline and save; validate imported asset counts/durations. Commit library/source.

### Task 2: Real simulation audio and scheduler
**Files:** `Simulation/ArenaAudio.h`, Match/Combat/Boss/Pickups/Evolution .h/.inl, `Tests/ArenaAudioTests.cpp`, `Scripts/Test-Audio.ps1`.
**Interfaces:** `gw::AudioEvents::emit(AudioKind,int)` fixed normal/important slots, `clear()`, focus/host IDs; `gw::AudioBudget::allow(AudioKind,int,double)` wall-clock rate budget. `Match.audio` shared across substeps.
- [ ] Write/run native behavior tests: actual gun trigger (six types), shotgun one per trigger, damage/death, armor-only hit, NPC, orb, pickup vs break, boss transition and loop cleanup, bounded events and scheduler priority/cooldown. Fail absent interfaces.
- [ ] Implement fixed collector and hook successful gameplay paths; preserve gameplay calculations.
- [ ] Implement frame/rate budget and priority selection. Run native audio plus full simulation suite. Commit.

### Task 3: UE playback and music state
**Files:** `ArenaAudioSubsystem.h/.cpp`, `ArenaAudioTests.cpp`, `ArenaGameMode.h/.cpp`, live session reset.
**Interfaces:** `UArenaAudioSubsystem::TickAudio(gw::Match&,int32 Focus,int32 Host,bool Active)`, `HostAssist()`, `ResetAudio()`, statistics; game instance subsystem owns assets/components and wall-clock controller.
- [ ] Add real UE tests of state selection, event draining, pool bounds, mute, music duck/restoration, results/session reset, host cooldown and failed assets; run RED.
- [ ] Load all cooked waves before formal startup return; create fixed pool and three music components. Precache; crossfade .6s, duck .15s/.35, restore .4s, assist cooldown2s.
- [ ] Tick after all substeps even when inactive; stop all on reset/shutdown. Emit assist only successful first/change-team. Run build and UE audio tests GREEN. Commit.

### Task 4: Saved sliders
**Files:** `ArenaUserSettings.h/.cpp`, `ArenaHUDSettings.cpp`, `ArenaHUDInput.cpp`, `ArenaSettingsTests.cpp`, default settings.
**Interfaces:** config `BgmVolume`, `SfxVolume`; setting actions15/16; `UpdateAudioSlider(float X)` writes live value, release/cancel saves once.
- [ ] Add tests RED: clamp/NaN, save/reload, real drag with focus, exact mute, performance reset preservation.
- [ ] Add defaults/validation and two labeled sliders; reflow panel within viewport; gain updates on tick, disk only on release/cancel.
- [ ] Run settings/adaptive/input automation and capture rendered panel. Commit.

### Task 5: 5000-player audio validation
**Files:** `ArenaGameMode` stress fixture/stats, `Scripts/Test-AudioPerformance.ps1`, verification report.
**Interfaces:** stress JSON adds audio scheduling samples/counts/peak and active device evidence; separate on/off output paths.
- [ ] Extend stress fixture with all six guns/resources and boss; shared deterministic parameters, warm5s/sample30s.
- [ ] Run audio-off/on with real device, compare frame/simulation/render and audio scheduling p95; require <=16 peak and <=.5ms scheduling p95. Report device/render limits honestly.
- [ ] Run complete UE automation and simulation suites, Editor/Game/Shipping builds. Commit evidence.

### Task 6: Deliver package
**Files:** package script/config version, `output/GeomeWar_1.1.8.zip`, final verification document.
- [ ] Package freshly cooked assets and existing SDK host; inspect archive content and SHA256; smoke run packaged executable.
- [ ] Fresh whole-change review, resolve important findings with reproducing tests. Preserve baseline and document local/cloud live audio capture requiring actual platform verification.
- [ ] Deliver archive, brief test results and preview music.

Execution: user explicitly instructed 开始实现 on 2026-10-05; implement natively without another plan approval gate. Current branch contains the working 1.1.7 integrations and dirty files; keep this checkout instead of discarding those changes in a HEAD-only worktree.
