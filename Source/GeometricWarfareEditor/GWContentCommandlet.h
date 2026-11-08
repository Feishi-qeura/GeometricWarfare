#pragma once
#include "Commandlets/Commandlet.h"
#include "GWContentCommandlet.generated.h"

// Rebuilds every generated presentation asset under /Game/Sketch from code:
// hand-drawn sprite textures, unlit ink/paper materials and lightweight Niagara systems.
// UnrealEditor-Cmd GeometricWarfare.uproject -run=GWContent [-dump]
UCLASS()
class UGWContentCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UGWContentCommandlet();
    virtual int32 Main(const FString& Params) override;
};
