// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterToolFlaps.h"

#include "Brushes/SlateColorBrush.h"
#include "Dom/JsonObject.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Ground/SimCopterDispatch.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
using namespace SimCopterFlapLayout;

// Lettering. The tool flaps carry their own painted labels; the dispatch strip is the remake's
// own, so it borrows the hangar shell's label colour.
const FLinearColor FlapLabel(0.94f, 0.94f, 0.90f, 1.0f);
const FLinearColor FlapReadout(1.0f, 0.86f, 0.42f, 1.0f);

// Text sizes, in screen pixels. They are deliberately independent of the art scale.
constexpr int32 ReadoutFontSize = 13;
constexpr int32 LabelFontSize = 11;

// The first three entries deliberately retain EService's order. Chase is an action on the
// police pool, not a fourth service, so it lives one slot past EService::Count and resolves back
// to Police when the player dispatches or clears it.
constexpr int32 PoliceChaseDispatchEntry = static_cast<int32>(SimCopterDispatch::EService::Count);
constexpr int32 DispatchEntryCount = PoliceChaseDispatchEntry + 1;

// Screen pixels between stacked panels. Matches the inset the pawn gives the whole column, so
// the gap above the first panel and the gaps between them read the same.
constexpr float PanelGapPixels = 12.0f;

FSlateFontInfo FlapFont(const int32 Size, const bool bBold = true);

class FSimCopterFlapCalibrationInputPreProcessor : public IInputProcessor
{
public:
	FSimCopterFlapCalibrationInputPreProcessor(TWeakPtr<SSimCopterToolFlaps> InFlapsPanel)
		: FlapsPanel(InFlapsPanel)
	{}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.GetKey() == EKeys::M && InKeyEvent.IsControlDown() && InKeyEvent.IsAltDown())
		{
			if (TSharedPtr<SSimCopterToolFlaps> Flaps = FlapsPanel.Pin())
			{
				Flaps->ToggleCalibrationMode();
				return true;
			}
		}
		return false;
	}

private:
	TWeakPtr<SSimCopterToolFlaps> FlapsPanel;
};

class SSimCopterCalibratableBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterCalibratableBox) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(FString, ElementKey)
		SLATE_ARGUMENT(SSimCopterToolFlaps*, OwnerFlaps)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ElementKey = InArgs._ElementKey;
		OwnerFlaps = InArgs._OwnerFlaps;

		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		int32 MaxLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

		if (OwnerFlaps != nullptr && OwnerFlaps->IsCalibrationMode())
		{
			MaxLayer++;
			const bool bIsSelected = (OwnerFlaps->GetSelectedCalibrationKey() == ElementKey);
			const FLinearColor OutlineColor = bDragging
				? FLinearColor(1.0f, 0.2f, 0.2f, 1.0f)
				: (bIsSelected ? FLinearColor(1.0f, 0.85f, 0.0f, 1.0f) : FLinearColor(0.0f, 1.0f, 1.0f, 0.9f));

			const FLinearColor FillColor = bIsSelected
				? FLinearColor(1.0f, 0.85f, 0.0f, 0.30f)
				: FLinearColor(0.0f, 0.8f, 1.0f, 0.20f);

			static const FSlateColorBrush FillBrush(FLinearColor::White);
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				MaxLayer,
				AllottedGeometry.ToPaintGeometry(),
				&FillBrush,
				ESlateDrawEffect::None,
				FillColor);

			TArray<FVector2D> Points;
			const FVector2D Size = AllottedGeometry.GetLocalSize();
			Points.Add(FVector2D(0.0f, 0.0f));
			Points.Add(FVector2D(Size.X, 0.0f));
			Points.Add(FVector2D(Size.X, Size.Y));
			Points.Add(FVector2D(0.0f, Size.Y));
			Points.Add(FVector2D(0.0f, 0.0f));

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				MaxLayer + 1,
				AllottedGeometry.ToPaintGeometry(),
				Points,
				ESlateDrawEffect::None,
				OutlineColor,
				true,
				bIsSelected ? 2.5f : 1.5f);

			if (bIsSelected && OwnerFlaps != nullptr)
			{
				const FVector2D Offset = OwnerFlaps->GetElementOffset(ElementKey);
				const FVector2D ElemScale = OwnerFlaps->GetElementScale(ElementKey);
				const FString InfoText = FString::Printf(TEXT("%s\nPos:(%.1f, %.1f) Scale:(%.2f, %.2f)"),
					*ElementKey, Offset.X, Offset.Y, ElemScale.X, ElemScale.Y);

				FSlateDrawElement::MakeText(
					OutDrawElements,
					MaxLayer + 2,
					AllottedGeometry.ToPaintGeometry(FVector2D(1.0f, 1.0f), FSlateLayoutTransform(FVector2D(2.0f, 2.0f))),
					InfoText,
					FlapFont(9, true),
					ESlateDrawEffect::None,
					FLinearColor(1.0f, 1.0f, 0.0f, 1.0f));
				MaxLayer++;
			}

			MaxLayer += 2;
		}

		return MaxLayer;
	}

	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (OwnerFlaps != nullptr && OwnerFlaps->IsCalibrationMode() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			OwnerFlaps->SetSelectedCalibrationKey(ElementKey);
			bDragging = true;
			DragStartMouse = MouseEvent.GetScreenSpacePosition();
			InitialOffset = OwnerFlaps->GetElementOffset(ElementKey);
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bDragging && HasMouseCapture() && OwnerFlaps != nullptr)
		{
			const FVector2D CurrentMouse = MouseEvent.GetScreenSpacePosition();
			const FVector2D DeltaScreen = CurrentMouse - DragStartMouse;
			const FVector2D DeltaPage = DeltaScreen / FMath::Max(0.1f, OwnerFlaps->GetScale());
			OwnerFlaps->SetElementOffset(ElementKey, InitialOffset + DeltaPage);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bDragging && HasMouseCapture())
		{
			bDragging = false;
			if (OwnerFlaps != nullptr)
			{
				OwnerFlaps->SaveCalibrationData();
			}
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (OwnerFlaps != nullptr && OwnerFlaps->IsCalibrationMode())
		{
			const float WheelDelta = MouseEvent.GetWheelDelta();
			const float Factor = (WheelDelta > 0.0f) ? 1.05f : 0.95f;
			FVector2D ElementScale = OwnerFlaps->GetElementScale(ElementKey);
			if (MouseEvent.IsShiftDown())
			{
				ElementScale.Y *= Factor;
			}
			else if (MouseEvent.IsControlDown())
			{
				ElementScale.X *= Factor;
			}
			else
			{
				ElementScale *= Factor;
			}
			OwnerFlaps->SetElementScale(ElementKey, ElementScale);
			OwnerFlaps->SaveCalibrationData();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	FString ElementKey;
	SSimCopterToolFlaps* OwnerFlaps = nullptr;
	bool bDragging = false;
	FVector2D DragStartMouse = FVector2D::ZeroVector;
	FVector2D InitialOffset = FVector2D::ZeroVector;
};

// The dispatch strip's background is a tool flap's frame taken apart: the left edge, a slice of
// bare grid tiled out to width, then the right edge with its dash corner. flap2 is the donor
// because its rope artwork leaves both of those clear.
const TCHAR* const DispatchFrameFile = TEXT("FLAP2.BMP");
const FIntRect DispatchFrameLeft(0, 0, 12, PageHeight);
const FIntRect DispatchFrameFill(12, 0, 18, PageHeight);
const FIntRect DispatchFrameRight(98, 0, PageWidth, PageHeight);

// The rocker, cut in half so each arrowhead is its own sprite. Turned on its side these are the
// dispatch strip's left and right arrows: the original ships no horizontal arrow.
//
// Both arrows come from the *lower* half. It is the half whose outer edge is inside the frame, so
// it carries a finished border where the upper half's is cut off by the sheet. Turning the one
// good half clockwise points it left and anticlockwise points it right, which gives a matched
// pair rather than two halves that do not look alike.
const TCHAR* const RockerFile = TEXT("FLAPBTN0.BMP");
const FIntRect RockerArrowNormal(0, 14, 17, 29);
const FIntRect RockerArrowPressed(17, 14, 34, 29);

// Turned on its side the 17x15 half becomes 15x17.
constexpr int32 RockerArrowWidth = 15;
constexpr int32 RockerArrowHeight = 17;

const TCHAR* const OctagonFile = TEXT("FLAPBTN1.BMP");
const FIntRect OctagonNormal(0, 0, 17, 24);
const FIntRect OctagonPressed(17, 0, 34, 24);

FSlateFontInfo FlapFont(const int32 Size, const bool bBold)
{
	return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
}

const TCHAR* GetDispatchServiceLabel(const int32 ServiceIndex)
{
	if (ServiceIndex == PoliceChaseDispatchEntry)
	{
		return TEXT("POLICE (CHASE)");
	}

	switch (static_cast<SimCopterDispatch::EService>(ServiceIndex))
	{
	case SimCopterDispatch::EService::FireTruck: return TEXT("FIRE TRUCK");
	case SimCopterDispatch::EService::Police: return TEXT("POLICE");
	case SimCopterDispatch::EService::Ambulance: return TEXT("AMBULANCE");
	default: return TEXT("-");
	}
}

bool ResolveDispatchEntry(
	const int32 EntryIndex,
	int32& OutServiceIndex,
	bool& bOutChase)
{
	if (EntryIndex == PoliceChaseDispatchEntry)
	{
		OutServiceIndex = static_cast<int32>(SimCopterDispatch::EService::Police);
		bOutChase = true;
		return true;
	}

	if (EntryIndex < 0 || EntryIndex >= static_cast<int32>(SimCopterDispatch::EService::Count))
	{
		return false;
	}

	OutServiceIndex = EntryIndex;
	bOutChase = false;
	return true;
}
}

