#include "FSPawn.h"

#include "EnhancedInputComponent.h"
#include "Interfaces/Combustible.h"

AFSPawn::AFSPawn()
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called to bind functionality to input
void AFSPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(SetOnFireInputAction, ETriggerEvent::Triggered, this, &AFSPawn::SetOnFireInput);
	}
}

void AFSPawn::SetOnFireInput()
{
	FVector ViewLocation;
	FRotator ViewRotation;
	GetActorEyesViewPoint(ViewLocation, ViewRotation);
	FHitResult Hit;
	FVector TraceEnd = ViewLocation + ViewRotation.Vector() * TraceDistance;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	bool bSetOnFire = false;
	bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params);
	if (bHit && Hit.GetActor())
	{
		auto Combustible = Cast<ICombustible>(Hit.GetActor());
		if (!Combustible->IsIgnited())
		{
			Combustible->StartFire();
			bSetOnFire = true;
		}
	}

	if (bSetOnFire)
		DrawDebugLine(GetWorld(), ViewLocation, Hit.ImpactPoint, FColor::Green, false, 2.f, 0, 1);
	else
		DrawDebugLine(GetWorld(), ViewLocation, TraceEnd, FColor::Red, false, 2.f, 0, 1);
}

