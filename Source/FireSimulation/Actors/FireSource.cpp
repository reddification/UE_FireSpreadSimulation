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
	
	// key - dot product between wind and world forward vector mapped from 0 to 7. preudocode: round(acos((wind, world x)) / 2pi * 8)
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
	FDoRepLifetimeParams DoRepLifetimeParams { COND_None, REPNOTIFY_OnChanged, true };
	DOREPLIFETIME_WITH_PARAMS_FAST(AFireSource, FireLocations, DoRepLifetimeParams);
}

void AFireSource::CreateNewFireCells(FAsyncFireSpreadResult& AggregatedResult)
{
	if (AggregatedResult.IgnitedCells.IsEmpty())
		return;

	int ThreadsCount = FPlatformMisc::NumberOfCoresIncludingHyperthreads();

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
			BatchResult.Reserve(CurrentBatchSize * 4); // in general, a cell will have 3 pending neighbors, but edge cells can have 5 
			int End = BatchStartIndex + CurrentBatchSize;
			for (int i = BatchStartIndex; i < End; i++)
			{
				auto NewFireCellIndex = IgnitedCellsArray[i];
				for (const auto& RadialDirection : RadialDirections)
				{
					auto TestCellIndex = NewFireCellIndex + RadialDirection;
					if (!Cells.Contains(TestCellIndex))
					{
						FFireCell NewCell;
						GetCell(Cells[NewFireCellIndex].Location, COQP, TestCellIndex, NewCell);
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

	if (bPendingProcessFireSpreadResult.load())
	{
		UpdateBurningActors();
	}
	
	if (!bAsyncUpdateRunning)
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
}

void AFireSource::UpdateBurningActors()
{
	if (PendingBurningActorsUpdates.IsEmpty())
		return;
	
	// TODO what if PendingBurningActorUpdates gets new data for the same actor?
	// perhaps replace PendingBurningActorsUpdates to TMap<AActor*, float> instead
	int Index = LastBurningActorUpdateIndex;
	int Until = FMath::Min(LastBurningActorUpdateIndex + MaxActorsUpdatesPerTick, PendingBurningActorsUpdates.Num());
	
	while (Index < Until)
	{
		auto Cell = Cells.Find(PendingBurningActorsUpdates[Index]);
		if (!ensure(Cell))
			continue;
		
		if (Cell->CombustibleActor.IsValid())
		{
			Cell->CombustibleInterface->AddCombustion(Cell->CombustionState.load());
		}
		else
		{
			Cell->CombustibleActor.Reset();
			Cell->CombustibleInterface = nullptr;
			Cell->bHasCombustibleInterface = false;
		}

		Index++;
	}

	if (Index >= PendingBurningActorsUpdates.Num())
	{
		PendingBurningActorsUpdates.Empty();
		LastBurningActorUpdateIndex = 0;
	}
}

bool AFireSource::GetCell(const FVector& LocationBase, const FCollisionObjectQueryParams& COQP,
                          const FIntVector2& CellIndex, FFireCell& OutCell) const
{
	if (Cells.Contains(CellIndex))
		return false;
				
	FHitResult Hit;
	// TODO TraceBase.Z should be average neighbor Z instead of LocationBase.Z because they can differ a lot
	FVector TraceBase = LocationBase + FVector(CellIndex.X * FireCellSize, CellIndex.Y * FireCellSize, 0);
	FCollisionShape SweepShape = FCollisionShape::MakeBox(FVector(FireCellSize * .5f, FireCellSize * .5f, BaseFireStrength * 0.5f));
	FVector SweepStart = TraceBase + FVector::UpVector * BaseFireStrength;
	FVector SweepEnd = TraceBase - FVector::UpVector * FireDownwardPropagationThreshold;
	bool bHit = GetWorld()->SweepSingleByObjectType(Hit, SweepStart, SweepEnd, FQuat::Identity, COQP, SweepShape);

	if (bHit)
	{
		OutCell.Location = Hit.ImpactPoint.Z < Hit.TraceStart.Z ? Hit.ImpactPoint : TraceBase;
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
					OutCell.IgnitionRate = SurfaceCombustionParameters->IgnitionRate;
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
		OutCell.Location = TraceBase;
		return false;
	}
}

void AFireSource::PrepareImmediateInitialCells(const FIntVector2& InitialCellKey, const FVector& BaseLocation, const FCollisionObjectQueryParams& COQP)
{
	for (const FIntVector2& CellIndex : RadialDirections)
	{
		FFireCell FireCell;
		GetCell(BaseLocation, COQP, InitialCellKey + CellIndex, FireCell);
		Cells.Emplace(CellIndex, MoveTemp(FireCell));
	}
}

void AFireSource::UpdateOverallVolumes()
{
	
}

bool AFireSource::StartFireAtCell(const FIntVector2& InitialCellKey, const FVector& OriginLocation)
{
	FCollisionObjectQueryParams COQP;
	COQP.AddObjectTypesToQuery(COLLISION_COMBUSTIBLE);

	FFireCell InitialCell;
	bool bInitialCellCreated = GetCell(OriginLocation, COQP, InitialCellKey, InitialCell);
	if (!bInitialCellCreated)
	{
		UE_VLOG_UELOG(this, LogFireSimulation, Warning, TEXT("Can't start fire at %s"), *OriginLocation.ToString())
		UE_VLOG_LOCATION(this, LogFireSimulation, Warning, OriginLocation, 25.f, FColor::Red, TEXT("Can't start fire here"));
		return false;
	}

	Cells.Emplace(InitialCellKey, MoveTemp(InitialCell));
	Cells[InitialCellKey].CombustionState = 1.f;
	EdgeCells.Emplace(InitialCellKey);

	FireLocations.Emplace(OriginLocation);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFireSource, FireLocations, this);
	UpdateFireLocations();
	
	PrepareImmediateInitialCells(InitialCellKey, OriginLocation, COQP);
	
	UpdateOverallVolumes();
	return true;
}

void AFireSource::StartFire()
{
	auto WindComponent = GetWorld()->GetGameState()->FindComponentByClass<UWindComponent>();
	WindDirection = WindComponent->GetWindDirection();
	WindStrength = WindComponent->GetWindStrength();
	WindComponent->WindChangedEvent.AddUObject(this, &AFireSource::OnWindChanged);
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

void AFireSource::PauseFire()
{
	if (HasAuthority())
		SetActorTickEnabled(false);
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
	// 3. keeping track of pre-cached cells and invoking bulk precache when there's less than some amount of cached cells left
	// 4. updating contiguous box collisions for damage and nav mesh
	
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
		
		// 4. updating contiguous box collisions for damage and nav mesh
		// TODO
		
		AsyncTask(ENamedThreads::Type::GameThread, [this, AggregatedResult = MoveTemp(AggregatedResult)] () mutable
		{
			ProcessFireSpreadResult(AggregatedResult);
			bAsyncUpdateRunning.store( false);
		});
	});
}

void AFireSource::MarkEdgeCellForRemoval(const FIntVector2& EdgeCellIndex, FAsyncFireSpreadResult& Result)
{
	bool bMustRemoveEdgeCell = true;
	//	2.3 mark for removal those who have no more pending neighbor cells
	for (const auto& Direction : RadialDirections)
	{
		FIntVector2 TestCellIndex = EdgeCellIndex + Direction;
		if (!Cells[TestCellIndex].IsObstacle() && !Cells[TestCellIndex].IsIgnited())
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
	// 6. Update FVector array of fire locations for niagara (and replicate)
	// 7. Add ignited cells to edge cells if there are combustible cells around it
	// 8. Append new cells
	// 9. Update all ICombustible actors (or add them to a time-sliced queue on game thread)
	
	// 5. Remove pending edge cells that are no more edge cells
	for (const auto& NotEdgeCellAnymore : AggregatedResult.NotEdgeCellAnymore)
		EdgeCells.Remove(NotEdgeCellAnymore);

	// Append new cells
	if (AggregatedResult.NewCells.IsEmpty())
		Cells.Append(MoveTemp(AggregatedResult.NewCells));
	
	// 6. Update FVector array of fire locations for niagara (and replicate)
	if (AggregatedResult.IgnitedCells.Num() > 0)
	{
		for (const auto& IgnitedCellIndex : AggregatedResult.IgnitedCells)
		{
			const auto* IgnitedCell = Cells.Find(IgnitedCellIndex);
			FireLocations.Emplace(IgnitedCell->Location);
			bool bIgnitedCellIsEdgeCell = false;
			
			for (const auto& Direction : RadialDirections)
			{
				FIntVector2 NeighborCellIndex = IgnitedCellIndex + Direction;
				if (ensure(Cells.Contains(NeighborCellIndex)) && !Cells[NeighborCellIndex].IsObstacle() && !Cells[NeighborCellIndex].IsIgnited())
				{
					bIgnitedCellIsEdgeCell = true;
					break;
				}
			}
			
			// 7. Add ignited cells to edge cells if there are combustible cells around it
			if (bIgnitedCellIsEdgeCell)
				EdgeCells.Emplace(IgnitedCellIndex);
		}

		MARK_PROPERTY_DIRTY_FROM_NAME(AFireSource, FireLocations, this);
		UpdateFireLocations();
	}
	
	// 8. add actor updates to a time-sliced queue on game thread)
	PendingBurningActorsUpdates.Append(AggregatedResult.CombustionActorUpdates.Array());
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
				const float WindEffect = WindStrength * (Cells[TestCellIndex].Location - Cells[EdgeCellIndex].Location).GetSafeNormal() | WindDirection;
				// 1. spreading fire by edge cells
				float DeltaTime = AccumulatedDeltaTime.load(); // i'm not sure if this is a good idea
				Combust(Cells[TestCellIndex], DeltaTime, WindEffect);

				// 2.1 mark for add newly ignited cells to edge cells
				if (Cells[TestCellIndex].IsIgnited())
					BatchResult.IgnitedCells.Emplace(TestCellIndex);

				// 2.2 mark actors that have combustible interface 
				if (Cells[TestCellIndex].bHasCombustibleInterface && !BatchResult.CombustionActorUpdates.Contains(TestCellIndex))
					BatchResult.CombustionActorUpdates.Emplace(TestCellIndex);
			}
		}

		MarkEdgeCellForRemoval(EdgeCellIndex, BatchResult);
	}
}

