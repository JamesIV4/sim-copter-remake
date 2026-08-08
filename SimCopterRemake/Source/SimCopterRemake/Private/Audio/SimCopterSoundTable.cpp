#include "Audio/SimCopterSoundTable.h"

namespace SimCopterSound
{
namespace
{
	// SCHOOK: SoundRegisterAll 0x00424b70
	//
	// Transcribed from the registration run verbatim, in id order. Two entries deserve a note:
	//
	//   0x11 and 0x20 are BOTH ambsrn11.wav. The original really does register the same file
	//   twice, because a slot is a voice: 0x11 is the ambulance driving to a call and 0x20 is
	//   the rescue flyby that answers a crash, and they have to be able to sound at once.
	//
	//   0x1d HELP1 registers against the language folder (dir 3) although the retail install
	//   ships help1.wav in sound\ itself. The loader searches language-then-root for every
	//   slot, which resolves that and costs nothing for the entries that are where they claim.
	const FSoundSlot GSlots[NumSlots] = {
		{ TEXT("COPLOOP"), ESoundDir::Root },       // 0x00
		{ TEXT("EXPLODE"), ESoundDir::Root },       // 0x01
		{ TEXT("CHOPSTOP"), ESoundDir::Root },      // 0x02
		{ TEXT("CHOPSTAR"), ESoundDir::Root },      // 0x03
		{ TEXT("BLDEXPL"), ESoundDir::Root },       // 0x04
		{ TEXT("MACHGUN1"), ESoundDir::Root },      // 0x05
		{ TEXT("MISSILE"), ESoundDir::Root },       // 0x06
		{ TEXT("BOOM1"), ESoundDir::Root },         // 0x07
		{ TEXT("PUNCH3"), ESoundDir::Root },        // 0x08
		{ TEXT("KAPOW4"), ESoundDir::Root },        // 0x09
		{ TEXT("SPLISH"), ESoundDir::Root },        // 0x0a
		{ TEXT("FIREMIS2"), ESoundDir::Root },      // 0x0b
		{ TEXT("MEVAC"), ESoundDir::Root },         // 0x0c
		{ TEXT("FIRELP11"), ESoundDir::Root },      // 0x0d
		{ TEXT("MOTOROLD"), ESoundDir::Root },      // 0x0e
		{ TEXT("DOUSE"), ESoundDir::Root },         // 0x0f
		{ TEXT("FIREDMG"), ESoundDir::Root },       // 0x10
		{ TEXT("AMBSRN11"), ESoundDir::Root },      // 0x11
		{ TEXT("FIRESIRE"), ESoundDir::Root },      // 0x12
		{ TEXT("POLICESI"), ESoundDir::Root },      // 0x13
		{ TEXT("FIRHOSLP"), ESoundDir::Root },      // 0x14
		{ TEXT("WINCHLP"), ESoundDir::Root },       // 0x15
		{ TEXT("SOFTBMP2"), ESoundDir::Root },      // 0x16
		{ TEXT("TGSHWH"), ESoundDir::Root },        // 0x17
		{ TEXT("TGPOP"), ESoundDir::Root },         // 0x18
		{ TEXT("TRAIN1"), ESoundDir::Root },        // 0x19
		{ TEXT("CRSH2"), ESoundDir::Root },         // 0x1a
		{ TEXT("DIVE1"), ESoundDir::Root },         // 0x1b
		{ TEXT("CESSLP1"), ESoundDir::Root },       // 0x1c
		{ TEXT("HELP1"), ESoundDir::Language },     // 0x1d
		{ TEXT("CA_CHING"), ESoundDir::Root },      // 0x1e
		{ TEXT("WATERCAN"), ESoundDir::Root },      // 0x1f
		{ TEXT("AMBSRN11"), ESoundDir::Root },      // 0x20
		{ TEXT("CHEER"), ESoundDir::Root },         // 0x21
		{ TEXT("BOO"), ESoundDir::Root },           // 0x22
		{ TEXT("UFO"), ESoundDir::Root },           // 0x23
		{ TEXT("LASER"), ESoundDir::Root },         // 0x24
		{ TEXT("DOROPN"), ESoundDir::Root },        // 0x25
		{ TEXT("DORCLS"), ESoundDir::Root },        // 0x26
		{ TEXT("HORN1"), ESoundDir::Root },         // 0x27
		{ TEXT("HORN2"), ESoundDir::Root },         // 0x28
		{ TEXT("HORN3"), ESoundDir::Root },         // 0x29
		{ TEXT("ACCEL2"), ESoundDir::Root },        // 0x2a
		{ TEXT("TSCREECH"), ESoundDir::Root },      // 0x2b
		{ TEXT("GASOUT"), ESoundDir::Root },        // 0x2c
		{ TEXT("AL01"), ESoundDir::Root },          // 0x2d
		{ TEXT("AL02"), ESoundDir::Root },          // 0x2e
		{ TEXT("D1000"), ESoundDir::Language },     // 0x2f
		{ TEXT("D1001"), ESoundDir::Language },     // 0x30
		{ TEXT("D1002"), ESoundDir::Language },     // 0x31
		{ TEXT("D1003"), ESoundDir::Language },     // 0x32
		{ TEXT("D1004"), ESoundDir::Language },     // 0x33
		{ TEXT("D1005"), ESoundDir::Language },     // 0x34
		{ TEXT("D1006"), ESoundDir::Language },     // 0x35
		{ TEXT("D1007"), ESoundDir::Language },     // 0x36
		{ TEXT("D1008"), ESoundDir::Language },     // 0x37
		{ TEXT("D1009"), ESoundDir::Language },     // 0x38
		{ TEXT("D1010"), ESoundDir::Language },     // 0x39
		{ TEXT("D1011"), ESoundDir::Language },     // 0x3a
		{ TEXT("D1012"), ESoundDir::Language },     // 0x3b
		{ TEXT("D1013"), ESoundDir::Language },     // 0x3c
		{ TEXT("D1014"), ESoundDir::Language },     // 0x3d
		{ TEXT("D1015"), ESoundDir::Language },     // 0x3e
		{ TEXT("D1016"), ESoundDir::Language },     // 0x3f
		{ TEXT("D1017"), ESoundDir::Language },     // 0x40
		{ TEXT("D1018"), ESoundDir::Language },     // 0x41
		{ TEXT("L001"), ESoundDir::Language },      // 0x42
		{ TEXT("L002"), ESoundDir::Language },      // 0x43
		{ TEXT("L003"), ESoundDir::Language },      // 0x44
		{ TEXT("L004"), ESoundDir::Language },      // 0x45
		{ TEXT("L005"), ESoundDir::Language },      // 0x46
		{ TEXT("L006"), ESoundDir::Language },      // 0x47
		{ TEXT("L007"), ESoundDir::Language },      // 0x48
		{ TEXT("L008"), ESoundDir::Language },      // 0x49
		{ TEXT("L009"), ESoundDir::Language },      // 0x4a
		{ TEXT("D2001"), ESoundDir::Language },     // 0x4b
		{ TEXT("D2002"), ESoundDir::Language },     // 0x4c
		{ TEXT("D2003"), ESoundDir::Language },     // 0x4d
		{ TEXT("D2004"), ESoundDir::Language },     // 0x4e
		{ TEXT("D2005"), ESoundDir::Language },     // 0x4f
		{ TEXT("D2006"), ESoundDir::Language },     // 0x50
		{ TEXT("D2007"), ESoundDir::Language },     // 0x51
		{ TEXT("D2008"), ESoundDir::Language },     // 0x52
		{ TEXT("D2009"), ESoundDir::Language },     // 0x53
		{ TEXT("D2010"), ESoundDir::Language },     // 0x54
		{ TEXT("D2011"), ESoundDir::Language },     // 0x55
		{ TEXT("D2012"), ESoundDir::Language },     // 0x56
		{ TEXT("D2013"), ESoundDir::Language },     // 0x57
		{ TEXT("D2014"), ESoundDir::Language },     // 0x58
		{ TEXT("D2015"), ESoundDir::Language },     // 0x59
		{ TEXT("D2016"), ESoundDir::Language },     // 0x5a
		{ TEXT("D2017"), ESoundDir::Language },     // 0x5b
		{ TEXT("D2018"), ESoundDir::Language },     // 0x5c
		{ TEXT("D2019"), ESoundDir::Language },     // 0x5d
		{ TEXT("D2020"), ESoundDir::Language },     // 0x5e
		{ TEXT("DIS053"), ESoundDir::Language },    // 0x5f
		{ TEXT("DIS054"), ESoundDir::Language },    // 0x60
		{ TEXT("DIS055"), ESoundDir::Language },    // 0x61
		{ TEXT("DIS056"), ESoundDir::Language },    // 0x62
		{ TEXT("DIS057"), ESoundDir::Language },    // 0x63
		{ TEXT("DIS058"), ESoundDir::Language },    // 0x64
		{ TEXT("DIS059"), ESoundDir::Language },    // 0x65
		{ TEXT("DIS060"), ESoundDir::Language },    // 0x66
		{ TEXT("DIS061"), ESoundDir::Language },    // 0x67
		{ TEXT("DIS062"), ESoundDir::Language },    // 0x68
		{ TEXT("DIS063"), ESoundDir::Language },    // 0x69
		{ TEXT("DIS064"), ESoundDir::Language },    // 0x6a
		{ TEXT("DIS065"), ESoundDir::Language },    // 0x6b
		{ TEXT("DIS066"), ESoundDir::Language },    // 0x6c
		{ TEXT("DIS067"), ESoundDir::Language },    // 0x6d
		{ TEXT("DIS068"), ESoundDir::Language },    // 0x6e
		{ TEXT("aDrOpen"), ESoundDir::Root },       // 0x6f
		{ TEXT("aDrClose"), ESoundDir::Root },      // 0x70
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x71 people voice bank, seeded by the
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x72 `do { } while (i < 0x7f)` tail of
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x73 FUN_00424b70 and swapped at runtime
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x74 by FUN_004c5210 -> SetFile
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x75
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x76
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x77
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x78
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x79
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x7a
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x7b
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x7c
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x7d
		{ TEXT("xWhoa"), ESoundDir::Root },         // 0x7e
		{ TEXT("BLIP1"), ESoundDir::Root },         // 0x7f
		{ TEXT("NOEQUIP"), ESoundDir::Root },       // 0x80
		{ TEXT("FIRESTAR"), ESoundDir::Root },      // 0x81
		{ TEXT("D1019"), ESoundDir::Language },     // 0x82
		{ TEXT("D1020"), ESoundDir::Language },     // 0x83
	};

