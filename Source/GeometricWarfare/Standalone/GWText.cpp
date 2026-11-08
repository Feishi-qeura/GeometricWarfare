#include "GWText.h"
#include "GWSettings.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Misc/Paths.h"
namespace
{
FString SelectedCulture = TEXT("en");
bool Registered = false;
} // namespace
void GWText::Initialize()
{
    if (!Registered)
    {
        for (const TCHAR* C : {TEXT("en"), TEXT("zh-Hans"), TEXT("ja"), TEXT("de")})
        {
            FStringTableRegistry::Get().Internal_LocTableFromFile(
                FName(*(FString(TEXT("GW.")) + C)), TEXT("GW"), FString(TEXT("Localization/Game/")) + C + TEXT(".csv"),
                FPaths::ProjectContentDir());
        }
        Registered = true;
    }
    auto* Settings = UGWUserSettings::Get();
    FString Initial = Settings ? Settings->Language : FString();
    if (Initial.IsEmpty())
        Initial = FPlatformMisc::GetDefaultLanguage();
    SetCulture(Initial);
}
void GWText::SetCulture(const FString& C)
{
    SelectedCulture = C.StartsWith(TEXT("zh"))   ? TEXT("zh-Hans")
                      : C.StartsWith(TEXT("ja")) ? TEXT("ja")
                      : C.StartsWith(TEXT("de")) ? TEXT("de")
                                                 : TEXT("en");
    FInternationalization::Get().SetCurrentCulture(SelectedCulture);
    if (auto* S = UGWUserSettings::Get())
    {
        S->Language = SelectedCulture;
        S->SaveSettings();
    }
}
FString GWText::Culture()
{
    return SelectedCulture;
}
FText GWText::Get(const FString& Key)
{
    FName Table(*(FString(TEXT("GW.")) + SelectedCulture));
    return FText::FromStringTable(Table, Key);
}
FText GWText::Number(const FString& Key, const TCHAR* Parameter, int32 Value)
{
    FFormatNamedArguments Args;
    Args.Add(Parameter, Value);
    return FText::Format(Get(Key), Args);
}