void SSimCopterToolFlaps::Construct(const FArguments& InArgs)
{
	Pawn = InArgs._Pawn;
	Art = InArgs._Art;
	Scale = FMath::Max(0.5f, InArgs._Scale);
	SelectedDispatchEntry = Pawn.IsValid()
		? FMath::Clamp(Pawn->GetSelectedDispatchService(), 0, PoliceChaseDispatchEntry - 1)
		: 0;

	LoadCalibrationData();

	if (FSlateApplication::IsInitialized())
	{
		TSharedRef<FSimCopterFlapCalibrationInputPreProcessor> PreProcessor =
			MakeShared<FSimCopterFlapCalibrationInputPreProcessor>(SharedThis(this));
		CalibrationInputProcessor = PreProcessor;
		FSlateApplication::Get().RegisterInputPreProcessor(PreProcessor);
	}

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	MissionMarkerAvoidancePanels.Reset();

	const FMargin PanelGap(0.0f, 0.0f, 0.0f, PanelGapPixels);

	TSharedRef<SWidget> DispatchPanel = BuildDispatchFlap();
	MissionMarkerAvoidancePanels.Add(DispatchPanel);
	Column->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(PanelGap)
		[
			DispatchPanel
		];

	// The Apache's armament strip sits with the tool flaps. It collapses on every other airframe,
	// so the column is unchanged for the eight civilian models.
	TSharedRef<SWidget> ApachePanel = BuildApacheFlap();
	MissionMarkerAvoidancePanels.Add(ApachePanel);
	Column->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(PanelGap)
		[
			ApachePanel
		];

	for (const FFlap& Flap : GetFlaps())
	{
		TSharedRef<SWidget> ToolPanel = BuildToolFlap(Flap);
		MissionMarkerAvoidancePanels.Add(ToolPanel);
		Column->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(PanelGap)
			[
				ToolPanel
			];
	}

	// Nothing but the flap column: the calibration panel is hosted separately, top-middle of the
	// screen, because this widget is one flap wide and pinned to the right edge.
	ChildSlot
	[
		Column
	];
}

SSimCopterToolFlaps::~SSimCopterToolFlaps()
{
	if (CalibrationInputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(CalibrationInputProcessor);
		CalibrationInputProcessor.Reset();
	}
}

void SSimCopterToolFlaps::ToggleCalibrationMode()
{
	bCalibrationMode = !bCalibrationMode;
	UE_LOG(LogTemp, Log, TEXT("SimCopter Tool Flap Calibration Mode: %s"), bCalibrationMode ? TEXT("ON") : TEXT("OFF"));

	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		if (APlayerController* PC = Cast<APlayerController>(Helicopter->GetController()))
		{
			if (bCalibrationMode)
			{
				PC->bShowMouseCursor = true;
				FInputModeGameAndUI InputMode;
				InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
				InputMode.SetHideCursorDuringCapture(false);
				PC->SetInputMode(InputMode);
			}
			else
			{
				Helicopter->RestoreGameViewportFocus();
			}
		}

		if (!bCalibrationMode && (!Art.IsValid() || !Art->IsUsable()))
		{
			Helicopter->RemoveToolFlapsWidget();
			return;
		}
	}

	Invalidate(EInvalidateWidgetReason::Paint | EInvalidateWidgetReason::Layout);
}

FReply SSimCopterToolFlaps::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.IsControlDown() && InKeyEvent.IsAltDown() && InKeyEvent.GetKey() == EKeys::M)
	{
		ToggleCalibrationMode();
		return FReply::Handled();
	}
	return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

