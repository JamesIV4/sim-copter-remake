#include "UI/SimCopterControllerHelp.h"

namespace SimCopterControllerHelp
{
EStyle DetectStyle(const FString& Identity)
{
	for (const TCHAR* Name : {TEXT("PlayStation"), TEXT("DualShock"), TEXT("DualSense"), TEXT("Sony"),
		TEXT("PS4"), TEXT("PS5"), TEXT("VID_054C"), TEXT("VID:054C")})
		if (Identity.Contains(Name, ESearchCase::IgnoreCase)) return EStyle::PlayStation;
	// XInput translation can conceal the physical brand; use the layout exposed by the driver.
	return EStyle::Xbox;
}

TArray<FPrompt> BuildPrompts(const FState& State)
{
	TArray<FPrompt> Rows;
	const auto Add = [&Rows](const TCHAR* Icon, FString Label) { Rows.Add({Icon, MoveTemp(Label)}); };
	if (State.bExpanded)
	{
		switch (State.Context)
		{
		case EContext::Camera:
			Add(TEXT("rs"), TEXT("Zoom camera"));
			Add(TEXT("x"), TEXT("Toggle spotlight"));
			Add(TEXT("r3"), TEXT("Release to return to tools"));
			break;
		case EContext::ToolWheel:
			Add(TEXT("rs"), TEXT("Select tool"));
			Add(TEXT("lb"), TEXT("Release to equip"));
			Add(TEXT("b"), TEXT("Cancel"));
			if (State.bHasPassengers) Add(TEXT("x"), TEXT("Select passenger"));
			break;
		case EContext::DispatchWheel:
			Add(TEXT("rs"), TEXT("Select service"));
			Add(TEXT("rb"), TEXT("Release to dispatch"));
			Add(TEXT("b"), TEXT("Cancel"));
			Add(TEXT("y"), TEXT("Recall all"));
			break;
		case EContext::Passengers:
			Add(TEXT("horizontal"), TEXT("Select passenger"));
			if (State.bHasPassengers) Add(TEXT("a"), TEXT("Choose drop action"));
			Add(TEXT("b"), TEXT("Back"));
			break;
		case EContext::ConfirmPassenger:
			Add(TEXT("horizontal"), TEXT("Drop / Cancel"));
			Add(TEXT("a"), TEXT("Confirm"));
			Add(TEXT("b"), TEXT("Back"));
			break;
		case EContext::OnFoot:
			Add(TEXT("a"), TEXT("Jump"));
			if (State.bCanBoard) Add(TEXT("y"), TEXT("Enter helicopter"));
			if (State.bCarryingPerson) Add(TEXT("x"), TEXT("Put down passenger"));
			break;
		default:
			if (State.bHasTool)
			{
				Add(TEXT("x"), State.bMegaphone ? FString(TEXT("Broadcast: ")) + State.Message : State.ToolAction);
				if (State.bMegaphone) Add(TEXT("horizontal"), TEXT("Change message"));
				else if (State.bRope) Add(TEXT("vertical"), TEXT("Raise / lower"));
				Add(TEXT("lb"), TEXT("Hold to change tool"));
			}
			Add(TEXT("rb"), TEXT("Hold to dispatch"));
			if (State.bCanExit) Add(TEXT("y"), TEXT("Exit helicopter"));
			break;
		}
	}
	// Always the final row: collapsing help never hides the way to bring it back.
	Add(TEXT("l3"), State.bExpanded ? TEXT("Hide help") : TEXT("Show help"));
	return Rows;
}
}
