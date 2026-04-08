#include "GATargetComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameAI/Grid/GAGridActor.h"
#include "GAPerceptionSystem.h"
#include "ProceduralMeshComponent.h"


UGATargetComponent::UGATargetComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// A bit of Unreal magic to make TickComponent below get called
	PrimaryComponentTick.bCanEverTick = true;

	SetTickGroup(ETickingGroup::TG_PostUpdateWork);

	// Generate a new guid
	TargetGuid = FGuid::NewGuid();

	OccupancyMapDiffusionPerSecond = 0.25f;
}


AGAGridActor* UGATargetComponent::GetGridActor() const
{
	AGAGridActor* Result = GridActor.Get();
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
				GridActor = Result;
			}
		}

		return Result;
	}
}


void UGATargetComponent::OnRegister()
{
	Super::OnRegister();

	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		PerceptionSystem->RegisterTargetComponent(this);
	}

	const AGAGridActor* Grid = GetGridActor();
	if (Grid)
	{
		OccupancyMap = FGAGridMap(Grid, 0.0f);
	}
}

void UGATargetComponent::OnUnregister()
{
	Super::OnUnregister();

	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		PerceptionSystem->UnregisterTargetComponent(this);
	}
}



void UGATargetComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	bool isImmediate = false;

	// update my perception state FSM
	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		TArray<TObjectPtr<UGAPerceptionComponent>> &PerceptionComponents = PerceptionSystem->GetAllPerceptionComponents();
		for (UGAPerceptionComponent* PerceptionComponent : PerceptionComponents)
		{
			const FTargetView* TargetView = PerceptionComponent->GetTargetView(TargetGuid);
			if (TargetView && (TargetView->Awareness >= 1.0f))
			{
				isImmediate = true;
				break;
			}
		}
	}

	if (isImmediate)
	{
		AActor* Owner = GetOwner();
		LastKnownState.State = GATS_Immediate;

		// REFRESH MY STATE
		LastKnownState.Set(Owner->GetActorLocation(), Owner->GetVelocity());

		// Tell the omap to clear out and put all the probability in the observed location
		OccupancyMapSetPosition(LastKnownState.Position);
	}
	else if (IsKnown())
	{
		LastKnownState.State = GATS_Hidden;
	}

	if (LastKnownState.State == GATS_Hidden)
	{
		OccupancyMapUpdate();
	}

	// As long as I'm known, whether I'm immediate or not, diffuse the probability in the omap

	if (IsKnown())
	{
		OccupancyMapDiffuse(DeltaTime);
	}

	if (bDebugOccupancyMap)
	{
		AGAGridActor* Grid = GetGridActor();
		if (Grid && Grid->DebugMeshComponent)
		{
			Grid->DebugGridMap = OccupancyMap;
			Grid->RefreshDebugTexture();
			Grid->DebugMeshComponent->SetVisibility(true);
		}
	}
}


void UGATargetComponent::OccupancyMapSetPosition(const FVector& Position)
{
	const AGAGridActor* Grid = GetGridActor();
	if (Grid)
	{
		FCellRef CellRef = Grid->GetCellRef(Position);
		if (CellRef.IsValid())
		{
			// TODO PART 4
			// We've been observed to be in a given position
			// Clear out all probability in the omap, and set the appropriate cell to P = 1.0
			OccupancyMap.ResetData(0.0f);
			OccupancyMap.SetValue(CellRef, 1.0f);
		}
	}
}


