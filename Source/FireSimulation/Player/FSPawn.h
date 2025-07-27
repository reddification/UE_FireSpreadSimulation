// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpectatorPawn.h"
#include "FSPawn.generated.h"

class UInputAction;

UCLASS()
class FIRESIMULATION_API AFSPawn : public ASpectatorPawn
{
	GENERATED_BODY()

public:
	AFSPawn();

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	UInputAction* SetOnFireInputAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float TraceDistance = 20000;

private:
	void SetOnFireInput();
};
