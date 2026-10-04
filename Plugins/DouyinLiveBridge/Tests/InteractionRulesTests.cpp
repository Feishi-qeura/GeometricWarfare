#include "../Source/DouyinLiveBridge/Public/DouyinInteractionRules.h"
#include <iostream>

int main() {
    int failures=0,assertions=0;
    const auto check=[&](bool value,const char* message) {
        ++assertions; if(!value) { ++failures; std::cerr<<"FAIL: "<<message<<'\n'; }
    };
    check(douyin::IsValidLikeDelta(1),"one new like is valid");
    check(douyin::IsValidLikeDelta(100),"bounded batch of one hundred new likes is valid");
    check(!douyin::IsValidLikeDelta(0),"zero cumulative update is not an event delta");
    check(!douyin::IsValidLikeDelta(-1),"negative like count rejected");
    check(!douyin::IsValidLikeDelta(101),"over-limit count rejected rather than truncated");
    check(!douyin::IsValidLikeDelta(.5),"fractional count below one rejected");
    check(!douyin::IsValidLikeDelta(1.5),"fractional count inside range rejected");
    check(!douyin::IsValidLikeDelta(NAN),"NaN rejected");
    check(!douyin::IsValidLikeDelta(INFINITY),"infinite count rejected");
    check(!douyin::IsValidLikeDelta(-INFINITY),"negative infinity rejected");
    check(!douyin::IsValidLikeDelta(1e100),"huge input rejected before integer conversion");
    std::cout<<"Like delta rules: "<<assertions<<" assertions, "<<failures<<" failures\n";
    return failures?1:0;
}
