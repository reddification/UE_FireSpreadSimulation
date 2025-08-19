// Fill out your copyright notice in the Description page of Project Settings.


#include "FireSource.h"

#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "Components/BoxComponent.h"
#include "Components/WindComponent.h"
#include "Data/CollisionChannels.h"
#include "Data/FireSimulationDataTypes.h"
#include "Data/LogChannels.h"
#include "GameFramework/GameStateBase.h"
#include "Interfaces/Combustible.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Settings/FireSimulationSettings.h"
#include "Subsystems/GlobalFireManagerSubsystem.h"

#define FIRESIM_LOG_ASYNC(Text, Verbosity) if (bLogDebugAtomic.load()) \
	{ \
		AsyncTask(ENamedThreads::GameThread, [this, Text = MoveTemp(Text)]() \
		{ \
			UE_VLOG(this, LogFireSimulation, Verbosity, *Text);\
		}); \
	} \

#define FIRESIM_LOG_ASYNC_RAW(Text, Verbosity) if (bLogDebugAtomic.load()) \
	{ \
		AsyncTask(ENamedThreads::GameThread, [this]() \
		{ \
			UE_VLOG(this, LogFireSimulation, Verbosity, Text);\
		}); \
	} \

AFireSource::AFireSource()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	
	NiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>("NiagaraComponent");
	NiagaraComponent->SetupAttachment(GetRootComponent());
	NiagaraComponent->SetAutoActivate(false);

	BoxComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("FireAreaVolume"));
	BoxComponent->SetupAttachment(GetRootComponent());
	
	// key - dot product between wind and world forward vector mapped from 0 to 7. pseudocode: round(acos((wind, world x)) / 2pi * 8)
	// values - group of 3 directions to neighbor cells where this wind direction makes fire spread 
	WindDirectionToNeighbors =
	{
		{ 0, { { 1, 0 }, { 1, 1 }, {1, -1 } } },
		{ 1, { { 1, 1 }, { 1, 0 }, { 0, 1 } } },
		{ 2, { {0, 1 }, {-1, 1 }, {1, 1 } } },
		{ 3, { {-1, 1 }, {0, 1 }, {-1, 0 } } },
		{ 4, { {-1, 0 }, {-1, -1 }, {-1, 1 } } },
		{ 5, { {-1, -1 }, {-1, 0 }, {0, -1 } } },
		{ 6, { {0, -1 }, {1, -1 }, {-1, -1 } } },
		{ 7, { {1, -1}, {0, -1}, {1, 0 } } },
		{ 8, { { 1, 0 }, { 1, 1 }, {1, -1 } } }
	};

	RadialDirections =
	{
		{1, 1},
		{ -1, -1},
		{ 1, 0},
		{0, 1},
		{1, -1},
		{-1, 1},
		{-1, 0},
		{0, -1}
	};
}

void AFireSource::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	FDoRepLifetimeParams DoRepLifetimeParamsForAllFireLocations { COND_InitialOnly, REPNOTIFY_OnChanged, true };
	FDoRepLifetimeParams DoRepLifetimeParamsForNewLocations { COND_None, REPNOTIFY_OnChanged, true };
	DOREPLIFETIME_WITH_PARAMS_FAST(AFireSource, FireLocations, DoRepLifetimeParamsForAllFireLocations);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFireSource, NewFireLocations, DoRepLifetimeParamsForNewLocations);
}

