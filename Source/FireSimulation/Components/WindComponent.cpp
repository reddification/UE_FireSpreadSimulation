// Fill out your copyright notice in the Description page of Project Settings.


#include "WindComponent.h"

#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"


UWindComponent::UWindComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UWindComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	FDoRepLifetimeParams DoRepLifetimeParams {COND_None, REPNOTIFY_OnChanged, true };
	DOREPLIFETIME_WITH_PARAMS_FAST(UWindComponent, WindDirection, DoRepLifetimeParams)
	DOREPLIFETIME_WITH_PARAMS_FAST(UWindComponent, WindStrength, DoRepLifetimeParams)
}

void UWindComponent::SetWindDirection(const FRotator& NewDirection)
{
	if (!GetOwner()->HasAuthority())
		return;
	
	if (WindDirection.EqualsOrientation(NewDirection))
		return;
	
	WindDirection = NewDirection;
	WindChangedEvent.Broadcast(WindDirection.Vector(), WindStrength);
	
	MARK_PROPERTY_DIRTY_FROM_NAME(UWindComponent, WindDirection, this);
}

void UWindComponent::SetWindStrength(const float NewStrength)
{
	if (!GetOwner()->HasAuthority())
		return;
	
	if (FMath::IsNearlyEqual(WindStrength, NewStrength, UE_KINDA_SMALL_NUMBER))
		return;
	
	WindStrength = NewStrength;
	WindChangedEvent.Broadcast(WindDirection.Vector(), WindStrength);
	
	MARK_PROPERTY_DIRTY_FROM_NAME(UWindComponent, WindStrength, this);
}

void UWindComponent::OnRep_WindDirection()
{
	WindChangedEvent.Broadcast(WindDirection.Vector(), WindStrength);
}

void UWindComponent::OnRep_WindStrength()
{
	WindChangedEvent.Broadcast(WindDirection.Vector(), WindStrength);
}
