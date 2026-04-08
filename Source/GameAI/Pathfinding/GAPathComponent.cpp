#include "GAPathComponent.h"
#include "GameFramework/NavMovementComponent.h"
#include "Kismet/GameplayStatics.h"

UGAPathComponent::UGAPathComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	State = GAPS_None;
	bDestinationValid = false;
	ArrivalDistance = 100.0f;

	// A bit of Unreal magic to make TickComponent below get called
	PrimaryComponentTick.bCanEverTick = true;
}


const AGAGridActor* UGAPathComponent::GetGridActor() const
{
	if (GridActor.Get())
	{
		return GridActor.Get();
	}
	else
	{
		AGAGridActor* Result = NULL;
		AActor *GenericResult = UGameplayStatics::GetActorOfClass(this, AGAGridActor::StaticClass());
		if (GenericResult)
		{
			Result = Cast<AGAGridActor>(GenericResult);
			if (Result)
			{
				// Cache the result
				// Note, GridActor is marked as mutable in the header, which is why this is allowed in a const method
				GridActor = Result;
			}
		}

		return Result;
	}
}

APawn* UGAPathComponent::GetOwnerPawn() const
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


void UGAPathComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (GetOwnerPawn() == NULL)
	{
		return;
	}

	bool Valid = false;
	if (bDestinationValid)
	{
		RefreshPath();
		Valid = true;
	}
	else if (bDistanceMapPathValid)
	{
		Valid = true;
	}
	if (Valid)
	{
		if (State == GAPS_Active)
		{
			FollowPath();
		}
	}

	// Super important! Otherwise, unbelievably, the Tick event in Blueprint won't get called

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

EGAPathState UGAPathComponent::RefreshPath()
{
	AActor* Owner = GetOwnerPawn();
	if (Owner == NULL)
	{
		State = GAPS_Invalid;
		return State;
	}

	FVector StartPoint = Owner->GetActorLocation();

	check(bDestinationValid);

	float DistanceToDestination = FVector::Dist(StartPoint, Destination);

	if (DistanceToDestination <= ArrivalDistance)
	{
		// Yay! We got there!
		State = GAPS_Finished;
	}
	else
	{
		TArray<FPathStep> UnsmoothedSteps;
		Steps.Empty();

		// Replan the path!
		State = AStar(StartPoint, UnsmoothedSteps);

		// To debug A* without smoothing uncomment this line and then skip the call to SmoothPath below:
		//Steps = UnsmoothedSteps;

		if (State == EGAPathState::GAPS_Active)
		{
			// Smooth the path!
			State = SmoothPath(StartPoint, UnsmoothedSteps, Steps);
		}
	}

	return State;
}

namespace 
{
	struct FSearchNode
    {
    	FCellRef CellRef;
		float GScore;
		float FScore;
		
		FSearchNode() : CellRef(FCellRef::Invalid), GScore(0.0f), FScore(0.0f) {}
		FSearchNode(FCellRef Cell, float G, float F) : CellRef(Cell), GScore(G), FScore(F) {}
		
		bool operator<(const FSearchNode& Other) const
		{
			return FScore < Other.FScore;
		}
    };
}


float UGAPathComponent::Heuristic(const FCellRef& Cell) const
{
	float DX = static_cast<float>(FMath::Abs(Cell.X - DestinationCell.X));
	float DY = static_cast<float>(FMath::Abs(Cell.Y - DestinationCell.Y));
	return FMath::Sqrt(DX * DX + DY * DY);
}

void UGAPathComponent::ReconstructPath(const TMap<FCellRef, FCellRef>& CameFrom, const FCellRef& StartCell, const FCellRef& CurrentCell, TArray<FPathStep>& OutSteps) const
{
	const AGAGridActor* Grid = GetGridActor();

	if (!Grid)
	{
		return;
	}
	
	TArray<FCellRef> PathCells;
	FCellRef TraceCell = CurrentCell;
	

	//trace back to the start cell
	while (TraceCell.IsValid() && TraceCell != StartCell)
	{
		PathCells.Insert(TraceCell, 0);
		
		const FCellRef* PrevCell = CameFrom.Find(TraceCell);
		if (PrevCell)
		{
			TraceCell = *PrevCell;
		}
		else
		{
			break;
		}
	}
	
	//transfer cell to the path steps
	OutSteps.Empty();
	for (const FCellRef& Cell : PathCells)
	{
		FVector CellPosition = Grid->GetCellPosition(Cell);
		FPathStep Step;
		Step.Set(CellPosition, Cell);
		OutSteps.Add(Step);
	}
}