// Called when the game starts or when spawned
void AFireSource::BeginPlay()
{
	Super::BeginPlay();
	
	if (!HasAuthority())
		return;
	
	if (auto GlobalFireManager = GetWorld()->GetSubsystem<UGlobalFireManagerSubsystem>())
	{
		GlobalFireManager->RegisterFireSource(this);
		// auto RelevantFireSources = GlobalFireManager->GetRelevantFireSources(this, RelevantDistanceToOtherFireSources);
		// TODO merge cells of relevant fire sources with this fire source cells
	}

	bLogDebugAtomic.store(bLog_Debug);
	
	auto FireSimSettings = GetDefault<UFireSimulationSettings>();
	SurfacesCombustionParameters = FireSimSettings->CombustionParameters;
	IncombustibleSurfaces = FireSimSettings->IncombustibleSurfaces;	
	if (bStartFireAutomatically)
		StartFire();

	BoxComponent->OnComponentBeginOverlap.AddDynamic(this, &AFireSource::OnSomethingEnteredFireVolume);
	BoxComponent->OnComponentEndOverlap.AddDynamic(this, &AFireSource::OnSomethingLeftFireVolume);
}

void AFireSource::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
		if (auto World = GetWorld())
			if (auto GlobalFireManager = World->GetSubsystem<UGlobalFireManagerSubsystem>())
				GlobalFireManager->UnregisterFireSource(this);
	
	Super::EndPlay(EndPlayReason);
}

// Called every frame
void AFireSource::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!HasAuthority())
	{
		ensure(false);
		return;
	}

	UpdateBurningActors();
	
	if (!bAsyncUpdateRunning.load() && !EdgeCells.IsEmpty())
	{
		AccumulatedDeltaTime.store(DeltaTime);
		SpreadFireAsync();
	}
	else
	{
		AccumulatedDeltaTime.fetch_add(DeltaTime);
	}
}

void AFireSource::StartFireAtLocation(const FVector& NewFireOrigin)
{
	FVector RootToNewOrigin = NewFireOrigin - GetActorLocation();
	int CellX = FMath::RoundToInt(RootToNewOrigin.X / FireCellSize);
	int CellY = FMath::RoundToInt(RootToNewOrigin.Y / FireCellSize);
	StartFireAtCell(FIntVector2(CellX, CellY), NewFireOrigin);
	StartFire();
}

void AFireSource::UpdateBurningActors()
{
	if (PendingBurningActorsUpdates.IsEmpty())
		return;
	
	int Index = LastBurningActorUpdateIndex;
	int Until = FMath::Min(LastBurningActorUpdateIndex + MaxActorsUpdatesPerTick, PendingBurningActorsUpdates.Num());
	
	while (Index < Until)
	{
		auto Cell = Cells.Find(PendingBurningActorsUpdates[Index].Key);
		if (!ensure(Cell))
			continue;
		
		if (Cell->CombustibleActor.IsValid())
		{
			Cell->CombustibleInterface->AddCombustion(PendingBurningActorsUpdates[Index].Value);
			if (Cell->CombustibleInterface->IsIgnited())
				Cell->bCombustibleActorIgnited = true;
		}
		else
		{
			Cell->CombustibleActor.Reset();
			Cell->CombustibleInterface = nullptr;
			Cell->bHasCombustibleInterface = false;
		}

		Index++;
	}

	LastBurningActorUpdateIndex = Index;
	if (Index >= PendingBurningActorsUpdates.Num())
	{
		PendingBurningActorsUpdates.Empty();
		LastBurningActorUpdateIndex = 0;
	}
}

