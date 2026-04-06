// Copyright Epic Games, Inc. All Rights Reserved.

#include "CombatAIController.h"
#include "Components/StateTreeAIComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"

//  Blackboard key names 
// These must exactly match the key names you create in the BB_Hunter asset.
const FName ACombatAIController::BB_PlayerLastKnownLocation = TEXT("PlayerLastKnownLocation");
const FName ACombatAIController::BB_bPlayerVisible          = TEXT("bPlayerVisible");
const FName ACombatAIController::BB_TargetActor             = TEXT("TargetActor");

// Constructor 

ACombatAIController::ACombatAIController()
{
	//  StateTree (kept for backward-compatibility) 
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
	check(StateTreeAI);
	bStartAILogicOnPossess = true;
	bAttachToPawn = true;

	// Perception component 
	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));

	// Vision cone — values can be tweaked per Blueprint subclass via EditDefaultsOnly
	UAISenseConfig_Sight* SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius                          = SightRadius;
	SightConfig->LoseSightRadius                      = LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees         = PeripheralVisionHalfAngle;
	SightConfig->DetectionByAffiliation.bDetectEnemies    = true;   // player is "enemy" to the hunter
	SightConfig->DetectionByAffiliation.bDetectNeutrals   = false;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;
	SightConfig->SetMaxAge(10.0f);                                   // forget sight stimulus after 10 s
	AIPerception->ConfigureSense(*SightConfig);

	// Hearing radius — used when the player fires a weapon (see UAISense_Hearing::ReportNoiseEvent)
	UAISenseConfig_Hearing* HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange                              = HearingRadius;
	HearingConfig->DetectionByAffiliation.bDetectEnemies    = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals   = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = false;
	HearingConfig->SetMaxAge(5.0f);                                  // forget sound after 5 s
	AIPerception->ConfigureSense(*HearingConfig);

	// Sight is the dominant sense — determines which stimulus is prioritised
	AIPerception->SetDominantSense(SightConfig->GetSenseImplementation());

	// Bind the perception callback.
	// OnPerceptionUpdated is a DYNAMIC_MULTICAST_DELEGATE so AddDynamic is correct.
	AIPerception->OnPerceptionUpdated.AddDynamic(this, &ACombatAIController::HandlePerceptionUpdated);
}

// Possession 

void ACombatAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Start the Behavior Tree if one has been assigned in the Blueprint subclass.
	// RunBehaviorTree() also initialises the BlackboardComponent automatically.
	if (BehaviorTreeAsset)
	{
		RunBehaviorTree(BehaviorTreeAsset);
	}
}

void ACombatAIController::OnUnPossess()
{
	Super::OnUnPossess();

	// Stop the Behavior Tree cleanly so it doesn't keep ticking with a null pawn
	if (UBrainComponent* BrainComp = GetBrainComponent())
	{
		BrainComp->StopLogic(TEXT("Unpossessed"));
	}
}

// ── Perception callback 
void ACombatAIController::HandlePerceptionUpdated(const TArray<AActor*>& UpdatedActors)
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB)
	{
		// No Blackboard yet — BT hasn't started (no asset assigned in BP subclass)
		return;
	}

	for (AActor* Actor : UpdatedActors)
	{
		// We only care about actors with the "Player" tag (set on ACombatCharacter)
		if (!Actor || !Actor->ActorHasTag(FName("Player")))
		{
			continue;
		}

		// Ask the perception component which actors are currently visible via sight
		TArray<AActor*> CurrentlyVisible;
		AIPerception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), CurrentlyVisible);
		const bool bIsVisible = CurrentlyVisible.Contains(Actor);

		// ── Write to Blackboard 

		BB->SetValueAsBool(BB_bPlayerVisible, bIsVisible);

		if (bIsVisible)
		{
			// Update last known location every frame we can see the player
			BB->SetValueAsVector(BB_PlayerLastKnownLocation, Actor->GetActorLocation());
			BB->SetValueAsObject(BB_TargetActor, Actor);
		}
		// When sight is lost we intentionally leave BB_PlayerLastKnownLocation unchanged
		// so the Investigate branch of the BT can still navigate to where we last saw them.
	}
}
