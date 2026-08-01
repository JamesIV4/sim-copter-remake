// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterToolFlaps.h"

#include "Brushes/SlateNoResource.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Ground/SimCopterDispatch.h"
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

// Screen pixels between stacked panels. Matches the inset the pawn gives the whole column, so
// the gap above the first panel and the gaps between them read the same.
constexpr float PanelGapPixels = 12.0f;

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

FSlateFontInfo FlapFont(const int32 Size, const bool bBold = true)
{
	return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
}

const TCHAR* GetDispatchServiceLabel(const int32 ServiceIndex)
{
	switch (static_cast<SimCopterDispatch::EService>(ServiceIndex))
	{
	case SimCopterDispatch::EService::FireTruck: return TEXT("FIRE TRUCK");
	case SimCopterDispatch::EService::Police: return TEXT("POLICE");
	case SimCopterDispatch::EService::Ambulance: return TEXT("AMBULANCE");
	default: return TEXT("-");
	}
}
}

void SSimCopterToolFlaps::Construct(const FArguments& InArgs)
{
	Pawn = InArgs._Pawn;
	Art = InArgs._Art;
	Scale = FMath::Max(0.5f, InArgs._Scale);

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	MissionMarkerAvoidancePanels.Reset();

	// The dispatch strip sits above the tools, with the same gap between panels that the column
	// leaves above itself. A flap the helicopter is not carrying collapses, and a collapsed slot
	// takes its padding with it, so the gaps never double up.
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

	ChildSlot
	[
		Column
	];
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

	// The pressed sprite, at its own origin rather than the hit box's: the click boxes carry a
	// pixel or two of slop, and a rocker's halves each take half the sprite.
	const FIntRect& Frame = Button.Art.PressedFrame;
	if (const FSlateBrush* Pressed = GetBrush(Button.Art.FileName, Frame))
	{
		TWeakPtr<SButton> WeakHotspot = Hotspot;
		AddAtPage(Canvas,
			static_cast<float>(Button.ArtOrigin.X),
			static_cast<float>(Button.ArtOrigin.Y),
			static_cast<float>(Frame.Width()),
			static_cast<float>(Frame.Height()),
			SNew(SImage)
			.Image(Pressed)
			// Hidden, never Collapsed: the canvas slot keeps its geometry either way, and the
			// sprite must not take the click that is lighting it.
			.Visibility(TAttribute<EVisibility>::CreateLambda([WeakHotspot]()
			{
				const TSharedPtr<SButton> Held = WeakHotspot.Pin();
				return (Held.IsValid() && Held->IsPressed())
					? EVisibility::HitTestInvisible
					: EVisibility::Hidden;
			})));
	}

	// Added last so it sits over the pressed sprite and takes the click.
	AddAtPage(Canvas,
		static_cast<float>(Button.Hit.Min.X),
		static_cast<float>(Button.Hit.Min.Y),
		static_cast<float>(Button.Hit.Width()),
		static_cast<float>(Button.Hit.Height()),
		Hotspot);

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

	const FSlateBrush* Empty = GetBrush(GetLampFileName(), GetLampEmptyFrame());
	if (Empty == nullptr)
	{
		return;
	}

	for (int32 Index = 0; Index < LampCount; ++Index)
	{
		const FIntPoint Origin = GetLampOrigin(Index);
		AddAtPage(
			Canvas,
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
		AddAtPage(
			Canvas,
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

	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	const int32 Level = Helicopter != nullptr
		? GetLevel(Helicopter->GetBucketWaterPounds(), Helicopter->GetMaxLoadPounds())
		: 0;
	return WaterGaugeBrushes[static_cast<int32>(GetCellState(CellIndex, Level))];
}

EVisibility SSimCopterToolFlaps::GetFlapVisibility(const int32 EquipmentMask) const
{
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
	case EAction::TearGasFire:
	case EAction::ApacheGunFire:
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
	AddStripBackground(*Canvas, ApachePageWidth, ApacheFillBrush);

	// Two buttons, no readout: each one just fires its weapon. The ammunition is unlimited and
	// the only limits - the shared 1 s missile cooldown and the pool sizes - are things the
	// player feels rather than reads.
	AddAtPage(*Canvas, 20.0f, 4.0f, 17.0f, 24.0f,
		MakeArtButton(OctagonFile, OctagonNormal, OctagonPressed, ESimCopterArtRotation::None,
			FOnClicked::CreateSP(this, &SSimCopterToolFlaps::HandleApacheMissile),
			NSLOCTEXT("SimCopterFlaps", "MissileTip", "Fire a missile")));
	AddTextAtPage(*Canvas, 28.5f, 35.0f, 120.0f, 20.0f,
		MakeLabel(NSLOCTEXT("SimCopterFlaps", "Missile", "MISSILE"), LabelFontSize));

	// Held, like the water cannon's trigger.
	AddAtPage(*Canvas, 72.0f, 4.0f, 17.0f, 24.0f,
		MakeHeldArtButton(OctagonFile, OctagonNormal, OctagonPressed,
			SimCopterFlapLayout::EAction::ApacheGunFire,
			NSLOCTEXT("SimCopterFlaps", "GunTip", "Hold to fire the machine gun")));
	AddTextAtPage(*Canvas, 80.5f, 35.0f, 120.0f, 20.0f,
		MakeLabel(NSLOCTEXT("SimCopterFlaps", "Gun", "GUN"), LabelFontSize));

	return SNew(SBox)
		.Visibility(TAttribute<EVisibility>::CreateSP(this, &SSimCopterToolFlaps::GetApacheFlapVisibility))
		[
			MakePanel(ApachePageWidth, Canvas)
		];
}

EVisibility SSimCopterToolFlaps::GetApacheFlapVisibility() const
{
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	// Both weapons come from the airframe, so one test covers the strip.
	return (Helicopter != nullptr &&
			Helicopter->IsToolAvailable(ESimCopterHelicopterTool::ApacheMachineGun))
		? EVisibility::SelfHitTestInvisible
		: EVisibility::Collapsed;
}

FReply SSimCopterToolFlaps::HandleApacheMissile()
{
	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		Helicopter->SetSelectedTool(ESimCopterHelicopterTool::ApacheMissile);
		Helicopter->StartPrimaryToolUse();
		Helicopter->StopPrimaryToolUse();
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SSimCopterToolFlaps::BuildDispatchFlap()
{
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
	AddStripBackground(*Canvas, DispatchPageWidth, DispatchFillBrush);

	// The service selector: one arrow sprite, turned each way.
	AddAtPage(*Canvas, 8.0f, 12.0f,
		static_cast<float>(RockerArrowWidth), static_cast<float>(RockerArrowHeight),
		MakeArtButton(RockerFile, RockerArrowNormal, RockerArrowPressed, ESimCopterArtRotation::Clockwise90,
			FOnClicked::CreateSP(this, &SSimCopterToolFlaps::HandleDispatchServiceStep, -1),
			NSLOCTEXT("SimCopterFlaps", "PrevService", "Previous service")));

	AddAtPage(*Canvas, 26.0f, 12.0f, 74.0f, 17.0f,
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.85f))
		.Padding(FMargin(0.0f)));

	AddTextAtPage(*Canvas, 63.0f, 20.5f, 170.0f, 24.0f,
		SNew(STextBlock)
		.Text(this, &SSimCopterToolFlaps::GetDispatchServiceText)
		.Justification(ETextJustify::Center)
		.ColorAndOpacity(FlapReadout)
		.Font(FlapFont(ReadoutFontSize)));

	AddAtPage(*Canvas, 104.0f, 12.0f,
		static_cast<float>(RockerArrowWidth), static_cast<float>(RockerArrowHeight),
		MakeArtButton(RockerFile, RockerArrowNormal, RockerArrowPressed, ESimCopterArtRotation::CounterClockwise90,
			FOnClicked::CreateSP(this, &SSimCopterToolFlaps::HandleDispatchServiceStep, 1),
			NSLOCTEXT("SimCopterFlaps", "NextService", "Next service")));

	// Dispatch and Dispatch (Chase), spaced so both labels clear each other and neither button
	// runs into the right edge's dash corner, which starts at DispatchPageWidth - 40.
	AddAtPage(*Canvas, 126.0f, 4.0f, 17.0f, 24.0f,
		MakeArtButton(OctagonFile, OctagonNormal, OctagonPressed, ESimCopterArtRotation::None,
			FOnClicked::CreateSP(this, &SSimCopterToolFlaps::HandleDispatch, false),
			NSLOCTEXT("SimCopterFlaps", "DispatchTip", "Dispatch the selected service to the spotlight")));
	AddTextAtPage(*Canvas, 134.5f, 35.0f, 120.0f, 20.0f,
		MakeLabel(NSLOCTEXT("SimCopterFlaps", "Dispatch", "DISPATCH"), LabelFontSize));

	// Chase is the original's F5, which only the police answer.
	AddAtPage(*Canvas, 168.0f, 4.0f, 17.0f, 24.0f,
		MakeArtButton(OctagonFile, OctagonNormal, OctagonPressed, ESimCopterArtRotation::None,
			FOnClicked::CreateSP(this, &SSimCopterToolFlaps::HandleDispatch, true),
			NSLOCTEXT("SimCopterFlaps", "ChaseTip", "Dispatch a police chase (F5)")));
	AddTextAtPage(*Canvas, 176.5f, 35.0f, 120.0f, 20.0f,
		MakeLabel(NSLOCTEXT("SimCopterFlaps", "Chase", "CHASE"), LabelFontSize));

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
	return FText::FromString(GetDispatchServiceLabel(Helicopter->GetSelectedDispatchService()));
}

FReply SSimCopterToolFlaps::HandleDispatchServiceStep(const int32 Delta)
{
	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		Helicopter->CycleSelectedDispatchService(Delta);
	}
	return FReply::Handled();
}

FReply SSimCopterToolFlaps::HandleDispatch(const bool bChase)
{
	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		Helicopter->RequestDispatch(Helicopter->GetSelectedDispatchService(), bChase, /*bClear=*/false);
	}
	return FReply::Handled();
}
