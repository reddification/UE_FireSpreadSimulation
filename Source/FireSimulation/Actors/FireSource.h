// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraComponent.h"
#include "Data/FireSimulationDataTypes.h"
#include "GameFramework/Actor.h"
#include "FireSource.generated.h"

struct FFireCell;
class ICombustible;
class UCombustionComponent;
class UBoxComponent;

typedef TKeyValuePair<FIntVector2, FFireCell> FFireCellKVP; 

UCLASS()
class FIRESIMULATION_API AFireSource : public AActor
{
	GENERATED_BODY()

public:
	AFireSource();
	virtual void Tick(float DeltaTime) override;

	void StartFireAtLocation(const FVector& NewFireOrigin);
	
	UFUNCTION(BlueprintCallable)
	void StartFire();

	UFUNCTION(BlueprintCallable)
	void SetFirePaused(bool bPaused);

	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	UBoxComponent* GetVolumeBox() const { return BoxComponent; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UNiagaraComponent* NiagaraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UBoxComponent* BoxComponent;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName NiagaraCellLocationsParameterName = FName("FireLocations");
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bStartFireAutomatically = true;

	// size of 1 dimension of a fire cell box, i.e. if its value is 30, then it's area is 30*30
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(UIMin = 10.f, ClampMin = 10.f))
	double FireCellSize = 25.f;

	// if a potential cell is lower than this value - fire won't spread there
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(UIMin = 0.f, ClampMin = 0.f))
	double FireDownwardPropagationThreshold = 20.0;

	// Vertical height of fire, affects how higher can a cell be to be ignited by this cell
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(UIMin = 0.f, ClampMin = 0.f))
	double BaseFireStrength = 50.0;

	// Currently only used to reserve memory for fire cell containers
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(UIMin = 1, ClampMin = 1))
	int FireSpreadLimit = 2000;

	// this is square root, so if this value is 30, then 30 * 30 = 900 cells will be cached
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(UIMin = 1, ClampMin = 1))
	int PreCacheCellCount = 250;

	// don't update more than this amount of actor that are burning per tick
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(UIMin = 1, ClampMin = 1))
	int MaxActorsUpdatesPerTick = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bLog_Debug = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bLog_Debug_VisLog_ShowCellIndex = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bDebug_DontUpdateVFX = false;


private:
	void OnWindChanged(const FVector& NewWindVector, float NewWindStrength);
	
	FVector WindDirection = FVector::ZeroVector;
	int WindDirectionQuantized = 0;
	float WindStrength = 0.f;
	
	// TODO FFastArraySerializer
	UPROPERTY(ReplicatedUsing=OnRep_FireLocations)
	TArray<FVector> FireLocations;

	// idk i'm too bad with niagara to make a good emitter so if I provide all fire locations and not just new ones - my current "as good as I could" emitter will just spawn all particles and not just new ones
	// but if i send only new particles - old ones still live. so eh fuck it who am i a technical artist?
	UPROPERTY(ReplicatedUsing=OnRep_NewFireLocations)
	TArray<FVector> NewFireLocations;
	
	UFUNCTION()
	void OnRep_FireLocations();

	UFUNCTION()
	void OnRep_NewFireLocations();
	
	void UpdateFireLocations();

	UFUNCTION()
	void OnSomethingEnteredFireVolume(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
									  const FHitResult& SweepResult);
	UFUNCTION()
	void OnSomethingLeftFireVolume(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	bool GetCell(const FVector& LocationBase, const FCollisionObjectQueryParams& COQP, FFireCell& OutCell) const;
	void PrepareImmediateInitialCells(const FIntVector2& InitialCellKey, const FVector& BaseLocation, const FCollisionObjectQueryParams& COQP);
	
	void SpreadFireAsync();
	void MarkEdgeCellForRemoval(const FIntVector2& EdgeCellKey, FAsyncFireSpreadResult& Result);
	void DebugLog();
	void ProcessFireSpreadResult(FAsyncFireSpreadResult& AggregatedResult);
	bool IsCombustible(const FFireCell& TargetCell, const FFireCell& ByCell) const;
	void SpreadFireBatch(const TArray<FIntVector2>& EdgeCells, int Start, int End, FAsyncFireSpreadResult& BatchResult);
	void CreateNewFireCells(FAsyncFireSpreadResult& AggregatedResult);

	float Combust(FFireCell& Cell, float DeltaTime, float WindEffect);
	bool StartFireAtCell(const FIntVector2& InitialCellKey, const FVector& OriginLocation);

	float WindEffectActivationThreshold = 2.f;
	float HighestAltitude = FLT_MAX;
	float LowestAltitude = -FLT_MAX;
	int LastBurningActorUpdateIndex = 0;
	
	std::atomic<float> AccumulatedDeltaTime = 0.f;
	
	std::atomic<bool> bAsyncUpdateRunning = false;
	
	std::atomic<bool> bLogDebugAtomic;
	
	TMap<FIntVector2, FFireCell> Cells;
	TSet<FIntVector2> EdgeCells;
	
	TMap<int, TArray<FIntVector2>> WindDirectionToNeighbors;
	TArray<FIntVector2> RadialDirections;
	TMap<TEnumAsByte<EPhysicalSurface>, FPhysicMaterialCombustionParameters> SurfacesCombustionParameters;
	TArray<TEnumAsByte<EPhysicalSurface>> IncombustibleSurfaces;
	
	UPROPERTY()
	TArray<UBoxComponent*> DiscreteVolumes;

	// I assume since there can be thousands or actors to update their visuals, doing it in 1 tick isn't the best idea. so I time-slice it
	TArray<TPair<FIntVector2, float>> PendingBurningActorsUpdates;
	
	void UpdateBurningActors();
};
