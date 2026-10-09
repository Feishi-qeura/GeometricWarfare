// Standalone receiver for the project's packaged Spout output.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <string>
#include <vector>
#include "SpoutDX.h"

static bool AllOpaque = true;
static constexpr unsigned RequiredNewFrames = 240;

static bool WriteBmp(const char* Path, const BITMAPFILEHEADER& File, const BITMAPINFOHEADER& Info,
    const std::vector<unsigned char>& Pixels)
{
    FILE* Output = nullptr;
    if (fopen_s(&Output, Path, "wb") != 0 || !Output) return false;
    const bool Written = fwrite(&File, sizeof(File), 1, Output) == 1
        && fwrite(&Info, sizeof(Info), 1, Output) == 1
        && fwrite(Pixels.data(), Pixels.size(), 1, Output) == 1;
    return fclose(Output) == 0 && Written;
}

static bool SaveBmp(const char* Path, ID3D11Texture2D* Texture, ID3D11Device* Device, ID3D11DeviceContext* Context)
{
    D3D11_TEXTURE2D_DESC Desc{};
    Texture->GetDesc(&Desc);
    if (Desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && Desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM
        && Desc.Format != DXGI_FORMAT_R10G10B10A2_UNORM) return false;
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.MiscFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* Stage = nullptr;
    if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &Stage))) return false;
    Context->CopyResource(Stage, Texture);
    D3D11_MAPPED_SUBRESOURCE Mapped{};
    if (FAILED(Context->Map(Stage, 0, D3D11_MAP_READ, 0, &Mapped))) { Stage->Release(); return false; }
    std::vector<unsigned char> Pixels(static_cast<size_t>(Desc.Width) * Desc.Height * 4);
    unsigned long long AlphaHistogram[256]{};
    unsigned long long Bright = 0, HiddenBright = 0;
    for (unsigned Y = 0; Y < Desc.Height; ++Y) for (unsigned X = 0; X < Desc.Width; ++X)
    {
        const auto* Src = static_cast<const unsigned char*>(Mapped.pData) + Y * Mapped.RowPitch + X * 4;
        auto* Dst = Pixels.data() + (static_cast<size_t>(Y) * Desc.Width + X) * 4;
        if (Desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM)
        {
            const unsigned Packed = *reinterpret_cast<const unsigned*>(Src);
            Dst[0] = static_cast<unsigned char>(((Packed >> 20) & 1023) * 255 / 1023);
            Dst[1] = static_cast<unsigned char>(((Packed >> 10) & 1023) * 255 / 1023);
            Dst[2] = static_cast<unsigned char>((Packed & 1023) * 255 / 1023);
        }
        else
        {
            Dst[0] = Src[Desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ? 2 : 0];
            Dst[1] = Src[1];
            Dst[2] = Src[Desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ? 0 : 2];
        }
        const unsigned A = Desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM
            ? ((*reinterpret_cast<const unsigned*>(Src) >> 30) & 3) * 85 : Src[3];
        ++AlphaHistogram[A];
        AllOpaque &= A == 255;
        if (Dst[0] + Dst[1] + Dst[2] > 600) { ++Bright; if (A == 0) ++HiddenBright; }
        Dst[3] = static_cast<unsigned char>(A);
    }
    printf("ALPHA total=%u bright=%llu hidden_bright=%llu opaque=%llu transparent=%llu all_opaque=%d\n",
        Desc.Width * Desc.Height, Bright, HiddenBright, AlphaHistogram[255], AlphaHistogram[0], AllOpaque);
    for (unsigned I = 0; I < 256; ++I) if (AlphaHistogram[I]) printf("ALPHA_BIN value=%u count=%llu\n", I, AlphaHistogram[I]);
    fflush(stdout);
    Context->Unmap(Stage, 0);
    Stage->Release();
    BITMAPFILEHEADER File{};
    File.bfType = 0x4d42;
    File.bfOffBits = sizeof(File) + sizeof(BITMAPINFOHEADER);
    File.bfSize = File.bfOffBits + static_cast<DWORD>(Pixels.size());
    BITMAPINFOHEADER Info{};
    Info.biSize = sizeof(Info);
    Info.biWidth = static_cast<LONG>(Desc.Width);
    Info.biHeight = -static_cast<LONG>(Desc.Height);
    Info.biPlanes = 1;
    Info.biBitCount = 32;
    Info.biCompression = BI_RGB;
    if (!WriteBmp(Path, File, Info, Pixels)) return false;
    for (unsigned Mode = 0; Mode < 2; ++Mode)
    {
        std::vector<unsigned char> Display = Pixels;
        for (size_t I = 0; I < Display.size(); I += 4)
        {
            if (Mode == 1) for (unsigned C = 0; C < 3; ++C)
                Display[I + C] = static_cast<unsigned char>(static_cast<unsigned>(Display[I + C]) * Display[I + 3] / 255);
            Display[I + 3] = 255;
        }
        const std::string Extra = std::string(Path) + (Mode == 0 ? ".rgb.bmp" : ".black-composite.bmp");
        if (!WriteBmp(Extra.c_str(), File, Info, Display)) return false;
    }
    return true;
}

