# LiveSpoutOutput

Windows D3D12 local companion output for UE 5.8. The sender name reads `DouyinLiveProvider.AppId` from Game.ini and uses `mate_spout_<AppId>` (no process ID). An empty or placeholder AppId uses `mate_spout_local` for local development. Native Spout calls use the actual matching SDK headers, import libraries and DLLs; there is no guessed native ABI.

## Host configuration

Enable this plugin, then add to the host's `Config/DefaultEngine.ini`:

```ini
[/Script/Engine.Engine]
GameViewportClientClassName=/Script/LiveSpoutOutput.LiveSpoutViewportClient
```

The game viewport client creates the main `SViewport` with `RenderDirectlyToWindow=false`, then fixes the `FSceneViewport` at **1920x1080**. The main scene and `AHUD::DrawHUD` Canvas draw into this same render target once. Slate scales it for the local window. Shrinking the local window does not lower the output texture resolution. UE's viewport geometry-to-pixel mapping handles scaled mouse input. This does not use a second SceneCapture.

The sender uses Slate's presentation callback as its synchronization point, filters the actual game window, and sends the fixed **game render target**, rather than the window backbuffer. Independent Slate overlays placed outside the game render target are not included. The current Arena HUD uses Canvas and is inside this target.

Default automatic output is restricted to a real game world on Windows D3D12. Editor/PIE viewports, commandlets, NullRHI and `-NoSpout` are skipped. `-cloud-game 1` and `-cloud-game=1` always disable output, so the platform's portrait `-screen-width`/`-screen-height` are not overridden. Development `-GWLocalTest` needs explicit `-Spout`; a platform `-token` or inherited-credential `-GWCredentialStdin` takes precedence over local testing. Shipping ignores the local-test flag.

Cloud launches translate the platform's space-delimited or equals `-screen-width`, `-screen-height` and `-screen-fullscreen` flags to Unreal's resolution request and game viewport. Width/height must be decimal integers in 320..8192; fullscreen accepts 0 or 1. The requested cloud render target dimensions are preserved, including portrait. These requests do not call `SaveSettings` or modify persistent user preferences. This only adapts the local Unreal rendering contract; cloud qualification, launch token, input/mobile gesture support and platform acceptance remain separate integration checks.

Example standalone editor game test:

```powershell
& 'F:\UNREAL\UE_5.8\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'D:\demo\GeometricWarfare\GeometricWarfare.uproject' -game -dx12 -GWLocalTest -Spout -log
```

Packaged local launch starts output automatically; `-NoSpout` disables it. No gameplay call or actor placement is needed. The viewport creates a transient sender actor, recreates it when the game world changes, and stops/unbinds/flushes the sender before teardown.

For an explicit development resize fixture, add `-SpoutResizeTest`. At 2, 6 and 10 game seconds, the game's own Slate window is resized to 1280x720, 320x180 and 640x360. The fixed main render target remains 1920x1080. This fixture is excluded from Shipping and is inert when Spout is disabled (including cloud launches). Verify the emitted size logs together with independent receiver frames; the test does not automate other applications' windows.

## Implementation and validation boundaries

Sender staging, D3D11On12 resource wrapping, acquire/release, and GPU fence lifetime come from the pinned MIT implementation documented in [UPSTREAM.md](UPSTREAM.md). Two staging slots are enabled. This implementation uses `SubmitAndBlockUntilGPUIdle()` for the RHI-to-native handoff; performance must be measured on the target machine. Output is a GPU texture copy, with no CPU pixel readback.

Compilation and the launch-policy automation test do not prove receiver image correctness. Verify with an actual external Spout receiver: select the exact sender, confirm 1920x1080, confirm visible HUD text and canvas elements, resize the game window, and check frame continuity, colors, level travel, minimize/restore and shutdown. The companion's whitelist/authorization and end-to-end acceptance remain separate platform checks.

The platform document still contains an older ProductID_ProcessID instruction alongside its `mate_spout_{gameId}` example and the author's correction. This plugin follows the explicit app ID correction; confirm the exact name against the actual companion during integration.