bool AFireSource::GetCell(const FVector& LocationBase, const FCollisionObjectQueryParams& COQP,
                          FFireCell& OutCell) const
{
	// ok. current issue with this function: it gets new cell for a requesting cell. but the same cell can be an obstacle for 1 cell and a valid cell for another cell
	// for example, if 1 cell if lower than other, it might consider it an obstacle as unreacheable, or if the cell is higher that the other
	// but at the same time there can be other neighbor cells that are on the same level
	// so perhaps the fact is a cell is an obstacle shouldn't be decided here, and instead tested individually cell by cell
	
	FHitResult Hit;
	FCollisionShape SweepShape = FCollisionShape::MakeBox(FVector(FireCellSize * .5f, FireCellSize * .5f, BaseFireStrength * 0.5f));
	FVector SweepStart = LocationBase + FVector::UpVector * BaseFireStrength;
	FVector SweepEnd = LocationBase - FVector::UpVector * FireDownwardPropagationThreshold;
	FCollisionQueryParams Params;
	Params.bReturnPhysicalMaterial = true;
	
	bool bHit = GetWorld()->SweepSingleByChannel(Hit, SweepStart, SweepEnd, FQuat::Identity, COLLISION_COMBUSTIBLE, SweepShape, Params);

	if (IsInGameThread())
	{
		UE_VLOG_LOCATION(this, LogFireSimulation, Verbose, SweepStart, 25, FColor::Cyan, TEXT("Sweep start"));
		UE_VLOG_LOCATION(this, LogFireSimulation, Verbose, SweepEnd, 25, FColor::Cyan, TEXT("Sweep end"));
	}
	
	if (bHit)
	{
		OutCell.Location = Hit.ImpactPoint.Z < Hit.TraceStart.Z ? Hit.ImpactPoint : SweepEnd;
		if (Hit.PhysMaterial.IsValid())
		{
			if (IncombustibleSurfaces.Contains(Hit.PhysMaterial->SurfaceType))
			{
				OutCell.bObstacle = true;
			}
			else 
			{
				const auto* SurfaceCombustionParameters = SurfacesCombustionParameters.Find(Hit.PhysMaterial->SurfaceType);
				if (SurfaceCombustionParameters)
				{
					OutCell.CombustionRate = SurfaceCombustionParameters->IgnitionRate;
					OutCell.BurnoutRate = SurfaceCombustionParameters->BurnoutRate;
					OutCell.FireHeight = SurfaceCombustionParameters->BurningStrength;
				}
			}
		}
		else
		{
			OutCell.bObstacle = true;
		}

		OutCell.SetActor(Hit.GetActor());
		
		return !OutCell.bObstacle;
	}
	else
	{
		OutCell.bObstacle = true;
		OutCell.Location = LocationBase;
		return false;
	}
}

void AFireSource::PrepareImmediateInitialCells(const FIntVector2& InitialCellKey, const FVector& BaseLocation, const FCollisionObjectQueryParams& COQP)
{
	for (const FIntVector2& RadialDirection : RadialDirections)
	{
		FFireCell FireCell;
		FVector NewLocation = BaseLocation + FVector(RadialDirection.X, RadialDirection.Y, 0) * FireCellSize;
		GetCell(NewLocation, COQP, FireCell);
		Cells.Emplace(InitialCellKey + RadialDirection, FireCell);
	}
}

bool AFireSource::StartFireAtCell(const FIntVector2& InitialCellKey, const FVector& OriginLocation)
{
	FCollisionObjectQueryParams COQP;
	COQP.AddObjectTypesToQuery(COLLISION_COMBUSTIBLE);

	FFireCell InitialCell;
	bool bInitialCellCreated = GetCell(OriginLocation, COQP, InitialCell);
	if (!bInitialCellCreated)
	{
		UE_VLOG_UELOG(this, LogFireSimulation, Warning, TEXT("Can't start fire at %s"), *OriginLocation.ToString())
		UE_VLOG_LOCATION(this, LogFireSimulation, Warning, OriginLocation, 25.f, FColor::Red, TEXT("Can't start fire here"));
		return false;
	}

	Cells.Emplace(InitialCellKey, InitialCell);
	Cells[InitialCellKey].CombustionState = 1.f;
	EdgeCells.Emplace(InitialCellKey);

	FireLocations.Emplace(OriginLocation);
	NewFireLocations.Emplace(OriginLocation);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFireSource, FireLocations, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFireSource, NewFireLocations, this);
	UpdateFireLocations();
	
	PrepareImmediateInitialCells(InitialCellKey, OriginLocation, COQP);
	
	return true;
}