FReply SSimCopterToolFlaps::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.IsControlDown() && InKeyEvent.IsAltDown() && InKeyEvent.GetKey() == EKeys::M)
	{
		ToggleCalibrationMode();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FVector2D SSimCopterToolFlaps::GetAuthoritativeDefaultOffset(const FString& Key)
{
	if (Key.Equals(TEXT("Button_Megaphone"), ESearchCase::IgnoreCase))
	{
		return FVector2D(0.0f, -0.56551747570433109f);
	}
	if (Key.Equals(TEXT("Button_Raise harness"), ESearchCase::IgnoreCase))
	{
		return FVector2D(1.6965524271129933f, 0.0f);
	}
	if (Key.Equals(TEXT("Button_Lower harness"), ESearchCase::IgnoreCase))
	{
		return FVector2D(1.6965524271129933f, 0.56551747570433109f);
	}
	if (Key.Equals(TEXT("Button_Raise bucket"), ESearchCase::IgnoreCase))
	{
		return FVector2D(4.3586223358907823f, 0.0f);
	}
	if (Key.Equals(TEXT("Button_Lower bucket"), ESearchCase::IgnoreCase))
	{
		return FVector2D(4.3586223358907823f, 0.20000000298023224f);
	}
	if (Key.Equals(TEXT("Button_Dump bucket"), ESearchCase::IgnoreCase))
	{
		return FVector2D(3.3931048542259865f, 0.0f);
	}
	if (Key.Equals(TEXT("Button_Water cannon"), ESearchCase::IgnoreCase))
	{
		return FVector2D(1.1310349514086622f, 0.0f);
	}
	if (Key.Equals(TEXT("Button_Fire tear gas"), ESearchCase::IgnoreCase))
	{
		return FVector2D(2.2620699028173243f, 0.0f);
	}
	if (Key.Equals(TEXT("CanisterLamp_0"), ESearchCase::IgnoreCase))
	{
		return FVector2D(0.20000000298023224f, 0.0f);
	}
	if (Key.Equals(TEXT("CanisterLamp_1"), ESearchCase::IgnoreCase))
	{
		return FVector2D(0.46551747421421497f, 0.0f);
	}
	if (Key.Equals(TEXT("CanisterLamp_2"), ESearchCase::IgnoreCase))
	{
		return FVector2D(0.56551747570433109f, 0.0f);
	}
	if (Key.Equals(TEXT("CanisterLamp_3"), ESearchCase::IgnoreCase))
	{
		return FVector2D(0.63103494395808157f, 0.0f);
	}
	if (Key.Equals(TEXT("CanisterLamp_4"), ESearchCase::IgnoreCase))
	{
		return FVector2D(1.0310349499185461f, 0.0f);
	}
	if (Key.Equals(TEXT("CanisterLamp_5"), ESearchCase::IgnoreCase) ||
		Key.Equals(TEXT("CanisterLamp_6"), ESearchCase::IgnoreCase))
	{
		return FVector2D(0.40000000596046448f, -0.20000000298023224f);
	}
	if (Key.Equals(TEXT("CanisterLamp_7"), ESearchCase::IgnoreCase))
	{
		return FVector2D(0.19999998807907104f, -0.20000000298023224f);
	}
	if (Key.Equals(TEXT("CanisterLamp_8"), ESearchCase::IgnoreCase))
	{
		return FVector2D(0.69999999552965164f, -0.20000000298023224f);
	}
	if (Key.Equals(TEXT("CanisterLamp_9"), ESearchCase::IgnoreCase))
	{
		return FVector2D(0.79999999701976776f, -0.20000000298023224f);
	}
	return FVector2D::ZeroVector;
}

FVector2D SSimCopterToolFlaps::GetAuthoritativeDefaultScale(const FString& Key)
{
	if (Key.StartsWith(TEXT("CanisterLamp_"), ESearchCase::IgnoreCase))
	{
		return FVector2D(1.1000000238418579f, 1.1000000238418579f);
	}
	if (Key.Equals(TEXT("Dispatch_PrevService"), ESearchCase::IgnoreCase) ||
		Key.Equals(TEXT("Dispatch_NextService"), ESearchCase::IgnoreCase))
	{
		return FVector2D(1.0f, 0.79999995231628418f);
	}
	return FVector2D(1.0f, 1.0f);
}

FVector2D SSimCopterToolFlaps::GetElementOffset(const FString& Key) const
{
	const FVector2D* Found = CalibrationOffsets.Find(Key);
	return Found != nullptr ? *Found : GetAuthoritativeDefaultOffset(Key);
}

void SSimCopterToolFlaps::SetElementOffset(const FString& Key, const FVector2D& Offset)
{
	CalibrationOffsets.Add(Key, Offset);

	if (const FVector4f* Bounds = ElementDefaultBounds.Find(Key))
	{
		if (SConstraintCanvas::FSlot** SlotPtr = ElementCanvasSlots.Find(Key))
		{
			if (*SlotPtr != nullptr)
			{
				const FVector2D ElementScale = GetElementScale(Key);
				const float PageX = Bounds->X + Offset.X;
				const float PageY = Bounds->Y + Offset.Y;
				const float PageW = Bounds->Z * ElementScale.X;
				const float PageH = Bounds->W * ElementScale.Y;
				(*SlotPtr)->SetOffset(FMargin(PageX * Scale, PageY * Scale, PageW * Scale, PageH * Scale));
			}
		}
	}

	SaveCalibrationData();
}

FVector2D SSimCopterToolFlaps::GetElementScale(const FString& Key) const
{
	const FVector2D* Found = CalibrationScales.Find(Key);
	return Found != nullptr ? *Found : GetAuthoritativeDefaultScale(Key);
}

void SSimCopterToolFlaps::SetElementScale(const FString& Key, const FVector2D& ElementScale)
{
	CalibrationScales.Add(Key, ElementScale);

	if (const FVector4f* Bounds = ElementDefaultBounds.Find(Key))
	{
		if (SConstraintCanvas::FSlot** SlotPtr = ElementCanvasSlots.Find(Key))
		{
			if (*SlotPtr != nullptr)
			{
				const FVector2D Offset = GetElementOffset(Key);
				const float PageX = Bounds->X + Offset.X;
				const float PageY = Bounds->Y + Offset.Y;
				const float PageW = Bounds->Z * ElementScale.X;
				const float PageH = Bounds->W * ElementScale.Y;
				(*SlotPtr)->SetOffset(FMargin(PageX * Scale, PageY * Scale, PageW * Scale, PageH * Scale));
			}
		}
	}

	SaveCalibrationData();
}

void SSimCopterToolFlaps::SetScale(const float NewScale)
{
	Scale = FMath::Max(0.5f, NewScale);
	for (const auto& Pair : ElementDefaultBounds)
	{
		SetElementOffset(Pair.Key, GetElementOffset(Pair.Key));
	}
	SaveCalibrationData();
}

void SSimCopterToolFlaps::SetSelectedCalibrationKey(const FString& Key)
{
	SelectedCalibrationKey = Key;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SSimCopterToolFlaps::CycleSelectedCalibrationKey(int32 Direction)
{
	TArray<FString> Keys;
	ElementDefaultBounds.GenerateKeyArray(Keys);
	if (Keys.Num() == 0)
	{
		return;
	}

	int32 CurrentIndex = Keys.IndexOfByKey(SelectedCalibrationKey);
	if (CurrentIndex == INDEX_NONE)
	{
		CurrentIndex = 0;
	}
	else
	{
		CurrentIndex = (CurrentIndex + Direction + Keys.Num()) % Keys.Num();
	}
	SetSelectedCalibrationKey(Keys[CurrentIndex]);
}

void SSimCopterToolFlaps::NudgeSelectedElement(float DeltaX, float DeltaY, float DeltaScaleX, float DeltaScaleY)
{
	if (SelectedCalibrationKey.IsEmpty())
	{
		CycleSelectedCalibrationKey(1);
	}
	if (SelectedCalibrationKey.IsEmpty())
	{
		return;
	}

	if (DeltaX != 0.0f || DeltaY != 0.0f)
	{
		const FVector2D CurrentOffset = GetElementOffset(SelectedCalibrationKey);
		SetElementOffset(SelectedCalibrationKey, CurrentOffset + FVector2D(DeltaX, DeltaY));
	}
	if (DeltaScaleX != 0.0f || DeltaScaleY != 0.0f)
	{
		const FVector2D CurrentScale = GetElementScale(SelectedCalibrationKey);
		const float NewSx = FMath::Max(0.1f, CurrentScale.X + DeltaScaleX);
		const float NewSy = FMath::Max(0.1f, CurrentScale.Y + DeltaScaleY);
		SetElementScale(SelectedCalibrationKey, FVector2D(NewSx, NewSy));
	}
	SaveCalibrationData();
}

void SSimCopterToolFlaps::ResetSelectedElement()
{
	if (SelectedCalibrationKey.IsEmpty())
	{
		return;
	}
	SetElementOffset(SelectedCalibrationKey, GetAuthoritativeDefaultOffset(SelectedCalibrationKey));
	SetElementScale(SelectedCalibrationKey, GetAuthoritativeDefaultScale(SelectedCalibrationKey));
	SaveCalibrationData();
}

FText SSimCopterToolFlaps::GetSelectedControlText() const
{
	if (SelectedCalibrationKey.IsEmpty())
	{
		return FText::FromString(TEXT("None Selected (Click or < / >)"));
	}
	return FText::FromString(SelectedCalibrationKey);
}

FText SSimCopterToolFlaps::GetSelectedXText() const
{
	if (SelectedCalibrationKey.IsEmpty()) return FText::FromString(TEXT("X: 0"));
	const FVector2D Offset = GetElementOffset(SelectedCalibrationKey);
	return FText::FromString(FString::Printf(TEXT("X: %+.1f"), Offset.X));
}

FText SSimCopterToolFlaps::GetSelectedYText() const
{
	if (SelectedCalibrationKey.IsEmpty()) return FText::FromString(TEXT("Y: 0"));
	const FVector2D Offset = GetElementOffset(SelectedCalibrationKey);
	return FText::FromString(FString::Printf(TEXT("Y: %+.1f"), Offset.Y));
}

FText SSimCopterToolFlaps::GetSelectedScaleXText() const
{
	if (SelectedCalibrationKey.IsEmpty()) return FText::FromString(TEXT("ScaleX: 1.00"));
	const FVector2D ElementScale = GetElementScale(SelectedCalibrationKey);
	return FText::FromString(FString::Printf(TEXT("ScaleX: %.2f"), ElementScale.X));
}

FText SSimCopterToolFlaps::GetSelectedScaleYText() const
{
	if (SelectedCalibrationKey.IsEmpty()) return FText::FromString(TEXT("ScaleY: 1.00"));
	const FVector2D ElementScale = GetElementScale(SelectedCalibrationKey);
	return FText::FromString(FString::Printf(TEXT("ScaleY: %.2f"), ElementScale.Y));
}

FText SSimCopterToolFlaps::GetGlobalScaleText() const
{
	return FText::FromString(FString::Printf(TEXT("Flap Scale: %.2f"), Scale));
}

TSharedRef<SWidget> SSimCopterToolFlaps::BuildCalibrationDebugPanel()
{
	const FSlateFontInfo Font = FlapFont(11, true);
	const FSlateFontInfo SmallFont = FlapFont(10, false);
	const FMargin ButtonPadding(4.0f, 2.0f);

	auto MakeNudgeBtn = [this, SmallFont, ButtonPadding](const FString& Label, float DX, float DY, float DSX, float DSY)
	{
		return SNew(SButton)
			.IsFocusable(false)
			.ContentPadding(ButtonPadding)
			.OnClicked_Lambda([this, DX, DY, DSX, DSY]() {
				NudgeSelectedElement(DX, DY, DSX, DSY);
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(FText::FromString(Label)).Font(SmallFont)
			];
	};

	TSharedRef<SVerticalBox> PanelContent = SNew(SVerticalBox);

	// Header
	PanelContent->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(TEXT("FLAP CALIBRATION CONTROL PANEL")))
		.Font(Font)
		.ColorAndOpacity(FLinearColor(0.0f, 1.0f, 1.0f, 1.0f))
	];

	// Control Selection Row
	PanelContent->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SButton)
			.IsFocusable(false)
			.ContentPadding(ButtonPadding)
			.OnClicked_Lambda([this]() { CycleSelectedCalibrationKey(-1); return FReply::Handled(); })
			[ SNew(STextBlock).Text(FText::FromString(TEXT("< Prev"))).Font(SmallFont) ]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(TAttribute<FText>::CreateSP(this, &SSimCopterToolFlaps::GetSelectedControlText))
			.Font(SmallFont)
			.ColorAndOpacity(FLinearColor(1.0f, 0.85f, 0.0f, 1.0f))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.IsFocusable(false)
			.ContentPadding(ButtonPadding)
			.OnClicked_Lambda([this]() { CycleSelectedCalibrationKey(1); return FReply::Handled(); })
			[ SNew(STextBlock).Text(FText::FromString(TEXT("Next >"))).Font(SmallFont) ]
		]
	];

	// Position Nudge Row X
	PanelContent->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[ SNew(STextBlock).Text(TAttribute<FText>::CreateSP(this, &SSimCopterToolFlaps::GetSelectedXText)).Font(SmallFont) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("-1"), -1.0f, 0.0f, 0.0f, 0.0f) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("-0.1"), -0.1f, 0.0f, 0.0f, 0.0f) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("+0.1"), 0.1f, 0.0f, 0.0f, 0.0f) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("+1"), 1.0f, 0.0f, 0.0f, 0.0f) ]
	];

	// Position Nudge Row Y
	PanelContent->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[ SNew(STextBlock).Text(TAttribute<FText>::CreateSP(this, &SSimCopterToolFlaps::GetSelectedYText)).Font(SmallFont) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("-1"), 0.0f, -1.0f, 0.0f, 0.0f) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("-0.1"), 0.0f, -0.1f, 0.0f, 0.0f) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("+0.1"), 0.0f, 0.1f, 0.0f, 0.0f) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("+1"), 0.0f, 1.0f, 0.0f, 0.0f) ]
	];

	// Scale Nudge Row X
	PanelContent->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[ SNew(STextBlock).Text(TAttribute<FText>::CreateSP(this, &SSimCopterToolFlaps::GetSelectedScaleXText)).Font(SmallFont) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("-0.1"), 0.0f, 0.0f, -0.1f, 0.0f) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("-0.01"), 0.0f, 0.0f, -0.01f, 0.0f) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("+0.01"), 0.0f, 0.0f, 0.01f, 0.0f) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("+0.1"), 0.0f, 0.0f, 0.1f, 0.0f) ]
	];

	// Scale Nudge Row Y
	PanelContent->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[ SNew(STextBlock).Text(TAttribute<FText>::CreateSP(this, &SSimCopterToolFlaps::GetSelectedScaleYText)).Font(SmallFont) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("-0.1"), 0.0f, 0.0f, 0.0f, -0.1f) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("-0.01"), 0.0f, 0.0f, 0.0f, -0.01f) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("+0.01"), 0.0f, 0.0f, 0.0f, 0.01f) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f) [ MakeNudgeBtn(TEXT("+0.1"), 0.0f, 0.0f, 0.0f, 0.1f) ]
	];

	// Global Flap Scale Row
	PanelContent->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[ SNew(STextBlock).Text(TAttribute<FText>::CreateSP(this, &SSimCopterToolFlaps::GetGlobalScaleText)).Font(SmallFont) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
		[
			SNew(SButton).IsFocusable(false).ContentPadding(ButtonPadding)
			.OnClicked_Lambda([this]() { SetScale(Scale - 0.1f); SaveCalibrationData(); return FReply::Handled(); })
			[ SNew(STextBlock).Text(FText::FromString(TEXT("-0.1"))).Font(SmallFont) ]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
		[
			SNew(SButton).IsFocusable(false).ContentPadding(ButtonPadding)
			.OnClicked_Lambda([this]() { SetScale(Scale + 0.1f); SaveCalibrationData(); return FReply::Handled(); })
			[ SNew(STextBlock).Text(FText::FromString(TEXT("+0.1"))).Font(SmallFont) ]
		]
	];

	// Actions Row
	PanelContent->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SButton).IsFocusable(false).ContentPadding(ButtonPadding)
			.OnClicked_Lambda([this]() { ResetSelectedElement(); return FReply::Handled(); })
			[ SNew(STextBlock).Text(FText::FromString(TEXT("Reset Element"))).Font(SmallFont) ]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
		[
			SNew(SButton).IsFocusable(false).ContentPadding(ButtonPadding)
			.OnClicked_Lambda([this]() { SaveCalibrationData(); return FReply::Handled(); })
			[ SNew(STextBlock).Text(FText::FromString(TEXT("Save JSON"))).Font(SmallFont) ]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
		[
			SNew(SButton).IsFocusable(false).ContentPadding(ButtonPadding)
			.OnClicked_Lambda([this]() { ToggleCalibrationMode(); return FReply::Handled(); })
			[ SNew(STextBlock).Text(FText::FromString(TEXT("Exit (Ctrl+Alt+M)"))).Font(SmallFont) ]
		]
	];

	return SNew(SBox)
		.Visibility_Lambda([this]() { return bCalibrationMode ? EVisibility::Visible : EVisibility::Collapsed; })
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.05f, 0.07f, 0.10f, 0.95f))
			.Padding(FMargin(8.0f))
			[
				PanelContent
			]
		];
}

