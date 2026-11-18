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
    // The promo capture's sound sheet (GWPromoCapture.cpp): while Notes is set, Update writes down every shot fired
    // ("shot": weapon, team), shot gone ("gone": weapon, team + 256 if it ran out), effect ("fx": kind, style), death
    // ("die": unit kind) and world cue (its system name), so the video's sound effects land on the frames showing them.
    struct FNote
    {
        FName What;
        int32 A = 0, B = 0;
        FVector From = FVector::ZeroVector, To = FVector::ZeroVector;
        float Size = 0;
    };
    static TArray<FNote>* Notes;
    // A one-shot cue by system name (NS_GW_<Name>) for the world's own moments, such as blocks landing while a
    // page assembles; it shares this frame's budget with the replicated cues.
    void Cue(FName Name, FVector Location, FLinearColor Color, float Scale = 1)
    {
        if (Notes)
            Notes->Add({Name, 0, 0, Location, Location, Scale});
        Burst(Name, Location, Color, Scale, FRotator::ZeroRotator, 9000);
    }
    // The title page's brawl (GWShowcase.cpp) plays its own cues: each frame starts the budget afresh round its
    // camera focus and keeps the floating paper bits there (Update does this in a match).
    void Showcase(FVector InFocus, float Delta);
    // A cue pointed along Rotation, a spark arc between two points, a jolt of camera shake, and a looping cue (a
    // burn, a frost) held in one of a few slots: on (and moved to At) or off.
    void CueToward(FName Name, FVector Location, FLinearColor Color, float Scale, FRotator Rotation)
    {
        Burst(Name, Location, Color, Scale, Rotation, 9000);
    }
    void Zap(FVector From, FVector To, FLinearColor Color, float Width)
    {
        Arc(From, To, Color, Width);
    }
    void Jolt(float Amount)
    {
        Shake = FMath::Min(1.f, Shake + Amount);
    }
    void Hold(int32 Slot, FName Name, bool On, FVector At, FLinearColor Color, float Scale);
    // The title page's burning and frozen units: their loops follow them as a match's do (unit id, place; size by id).
    void ShowcaseStatus(const TArray<TPair<int32, FVector>>& Burning, const TArray<TPair<int32, FVector>>& Slowed,
                        const TMap<int32, float>& Sizes);

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
        // It was about to run out of range when last seen: it drops to the floor instead of bursting on a hit.
        bool RanOut = false;
    };
    UPROPERTY() TObjectPtr<AActor> Owner;
    UPROPERTY() TMap<FName, TObjectPtr<UNiagaraSystem>> Systems;
    UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> BurnLoops;
    UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> FrostLoops;
    UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> Held;
    UPROPERTY() TObjectPtr<UNiagaraComponent> Ambient;
    // The title page's warm-up: each system spawned once, tiny and inside the floor, one a frame (so none hitches the
    // brawl the first time it is called for), and destroyed again once all of them have drawn.
    UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> Warming;
    int32 Warmed = 0, WarmFrames = 0;
    TArray<int32> BurnOwners, FrostOwners;
    TMap<int32, FUnitMemory> Units;
    TMap<int32, FShotMemory> Shots;
    uint32 LastEffect = 0, Seed = 0;
    int32 LastLevel = 1, Budget = 0, Spawned = 0;
    double Now = 0;
    float Shake = 0;
    FVector Focus = FVector::ZeroVector;
    // The state being played this frame (a tower's cue looks up which kind of tower it is), only during Update.
    const AGWGameState* Playing = nullptr;
    UNiagaraSystem* System(FName Name);
    UNiagaraComponent* Burst(FName Name, FVector Location, FLinearColor Color, float Scale = 1,
                             FRotator Rotation = FRotator::ZeroRotator, float Range = 2600);
    void Arc(FVector From, FVector To, FLinearColor Color, float Width);
    void Effect(const FGWEffectView& E);
    void Loops(FName Name, TArray<TObjectPtr<UNiagaraComponent>>& Pool, TArray<int32>& Owners,
               const TArray<TPair<int32, FVector>>& Wanted, const TMap<int32, float>& Scales, int32 Limit,
               FLinearColor Color = FLinearColor::White);
};
