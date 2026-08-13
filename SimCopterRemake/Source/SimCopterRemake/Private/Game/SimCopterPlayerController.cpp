// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterPlayerController.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "Audio/SimCopterRadio.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Formats/SimCopterOriginalGamePaths.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/SimCopterSessionSubsystem.h"
#include "Game/SimCopterSaveSubsystem.h"
#include "Game/SimCopterSettings.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Kismet/GameplayStatics.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "Replay/SimCopterReplayFreeCamera.h"
#include "Replay/SimCopterReplaySubsystem.h"
#include "UI/SSimCopterReplayPanel.h"
#include "UI/SSimCopterCitySettings.h"
#include "UI/SSimCopterControlSettings.h"
#include "UI/SSimCopterGraphicsSettings.h"
#include "UI/SSimCopterMessageBox.h"
#include "UI/SSimCopterSaveNameDialog.h"
#include "UI/SSimCopterSettingsMenu.h"
#include "UI/SSimCopterSoundSettings.h"
#include "UI/SimCopterHangarArt.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCopterPlayerController"

namespace
{
/** The action DefaultInput.ini binds to Escape. */
const TCHAR* const SettingsAction = TEXT("SimCopterSettingsMenu");

// The replay panel's own bindings, plus the flight axes it borrows for the free camera. The axes
// are deliberately the ones already in DefaultInput.ini: W/S, A/D and Space/LeftCtrl mean forward,
// strafe and up/down to anyone who has flown the helicopter, and a second set of mappings for the
// same keys would only be one more thing to keep in step.
const TCHAR* const ReplayPanelAction = TEXT("SimCopterReplayPanel");
const TCHAR* const ReplayHideHudAction = TEXT("SimCopterReplayHideHud");
const TCHAR* const CycleCameraAction = TEXT("SimCopterCycleCamera");
const TCHAR* const CameraDragAction = TEXT("SimCopterCameraDrag");
const TCHAR* const ForwardAxis = TEXT("SimCopterPitch");
const TCHAR* const StrafeAxis = TEXT("SimCopterRoll");
const TCHAR* const VerticalAxis = TEXT("SimCopterCollective");
const TCHAR* const LookYawAxis = TEXT("SimCopterMouseLookYaw");
const TCHAR* const LookPitchAxis = TEXT("SimCopterMouseLookPitch");
const TCHAR* const ZoomAxis = TEXT("SimCopterCameraZoom");

// The review transport. Every one of these shares its key with something the pawn does while the
// world is running (Space is collective, the arrows are roll, Home resets the aircraft), which is
// safe because each handler refuses unless a clip is being reviewed - and a review is paused, so
// the pawn's own bindings are not firing then.
const TCHAR* const TransportPlayPauseAction = TEXT("SimCopterReplayPlayPause");
const TCHAR* const TransportStepBackAction = TEXT("SimCopterReplayStepBack");
const TCHAR* const TransportStepForwardAction = TEXT("SimCopterReplayStepForward");
const TCHAR* const TransportGoToStartAction = TEXT("SimCopterReplayGoToStart");
const TCHAR* const TransportBookmarkAction = TEXT("SimCopterReplayBookmark");
}

ASimCopterPlayerController::ASimCopterPlayerController()
{
	bShowMouseCursor = true;
}

void ASimCopterPlayerController::BeginPlay()
{
	Super::BeginPlay();

	Art = NewObject<USimCopterHangarArt>(this, TEXT("SettingsArt"));
	Art->SetOriginalGameRoot(ResolveOriginalGameRoot());

	// Kill the engine's own "PAUSED / START RESUME" overlay.
	//
	// UGameViewportClient::DrawTransition draws it from ETransitionType::Paused whenever the world
	// is paused, straight onto the canvas above everything - it is console-era engine furniture, it
	// is not this game's art, and it appears over both of the remake's pauses: the Settings screen
	// (FUN_004346c0's reference-counted pause) and a replay review, where it sits in the middle of
	// every shot the replay tool exists to capture. The viewport client outlives level travel, so
	// setting it once here covers the session.
	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->SetSuppressTransitionMessage(true);
	}

	// The stored settings are the mixer's and the renderer's starting point; the front end never
	// gets a chance to apply them because it runs in a different world.
	if (USimCopterSettings* Settings = USimCopterSettings::Get(this))
	{
		Settings->ApplyAll(this);
	}

	// Bound for the whole session rather than only while the panel is up, because a take outlives
	// the panel: Tab lowers the panel and the recording carries on, and the REC indicator has to
	// appear and disappear with the take rather than with the panel.
	if (USimCopterReplaySubsystem* Replay = ResolveReplay())
	{
		ReplayStateChangedHandle = Replay->OnStateChanged().AddUObject(
			this, &ASimCopterPlayerController::HandleReplayStateChanged);
	}
}

void ASimCopterPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (USimCopterReplaySubsystem* Replay = ResolveReplay())
	{
		Replay->OnStateChanged().Remove(ReplayStateChangedHandle);
	}
	ReplayStateChangedHandle.Reset();

	// Before CloseScreen, because closing the replay panel resumes the world it paused and that
	// has to happen while the reference-counted pause still has a controller to answer to.
	CloseReplayPanel();
	RemoveRecordingIndicator();
	CloseScreen();
	Super::EndPlay(EndPlayReason);
}

void ASimCopterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent == nullptr)
	{
		return;
	}

	// bExecuteWhenPaused, because the screen pauses the sim and then has to be closable.
	FInputActionBinding& Binding = InputComponent->BindAction(
		SettingsAction, IE_Pressed, this, &ASimCopterPlayerController::SimSettings);
	Binding.bExecuteWhenPaused = true;

	BindReplayInput(*InputComponent, *this);
}

bool ASimCopterPlayerController::IsReplayExclusiveAction(const FName ActionName)
{
	// The only two keys in the whole feature that nothing else binds.
	return ActionName == FName(ReplayPanelAction) || ActionName == FName(ReplayHideHudAction);
}

void ASimCopterPlayerController::BindReplayInput(
	UInputComponent& Component,
	ASimCopterPlayerController& Owner)
{
	// Every replay binding lives on the controller rather than on the pawn, and every one of them
	// runs while paused. Reviewing a clip pauses the whole sim, so a binding on the pawn would go
	// quiet exactly when the panel needs it - and the panel has to work whether the player is in
	// the helicopter, on foot, or in neither.
	//
	// bConsumeInput = false ON EVERY ONE OF THEM THAT SHARES A KEY WITH GAMEPLAY, and this is not
	// optional. "Controlled pawn gets last dibs on the input stack" (APlayerController::
	// BuildInputStack): the controller's InputComponent is processed FIRST, and both action and
	// axis bindings consume their keys by default. Binding the flight axes here therefore ate
	// W/A/S/D, Space, Ctrl, the mouse look, the wheel, C and the right button before the pawn ever
	// saw them - the helicopter stopped answering the controls entirely, with no panel involved.
	//
	// The free camera does not need exclusivity anyway: while it is live the pawn's input is
	// blocked outright (UpdateFreeCameraInputSuppression), and while it is not, these handlers
	// do nothing.
	const auto BindSharedAction = [&Component, &Owner](
		const TCHAR* ActionName,
		const EInputEvent Event,
		void (ASimCopterPlayerController::*Handler)())
	{
		FInputActionBinding& ActionBinding = Component.BindAction(ActionName, Event, &Owner, Handler);
		ActionBinding.bExecuteWhenPaused = true;
		ActionBinding.bConsumeInput = false;
	};
	const auto BindSharedAxis = [&Component, &Owner](
		const TCHAR* AxisName,
		void (ASimCopterPlayerController::*Handler)(float))
	{
		FInputAxisBinding& AxisBinding = Component.BindAxis(AxisName, &Owner, Handler);
		AxisBinding.bExecuteWhenPaused = true;
		AxisBinding.bConsumeInput = false;
	};

	// Tab and H are the panel's own keys and nothing else binds them, so they may consume.
	Component.BindAction(ReplayPanelAction, IE_Pressed, &Owner, &ASimCopterPlayerController::ToggleReplayPanel)
		.bExecuteWhenPaused = true;
	Component.BindAction(ReplayHideHudAction, IE_Pressed, &Owner, &ASimCopterPlayerController::ToggleReplayHud)
		.bExecuteWhenPaused = true;

	BindSharedAction(CycleCameraAction, IE_Pressed, &ASimCopterPlayerController::ReplayCycleCamera);
	BindSharedAction(CameraDragAction, IE_Pressed, &ASimCopterPlayerController::FreeCameraLookPressed);
	BindSharedAction(CameraDragAction, IE_Released, &ASimCopterPlayerController::FreeCameraLookReleased);

	BindSharedAxis(ForwardAxis, &ASimCopterPlayerController::FreeCameraForward);
	BindSharedAxis(StrafeAxis, &ASimCopterPlayerController::FreeCameraStrafe);
	BindSharedAxis(VerticalAxis, &ASimCopterPlayerController::FreeCameraVertical);
	BindSharedAxis(LookYawAxis, &ASimCopterPlayerController::FreeCameraLookYaw);
	BindSharedAxis(LookPitchAxis, &ASimCopterPlayerController::FreeCameraLookPitch);
	BindSharedAxis(ZoomAxis, &ASimCopterPlayerController::FreeCameraFov);

	// The transport keys. They live here rather than in a Slate key handler because THE PANEL NEVER
	// TAKES KEYBOARD FOCUS - see SSimCopterReplayPanel::SupportsKeyboardFocus - so a key handler on
	// it would never fire. Each is inert unless a clip is actually being reviewed, which is the
	// only time the world is paused and these keys are not doing their gameplay job.
	BindSharedAction(TransportPlayPauseAction, IE_Pressed, &ASimCopterPlayerController::ReplayTogglePlayPause);
	BindSharedAction(TransportStepBackAction, IE_Pressed, &ASimCopterPlayerController::ReplayStepBack);
	BindSharedAction(TransportStepForwardAction, IE_Pressed, &ASimCopterPlayerController::ReplayStepForward);
	BindSharedAction(TransportGoToStartAction, IE_Pressed, &ASimCopterPlayerController::ReplayGoToStart);
	BindSharedAction(TransportBookmarkAction, IE_Pressed, &ASimCopterPlayerController::ReplayAddBookmark);
}

// ---------------------------------------------------------------------------------------------
// The replay panel (Tab)
//
// NOT a port - the original has no replay. See Docs/memory/simcopter-replay-clips.md.
// ---------------------------------------------------------------------------------------------