void SSimCopterToolFlaps::LoadCalibrationData()
{
	CalibrationOffsets.Reset();
	CalibrationScales.Reset();

	const FString SavedPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FlapLayoutCalibration.json"));
	const FString ContentPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Slate"), TEXT("FlapLayoutCalibration.json"));

	FString FilePath = SavedPath;
	if (!IFileManager::Get().FileExists(*FilePath) && IFileManager::Get().FileExists(*ContentPath))
	{
		FilePath = ContentPath;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		double LoadedGlobalScale = 0.0;
		if (JsonObject->TryGetNumberField(TEXT("GlobalScale"), LoadedGlobalScale) && LoadedGlobalScale > 0.1)
		{
			Scale = static_cast<float>(LoadedGlobalScale);
		}

		for (const auto& Pair : JsonObject->Values)
		{
			if (FString(Pair.Key).Equals(TEXT("GlobalScale"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			TSharedPtr<FJsonObject> ValueObj = Pair.Value->AsObject();
			if (ValueObj.IsValid())
			{
				double X = 0.0;
				double Y = 0.0;
				double ScaleX = 1.0;
				double ScaleY = 1.0;
				ValueObj->TryGetNumberField(TEXT("x"), X);
				ValueObj->TryGetNumberField(TEXT("y"), Y);
				ValueObj->TryGetNumberField(TEXT("scaleX"), ScaleX);
				ValueObj->TryGetNumberField(TEXT("scaleY"), ScaleY);
				CalibrationOffsets.Add(FString(Pair.Key), FVector2D(X, Y));
				CalibrationScales.Add(FString(Pair.Key), FVector2D(ScaleX, ScaleY));
			}
		}
	}
}

void SSimCopterToolFlaps::SaveCalibrationData() const
{
	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(TEXT("GlobalScale"), Scale);

	for (const auto& Pair : CalibrationOffsets)
	{
		TSharedRef<FJsonObject> EntryObj = MakeShared<FJsonObject>();
		EntryObj->SetNumberField(TEXT("x"), Pair.Value.X);
		EntryObj->SetNumberField(TEXT("y"), Pair.Value.Y);
		const FVector2D ElementScale = GetElementScale(Pair.Key);
		EntryObj->SetNumberField(TEXT("scaleX"), ElementScale.X);
		EntryObj->SetNumberField(TEXT("scaleY"), ElementScale.Y);
		JsonObject->SetObjectField(Pair.Key, EntryObj);
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	if (FJsonSerializer::Serialize(JsonObject, Writer))
	{
		const FString SavedPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FlapLayoutCalibration.json"));
		const FString ContentPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Slate"), TEXT("FlapLayoutCalibration.json"));

		FFileHelper::SaveStringToFile(OutputString, *SavedPath);
		FFileHelper::SaveStringToFile(OutputString, *ContentPath);
		UE_LOG(LogTemp, Log, TEXT("SimCopter Flap Calibration saved to '%s'."), *SavedPath);
	}
}

void SSimCopterToolFlaps::AddAtPageKey(
	SConstraintCanvas& Canvas,
	const FString& Key,
	const float DefaultX,
	const float DefaultY,
	const float Width,
	const float Height,
	TSharedRef<SWidget> Content)
{
	ElementDefaultBounds.Add(Key, FVector4f(DefaultX, DefaultY, Width, Height));

	const FVector2D Offset = GetElementOffset(Key);
	const FVector2D ElementScale = GetElementScale(Key);
	const float PageX = DefaultX + Offset.X;
	const float PageY = DefaultY + Offset.Y;
	const float PageW = Width * ElementScale.X;
	const float PageH = Height * ElementScale.Y;

	TSharedRef<SWidget> WrappedContent = SNew(SSimCopterCalibratableBox)
		.ElementKey(Key)
		.OwnerFlaps(this)
		[
			Content
		];

	SConstraintCanvas::FSlot* Slot = nullptr;
	Canvas.AddSlot()
		.Expose(Slot)
		.Offset(FMargin(PageX * Scale, PageY * Scale, PageW * Scale, PageH * Scale))
		.Alignment(FVector2D::ZeroVector)
		[
			WrappedContent
		];

	if (Slot != nullptr)
	{
		ElementCanvasSlots.Add(Key, Slot);
	}
}

void SSimCopterToolFlaps::AppendMissionMarkerAvoidanceWidgets(TArray<TSharedPtr<SWidget>>& OutWidgets) const
{
	for (const TSharedPtr<SWidget>& Panel : MissionMarkerAvoidancePanels)
	{
		if (Panel.IsValid() && Panel->GetVisibility().IsVisible())
		{
			OutWidgets.Add(Panel);
		}
	}
}

TSharedRef<SWidget> SSimCopterToolFlaps::MakePanel(
	const float PageWidthUnits,
	TSharedRef<SConstraintCanvas> Canvas)
{
	return SNew(SBox)
		.WidthOverride(PageWidthUnits * Scale)
		.HeightOverride(PageHeight * Scale)
		[
			Canvas
		];
}

void SSimCopterToolFlaps::AddAtPage(
	SConstraintCanvas& Canvas,
	const float X,
	const float Y,
	const float Width,
	const float Height,
	TSharedRef<SWidget> Content) const
{
	Canvas.AddSlot()
		.Offset(FMargin(X * Scale, Y * Scale, Width * Scale, Height * Scale))
		.Alignment(FVector2D::ZeroVector)
		[
			Content
		];
}

void SSimCopterToolFlaps::AddTextAtPage(
	SConstraintCanvas& Canvas,
	const float CentreX,
	const float CentreY,
	const float ScreenWidth,
	const float ScreenHeight,
	TSharedRef<SWidget> Content) const
{
	// Anchored on the page position but sized in screen pixels, so the lettering keeps one size
	// whatever the art is scaled to.
	Canvas.AddSlot()
		.Offset(FMargin(CentreX * Scale, CentreY * Scale, ScreenWidth, ScreenHeight))
		.Alignment(FVector2D(0.5f, 0.5f))
		[
			Content
		];
}

const FSlateBrush* SSimCopterToolFlaps::GetBrush(
	const TCHAR* FileName,
	const FIntRect& Source,
	const ESimCopterArtRotation Rotation)
{
	USimCopterHangarArt* ArtObject = Art.Get();
	if (ArtObject == nullptr)
	{
		return nullptr;
	}
	return ArtObject->GetSubImage(FileName, Source, /*bColorKeyed=*/true, Rotation);
}

TSharedRef<SWidget> SSimCopterToolFlaps::MakeImage(const TCHAR* FileName, const FIntRect& Source)
{
	const FSlateBrush* Brush = GetBrush(FileName, Source);
	if (Brush == nullptr)
	{
		return SNullWidget::NullWidget;
	}
	return SNew(SImage).Image(Brush);
}

TSharedRef<SWidget> SSimCopterToolFlaps::MakeLabel(const FText& Text, const int32 FontSize) const
{
	return SNew(STextBlock)
		.Text(Text)
		.Justification(ETextJustify::Center)
		.ColorAndOpacity(FlapLabel)
		.ShadowOffset(FVector2D(1.0f, 1.0f))
		.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f))
		.Font(FlapFont(FontSize));
}

// --- tool flaps --------------------------------------------------------------------------------

TSharedRef<SWidget> SSimCopterToolFlaps::BuildToolFlap(const FFlap& Flap)
{
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	// The page, colour keyed on index 254 the way FUN_004127d0 keys it.
	AddAtPage(*Canvas, 0.0f, 0.0f, static_cast<float>(PageWidth), static_cast<float>(PageHeight),
		MakeImage(Flap.PageFileName, FIntRect(0, 0, PageWidth, PageHeight)));

	for (const FButton& Button : Flap.Buttons)
	{
		AddFlapButton(*Canvas, Button);
	}

	// The two flaps with a readout on them. flap0 is the shared water flap, so its meter is
	// gated on the pair of bits FUN_004127d0 tests, not on one tool.
	if (Flap.EquipmentMask ==
		SimCopterHelicopterRegistry::GetToolCareerBit(ESimCopterHelicopterTool::TearGas))
	{
		AddCanisterCounter(*Canvas);
	}
	else if ((Flap.EquipmentMask &
		(SimCopterHelicopterRegistry::GetToolCareerBit(ESimCopterHelicopterTool::WaterBucket) |
		 SimCopterHelicopterRegistry::GetToolCareerBit(ESimCopterHelicopterTool::WaterCannon))) != 0)
	{
		AddWaterGauge(*Canvas);
	}

	const int32 EquipmentMask = Flap.EquipmentMask;
	return SNew(SBox)
		.Visibility(TAttribute<EVisibility>::CreateSP(this, &SSimCopterToolFlaps::GetFlapVisibility, EquipmentMask))
		[
			MakePanel(static_cast<float>(PageWidth), Canvas)
		];
}

void SSimCopterToolFlaps::AddFlapButton(SConstraintCanvas& Canvas, const FButton& Button)
{
	const EAction Action = Button.Action;
	const ESimCopterHelicopterTool Tool = Button.Tool;

	// The hit box. Nothing is drawn for it: the unpressed button is part of the page.
	TSharedRef<SButton> Hotspot = SNew(SButton)
		// A cockpit control must never hold keyboard focus - the space bar is the collective.
		.IsFocusable(false)
		.ContentPadding(FMargin(0.0f))
		.ToolTipText(FText::FromString(GetActionName(Action)))
		// Deliberately always enabled. A disabled SButton does not handle the mouse, so the click
		// fell straight through the cockpit to the world's left-click binding, which runs the
		// *selected* tool's primary action - pressing the cannon's fire button with the rescue
		// harness selected paid the harness out to full length. The button now always swallows the
		// press and PressAction decides whether the tool can act.
		.OnPressed(FSimpleDelegate::CreateSP(this, &SSimCopterToolFlaps::PressAction, Action))
		.OnReleased(FSimpleDelegate::CreateSP(this, &SSimCopterToolFlaps::ReleaseAction, Action));

	// Transparent in every state, so only the page and the pressed sprite are visible.
	TSharedRef<FButtonStyle> Style = MakeShared<FButtonStyle>();
	Style->SetNormal(FSlateNoResource());
	Style->SetHovered(FSlateNoResource());
	Style->SetPressed(FSlateNoResource());
	Style->SetDisabled(FSlateNoResource());
	Style->SetNormalPadding(FMargin(0.0f));
	Style->SetPressedPadding(FMargin(0.0f));
	ButtonStyles.Add(Style);
	Hotspot->SetButtonStyle(&Style.Get());

	TSharedRef<SConstraintCanvas> ButtonCanvas = SNew(SConstraintCanvas);

	const FIntRect& Frame = Button.Art.PressedFrame;
	if (const FSlateBrush* Pressed = GetBrush(Button.Art.FileName, Frame))
	{
		TWeakPtr<SButton> WeakHotspot = Hotspot;
		const float ArtRelX = static_cast<float>(Button.ArtOrigin.X - Button.Hit.Min.X);
		const float ArtRelY = static_cast<float>(Button.ArtOrigin.Y - Button.Hit.Min.Y);

		ButtonCanvas->AddSlot()
			.Offset(FMargin(ArtRelX * Scale, ArtRelY * Scale, static_cast<float>(Frame.Width()) * Scale, static_cast<float>(Frame.Height()) * Scale))
			.Alignment(FVector2D::ZeroVector)
			[
				SNew(SImage)
				.Image(Pressed)
				.Visibility(TAttribute<EVisibility>::CreateLambda([this, WeakHotspot]()
				{
					if (bCalibrationMode)
					{
						return EVisibility::HitTestInvisible;
					}
					const TSharedPtr<SButton> Held = WeakHotspot.Pin();
					return (Held.IsValid() && Held->IsPressed())
						? EVisibility::HitTestInvisible
						: EVisibility::Hidden;
				}))
			];
	}

	ButtonCanvas->AddSlot()
		.Offset(FMargin(0.0f, 0.0f, static_cast<float>(Button.Hit.Width()) * Scale, static_cast<float>(Button.Hit.Height()) * Scale))
		.Alignment(FVector2D::ZeroVector)
		[
			Hotspot
		];

	const FString Key = FString::Printf(TEXT("Button_%s"), GetActionName(Action));
	AddAtPageKey(Canvas, Key,
		static_cast<float>(Button.Hit.Min.X),
		static_cast<float>(Button.Hit.Min.Y),
		static_cast<float>(Button.Hit.Width()),
		static_cast<float>(Button.Hit.Height()),
		ButtonCanvas);

	// The megaphone's button opens a menu instead of firing, so it needs an anchor under its
	// hit box to hang the menu from.
	if (Action == EAction::MegaphoneBroadcast)
	{
		AddAtPage(Canvas,
			static_cast<float>(Button.Hit.Min.X),
			static_cast<float>(Button.Hit.Max.Y),
			static_cast<float>(Button.Hit.Width()),
			1.0f,
			SAssignNew(MegaphoneMenu, SMenuAnchor)
			.Placement(MenuPlacement_BelowAnchor)
			.OnGetMenuContent(FOnGetContent::CreateSP(this, &SSimCopterToolFlaps::BuildMegaphoneMenu)));
	}
}

// SCHOOK: TearGasCanisterCounter 0x00455790
// Ten 4x4 dots on a 12x13 grid. The original repaints the whole row whenever career + 0x54
// changes; here each lamp is a Slate image that shows itself when its round has been spent, which
// is the same picture without the polling.
void SSimCopterToolFlaps::AddCanisterCounter(SConstraintCanvas& Canvas)
{
	using namespace SimCopterFlapLayout::CanisterCounter;

	USimCopterHangarArt* ArtObject = Art.Get();
	const FSlateBrush* Empty = ArtObject != nullptr
		? ArtObject->GetBitmap(TEXT("red-light.png"), /*bColorKeyed=*/false)
		: nullptr;
	if (Empty == nullptr)
	{
		Empty = GetBrush(GetLampFileName(), GetLampEmptyFrame());
	}
	if (Empty == nullptr)
	{
		return;
	}

	for (int32 Index = 0; Index < LampCount; ++Index)
	{
		const FIntPoint Origin = GetLampOrigin(Index);
		const FString Key = FString::Printf(TEXT("CanisterLamp_%d"), Index);
		AddAtPageKey(
			Canvas,
			Key,
			static_cast<float>(Origin.X),
			static_cast<float>(Origin.Y),
			static_cast<float>(LampSize),
			static_cast<float>(LampSize),
			SNew(SImage)
			.Image(Empty)
			.Visibility(TAttribute<EVisibility>::CreateSP(
				this, &SSimCopterToolFlaps::GetCanisterLampVisibility, Index)));
	}
}

EVisibility SSimCopterToolFlaps::GetCanisterLampVisibility(const int32 LampIndex) const
{
	if (bCalibrationMode)
	{
		return EVisibility::HitTestInvisible;
	}
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr)
	{
		return EVisibility::Collapsed;
	}
	// Hidden rather than Collapsed so the canvas slot keeps its geometry, and never hit-testable:
	// the flap's fire button is the only thing on this page that takes a click.
	return SimCopterFlapLayout::CanisterCounter::IsLampEmpty(
		LampIndex,
		Helicopter->GetEquipmentState().GetTearGasRounds())
		? EVisibility::HitTestInvisible
		: EVisibility::Hidden;
}

// SCHOOK: WaterGaugeRepaint 0x00455700
// Eleven 5x10 cells from x 16, y 43. The original repaints the row whenever heli[0x74] * 11 /
// maxLoad changes; here each cell binds its sprite to that same quotient.
void SSimCopterToolFlaps::AddWaterGauge(SConstraintCanvas& Canvas)
{
	using namespace SimCopterFlapLayout::WaterGauge;

	WaterGaugeBrushes[0] = GetBrush(GetGaugeFileName(), GetCellFrame(ECell::Full));
	WaterGaugeBrushes[1] = GetBrush(GetGaugeFileName(), GetCellFrame(ECell::LeadingEdge));
	WaterGaugeBrushes[2] = GetBrush(GetGaugeFileName(), GetCellFrame(ECell::Empty));
	if (WaterGaugeBrushes[0] == nullptr)
	{
		// Without the art the page's own printed gauge is still there, reading empty.
		return;
	}

	for (int32 Index = 0; Index < CellCount; ++Index)
	{
		const FIntPoint Origin = GetCellOrigin(Index);
		const FString Key = FString::Printf(TEXT("WaterGauge_%d"), Index);
		AddAtPageKey(
			Canvas,
			Key,
			static_cast<float>(Origin.X),
			static_cast<float>(Origin.Y),
			static_cast<float>(CellWidth),
			static_cast<float>(CellHeight),
			SNew(SImage)
			.Image(TAttribute<const FSlateBrush*>::CreateSP(
				this, &SSimCopterToolFlaps::GetWaterGaugeCellBrush, Index))
			// The gauge is a readout, not a control; the flap's own buttons take every click.
			.Visibility(EVisibility::HitTestInvisible));
	}
}

const FSlateBrush* SSimCopterToolFlaps::GetWaterGaugeCellBrush(const int32 CellIndex) const
{
	using namespace SimCopterFlapLayout::WaterGauge;

	if (bCalibrationMode)
	{
		return WaterGaugeBrushes[0] != nullptr ? WaterGaugeBrushes[0] : WaterGaugeBrushes[2];
	}

	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	const int32 Level = Helicopter != nullptr
		? GetLevel(Helicopter->GetBucketWaterPounds(), Helicopter->GetMaxLoadPounds())
		: 0;
	return WaterGaugeBrushes[static_cast<int32>(GetCellState(CellIndex, Level))];
}

EVisibility SSimCopterToolFlaps::GetFlapVisibility(const int32 EquipmentMask) const
{
	if (bCalibrationMode)
	{
		return EVisibility::SelfHitTestInvisible;
	}
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr)
	{
		return EVisibility::Collapsed;
	}

	// Exactly FUN_004127d0's test: the flap exists when any of its bits is aboard.
	return (Helicopter->GetEquipmentState().GetEffectiveEquipmentMask() & EquipmentMask) != 0
		? EVisibility::SelfHitTestInvisible
		: EVisibility::Collapsed;
}