void UGAPathComponent::GetNeighbors(const FCellRef& Cell, TArray<FCellRef>& StepsOut) const
{
	//8 directions of the cell
	static const FCellRef Directions[8] = {
		FCellRef(-1, 0), FCellRef(-1, -1), FCellRef(-1, 1), 
		FCellRef(1, 0), FCellRef(1, 1), FCellRef(1, -1),
		FCellRef(0, -1), FCellRef(0, 1)
	};
	
	StepsOut.Empty(8);
	for (int32 i = 0; i < 8; i++)
	{
		FCellRef NeighborCell(Cell.X + Directions[i].X, Cell.Y + Directions[i].Y);
		StepsOut.Add(NeighborCell);
	}
}


EGAPathState UGAPathComponent::AStar(const FVector& StartPoint, TArray<FPathStep>& StepsOut) const
{
	const AGAGridActor* Grid = GetGridActor();
	if (!Grid)
	{
		return GAPS_Invalid;
	}

	//get start cell & check validity
	FCellRef StartCell = Grid->GetCellRef(StartPoint);
	if (!StartCell.IsValid() || !bDestinationValid)
	{
		return GAPS_Invalid;
	}
	
	// if the start point is the destination
	if (StartCell == DestinationCell)
	{
		StepsOut.SetNum(1);
        StepsOut[0].Set(Destination, DestinationCell);
		return GAPS_Active;
	}
	
	// initial data structure of the algorithm
	TArray<FSearchNode> OpenSet;
	TSet<FCellRef> VisitedSet;
	TMap<FCellRef, FCellRef> CameFrom;
	TMap<FCellRef, float> GScore;
	TArray<FCellRef> Neighbors;
	
	float StartHScore = Heuristic(StartCell);
	OpenSet.HeapPush(FSearchNode(StartCell, 0.0f, StartHScore));
	GScore.Add(StartCell, 0.0f);


	while (!OpenSet.IsEmpty())
	{
		// extract the lowest fscore node
		FSearchNode CurrentNode;
		OpenSet.HeapPop(CurrentNode);

		if (VisitedSet.Contains(CurrentNode.CellRef))
		{
			continue;
		}
		
		if (CurrentNode.CellRef == DestinationCell)
		{
			// reconstruct path
			ReconstructPath(CameFrom, StartCell, DestinationCell, StepsOut);
			return GAPS_Active;
		}
		
		VisitedSet.Add(CurrentNode.CellRef);
		
		GetNeighbors(CurrentNode.CellRef, Neighbors);
		
		for (const FCellRef& Cell : Neighbors)
		{
			//check if the cell is in the grid
			if (!Grid->IsCellRefInBounds(Cell))
			{
				continue;
			}
			
			//check the traversability
			ECellData Flags = Grid->GetCellData(Cell);
			if (!EnumHasAllFlags(Flags, ECellData::CellDataTraversable))
			{
				continue;
			}
			
			//check if the cell is visited
			if (VisitedSet.Contains(Cell))
			{
				continue;
			}
			
			//calculate the cost
			int32 D_X = FMath::Abs(Cell.X - CurrentNode.CellRef.X);
			int32 D_Y = FMath::Abs(Cell.Y - CurrentNode.CellRef.Y);
			bool isDiagonal = (D_X != 0) && (D_Y != 0);
			float Cost = isDiagonal ? FMath::Sqrt(2.0f) : 1.0f;
			float TentativeGScore = CurrentNode.GScore + Cost;
			
			//find if the lower-score cell exists
			float* Score = GScore.Find(Cell);
			if (Score == nullptr || TentativeGScore < *Score)
			{
				CameFrom.Emplace(Cell, CurrentNode.CellRef);
				GScore.Emplace(Cell, TentativeGScore);
				
				float HScore = Heuristic(Cell);
				float FScore = TentativeGScore + HScore;
				OpenSet.HeapPush(FSearchNode(Cell, TentativeGScore, FScore));
			}
		}
	}
	return GAPS_Invalid;
}

