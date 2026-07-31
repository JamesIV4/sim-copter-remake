// SimCopter sound table - the slot numbers the original registers into its sound manager.
//
// Ported from:
//   FUN_00424b70  the one-shot registration run: 130 calls of
//                 "build path -> new Sound(path, mode) -> manager.SetSlot(obj, id)".
//                 The manager is the global at 0x0055b1a8; slot pointers live at
//                 0x0055b1ac + id*4, which is why every play/stop wrapper indexes by id.
//   FUN_00433b20  the path builder. Its first argument is a directory index:
//                 2 = "sound\", 3 = "sound\" + the language folder ("English\").
//
// The ids are the ABI between the sim and the mixer: every call site in the original names its
// sound by one of these numbers, so the port keeps them verbatim rather than inventing an enum
// ordering of its own. See Docs/memory/simcopter-sound.md.

#pragma once

#include "CoreMinimal.h"

namespace SimCopterSound
{
	/** Directory a slot's WAV is registered against (FUN_00433b20's first argument). */
	enum class ESoundDir : uint8
	{
		Root,      // dir 2 -> sound\_
		Language,  // dir 3 -> sound\English\_
	};

	/** One registered slot. */
	struct FSoundSlot
	{
		const TCHAR* Wav = nullptr;
		ESoundDir Dir = ESoundDir::Root;
	};

	/** 0x00..0x81 inclusive: every id FUN_00424b70 registers. */
	inline constexpr int32 NumSlots = 0x82;

	/**
	 * Slots 0x71..0x7e are a bank, not a fixed sound. FUN_00424b70 seeds all fourteen with
	 * xWhoa.WAV in a `do { ... } while (i < 0x7f)` loop; at runtime FUN_004c5210 hands one out
	 * per speaking person and swaps its WAV with FUN_0042a100 (SetFile). A person stores its
	 * slot index in obj[0x172] and every caller recomputes the id as `obj[0x172] + 0x71`.
	 */
	inline constexpr int32 VoiceBankFirst = 0x71;
	inline constexpr int32 VoiceBankLast = 0x7e;
	inline constexpr int32 VoiceBankCount = VoiceBankLast - VoiceBankFirst + 1;

	/** Named ids, in registration order. Only ids the port actually references are named. */
	enum : int32
	{
		SND_COPLOOP  = 0x00, // rotor loop; the WAV is swapped per helicopter model
		SND_EXPLODE  = 0x01, // helicopter slams the ground
		SND_CHOPSTOP = 0x02, // engine shutdown / rotor spins down
		SND_CHOPSTAR = 0x03, // engine start / rotor spools up
		SND_BLDEXPL  = 0x04, // building explodes (burned down, or flown into)
		SND_MACHGUN1 = 0x05, // machine gun, looped while held
		SND_MISSILE  = 0x06, // missile launch
		SND_BOOM1    = 0x07, // generic impact / detonation
		SND_PUNCH3   = 0x08, // particle hit a person
		SND_KAPOW4   = 0x09, // particle hit a vehicle
		SND_SPLISH   = 0x0a, // water: bucket fill, douse splash
		SND_FIREMIS2 = 0x0b, // fire-extinguisher missile burst
		SND_MEVAC    = 0x0c, // medical evac (registered; no shipped call site)
		SND_FIRELP11 = 0x0d, // burning building loop
		SND_MOTOROLD = 0x0e, // damaged-engine warning
		SND_DOUSE    = 0x0f, // water hits a burnable tile / helicopter ditches
		SND_FIREDMG  = 0x10, // taking damage from flying through fire
		SND_AMBSRN11 = 0x11, // ambulance siren (looped, 3D)
		SND_FIRESIRE = 0x12, // fire engine siren (looped, 3D)
		SND_POLICESI = 0x13, // police siren (looped, 3D)
		SND_FIRHOSLP = 0x14, // fire hose loop (looped, 3D)
		SND_WINCHLP  = 0x15, // winch motor loop
		SND_SOFTBMP2 = 0x16, // soft touchdown / gentle bump
		SND_TGSHWH   = 0x17, // tear gas canister launch
		SND_TGPOP    = 0x18, // tear gas canister bursts
		SND_TRAIN1   = 0x19, // train loop (looped, 3D)
		SND_CRSH2    = 0x1a, // plane / train crash
		SND_DIVE1    = 0x1b, // airliner dive (looped, 3D)
		SND_CESSLP1  = 0x1c, // light plane loop (looped, 3D)
		SND_HELP1    = 0x1d, // "help!" shout
		SND_CA_CHING = 0x1e, // money awarded
		SND_WATERCAN = 0x1f, // water cannon, looped while held
		SND_AMBSRN2  = 0x20, // second ambulance siren voice: the crash-rescue flyby
		SND_CHEER    = 0x21, // crowd cheer
		SND_BOO      = 0x22, // crowd boo
		SND_UFO      = 0x23, // UFO loop (looped, 3D)
		SND_LASER    = 0x24, // UFO laser
		SND_DOROPN   = 0x25, // helicopter door open
		SND_DORCLS   = 0x26, // helicopter door close
		SND_HORN1    = 0x27, // car horn variant 1
		SND_HORN2    = 0x28, // car horn variant 2
		SND_HORN3    = 0x29, // car horn variant 3
		SND_ACCEL2   = 0x2a, // car pulling away
		SND_TSCREECH = 0x2b, // tyre screech
		SND_GASOUT   = 0x2c, // out of fuel
		SND_AL01     = 0x2d, // passenger scream 1
		SND_AL02     = 0x2e, // passenger scream 2

