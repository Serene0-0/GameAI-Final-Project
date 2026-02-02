#include "GASpatialComponent.h"
#include "GameAI/Pathfinding/GAPathComponent.h"
#include "GameAI/Grid/GAGridMap.h"
#include "Kismet/GameplayStatics.h"
#include "Math/MathFwd.h"
#include "GASpatialFunction.h"
#include "ProceduralMeshComponent.h"



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

		// For each layer in the spatial function, evaluate and accumulate the layer in GridMap
		// Note, only evaluate accessible cells found in step 1
		for (const FFunctionLayer& Layer : SpatialFunction->Layers)
		{
			// figure out how to evaluate each layer type, and accumulate the value in the ScoreMap
			EvaluateLayer(Layer, DistanceMap, ScoreMap);
		}

		// (b) add some hysteresis (a score bonus) to the last tick's chosen cell

		// (c) pick the best cell in ScoreMap

		// Let's pretend for now we succeeded.
		Result = true;

		if (PathfindToPosition)
		{
			// (d) Go there! You should call your pathcomponent's UGAPathComponent::BuildPathFromDistanceMap() function
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


void UGASpatialComponent::EvaluateLayer(const FFunctionLayer& Layer, const FGAGridMap& DistanceMap, FGAGridMap& ScoreMap) const
{
	AActor* OwnerPawn = GetOwnerPawn();
	const AGAGridActor* Grid = GetGridActor();

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

				// Next, run it through the response curve using something like this
				// float Value = 4.5f;
				// float ModifiedValue = Layer.ResponseCurve.GetRichCurveConst()->Eval(Value, 0.0f);

				// Then add it's influence to the grid map, combining with the current value using one of the two operators
				//	SO_None				UMETA(DisplayName = "None"),
				//	SO_Add				UMETA(DisplayName = "Add"),			// add this layer to the accumulated buffer
				//	SO_Multiply			UMETA(DisplayName = "Multiply")		// multiply this layer into the accumulated buffer

				//ScoreMap.SetValue(CellRef, CombinedValue);


				// HERE ARE SOME ADDITIONAL HINTS

				// Here's how to get the player's pawn
				// APawn *PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);

				// Here's how to cast a ray

				// UWorld* World = GetWorld();
				// FHitResult HitResult;
				// FCollisionQueryParams Params;
				// FVector Start = Grid->GetCellPosition(CellRef);		// need a ray start
				// FVector End = PlayerPawn->GetActorLocation();		// need a ray end
				// Start.Z += 50.0f;									// offset by 50uus so 
				// Add any actors that should be ignored by the raycast by calling
				// Params.AddIgnoredActor(PlayerPawn);			// Probably want to ignore the player pawn
				// Params.AddIgnoredActor(OwnerPawn);			// Probably want to ignore the AI themself
				// bool bHitSomething = World->LineTraceSingleByChannel(HitResult, Start, End, ECollisionChannel::ECC_Visibility, Params);
				// If bHitSomething is false, then we have a clear LOS
			}
		}
	}
}