USimCopterReplaySubsystem* ASimCopterPlayerController::ResolveReplay() const
{
	return USimCopterReplaySubsystem::Get(this);
}

void ASimCopterPlayerController::SimReplay()
{
	ToggleReplayPanel();
}

void ASimCopterPlayerController::SimReplayStatus()
{
	const USimCopterReplaySubsystem* Replay = ResolveReplay();

	// Who owns the keyboard is the first thing to check for any "the controls stopped answering"
	// report: gameplay axis bindings only fire while the game viewport holds focus, and anything in
	// Slate that accepts focus takes it away on a single click.
	FString FocusedWidget = TEXT("<slate not initialised>");
	if (FSlateApplication::IsInitialized())
	{
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(0);
		FocusedWidget = Focused.IsValid() ? Focused->GetTypeAsString() : FString(TEXT("<none>"));
	}

	UE_LOG(
		LogSimCopterReplay,
		Log,
		TEXT("SimReplayStatus: panelWidget=%d pawnInputBlocked=%d lookHeld=%d focus='%s' | %s"),
		ReplayPanelWidget.IsValid() ? 1 : 0,
		PawnWithSuppressedInput.IsValid() ? 1 : 0,
		bFreeCameraLookHeld ? 1 : 0,
		*FocusedWidget,
		Replay != nullptr ? *Replay->DescribeState() : TEXT("<no replay subsystem>"));
}

void ASimCopterPlayerController::SimReplayDump()
{
	const USimCopterReplaySubsystem* Replay = ResolveReplay();
	if (Replay == nullptr)
	{
		UE_LOG(LogSimCopterReplay, Log, TEXT("SimReplayDump: no replay subsystem."));
		return;
	}

	TArray<FString> Lines;
	Replay->DumpTracks(Lines);
	for (const FString& Line : Lines)
	{
		UE_LOG(LogSimCopterReplay, Log, TEXT("%s"), *Line);
	}
}

void ASimCopterPlayerController::HandleReplayStateChanged()
{
	UpdateFreeCameraInputSuppression();
	UpdateRecordingIndicator();
	UpdateReplayKeyboardFocus();
}

void ASimCopterPlayerController::UpdateReplayKeyboardFocus()
{
	const USimCopterReplaySubsystem* Replay = ResolveReplay();
	const ESimCopterReplayState NewState = Replay != nullptr
		? Replay->GetState()
		: ESimCopterReplayState::Idle;
	if (NewState == LastObservedReplayState)
	{
		return;
	}

	LastObservedReplayState = NewState;
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	// The keyboard belongs to the GAME VIEWPORT at all times - the panel is mouse-only and every
	// shortcut it needs is a controller binding. This is only a backstop for a stray focus grab;
	// the panel is not supposed to be able to take it in the first place.
	//
	// The single exception is the player typing a clip name, which is the one place in the panel
	// that legitimately holds the keyboard.
	const SSimCopterReplayPanel* Panel = static_cast<const SSimCopterReplayPanel*>(ReplayPanelWidget.Get());
	if (Panel != nullptr && Panel->IsTypingClipName())
	{
		return;
	}

	FSlateApplication::Get().SetAllUserFocusToGameViewport();
	UE_LOG(LogSimCopterReplay, Verbose, TEXT("Keyboard focus -> game viewport."));
}

void ASimCopterPlayerController::ToggleReplayPanel()
{
	UE_LOG(
		LogSimCopterReplay,
		Log,
		TEXT("Replay panel toggle requested (open=%d, settingsOpen=%d)."),
		IsReplayPanelOpen() ? 1 : 0,
		IsSettingsOpen() ? 1 : 0);

	if (IsReplayPanelOpen())
	{
		CloseReplayPanel();
		return;
	}

	// The Settings screen is a real modal and owns the whole viewport; opening the replay panel
	// underneath it would put two things on screen with a claim on the keyboard.
	if (IsSettingsOpen())
	{
		return;
	}
	OpenReplayPanel();
}

void ASimCopterPlayerController::OpenReplayPanel()
{
	USimCopterReplaySubsystem* Replay = ResolveReplay();
	if (Replay == nullptr || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	Replay->OpenPanel();

	TSharedRef<SSimCopterReplayPanel> Panel =
		SNew(SSimCopterReplayPanel)
		.Replay(Replay)
		// Tab and the panel's CLOSE button both come back out here, because lowering the panel also
		// has to restore the input mode - which is this controller's to give back, not the widget's.
		.OnRequestClose(FSimpleDelegate::CreateUObject(
			this, &ASimCopterPlayerController::CloseReplayPanel));
	ReplayPanelWidget = Panel;
	// Above the cockpit overlays (25) but below the Settings screen (200): Escape must still be
	// able to raise Settings over the top of a review.
	GEngine->GameViewport->AddViewportWidgetContent(Panel, 80);

	// GameAndUI, not UIOnly, and with NO widget to focus. The panel needs the pointer for its
	// transport and its scrub bar, but the KEYBOARD has to stay on the game viewport: this is the
	// same mode the cockpit runs in normally, and the player must be able to keep flying with the
	// panel up - that is the whole point of recording by playing.
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}

	LastObservedReplayState = Replay->GetState();
	UpdateFreeCameraInputSuppression();
	UpdateRecordingIndicator();
}