void AFireSource::StartFire()
{
	auto WindComponent = GetWorld()->GetGameState()->FindComponentByClass<UWindComponent>();
	if (ensure(WindComponent))
	{
		OnWindChanged(WindComponent->GetWindDirection(), WindComponent->GetWindStrength());
		WindComponent->WindChangedEvent.AddUObject(this, &AFireSource::OnWindChanged);
	}
	
	NiagaraComponent->ActivateSystem();
	SetActorTickEnabled(true);
	
	if (!Cells.IsEmpty())
		return;
	
	Cells.Reserve(FireSpreadLimit * FireSpreadLimit);
	EdgeCells.Reserve(FireSpreadLimit * 2);
	FIntVector2 InitialCellKey = { 0, 0 };
	FVector OriginLocation = GetActorLocation(); 
	
	StartFireAtCell(InitialCellKey, OriginLocation);
}

void AFireSource::SetFirePaused(bool bPaused)
{
	SetActorTickEnabled(!bPaused);
}

void AFireSource::SpreadFireAsync()
{
	bAsyncUpdateRunning.store(true);
	// what happens asynchronously:
	// 1. spreading fire by edge cells
	// 2. updating edge cells:
	//	   2.1 mark for add newly ignited cells to edge cells
	//	   2.2 mark actors that have combustible interface 
	//	   2.3 mark for removal those who have no more pending neighbor cells
	// 3. updating contiguous box collisions for damage and nav mesh
	UE_VLOG(this, LogFireSimulation, Log, TEXT("SpreadFireAsync::Start"));
	Async(EAsyncExecution::ThreadPool, [this]()
	{
		int ThreadsCount = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
		
		TArray<TFuture<FAsyncFireSpreadResult>> ThreadResults;
		TArray<FIntVector2> EdgeCellsArray = EdgeCells.Array();
		int EdgeCellsCount = EdgeCells.Num();
		int BaseBatchSize = EdgeCellsCount < ThreadsCount ? EdgeCellsCount : EdgeCellsCount / ThreadsCount;
		int BatchStartIndex = 0;
		int CurrentBatchSize = BaseBatchSize;
		while (BatchStartIndex < EdgeCellsCount)
		{
			CurrentBatchSize = FMath::Min(EdgeCellsCount - BatchStartIndex, BaseBatchSize);
			TFuture<FAsyncFireSpreadResult> Future = Async(EAsyncExecution::Thread, [this, &EdgeCellsArray, BatchStartIndex, CurrentBatchSize]()
			{
				// FIRESIM_LOG_ASYNC_RAW(TEXT("Spread fire in thread"), Verbose);
				FAsyncFireSpreadResult BatchResult;
				SpreadFireBatch(EdgeCellsArray, BatchStartIndex, BatchStartIndex + CurrentBatchSize, BatchResult);
				return BatchResult;
			});

			BatchStartIndex += CurrentBatchSize;
			ThreadResults.Add(MoveTemp(Future));
		}
		
		FAsyncFireSpreadResult AggregatedResult;
		for (int i = 0; i < ThreadResults.Num(); i++)
		{
			auto ThreadFireSpreadResult = MoveTemp(ThreadResults[i].GetMutable());
			AggregatedResult.Aggregate(ThreadFireSpreadResult);
		}

		// Make new cells for ignited cells
		CreateNewFireCells(AggregatedResult);
		
		// 3. updating contiguous box collisions for damage and nav mesh
		// TODO
		
		AsyncTask(ENamedThreads::Type::GameThread, [this, AggregatedResult = MoveTemp(AggregatedResult)] () mutable
		{
			UE_VLOG(this, LogFireSimulation, Log, TEXT("SpreadFireAsync::End\nCombustion actor updates: %d\nIgnited cells: %d\nNot edge cells anymore: %d\nNew cells: %d"),
				AggregatedResult.CombustionActorUpdates.Num(), AggregatedResult.IgnitedCells.Num(), AggregatedResult.NotEdgeCellAnymore.Num(), AggregatedResult.NewCells.Num());

#if WITH_EDITOR
			if (bLog_Debug)
			{
				for (const auto& CombustionActor : AggregatedResult.CombustionActorUpdates)
				{
					auto Index = FIntVector2(CombustionActor.Key.X, CombustionActor.Key.Y);
					UE_VLOG_LOCATION(this, LogFireSimulation_Actors, VeryVerbose, Cells[Index].Location, 25, FColor::Yellow, TEXT("Combustible actor update"));
				}

				for (const auto& IgnitedCell : AggregatedResult.IgnitedCells)
				{
					auto Index = FIntVector2(IgnitedCell.X, IgnitedCell.Y);
					UE_VLOG_LOCATION(this, LogFireSimulation, VeryVerbose, Cells[Index].Location, 25, FColor::Orange, TEXT("Ignited cell"));
				}

				for (const auto& NotEdgeCellAnymore : AggregatedResult.NotEdgeCellAnymore)
				{
					auto Index = FIntVector2(NotEdgeCellAnymore.X, NotEdgeCellAnymore.Y);
					UE_VLOG_LOCATION(this, LogFireSimulation_EdgeCells, Verbose, Cells[Index].Location, 25, FColor::Black, TEXT("Not edge cell anymore"));
				}

				for (const auto& NewCell : AggregatedResult.NewCells)
				{
					UE_VLOG_LOCATION(this, LogFireSimulation, VeryVerbose, NewCell.Value.Location, 25, FColor::White, TEXT("New cell"));
				}
			}
#endif			
			ProcessFireSpreadResult(AggregatedResult);
			bAsyncUpdateRunning.store( false);
		});
	});
}

