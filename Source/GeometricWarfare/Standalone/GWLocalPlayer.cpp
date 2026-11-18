#include "GWLocalPlayer.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"

FString UGWLocalPlayer::GetNickname() const
{
    IOnlineSubsystem* Steam = GetWorld() ? Online::GetSubsystem(GetWorld(), STEAM_SUBSYSTEM) : nullptr;
    const IOnlineIdentityPtr Identity = Steam ? Steam->GetIdentityInterface() : nullptr;
    if (Identity.IsValid() && Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
    {
        // The name travels as a URL option, so the characters that would end or split one are replaced.
        FString Name = Identity->GetPlayerNickname(0).TrimStartAndEnd();
        for (TCHAR& Letter : Name)
            if (Letter < 32 || Letter == TEXT('?') || Letter == TEXT('=') || Letter == TEXT('#') || Letter == TEXT('"'))
                Letter = TEXT('_');
        if (!Name.IsEmpty())
            return Name;
    }
    return Super::GetNickname();
}