void ASimCopterPlayerController::CloseReplayPanel()
{
	if (!ReplayPanelWidget.IsValid())
	{
		return;
	}

	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ReplayPanelWidget.ToSharedRef());
	}
	ReplayPanelWidget.Reset();

	// ClosePanel leaves any review (which resumes the world), puts the HUD back and hands the camera
	// to the possessed pawn. A RUNNING TAKE IS LEFT RUNNING - the subscription stays live for the
	// session, so the REC indicator appears as soon as the panel is gone.
	if (USimCopterReplaySubsystem* Replay = ResolveReplay())
	{
		Replay->ClosePanel();
	}

	bFreeCameraLookHeld = false;
	UpdateFreeCameraInputSuppression();
	UpdateRecordingIndicator();
	RestoreGameInput();
}

void ASimCopterPlayerController::UpdateRecordingIndicator()
{
	const USimCopterReplaySubsystem* Replay = ResolveReplay();
	// Shown while a take is running OR a clip is being reviewed, but only with the panel DOWN -
	// with the panel up its state line already says so, and two of them at once is noise.
	//
	// The review case matters more than the recording one: Tab hides the panel without ending the
	// review, so without this the player would be sitting in a paused world with nothing on screen
	// explaining why or how to get out.
	const bool bWanted = Replay != nullptr
		&& !Replay->IsPanelOpen()
		&& !Replay->IsHudHidden()
		&& (Replay->GetState() == ESimCopterReplayState::Recording
			|| Replay->GetState() == ESimCopterReplayState::Reviewing);

	if (!bWanted)
	{
		RemoveRecordingIndicator();
		return;
	}
	if (RecordingIndicatorWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	TWeakObjectPtr<USimCopterReplaySubsystem> WeakReplay(const_cast<USimCopterReplaySubsystem*>(Replay));
	TSharedRef<SWidget> Indicator =
		SNew(SBox)
		// Viewport content covers the whole screen; a status light must not eat the player's clicks.
		.Visibility(EVisibility::HitTestInvisible)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0.0f, 18.0f, 18.0f, 0.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.02f, 0.03f, 0.05f, 0.72f))
			.Padding(FMargin(10.0f, 5.0f))
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12))
				.ColorAndOpacity_Lambda([WeakReplay]()
				{
					const USimCopterReplaySubsystem* Live = WeakReplay.Get();
					const bool bRecording = Live != nullptr
						&& Live->GetState() == ESimCopterReplayState::Recording;
					// Red for a take in progress, amber for a clip being watched.
					return FSlateColor(bRecording
						? FLinearColor(0.92f, 0.24f, 0.24f, 1.0f)
						: FLinearColor(0.98f, 0.72f, 0.22f, 1.0f));
				})
				.Text_Lambda([WeakReplay]()
				{
					const USimCopterReplaySubsystem* Live = WeakReplay.Get();
					if (Live == nullptr)
					{
						return FText::GetEmpty();
					}

					const auto Timecode = [](const float Seconds)
					{
						const int32 Minutes = FMath::FloorToInt(FMath::Max(Seconds, 0.0f) / 60.0f);
						return FString::Printf(
							TEXT("%d:%05.2f"),
							Minutes,
							FMath::Max(Seconds, 0.0f) - static_cast<float>(Minutes) * 60.0f);
					};

					if (Live->GetState() == ESimCopterReplayState::Recording)
					{
						return FText::FromString(FString::Printf(
							TEXT("● REC  %s      Tab for controls"),
							*Timecode(Live->GetRecordedSeconds())));
					}
					return FText::FromString(FString::Printf(
						TEXT("%s REPLAY  %s / %s      Tab for controls"),
						Live->IsPlaying() ? TEXT("▶") : TEXT("‖"),
						*Timecode(Live->GetPlayheadSeconds()),
						*Timecode(Live->GetClipDurationSeconds())));
				})
			]
		];

	RecordingIndicatorWidget = Indicator;
	// Above the cockpit overlays, below the panel: it is a status light, not a control.
	GEngine->GameViewport->AddViewportWidgetContent(Indicator, 70);
}

void ASimCopterPlayerController::RemoveRecordingIndicator()
{
	if (!RecordingIndicatorWidget.IsValid())
	{
		return;
	}
	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(RecordingIndicatorWidget.ToSharedRef());
	}
	RecordingIndicatorWidget.Reset();
}

void ASimCopterPlayerController::ToggleReplayHud()
{
	// H only means anything while the panel is up; outside it the key is unbound, so the HUD can
	// never be left hidden with no way to get it back.
	USimCopterReplaySubsystem* Replay = ResolveReplay();
	if (Replay != nullptr && Replay->IsPanelOpen())
	{
		Replay->ToggleHudHidden();
	}
}

void ASimCopterPlayerController::ReplayCycleCamera()
{
	USimCopterReplaySubsystem* Replay = ResolveReplay();
	if (Replay == nullptr || !Replay->IsPanelOpen())
	{
		// Panel closed: C is the helicopter's own view cycle and the pawn's binding handles it.
		return;
	}
	Replay->CycleCameraView();
	UpdateFreeCameraInputSuppression();
}

// --- the review transport ---
//
// All five refuse unless a clip is actually being reviewed. While the world is running these keys
// belong to the game (Space is collective, the arrows are roll, Home resets the aircraft) and the
// bindings do not consume, so the pawn gets them exactly as it always did.

