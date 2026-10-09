# Douyin credential launcher

The native Windows GUI launcher is the packaged root entry. It accepts the official `-token=...` argument, strips it, and starts `GeometricWarfare/Binaries/Win64/GeometricWarfare-Win64-Shipping.exe` when present, otherwise the Development `GeometricWarfare.exe`, relative to its own directory. The UE process receives only `-GWCredentialStdin`; the credential arrives as raw UTF-8 followed by LF over anonymous stdin. UE then forwards it to the actual Unity SDK host through that host's init frame.

UE records startup arguments before game/provider initialization. Passing a token directly to the UE executable therefore bypasses this protection. Select the root launcher in the companion/platform configuration. The platform's original launcher command line still exists in OS process metadata; the launcher never writes it or the token to logs, output or files. It does not place the token in the child's arguments or environment.

The launcher accepts case-insensitive `-token=` and also separated `-token value` for compatibility. Duplicate, empty, missing, CR/LF, invalid UTF-16 and credentials exceeding 16 KiB UTF-8 including LF fail before launching. With a token, case-insensitive `-GWLocalTest` (including a value suffix) is removed. Caller-supplied `-GWCredentialStdin` is always removed; only validated input generates it. All other argument values, including empty strings, use Windows quoting and are forwarded exactly.

`CreatePipe` requests 64 KiB. The child starts suspended with an explicit inherited-handle allowlist. The launcher writes the complete credential and closes the writer before resuming the child, allowing UE's nonblocking startup read to receive a complete line. Write/resume failure terminates only this owned suspended child. The launcher waits for UE and returns its exit code. It creates no visible window.

Build with the installed MSVC x64 toolchain:

```powershell
.\Tools\DouyinLauncher\Build.ps1
pwsh -NoProfile -File .\Tools\DouyinLauncher\Test-Launcher.ps1
```

Output is `tmp/DouyinLauncherBuild/DouyinLauncher.exe`. The build also creates a fake child used only by tests. Packaging must preserve the original UE root bootstrap as a backup and replace the root entry with this launcher; retain the actual inner UE exe and all its dependencies. Do not ship `FakeChild.exe` or the generated test folders. This tool does not package or replace the root bootstrap by itself.

The eleven fixture cases use no real credential. They cover Unicode and spaces, quotes, trailing backslashes, empty arguments, executable paths containing spaces, token/local-flag stripping, exact UTF-8/LF delivery, validation limits and preserving child exit code 37. This is launcher boundary evidence; actual platform startup, UE credential reading and live-room authentication still require integration testing.