	// SCHOOK: PersonSpeak 0x004c5210 (the `switch (param_2)` clip picker)
	//
	// Extracted mechanically from the decompile; the generator is
	// Docs/scratchpad/sound/extract_people_voices.py. Clips live in sound\people\_.
	//
	// Two names Ghidra normalised: trbnc_ and trptf_ are trbnc#.WAV and trptf#.WAV on disk (the
	// sharp is a legal filename character but not a legal symbol one). The loader retries a
	// trailing '_' as '#', so they are written here as the symbol spelled them.
	//
	// Events 56 (MORITURI) and 62 (REPAIR) name clips the retail install does not ship; they are
	// kept because the table is evidence, and the loader simply finds nothing for them.
	const TCHAR* const GVox01[] = { TEXT("assert1"), TEXT("assert2"), TEXT("assert3"), TEXT("assert4") };
	const TCHAR* const GVox02[] = { TEXT("duno1"), TEXT("duno2"), TEXT("duno3"), TEXT("duno4") };
	const TCHAR* const GVox03[] = { TEXT("hohum1"), TEXT("hohum2"), TEXT("hohum3"), TEXT("hohum4"), TEXT("hohum5") };
	const TCHAR* const GVox04[] = { TEXT("query1"), TEXT("query2") };
	const TCHAR* const GVox05[] = { TEXT("sad1"), TEXT("sad2"), TEXT("sad3") };
	const TCHAR* const GVox06[] = { TEXT("titer1"), TEXT("titer2"), TEXT("titer3"), TEXT("titer4"), TEXT("titer5"), TEXT("titer6") };
	const TCHAR* const GVox07[] = { TEXT("gogirl1"), TEXT("gogirl2"), TEXT("gogirl3"), TEXT("gogirl4"), TEXT("gogirl5"), TEXT("gogirl6") };
	const TCHAR* const GVox08[] = { TEXT("grunt1"), TEXT("grunt2"), TEXT("grunt3"), TEXT("grunt4"), TEXT("grunt5"), TEXT("grunt6") };
	const TCHAR* const GVox09[] = { TEXT("woh1"), TEXT("woh2"), TEXT("woh3"), TEXT("woh4") };
	const TCHAR* const GVox10[] = { TEXT("hey1"), TEXT("hey2"), TEXT("hey3") };
	const TCHAR* const GVox11[] = { TEXT("hithere1"), TEXT("hithere2"), TEXT("hithere3") };
	const TCHAR* const GVox12[] = { TEXT("ah1"), TEXT("ah2") };
	const TCHAR* const GVox13[] = { TEXT("achdie1"), TEXT("achdie2"), TEXT("achdie3") };
	const TCHAR* const GVox14[] = { TEXT("xFtShoes") };
	const TCHAR* const GVox15[] = { TEXT("arrest1"), TEXT("arrest2") };
	const TCHAR* const GVox16[] = { TEXT("gimme1"), TEXT("gimme2") };
	const TCHAR* const GVox17[] = { TEXT("sinistr1"), TEXT("sinistr2"), TEXT("sinistr3"), TEXT("sinistr4") };
	const TCHAR* const GVox18[] = { TEXT("hicute1"), TEXT("hicute2"), TEXT("hicute3"), TEXT("hicute4") };
	const TCHAR* const GVox19[] = { TEXT("mdvc1") };
	const TCHAR* const GVox20[] = { TEXT("mdvc2") };
	const TCHAR* const GVox21[] = { TEXT("mdvc3") };
	const TCHAR* const GVox22[] = { TEXT("mdvc4") };
	const TCHAR* const GVox23[] = { TEXT("pol1") };
	const TCHAR* const GVox24[] = { TEXT("pol2") };
	const TCHAR* const GVox25[] = { TEXT("pol3") };
	const TCHAR* const GVox26[] = { TEXT("pol4") };
	const TCHAR* const GVox27[] = { TEXT("pol5") };
	const TCHAR* const GVox28[] = { TEXT("pol6") };
	const TCHAR* const GVox29[] = { TEXT("grntsplt") };
	const TCHAR* const GVox30[] = { TEXT("grnthit1"), TEXT("grnthit2") };
	const TCHAR* const GVox31[] = { TEXT("Bodyhit2") };
	const TCHAR* const GVox32[] = { TEXT("FallWhsl") };
	const TCHAR* const GVox33[] = { TEXT("UFOup") };
	const TCHAR* const GVox34[] = { TEXT("bowang") };
	const TCHAR* const GVox35[] = { TEXT("ohdrat") };
	const TCHAR* const GVox36[] = { TEXT("chewgum1") };
	const TCHAR* const GVox37[] = { TEXT("trbna"), TEXT("trbnc_"), TEXT("trbng"), TEXT("trptb"), TEXT("trpte"), TEXT("trptf_"), TEXT("tubab"), TEXT("tubae"), TEXT("tubaf_") };
	const TCHAR* const GVox38[] = { TEXT("march") };
	const TCHAR* const GVox39[] = { TEXT("kissA"), TEXT("kissB"), TEXT("kissG") };
	const TCHAR* const GVox40[] = { TEXT("xFtHeels") };
	const TCHAR* const GVox41[] = { TEXT("xFtBoots") };
	const TCHAR* const GVox42[] = { TEXT("Bnghyt"), TEXT("Bnggng"), TEXT("bngthrm") };
	const TCHAR* const GVox43[] = { TEXT("btchmp") };
	const TCHAR* const GVox44[] = { TEXT("hithead") };
	const TCHAR* const GVox45[] = { TEXT("spacidle") };
	const TCHAR* const GVox46[] = { TEXT("atomchg"), TEXT("ftrchkbr"), TEXT("hum"), TEXT("decphum"), TEXT("popon"), TEXT("trnsfrm"), TEXT("spacidle"), TEXT("pasfstB"), TEXT("passlwB") };
	const TCHAR* const GVox47[] = { TEXT("atomchg") };
	const TCHAR* const GVox48[] = { TEXT("ftrchkbr") };
	const TCHAR* const GVox49[] = { TEXT("hum") };
	const TCHAR* const GVox50[] = { TEXT("decphum") };
	const TCHAR* const GVox51[] = { TEXT("popon") };
	const TCHAR* const GVox52[] = { TEXT("trnsfrm") };
	const TCHAR* const GVox53[] = { TEXT("pasfstB") };
	const TCHAR* const GVox54[] = { TEXT("passlwB") };
	const TCHAR* const GVox55[] = { TEXT("doorah") };
	const TCHAR* const GVox56[] = { TEXT("MORITURI") };
	const TCHAR* const GVox57[] = { TEXT("hgun1"), TEXT("hgun2"), TEXT("hgun3") };
	const TCHAR* const GVox58[] = { TEXT("EKG") };
	const TCHAR* const GVox59[] = { TEXT("spray") };
	const TCHAR* const GVox60[] = { TEXT("doropn") };
	const TCHAR* const GVox61[] = { TEXT("slide") };
	const TCHAR* const GVox62[] = { TEXT("REPAIR") };

