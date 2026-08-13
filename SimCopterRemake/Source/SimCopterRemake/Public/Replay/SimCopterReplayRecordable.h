// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Replay/SimCopterReplayTypes.h"
#include "UObject/Interface.h"
#include "SimCopterReplayRecordable.generated.h"

UINTERFACE(MinimalAPI)
class USimCopterReplayRecordable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by every actor the replay recorder can capture and play back.
 *
 * A reflected interface rather than the plain C++ one `ISimCopterBehaviorWorld` uses, for one
 * reason: the recorder sweeps the world with `TActorIterator<AActor>` and has to ask each actor
 * whether it takes part. `Cast<>` answers that off the UClass's interface list, and the module
 * builds without RTTI, so a plain interface could only be found by testing concrete classes one at
 * a time - which is exactly the coupling this avoids.
 *
 * None of the members are `UFUNCTION`s: they pass non-reflected types and are never reached from
 * Blueprint.
 *
 * A recordable is asked for its state at 20 Hz while a clip records, so `CaptureReplayState` must
 * be cheap and must not allocate. `ApplyReplayState` is called with the sim PAUSED - the actor's
 * own Tick is not running - so it has to put the actor where it is told outright rather than
 * setting a target for some other system to chase.
 */
class SIMCOPTERREMAKE_API ISimCopterReplayRecordable
{
	GENERATED_BODY()

public:
	virtual SimCopterReplay::EReplayActorKind GetReplayActorKind() const = 0;
	/** Short human label for the panel's actor list ("Robber", "Police Car"). */
	virtual FString GetReplayLabel() const = 0;
	/** Behaviour state for a person; `INDEX_NONE` when the actor has none. */
	virtual int32 GetReplayPersonState() const { return INDEX_NONE; }
	/** What playback needs to build a stand-in. Read once, when the track is opened. */
	virtual void GetReplaySpawnDescriptor(SimCopterReplay::FReplaySpawnDescriptor& OutDescriptor) const {}

	virtual void CaptureReplayState(
		SimCopterReplay::FReplayMnemonicTable& Mnemonics,
		SimCopterReplay::FReplayActorState& OutState) const = 0;

	virtual void ApplyReplayState(
		const SimCopterReplay::FReplayMnemonicTable& Mnemonics,
		const SimCopterReplay::FReplayActorState& State) = 0;

	/**
	 * Turns the actor into an inert stand-in: no behaviour, no collision, no audio, no despawn
	 * budget. Called once on a puppet right after it is spawned, and never on a live actor.
	 */
	virtual void BecomeReplayPuppet() {}

	/** Puts back whatever `ApplyReplayState` overrode. Called once when review ends. */
	virtual void EndReplayPlayback() {}
};
