#pragma once
#include <cstddef>
#include <cstdint>

namespace liveinteraction {
constexpr int AvatarSide=64;
constexpr std::size_t AvatarBytes=AvatarSide*AvatarSide*4;
// CPU-only center crop. The caller uploads only these 16 KiB to the GPU.
// Nearest pixel-center sampling also supports small source avatars safely.
inline bool MakeAvatarThumbnailBGRA(const std::uint8_t* source, std::size_t sourceBytes,
                                   int width, int height, std::uint8_t* output, std::size_t outputBytes) {
    if(!source || !output || width<=0 || height<=0 || width>1024 || height>1024
        || sourceBytes<static_cast<std::size_t>(width)*static_cast<std::size_t>(height)*4
        || outputBytes<AvatarBytes) return false;
    const int crop=width<height?width:height;
    const int left=(width-crop)/2,top=(height-crop)/2;
    for(int y=0;y<AvatarSide;++y) for(int x=0;x<AvatarSide;++x) {
        const int sx=left+((2*x+1)*crop)/(2*AvatarSide);
        const int sy=top+((2*y+1)*crop)/(2*AvatarSide);
        const std::size_t from=(static_cast<std::size_t>(sy)*width+sx)*4;
        const std::size_t to=static_cast<std::size_t>(y*AvatarSide+x)*4;
        for(int channel=0;channel<4;++channel) output[to+channel]=source[from+channel];
    }
    return true;
}
}
