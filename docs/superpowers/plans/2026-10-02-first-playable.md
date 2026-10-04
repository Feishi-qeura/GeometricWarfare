# First Playable Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan in this session. User explicitly requested implementation after reviewing the design; continue without another authorization loop.

**Goal:** Deliver a minimalist UE 5.8 local playable arena and an independently reusable Douyin event bridge.

**Architecture:** A portable, fixed-step 2D convex-polygon simulation owns geometry and motion. UE GameMode owns viewer identity and consumes plugin events; Canvas HUD and a Slate input panel display the arena and provide simulator controls. The plugin isolates event parsing, deduplication, transport and avatar downloads from gameplay.

**Tech Stack:** UE 5.8 C++, Canvas, Slate, HTTP, JSON, WebSockets; MSVC for portable physics tests.

**Spec:** ../specs/2026-10-02-geometric-warfare-design.md

## Global Constraints

- Implement the first milestone, not unconfirmed combat rules: joining, unique identity, four random shapes, names, avatars, full health bars, physical movement and collisions.
- Minimalist light visual style with red/blue/gray accents.
- Mock mode must be explicit. A relay protocol is our adapter contract, not an official SDK API.
- Real LiveOpenSDK integration requires inspecting the authorized SDK package; no SDK or credentials are currently supplied.
- Workspace has no Git metadata; edit this project directly and do not initialize or commit an unrelated repository.

## Review Focus

- Repeat joins and messages cannot create duplicate viewers.
- Malformed external messages cannot crash or mutate gameplay.
- Dense spawns and shape changes cannot produce persistent overlap or escape the arena.
- Closed sessions and completed HTTP requests cannot access destroyed UObjects.
- Resizing the viewport keeps the square arena and controls usable.

## Tasks

### 1 Portable physics and tests

Files: Source/GeometricWarfare/Simulation/ArenaPhysics.h, Tests/ArenaPhysicsTests.cpp, Scripts/Test-Physics.ps1.

Interface: gw::World::add(id, shape), step(dt), reshape(id, shape); bodies contain positions, velocities, angle and convex vertices.

- [x] Write regression assertions for wall reflection, head-on collision, safe spawning, density, long-run finite positions and frame-step consistency; run against stubs to observe failure.
- [x] Implement SAT collision, positional correction, fixed timesteps and speed bounds; pass the standalone test executable.

### 2 Independent event bridge

Files: Plugins/DouyinLiveBridge/DouyinLiveBridge.uplugin and Source/DouyinLiveBridge/{Public,Private}.

Interface: UDouyinLiveSubsystem emits FDouyinComment through OnComment; supports explicit simulated comments and an optional ws/wss adapter connection. Avatar textures are fetched asynchronously with bounded caching.

- [x] Add UE automation tests for malformed JSON, duplicate IDs and batch comments.
- [x] Implement validated parsing and lifecycle-safe event dispatch, source mode isolation, bounded replay window and avatar requests.
- [x] Document native SDK adapter extension point and exact relay schema.

### 3 Playable arena and minimalist presentation

Files: Source/GeometricWarfare/ArenaGameMode.*, ArenaHUD.*, ArenaPlayerController.*; Config/DefaultEngine.ini; module dependencies.

- [x] Connect comments to identity registry, spawn and optional team/shape commands; reject empty identity and capacity overflow.
- [x] Draw a responsive square arena, polygon outlines, identity portraits, names and health bars, plus simulator sidebar and status.
- [x] Add local viewer/command inputs, one/fifty viewer controls, pause/reset and stable default startup map.

### 4 Build, run and review

- [x] Compile Development Editor and Development Game with local UE 5.8.
- [x] Run physics and UE automation suites, execute a rendering smoke run, inspect a generated screenshot when available.
- [x] Have a fresh reviewer inspect the complete implementation, fix material findings and rerun affected verification.
- [x] Deliver launch instructions and accurately separate working local features from unverified live SDK connectivity.

## Execution Ledger

- Scope: user authorized implementation; execute inline. Native SDK package query sent; local work continues independently.
- Ruling: use portable SAT simulation instead of the earlier optional Chaos suggestion for predictable flat geometry and standalone validation. No gameplay requirement depends on Chaos specifically.
- Ruling: no Git worktree available because the project is not a Git repository. Work in the user-provided directory.
- Completed 2026-10-02: Development Editor and Development Game compile successfully in the installed UE 5.8. Public HTTP dependency was corrected after the Game target exposed a missing include path.
- Verification: standalone physics suite reports zero failures. Saved/AutomationFinal/index.json records four successful UE tests, zero warnings and zero failures (09:32 UTC). Room-switch and avatar ordering regressions were first reproduced with failing tests, then fixed.
- Rendering: inspected Saved/Screenshots/Arena.png at 1440 x 1000, including Chinese text, four shapes, avatar placeholders, team colors, health bars and local controls. Native mouse interaction was not separately automated.
- Review: independent reviewer identified room-state retention, stale/cross-room avatar completion and registry-free engine discovery issues; all four were corrected. Session changes cancel pending HTTP requests; per-viewer tickets also reject stale completion.
- Scope boundary: SDK package and platform capabilities are unavailable. No official live connection, gift fulfillment, combat, scoring, bases, five-minute round or leaderboard is claimed implemented.
- Proposal: docs/proposal/【提案评估】几何战争_草稿.docx preserves the supplied template and records missing submission materials. Package/XML checks passed; page rendering could not run because Word/LibreOffice is unavailable. Treat as an editable draft pending visual review and complete demo video.