bool SSimCopterToolFlaps::IsToolButtonEnabled(const ESimCopterHelicopterTool Tool) const
{
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	return Helicopter != nullptr && Helicopter->IsToolAvailable(Tool);
}

// --- actions -----------------------------------------------------------------------------------

void SSimCopterToolFlaps::PressAction(const EAction Action)
{
	ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr)
	{
		return;
	}
	// The hotspot is always clickable so it consumes the press; whether the tool may act is
	// decided here.
	{
		const ESimCopterHelicopterTool ActionTool =
			(Action == EAction::CannonFire) ? ESimCopterHelicopterTool::WaterCannon :
			(Action == EAction::MegaphoneBroadcast) ? ESimCopterHelicopterTool::Megaphone :
			(Action == EAction::TearGasFire) ? ESimCopterHelicopterTool::TearGas :
			(Action == EAction::ApacheMissileFire) ? ESimCopterHelicopterTool::ApacheMissile :
			(Action == EAction::ApacheGunFire) ? ESimCopterHelicopterTool::ApacheMachineGun :
			(Action == EAction::HarnessRaise || Action == EAction::HarnessLower)
				? ESimCopterHelicopterTool::RescueHarness
				: ESimCopterHelicopterTool::WaterBucket;
		if (!IsToolButtonEnabled(ActionTool))
		{
			return;
		}
	}

	switch (Action)
	{
	case EAction::CannonFire:
		Helicopter->StartWaterCannon();
		break;
	case EAction::BucketDump:
		Helicopter->StartBucketDump();
		break;
	// The winch rockers pay out for as long as they are held, so the player can stop the bucket
	// part way down - which is where it fills.
	case EAction::BucketRaise:
		Helicopter->SetWinchHeldInput(/*bHarness=*/false, 1);
		break;
	case EAction::BucketLower:
		Helicopter->SetWinchHeldInput(/*bHarness=*/false, -1);
		break;
	case EAction::HarnessRaise:
		Helicopter->SetWinchHeldInput(/*bHarness=*/true, 1);
		break;
	case EAction::HarnessLower:
		Helicopter->SetWinchHeldInput(/*bHarness=*/true, -1);
		break;
	case EAction::MegaphoneBroadcast:
		if (MegaphoneMenu.IsValid())
		{
			// Keep keyboard focus on the game viewport while the mouse-only phrase menu is open;
			// otherwise clicking the cockpit art interrupts keyboard flight input.
			MegaphoneMenu->SetIsOpen(!MegaphoneMenu->IsOpen(), /*bFocusMenu=*/false);
		}
		break;
	case EAction::TearGasFire:
		// One shot, through the same latch left click uses, with the launcher selected so the
		// dispatch picks the right tool.
		Helicopter->SetSelectedTool(ESimCopterHelicopterTool::TearGas);
		Helicopter->StartPrimaryToolUse();
		break;
	case EAction::ApacheGunFire:
		// Held: the emitter lays down a tracer every frame the button is down.
		Helicopter->SetSelectedTool(ESimCopterHelicopterTool::ApacheMachineGun);
		Helicopter->StartPrimaryToolUse();
		break;
	case EAction::ApacheMissileFire:
		// One per press, gated downstream by the shared cooldown - bPrimaryToolUsePressed is a
		// one-shot latch, so holding the button still only launches once.
		Helicopter->SetSelectedTool(ESimCopterHelicopterTool::ApacheMissile);
		Helicopter->StartPrimaryToolUse();
		break;
	default:
		break;
	}
}

