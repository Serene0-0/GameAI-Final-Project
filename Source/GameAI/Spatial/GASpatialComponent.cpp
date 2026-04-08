#include "GASpatialComponent.h"
#include "GameAI/Pathfinding/GAPathComponent.h"
#include "GameAI/Grid/GAGridMap.h"
#include "Kismet/GameplayStatics.h"
#include "Math/MathFwd.h"
#include "GASpatialFunction.h"
#include "ProceduralMeshComponent.h"
#include "GameAI/Perception/GAPerceptionComponent.h"

UE_DISABLE_OPTIMIZATION

UGASpatialComponent::UGASpatialComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SampleDimensions = 8000.0f;		// should cover the bulk of the test map
}


AGAGridActor* UGASpatialComponent::GetGridActor() const
{
	AGAGridActor* Result = GridActorInternal.Get();
	if (Result)
	{
		return Result;
	}
	else
	{
		AActor* GenericResult = UGameplayStatics::GetActorOfClass(this, AGAGridActor::StaticClass());
		if (GenericResult)
		{
			Result = Cast<AGAGridActor>(GenericResult);
			if (Result)
			{
				// Cache the result
				// Note, GridActor is marked as mutable in the header, which is why this is allowed in a const method
				GridActorInternal = Result;
			}
		}

		return Result;
	}
}

UGAPathComponent* UGASpatialComponent::GetPathComponent() const
{
	UGAPathComponent* Result = PathComponentInternal.Get();
	if (Result)
	{
		return Result;
	}
	else
	{
		AActor* Owner = GetOwner();
		if (Owner)
		{
			// Note, the UGAPathComponent and the UGASpatialComponent are both on the controller
			Result = Owner->GetComponentByClass<UGAPathComponent>();
			if (Result)
			{
				PathComponentInternal = Result;
			}
		}
		return Result;
	}
}

APawn* UGASpatialComponent::GetOwnerPawn() const
{
	AActor* Owner = GetOwner();
	if (Owner)
	{
		APawn* Pawn = Cast<APawn>(Owner);
		if (Pawn)
		{
			return Pawn;
		}
		else
		{
			AController* Controller = Cast<AController>(Owner);
			if (Controller)
			{
				return Controller->GetPawn();
			}
		}
	}

	return NULL;
}


AActor* UGASpatialComponent::GetTargetState(FTargetState &TargetStateOut) const
{
	AActor* Result = NULL;
	AActor* Owner = GetOwner();
	UGAPerceptionComponent *PerceptionComponent = Owner->GetComponentByClass<UGAPerceptionComponent>();

	if (PerceptionComponent)
	{
		UGATargetComponent *TargetComponent = PerceptionComponent->GetCurrentTarget();
		if (TargetComponent)
		{
			Result = TargetComponent->GetOwner();
			TargetStateOut = TargetComponent->GetTargetState();
		}
	}

	return Result;
}

