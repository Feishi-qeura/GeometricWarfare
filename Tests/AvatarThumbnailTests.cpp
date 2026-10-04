#include "../Plugins/DouyinLiveBridge/Source/DouyinLiveBridge/Private/AvatarThumbnail.h"
#include <array>
#include <iostream>
#include <vector>

int main() {
    int failures=0;
    const auto check=[&](bool ok,const char* message) { if(!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; } };
    std::array<std::uint8_t,16385> output{};
    output.back()=173;
    const std::array<std::uint8_t,4> one={11,22,33,44};
    check(douyin::MakeAvatarThumbnailBGRA(one.data(),one.size(),1,1,output.data(),16384), "single pixel produces thumbnail");
    bool repeated=true;
    for(int i=0;i<16384;++i) repeated=repeated && output[i]==one[i%4];
    check(repeated && output.back()==173, "BGRA and alpha preserved without writing past 64x64 output");

    // 128x64 input: crop 32 pixels from each horizontal edge.
    std::vector<std::uint8_t> wide(128*64*4);
    for(int y=0;y<64;++y) for(int x=0;x<128;++x) {
        const int p=(y*128+x)*4; wide[p]=static_cast<std::uint8_t>(x); wide[p+1]=static_cast<std::uint8_t>(y); wide[p+2]=91; wide[p+3]=255;
    }
    check(douyin::MakeAvatarThumbnailBGRA(wide.data(),wide.size(),128,64,output.data(),16384), "landscape input accepted");
    check(output[0]==32 && output[1]==0 && output[16380]==95 && output[16381]==63, "landscape image is centered and cropped");
    // 64x128 input: crop 32 pixels from each vertical edge.
    for(int y=0;y<128;++y) for(int x=0;x<64;++x) {
        const int p=(y*64+x)*4; wide[p]=static_cast<std::uint8_t>(x); wide[p+1]=static_cast<std::uint8_t>(y);
    }
    check(douyin::MakeAvatarThumbnailBGRA(wide.data(),wide.size(),64,128,output.data(),16384), "portrait input accepted");
    check(output[0]==0 && output[1]==32 && output[16380]==63 && output[16381]==95, "portrait image is centered and cropped");
    std::vector<std::uint8_t> large(1024*1024*4,117);
    check(douyin::MakeAvatarThumbnailBGRA(large.data(),large.size(),1024,1024,output.data(),16384), "maximum resolution reduces to fixed thumbnail");
    check(output[0]==117 && output[16383]==117 && output.back()==173, "maximum source keeps fixed output allocation");
    check(!douyin::MakeAvatarThumbnailBGRA(large.data(),large.size(),1025,1024,output.data(),16384), "oversized image rejected");
    check(!douyin::MakeAvatarThumbnailBGRA(large.data(),large.size(),0,1024,output.data(),16384), "empty dimensions rejected");
    check(!douyin::MakeAvatarThumbnailBGRA(large.data(),1,1024,1024,output.data(),16384), "truncated source rejected");
    check(!douyin::MakeAvatarThumbnailBGRA(one.data(),one.size(),1,1,output.data(),1), "undersized output rejected");
    check(!douyin::MakeAvatarThumbnailBGRA(nullptr,4,1,1,output.data(),16384), "null input rejected");
    std::cout << "Avatar thumbnail tests: " << failures << " failures\n";
    return failures?1:0;
}
