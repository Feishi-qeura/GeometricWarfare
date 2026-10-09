# Observer UI and team capacity plan

**Goal:** Implement the user's seven requested improvements in the existing UE 5.8 game, preserving the five-minute rules and minimal visual style.

**Architecture:** Match enforces population caps and reports actual damage. Portable ArenaView owns camera math; HUD owns screen hit regions, minimap, pooled feedback and icons. PlayerController forwards pointer gestures. Existing Douyin identity and comment handling remains the transport boundary.

**Decisions:** Red and blue each hold at most 2000 participants; gray holds at most 1000, including dead players. A new viewer can send 1/2 directly to join an available red/blue team, as confirmed by the user. A full team never silently redirects someone. Left click selects; dragging at least 5 pixels pans and releases follow. Zoom anchors the mouse point and releases follow; clicking a ranked player restores follow. Minimap click/drag moves the view. Camera bounds take precedence when anchored zoom reaches an edge.

## Implementation and verification

- [x] Match capacity counters and bounded actual-damage events; test full-team rejection, atomic transfers, death/respawn and next-round preservation, armor and overkill.
- [x] ArenaView and PointerDrag; test anchor preservation, edge bounds, pan direction, minimap mapping and click/drag threshold.
- [x] GameMode direct join, feedback, damage pool consumption and reset; update UE lifecycle coverage for legal 1000/2000/2000 distribution.
- [x] HUD clickable rankings, mouse-anchor zoom, drag, minimap with camera frame, wider centered numeric health bars, score in focus card, vector icons and floating damage.
- [x] Compile Editor/Game, run relevant native and UE tests, render full/compact/focused/stress views and check interactions through real HUD methods.
- [x] Update local instructions, proposal and paste-ready Feishu text with current implementation and verification results; preserve the supplied template structure and record exact access outcome.
- [ ] Update the supplied Feishu online document and attach the video. Blocked: Chrome inventory, direct URL binding, session reset and new-tab creation all return `nodeRepl.fetch request failed`; no online changes were made.

## Review focus

Leaderboard identity is captured when the row is pressed so sorting during a click cannot select another player. Pointer release outside the viewport cancels dragging. Team counters are core-owned and do not change on death. Minimap and overlays have input priority over the arena. Damage and minimap work remains bounded for 5000 people. Tooltips and short labels retain meaning for unfamiliar icons.

## External document

User supplied （私有文档链接已省略） . Chrome inventory and direct-tab binding currently fail with `nodeRepl.fetch request failed`; login alone has not restored the tool connection. No online edits have been made.

## Verification progress

- Native Match: 30,165 assertions, zero failed groups. Damage pool: 11 assertions passed. ArenaView and pointer gesture math: 23 assertions passed.
- Regular Editor/Game builds succeeded. UE report `Saved/ObserverUIAutomation/index.json`: five tests succeeded with zero warnings or failures, including actual HUD handlers and expanded viewer lifecycle.
- Rendered and inspected 1080p focus view, 1280×720 focus view, and 1024×768 results. Short-window join feed visibility and scaled results fixed after independent review.
- HUD regression calls the real handlers; it does not inject operating-system mouse events. Local UE source confirms the current visible-cursor capture settings preserve mouse movement and first-click input.
- The new independent 5000-participant overview sample contains 4496 frames: frame mean 4.449 ms / P95 5.643 ms; 3340–4498 participants alive during sampling. These are local offscreen results, not a live-stream frame-rate guarantee.
- New normal-speed video verified at 4057 frames / 338.083333 seconds / 1920×1080 / H.264 yuv420p / 12 fps capture. It covers the full round, results and next-round start. Metadata created UTC 2026-10-02 12:44:44.
- Local DOCX and Feishu snippet finalized. ZIP/XML, required content, template page geometry, tables/drawings and unchanged non-body parts passed structural checks. Pagination remains visually unverified because no Word/LibreOffice renderer is available on this machine.
