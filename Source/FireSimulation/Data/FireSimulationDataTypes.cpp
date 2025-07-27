#include "FireSimulationDataTypes.h"

#include "Interfaces/Combustible.h"

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
