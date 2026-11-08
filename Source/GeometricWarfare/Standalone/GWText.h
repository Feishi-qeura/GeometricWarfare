#pragma once
#include "CoreMinimal.h"
namespace GWText
{
void Initialize();
void SetCulture(const FString& Culture);
FString Culture();
FText Get(const FString& Key);
FText Number(const FString& Key, const TCHAR* Parameter, int32 Value);
} // namespace GWText