void SSimCopterToolFlaps::ReleaseAction(const EAction Action)
{
	ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr)
	{
		return;
	}

	switch (Action)
	{
	case EAction::CannonFire:
		Helicopter->StopWaterCannon();
		break;
	case EAction::BucketDump:
		Helicopter->StopBucketDump();
		break;
	case EAction::BucketRaise:
	case EAction::BucketLower:
		Helicopter->SetWinchHeldInput(/*bHarness=*/false, 0);
		break;
	case EAction::HarnessRaise:
	case EAction::HarnessLower:
		Helicopter->SetWinchHeldInput(/*bHarness=*/true, 0);
		break;
	case EAction::ApacheGunFire:
	case EAction::ApacheMissileFire:
	case EAction::TearGasFire:
		Helicopter->StopPrimaryToolUse();
		break;
	default:
		// The megaphone opens a menu; there is nothing to let go of.
		break;
	}
}

// --- megaphone menu ----------------------------------------------------------------------------

TSharedRef<SWidget> SSimCopterToolFlaps::BuildMegaphoneMenu()
{
	// The five messages and the keys the help pins them to (34ref.htm).
	static const TCHAR* const MessageKeys[] = { TEXT("F6"), TEXT("F7"), TEXT("F8"), TEXT("F9"), TEXT("F10") };

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	const int32 Count = static_cast<int32>(ESimCopterMegaphoneMessage::Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const ESimCopterMegaphoneMessage Message = static_cast<ESimCopterMegaphoneMessage>(Index);
		const FString Label = FString::Printf(
			TEXT("%s   %s"),
			SimCopterHelicopterRegistry::GetMegaphoneMessageName(Message),
			MessageKeys[Index]);

		List->AddSlot()
			.AutoHeight()
			[
				SNew(SButton)
				.IsFocusable(false)
				.ContentPadding(FMargin(8.0f, 2.0f))
				.HAlign(HAlign_Left)
				.OnClicked(FOnClicked::CreateSP(this, &SSimCopterToolFlaps::HandleMegaphoneMessageChosen, Message))
				[
					SNew(STextBlock).Text(FText::FromString(Label)).Font(FlapFont(LabelFontSize, false))
				]
			];
	}

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FLinearColor(0.05f, 0.06f, 0.08f, 0.95f))
		.Padding(FMargin(2.0f))
		[
			List
		];
}