bool UGAPathComponent::LineTrace(const FCellRef& StartCell, const FCellRef& EndCell) const
{
	const AGAGridActor* Grid = GetGridActor();

	if (!Grid)
	{
		return false;
	}

	if (StartCell == EndCell)
	{
		return true;
	}
	
	//check the traversability of start&end cell
	if (!EnumHasAllFlags(Grid->GetCellData(StartCell), ECellData::CellDataTraversable) ||
		!EnumHasAllFlags(Grid->GetCellData(EndCell), ECellData::CellDataTraversable))
	{
		return false;
	}
	
	//change the coordinate to grid space
	FVector2D StartPos = Grid->GetCellNormalizedGridSpacePosition(StartCell);
	FVector2D EndPos = Grid->GetCellNormalizedGridSpacePosition(EndCell);
	
	FVector2D Direction = EndPos - StartPos;
	float Distance = Direction.Size();

	if (Distance < KINDA_SMALL_NUMBER)
	{
		return true;
	}
	Direction.Normalize();
	
	float StepSize = 0.5f;
	int32 NumSteps = FMath::CeilToInt(Distance / StepSize);
	
	FCellRef CurrentCell = StartCell;

	for (int32 i = 1; i <= NumSteps; i++)
	{
		float t =  static_cast<float>(i) * StepSize;
		
		if (t > Distance)
        {
        	break;
        }
		
		FVector2D CurrentPos = StartPos + Direction * t;
		
		// change back to world coordinate
		FVector WorldPos = Grid->TransformNormalizedGridSpaceToWorld(CurrentPos);
		FCellRef WorldCell = Grid->GetCellRef(WorldPos, true);

		if (WorldCell != CurrentCell)
		{
			//check the boundary
			if (!Grid->IsCellRefInBounds(WorldCell))
			{
				return false;
			}
			//check the traversability
			if (!EnumHasAllFlags(Grid->GetCellData(WorldCell), ECellData::CellDataTraversable))
			{
				return false;
			}
			
			CurrentCell = WorldCell;
		}
	}
	
	return true;
}


namespace
{
	struct FDijkstraNode
	{
		FCellRef CellRef;
		float Distance;
		
		FDijkstraNode() : CellRef(FCellRef::Invalid), Distance(0.0f) {}
		FDijkstraNode(FCellRef Cell, float Dist) : CellRef(Cell), Distance(Dist) {}
		
		//min-heap comparator
		bool operator<(const FDijkstraNode& Other) const
		{
			return Distance < Other.Distance;
		}
	};
}


bool UGAPathComponent::Dijkstra(const FVector& StartPoint, FGAGridMap& DistanceMapOut) const
{
	const AGAGridActor* Grid = GetGridActor();
	
	FCellRef StartCell = Grid->GetCellRef(StartPoint);
	if (!StartCell.IsValid())
	{
		return false;
	}
	
	// initialize distance map
	DistanceMapOut = FGAGridMap(Grid, FLT_MAX);
	
	// initialize data structure
	TArray<FDijkstraNode> OpenSet;
	TSet<FCellRef> VisitedSet;
	TArray<FCellRef> Neighbors;
	
	// push the StartCell into the data structure
	OpenSet.HeapPush(FDijkstraNode(StartCell, 0.0f));
	DistanceMapOut.SetValue(StartCell, 0.0f);
	
	while (!OpenSet.IsEmpty())
	{
		// extract the shortest-distance cell
		FDijkstraNode CurrentNode;
		OpenSet.HeapPop(CurrentNode);
		
		if (VisitedSet.Contains(CurrentNode.CellRef))
		{
			continue;
		}
		// marked visited cell
		VisitedSet.Add(CurrentNode.CellRef);
		// get all neighbors of the cell
		GetNeighbors(CurrentNode.CellRef, Neighbors);
		
		// iterate all neighbor
		for (const FCellRef& Cell : Neighbors)
		{
			// check if the cell is in the grid
			if (!Grid->IsCellRefInBounds(Cell))
			{
				continue;
			}
			
			// check the traversability
			ECellData Flags = Grid->GetCellData(Cell);
			if (!EnumHasAllFlags(Flags, ECellData::CellDataTraversable))
			{
				continue;
			}
			
			// check if the cell is visited
			if (VisitedSet.Contains(Cell))
			{
				continue;
			}
			
			// calculate the cost
			int32 D_X = FMath::Abs(Cell.X - CurrentNode.CellRef.X);
			int32 D_Y = FMath::Abs(Cell.Y - CurrentNode.CellRef.Y);
			bool isDiagonal = (D_X != 0) && (D_Y != 0);
			float Cost = isDiagonal ? FMath::Sqrt(2.0f) : 1.0f;
			float NewDistance = CurrentNode.Distance + Cost;
			
			// extract currentDistance of the neighbor
			float CurrentDistance = FLT_MAX;
			DistanceMapOut.GetValue(Cell, CurrentDistance);
			
			// update distance if it is shorter
			if (NewDistance < CurrentDistance)
			{
				DistanceMapOut.SetValue(Cell, NewDistance);
				OpenSet.HeapPush(FDijkstraNode(Cell, NewDistance));
			}
		}
	}

	return true;
}