void ASimCopterPlayerController::ReplayTogglePlayPause()
{
	USimCopterReplaySubsystem* Replay = ResolveReplay();
	if (Replay != nullptr && Replay->GetState() == ESimCopterReplayState::Reviewing)
	{
		Replay->TogglePlayPause();
	}
}

void ASimCopterPlayerController::ReplayStepBack()
{
	ReplayStepFrames(-1);
}

void ASimCopterPlayerController::ReplayStepForward()
{
	ReplayStepFrames(1);
}

void ASimCopterPlayerController::ReplayStepFrames(const int32 FrameDelta)
{
	USimCopterReplaySubsystem* Replay = ResolveReplay();
	if (Replay == nullptr || Replay->GetState() != ESimCopterReplayState::Reviewing)
	{
		return;
	}
	// Stepping is how you find the exact instant something happened, so it always pauses first -
	// otherwise the step is immediately overwritten by the next frame of playback.
	Replay->Pause();
	Replay->SetPlayheadSeconds(
		Replay->GetPlayheadSeconds() + SimCopterReplay::FrameIntervalSeconds * static_cast<float>(FrameDelta));
}

void ASimCopterPlayerController::ReplayGoToStart()
{
	USimCopterReplaySubsystem* Replay = ResolveReplay();
	if (Replay != nullptr && Replay->GetState() == ESimCopterReplayState::Reviewing)
	{
		Replay->GoToStart();
	}
}

void ASimCopterPlayerController::ReplayAddBookmark()
{
	// The one transport key that is also useful mid-take: marking the moment something happened is
	// exactly what you want while flying, not afterwards.
	USimCopterReplaySubsystem* Replay = ResolveReplay();
	if (Replay != nullptr && Replay->IsPanelOpen())
	{
		Replay->AddBookmark(TEXT("Mark"));
	}
}

void ASimCopterPlayerController::UpdateFreeCameraInputSuppression()
{
	const USimCopterReplaySubsystem* Replay = ResolveReplay();
	const bool bShouldSuppress = Replay != nullptr && Replay->IsFreeCameraActive();
	APawn* CurrentlySuppressed = PawnWithSuppressedInput.Get();

	// Restore first, and restore the pawn that was actually blocked - which may no longer be the
	// possessed one, if the player climbed out of the helicopter while the free camera was live.
	if (CurrentlySuppressed != nullptr && (!bShouldSuppress || CurrentlySuppressed != GetPawn()))
	{
		CurrentlySuppressed->EnableInput(this);
		PawnWithSuppressedInput.Reset();
		CurrentlySuppressed = nullptr;
		UE_LOG(LogSimCopterReplay, Verbose, TEXT("Pawn input restored after free camera."));
	}

	// Without this, W both pitches the helicopter and flies the camera - very obvious while
	// recording, because the take then contains the aircraft lurching every time the operator
	// moved the shot.
	if (bShouldSuppress && CurrentlySuppressed == nullptr)
	{
		if (APawn* ControlledPawn = GetPawn())
		{
			ControlledPawn->DisableInput(this);
			PawnWithSuppressedInput = ControlledPawn;
			UE_LOG(LogSimCopterReplay, Verbose, TEXT("Pawn input blocked for free camera."));
		}
	}
}

// --- free-camera input ---

ASimCopterReplayFreeCamera* ASimCopterPlayerController::ResolveActiveFreeCamera() const
{
	const USimCopterReplaySubsystem* Replay = ResolveReplay();
	return (Replay != nullptr && Replay->IsFreeCameraActive()) ? Replay->GetFreeCamera() : nullptr;
}

void ASimCopterPlayerController::FreeCameraForward(const float Value)
{
	if (ASimCopterReplayFreeCamera* Camera = ResolveActiveFreeCamera())
	{
		Camera->AddMoveInput(FVector(Value, 0.0f, 0.0f));
	}
}

void ASimCopterPlayerController::FreeCameraStrafe(const float Value)
{
	if (ASimCopterReplayFreeCamera* Camera = ResolveActiveFreeCamera())
	{
		Camera->AddMoveInput(FVector(0.0f, Value, 0.0f));
	}
}

void ASimCopterPlayerController::FreeCameraVertical(const float Value)
{
	// SimCopterCollective is Space at +1 and LeftCtrl at -1, which is already "up" and "down".
	if (ASimCopterReplayFreeCamera* Camera = ResolveActiveFreeCamera())
	{
		Camera->AddMoveInput(FVector(0.0f, 0.0f, Value));
	}
}

void ASimCopterPlayerController::FreeCameraLookYaw(const float Value)
{
	// Look is gated on the right mouse button because the panel needs a visible cursor to be
	// clickable, and a cursor that also steers the camera makes the panel unusable.
	if (!bFreeCameraLookHeld || FMath::IsNearlyZero(Value))
	{
		return;
	}
	if (ASimCopterReplayFreeCamera* Camera = ResolveActiveFreeCamera())
	{
		Camera->AddLookInput(Value, 0.0f);
	}
}

