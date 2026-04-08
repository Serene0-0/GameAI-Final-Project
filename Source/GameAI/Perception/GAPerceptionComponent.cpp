#include "GAPerceptionComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GAPerceptionSystem.h"
#include "Engine/Light.h"
#include "Components/LightComponent.h"

namespace
{
	bool IsPointLitSimple(UWorld* World, const FVector& Point, float LightDetectionRadius)
	{
		if (!World || LightDetectionRadius <= 0.0f)
		{
			return false;
		}

		TArray<AActor*> LightActors;
		UGameplayStatics::GetAllActorsOfClass(World, ALight::StaticClass(), LightActors);

		const float RadiusSq = LightDetectionRadius * LightDetectionRadius;
		for (AActor* LightActor : LightActors)
		{
			ALight* Light = Cast<ALight>(LightActor);
			if (!Light)
			{
				continue;
			}

			const ULightComponent* LightComponent = Light->GetLightComponent();
			if (!LightComponent || !LightComponent->IsVisible() || (LightComponent->Intensity <= 0.0f))
			{
				continue;
			}

			const float DistSq = FVector::DistSquared(Point, Light->GetActorLocation());
			if (DistSq <= RadiusSq)
			{
				return true;
			}
		}

		return false;
	}
}

UGAPerceptionComponent::UGAPerceptionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// A bit of Unreal magic to make TickComponent below get called
	PrimaryComponentTick.bCanEverTick = true;

	TimeToAcknowledge = 2.0f;
	TimeToLose = 0.5f;
	bAffectedByLight = true;
	DarkVisionMultiplier = 0.3f;
	LightDetectionRadius = 500.0f;
}


void UGAPerceptionComponent::OnRegister()
{
	Super::OnRegister();

	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		PerceptionSystem->RegisterPerceptionComponent(this);
	}
}

void UGAPerceptionComponent::OnUnregister()
{
	Super::OnUnregister();

	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		PerceptionSystem->UnregisterPerceptionComponent(this);
	}
}


APawn* UGAPerceptionComponent::GetOwnerPawn() const
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



// Returns the Target this AI is attending to right now.

UGATargetComponent* UGAPerceptionComponent::GetCurrentTarget() const
{
	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);

	if (PerceptionSystem && PerceptionSystem->TargetComponents.Num() > 0)
	{
		UGATargetComponent* TargetComponent = PerceptionSystem->TargetComponents[0];
		if (TargetComponent->IsKnown())
		{
			return PerceptionSystem->TargetComponents[0];
		}
	}

	return NULL;
}

bool UGAPerceptionComponent::HasTarget() const
{
	return GetCurrentTarget() != NULL;
}


bool UGAPerceptionComponent::GetCurrentTargetState(FTargetState& TargetStateOut, FTargetView& TargetViewOut) const
{
	UGATargetComponent* Target = GetCurrentTarget();
	if (Target)
	{
		const FTargetView* TargetView = TargetMap.Find(Target->TargetGuid);
		if (TargetView)
		{
			TargetStateOut = Target->LastKnownState;
			TargetViewOut = *TargetView;
			return true;
		}

	}
	return false;
}


void UGAPerceptionComponent::GetAllTargetStates(bool OnlyKnown, TArray<FTargetState>& TargetStatesOut, TArray<FTargetView>& TargetViewsOut) const
{
	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		TArray<TObjectPtr<UGATargetComponent>>& TargetComponents = PerceptionSystem->GetAllTargetComponents();
		for (UGATargetComponent* TargetComponent : TargetComponents)
		{
			const FTargetView* TargetView = TargetMap.Find(TargetComponent->TargetGuid);
			if (TargetView)
			{
				if (!OnlyKnown || TargetComponent->IsKnown())
				{
					TargetStatesOut.Add(TargetComponent->LastKnownState);
					TargetViewsOut.Add(*TargetView);
				}
			}
		}
	}
}


void UGAPerceptionComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateAllTargetViews(DeltaTime);
}


void UGAPerceptionComponent::UpdateAllTargetViews(float DeltaTime)
{
	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		TArray<TObjectPtr<UGATargetComponent>>& TargetComponents = PerceptionSystem->GetAllTargetComponents();
		for (UGATargetComponent* TargetComponent : TargetComponents)
		{
			UpdateTargetView(TargetComponent, DeltaTime);
		}
	}
}

