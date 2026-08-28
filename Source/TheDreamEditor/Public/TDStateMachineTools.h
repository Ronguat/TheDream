#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TDStateMachineTools.generated.h"

class UAnimationAsset;
class UEdGraph;
class UEdGraphNode;

/**
 *  Editor-only scripting surface for animation state machines, which no other route reaches.
 *
 *  Reflection sees a state machine graph's `Nodes` as protected, and the MCP layer's node tools
 *  resolve a graph's owning Blueprint by checking its immediate outer -- which for a state machine
 *  graph is an AnimGraphNode, so they fail. Both walls are C++-side only: `UEdGraph::Nodes` is
 *  public to C++, and `FBlueprintEditorUtils::FindBlueprintForGraph` walks the whole outer chain.
 */
UCLASS()
class THEDREAMEDITOR_API UTDStateMachineTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Every node in a graph, including the interiors reflection cannot enumerate. */
	UFUNCTION(BlueprintCallable, Category="TheDream|StateMachine")
	static TArray<UEdGraphNode*> GetGraphNodes(UEdGraph* Graph);

	/**
	 *  Adds a state to a state machine graph and names it. Returns the new AnimStateNode.
	 *  The state's own graph and its result node are created by the schema action, as they are
	 *  when a designer adds a state by hand.
	 */
	UFUNCTION(BlueprintCallable, Category="TheDream|StateMachine")
	static UEdGraphNode* AddState(UEdGraph* StateMachineGraph, FName StateName, FIntPoint Position);

	/**
	 *  Connects two states, which is what makes the schema spawn a transition between them.
	 *  Returns the transition node, found by diffing the graph's nodes across the connection.
	 */
	UFUNCTION(BlueprintCallable, Category="TheDream|StateMachine")
	static UEdGraphNode* AddTransition(UEdGraphNode* FromState, UEdGraphNode* ToState);

	/**
	 *  Puts a sequence player into a state and wires it to the state result, which is what makes
	 *  the state play something rather than hold a blank pose.
	 */
	UFUNCTION(BlueprintCallable, Category="TheDream|StateMachine")
	static bool SetStateAnimation(UEdGraphNode* StateNode, UAnimationAsset* Asset);

	/**
	 *  Authors a transition rule as a single bool member variable of the AnimBlueprint, optionally
	 *  negated -- the shape every hand-authored rule in this project uses.
	 */
	UFUNCTION(BlueprintCallable, Category="TheDream|StateMachine")
	static bool SetTransitionRule(UEdGraphNode* TransitionNode, FName VariableName, bool bNegate);

	/** The graph a state or transition node owns, which holds its pose or its rule. */
	UFUNCTION(BlueprintCallable, Category="TheDream|StateMachine")
	static UEdGraph* GetBoundGraph(UEdGraphNode* StateOrTransitionNode);

	/**
	 *  Binds an anim graph node's **On Update** to a function, including a static one on a
	 *  BlueprintFunctionLibrary -- `UAnimGraphNode_Base`'s function properties carry
	 *  `AllowFunctionLibraries`.
	 *
	 *  The reference is what gets saved; the runtime node is resolved from it during compilation,
	 *  so the caller must recompile the AnimBlueprint for the binding to take effect. Fails if the
	 *  named function is absent from OwnerClass, rather than writing a reference that will not
	 *  resolve.
	 *
	 *  Exists because `FMemberReference`'s members are `SaveGame` UPROPERTYs and private, so no
	 *  reflection route reaches them; `SetExternalMember` is public C++.
	 */
	UFUNCTION(BlueprintCallable, Category="TheDream|StateMachine")
	static bool SetNodeUpdateFunction(UEdGraphNode* AnimNode, UClass* OwnerClass, FName FunctionName);

	/**
	 *  Marks an anim node property **Always Dynamic**, which is what permits a runtime setter such
	 *  as `USequencePlayerLibrary::SetPlayRate` to write it. Without the mark the write is refused
	 *  and logs *"value is not dynamic"* every frame while the node is relevant, leaving the
	 *  authored value in place.
	 *
	 *  Exists because `AlwaysDynamicProperties` is a bare `UPROPERTY()` -- public to C++, invisible
	 *  to reflection -- so no Python or MCP route reaches it.
	 */
	UFUNCTION(BlueprintCallable, Category="TheDream|StateMachine")
	static bool SetAnimNodePropertyAlwaysDynamic(UEdGraphNode* AnimNode, FName PropertyName);
};