bool UGAPathComponent::BuildPathFromDistanceMap(const FVector& EndPoint, const FCellRef& EndCellRef, 
												const FGAGridMap& DistanceMap)
{
	if (bChargePlayerMode)
	{
		return false;
	}
	bDistanceMapPathValid = false;
	bDestinationValid = false;
	
	const AGAGridActor* Grid = GetGridActor();
	
	if (!EndCellRef.IsValid())
	{
		return false;
	}
	
	// extract distance of start cell
	float StartDistance = FLT_MAX;
	if (!DistanceMap.GetValue(EndCellRef, StartDistance))
	{
		return false;
	}
	
	// if the start cell is the destination, no need to move
	if (StartDistance < KINDA_SMALL_NUMBER)
	{
		bDistanceMapPathValid = true;
		State = GAPS_Finished;
		Steps.Empty();
		return true;
	}
	
	// reconstruct unsmoothed path
	TArray<FPathStep> UnsmoothedSteps;
	TArray<FCellRef> Neighbors;
	
	FCellRef CurrentCell = EndCellRef;
	float CurrentDistance = StartDistance;
	
	// extract the shortest-distance neighbor each iteration
	while (CurrentDistance > KINDA_SMALL_NUMBER)
	{
		GetNeighbors(CurrentCell, Neighbors);
		
		FCellRef BestNeighbor = FCellRef::Invalid;
		float ShortestDistance = CurrentDistance;
		
		for (const FCellRef& Cell : Neighbors)
		{
			// check if the cell is in the grid
			if (!Grid->IsCellRefInBounds(Cell))
			{
				continue;
			}
			
			// check the traversability
			ECellData Flags = Grid->GetCellData(Cell);
			if (!EnumHasAllFlags(Flags, ECellData::CellDataTraversable))
			{
				continue;
			}
			
			// extract neighbor distance
			float NeighborDistance = FLT_MAX;
			if (!DistanceMap.GetValue(Cell, NeighborDistance))
			{
				continue;
			}

			// update best cell & shortest distance
			if (NeighborDistance < ShortestDistance)
			{
				ShortestDistance = NeighborDistance;
				BestNeighbor = Cell;
			}
		}
		
		// no valid neighbor, fail to build
		if (!BestNeighbor.IsValid())
		{
			return false;
		}
		
		// add best neighbor cell to PathStep
		FVector CellPosition = Grid->GetCellPosition(BestNeighbor);
		FPathStep CurrentStep;
		CurrentStep.Set(CellPosition, BestNeighbor);
		UnsmoothedSteps.Add(CurrentStep);
		
		// to next cell
		CurrentCell = BestNeighbor;
		CurrentDistance = ShortestDistance;
	}

	if (UnsmoothedSteps.Num() == 0)
	{
		return false;
	}
	
	// smooth paths
	Steps.Empty();
	EGAPathState SmoothedState = SmoothPath(EndPoint, UnsmoothedSteps, Steps);
	
	if (SmoothedState != GAPS_Active)
	{
		return false;
	}

	bDistanceMapPathValid = true;
	
	if (bDistanceMapPathValid)
	{
		// once you have built the path (i.e. filled in the Steps array in the GAPathComponent), set the path component's state to GAPS_Active
		// This will cause 
		State = GAPS_Active;
	}

	return bDistanceMapPathValid;
}


