// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/Combustible.h"
#include "CombustibleActor.generated.h"

UCLASS()
class FIRESIMULATION_API ACombustibleActor : public AActor, public ICombustible
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ACombustibleActor();

	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
protected:
	virtual void BeginPlay() override;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UStaticMeshComponent* StaticMeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	UCurveLinearColor* CombustionColorCurve;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin = 0.f, UIMin = 0.f))
	float MaxCombustionLevel = 1.f;

	// personal combustion rate which is multiplied with physics material combustion rate
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin = 0.f, UIMin = 0.f))
	float CombustionRate = 0.25;

public: // ICombustionInterface
	virtual void SetCombustion(float NewCombustionState) override;
	virtual bool IsIgnited() const override;
	virtual void StartFire() override;
	virtual float GetCombustionRate() const override;
	
private:
	UPROPERTY(ReplicatedUsing=OnRep_CombustionState)
	float CombustionState = 0.f;

	UPROPERTY()
	UMaterialInstanceDynamic* DynamicMaterialInstance;
	
	void UpdateCombustionState();

	UFUNCTION()
	void OnRep_CombustionState(float PrevValue);
};
