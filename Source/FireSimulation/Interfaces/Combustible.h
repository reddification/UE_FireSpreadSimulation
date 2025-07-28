// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Combustible.generated.h"

// This class does not need to be modified.
UINTERFACE()
class UCombustible : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class FIRESIMULATION_API ICombustible
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	virtual void AddCombustion(float NewConbustionState) = 0;
	virtual bool IsIgnited() const = 0;
	virtual void StartFire() = 0;
	virtual float GetCombustionRate() const = 0;
};
