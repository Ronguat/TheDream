#include "TDStateMachineTools.h"

#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
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
