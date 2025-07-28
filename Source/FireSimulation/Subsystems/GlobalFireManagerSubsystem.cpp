// Fill out your copyright notice in the Description page of Project Settings.


#include "GlobalFireManagerSubsystem.h"

#include "Actors/FireSource.h"
#include "Components/BoxComponent.h"

void UGlobalFireManagerSubsystem::RegisterFireSource(AFireSource* FireSource)
{
	if (!GlobalFireSource.IsValid())
		GlobalFireSource = FireSource;
	
	FireSources.Add(FireSource);
}

void UGlobalFireManagerSubsystem::UnregisterFireSource(AFireSource* FireSource)
{
	if (FireSource == GlobalFireSource)
		GlobalFireSource.Reset();
	
	FireSources.Remove(FireSource);
}

void UGlobalFireManagerSubsystem::StartFire(const FVector& Origin)
{
	if (GlobalFireSource.IsValid())
		GlobalFireSource->StartFireAtLocation(Origin);
}

void UGlobalFireManagerSubsystem::SetFireSimulationPaused(bool bPaused)
{
	if (GlobalFireSource.IsValid())
	{
		GlobalFireSource->SetFirePaused(bPaused);
	}
}

TArray<AFireSource*> UGlobalFireManagerSubsystem::GetRelevantFireSources(AFireSource* FireSource, float RelevancyDistance) const
{
	TArray<AFireSource*> RelevantFireSources;
	FVector NewFireSourceOrigin = FireSource->GetActorLocation();
	for (const auto& ExistingFireSource : FireSources)
	{
		if (!ExistingFireSource.IsValid())
		{
			ensure(false);
			continue;
		}
		
		UBoxComponent* ExistingFireSourceBounds = ExistingFireSource->GetVolumeBox();
		if (!IsValid(ExistingFireSourceBounds))
		{
			ensure(false);
			continue;
		}

		FVector LocalOrigin = ExistingFireSourceBounds->GetComponentTransform().InverseTransformPosition(NewFireSourceOrigin);
		FVector BoxExtent = ExistingFireSourceBounds->GetUnscaledBoxExtent() + FVector(RelevancyDistance);;
		const bool bInside = FMath::Abs(LocalOrigin.X) <= BoxExtent.X && FMath::Abs(LocalOrigin.Y) <= BoxExtent.Y && FMath::Abs(LocalOrigin.Z) <= BoxExtent.Z;
		if (bInside)
			RelevantFireSources.Add(ExistingFireSource.Get());
	}
	
	return RelevantFireSources;
}
