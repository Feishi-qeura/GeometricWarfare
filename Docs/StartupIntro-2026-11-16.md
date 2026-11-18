# Startup intro

Plays the supplied six-second OpenFrqush MP4 once per game instance, before the title menu. The runtime copy lives in `Content/Movies`; the original under `output/StudioIntro` is preserved.

Implementation: use the existing Canvas HUD with MediaAssets/WmfMedia, preserving the video's aspect ratio. Reuse the HUD's composite fonts, hand lettering, ink strokes and geometric motifs for a small top-right skip hint. The video and hint fade together; a fresh Space press fades them out in 0.28 seconds. Other keys and clicks are consumed. Title music waits until playback ends. Existing language selection supplies English, Simplified Chinese, Japanese and German.

Failure behavior: missing file, decoder error or eight seconds without playback progress returns to the title. Dedicated servers do not play media. Map changes and returning to the title do not replay it. `-GWNoIntro` is an explicit development/test bypass.

## Implementation and verification plan

- [x] Add and run native timeline checks for opening, normal fades, early/repeated skip, completion and stalled playback; observe failure before implementing the timeline.
- [x] Connect media lifetime, audio, Canvas rendering, input interception and per-instance startup guard. Stage the MP4 as a loose movie for the Windows decoder.
- [x] Add the localized hint through the existing translation source and regenerate all four CSV files.
- [x] Build the editor/game targets. Run timeline and existing core checks, then real rendered playback and skip checks, including localized screenshots and title return.

Routine choices above are implementation decisions within the requested feature. Existing uncommitted work stays in place; no commit or push is requested.

## Verification results

- Native timeline checks: observed six initial failures, then zero failures after implementation.
- Existing `Scripts/Test-CoreRules.ps1`: all three suites passed (DefenseRulesTests, MetaRulesTests, ExpeditionRulesTests).
- Localization generator: 980 keys across four languages, UTF-8 roundtrip and format placeholders verified.
- Win64 Development game build passed. Editor build passed using separate hot-reload module suffix `17161`, preserving the open editor's DLLs/session.
- Full Cook, stage and archive passed. A first staging attempt using old cooked shaders failed at launch because the newly enabled decoder needs additional global shaders; the subsequent full Cook resolved this. Future releases must Cook with WmfMedia enabled.
- Packaged `GW.Standalone.Intro`: success, zero failed checks, process exit 0. Tests decode 1920x1080 video and nonzero audio samples, render all four localized hints, consume Escape/clicks during playback, route actual Space input into the fade, release media objects on skip/end, prevent replay on title return, and confirm title buttons return.
- Four hint screenshots and the restored title were visually inspected. Review also corrected the hint's graphite shadow to share its fade.
- Source, runtime and packaged MP4 SHA-256 match: `B56B464F24129161934934673C20C67112AABC289C51D29EC14EDBE920274EDC`.

Artifacts (project-relative):

- Runnable verification package: `Saved/StartupIntro/Package-20261116/Windows/GeometricWarfare.exe`
- Screenshots: `Saved/StartupIntro/Preview/`
- Automation JSON: `Saved/StartupIntro/AutomationReportFinal/index.json`
- Playback log: `Saved/StartupIntro/package-playback-final.log`

Verification runs used `-saveddirsuffix=IntroVerifyFinal`; real player saves/settings were not used. The packaged automation runner lacks the engine HTML report template, so its report is JSON. This does not affect the successful test result or game playback. No phone, other-platform, or live PIE-session acceptance is claimed.

To repeat the rendered checks, launch the inner packaged game executable with:

```text
-RenderOffscreen -windowed -ResX=1600 -ResY=900 -ForceRes -unattended -nosplash -nosteam -saveddirsuffix=IntroVerifyRepeat -ExecCmds="Automation RunTests GW.Standalone.Intro" -TestExit="Automation Test Queue Empty"
```