void AFireSource::CreateNewFireCells(FAsyncFireSpreadResult& AggregatedResult)
{
	if (AggregatedResult.IgnitedCells.IsEmpty())
		return;

	const int ThreadsCount = FPlatformMisc::NumberOfCoresIncludingHyperthreads();

	const int IgnitedCellsNum = AggregatedResult.IgnitedCells.Num();
	int BaseBatchSize = IgnitedCellsNum < ThreadsCount ? IgnitedCellsNum : IgnitedCellsNum / ThreadsCount;
	int BatchStartIndex = 0;
	int CurrentBatchSize = BaseBatchSize;
	TArray<TFuture<TMap<FIntVector2, FFireCell>>> AllNewCellsFutures;
	const auto IgnitedCellsArray = AggregatedResult.IgnitedCells.Array();

	while (BatchStartIndex < IgnitedCellsNum)
	{
		CurrentBatchSize = FMath::Min(IgnitedCellsNum - BatchStartIndex, BaseBatchSize);
		TFuture<TMap<FIntVector2, FFireCell>> Future = Async(EAsyncExecution::ThreadPool, [this, &IgnitedCellsArray, BatchStartIndex, CurrentBatchSize]()
		{
			FCollisionObjectQueryParams COQP;
			COQP.AddObjectTypesToQuery(COLLISION_COMBUSTIBLE);
			
			TMap<FIntVector2, FFireCell> BatchResult;
			BatchResult.Reserve(CurrentBatchSize * 4); // in general, a cell has 3 pending neighbors, but corner cells can have 5 
			int End = BatchStartIndex + CurrentBatchSize;
			for (int i = BatchStartIndex; i < End; i++)
			{
				auto IgnitedCellIndex = IgnitedCellsArray[i];
				for (const auto& RadialDirection : RadialDirections)
				{
					auto TestCellIndex = IgnitedCellIndex + RadialDirection;
					if (!Cells.Contains(TestCellIndex))
					{
						FFireCell NewCell;
						FVector NeighborLocation = Cells[IgnitedCellIndex].Location + FVector(RadialDirection.X, RadialDirection.Y, 0) * FireCellSize;
						GetCell(NeighborLocation, COQP, NewCell);
						BatchResult.Emplace(TestCellIndex, NewCell);
					}
				}
			}
					
			return BatchResult;
		});

		BatchStartIndex += CurrentBatchSize;
		AllNewCellsFutures.Add(MoveTemp(Future));
	}
		
	for (int i = 0; i < AllNewCellsFutures.Num(); i++)
	{
		auto ThreadFireSpreadResult = MoveTemp(AllNewCellsFutures[i].GetMutable());
		AggregatedResult.NewCells.Append(MoveTemp(ThreadFireSpreadResult));
	}
}

