#include "FireSimulationDataTypes.h"

#include "Interfaces/Combustible.h"

FFireCell::FFireCell(const FFireCell& Other)
{
	CombustionState.store(Other.CombustionState.load());
}

FFireCell& FFireCell::operator=(const FFireCell& Other)
{
	CombustionState.store(Other.CombustionState.load());
	return *this;
}

void FFireCell::SetActor(AActor* Actor)
{
	if (auto Combustible = Cast<ICombustible>(Actor))
	{
		CombustibleInterface.SetObject(Actor);
		CombustibleInterface.SetInterface(Combustible);
		CombustibleActor = Actor;
		bHasCombustibleInterface = true;
	}
}
