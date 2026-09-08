#include "SSimCopterControllerHelp.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Game/SimCopterPlayerController.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

using namespace SimCopterControllerHelp;

namespace
{
FState ReadState(APawn* Pawn)
{
	FState State;
	if (!Pawn) return State;
	const auto* PC = Cast<ASimCopterPlayerController>(Pawn->GetController());
	State.bExpanded = PC && PC->IsControllerHelpExpanded();
	if (!State.bExpanded) return State;
	if (const auto* Helicopter = Cast<ASimCopterHelicopterPawn>(Pawn))
	{
		State.bHasPassengers = !Helicopter->GetMissionPassengerSlots().IsEmpty();
		State.bCanExit = Helicopter->CanExitHelicopter();
		const ESimCopterHelicopterTool Tool = Helicopter->GetActiveTool();
		State.bHasTool = Helicopter->IsToolAvailable(Tool);
		State.bRope = Tool == ESimCopterHelicopterTool::WaterBucket || Tool == ESimCopterHelicopterTool::RescueHarness;
		State.bMegaphone = Tool == ESimCopterHelicopterTool::Megaphone;
		State.Message = SimCopterHelicopterRegistry::GetMegaphoneMessageName(Helicopter->GetSelectedMegaphoneMessage());
		switch (Tool)
		{
		case ESimCopterHelicopterTool::WaterBucket: State.ToolAction = TEXT("Dump water bucket"); break;
		case ESimCopterHelicopterTool::WaterCannon: State.ToolAction = TEXT("Hold to spray water"); break;
		case ESimCopterHelicopterTool::TearGas: State.ToolAction = TEXT("Fire tear gas"); break;
		case ESimCopterHelicopterTool::ApacheMachineGun: State.ToolAction = TEXT("Hold to fire machine gun"); break;
		case ESimCopterHelicopterTool::ApacheMissile: State.ToolAction = TEXT("Fire missile"); break;
		default: State.ToolAction = TEXT("Use rescue harness"); break;
		}
		switch (Helicopter->GetControllerMode())
		{
		case ESimCopterControllerMode::ToolWheel: State.Context = EContext::ToolWheel; break;
		case ESimCopterControllerMode::DispatchWheel: State.Context = EContext::DispatchWheel; break;
		case ESimCopterControllerMode::PassengerSelect: State.Context = EContext::Passengers; break;
		case ESimCopterControllerMode::PassengerConfirm: State.Context = EContext::ConfirmPassenger; break;
		default: State.Context = Helicopter->IsControllerCameraAdjustHeld() ? EContext::Camera : EContext::Flight; break;
		}
	}
	else if (const auto* OnFoot = Cast<ASimCopterOnFootPawn>(Pawn))
	{
		State.Context = EContext::OnFoot;
		State.bCarryingPerson = OnFoot->IsCarryingMissionPerson();
		State.bCanBoard = OnFoot->CanBoardNearbyHelicopter();
	}
	return State;
}
}

TSharedRef<SWidget> SSimCopterControllerHelp::ForPawn(TWeakObjectPtr<APawn> Pawn, float Scale)
{
	return SNew(SSimCopterControllerHelp).Scale(Scale)
		.State_Lambda([Pawn] { return ReadState(Pawn.Get()); })
		.IconStyle_Lambda([Pawn]
		{
			const auto* PC = Pawn.IsValid() ? Cast<ASimCopterPlayerController>(Pawn->GetController()) : nullptr;
			return PC ? PC->GetControllerIconStyle() : EStyle::Xbox;
		})
		.Visibility_Lambda([Pawn]
		{
			const auto* PC = Pawn.IsValid() ? Cast<ASimCopterPlayerController>(Pawn->GetController()) : nullptr;
			return PC && PC->IsUsingGamepadInput() && !PC->IsPaused() && GEngine && GEngine->GameViewport &&
				!GEngine->GameViewport->IgnoreInput() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
		});
}

void SSimCopterControllerHelp::Construct(const FArguments& Args)
{
	State = Args._State;
	IconStyle = Args._IconStyle;
	Scale = Args._Scale;
	ChildSlot
	[
		SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FLinearColor(0.015f, 0.02f, 0.03f, 0.78f)).Padding(FMargin(10, 6) * Scale)
		[
			SAssignNew(Rows, SVerticalBox)
		]
	];
	Refresh();
}

const FSlateBrush* SSimCopterControllerHelp::GetIcon(const FString& Name)
{
	const FString Key = FString(IconStyle.Get() == EStyle::PlayStation ? TEXT("playstation_") : TEXT("xbox_")) + Name;
	TSharedPtr<FSlateDynamicImageBrush>& Brush = Icons.FindOrAdd(Key);
	if (!Brush)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Slate/ControllerIcons") / (Key + TEXT(".png")));
		Brush = MakeShared<FSlateDynamicImageBrush>(FName(*Path), FVector2D(32, 32));
	}
	return Brush.Get();
}

void SSimCopterControllerHelp::Refresh()
{
	const TArray<FPrompt> Prompts = BuildPrompts(State.Get());
	FString Layout = FString::FromInt(static_cast<int32>(IconStyle.Get()));
	for (const FPrompt& Prompt : Prompts) Layout += TEXT("|") + Prompt.Icon + TEXT(":") + Prompt.Label;
	if (Layout == PreviousLayout) return;
	PreviousLayout = MoveTemp(Layout);
	Rows->ClearChildren();
	for (const FPrompt& Prompt : Prompts)
	{
		const bool bToggle = Prompt.Icon == TEXT("l3");
		Rows->AddSlot().AutoHeight().Padding(0, bToggle && Prompts.Num() > 1 ? 6 * Scale : 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(32 * Scale).HeightOverride(32 * Scale)
				[SNew(SImage).Image(GetIcon(Prompt.Icon))]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8 * Scale, 0)
			[
				SNew(STextBlock).Text(FText::FromString(Prompt.Label))
				.Font(FCoreStyle::GetDefaultFontStyle(bToggle ? TEXT("Bold") : TEXT("Regular"), FMath::RoundToInt(12 * Scale)))
				.ColorAndOpacity(bToggle ? FLinearColor(1, 0.83f, 0.42f) : FLinearColor::White)
			]
		];
	}
}

void SSimCopterControllerHelp::Tick(const FGeometry& Geometry, double Time, float Delta)
{
	SCompoundWidget::Tick(Geometry, Time, Delta);
	Refresh();
}