		SND_D1000    = 0x2f, // 0x2f..0x41 D1000..D1018 dispatcher lines
		SND_D1018    = 0x41,
		SND_L001     = 0x42, // 0x42..0x4a L001..L009 location lines
		SND_L009     = 0x4a,
		SND_D2001    = 0x4b, // 0x4b..0x5e D2001..D2020 dispatcher lines
		SND_D2020    = 0x5e,
		SND_DIS053   = 0x5f, // 0x5f..0x6e DIS053..DIS068 dispatcher lines
		SND_DIS068   = 0x6e,

		SND_ADROPEN  = 0x6f, // ground vehicle door open
		SND_ADRCLOSE = 0x70, // ground vehicle door close
		SND_BLIP1    = 0x7f, // HUD blip
		SND_NOEQUIP  = 0x80, // action refused: equipment not fitted
		SND_FIRESTAR = 0x81, // something catches fire
	};

	/** The registered table, indexed by id. Entries are never null for 0x00..0x81. */
	SIMCOPTERREMAKE_API TArrayView<const FSoundSlot> GetSlotTable();

	/** Null for out-of-range ids. */
	SIMCOPTERREMAKE_API const FSoundSlot* GetSlot(int32 Id);

	/** True for the 0x71..0x7e people-voice bank. */
	inline bool IsVoiceBankSlot(int32 Id)
	{
		return Id >= VoiceBankFirst && Id <= VoiceBankLast;
	}

	/**
	 * One people-voice event: the outer `switch (param_2)` of FUN_004c5210. The handler picks
	 * uniformly from Clips (the original calls FUN_004cea00(N), its own small-range RNG) and
	 * SetFile()s the winner into the speaker's bank slot. Clips live in sound\people\_.
	 */
	struct FVoiceEvent
	{
		int32 Event = 0;
		TArrayView<const TCHAR* const> Clips;
	};

	/** All 62 voice events, ascending by Event. */
	SIMCOPTERREMAKE_API TArrayView<const FVoiceEvent> GetVoiceEventTable();

	/** Null when the event id is not one of the 62. */
	SIMCOPTERREMAKE_API const FVoiceEvent* GetVoiceEvent(int32 Event);

	/**
	 * Voice event ids raised by FUN_004c6970, the animation-driven speech dispatcher.
	 * Only the events the port raises are named.
	 */
	enum : int32
	{
		VOX_ASSERT   = 1,    // assert1..4 - assertive chatter
		VOX_DUNNO    = 2,    // duno1..4
		VOX_HOHUM    = 3,    // hohum1..5 - idle
		VOX_QUERY    = 4,    // query1..2
		VOX_SAD      = 5,    // sad1..3
		VOX_TITTER   = 6,    // titer1..6
		VOX_GOGIRL   = 7,    // gogirl1..6
		VOX_GRUNT    = 8,    // grunt1..6 - exertion / shoved
		VOX_WHOA     = 9,    // woh1..4 - startled
		VOX_HEY      = 10,   // hey1..3
		VOX_HITHERE  = 11,   // hithere1..3 - greeting
		VOX_AH       = 12,   // ah1..2
		VOX_DYING    = 13,   // achdie1..3 - fatal injury
		VOX_ARREST   = 15,   // arrest1..2
		VOX_SINISTER = 17,   // sinistr1..4 - criminal
		VOX_BODYHIT  = 31,   // Bodyhit2 - struck by an object
		VOX_FALLING  = 32,   // FallWhsl - falling whistle
		VOX_UFOUP    = 33,   // UFOup - abduction
		VOX_OHDRAT   = 35,   // ohdrat
	};
}
