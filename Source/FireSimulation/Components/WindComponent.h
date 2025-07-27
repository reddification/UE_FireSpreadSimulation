// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WindComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class FIRESIMULATION_API UWindComponent : public UActorComponent
{
	GENERATED_BODY()

private:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FWindChangedEvent, const FVector& WindDirection, const float WindStrength);
	
public:
	UWindComponent();
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	FORCEINLINE FVector GetWindDirection() const { return WindDirection.Vector(); } 
	FORCEINLINE float GetWindStrength() const { return WindStrength; } 

	UFUNCTION(BlueprintCallable)
	void SetWindDirection(const FRotator& NewDirection);

	UFUNCTION(BlueprintCallable)
	void SetWindStrength(const float NewStrength);
	
	mutable FWindChangedEvent WindChangedEvent;

protected:
	UPROPERTY(ReplicatedUsing=OnRep_WindDirection, EditAnywhere, BlueprintReadOnly)
	FRotator WindDirection;

	// unrea units per seconds i guess
	UPROPERTY(ReplicatedUsing=OnRep_WindStrength, EditAnywhere, BlueprintReadOnly, meta=(UIMin = 0.0, ClampMin = 0.0))
	float WindStrength = 1.f;

private:
	UFUNCTION()
	void OnRep_WindDirection();

	UFUNCTION()
	void OnRep_WindStrength();
};