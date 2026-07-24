/*
RE_AGENT_NOTE
purpose: Requests a block sized as 0x98 + Count*0x40, clears it, and initializes a header followed by 0x0c-byte entries, an 8-byte gap, and linked 0x34-byte records.
use: Port at the four reported callers when reproducing this original packed Win32 allocation layout; FUN_00468380 supplies the block, while FUN_00470393 is an unknown check whose zero result selects error code 9.
evidence: Allocation failure selects error code 4. Header offsets +0x04 and +0x10 receive Count, +0x08 points to offset +0x90, and +0x4c points eight bytes beyond the entry array. Only when Count is positive are the 0x0c-byte entries cleared and the records initialized with 1, 0x00010002, Parameter, sequential indices, linked pointers, and values increasing by 0x10.
caveats: Field meanings, caller roles, allocator/check semantics, and error-code meanings are unknown. Nonpositive Count values are still stored in both header count fields and participate in allocation and pointer arithmetic. Pointers are stored as 32-bit addresses for the original Win32 layout. The FUN_00470393 zero-result path returns null without releasing the block in this function.
*/

#include <cstddef>
#include <cstdint>

using OriginalAddress = std::uint32_t;

struct FRuntimeEntry0C
{
	std::int32_t Value00;
	std::int32_t Value04;
	std::int32_t Value08;
};

static_assert(sizeof(FRuntimeEntry0C) == 0x0c);
static_assert(offsetof(FRuntimeEntry0C, Value00) == 0x00);
static_assert(offsetof(FRuntimeEntry0C, Value04) == 0x04);
static_assert(offsetof(FRuntimeEntry0C, Value08) == 0x08);

struct FRuntimeRecord34
{
	OriginalAddress Pointer00;   // +0x00
	std::int32_t Value04;        // +0x04
	std::uint32_t Value08;       // +0x08
	std::int32_t Value0C;        // +0x0c
	std::int32_t Parameter10;    // +0x10
	std::uint32_t Value14;       // +0x14
	std::uint32_t Value18;       // +0x18
	OriginalAddress Pointer1C;   // +0x1c
	OriginalAddress Pointer20;   // +0x20
	std::int32_t Value24;        // +0x24
	std::uint32_t Value28;       // +0x28
	std::uint32_t Value2C;       // +0x2c
	std::uint32_t Value30;       // +0x30
};

static_assert(sizeof(FRuntimeRecord34) == 0x34);
static_assert(offsetof(FRuntimeRecord34, Pointer00) == 0x00);
static_assert(offsetof(FRuntimeRecord34, Value04) == 0x04);
static_assert(offsetof(FRuntimeRecord34, Value08) == 0x08);
static_assert(offsetof(FRuntimeRecord34, Value0C) == 0x0c);
static_assert(offsetof(FRuntimeRecord34, Parameter10) == 0x10);
static_assert(offsetof(FRuntimeRecord34, Value14) == 0x14);
static_assert(offsetof(FRuntimeRecord34, Value18) == 0x18);
static_assert(offsetof(FRuntimeRecord34, Pointer1C) == 0x1c);
static_assert(offsetof(FRuntimeRecord34, Pointer20) == 0x20);
static_assert(offsetof(FRuntimeRecord34, Value24) == 0x24);
static_assert(offsetof(FRuntimeRecord34, Value28) == 0x28);
static_assert(offsetof(FRuntimeRecord34, Value2C) == 0x2c);
static_assert(offsetof(FRuntimeRecord34, Value30) == 0x30);

struct FRuntimeHeader90
{
	std::uint32_t Value00;       // +0x00
	std::int32_t Count04;        // +0x04
	OriginalAddress Pointer08;   // +0x08
	std::uint32_t Value0C;       // +0x0c
	std::int32_t Count10;        // +0x10
	std::byte Unknown14[0x1c];   // +0x14
	std::uint32_t Value30;       // +0x30
	std::byte Unknown34[0x0c];   // +0x34
	std::uint32_t Value40;       // +0x40
	std::uint32_t Value44;       // +0x44
	std::uint32_t Value48;       // +0x48
	OriginalAddress Pointer4C;   // +0x4c
	std::byte Unknown50[0x40];   // +0x50
};