bool UGASpatialComponent::ChoosePosition(bool PathfindToPosition, bool Debug)
{
	bool Result = false;
	const APawn* OwnerPawn = GetOwnerPawn();
	if (OwnerPawn == NULL)
	{
		return false;
	}

	AGAGridActor* Grid = GetGridActor();

	FCellRef LastCell = BestCell;
	BestCell = FCellRef::Invalid;

	if (Grid == NULL)
	{
		return false;
	}

	if (SpatialFunctionReference.Get() == NULL)
	{
		UE_LOG(LogTemp, Warning, TEXT("UGASpatialComponent has no SpatialFunctionReference assigned."));
		return false;
	}

	if (Grid == NULL)
	{
		UE_LOG(LogTemp, Warning, TEXT("UGASpatialComponent::ChoosePosition can't find a GridActor."));
		return false;
	}

	UGAPathComponent* PathComponent = GetPathComponent();
	if (PathComponent == NULL)
	{
		UE_LOG(LogTemp, Warning, TEXT("UGASpatialComponent::ChoosePosition can't find a PathComponent."));
		return false;
	}


	// Don't worry too much about the Unreal-ism below. Technically our SpatialFunctionReference is not ACTUALLY
	// a spatial function instance, rather it's a class, which happens to have a lot of data in it.
	// Happily, Unreal creates, under the hood, a default object for every class, that lets you access that data
	// as if it were a normal instance
	const UGASpatialFunction* SpatialFunction = SpatialFunctionReference->GetDefaultObject<UGASpatialFunction>();

	// The below is to create a GridMap (which you will fill in) based on a bounding box centered around the OwnerPawn

	FBox2D Box(EForceInit::ForceInit);
	FIntRect CellRect;
	FVector StartLocation = OwnerPawn->GetActorLocation();
	FVector2D PawnLocation(StartLocation);
	Box += PawnLocation;
	Box = Box.ExpandBy(SampleDimensions / 2.0f);
	if (Grid->GridSpaceBoundsToRect2D(Box, CellRect))
	{
		// Super annoying, by the way, that FIntRect is not blueprint accessible, because it forces us instead
		// to make a separate bp-accessible FStruct that represents _exactly the same thing_.
		FGridBox GridBox(CellRect);

		// This is the grid map I'm going to fill with values
		FGAGridMap ScoreMap(Grid, GridBox, 0.0f);

		// Fill in this distance map using Dijkstra!
		FGAGridMap DistanceMap(Grid, GridBox, FLT_MAX);


		// ~~~ STEPS TO FILL IN FOR ASSIGNMENT 3 part 4-3 ~~~


		// (a) Run Dijkstra's to determine which cells we should even be evaluating (the GATHER phase)
		// call UGAPathComponent::Dijkstra(const FVector &StartPoint, FGAGridMap &DistanceMapOut) const;
		PathComponent->Dijkstra(StartLocation, DistanceMap);

		// Give the last best cell a bonus
		ScoreMap.SetValue(LastCell, SpatialFunction->LastCellBonus);

		// For each layer in the spatial function, evaluate and accumulate the layer in GridMap
		// Note, only evaluate accessible cells found in step 1
		for (const FFunctionLayer& Layer : SpatialFunction->Layers)
		{
			// figure out how to evaluate each layer type, and accumulate the value in the GridMap
			EvaluateLayer(Layer, DistanceMap, ScoreMap);
		}

		// (b) pick the best cell in GridMap

		{
			float BestScore = -FLT_MAX;

			for (int32 Y = ScoreMap.GridBounds.MinY; Y <= ScoreMap.GridBounds.MaxY; Y++)
			{
				for (int32 X = ScoreMap.GridBounds.MinX; X <= ScoreMap.GridBounds.MaxX; X++)
				{
					FCellRef CellRef(X, Y);
					float D;

					DistanceMap.GetValue(CellRef, D);

					if (D < FLT_MAX)
					{
						float V;

						ScoreMap.GetValue(CellRef, V);
						if (V > BestScore)
						{
							BestScore = V;
							BestCell = CellRef;
							Result = true;
						}
					}
				}
			}
		}

		if (PathfindToPosition)
		{
			if (BestCell.IsValid())
			{
				// (c) Go there! You should call your pathcomponent's UGAPathComponent::BuildPathFromDistanceMap() function
				PathComponent->BuildPathFromDistanceMap(StartLocation, BestCell, DistanceMap);
			}
			else
			{
				PathComponent->ClearPath();
			}
		}


		if (Debug)
		{
			// Note: this outputs (basically) the results of the position selection
			// However, you can get creative with the debugging here. For example, maybe you want
			// to be able to examine the values of a specific layer in the spatial function
			// You could create a separate debug map above (where you're doing the evaluations) and
			// cache it off for debug rendering. Ideally you'd be able to control what layer you wanted to
			// see from blueprint

			Grid->DebugGridMap = ScoreMap;
			Grid->RefreshDebugTexture();
			Grid->DebugMeshComponent->SetVisibility(true);		//cheeky!
		}
	}

	return Result;
}


