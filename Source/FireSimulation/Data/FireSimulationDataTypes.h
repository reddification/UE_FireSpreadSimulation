#pragma once

#include "FireSimulationDataTypes.generated.h"

class ICombustible;

USTRUCT(BlueprintType)
struct FPhysicMaterialCombustionParameters
{
	GENERATED_BODY()
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(UIMin = 0.f, ClampMin = 0.f))
	float IgnitionRate = 0.4f; // affects how fast cell ignites

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(UIMin = 1.f, ClampMin = 1.f))
	float BurningStrength = 100.f; // affects how high fire goes (in unreal units)

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(UIMin = 0.f, ClampMin = 0.f))
	float BurnoutRate = 0.015f; // affects how slow fire goes out
};

struct FFireCell
{
	FFireCell() {  }
	FFireCell(const FFireCell&);
	FFireCell& operator=(const FFireCell&);

	// this one is local. in theory, there can be multiple AFireSource that affect the same cell. IDK. perhaps make a UFireSubsystem that has like global map of fire cells
	// but then again - currently fire cells indices are relative to actor root
	std::atomic<float> CombustionState = 0.f;
	
	float IgnitionRate = 1.f;
	float BurnoutRate = 0.005f;
	float FireHeight = 100.f; // affects verticality

	FVector Location;
	
	TWeakObjectPtr<AActor> CombustibleActor;
	TScriptInterface<ICombustible> CombustibleInterface;
	
	bool bObstacle = false;
	bool bHasCombustibleInterface = false; // used for async execution since calling .IsValid() or IsValid(Actor) is not safe outside of GT 

	bool IsObstacle() const { return bObstacle; }
	bool IsIgnited() const { return CombustionState.load() >= 1.f; }
	void SetActor(AActor* Actor);
};

struct FCellularAutomataPendingNode
{
	FFireCell* IgnitedBy;
	TArray<FFireCell*> Igniting;

	float CombustionState = 0.f;
	float Offset = 0.f; // using radial coords
	float CombustionRate = 0.f;
	TWeakObjectPtr<class UCombustionComponent> CombustionComponent;
	FVector IgnitionDirection;	
};