void AFireSource::MarkEdgeCellForRemoval(const FIntVector2& EdgeCellIndex, FAsyncFireSpreadResult& Result)
{
	bool bMustRemoveEdgeCell = true;
	//	2.3 mark for removal those who have no more pending neighbor cells
	for (const auto& Direction : RadialDirections)
	{
		FIntVector2 TestCellIndex = EdgeCellIndex + Direction;
		if (IsCombustible(Cells[TestCellIndex], Cells[EdgeCellIndex]))
		{
			bMustRemoveEdgeCell = false;
			break;
		}
	}
					
	if (bMustRemoveEdgeCell)
		Result.NotEdgeCellAnymore.Emplace(EdgeCellIndex);
}

void AFireSource::ProcessFireSpreadResult(FAsyncFireSpreadResult& AggregatedResult)
{
	// after async update is completed, on game thread
	// 5. Remove pending edge cells that are no more edge cells
	// 6. Append new cells
	// 7. Update FVector array of fire locations for niagara (and replicate)
	// 8. Add ignited cells to edge cells if there are combustible cells around it
	// 9. Update all ICombustible actors (or add them to a time-sliced queue on game thread)
	
	// 5. Remove pending edge cells that are no more edge cells
	for (const auto& NotEdgeCellAnymore : AggregatedResult.NotEdgeCellAnymore)
		EdgeCells.Remove(NotEdgeCellAnymore);

#if WITH_EDITOR
	if (bLog_Debug)
	{
		for (const auto& NewCell : AggregatedResult.NewCells)
		{
			bool bGood = !Cells.Contains(NewCell.Key);
			ensure(bGood);
		}
	}
#endif
	
	// 6. Append new cells
	if (!AggregatedResult.NewCells.IsEmpty())
		Cells.Append(MoveTemp(AggregatedResult.NewCells));
	
	// 7. Update FVector array of fire locations for niagara (and replicate)
	if (AggregatedResult.IgnitedCells.Num() > 0)
	{
		for (const auto& IgnitedCellIndex : AggregatedResult.IgnitedCells)
		{
			const auto& IgnitedCell = Cells[IgnitedCellIndex];
			FireLocations.Emplace(IgnitedCell.Location);
			NewFireLocations.Emplace(IgnitedCell.Location);
			bool bIgnitedCellIsEdgeCell = false;
			
			for (const auto& Direction : RadialDirections)
			{
				FIntVector2 NeighborCellIndex = IgnitedCellIndex + Direction;
				if (ensure(Cells.Contains(NeighborCellIndex)) && IsCombustible(Cells[NeighborCellIndex], IgnitedCell))
				{
					bIgnitedCellIsEdgeCell = true;
					break;
				}
			}
			
			// 8. Add ignited cells to edge cells if there are combustible cells around it
			if (bIgnitedCellIsEdgeCell)
				EdgeCells.Emplace(IgnitedCellIndex);
		}

		MARK_PROPERTY_DIRTY_FROM_NAME(AFireSource, FireLocations, this);
		MARK_PROPERTY_DIRTY_FROM_NAME(AFireSource, NewFireLocations, this);
		UpdateFireLocations();
	}
	
	// 9. add actor updates to a time-sliced queue on game thread)
	PendingBurningActorsUpdates.Append(AggregatedResult.CombustionActorUpdates.Array());

#if WITH_EDITOR
	if (bLog_Debug)
	{
		DebugLog();
	}
#endif
}	