static void Usage()
{
    printf("Usage: receiver-package-probe.exe [--sender NAME] [--output FILE.bmp] [--list]\n"
        "Defaults: --sender mate_spout_local --output spout-received.bmp\n"
        "--list enumerates senders without receiving textures.\n"
        "Capture requires 240 new 1920x1080 frames within 50 seconds and opaque BMP samples.\n");
}

int main(int Argc, char** Argv)
{
    std::string Sender = "mate_spout_local", Output = "spout-received.bmp";
    bool ListOnly = false;
    for (int I = 1; I < Argc; ++I)
    {
        const std::string Arg = Argv[I];
        if (Arg == "--help") { Usage(); return 0; }
        if (Arg == "--list") { ListOnly = true; continue; }
        if ((Arg == "--sender" || Arg == "--output") && I + 1 < Argc && Argv[I + 1][0] != '\0'
            && std::string(Argv[I + 1]).rfind("--", 0) != 0)
        {
            (Arg == "--sender" ? Sender : Output) = Argv[++I];
            continue;
        }
        fprintf(stderr, "ERROR invalid or missing argument: %s\n", Arg.c_str());
        Usage();
        return 2;
    }
    spoutDX Receiver;
    printf("receiver-start sender=%s\n", Sender.c_str());
    const auto ListSenders = [&Receiver]()
    {
        for (const std::string& Name : Receiver.GetSenderList())
        {
            unsigned W = 0, H = 0;
            HANDLE Share = nullptr;
            DWORD Format = 0;
            const bool Info = Receiver.GetSenderInfo(Name.c_str(), W, H, Share, Format);
            printf("SENDER name=%s info=%d size=%ux%u format=%lu shared=%p\n", Name.c_str(), Info, W, H, Format, Share);
        }
        fflush(stdout);
    };
    ListSenders();
    if (ListOnly) return 0;
    if (!Receiver.OpenDirectX11()) { printf("ERROR device\n"); return 2; }
    Receiver.SetReceiverName(Sender.c_str());
    Receiver.SetAdapterAuto(true);
    printf("receiver-device-ready adapter=%d\n", Receiver.GetAdapter());
    fflush(stdout);
    const ULONGLONG Start = GetTickCount64();
    unsigned Frames = 0, NewFrames = 0, LastReport = 0;
    bool Saved = false, WrongSize = false, WrongSender = false;
    while (GetTickCount64() - Start < 50000)
    {
        const unsigned Elapsed = static_cast<unsigned>(GetTickCount64() - Start);
        if (Elapsed / 3000 > LastReport)
        {
            LastReport = Elapsed / 3000;
            unsigned W = 0, H = 0;
            HANDLE Share = nullptr;
            DWORD Format = 0;
            const bool Found = Receiver.GetSenderInfo(Sender.c_str(), W, H, Share, Format);
            printf("info found=%d size=%ux%u format=%lu count=%d\n", Found, W, H, Format, Receiver.GetSenderCount());
            if (Frames == 0) ListSenders();
            fflush(stdout);
        }
        const bool Received = Receiver.ReceiveTexture();
        ID3D11Texture2D* Texture = Receiver.GetSenderTexture(); // SDK owns and allocates this texture.
        // Consume the sender-change flag so the SDK can resume delivery.
        if (Receiver.IsUpdated()) printf("receiver-updated %ux%u\n", Receiver.GetSenderWidth(), Receiver.GetSenderHeight());
        if (Received && Texture)
        {
            ++Frames;
            const bool IsNew = Receiver.IsFrameNew();
            if (IsNew) ++NewFrames;
            D3D11_TEXTURE2D_DESC Desc{};
            Texture->GetDesc(&Desc);
            WrongSize |= Desc.Width != 1920 || Desc.Height != 1080;
            WrongSender |= Sender != Receiver.GetSenderName();
            if (Frames == 1 || (IsNew && NewFrames % 120 == 0))
                printf("frame=%u new=%u size=%ux%u format=%u sender=%s\n", Frames, NewFrames, Desc.Width, Desc.Height,
                    static_cast<unsigned>(Desc.Format), Receiver.GetSenderName());
            if (IsNew && (NewFrames == 91 || NewFrames % 120 == 0))
            {
                Saved = SaveBmp(Output.c_str(), Texture, Receiver.GetDX11Device(), Receiver.GetDX11Context());
                printf("saved=%d sampled_frame=%u sampled_new=%u\n", Saved, Frames, NewFrames);
            }
            fflush(stdout);
        }
        else if (Frames > 0 && !Receiver.IsConnected())
        {
            printf("sender-disconnected frames=%u new=%u\n", Frames, NewFrames);
            break;
        }
        if (NewFrames >= RequiredNewFrames && Saved) { printf("capture-target-met new=%u\n", NewFrames); break; }
        Sleep(16);
    }
    Receiver.ReleaseReceiver();
    Receiver.CloseDirectX11();
    printf("RESULT frames=%u new=%u wrongsize=%d saved=%d wrongsender=%d\n", Frames, NewFrames, WrongSize, Saved, WrongSender);
    return NewFrames >= RequiredNewFrames && !WrongSize && !WrongSender && Saved && AllOpaque ? 0 : 1;
}
