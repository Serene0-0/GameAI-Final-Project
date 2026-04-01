// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "CombatAIController.generated.h"

class UStateTreeAIComponent;
class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UBehaviorTree;

/**
 *	An AI Controller with perception (sight + hearing) and Behavior Tree support.
 *
 *	Perception fires HandlePerceptionUpdated() which writes three Blackboard keys:
 *	  - BB_PlayerLastKnownLocation  (Vector)
 *	  - BB_bPlayerVisible           (Bool)
 *	  - BB_TargetActor              (Object)
 *
 *	To use: create a Blueprint subclass, assign BehaviorTreeAsset, and create a
 *	matching Blackboard asset with the three keys above.
 */
UCLASS(abstract)
class ACombatAIController : public AAIController
{
	GENERATED_BODY()

	/** StateTree component — kept for legacy compatibility */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UStateTreeAIComponent* StateTreeAI;

	/** Perception component — drives sight and hearing detection */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UAIPerceptionComponent* AIPerception;

public:

	/** Blackboard key: player's last known world position */
	static const FName BB_PlayerLastKnownLocation;

	/** Blackboard key: whether the player is currently inside the vision cone */
	static const FName BB_bPlayerVisible;

	/** Blackboard key: reference to the player pawn (Object) */
	static const FName BB_TargetActor;

	/** Constructor */
	ACombatAIController();

protected:

	/**
	 *	Behavior Tree asset to run when this controller possesses a pawn.
	 *	Assign this in the Blueprint subclass (BP_HunterAIController).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

	/**
	 *	Sight detection radius (cm).
	 *	Editable per Blueprint subclass so different hunter types can have
	 *	different vision ranges without changing C++.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Perception", meta = (ClampMin = 100, ClampMax = 5000, Units = "cm"))
	float SightRadius = 1500.0f;

	/** Radius at which sight of the player is lost (should be > SightRadius) */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Perception", meta = (ClampMin = 100, ClampMax = 6000, Units = "cm"))
	float LoseSightRadius = 1800.0f;

	/** Half-angle of the vision cone in degrees (60 = 120-degree total cone) */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Perception", meta = (ClampMin = 1, ClampMax = 180, Units = "deg"))
	float PeripheralVisionHalfAngle = 60.0f;

	/** Radius within which gunshots or other sounds are heard */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Perception", meta = (ClampMin = 100, ClampMax = 8000, Units = "cm"))
	float HearingRadius = 2000.0f;

	/** Called when we take control of a pawn — starts the Behavior Tree */
	virtual void OnPossess(APawn* InPawn) override;

	/** Called when we release control of a pawn */
	virtual void OnUnPossess() override;

	/**
	 *	Fires whenever the perception system detects a change for any tracked actor.
	 *	Writes to the Blackboard so the Behavior Tree can react.
	 */
	UFUNCTION()
	void HandlePerceptionUpdated(const TArray<AActor*>& UpdatedActors);
};