static_assert(sizeof(FRuntimeHeader90) == 0x90);
static_assert(offsetof(FRuntimeHeader90, Value00) == 0x00);
static_assert(offsetof(FRuntimeHeader90, Count04) == 0x04);
static_assert(offsetof(FRuntimeHeader90, Pointer08) == 0x08);
static_assert(offsetof(FRuntimeHeader90, Value0C) == 0x0c);
static_assert(offsetof(FRuntimeHeader90, Count10) == 0x10);
static_assert(offsetof(FRuntimeHeader90, Value30) == 0x30);
static_assert(offsetof(FRuntimeHeader90, Value40) == 0x40);
static_assert(offsetof(FRuntimeHeader90, Value44) == 0x44);
static_assert(offsetof(FRuntimeHeader90, Value48) == 0x48);
static_assert(offsetof(FRuntimeHeader90, Pointer4C) == 0x4c);

extern void* DAT_00592068;
extern std::uint32_t DAT_0055d1d4;
extern std::int32_t DAT_005bdb40;

extern void* FUN_00468380(void* Context, std::uint32_t Size);
extern std::int32_t FUN_00470393(void* Block);

FRuntimeHeader90* FUN_0046edb0(
	const std::int32_t Count,
	const std::int32_t Parameter)
{
	DAT_0055d1d4 =
		static_cast<std::uint32_t>(Count) * 0x40u + 0x98u;

	auto* const Header = static_cast<FRuntimeHeader90*>(
		FUN_00468380(DAT_00592068, DAT_0055d1d4));

	if (Header == nullptr)
	{
		DAT_005bdb40 = 4;
		return nullptr;
	}

	const std::int32_t CheckResult = FUN_00470393(Header);
	std::uint32_t RemainingBytes = DAT_0055d1d4;

	if (CheckResult == 0)
	{
		DAT_005bdb40 = 9;
		return nullptr;
	}

	auto* ZeroCursor = reinterpret_cast<std::uint32_t*>(Header);
	for (std::uint32_t DwordCount = DAT_0055d1d4 >> 2;
		 DwordCount != 0;
		 --DwordCount)
	{
		*ZeroCursor = 0;
		++ZeroCursor;
	}

	for (RemainingBytes &= 3u; RemainingBytes != 0; --RemainingBytes)
	{
		*reinterpret_cast<std::uint8_t*>(ZeroCursor) = 0;
		ZeroCursor = reinterpret_cast<std::uint32_t*>(
			reinterpret_cast<std::uint8_t*>(ZeroCursor) + 1);
	}

	auto* Entry = reinterpret_cast<FRuntimeEntry0C*>(
		reinterpret_cast<std::byte*>(Header) + 0x90);

	Header->Value00 = 0;
	Header->Count04 = Count;
	Header->Count10 = Count;
	Header->Value44 = 0;
	Header->Value30 = 0;
	Header->Pointer08 = static_cast<OriginalAddress>(
		reinterpret_cast<std::uintptr_t>(Entry));
	Header->Value40 = 0;

	auto* const AuxiliaryBase =
		reinterpret_cast<std::byte*>(Entry) +
		static_cast<std::uint32_t>(Count) * sizeof(FRuntimeEntry0C);

	std::int32_t RemainingEntries = Count;
	if (Count > 0)
	{
		do
		{
			Entry->Value08 = 0;
			--RemainingEntries;
			Entry->Value00 = 0;
			Entry->Value04 = 0;
			++Entry;
		}
		while (RemainingEntries != 0);
	}

	std::uint32_t IncrementingValue = 0;
	std::int32_t RecordIndex = 0;

	auto* Record = reinterpret_cast<FRuntimeRecord34*>(
		AuxiliaryBase + 8);

	Header->Pointer4C = static_cast<OriginalAddress>(
		reinterpret_cast<std::uintptr_t>(Record));

	if (Count > 0)
	{
		do
		{
			auto* const NextRecord = reinterpret_cast<FRuntimeRecord34*>(
				reinterpret_cast<std::byte*>(Record) +
				sizeof(FRuntimeRecord34));

			Record->Value04 = 1;
			Record->Value08 = 0x00010002;
			Record->Value0C = RecordIndex;
			Record->Parameter10 = Parameter;
			Record->Value24 = RecordIndex;
			++RecordIndex;

			Record->Pointer20 = static_cast<OriginalAddress>(
				reinterpret_cast<std::uintptr_t>(AuxiliaryBase));
			Record->Pointer00 = static_cast<OriginalAddress>(
				reinterpret_cast<std::uintptr_t>(NextRecord));
			Record->Pointer1C = static_cast<OriginalAddress>(
				reinterpret_cast<std::uintptr_t>(&Record->Value30));
			Record->Value30 = IncrementingValue;

			IncrementingValue += 0x10;
			Record = NextRecord;
		}
		while (RecordIndex < Count);
	}

	return Header;
}

// REVERSED_FUNCTION: ::0x0046edb0 (0x0046edb0)