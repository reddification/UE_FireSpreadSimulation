// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/FireSimulationDataTypes.h"
#include "Engine/DeveloperSettings.h"
#include "FireSimulationSettings.generated.h"

/**
 * 
 */
UCLASS(config=Game, defaultconfig)
class FIRESIMULATION_API UFireSimulationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Config)
	TMap<TEnumAsByte<EPhysicalSurface>, FPhysicMaterialCombustionParameters> CombustionParameters;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Config)
	TArray<TEnumAsByte<EPhysicalSurface>> IncombustibleSurfaces;
};
