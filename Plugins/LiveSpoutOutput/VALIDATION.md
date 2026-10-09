# Validation evidence (2026-10-04)

The native Spout output was tested with an independent `spoutDX` receiver built against the vendored public SDK headers/import library. The receiver uses the SDK-owned texture from `ReceiveTexture()` / `GetSenderTexture()`, copies that texture for inspection, and checks each received frame's dimensions. This receiver readback is a test probe; the game sender uses GPU copies.

## Actual GPU output and application-owned resize

Run logs are under the project's ignored `Saved` directory:

- `Saved/Spout-Final-Resize-GPU.log`
- `Saved/Spout-Final-Resize-Receiver.log`
- `Saved/Screenshots/Spout-Final-Resize-Received.png`
- `Saved/Screenshots/Spout-Final-Resize-Preview.png`

Both game and receiver exited with code 0. The receiver recorded **491 received frames / 491 new frames**, all **1920x1080**, DXGI format 24 (`R10G10B10A2_UNORM`), with `wrongsize=0` and a saved image. Sender name was `mate_spout_YOUR_APP_ID`. The received image visibly contains the main scene, Canvas team totals/timer, left command legend, right leaderboard, minimap, and bottom messages.

The game's own `-SpoutResizeTest` fixture requested preview sizes 1280x720, 320x180 and 640x360. The actual Slate backbuffer log proves **640x360 -> 320x180 -> 640x360**; the 1280x720 request did not produce a logged backbuffer change during this offscreen run and is not claimed as verified. The game render target remained 1920x1080 throughout. The preview screenshot is 640x360; the independently received image is 1920x1080. No operating-system window automation was used for this run. The receiver detected sender disconnection when the game shut down.

Launch flags: `-game -dx12 -GWLocalTest -Spout -SpoutResizeTest -RenderOffscreen -ResX=640 -ResY=360 -Windowed -GWVisualTest -GWCombatTest=laser -GWCaptureAt=12 -GWCaptureName=Spout-Final-Resize-Preview`, with an isolated `-GWProgressSlot`.

An earlier independent run also received 749 new 1920x1080 frames and saved `Saved/Screenshots/Spout-Received-1920.png`.

## Cloud rendering adapter

`Saved/Cloud-Portrait-Test.log` and `Saved/Screenshots/Cloud-Portrait-1080.png` record a successful offscreen launch with `-cloud-game 1 -screen-width 1080 -screen-height 1920 -screen-fullscreen 1 -GWLocalTest -GWVisualTest -GWCombatTest=laser -GWCaptureAt=6`. A second run of the equals-form flags (`-cloud-game=1 -screen-width=1080 -screen-height=1920 -screen-fullscreen=1`) also exited with code 0; its log is `Saved/Spout-Final-Cloud.log` and screenshot is `Saved/Screenshots/Spout-Final-Cloud-1080.png`. Both actual screenshots are **1080x1920**, the logs record the requested cloud viewport, and no Spout sender starts. Portrait layout uses the existing game HUD; these tests establish the requested rendering dimensions, not an optimized portrait layout or mobile gesture acceptance.

## Scope and remaining integration checks

These are actual SDK texture reception tests, not proof of Douyin Live Companion acceptance. Companion sender discovery/name, whitelist, launch credentials and platform acceptance require the real companion workflow. The platform document has conflicting older PID naming text; the plugin follows its explicit corrected app ID name.

Level travel, minimize/restore, prolonged sessions and target-machine performance are not covered by the short GPU runs. The sender retains an RHI GPU-idle handoff for correctness; its performance cost needs measurement. Native input clicks follow the Unreal viewport; complex mobile multitouch gestures are not implemented by this plugin.

## Production launch without credentials

A standalone game launch without `-GWLocalTest` or token exited with code 0 using Unreal's own `-Seconds=8` timer and `-ExecCmds="Shot showui filename=D:/demo/GeometricWarfare/Saved/Screenshots/Spout-Production-NoToken.png -nosuffix"`. Other flags were `-game -dx12 -RenderOffscreen -ResX=960 -ResY=540 -Windowed`. `Saved/Spout-Production-NoToken.log` records default automatic Spout startup and an engine-owned normal exit. The 960x540 window screenshot visibly shows the waiting-for-companion state, 0/5000 spectators and an empty leaderboard. The sender source remains fixed at 1920x1080. This is a standalone Editor executable game run; packaged executable and native launcher acceptance are separate checks.

`-GWVisualTest` is deliberately local-only and does not trigger capture/exit in production mode. The engine-owned screenshot and timer flags above can exercise production rendering without enabling mock users.

## Final compilation and automation

The final Editor and Game Development builds succeeded, with logs `Saved/Spout-Final3-Editor-Build.log` and `Saved/Spout-Final3-Game-Build.log`. Complete `Automation RunTests GeometricWarfare` under NullRHI completed with **22 succeeded, 0 failed, 0 not run, 0 in process**, confirmed in `Saved/Spout-Final3-Automation/index.json` and `Saved/Spout-Final3-Automation.log`. This includes launch policy, provider host contract, production/local admission, advanced commands, viewer lifecycle and round reporting. A fresh GUID progress slot isolates this run's game progress.

An automation process exit code alone is insufficient: the previous run returned 0 with two failed tests; the final claim above uses the actual JSON test counts. Native dependency SHA-256 verification reported no mismatches. `git diff --check` returned 0.