void ASimCopterPlayerController::FreeCameraLookPitch(const float Value)
{
	if (!bFreeCameraLookHeld || FMath::IsNearlyZero(Value))
	{
		return;
	}
	if (ASimCopterReplayFreeCamera* Camera = ResolveActiveFreeCamera())
	{
		// Negated. `SimCopterMouseLookPitch` is mapped at scale -1 because the helicopter's boom
		// views are a DRAG - pulling the mouse down drags the world down, which raises the camera.
		// A free camera is a head, not a drag handle: mouse up must look up.
		Camera->AddLookInput(0.0f, -Value);
	}
}

void ASimCopterPlayerController::FreeCameraFov(const float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}
	if (ASimCopterReplayFreeCamera* Camera = ResolveActiveFreeCamera())
	{
		Camera->AddFovInput(Value);
	}
}

void ASimCopterPlayerController::FreeCameraLookPressed()
{
	const USimCopterReplaySubsystem* Replay = ResolveReplay();
	bFreeCameraLookHeld = Replay != nullptr && Replay->IsFreeCameraActive();
}

void ASimCopterPlayerController::FreeCameraLookReleased()
{
	bFreeCameraLookHeld = false;
}

FString ASimCopterPlayerController::ResolveOriginalGameRoot()
{
	return SimCopterOriginalGame::ResolveRoot();
}

void ASimCopterPlayerController::SimSettings()
{
	// The original raises the page from command 0x3f and answers Escape on the page itself with
	// 0x3ea, so the key does not toggle - but a second press with the screen already up should
	// still close it rather than doing nothing.
	if (IsSettingsOpen())
	{
		CloseScreen();
		return;
	}

	OpenSettings();
}

// ---------------------------------------------------------------------------------------------
// The reference-counted pause, FUN_004346c0 / [vt+0x44]
// ---------------------------------------------------------------------------------------------

void ASimCopterPlayerController::PushPause()
{
	if (PauseDepth++ == 0)
	{
		SetPause(true);
	}
}

void ASimCopterPlayerController::PopPause()
{
	if (PauseDepth > 0 && --PauseDepth == 0)
	{
		SetPause(false);
	}
}

// ---------------------------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------------------------

bool ASimCopterPlayerController::IsUserGame() const
{
	const USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;

	// DAT_00518d50 == 1. A session entered directly (PIE into the city level) has no kind at all;
	// it plays a single SimCity file the way mode 1 does, so it counts as a user game.
	return Session == nullptr || Session->GetSessionKind() != ESimCopterSessionKind::Career;
}

ASimCopterMissionSystemActor* ASimCopterPlayerController::ResolveMissionSystem() const
{
	return Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));
}

void ASimCopterPlayerController::OpenSettings()
{
	PushPause();
	EnterScreen(ESimCopterSettingsScreen::Menu);
}

