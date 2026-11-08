#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GWFx.generated.h"
class AGWGameState;
class AGWWorld;
class UNiagaraComponent;
class UNiagaraSystem;
struct FGWEffectView;
struct FGWUnitView;

// Client-side Niagara director for the 3D arena. It only reads replicated state:
// each effect id, death, spawn, hit and status change becomes a pooled lightweight system,
// so listen servers and clients show the same skill feedback without simulating anything.
UCLASS()
class GEOMETRICWARFARE_API UGWFxDirector : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(AActor* InOwner);
    void Update(const AGWGameState& State, const AGWWorld& World, FVector Focus, float Delta);
    void Stop();
    FVector CameraShake(float Delta);
    int32 SpawnCount() const
    {
        return Spawned;
    }
    int32 LoopCount() const;
    static FLinearColor UnitColor(const FGWUnitView& Unit);

private:
    struct FUnitMemory
    {
        bool Alive = false;
        float HP = 0, Armor = 0, Radius = 25;
        uint8 Kind = 0, Shape = 0;
        FVector Position = FVector::ZeroVector;
        FLinearColor Color = FLinearColor::White;
        double LastShield = -10, LastHeal = -10;
        bool Seen = false;
    };
    struct FShotMemory
    {
        FVector Position = FVector::ZeroVector;
        float Remaining = 0;
        uint8 Weapon = 0, Team = 0;
        bool Seen = false;
    };
    UPROPERTY() TObjectPtr<AActor> Owner;
    UPROPERTY() TMap<FName, TObjectPtr<UNiagaraSystem>> Systems;
    UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> BurnLoops;
    UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> FrostLoops;
    UPROPERTY() TObjectPtr<UNiagaraComponent> Ambient;
    TArray<int32> BurnOwners, FrostOwners;
    TMap<int32, FUnitMemory> Units;
    TMap<int32, FShotMemory> Shots;
    uint32 LastEffect = 0, Seed = 0;
    int32 LastLevel = 1, Budget = 0, Spawned = 0;
    double Now = 0;
    float Shake = 0;
    FVector Focus = FVector::ZeroVector;
    UNiagaraSystem* System(FName Name);
    UNiagaraComponent* Burst(FName Name, FVector Location, FLinearColor Color, float Scale = 1,
                             FRotator Rotation = FRotator::ZeroRotator, float Range = 2600);
    void Arc(FVector From, FVector To, FLinearColor Color, float Width);
    void Effect(const FGWEffectView& E);
    void Loops(FName Name, TArray<TObjectPtr<UNiagaraComponent>>& Pool, TArray<int32>& Owners,
               const TArray<TPair<int32, FVector>>& Wanted, const TMap<int32, float>& Scales, int32 Limit,
               FLinearColor Color = FLinearColor::White);
};
