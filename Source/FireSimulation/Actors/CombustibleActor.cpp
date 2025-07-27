// Fill out your copyright notice in the Description page of Project Settings.

#include "CombustibleActor.h"

#include "Curves/CurveLinearColor.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Subsystems/GlobalFireManagerSubsystem.h"

// Sets default values
ACombustibleActor::ACombustibleActor()
{
	PrimaryActorTick.bCanEverTick = false;

	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("StaticMeshComponent");
	SetRootComponent(StaticMeshComponent);

	bReplicates = true;
}

void ACombustibleActor::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	FDoRepLifetimeParams DoRepLifetimeParams { COND_None, REPNOTIFY_OnChanged, true };
	DOREPLIFETIME_WITH_PARAMS_FAST(ACombustibleActor, CombustionState, DoRepLifetimeParams);
}

void ACombustibleActor::BeginPlay()
{
	Super::BeginPlay();

	if (!ensure(CombustionColorCurve))
	{
		SetReplicates(false);
		return;
	}

	DynamicMaterialInstance = StaticMeshComponent->CreateDynamicMaterialInstance(0);
	if (!ensure(DynamicMaterialInstance))
	{
		SetReplicates(false);
		return;
	}
	
	if (CombustionState > 0.f)
		UpdateCombustionState();
}

void ACombustibleActor::AddCombustion(float NewCombustionState)
{
	if (HasAuthority())
	{
		CombustionState = FMath::Clamp(CombustionState + NewCombustionState, 0.f, MaxCombustionLevel);
		MARK_PROPERTY_DIRTY_FROM_NAME(ACombustibleActor, CombustionState, this);
	}
}

bool ACombustibleActor::IsIgnited() const
{
	return CombustionState >= MaxCombustionLevel;
}

void ACombustibleActor::StartFire()
{
	if (!HasAuthority())
		return;

	AddCombustion(MaxCombustionLevel);
	if (auto GlobalFireSubsystem = GetWorld()->GetSubsystem<UGlobalFireManagerSubsystem>())
		GlobalFireSubsystem->StartFire(GetActorLocation());
}

void ACombustibleActor::UpdateCombustionState()
{
	if (CombustionColorCurve && DynamicMaterialInstance)
		DynamicMaterialInstance->SetVectorParameterValue(FName("Color"), CombustionColorCurve->GetLinearColorValue(CombustionState));
}

void ACombustibleActor::OnRep_CombustionState(float PrevValue)
{
	UpdateCombustionState();
}