TSharedRef<SWidget> ASimCopterPlayerController::BuildScreen(const ESimCopterSettingsScreen NewScreen)
{
	switch (NewScreen)
	{
	case ESimCopterSettingsScreen::CitySettings:
	{
		FSimCopterCitySettingsValues Values;
		if (const ASimCopterMissionSystemActor* Missions = ResolveMissionSystem())
		{
			const SimCopterMissions::FSimCopterCareerCity& City = Missions->GetSessionCareerCity();
			Values.Difficulty = City.Difficulty;
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Values.Weights); ++Index)
			{
				Values.Weights[Index] = City.Weights[Index];
			}
		}

		return SNew(SSimCopterCitySettings)
			.Art(Art)
			.Values(Values)
			.OnAccepted(FOnSimCopterCitySettingsAccepted::CreateLambda(
				[this](const FSimCopterCitySettingsValues& Accepted)
				{
					// FUN_00440ec0 writes the eight values straight back into the live block.
					if (ASimCopterMissionSystemActor* Missions = ResolveMissionSystem())
					{
						SimCopterMissions::FSimCopterCareerCity City = Missions->GetSessionCareerCity();
						City.Difficulty = Accepted.Difficulty;
						for (int32 Index = 0; Index < UE_ARRAY_COUNT(Accepted.Weights); ++Index)
						{
							City.Weights[Index] = Accepted.Weights[Index];
						}
						Missions->SetSessionCareerCity(City);
					}
					EnterScreen(ESimCopterSettingsScreen::Menu);
				}))
			.OnCancelled(FSimpleDelegate::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}));
	}

	case ESimCopterSettingsScreen::Graphics:
		return SNew(SSimCopterGraphicsSettings)
			.Art(Art)
			.OnAccepted(FOnSimCopterGraphicsSettingsClosed::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}))
			.OnCancelled(FOnSimCopterGraphicsSettingsClosed::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}));

	case ESimCopterSettingsScreen::Sound:
	{
		USimCopterSettings* Settings = USimCopterSettings::Get(this);
		const USimCopterRadioSubsystem* Radio = USimCopterRadioSubsystem::Get(this);

		FSimCopterSoundSettingsValues Values;
		if (Settings != nullptr)
		{
			Values.GameVolume = Settings->GetGameVolume();
			Values.RadioVolume = Settings->GetRadioVolume();
			Values.RadioStation = Settings->GetRadioStation();
			Values.bDj = Settings->IsDjEnabled();
			Values.bCommercials = Settings->AreCommercialsEnabled();
			Values.bAutoQuiet = Settings->IsAutoQuietEnabled();
		}

		TArray<FString> CallSigns;
		if (Radio != nullptr)
		{
			for (const FSimCopterRadioStation& Station : Radio->GetStations())
			{
				CallSigns.Add(Station.CallSign);
			}
		}

		// The page previews as it is dragged, so both Preview and Accept push the same values -
		// Accept only adds the save.
		const auto Push = [this](const FSimCopterSoundSettingsValues& New)
		{
			if (USimCopterSettings* Store = USimCopterSettings::Get(this))
			{
				Store->SetGameVolume(New.GameVolume);
				Store->SetRadioVolume(New.RadioVolume);
				Store->SetRadioStation(New.RadioStation);
				Store->SetDjEnabled(New.bDj);
				Store->SetCommercialsEnabled(New.bCommercials);
				Store->SetAutoQuietEnabled(New.bAutoQuiet);
				Store->ApplyAll(this);
			}
		};

		return SNew(SSimCopterSoundSettings)
			.Art(Art)
			.Values(Values)
			.StationCount(Radio != nullptr ? Radio->GetStationCount() : 0)
			.StationCallSigns(CallSigns)
			.OnPreviewChanged(FOnSimCopterSoundSettingsAccepted::CreateLambda(Push))
			.OnAccepted(FOnSimCopterSoundSettingsAccepted::CreateLambda(
				[this, Push](const FSimCopterSoundSettingsValues& New)
				{
					Push(New);
					if (USimCopterSettings* Store = USimCopterSettings::Get(this))
					{
						Store->Save();
					}
					EnterScreen(ESimCopterSettingsScreen::Menu);
				}))
			.OnCancelled(FSimpleDelegate::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}));
	}

	case ESimCopterSettingsScreen::Controls:
		return SNew(SSimCopterControlSettings)
			.Art(Art)
			.OnAccepted(FOnSimCopterControlSettingsClosed::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}))
			.OnCancelled(FOnSimCopterControlSettingsClosed::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}));

	case ESimCopterSettingsScreen::SaveName:
	{
		USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this);
		const FString SuggestedName = Saves != nullptr
			? Saves->GetSuggestedSaveName(this)
			: FString(TEXT("Saved Game"));
		TSharedRef<SSimCopterSaveNameDialog> Dialog =
			SNew(SSimCopterSaveNameDialog)
			.Art(Art)
			.SuggestedName(SuggestedName)
			.OnAccepted(FOnSimCopterSaveNameAccepted::CreateUObject(
				this, &ASimCopterPlayerController::SaveAsName))
			.OnCancelled(FSimpleDelegate::CreateLambda([this]()
			{
				bLeaveAfterSaveAs = false;
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}));
		InitialFocusWidget = Dialog->GetInitialFocusWidget();
		return Dialog;
	}

	case ESimCopterSettingsScreen::Message:
		return SNew(SSimCopterMessageBox)
			.Art(Art)
			.Message(PendingMessage)
			.OnDismissed(FSimpleDelegate::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}));

	case ESimCopterSettingsScreen::Confirm:
		return SNew(SSimCopterMessageBox)
			.Art(Art)
			.Message(PendingMessage)
			.Confirm(true)
			.OnConfirmed(FSimpleDelegate::CreateLambda([this]() { ConfirmLeaveCity(); }))
			.OnDismissed(FSimpleDelegate::CreateLambda([this]()
			{
				EnterScreen(ESimCopterSettingsScreen::Menu);
			}));

	case ESimCopterSettingsScreen::SaveBeforeLeave:
		return SNew(SSimCopterMessageBox)
			.Art(Art)
			.Message(PendingMessage)
			.Confirm(true)
			.OnConfirmed(FSimpleDelegate::CreateLambda([this]() { HandleSaveBeforeLeave(); }))
			.OnDismissed(FSimpleDelegate::CreateLambda([this]() { LeaveCity(); }));

	default:
		return SNew(SSimCopterSettingsMenu)
			.Art(Art)
			.AllowCitySettings(IsUserGame())
			.OnItemChosen(FOnSimCopterSettingsItemChosen::CreateUObject(
				this, &ASimCopterPlayerController::HandleSettingsItem));
	}
}

void ASimCopterPlayerController::EnterScreen(const ESimCopterSettingsScreen NewScreen)
{
	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	// Only the widget changes; the pause stays where it is, because the screen as a whole is still
	// up and the original's counter is likewise untouched moving between its pages.
	if (ScreenWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ScreenWidget.ToSharedRef());
		ScreenWidget.Reset();
	}

	InitialFocusWidget.Reset();
	TSharedRef<SWidget> Content = BuildScreen(NewScreen);
	ScreenWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			Content
		];
	Screen = NewScreen;

	// Above the cockpit overlays, which sit at 25.
	GEngine->GameViewport->AddViewportWidgetContent(ScreenWidget.ToSharedRef(), 200);

	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetWidgetToFocus(InitialFocusWidget.IsValid() ? InitialFocusWidget.ToSharedRef() : Content);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

void ASimCopterPlayerController::CloseScreen()
{
	if (!ScreenWidget.IsValid())
	{
		return;
	}

	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ScreenWidget.ToSharedRef());
	}
	ScreenWidget.Reset();
	Screen = ESimCopterSettingsScreen::None;

	PopPause();
	RestoreGameInput();
}