bool AFireSource::IsCombustible(const FFireCell& TargetCell, const FFireCell& ByCell) const
{
	if (TargetCell.IsObstacle() || TargetCell.IsIgnited())
		return false;

	return TargetCell.Location.Z < ByCell.Location.Z + ByCell.FireHeight && TargetCell.Location.Z > ByCell.Location.Z - FireDownwardPropagationThreshold;
}

void AFireSource::SpreadFireBatch(const TArray<FIntVector2>& EdgeCellsBatch, int Start, int End, FAsyncFireSpreadResult& BatchResult)
{
	for (int i = Start; i < End; i++)
	{
		const auto& EdgeCellIndex = EdgeCellsBatch[i];
		const TArray<FIntVector2>* Directions = WindStrength < WindEffectActivationThreshold
			? &RadialDirections
			: &WindDirectionToNeighbors[WindDirectionQuantized];

		for (const auto& Direction : *Directions)
		{
			FIntVector2 TestCellIndex = EdgeCellIndex + Direction;
			if (!ensure(Cells.Contains(TestCellIndex)))
			{
// #if WITH_EDITOR
				// FString LogString = FString::Printf(TEXT("Cell [%d, %d] doesnt exist for edge cell [%d, %d]"), TestCellIndex.X, TestCellIndex.Y, EdgeCellIndex.X, EdgeCellIndex.Y);
				// FIRESIM_LOG_ASYNC(LogString, Warning);
// #endif
				continue;
			}
			
			if (IsCombustible(Cells[TestCellIndex], Cells[EdgeCellIndex]))
			{
				constexpr float MinWindEffect = 0.1f;
				// it can be that cell dot product between burner->burnee and wind can be negative, so clamp by some small value to reduce the effect of burning against wind
				const float WindEffect = WindStrength > WindEffectActivationThreshold
					? FMath::Max(MinWindEffect, WindStrength * (Cells[TestCellIndex].Location - Cells[EdgeCellIndex].Location).GetSafeNormal() | WindDirection)
					: MinWindEffect;
				
				// 1. spreading fire by edge cells
				float DeltaTime = AccumulatedDeltaTime.load(); // i'm not sure if this is a good idea
				float CombustIncrease = Combust(Cells[TestCellIndex], DeltaTime, WindEffect);

				// 2.1 mark for add newly ignited cells to edge cells
				if (Cells[TestCellIndex].IsIgnited())
					BatchResult.IgnitedCells.Emplace(TestCellIndex);

				// 2.2 aggregate combustion increase for actors that have combustible interface.
				// actor's individual combustion state != individual cell combustion state
				if (Cells[TestCellIndex].bHasCombustibleInterface && !BatchResult.CombustionActorUpdates.Contains(TestCellIndex))
				{
					float& AggregatedIncrease = BatchResult.CombustionActorUpdates.FindOrAdd(TestCellIndex);
					AggregatedIncrease += CombustIncrease;
				}
			}
		}

		MarkEdgeCellForRemoval(EdgeCellIndex, BatchResult);
	}
}

void AFireSource::OnWindChanged(const FVector& NewWindVector, float NewWindStrength)
{
	WindDirection = NewWindVector;
	WindStrength = NewWindStrength;
	float acos = FMath::Acos(WindDirection | FVector::ForwardVector);
	if ((WindDirection | FVector::RightVector) < 0.f)
		acos = UE_TWO_PI - acos;
	
	WindDirectionQuantized = FMath::RoundToInt32(acos / UE_TWO_PI * 8.f);
}

void AFireSource::OnRep_FireLocations()
{
#if WITH_EDITOR
	if (bDebug_DontUpdateVFX)
		return;
#endif	
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(NiagaraComponent, NiagaraCellLocationsParameterName, FireLocations);
}

