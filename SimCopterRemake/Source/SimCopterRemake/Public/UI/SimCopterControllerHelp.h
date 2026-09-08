#pragma once
#include "CoreMinimal.h"

namespace SimCopterControllerHelp
{
enum class EStyle : uint8 { Xbox, PlayStation };
enum class EContext : uint8 { Flight, Camera, ToolWheel, DispatchWheel, Passengers, ConfirmPassenger, OnFoot };
struct FState
{
	EContext Context = EContext::Flight;
	bool bExpanded = false;
	bool bHasTool = false;
	bool bCanExit = false;
	bool bHasPassengers = false;
	bool bCanBoard = false;
	bool bCarryingPerson = false;
	bool bRope = false;
	bool bMegaphone = false;
	FString ToolAction;
	FString Message;
};
struct FPrompt
{
	FString Icon;
	FString Label;
};
// Hardware identity belongs to the device producing the event, never an arbitrary attached pad.
SIMCOPTERREMAKE_API EStyle DetectStyle(const FString& HardwareIdentity);
SIMCOPTERREMAKE_API TArray<FPrompt> BuildPrompts(const FState& State);
}