void ASimCopterPlayerController::RestoreGameInput()
{
	// On foot the mouse drives look, so it should come back locked and hidden (view mode) -
	// the same setup ASimCopterOnFootPawn applies when it's possessed - rather than the
	// cockpit's free-roaming cursor.
	if (Cast<ASimCopterOnFootPawn>(GetPawn()) != nullptr)
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		return;
	}

	// The cockpit needs the pointer for the dash and the tool flaps, so it is GameAndUI rather
	// than GameOnly - the same mode the helicopter pawn sets when it is possessed.
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

// ---------------------------------------------------------------------------------------------
// FUN_0044c9e0's switch
// ---------------------------------------------------------------------------------------------

void ASimCopterPlayerController::ShowMessage(const FText& Message)
{
	PendingMessage = Message;
	EnterScreen(ESimCopterSettingsScreen::Message);
}

void ASimCopterPlayerController::HandleSettingsItem(const ESimCopterSettingsItem Item)
{
	switch (Item)
	{
	case ESimCopterSettingsItem::CitySettings:
		EnterScreen(ESimCopterSettingsScreen::CitySettings);
		return;

	case ESimCopterSettingsItem::Graphics:
		EnterScreen(ESimCopterSettingsScreen::Graphics);
		return;

	case ESimCopterSettingsItem::Sound:
		EnterScreen(ESimCopterSettingsScreen::Sound);
		return;

	case ESimCopterSettingsItem::Controls:
		EnterScreen(ESimCopterSettingsScreen::Controls);
		return;

	case ESimCopterSettingsItem::SaveGame:
		if (USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this);
			Saves != nullptr && Saves->HasCurrentSave())
		{
			SaveToCurrentSlot();
		}
		else
		{
			OpenSaveNameDialog(/*bLeaveAfterSave=*/false);
		}
		return;

	case ESimCopterSettingsItem::SaveGameAs:
		OpenSaveNameDialog(/*bLeaveAfterSave=*/false);
		return;

	case ESimCopterSettingsItem::LeaveCity:
		// FUN_004352f0(0, 11, 0x20002): a modal Yes/No on STRINGTABLE 11.
		PendingMessage = LOCTEXT("LeaveCityConfirm", "Are you sure you want to leave this city?");
		EnterScreen(ESimCopterSettingsScreen::Confirm);
		return;

	case ESimCopterSettingsItem::Continue:
		// [vt+0x44] resume, then close the page.
		CloseScreen();
		return;
	}
}

void ASimCopterPlayerController::LeaveCity()
{
	if (USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
			? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
			: nullptr)
	{
		Session->ClearPendingSession();
	}

	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		Audio->StopRadio();
		Audio->StopMusic();
	}

	CloseScreen();
	UGameplayStatics::OpenLevel(this, FName(USimCopterSessionSubsystem::GetMainMenuLevelName()));
}

void ASimCopterPlayerController::OpenSaveNameDialog(const bool bLeaveAfterSave)
{
	bLeaveAfterSaveAs = bLeaveAfterSave;
	EnterScreen(ESimCopterSettingsScreen::SaveName);
}

bool ASimCopterPlayerController::SaveToCurrentSlot()
{
	USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this);
	FString Error;
	if (Saves == nullptr || !Saves->SaveCurrentGame(this, Error))
	{
		ShowMessage(FText::FromString(Error.IsEmpty() ? TEXT("The game could not be saved.") : Error));
		return false;
	}

	ShowMessage(LOCTEXT("GameSaved", "Game saved!")); // STRINGTABLE 48
	return true;
}

void ASimCopterPlayerController::SaveAsName(const FString& SaveName)
{
	USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this);
	FString Error;
	if (Saves == nullptr || !Saves->SaveCurrentGameAs(this, SaveName, Error))
	{
		bLeaveAfterSaveAs = false;
		ShowMessage(FText::FromString(Error.IsEmpty() ? TEXT("The game could not be saved.") : Error));
		return;
	}

	if (bLeaveAfterSaveAs)
	{
		bLeaveAfterSaveAs = false;
		LeaveCity();
		return;
	}
	ShowMessage(LOCTEXT("GameSavedAs", "Game saved!")); // STRINGTABLE 48
}

void ASimCopterPlayerController::ConfirmLeaveCity()
{
	PendingMessage = LOCTEXT("SaveBeforeLeave", "Do you want to save the game?"); // STRINGTABLE 49
	EnterScreen(ESimCopterSettingsScreen::SaveBeforeLeave);
}

void ASimCopterPlayerController::HandleSaveBeforeLeave()
{
	USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this);
	if (Saves != nullptr && Saves->HasCurrentSave())
	{
		FString Error;
		if (Saves->SaveCurrentGame(this, Error))
		{
			LeaveCity();
			return;
		}
		ShowMessage(FText::FromString(Error.IsEmpty() ? TEXT("The game could not be saved.") : Error));
		return;
	}
	OpenSaveNameDialog(/*bLeaveAfterSave=*/true);
}

void ASimCopterPlayerController::SimSaveGame(const FString& SaveName)
{
	USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this);
	if (Saves == nullptr)
	{
		return;
	}

	FString Error;
	const bool bSaved = SaveName.IsEmpty()
		? Saves->SaveCurrentGame(this, Error)
		: Saves->SaveCurrentGameAs(this, SaveName, Error);
	if (!bSaved)
	{
		UE_LOG(LogTemp, Warning, TEXT("SimSaveGame: %s"), *Error);
	}
}

#undef LOCTEXT_NAMESPACE