float UGAPathComponent::GetPathLength() const
{
	if (State == GAPS_Active)
	{
		float L = 0.0f;
		FVector CurrentPoint;

		const APawn *Pawn = GetOwnerPawn();
		CurrentPoint = Pawn->GetActorLocation();

		for (const FPathStep& Step : Steps)
		{
			L += FVector::Distance(CurrentPoint, Step.Point);
			CurrentPoint = Step.Point;
		}

		return L;
	}
	else
	{
		return 0.0f;
	}
}


EGAPathState UGAPathComponent::SmoothPath(const FVector& StartPoint, const TArray<FPathStep>& UnsmoothedSteps, TArray<FPathStep>& SmoothedStepsOut) const
{
	const AGAGridActor* Grid = GetGridActor();
	if (!Grid)
	{
		return GAPS_Invalid;
	}
	
	// skip smoothing if empty or single-step path 
	if (UnsmoothedSteps.Num() <= 1)
	{
		SmoothedStepsOut = UnsmoothedSteps;
		return GAPS_Active;
	}
	
	SmoothedStepsOut.Empty();
	
	//get the start cell
	FCellRef StartCell = Grid->GetCellRef(StartPoint);
	if (!StartCell.IsValid())
	{
		return GAPS_Invalid;
	}
	
	FCellRef CurrentCell = StartCell;
	int32 CurrentIndex = 0;

	while (CurrentIndex < UnsmoothedSteps.Num())
	{
		int32 FarthestReachableIndex = CurrentIndex;
		
		for (int32 Test = CurrentIndex + 1; Test < UnsmoothedSteps.Num(); Test++)
		{
			FCellRef TestCell = UnsmoothedSteps[Test].CellRef;

			if (LineTrace(CurrentCell, TestCell))
			{
				FarthestReachableIndex = Test;
			}
			else
			{
				break;
			}
		}
		
		// add new fatest cell
		if (FarthestReachableIndex > CurrentIndex)
		{
			SmoothedStepsOut.Add(UnsmoothedSteps[FarthestReachableIndex]);
			CurrentCell = UnsmoothedSteps[FarthestReachableIndex].CellRef;
			CurrentIndex = FarthestReachableIndex;
		}
		else
		{
			if (CurrentIndex + 1 < UnsmoothedSteps.Num())
			{
				SmoothedStepsOut.Add(UnsmoothedSteps[CurrentIndex + 1]);
				CurrentCell = UnsmoothedSteps[CurrentIndex + 1].CellRef;
				CurrentIndex++;
			}
			else
			{
				break;
			}
		}
	}

	if (SmoothedStepsOut.Num() == 0 || SmoothedStepsOut.Last().CellRef != UnsmoothedSteps.Last().CellRef)
	{
		SmoothedStepsOut.Add(UnsmoothedSteps.Last());
	}

	return GAPS_Active;
}

void UGAPathComponent::FollowPath()
{
	AActor* Owner = GetOwnerPawn();
	if (Owner == NULL)
	{
		return;
	}

	FVector StartPoint = Owner->GetActorLocation();

	check(State == GAPS_Active);
	check(Steps.Num() > 0);

	// Always follow the first step, assuming that we are refreshing the whole path every tick
	FVector V = Steps[0].Point - StartPoint;
	V.Normalize();

	UNavMovementComponent* MovementComponent = Owner->FindComponentByClass<UNavMovementComponent>();
	if (MovementComponent)
	{
		MovementComponent->RequestPathMove(V);
	}
}


EGAPathState UGAPathComponent::SetDestination(const FVector &DestinationPoint)
{
	bChargePlayerMode = true;
	bDistanceMapPathValid = false;
	
	Destination = DestinationPoint;

	State = GAPS_Invalid;
	bDestinationValid = true;

	const AGAGridActor* Grid = GetGridActor();
	if (Grid)
	{
		FCellRef CellRef = Grid->GetCellRef(Destination);
		if (CellRef.IsValid())
		{
			DestinationCell = CellRef;
			bDestinationValid = true;

			RefreshPath();
		}
	}

	return State;
}

void UGAPathComponent::ClearPath()
{
	bDestinationValid = false;
	bDistanceMapPathValid = false;
	bChargePlayerMode = false;
	State = GAPS_None;
	Steps.Empty();
}