void AFireSource::OnWindChanged(const FVector& NewWindVector, float NewWindStrength)
{
	WindDirection = NewWindVector;
	WindStrength = NewWindStrength;
	WindDirectionQuantized = FMath::RoundToInt32(FMath::Acos(WindDirection | FVector::ForwardVector) / UE_TWO_PI * 8);
}

void AFireSource::HandleCellShapeSweepCompleted(const FTraceHandle& TraceHandle, FTraceDatum& TraceDatum,
                                                const FFireCell* CellularAutomataCell)
{
	if (TraceDatum.OutHits.IsEmpty())
		return;
}

void AFireSource::SweepForCell(const FFireCell* Cell)
{
	FCollisionObjectQueryParams CollisionObjectQueryParams;
	CollisionObjectQueryParams.AddObjectTypesToQuery(COLLISION_COMBUSTIBLE);
	FCollisionQueryParams CollisionQueryParams;
	
	FTraceDelegate AIProximityShotSweepTraceDelegate = FTraceDelegate::CreateUObject(this, &AFireSource::HandleCellShapeSweepCompleted, Cell);
	FVector SweepStart = FVector( Cell->Location.Z + Cell->FireHeight);
	FVector SweepEnd = FVector(SweepStart.X, SweepStart.Y, Cell->Location.Z - FireDownwardPropagationThreshold);
	FQuat SweepRotation = FQuat::Identity;
	auto SweepShape = FCollisionShape::MakeBox(FVector(FireCellSize, FireCellSize, Cell->FireHeight));
	GetWorld()->AsyncSweepByObjectType(EAsyncTraceType::Single, SweepStart, SweepEnd, SweepRotation,
									   CollisionObjectQueryParams, SweepShape, CollisionQueryParams, &AIProximityShotSweepTraceDelegate);
}

void AFireSource::OnRep_FireLocations()
{
	UpdateFireLocations();
}

void AFireSource::UpdateFireLocations()
{
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(NiagaraComponent, NiagaraCellLocationsParameterName, FireLocations);
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

void AFireSource::Combust(FFireCell& Cell, float DeltaTime, float WindEffect)
{
	Cell.CombustionState.fetch_add(DeltaTime * WindEffect * Cell.IgnitionRate);
}
