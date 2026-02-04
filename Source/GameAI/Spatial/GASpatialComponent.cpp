#include "GASpatialComponent.h"
#include "GameAI/Pathfinding/GAPathComponent.h"
#include "GameAI/Grid/GAGridMap.h"
#include "Kismet/GameplayStatics.h"
#include "Math/MathFwd.h"
#include "GASpatialFunction.h"
#include "ProceduralMeshComponent.h"
#include "Components/SplineComponent.h"


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


bool UGASpatialComponent::ChoosePosition(bool PathfindToPosition, bool Debug)
{
	bool Result = false;
	const APawn* OwnerPawn = GetOwnerPawn();
	if (OwnerPawn == NULL)
	{
		return false;
	}

	AGAGridActor* Grid = GetGridActor();

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
	FVector2D PawnLocation(OwnerPawn->GetActorLocation());
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
		APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
		FVector PlayerPosition = PlayerPawn->GetActorLocation();
		FVector AIPosition = OwnerPawn->GetActorLocation();
		
		bool bMap = PathComponent->Dijkstra(PlayerPosition, DistanceMap);
		if (!bMap)
		{
			return false;
		}
		
		
		// For each layer in the spatial function, evaluate and accumulate the layer in GridMap
		// Note, only evaluate accessible cells found in step 1
		for (const FFunctionLayer& Layer : SpatialFunction->Layers)
		{
			// figure out how to evaluate each layer type, and accumulate the value in the ScoreMap
			EvaluateLayer(Layer, DistanceMap, ScoreMap);
		}

		// (b) add some hysteresis (a score bonus) to the last tick's chosen cell
		if (LastChosenCell.IsValid() && Grid->IsCellRefInBounds(LastChosenCell))
		{
			float CurrentScore = 0.0f;
			const float HysteresisBonus = 1.0f;
			if (ScoreMap.GetValue(LastChosenCell, CurrentScore))
			{
				ScoreMap.SetValue(LastChosenCell, CurrentScore + HysteresisBonus);
			}
		}
		// (c) pick the best cell in ScoreMap
		FCellRef BestCell = FCellRef :: Invalid;
		float BestScore = - FLT_MAX;

		for (int32 Y = ScoreMap.GridBounds.MinY; Y < ScoreMap.GridBounds.MaxY; Y++)
		{
			for (int32 X = ScoreMap.GridBounds.MinX; X < ScoreMap.GridBounds.MaxX; X++)
			{
				FCellRef CellRef(X, Y);
				
				// check the traversability
				ECellData Flags = Grid->GetCellData(CellRef);
				if (!EnumHasAllFlags(Flags, ECellData::CellDataTraversable))
				{
					continue;
				}
				
				float CellDistance = FLT_MAX;
				if (!ScoreMap.GetValue(CellRef, CellDistance))
				{
					continue;
				}
				
				if (CellDistance >= FLT_MAX)
				{
					continue;
				}
				
				float Score = 0.0f;
				if (ScoreMap.GetValue(CellRef, Score) && Score > BestScore)
				{
					BestScore = Score;
					BestCell = CellRef;
				}
			}
		}

		bool bSelected = BestCell.IsValid();
		bool bPathBuilt = false;

		if (PathfindToPosition && bSelected)
		{
			FVector BestPosition = Grid->GetCellPosition(BestCell);
			
			FGAGridMap PathDistanceMap;
			PathComponent->Dijkstra(BestPosition, PathDistanceMap);
			
			FVector CurrentPosition = OwnerPawn->GetActorLocation();
			FCellRef CurrentCell = Grid->GetCellRef(CurrentPosition);
			bPathBuilt = PathComponent->BuildPathFromDistanceMap(CurrentPosition, CurrentCell, PathDistanceMap);
		}

		if (bPathBuilt)
		{
			LastChosenCell = BestCell;
		}
		
		Result = PathfindToPosition ? bPathBuilt : bSelected;
		
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


void UGASpatialComponent::EvaluateLayer(const FFunctionLayer& Layer, const FGAGridMap& DistanceMap, FGAGridMap& ScoreMap) const
{
	AActor* OwnerPawn = GetOwnerPawn();
	const AGAGridActor* Grid = GetGridActor();
	
	// get player pawn & position
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	FVector PlayerPosition = PlayerPawn->GetActorLocation();

	for (int32 Y = ScoreMap.GridBounds.MinY; Y < ScoreMap.GridBounds.MaxY; Y++)
	{
		for (int32 X = ScoreMap.GridBounds.MinX; X < ScoreMap.GridBounds.MaxX; X++)
		{
			FCellRef CellRef(X, Y);

			if (EnumHasAllFlags(Grid->GetCellData(CellRef), ECellData::CellDataTraversable))
			{
				// Assignment 3 part 4-4: evaluate me!

				// First step is determine input value. Remember there are three possible inputs to handle:
				// 	SI_None				UMETA(DisplayName = "None"),
				//	SI_TargetRange		UMETA(DisplayName = "Target Range"),
				//	SI_PathDistance		UMETA(DisplayName = "PathDistance"),
				//	SI_LOS				UMETA(DisplayName = "Line Of Sight")
				float InputValue = 0.0f;

				switch (Layer.Input) {
				case SI_None:
					{
						InputValue = 0.0f;
						break;
					}
				case SI_TargetRange:
					{
						// calculate distance to the player
						FVector CellPosition = Grid->GetCellPosition(CellRef);
						InputValue = FVector::Dist2D(CellPosition, PlayerPosition);
						break;
					}
						
				case SI_PathDistance:
					{
						// extract distance from dijkstra map
						float PathDistance = FLT_MAX;
						if (DistanceMap.GetValue(CellRef, PathDistance))
						{
							if (PathDistance < FLT_MAX)
							{
								InputValue = PathDistance;
							}
							else
							{
								continue;
							}
						}
						else
						{
							continue;
						}
						break;
					}
						
				case SI_LOS:
					{
						// check if it is line of sight
						UWorld* World = GetWorld();
						FHitResult HitResult;
						FCollisionQueryParams Params;
						FVector Start = Grid->GetCellPosition(CellRef);
						FVector End = PlayerPosition;
						
						// avoid ground-level radiation, elevate the launch point
						Start.Z += 50.0f;
						
						// ignore player and self
						Params.AddIgnoredActor(OwnerPawn);
						Params.AddIgnoredActor(PlayerPawn);
							
						bool bHitSomething = World->LineTraceSingleByChannel(HitResult, Start, 
							End, ECollisionChannel::ECC_Visibility, Params);
						
						// if bHit is false, indicate a clear line of sight
						InputValue = bHitSomething ? 0.0f : 1.0f;
						break;
					}
					
					default:
						break;
				}

				// Next, run it through the response curve using something like this
				// float Value = 4.5f;
				// float ModifiedValue = Layer.ResponseCurve.GetRichCurveConst()->Eval(Value, 0.0f);
				float ModifiedValue = 0.0f;
				if (Layer.ResponseCurve.GetRichCurveConst())
				{
					ModifiedValue = Layer.ResponseCurve.GetRichCurveConst()->Eval(InputValue, 0.0f);
				}
				else
				{
					// no curve, set as input value
					ModifiedValue = InputValue;
				}
				// Then add it's influence to the grid map, combining with the current value using one of the two operators
				//	SO_None				UMETA(DisplayName = "None"),
				//	SO_Add				UMETA(DisplayName = "Add"),			// add this layer to the accumulated buffer
				//	SO_Multiply			UMETA(DisplayName = "Multiply")		// multiply this layer into the accumulated buffer

				//ScoreMap.SetValue(CellRef, CombinedValue);
				float CurrentScore = 0.0f;
				ScoreMap.GetValue(CellRef, CurrentScore);
				float NewScore = CurrentScore;

				switch (Layer.Op)
				{
				case SO_None:
					{
						continue;
					}
				case SO_Add:
					{
						NewScore = CurrentScore + ModifiedValue;
						break;
					}
				case SO_Multiply:
					{
						float Base = (CurrentScore == 0.0f ? 1.0f : CurrentScore);
						NewScore = Base * ModifiedValue;
						break;
					}
				default:
					continue;
				}
				
				ScoreMap.SetValue(CellRef, NewScore);
			}
		}
	}
}