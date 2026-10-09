# Local Spout alpha diagnosis (2026-10-07)

## Reproduction before the fix

The independent `spoutDX` D3D11 receiver in `tmp/spout-research/receiver-alpha-test.cpp` reads the actual SDK-owned shared texture. Unlike the earlier `receiver-test.cpp`, it preserves received alpha, checks opacity, and records both RGB-only and alpha-composited black-background views. It does not change the game sender.

Baseline launch: UnrealEditor standalone `-game -unattended -nosplash -dx12 -GWLocalTest -Spout -RenderOffscreen -ResX=640 -ResY=360 -Windowed -GWVisualTest -GWCombatTest=laser -Seconds=45`, with a fresh GUID `-GWProgressSlot` and explicit absolute log path.

`Saved/Spout-Alpha-Red-Receiver.log` records **275 received / 275 new frames**, all **1920x1080**, **DXGI 24 (`R10G10B10A2_UNORM`)**. The game exited 0. The receiver exited **1**, correctly failing the opaque-frame assertion:

- Total sampled pixels: **2,073,600**.
- Alpha 0: **2,070,828 (99.866%)**.
- Alpha 255: **2,772**.
- Bright RGB pixels with alpha 0: **1,894,451**.

The same received frame's RGB-only image contains the complete white scene and Canvas HUD. Compositing that frame with its original alpha over black reproduces the reported symptom: almost everything disappears, leaving small vector outlines. The diagnostic BMP files are `tmp/spout-research/spout-alpha-red.bmp.rgb.bmp` and `tmp/spout-research/spout-alpha-red.bmp.black-composite.bmp`; the original received alpha is retained in `spout-alpha-red.bmp`.

The earlier `Plugins/LiveSpoutOutput/VALIDATION.md` proved frame delivery, dimensions and RGB content, but its receiver forcibly saved alpha 255. Those earlier images therefore did **not** establish alpha-compositor correctness.

## Root cause and fix

Unreal's completed scene/Canvas render target contains correct RGB but uses an intermediate alpha channel. The previous Spout sender copied that alpha unchanged to the shared texture. A receiver that honors alpha makes the scene and most Canvas HUD transparent. This explains why the ordinary game/cloud image can look correct while the local composited output is nearly black.

`LiveSpoutSenderComponent.cpp` now draws `DrawClearQuadAlpha(..., 1.0f)` onto the private staging texture **after** the RGB GPU copy and **before** the D3D11On12/Spout handoff, for game-viewport sources only. UE RenderCore's implementation writes with `CW_ALPHA`, preserving copied RGB. The render pass loads/stores the existing output and restores it to `CopySrc` for the native handoff. The game source texture remains unchanged. Explicit render-target senders retain authored transparency. Cloud launch policy and fixed 1920x1080 output are unchanged.

## Verification status

The shared Editor build compiled and linked the modified plugin successfully. The alpha-aware receiver also compiled successfully against the vendored SDK. A fresh post-fix run of `tmp/spout-research/Run-AlphaProbe.ps1 -Phase Green -Resize` recorded:

- **Game exit 0 / receiver exit 0**.
- **196 received frames / 196 new frames**, all **1920x1080**, DXGI format **24**, `wrongsize=0`.
- Actual GPU alpha readbacks at received frames **91** and **120**: each has **2,073,600 alpha-255 pixels**, **0 transparent pixels**, and **0 bright RGB pixels hidden by zero alpha**.
- The final saved RGB-only and alpha-composited black-background BMPs are **byte-for-byte identical** (8,294,454 bytes). The composited image was visually inspected and contains the white scene, title, timer, team totals, command legend, leaderboard, notifications, minimap and bottom messages.
- The actual preview backbuffer changed **640x360 → 320x180** while all received frames stayed **1920x1080**. The fixture requested 1280x720 without an actual logged backbuffer change. The run ended before the third resize stage; no third-stage restore is claimed.

Logs: `Saved/Spout-Alpha-Green-Game.log` and `Saved/Spout-Alpha-Green-Receiver.log`. Isolated progress slot: `fef9a48a3e2d4b39b1a13a0c275f1cb9`. The probe preserves alpha and samples complete frames; it does not claim per-pixel readback of every received frame.

PNG diagnostics are in `tmp/spout-research/spout-alpha-red.rgb.png`, `spout-alpha-red.black-composite.png`, and `spout-alpha-green.black-composite.png`. The red image is computed from the **pre-fix actual received alpha**, and the green image from the **post-fix actual received alpha**.

**Actual Douyin Live Companion acceptance remains pending.** The independent SDK receiver establishes correct opaque texture content and reproduces/resolves the reported black-compositing symptom, but it does not exercise the authenticated Companion UI or its launch workflow.
