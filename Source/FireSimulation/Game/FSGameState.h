// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FSGameState.generated.h"

class UWindComponent;
/**
 * 
 */
UCLASS()
class FIRESIMULATION_API AFSGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AFSGameState();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UWindComponent* WindComponent;
};
