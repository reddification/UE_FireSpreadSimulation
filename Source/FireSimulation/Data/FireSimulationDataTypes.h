#pragma once

#include "FireSimulationDataTypes.generated.h"

class ICombustible;

USTRUCT(BlueprintType)
struct FPhysicMaterialCombustionParameters
{
	GENERATED_BODY()
	
	// affects how fast cell ignites
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(UIMin = 0.f, ClampMin = 0.f))
	float IgnitionRate = 0.4f;

	// affects how high fire goes (in unreal units)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(UIMin = 1.f, ClampMin = 1.f))
	float BurningStrength = 100.f;

	// affects how slow fire goes out
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(UIMin = 0.f, ClampMin = 0.f))
	float BurnoutRate = 0.015f;
};

struct FFireCell
{
    FFireCell()
    {
    	CombustionState.store(0.f);
    };

    FFireCell(const FFireCell& Other) : 
          CombustionRate(Other.CombustionRate),
          BurnoutRate(Other.BurnoutRate),
          FireHeight(Other.FireHeight),
          Location(Other.Location),
          CombustibleActor(Other.CombustibleActor),
          CombustibleInterface(Other.CombustibleInterface),
          bObstacle(Other.bObstacle),
          bHasCombustibleInterface(Other.bHasCombustibleInterface)
    {
    	CombustionState.store(Other.CombustionState.load());
    }

    FFireCell& operator=(const FFireCell& Other)
    {
        CombustionState.store(Other.CombustionState.load());
        CombustionRate = Other.CombustionRate;
        BurnoutRate = Other.BurnoutRate;
        FireHeight = Other.FireHeight;
        Location = Other.Location;
        CombustibleActor = Other.CombustibleActor;
        CombustibleInterface = Other.CombustibleInterface;
        bObstacle = Other.bObstacle;
        bHasCombustibleInterface = Other.bHasCombustibleInterface;
        return *this;
    }

    FFireCell(FFireCell&& Other) noexcept
        :
          CombustionRate(Other.CombustionRate),
          BurnoutRate(Other.BurnoutRate),
          FireHeight(Other.FireHeight),
          Location(MoveTemp(Other.Location)),
          CombustibleActor(MoveTemp(Other.CombustibleActor)),
          CombustibleInterface(MoveTemp(Other.CombustibleInterface)),
          bObstacle(Other.bObstacle),
          bHasCombustibleInterface(Other.bHasCombustibleInterface)
    {
    	CombustionState.store(Other.CombustionState.load());
        // Other.CombustionState.store(0.f);
    }

    FFireCell& operator=(FFireCell&& Other) noexcept
    {
        CombustionState.store(Other.CombustionState.load());
        CombustionRate = Other.CombustionRate;
        BurnoutRate = Other.BurnoutRate;
        FireHeight = Other.FireHeight;
        Location = MoveTemp(Other.Location);
        CombustibleActor = MoveTemp(Other.CombustibleActor);
        CombustibleInterface = MoveTemp(Other.CombustibleInterface);
        bObstacle = Other.bObstacle;
        bHasCombustibleInterface = Other.bHasCombustibleInterface;

        // Other.CombustionState.store(0.f);
        return *this;
    }

    ~FFireCell() = default;

	// this one is local. in theory, there can be multiple AFireSource that affect the same cell. IDK. perhaps make a UFireSubsystem that has like global map of fire cells
	// but then again - currently fire cells indices are relative to actor root
	std::atomic<float> CombustionState;
	
	float CombustionRate = 1.f;
	float BurnoutRate = 0.005f;
	float FireHeight = 100.f; // affects verticality

	FVector Location = FVector::ZeroVector;
	
	TWeakObjectPtr<AActor> CombustibleActor;
	TScriptInterface<ICombustible> CombustibleInterface;
	
	bool bObstacle = false;
	bool bHasCombustibleInterface = false; // used for async execution since calling .IsValid() or IsValid(Actor) is not safe outside of GT 

	bool IsObstacle() const { return bObstacle; }
	bool IsIgnited() const { return CombustionState.load() >= 1.f; }
	void SetActor(AActor* Actor);
};