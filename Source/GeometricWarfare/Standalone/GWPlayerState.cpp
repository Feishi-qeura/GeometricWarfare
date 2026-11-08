#include "GWPlayerState.h"
#include "Net/UnrealNetwork.h"
void AGWPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AGWPlayerState, Identity);
    DOREPLIFETIME(AGWPlayerState, UnitId);
    DOREPLIFETIME(AGWPlayerState, Team);
    DOREPLIFETIME(AGWPlayerState, Ready);
    DOREPLIFETIME(AGWPlayerState, Throwable);
    DOREPLIFETIME(AGWPlayerState, Achievements);
    DOREPLIFETIME(AGWPlayerState, Hat);
    DOREPLIFETIME(AGWPlayerState, Title);
}