void UGAPerceptionComponent::UpdateTargetView(UGATargetComponent* TargetComponent, float DeltaTime)
{
	// REMEMBER: the UGAPerceptionComponent is going to be attached to the controller, not the pawn. So we call this special accessor to 
	// get the pawn that our controller is controlling
	APawn* OwnerPawn = GetOwnerPawn();
	if (OwnerPawn == NULL)
	{
		return;
	}

	FTargetView* TargetView = TargetMap.Find(TargetComponent->TargetGuid);
	if (TargetView == NULL)		// If we don't already have a target data for the given target component, add it
	{
		FTargetView NewTargetView;
		FGuid TargetGuid = TargetComponent->TargetGuid;
		TargetView = &TargetMap.Add(TargetGuid, NewTargetView);
	}


	// TODO PART 3
	// 
	// - Update TargetView->bClearLOS
	//		Use this.VisionParameters to determine whether the target is within the vision cone or not 
	//		(and ideally do so before you case a ray towards it)
	// - Update TargetView->Awareness
	//		On ticks when the AI has a clear LOS, the Awareness should grow
	//		On ticks when the AI does not have a clear LOS, the Awareness should decay
	//
	// Awareness should be clamped to the range [0, 1]
	// You can add parameters to the UGAPerceptionComponent to control the speed at which awareness rises and falls

	if (TargetView)
	{
		AActor* TargetActor = TargetComponent->GetOwner();
		FVector TargetPoint = TargetActor->GetActorLocation();

		TargetView->bClearLos = HasClearLOS(TargetActor, TargetPoint);

		float AwarenessChangeTime = TargetView->bClearLos ? TimeToAcknowledge : -TimeToLose;
		float AwarenessChangePerSecond = 1.0f / AwarenessChangeTime;
		float AwarenessDelta = AwarenessChangePerSecond * DeltaTime;

		TargetView->Awareness += AwarenessDelta;
		TargetView->Awareness = FMath::Clamp(TargetView->Awareness, 0.0f, 1.0f);
	}
}


const FTargetView* UGAPerceptionComponent::GetTargetView(FGuid TargetGuid) const
{
	return TargetMap.Find(TargetGuid);
}


bool UGAPerceptionComponent::HasClearLOS(const AActor *TargetActor, const FVector& TargetPoint, bool bApplyLightPenalty) const
{
	APawn* OwnerPawn = GetOwnerPawn();
	if (OwnerPawn == NULL)
	{
		return false;
	}

	FVector OwnerLocation = OwnerPawn->GetActorLocation();
	UWorld* World = GetWorld();
	bool ClearLos = false;
	float EffectiveVisionDistance = VisionParameters.VisionDistance;

	if (bApplyLightPenalty && bAffectedByLight)
	{
		const bool bPointIsLit = IsPointLitSimple(World, TargetPoint, LightDetectionRadius);
		if (!bPointIsLit)
		{
			EffectiveVisionDistance *= FMath::Max(0.0f, DarkVisionMultiplier);
		}
	}

	float D = FVector::Dist(TargetPoint, OwnerPawn->GetActorLocation());
	if (D <= EffectiveVisionDistance)
	{
		float AngleDot = FMath::Cos(FMath::DegreesToRadians(VisionParameters.VisionAngle/2.0f));
		FVector Forward = OwnerPawn->GetActorForwardVector();
		FVector OwnerToTarget = TargetPoint - OwnerLocation;
		OwnerToTarget.Normalize();

		if ((Forward | OwnerToTarget) >= AngleDot)
		{
			// within the vision angle
			// finally actually trace the line
			FHitResult HitResult;
			FCollisionQueryParams Params;
			FVector Start = OwnerLocation;
			FVector End = TargetPoint;
			Params.AddIgnoredActor(TargetActor);			// Probably want to ignore the player pawn
			Params.AddIgnoredActor(OwnerPawn);			// Probably want to ignore the AI themself
			bool bHitSomething = World->LineTraceSingleByChannel(HitResult, Start, End, ECollisionChannel::ECC_Visibility, Params);
			ClearLos = !bHitSomething;
		}
	}

	return ClearLos;
}