#include "FireSimulationDataTypes.h"

#include "Interfaces/Combustible.h"

bool FFireCell::IsIgnited() const
{
	if (!bHasCombustibleInterface)
		return CombustionState.load() >= 1.f;

	return CombustionState.load() >= 1.f && bCombustibleActorIgnited;
}

void FFireCell::SetActor(AActor* Actor)
{
	if (auto Combustible = Cast<ICombustible>(Actor))
	{
		CombustibleInterface.SetObject(Actor);
		CombustibleInterface.SetInterface(Combustible);
		CombustibleActor = Actor;
		CombustionRate *= Combustible->GetCombustionRate();
		bHasCombustibleInterface = true;
	}
}
