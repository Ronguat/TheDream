#include "TDStateMachineTools.h"

#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace
{
	UEdGraphPin* FindPin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction)
			{
				return Pin;
			}
		}
		return nullptr;
	}
}

TArray<UEdGraphNode*> UTDStateMachineTools::GetGraphNodes(UEdGraph* Graph)
{
	TArray<UEdGraphNode*> Result;
	if (Graph)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			Result.Add(Node);
		}
	}
	return Result;
}

UEdGraphNode* UTDStateMachineTools::AddState(UEdGraph* StateMachineGraph, FName StateName, FIntPoint Position)
{
	UAnimationStateMachineGraph* Machine = Cast<UAnimationStateMachineGraph>(StateMachineGraph);
	if (!Machine)
	{
		return nullptr;
	}

	UAnimStateNode* Template = NewObject<UAnimStateNode>(Machine);
	UAnimStateNode* NewState = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateNode>(
		Machine, Template, FVector2f(static_cast<float>(Position.X), static_cast<float>(Position.Y)));
	if (!NewState)
	{
		return nullptr;
	}

	// The bound graph's name is what the state is called; the node itself carries no title.
	if (NewState->BoundGraph && StateName != NAME_None)
	{
		FBlueprintEditorUtils::RenameGraph(NewState->BoundGraph, StateName.ToString());
	}

	if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Machine))
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	return NewState;
}

UEdGraphNode* UTDStateMachineTools::AddTransition(UEdGraphNode* FromState, UEdGraphNode* ToState)
{
	UAnimStateNodeBase* From = Cast<UAnimStateNodeBase>(FromState);
	UAnimStateNodeBase* To = Cast<UAnimStateNodeBase>(ToState);
	if (!From || !To || From->GetGraph() != To->GetGraph())
	{
		return nullptr;
	}

	UEdGraph* Graph = From->GetGraph();
	const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
	UEdGraphPin* OutPin = FindPin(From, EGPD_Output);
	UEdGraphPin* InPin = FindPin(To, EGPD_Input);
	if (!Schema || !OutPin || !InPin)
	{
		return nullptr;
	}

	TSet<UEdGraphNode*> Before;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		Before.Add(Node);
	}

	if (!Schema->TryCreateConnection(OutPin, InPin))
	{
		return nullptr;
	}

	UEdGraphNode* Created = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Before.Contains(Node) && Node->IsA<UAnimStateTransitionNode>())
		{
			Created = Node;
			break;
		}
	}

	if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph))
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	return Created;
}

UEdGraph* UTDStateMachineTools::GetBoundGraph(UEdGraphNode* StateOrTransitionNode)
{
	if (UAnimStateNode* State = Cast<UAnimStateNode>(StateOrTransitionNode))
	{
		return State->BoundGraph;
	}
	if (UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(StateOrTransitionNode))
	{
		return Transition->BoundGraph;
	}
	return nullptr;
}

bool UTDStateMachineTools::SetStateAnimation(UEdGraphNode* StateNode, UAnimationAsset* Asset)
{
	UAnimStateNode* State = Cast<UAnimStateNode>(StateNode);
	UEdGraph* Bound = State ? State->BoundGraph : nullptr;
	if (!Bound || !Asset)
	{
		return false;
	}

	UAnimGraphNode_StateResult* Result = nullptr;
	for (UEdGraphNode* Node : Bound->Nodes)
	{
		if (UAnimGraphNode_StateResult* Candidate = Cast<UAnimGraphNode_StateResult>(Node))
		{
			Result = Candidate;
			break;
		}
	}
	if (!Result)
	{
		return false;
	}

	UAnimGraphNode_SequencePlayer* Player = nullptr;
	{
		FGraphNodeCreator<UAnimGraphNode_SequencePlayer> Creator(*Bound);
		Player = Creator.CreateNode();
		Player->SetAnimationAsset(Asset);
		Player->NodePosX = Result->NodePosX - 320;
		Player->NodePosY = Result->NodePosY;
		Creator.Finalize();
	}

	UEdGraphPin* Pose = FindPin(Player, EGPD_Output);
	UEdGraphPin* Into = FindPin(Result, EGPD_Input);
	const UEdGraphSchema* Schema = Bound->GetSchema();
	if (!Pose || !Into || !Schema || !Schema->TryCreateConnection(Pose, Into))
	{
		return false;
	}

	if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Bound))
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	return true;
}

bool UTDStateMachineTools::SetTransitionRule(UEdGraphNode* TransitionNode, FName VariableName, bool bNegate)
{
	UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(TransitionNode);
	UEdGraph* Bound = Transition ? Transition->BoundGraph : nullptr;
	if (!Bound || VariableName == NAME_None)
	{
		return false;
	}

	UAnimGraphNode_TransitionResult* Result = nullptr;
	for (UEdGraphNode* Node : Bound->Nodes)
	{
		if (UAnimGraphNode_TransitionResult* Candidate = Cast<UAnimGraphNode_TransitionResult>(Node))
		{
			Result = Candidate;
			break;
		}
	}
	if (!Result)
	{
		return false;
	}

	UK2Node_VariableGet* Getter = nullptr;
	{
		FGraphNodeCreator<UK2Node_VariableGet> Creator(*Bound);
		Getter = Creator.CreateNode();
		Getter->VariableReference.SetSelfMember(VariableName);
		Getter->NodePosX = Result->NodePosX - (bNegate ? 520 : 300);
		Getter->NodePosY = Result->NodePosY;
		Creator.Finalize();
	}

	UEdGraphPin* Source = nullptr;
	for (UEdGraphPin* Pin : Getter->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && Pin->PinName == VariableName)
		{
			Source = Pin;
			break;
		}
	}
	if (!Source)
	{
		return false;
	}

	const UEdGraphSchema* Schema = Bound->GetSchema();
	if (!Schema)
	{
		return false;
	}

	if (bNegate)
	{
		UK2Node_CallFunction* Not = nullptr;
		{
			FGraphNodeCreator<UK2Node_CallFunction> Creator(*Bound);
			Not = Creator.CreateNode();
			Not->FunctionReference.SetExternalMember(
				GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Not_PreBool), UKismetMathLibrary::StaticClass());
			Not->NodePosX = Result->NodePosX - 260;
			Not->NodePosY = Result->NodePosY;
			Creator.Finalize();
		}
		UEdGraphPin* NotIn = Not->FindPin(TEXT("A"), EGPD_Input);
		UEdGraphPin* NotOut = Not->GetReturnValuePin();
		if (!NotIn || !NotOut || !Schema->TryCreateConnection(Source, NotIn))
		{
			return false;
		}
		Source = NotOut;
	}

	UEdGraphPin* Into = FindPin(Result, EGPD_Input);
	if (!Into || !Schema->TryCreateConnection(Source, Into))
	{
		return false;
	}

	if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Bound))
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	return true;
}
