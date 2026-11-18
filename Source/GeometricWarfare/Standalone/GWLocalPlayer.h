#pragma once
#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "GWLocalPlayer.generated.h"

// The name a player goes by in rooms (it rides in the join URL as ?Name=): their Steam name while Steam is signed
// in. The default online service is Null, whose "nickname" is the machine's login id, so without this every Steam
// room showed ids like AVHERD-5B83... on its roster and in "X's hub".
UCLASS()
class GEOMETRICWARFARE_API UGWLocalPlayer : public ULocalPlayer
{
    GENERATED_BODY()
public:
    virtual FString GetNickname() const override;
};