void UGATargetComponent::OccupancyMapUpdate()
{
	AActor* Owner = GetOwner();
	FVector OwnerLocation = Owner->GetActorLocation();
	const AGAGridActor* Grid = GetGridActor();
	if (Grid)
	{
		FGAGridMap VisibilityMap(Grid, 0.0f);
		float Offset = 50.0f;

		APawn* OwnerPawn = Cast<APawn>(Owner);
		if (OwnerPawn)
		{
			Offset = OwnerPawn->GetDefaultHalfHeight();
		}

		// TODO PART 4

		// STEP 1: Build a visibility map, based on the perception components of the AIs in the world
		// The visibility map is a simple map where each cell is either 0 (not currently visible to ANY perceiver) or 1 (currently visible to one or more perceivers).
		// 

		UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
		if (PerceptionSystem)
		{
			TArray<TObjectPtr<UGAPerceptionComponent>>& PerceptionComponents = PerceptionSystem->GetAllPerceptionComponents();
			for (UGAPerceptionComponent* PerceptionComponent : PerceptionComponents)
			{
				//  Find visible cells from this AI
				for (int32 Y = VisibilityMap.GridBounds.MinY; Y <= VisibilityMap.GridBounds.MaxY; Y++)
				{
					for (int32 X = VisibilityMap.GridBounds.MinX; X <= VisibilityMap.GridBounds.MaxX; X++)
					{
						float Value;
						FCellRef Cell(X, Y);
						// Note, don't bother re-testing if we already know the cell to be visible.
						if (EnumHasAllFlags(Grid->GetCellData(Cell), ECellData::CellDataTraversable))
						{
							if (VisibilityMap.GetValue(Cell, Value) && (Value == 0.0f))
							{
								FVector CellPoint = Grid->GetCellPosition(Cell);
								CellPoint.Z += Offset;
								if (PerceptionComponent->HasClearLOS(Owner, CellPoint))
								{
									// it's visible!
									VisibilityMap.SetValue(Cell, 1.0f);
								}
							}
						}
						else
						{
							// consider it visible if it's not traversable
							VisibilityMap.SetValue(Cell, 1.0f);
						}
					}
				}
			}
		}

		// as long as the target is not standing on a non traversable cell, don't consider that cell visible
		// other we could potentially be clearing out probability in the location the target actually is.
		{
			FCellRef ActualTargetCell = Grid->GetCellRef(OwnerLocation);
			if (EnumHasAllFlags(Grid->GetCellData(ActualTargetCell), ECellData::CellDataTraversable))
			{
				// consider it not visible if the player is standing on it
				VisibilityMap.SetValue(ActualTargetCell, 0.0f);
			}
		}

		// STEP 2: Clear out the probability in the visible cells
		{
			float TotalP = 0.0f;

			for (int32 Y = VisibilityMap.GridBounds.MinY; Y <= VisibilityMap.GridBounds.MaxY; Y++)
			{
				for (int32 X = VisibilityMap.GridBounds.MinX; X <= VisibilityMap.GridBounds.MaxX; X++)
				{
					float Visible;
					FCellRef Cell(X, Y);

					if (VisibilityMap.GetValue(Cell, Visible) && (Visible > 0.0f))
					{
						OccupancyMap.SetValue(Cell, 0.0f);
					}
					else
					{
						float P;
						if (OccupancyMap.GetValue(Cell, P))
						{
							TotalP += P;
						}
					}
				}
			}

			if (TotalP > 0.0f)
			{
				float NormFactor = 1.0f / TotalP;
				float MaxP = 0.0f;
				FCellRef MaxCell;

				// STEP 3: Renormalize the OMap, so that it's still a valid probability distribution
				for (int32 Y = OccupancyMap.GridBounds.MinY; Y <= OccupancyMap.GridBounds.MaxY; Y++)
				{
					for (int32 X = OccupancyMap.GridBounds.MinX; X <= OccupancyMap.GridBounds.MaxX; X++)
					{
						float P;
						FCellRef Cell(X, Y);

						if (OccupancyMap.GetValue(Cell, P) && (P > 0.0f))
						{
							float NewP = P * NormFactor;
							OccupancyMap.SetValue(Cell, NewP);
							if (NewP > MaxP)
							{
								MaxP = NewP;
								MaxCell = Cell;
							}
						}
					}
				}

				// STEP 4: Extract the highest-likelihood cell on the omap and refresh the LastKnownState.
				if (MaxCell.IsValid())
				{
					LastKnownState.Position = Grid->GetCellPosition(MaxCell);
					LastKnownState.Position.Z += Offset;
					LastKnownState.Velocity = FVector::ZeroVector;
				}
			}
		}
	}
}


void UGATargetComponent::OccupancyMapDiffuse(float DeltaTime)
{
	const AGAGridActor* Grid = GetGridActor();
	if (Grid)
	{
		FGAGridMap ScratchMap(Grid, 0.0f);

		// TODO PART 4
		// Diffuse the probability in the OMAP

		float DiffusionRate = OccupancyMapDiffusionPerSecond * DeltaTime;

		// How much I'll give an adjacent neighbor
		// This assumes I give adjacent neighbors Alpha and diagonal neighbors Alpha/sqrt(2) to account for greater distance
		float Alpha = DiffusionRate / (4.0f + 4.0f / UE_SQRT_2);
		float DiagonalAlpha = Alpha / UE_SQRT_2;

		for (int32 Y = OccupancyMap.GridBounds.MinY; Y <= OccupancyMap.GridBounds.MaxY; Y++)
		{
			for (int32 X = OccupancyMap.GridBounds.MinX; X <= OccupancyMap.GridBounds.MaxX; X++)
			{
				FCellRef Cell(X, Y);
				float P;

				// Only do anything if I have probability to diffuse
				if (OccupancyMap.GetValue(Cell, P) && (P > 0.0f))
				{
					float AdjacentD = Alpha * P;
					float DiagonalD = DiagonalAlpha * P;
					float TotalPDiffused = 0.0f;

					for (int32 DY = -1; DY <= 1; DY++)
					{
						for (int32 DX = -1; DX <= 1; DX++)
						{
							if ((DX != 0) || (DY != 0))
							{
								FCellRef NeighborCell(X + DX, Y + DY);
								if (Grid->IsCellRefInBounds(NeighborCell))
								{
									if (EnumHasAllFlags(Grid->GetCellData(NeighborCell), ECellData::CellDataTraversable))
									{
										float NP;
										if (ScratchMap.GetValue(NeighborCell, NP))
										{
											bool Adjacent = (DX == 0) || DY == 0;
											float D = Adjacent ? AdjacentD : DiagonalD;
											NP += D;
											TotalPDiffused += D;

											ScratchMap.SetValue(NeighborCell, NP);
										}
									}
								}
							}
						}
					}

					float SP;
					if (ScratchMap.GetValue(Cell, SP))
					{
						// Remember to also give my future self the remaining P that was not diffused
						ScratchMap.SetValue(Cell, SP + (P - TotalPDiffused));
					}
				}
			}
		}

		// Finally, copy the results from the scratch map to the occupancy map
		OccupancyMap.Data = ScratchMap.Data;
	}
}