void UGASpatialComponent::EvaluateLayer(const FFunctionLayer& Layer, const FGAGridMap& DistanceMap, FGAGridMap & ScoreMap) const
{
	UWorld* World = GetWorld();
	AActor* OwnerPawn = GetOwnerPawn();
	const AGAGridActor* Grid = GetGridActor();
	FTargetState TargetState;
	AActor* TargetActor = GetTargetState(TargetState);
	FVector TargetPosition = TargetState.Position;
	FVector Offset(0.0f, 0.0f, 60.0f);

	TArray<FVector> AllyPositions;
	TArray<float> AllyDistances;

	if (Layer.Input == SI_AllyDistance)
	{
		TArray<AActor *> Actors;
		UGameplayStatics::GetAllActorsOfClass(World, APawn::StaticClass(), Actors);

		for (AActor* Actor : Actors)
		{
			if ((Actor == OwnerPawn) || (Actor == TargetActor))
			{
				continue;
			}

			APawn* Pawn = Cast<APawn>(Actor);
			if (Pawn)
			{
				AController* Controller = Pawn->GetController();
				if (Controller)
				{
					UGAPathComponent *OtherPathComponent = Controller->GetComponentByClass<UGAPathComponent>();
					if (OtherPathComponent)
					{
						FVector Position;
						float D;

						// Keep track of where our allies are -- but note that if they are headed towards a
						// destination (according to their path component) we use THAT as the ally position,
						// rather than their current position.
						// Note, we also keep track of their distance to that destination.

						if (OtherPathComponent->State == GAPS_Active)
						{
							Position = OtherPathComponent->Destination;
							D = OtherPathComponent->GetPathLength();
						}
						else
						{
							Position = Pawn->GetActorLocation();
							D = 0.0f;
						}
						AllyPositions.Add(Position);
						AllyDistances.Add(D);
					}
				}
			}
		}
	}


	for (int32 Y = ScoreMap.GridBounds.MinY; Y < ScoreMap.GridBounds.MaxY; Y++)
	{
		for (int32 X = ScoreMap.GridBounds.MinX; X < ScoreMap.GridBounds.MaxX; X++)
		{
			FCellRef CellRef(X, Y);

			if (EnumHasAllFlags(Grid->GetCellData(CellRef), ECellData::CellDataTraversable))
			{
				float CellDistance;
				if (DistanceMap.GetValue(CellRef, CellDistance) &&
					(CellDistance < FLT_MAX))
				{
					// evaluate me!

					float Value = 0.0f;

					switch (Layer.Input)
					{
					case SI_None:
						break;
					case SI_TargetRange:
					{
						FVector CellPosition = Grid->GetCellPosition(CellRef);
						Value = FVector::Distance(CellPosition, TargetPosition);
					}
					break;
					case SI_PathDistance:
						Value = CellDistance;
						break;
					case SI_LOS:
					{
						FVector CellPosition = Grid->GetCellPosition(CellRef) + Offset;
						FHitResult HitResult;
						FCollisionQueryParams Params;
						FVector Start = CellPosition;
						FVector End = TargetPosition;
						Params.AddIgnoredActor(TargetActor);		// Probably want to ignore the target actor
						Params.AddIgnoredActor(OwnerPawn);			// Probably want to ignore the AI themself
						bool bHitSomething = World->LineTraceSingleByChannel(HitResult, Start, End, ECollisionChannel::ECC_Visibility, Params);
						Value = bHitSomething ? 0.0f : 1.0f;
						break;
					}
					case SI_AllyDistance:
					{
						FVector CellPosition = Grid->GetCellPosition(CellRef);
						float MinDistanceToAlly = BIG_NUMBER;
						int32 NumAllies = AllyPositions.Num();

						// find the closest ally to this point
						// HOWEVER ... if we are (path) closer to this cell than THEY are to THEIR destination
						// we are allowed to disregard them, since we would get their first, and they can deal
						// with us instead.


						for (int32 AllyIndex = 0; AllyIndex < NumAllies; AllyIndex++)
						{
							if (AllyDistances[AllyIndex] < CellDistance)
							{
								float D = FVector::Distance(CellPosition, AllyPositions[AllyIndex]);
								if (D < MinDistanceToAlly)
								{
									MinDistanceToAlly = D;
								}
							}
						}
						Value = MinDistanceToAlly;
						break;
					}
					};

					{
						// Next, run it through the response curve using something like this
						float ModifiedValue = Layer.ResponseCurve.GetRichCurveConst()->Eval(Value, Value);
						float CurrentValue = 0.0f;
						float ResultValue = 0.0f;

						ScoreMap.GetValue(CellRef, CurrentValue);

						switch (Layer.Op)
						{
						case SO_None:
							ResultValue = CurrentValue;
							break;
						case SO_Add:
							ResultValue = CurrentValue + ModifiedValue;
							break;
						case SO_Multiply:
							ResultValue = CurrentValue * ModifiedValue;
							break;
						}

						ScoreMap.SetValue(CellRef, ResultValue);
					}
				}
			}
		}
	}
}

UE_ENABLE_OPTIMIZATION
