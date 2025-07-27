// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GlobalFireManagerSubsystem.generated.h"

class AFireSource;
/**
 * 
 */
UCLASS()
class FIRESIMULATION_API UGlobalFireManagerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterFireSource(AFireSource* FireSource);
	void UnregisterFireSource(AFireSource* FireSource);
	void StartFire(const FVector& Origin);

private:
	TArray<AFireSource*> GetRelevantFireSources(AFireSource* FireSource, const float RelevancyDistance) const;
	TSet<TWeakObjectPtr<AFireSource>> FireSources;

	// at first I had an idea that there should be a fire source per starting fire (i.e. exploding a barrel, throwing a molotov)
	// but then there's a problem of synchronizing and/or merging overlapping fire sources but I'm short on time by now
	// so fuck it. for the demo there will be just one global fire sim. what could go wrong after all
	TWeakObjectPtr<AFireSource> GlobalFireSource;	
};