void AFireSource::OnRep_NewFireLocations()
{
	UpdateFireLocations();
}

void AFireSource::UpdateFireLocations()
{
#if WITH_EDITOR
	if (bDebug_DontUpdateVFX)
		return;
#endif	
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(NiagaraComponent, NiagaraCellLocationsParameterName, NewFireLocations);
	NewFireLocations.Empty();
}

void AFireSource::DebugLog()
{
	UE_VLOG(this, LogFireSimulation_EdgeCells, Log, TEXT("Edge cells count: %d"), EdgeCells.Num());
	for (const auto& EdgeCell : EdgeCells)
	{
		auto Index = FIntVector2(EdgeCell.X, EdgeCell.Y);
		if (bLog_Debug_VisLog_ShowCellIndex)
		{
			UE_VLOG_LOCATION(this, LogFireSimulation_EdgeCells, VeryVerbose, Cells[Index].Location, 25, FColor::Blue, TEXT("Edge cell [%d, %d]"), EdgeCell.X, EdgeCell.Y);
		}
		else
		{
			UE_VLOG_LOCATION(this, LogFireSimulation_EdgeCells, VeryVerbose, Cells[Index].Location, 25, FColor::Blue, TEXT(""));
		}
	}

	for (const auto& Cell : Cells)
	{
		if (bLog_Debug_VisLog_ShowCellIndex)
		{
			UE_VLOG_LOCATION(this, LogFireSimulation_Combustions, VeryVerbose, Cell.Value.Location, 25, FColor::Purple,
							 TEXT("Cell [%d, %d] = %.2f"), Cell.Key.X, Cell.Key.Y, Cell.Value.CombustionState.load());
		}
		else
		{
			UE_VLOG_LOCATION(this, LogFireSimulation_Combustions, VeryVerbose, Cell.Value.Location, 25, FColor::Purple,
							 TEXT(""));
		}
			
		if (Cell.Value.bHasCombustibleInterface)
		{
			if (bLog_Debug_VisLog_ShowCellIndex)
			{
				UE_VLOG_LOCATION(this, LogFireSimulation_Actors, VeryVerbose, Cell.Value.Location, 25, FColor::Cyan, TEXT("Actor cell [%d, %d]"), Cell.Key.X, Cell.Key.Y);
			}
			else
			{
				UE_VLOG_LOCATION(this, LogFireSimulation_Actors, VeryVerbose, Cell.Value.Location, 25, FColor::Cyan, TEXT(""));
			}
		}

		if (Cell.Value.bObstacle)
		{
			if (bLog_Debug_VisLog_ShowCellIndex)
			{
				UE_VLOG_LOCATION(this, LogFireSimulation_Obstacles, VeryVerbose, Cell.Value.Location, 25, FColor::Red, TEXT("Obstacle [%d, %d]"), Cell.Key.X, Cell.Key.Y);
			}
			else
			{
				UE_VLOG_LOCATION(this, LogFireSimulation_Obstacles, VeryVerbose, Cell.Value.Location, 25, FColor::Red, TEXT(""));
			}
		}
	}
}

void AFireSource::OnSomethingEnteredFireVolume(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// TODO invalidate cells
}

void AFireSource::OnSomethingLeftFireVolume(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	// TODO Some cells are now edge cells i.e. they can burn area where was something and now its gone
}

float AFireSource::Combust(FFireCell& Cell, float DeltaTime, float WindEffect)
{
	// {
		// FScopeLock Lock(&Cell.CombustionLock);
		// ensure(DeltaTime * WindEffect * Cell.CombustionRate >= 0.f);
		// Cell.CombustionState += DeltaTime * WindEffect * Cell.CombustionRate;
	// }

	const float Increase = DeltaTime * WindEffect * Cell.CombustionRate; 
	ensure(Increase >= 0.f);
	Cell.CombustionState.fetch_add(Increase);
	return Increase; 	
}