	// Two parameters, not one: the array suffix is zero-padded to keep the declarations above
	// aligned, and a bare 08 / 09 in the id column would be an invalid octal literal.
	#define SC_VOX(Id, Suffix) { Id, MakeArrayView(GVox##Suffix) }
	const FVoiceEvent GVoiceEvents[] = {
		SC_VOX( 1, 01), SC_VOX( 2, 02), SC_VOX( 3, 03), SC_VOX( 4, 04), SC_VOX( 5, 05),
		SC_VOX( 6, 06), SC_VOX( 7, 07), SC_VOX( 8, 08), SC_VOX( 9, 09), SC_VOX(10, 10),
		SC_VOX(11, 11), SC_VOX(12, 12), SC_VOX(13, 13), SC_VOX(14, 14), SC_VOX(15, 15),
		SC_VOX(16, 16), SC_VOX(17, 17), SC_VOX(18, 18), SC_VOX(19, 19), SC_VOX(20, 20),
		SC_VOX(21, 21), SC_VOX(22, 22), SC_VOX(23, 23), SC_VOX(24, 24), SC_VOX(25, 25),
		SC_VOX(26, 26), SC_VOX(27, 27), SC_VOX(28, 28), SC_VOX(29, 29), SC_VOX(30, 30),
		SC_VOX(31, 31), SC_VOX(32, 32), SC_VOX(33, 33), SC_VOX(34, 34), SC_VOX(35, 35),
		SC_VOX(36, 36), SC_VOX(37, 37), SC_VOX(38, 38), SC_VOX(39, 39), SC_VOX(40, 40),
		SC_VOX(41, 41), SC_VOX(42, 42), SC_VOX(43, 43), SC_VOX(44, 44), SC_VOX(45, 45),
		SC_VOX(46, 46), SC_VOX(47, 47), SC_VOX(48, 48), SC_VOX(49, 49), SC_VOX(50, 50),
		SC_VOX(51, 51), SC_VOX(52, 52), SC_VOX(53, 53), SC_VOX(54, 54), SC_VOX(55, 55),
		SC_VOX(56, 56), SC_VOX(57, 57), SC_VOX(58, 58), SC_VOX(59, 59), SC_VOX(60, 60),
		SC_VOX(61, 61), SC_VOX(62, 62),
	};
	#undef SC_VOX
} // namespace

TArrayView<const FSoundSlot> GetSlotTable()
{
	return MakeArrayView(GSlots);
}

const FSoundSlot* GetSlot(int32 Id)
{
	return (Id >= 0 && Id < NumSlots) ? &GSlots[Id] : nullptr;
}

TArrayView<const FVoiceEvent> GetVoiceEventTable()
{
	return MakeArrayView(GVoiceEvents);
}

const FVoiceEvent* GetVoiceEvent(int32 Event)
{
	// The table is dense from 1..62, but index by search so a future gap cannot silently
	// return the wrong line.
	for (const FVoiceEvent& Entry : GVoiceEvents)
	{
		if (Entry.Event == Event)
		{
			return &Entry;
		}
	}
	return nullptr;
}
} // namespace SimCopterSound
