# Upstream dependency provenance

Selected upstream: [GPUbrainStorm/UE5_Spout2_DX12](https://github.com/GPUbrainStorm/UE5_Spout2_DX12), pinned commit `ac226a0b282ce07b7dbd25bc0d433ab7c45d353c`.

Copied files: sender component implementation/header, sender source/world policy enums, and matching `Source/ThirdParty/include`, Win64 `Spout.dll`, `SpoutDX12.dll`, `Spout.lib`, `SpoutDX12.lib`. No upstream prebuilt Unreal module DLL/PDB or receiver implementation is included. The upstream explicitly supports UE 5.8's `ISlateViewportProvider` presentation callback.

Changes to the sender: module/class/file renaming, exact fixed viewport source captured by shared pointer in the Slate callback, rejection of any source other than 1920x1080, and source transition matching the separate scene viewport's SRVMask state. Staging buffers use COPY_SOURCE before and after native access. The public D3D11On12 `CreateWrappedResource` API is used with matching COPY_SOURCE in/out states, because the SDK's convenience wrapper releases to PRESENT/COMMON, which disagrees with Unreal's tracked staging state. Both GPU completion before native access and the fence after native release are retained. The host viewport wrapper controls launch/world eligibility, fixed resolution and transient sender lifecycle.

The wrapper contract was checked against [the official native SDK source](https://github.com/leadedge/Spout2/blob/master/SPOUTSDK/SpoutDirectX/SpoutDX/SpoutDX12/SpoutDX12.cpp), including its `CreateWrappedResource` InState/OutState. The send path calls the public `SendTexture` inside one balanced acquire/release pair, since `SendDX11Resource` already performs its own pair. This avoids nesting native resource acquisition. No code from that additional checkout is distributed in this plugin.

The upstream Unreal integration is MIT, copyright 2025 Mohamad Darwish. Preserve and distribute `Licenses/GPUbrainStorm-MIT.txt`. The native SDK headers carry Lynn Jarvis's BSD 2-clause notices (including credit to Malcolm Bechard in SpoutCommon.h); preserve and distribute `Licenses/Spout-BSD-2-Clause.txt`. Build rules stage both notice files with packaged output. Header notices remain intact.

Also reviewed the platform-recommended [kessoning/Spout-UE5](https://github.com/kessoning/Spout-UE5). Its README claims MIT but the linked LICENSE was unavailable during review; its source compatibility claim is UE 5.5+. The selected upstream has an actual license file, UE 5.8 callback handling and explicit staging/fence lifetime handling.

The exact original SDK release version is not established by the vendored DLL filenames. These binaries are retained as a matching set from the pinned integration, without claiming a newer SDK version. `NATIVE-SHA256.txt` records content hashes for provenance and reproducibility.