FReply SSimCopterToolFlaps::HandleMegaphoneMessageChosen(const ESimCopterMegaphoneMessage Message)
{
	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		Helicopter->SendMegaphoneMessage(Message);
	}
	if (MegaphoneMenu.IsValid())
	{
		MegaphoneMenu->SetIsOpen(false, /*bFocusMenu=*/false);
	}
	return FReply::Handled();
}

// --- dispatch strip ------------------------------------------------------------------------------

// The remake's own strip background, for the two panels the original has no artwork for: a tool
// flap's frame taken apart, with a slice of bare grid tiled out to whatever width is wanted.
void SSimCopterToolFlaps::AddStripBackground(
	SConstraintCanvas& Canvas,
	const float PageWidthUnits,
	TSharedPtr<FSlateBrush>& InOutFillBrush)
{
	const float LeftWidth = static_cast<float>(DispatchFrameLeft.Width());
	const float RightWidth = static_cast<float>(DispatchFrameRight.Width());
	const float FillWidth = PageWidthUnits - LeftWidth - RightWidth;

	AddAtPage(Canvas, 0.0f, 0.0f, LeftWidth, static_cast<float>(PageHeight),
		MakeImage(DispatchFrameFile, DispatchFrameLeft));

	if (const FSlateBrush* Fill = GetBrush(DispatchFrameFile, DispatchFrameFill))
	{
		// Tiled rather than stretched: the grid is a six pixel repeat, and stretching it across
		// the strip would smear it into bands.
		InOutFillBrush = MakeShared<FSlateBrush>(*Fill);
		InOutFillBrush->Tiling = ESlateBrushTileType::Horizontal;
		InOutFillBrush->ImageSize = FVector2D(DispatchFrameFill.Width() * Scale, PageHeight * Scale);
		AddAtPage(Canvas, LeftWidth, 0.0f, FillWidth, static_cast<float>(PageHeight),
			SNew(SImage).Image(InOutFillBrush.Get()));
	}

	AddAtPage(Canvas, PageWidthUnits - RightWidth, 0.0f, RightWidth, static_cast<float>(PageHeight),
		MakeImage(DispatchFrameFile, DispatchFrameRight));
}

// SCHOOK: none - the Apache's weapons are model capabilities, not equipment bits, so
// FUN_004127d0 never builds a flap for them and the original ships no artwork. This strip is the
// remake's, built from the same donor frame as the dispatch strip.
TSharedRef<SWidget> SSimCopterToolFlaps::BuildApacheFlap()
{
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
	if (const FSlateBrush* BackgroundBrush = GetBrush(TEXT("FLAP2.BMP"), FIntRect(0, 0, PageWidth, PageHeight)))
	{
		AddAtPage(*Canvas, 0.0f, 0.0f, ApachePageWidth, static_cast<float>(PageHeight),
			SNew(SImage).Image(BackgroundBrush));
	}
	else
	{
		AddStripBackground(*Canvas, ApachePageWidth, ApacheFillBrush);
	}

	// Two buttons, no readout: each one just fires its weapon. The ammunition is unlimited and
	// the only limits - the shared 1 s missile cooldown and the pool sizes - are things the
	// player feels rather than reads.
	//
	// BOTH go through the press/release latch, not OnClicked. StopPrimaryToolUse() clears
	// bPrimaryToolUsePressed as well as the held flag, so a handler that pressed and released
	// inside one frame cancelled the shot before UpdateToolDispatch ever ran - the missile button
	// selected the launcher and then fired nothing. Press and release have to straddle a tick.
	//
	// Both strips are right-aligned in the column, so the two buttons are placed by their
	// distance from the RIGHT edge to line up with DISPATCH and CLEAR above them.
	AddAtPageKey(*Canvas, TEXT("Apache_MissileBtn"), ApachePageWidth - MissileButtonInsetFromRight, 4.0f, 17.0f, 24.0f,
		MakeHeldArtButton(OctagonFile, OctagonNormal, OctagonPressed,
			SimCopterFlapLayout::EAction::ApacheMissileFire,
			NSLOCTEXT("SimCopterFlaps", "MissileTip", "Fire a missile")));
	AddTextAtPage(*Canvas, ApachePageWidth - MissileButtonInsetFromRight + 8.5f, 35.0f, 120.0f, 20.0f,
		MakeLabel(NSLOCTEXT("SimCopterFlaps", "Missile", "MISSILE"), LabelFontSize));

	AddAtPageKey(*Canvas, TEXT("Apache_GunBtn"), ApachePageWidth - GunButtonInsetFromRight, 4.0f, 17.0f, 24.0f,
		MakeHeldArtButton(OctagonFile, OctagonNormal, OctagonPressed,
			SimCopterFlapLayout::EAction::ApacheGunFire,
			NSLOCTEXT("SimCopterFlaps", "GunTip", "Hold to fire the machine gun")));
	AddTextAtPage(*Canvas, ApachePageWidth - GunButtonInsetFromRight + 8.5f, 35.0f, 120.0f, 20.0f,
		MakeLabel(NSLOCTEXT("SimCopterFlaps", "Gun", "GUN"), LabelFontSize));

	return SNew(SBox)
		.Visibility(TAttribute<EVisibility>::CreateSP(this, &SSimCopterToolFlaps::GetApacheFlapVisibility))
		[
			MakePanel(ApachePageWidth, Canvas)
		];
}

EVisibility SSimCopterToolFlaps::GetApacheFlapVisibility() const
{
	if (bCalibrationMode)
	{
		return EVisibility::SelfHitTestInvisible;
	}
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	// Both weapons come from the airframe, so one test covers the strip.
	return (Helicopter != nullptr &&
			Helicopter->IsToolAvailable(ESimCopterHelicopterTool::ApacheMachineGun))
		? EVisibility::SelfHitTestInvisible
		: EVisibility::Collapsed;
}

TSharedRef<SWidget> SSimCopterToolFlaps::BuildDispatchFlap()
{
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
	if (const FSlateBrush* BackgroundBrush = GetBrush(TEXT("FLAP-dispatch.BMP"), FIntRect(0, 0, FMath::RoundToInt(DispatchPageWidth), PageHeight)))
	{
		AddAtPage(*Canvas, 0.0f, 0.0f, DispatchPageWidth, static_cast<float>(PageHeight),
			SNew(SImage).Image(BackgroundBrush));
	}
	else
	{
		AddStripBackground(*Canvas, DispatchPageWidth, DispatchFillBrush);
	}

	// The service selector: one arrow sprite, turned each way.
	AddAtPageKey(*Canvas, TEXT("Dispatch_PrevService"), 8.0f, 12.0f,
		static_cast<float>(RockerArrowWidth), static_cast<float>(RockerArrowHeight),
		MakeArtButton(RockerFile, RockerArrowNormal, RockerArrowPressed, ESimCopterArtRotation::Clockwise90,
			FOnClicked::CreateSP(this, &SSimCopterToolFlaps::HandleDispatchServiceStep, -1),
			NSLOCTEXT("SimCopterFlaps", "PrevService", "Previous service")));

	AddAtPageKey(*Canvas, TEXT("Dispatch_ServiceBox"), 26.0f, 12.0f, 74.0f, 17.0f,
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.85f))
		.Padding(FMargin(0.0f)));

	AddTextAtPage(*Canvas, 63.0f, 20.5f, 170.0f, 24.0f,
		SNew(STextBlock)
		.Text(this, &SSimCopterToolFlaps::GetDispatchServiceText)
		// The long POLICE (CHASE) label extends over the left arrow. The right arrow is added
		// after it, but the left one sits underneath, so a hit-testable label swallowed only
		// the previous-service click.
		.Visibility(EVisibility::HitTestInvisible)
		.Justification(ETextJustify::Center)
		.ColorAndOpacity(FlapReadout)
		.Font(FlapFont(ReadoutFontSize)));

	AddAtPageKey(*Canvas, TEXT("Dispatch_NextService"), 104.0f, 12.0f,
		static_cast<float>(RockerArrowWidth), static_cast<float>(RockerArrowHeight),
		MakeArtButton(RockerFile, RockerArrowNormal, RockerArrowPressed, ESimCopterArtRotation::CounterClockwise90,
			FOnClicked::CreateSP(this, &SSimCopterToolFlaps::HandleDispatchServiceStep, 1),
			NSLOCTEXT("SimCopterFlaps", "NextService", "Next service")));

	// Dispatch and Clear, spaced so both labels clear each other and neither button
	// runs into the right edge's dash corner, which starts at DispatchPageWidth - 40.
	AddAtPageKey(*Canvas, TEXT("Dispatch_DispatchBtn"), 126.0f, 4.0f, 17.0f, 24.0f,
		MakeArtButton(OctagonFile, OctagonNormal, OctagonPressed, ESimCopterArtRotation::None,
			FOnClicked::CreateSP(this, &SSimCopterToolFlaps::HandleDispatch),
			NSLOCTEXT("SimCopterFlaps", "DispatchTip", "Dispatch the selected service to the spotlight")));
	AddTextAtPage(*Canvas, 134.5f, 35.0f, 120.0f, 20.0f,
		MakeLabel(NSLOCTEXT("SimCopterFlaps", "Dispatch", "DISPATCH"), LabelFontSize));

	AddAtPageKey(*Canvas, TEXT("Dispatch_ClearBtn"), 168.0f, 4.0f, 17.0f, 24.0f,
		MakeArtButton(OctagonFile, OctagonNormal, OctagonPressed, ESimCopterArtRotation::None,
			FOnClicked::CreateSP(this, &SSimCopterToolFlaps::HandleDispatchClear),
			NSLOCTEXT("SimCopterFlaps", "ClearTip", "Immediately clear all dispatched vehicles")));
	AddTextAtPage(*Canvas, 176.5f, 35.0f, 120.0f, 20.0f,
		MakeLabel(NSLOCTEXT("SimCopterFlaps", "Clear", "CLEAR"), LabelFontSize));

	return MakePanel(DispatchPageWidth, Canvas);
}

// The held variant: the machine gun has to fire for as long as the button is down, so it goes
// through the same press/release latch the flap controls use rather than OnClicked.
TSharedRef<SWidget> SSimCopterToolFlaps::MakeHeldArtButton(
	const TCHAR* FileName,
	const FIntRect& NormalFrame,
	const FIntRect& PressedFrame,
	const SimCopterFlapLayout::EAction Action,
	const FText& ToolTip)
{
	TSharedRef<SButton> Button = SNew(SButton)
		.IsFocusable(false)
		.ContentPadding(FMargin(0.0f))
		.ToolTipText(ToolTip)
		.OnPressed(FSimpleDelegate::CreateSP(this, &SSimCopterToolFlaps::PressAction, Action))
		.OnReleased(FSimpleDelegate::CreateSP(this, &SSimCopterToolFlaps::ReleaseAction, Action));

	const FSlateBrush* Normal = GetBrush(FileName, NormalFrame);
	const FSlateBrush* Pressed = GetBrush(FileName, PressedFrame);
	if (Normal != nullptr)
	{
		TSharedRef<FButtonStyle> Style = MakeShared<FButtonStyle>();
		Style->SetNormal(*Normal);
		Style->SetHovered(*Normal);
		Style->SetPressed(Pressed != nullptr ? *Pressed : *Normal);
		Style->SetDisabled(*Normal);
		Style->SetNormalPadding(FMargin(0.0f));
		Style->SetPressedPadding(FMargin(0.0f));
		ButtonStyles.Add(Style);
		Button->SetButtonStyle(&Style.Get());
	}

	return Button;
}

TSharedRef<SWidget> SSimCopterToolFlaps::MakeArtButton(
	const TCHAR* FileName,
	const FIntRect& NormalFrame,
	const FIntRect& PressedFrame,
	const ESimCopterArtRotation Rotation,
	FOnClicked OnClicked,
	const FText& ToolTip)
{
	TSharedRef<SButton> Button = SNew(SButton)
		.IsFocusable(false)
		.ContentPadding(FMargin(0.0f))
		.ToolTipText(ToolTip)
		.OnClicked(OnClicked);

	// Unlike a flap's controls there is no page art underneath, so the unpressed frame is the
	// button's normal state and the pressed frame swaps in on click.
	const FSlateBrush* Normal = GetBrush(FileName, NormalFrame, Rotation);
	const FSlateBrush* Pressed = GetBrush(FileName, PressedFrame, Rotation);
	if (Normal != nullptr)
	{
		TSharedRef<FButtonStyle> Style = MakeShared<FButtonStyle>();
		Style->SetNormal(*Normal);
		Style->SetHovered(*Normal);
		Style->SetPressed(Pressed != nullptr ? *Pressed : *Normal);
		Style->SetDisabled(*Normal);
		Style->SetNormalPadding(FMargin(0.0f));
		Style->SetPressedPadding(FMargin(0.0f));
		ButtonStyles.Add(Style);
		Button->SetButtonStyle(&Style.Get());
	}

	return Button;
}

FText SSimCopterToolFlaps::GetDispatchServiceText() const
{
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr)
	{
		return FText::GetEmpty();
	}
	return FText::FromString(GetDispatchServiceLabel(SelectedDispatchEntry));
}

FReply SSimCopterToolFlaps::HandleDispatchServiceStep(const int32 Delta)
{
	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		SelectedDispatchEntry =
			((SelectedDispatchEntry + Delta) % DispatchEntryCount + DispatchEntryCount) % DispatchEntryCount;

		// Keep the pawn's real-service selection in sync for the controller wheel and debug panel.
		// The chase pseudo-entry selects Police in those surfaces because they expose chase as a
		// separate action rather than as a list entry.
		int32 ServiceIndex = INDEX_NONE;
		bool bChase = false;
		if (ResolveDispatchEntry(SelectedDispatchEntry, ServiceIndex, bChase))
		{
			Helicopter->CycleSelectedDispatchService(
				ServiceIndex - Helicopter->GetSelectedDispatchService());
		}
	}
	return FReply::Handled();
}

FReply SSimCopterToolFlaps::HandleDispatch()
{
	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		int32 ServiceIndex = INDEX_NONE;
		bool bChase = false;
		if (ResolveDispatchEntry(SelectedDispatchEntry, ServiceIndex, bChase))
		{
			Helicopter->RequestDispatch(ServiceIndex, bChase, /*bClear=*/false);
		}
	}
	return FReply::Handled();
}

FReply SSimCopterToolFlaps::HandleDispatchClear()
{
	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		Helicopter->ClearAllDispatchVehicles();
	}
	return FReply::Handled();
}
