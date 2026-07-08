/*
RE_AGENT_NOTE
purpose: Configures and activates one original person spawn: resolves tile/object/world placement, binds behavior/state/host data, adjusts height against terrain/carrier, queues path events for modes 4/6, and uploads the render transform.
use: Port as the main original-person spawn configurator at the two known caller sites from the Ghidra header. Important callees include FUN_004c02a0, FUN_004c7020, FUN_004c4070, FUN_004c9000 for modes 5/default explicit-position probing, FUN_004c00b0, FUN_004a89c0, and FUN_004704d1.
evidence: Switches on SpawnMode; tile cases sample DAT_005d9200 scene cells, object cases call FUN_004c00b0, explicit mode 0 directly calls FUN_004c6fa0, modes 5/default explicit paths probe FUN_004c9000, success sets +0x142/+0x152, updates counters, binds "NoMo" if +0x224 is null, and uploads position plus 16 transform ints.
caveats: Thiscall ECX is modeled as an explicit Person pointer on same-object helper calls even where Ghidra prints zero explicit arguments. Field names are semantic and must remain offset-validated against the project type. The vtable +8 dispatch is preserved as raw code* calls because Ghidra does not prove the recovered member signature. DAT_0058dc3e is an overlapping-width global: the final comparison uses signed-short sVar4, while the decompile shows a 32-bit assignment of sign-extended sVar4.
*/

#include <cstddef>
#include <cstdint>

struct FSimCopterOriginalCarrier
{
	std::byte Pad0000[0x08];
	int32_t TransformOwner; // +0x08
	std::byte Pad000c[0x0c];
	int32_t WorldX; // +0x18
	std::byte Pad001c[0x04];
	int32_t WorldZ; // +0x20
	std::byte LocalTransform[1]; // +0x24
};

struct FSimCopterOriginalPerson
{
	using RawCodeProc = void (*)();

	RawCodeProc* VTable; // +0x000
	std::byte Pad0004[0x126];
	int16_t TileX; // +0x12a
	int16_t TileY; // +0x12c
	int16_t FigureId; // +0x12e
	std::byte Pad0130[0x12];
	int16_t ActiveFlag; // +0x142
	std::byte Pad0144[0x04];
	int16_t State; // +0x148
	std::byte Pad014a[0x06];
	int16_t AmbientClassIndex; // +0x150
	int16_t RenderActiveFlag; // +0x152
	std::byte Pad0154[0x30];
	int16_t ModeSearchDelay; // +0x184
	int16_t Mode4PathFlag; // +0x186
	int16_t CachedTileX; // +0x188
	int16_t CachedTileY; // +0x18a
	std::byte Pad018c[0x14];
	FSimCopterOriginalCarrier* Carrier; // +0x1a0
	std::byte Pad01a4[0x18];
	int32_t RenderNode; // +0x1bc
	uint16_t SpawnFlags; // +0x1c0
	std::byte Pad01c2[0x02];
	int32_t WalkSpeed; // +0x1c4
	std::byte Pad01c8[0x04];
	int32_t WorldX; // +0x1cc
	int32_t WorldY; // +0x1d0
	int32_t WorldZ; // +0x1d4
	int32_t Transform[16]; // +0x1d8
	std::byte Pad0218[0x0c];
	int32_t BoundAnimClip; // +0x224
};

static_assert(offsetof(FSimCopterOriginalPerson, TileX) == 0x12a);
static_assert(offsetof(FSimCopterOriginalPerson, TileY) == 0x12c);
static_assert(offsetof(FSimCopterOriginalPerson, FigureId) == 0x12e);
static_assert(offsetof(FSimCopterOriginalPerson, ActiveFlag) == 0x142);
static_assert(offsetof(FSimCopterOriginalPerson, State) == 0x148);
static_assert(offsetof(FSimCopterOriginalPerson, AmbientClassIndex) == 0x150);
static_assert(offsetof(FSimCopterOriginalPerson, RenderActiveFlag) == 0x152);
static_assert(offsetof(FSimCopterOriginalPerson, ModeSearchDelay) == 0x184);
static_assert(offsetof(FSimCopterOriginalPerson, Mode4PathFlag) == 0x186);
static_assert(offsetof(FSimCopterOriginalPerson, CachedTileX) == 0x188);
static_assert(offsetof(FSimCopterOriginalPerson, CachedTileY) == 0x18a);
static_assert(offsetof(FSimCopterOriginalPerson, Carrier) == 0x1a0);
static_assert(offsetof(FSimCopterOriginalPerson, RenderNode) == 0x1bc);
static_assert(offsetof(FSimCopterOriginalPerson, SpawnFlags) == 0x1c0);
static_assert(offsetof(FSimCopterOriginalPerson, WalkSpeed) == 0x1c4);
static_assert(offsetof(FSimCopterOriginalPerson, WorldX) == 0x1cc);
static_assert(offsetof(FSimCopterOriginalPerson, WorldY) == 0x1d0);
static_assert(offsetof(FSimCopterOriginalPerson, WorldZ) == 0x1d4);
static_assert(offsetof(FSimCopterOriginalPerson, Transform) == 0x1d8);
static_assert(offsetof(FSimCopterOriginalPerson, BoundAnimClip) == 0x224);

extern int32_t DAT_00506aec;
extern int32_t DAT_0058dc1c;
extern int32_t _DAT_0058dc1a;
extern int32_t DAT_0058dc3e;
extern int16_t DAT_005d91d0;
extern int16_t DAT_005d91d4;
extern uint8_t* DAT_005910b0[0x80];
extern int32_t DAT_005d9200[0x10000];
extern std::byte DAT_0058d6d0[];

extern int32_t FUN_0046c4bf(int32_t Value, int32_t Scale);
extern int32_t FUN_0046d770(int32_t TransformOwner, void* Transform, void* LocalPoint, int32_t FacingOrSpeed);
extern int16_t FUN_004c9000(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
extern void FUN_004c4e10(FSimCopterOriginalPerson* Person, int32_t HostObject);
extern void FUN_004c6fa0(FSimCopterOriginalPerson* Person, int32_t WorldX, int32_t WorldY, int32_t WorldZ);
extern int32_t FUN_004c82c0(int32_t WorldX, int32_t CurrentWorldY, int32_t WorldZ);
extern int16_t FUN_004c02a0(FSimCopterOriginalPerson* Person, int32_t SceneCell, void* OutLocalX, void* OutLocalZ, int32_t PlacementMode);
extern void FUN_004c7020(FSimCopterOriginalPerson* Person, uint16_t TileX, uint16_t TileY, int32_t LocalX, int32_t LocalZ);
extern void FUN_004c7090(FSimCopterOriginalPerson* Person, int32_t State);
extern void FUN_004c7080(FSimCopterOriginalPerson* Person, int32_t Unknown);
extern void FUN_004c71c0(FSimCopterOriginalPerson* Person, int32_t BehaviorClass);
extern void FUN_004c6360(FSimCopterOriginalPerson* Person, int32_t HostObject);
extern int32_t FUN_004c9220(uint32_t RawArg0, uint32_t RawArg1);
extern int16_t FUN_004c00b0(FSimCopterOriginalPerson* Person, int32_t HostObject, void* OutPosition);
extern void FUN_004c4070(FSimCopterOriginalPerson* Person, int32_t BehaviorClass, int32_t SpawnMode, int32_t InitialStateOrEvent);
extern int16_t FUN_004c40a0(int32_t SpawnMode, uint32_t TileXOrPacked, uint32_t TileY);
extern void FUN_004c4e60(FSimCopterOriginalPerson* Person);
extern void* FUN_004a8890(int32_t EventId);
extern void FUN_004a89c0(void* PathEvent);
extern void FUN_004c68f0(FSimCopterOriginalPerson* Person, uint32_t ClipFourCc);
extern void FUN_004c78c0(FSimCopterOriginalPerson* Person);
extern void FUN_004704d1(int32_t RenderNode, void* TransformPayload, int32_t Mode);

static uint32_t CONCAT22_EXACT(int16_t High, int16_t Low)
{
	return (uint32_t(uint16_t(High)) << 16) | uint32_t(uint16_t(Low));
}

int16_t FUN_004c4190(
	FSimCopterOriginalPerson* Person,
	int32_t BehaviorClass,
	int32_t SpawnMode,
	uint16_t TileX,
	uint16_t TileY,
	int32_t InitialStateOrEvent,
	int32_t HostObject,
	int32_t* ExplicitPosition)
{
	FSimCopterOriginalPerson::RawCodeProc RawProcAtVtablePlus8;
	uint16_t uVar2;
	bool bVar3;
	int16_t sVar4;
	uint16_t uVar5;
	uint16_t uVar6;
	uint16_t uVar7;
	int32_t iVar8;
	int16_t* psVar9;
	int32_t iVar10;
	uint16_t uVar11;
	uint32_t uVar12;
	uint32_t uVar14;
	uint16_t local_74;
	int16_t local_72;
	int16_t sStack_70;
	int16_t sStack_6e;
	int32_t Frame.local_6c;
	int32_t Frame.local_68;
	int32_t Frame.local_64;
	int32_t Frame.local_60;
	uint32_t Frame.local_5c;
	int32_t Frame.local_58;
	int32_t Frame.local_54;
	int32_t Frame.local_50;
	int32_t local_4c;
	int32_t aiStack_40[16];

	switch (SpawnMode)
	{
	case 0:
		sStack_6e = 0;
		FUN_004c7090(Person, SpawnMode);
		FUN_004c7080(Person, InitialStateOrEvent);
		FUN_004c71c0(Person, BehaviorClass);
		if (ExplicitPosition == nullptr)
		{
			iVar8 = FUN_004c9220(TileX, TileY);
			sVar4 = FUN_004c02a0(Person, DAT_005d9200[(TileX & 0xff) * 0x100 + (TileY & 0xff)], &Frame.local_6c, &Frame.local_68, *reinterpret_cast<int32_t*>(&DAT_0058d6d0[iVar8 * 6]));
			if (sVar4 == 0)
			{
				goto LAB_004c4ce1;
			}
			FUN_004c7020(Person, TileX, TileY, Frame.local_6c, Frame.local_68);
			iVar8 = reinterpret_cast<int32_t>(Person->Carrier);
			if (iVar8 != 0)
			{
				iVar10 = Person->WalkSpeed;
				Frame.local_58 = Person->WorldX - *reinterpret_cast<int32_t*>(iVar8 + 0x18);
				Frame.local_50 = Person->WorldZ - *reinterpret_cast<int32_t*>(iVar8 + 0x20);
				goto LAB_004c4c85;
			}
			iVar8 = FUN_004c82c0(Person->WorldX, Person->WorldY, Person->WorldZ);
			goto LAB_004c4cb7;
		}
		FUN_004c6360(Person, HostObject);
		FUN_004c6fa0(Person, ExplicitPosition[0], ExplicitPosition[1], ExplicitPosition[2]);
		iVar8 = reinterpret_cast<int32_t>(Person->Carrier);
		if (iVar8 == 0)
		{
			iVar10 = FUN_004c82c0(Person->WorldX, Person->WorldY, Person->WorldZ);
		}
		else
		{
			Frame.local_58 = Person->WorldX - *reinterpret_cast<int32_t*>(iVar8 + 0x18);
			Frame.local_54 = 0;
			Frame.local_50 = Person->WorldZ - *reinterpret_cast<int32_t*>(iVar8 + 0x20);
			iVar8 = FUN_0046d770(*reinterpret_cast<int32_t*>(iVar8 + 8), reinterpret_cast<void*>(iVar8 + 0x24), &Frame.local_58, Person->WalkSpeed);
			iVar10 = FUN_004c82c0(Person->WorldX, Person->WorldY, Person->WorldZ);
			iVar10 = iVar10 + iVar8;
		}
		Person->WorldY = iVar10 + 0x30000;
		break;

	case 1:
	case 0x0f:
	case 0x13:
		sStack_6e = 0;
		iVar8 = *reinterpret_cast<int32_t*>(HostObject + 0x18);
		iVar10 = *reinterpret_cast<int32_t*>(HostObject + 0x20);
		sVar4 = FUN_004c00b0(Person, HostObject, &Frame.local_58);
		if (sVar4 == 0)
		{
			goto LAB_004c4ce1;
		}
		Person->TileX = static_cast<int16_t>((iVar8 + 0x20000000) >> 0x16);
		Person->TileY = static_cast<int16_t>((0x20000000 - iVar10) >> 0x16);
		Person->WorldX = Frame.local_58;
		Person->WorldY = Frame.local_54;
		Person->WorldZ = Frame.local_50;
		FUN_004c4070(Person, BehaviorClass, SpawnMode, InitialStateOrEvent);
		FUN_004c6360(Person, HostObject);
		break;

	case 2:
		sStack_6e = 0;
		uVar11 = TileY & 0xff;
		Frame.local_58 = static_cast<int32_t>(((uint32_t(Frame.local_58) & 0xffff0000u) | uint32_t(uint16_t(TileX))) & 0xffff00ffu);
		if (((TileX & 0xff) < 0x80) && (uVar11 < 0x80))
		{
			uVar5 = static_cast<uint16_t>(*reinterpret_cast<uint8_t*>(DAT_005910b0[static_cast<int16_t>(TileX & 0xff)] + static_cast<int16_t>(uVar11)));
		}
		else
		{
			uVar5 = static_cast<uint16_t>(static_cast<int16_t>(static_cast<int16_t>(Frame.local_58) - DAT_005d91d0) >> 0x0f);
			if ((1 < static_cast<int16_t>(((static_cast<int16_t>(Frame.local_58) - DAT_005d91d0) ^ uVar5) - uVar5)) ||
				(uVar12 = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(uVar11 - DAT_005d91d4)) >> 0x1f), uVar5 = 0x00f6,
				 1 < static_cast<int16_t>((static_cast<int32_t>(static_cast<int16_t>(uVar11 - DAT_005d91d4)) ^ uVar12) - uVar12)))
			{
				uVar5 = 0xffff;
			}
		}
		if ((((uVar5 == 0x00fe) || (uVar5 == 0x00b7)) || (uVar5 == 0x00ff)) ||
			(sVar4 = FUN_004c02a0(Person, DAT_005d9200[static_cast<int16_t>(Frame.local_58) * 0x100 + static_cast<int16_t>(uVar11)], &Frame.local_6c, &Frame.local_68, 2), sVar4 == 0))
		{
			goto LAB_004c4ce1;
		}
		FUN_004c7020(Person, TileX, TileY, Frame.local_6c, Frame.local_68);
		iVar8 = reinterpret_cast<int32_t>(Person->Carrier);
		if (iVar8 == 0)
		{
			iVar8 = FUN_004c82c0(Person->WorldX, Person->WorldY, Person->WorldZ);
		}
		else
		{
			iVar10 = Person->WalkSpeed;
			Frame.local_58 = Person->WorldX - *reinterpret_cast<int32_t*>(iVar8 + 0x18);
			Frame.local_50 = Person->WorldZ - *reinterpret_cast<int32_t*>(iVar8 + 0x20);
LAB_004c4c85:
			Frame.local_54 = 0;
			iVar10 = FUN_0046d770(*reinterpret_cast<int32_t*>(iVar8 + 8), reinterpret_cast<void*>(iVar8 + 0x24), &Frame.local_58, iVar10);
			iVar8 = FUN_004c82c0(Person->WorldX, Person->WorldY, Person->WorldZ);
			iVar8 = iVar8 + iVar10;
		}
		goto LAB_004c4cb7;

	case 3:
		bVar3 = false;
		Person->AmbientClassIndex = 7;
		iVar8 = FUN_0046c4bf(0x30000, 0x20000);
		Person->WalkSpeed = iVar8;
		Frame.local_5c = Frame.local_5c & 0xffff0000;
		do
		{
			Frame.local_58 = FUN_004c9220(TileX, TileY);
			sVar4 = FUN_004c40a0(SpawnMode, TileX, TileY);
			if ((sVar4 != 0) &&
				(sVar4 = FUN_004c02a0(Person, DAT_005d9200[(TileX & 0xff) * 0x100 + (TileY & 0xff)], &Frame.local_6c, &Frame.local_68, *reinterpret_cast<int32_t*>(&DAT_0058d6d0[Frame.local_58 * 6])), sVar4 != 0))
			{
				bVar3 = true;
				break;
			}
			sVar4 = static_cast<int16_t>(Frame.local_5c) + 1;
			Frame.local_5c = (Frame.local_5c & 0xffff0000u) | uint16_t(sVar4);
		}
		while (sVar4 < 4);
		if (!bVar3)
		{
			sStack_6e = 0;
			goto LAB_004c4ce1;
		}
		FUN_004c7020(Person, TileX, TileY, Frame.local_6c, Frame.local_68);
		iVar8 = reinterpret_cast<int32_t>(Person->Carrier);
		if (iVar8 == 0)
		{
			iVar10 = FUN_004c82c0(Person->WorldX, Person->WorldY, Person->WorldZ);
		}
		else
		{
			Frame.local_58 = Person->WorldX - *reinterpret_cast<int32_t*>(iVar8 + 0x18);
			Frame.local_54 = 0;
			Frame.local_50 = Person->WorldZ - *reinterpret_cast<int32_t*>(iVar8 + 0x20);
			iVar8 = FUN_0046d770(*reinterpret_cast<int32_t*>(iVar8 + 8), reinterpret_cast<void*>(iVar8 + 0x24), &Frame.local_58, Person->WalkSpeed);
			iVar10 = FUN_004c82c0(Person->WorldX, Person->WorldY, Person->WorldZ);
			iVar10 = iVar10 + iVar8;
		}
		Person->WorldY = iVar10 + 0x30000;
		FUN_004c71c0(Person, BehaviorClass);
		FUN_004c4e60(Person);
		break;

	case 4:
	case 6:
		uVar14 = uint32_t(TileX);
		uVar12 = uint32_t(TileY);
		sStack_6e = 0;
		local_74 = 0;
		do
		{
			Frame.local_58 = FUN_004c9220(uVar14, uVar12);
			sVar4 = FUN_004c40a0(SpawnMode, uVar14, uVar12);
			uVar11 = static_cast<uint16_t>(static_cast<int16_t>(local_74) >> 0x0f);
			if ((sVar4 != 0) &&
				(sVar4 = FUN_004c02a0(Person, DAT_005d9200[(uVar14 & 0xff) * 0x100 + (uVar12 & 0xff)], &Frame.local_5c, &Frame.local_6c, *reinterpret_cast<int32_t*>(&DAT_0058d6d0[Frame.local_58 * 6])), sVar4 != 0))
			{
				bVar3 = false;
				local_72 = -1;
				sStack_70 = -1;
				if (SpawnMode != 4)
				{
					goto LAB_004c4a42;
				}
				psVar9 = static_cast<int16_t*>(FUN_004a8890(InitialStateOrEvent));
				local_72 = *psVar9;
				iVar8 = reinterpret_cast<int32_t>(FUN_004a8890(InitialStateOrEvent));
				sStack_70 = *reinterpret_cast<int16_t*>(iVar8 + 4);
				Frame.local_68 = static_cast<int32_t>(uint32_t(Frame.local_68) & 0xffff0000u);
				goto LAB_004c49a6;
			}
			if (((((local_74 ^ uVar11) - uVar11) & 1) ^ uVar11) == uVar11)
			{
				uVar14 = uint32_t(uint16_t(static_cast<int16_t>(uVar14) + 1));
			}
			else
			{
				uVar12 = uint32_t(uint16_t(static_cast<int16_t>(uVar12) + 1));
			}
			local_74 = uint16_t(local_74 + 1);
		}
		while (static_cast<int16_t>(local_74) < 4);
		goto LAB_004c4ce1;

	case 5:
		sStack_6e = 0;
		if ((Person->SpawnFlags & 0x80) == 0)
		{
			iVar8 = DAT_00506aec;
			if ((Person->SpawnFlags & 0x200) != 0)
			{
				iVar8 = DAT_00506aec * 2;
			}
			iVar8 = FUN_0046c4bf(Person->WalkSpeed, iVar8);
		}
		else
		{
			iVar8 = 0x80000;
		}
		iVar8 = FUN_004c9000(0, ExplicitPosition[0], ExplicitPosition[1], ExplicitPosition[2], iVar8 * 2, 0, 0);
		if (iVar8 != 0)
		{
			goto LAB_004c4ce1;
		}
		FUN_004c4e10(Person, HostObject);
		FUN_004c6fa0(Person, ExplicitPosition[0], ExplicitPosition[1], ExplicitPosition[2]);
		iVar8 = reinterpret_cast<int32_t>(Person->Carrier);
		if (iVar8 != 0)
		{
			iVar10 = Person->WalkSpeed;
			Frame.local_58 = Person->WorldX - *reinterpret_cast<int32_t*>(iVar8 + 0x18);
			Frame.local_50 = Person->WorldZ - *reinterpret_cast<int32_t*>(iVar8 + 0x20);
			goto LAB_004c4c85;
		}
		iVar8 = FUN_004c82c0(Person->WorldX, Person->WorldY, Person->WorldZ);
		goto LAB_004c4cb7;

	default:
		sStack_6e = 0;
		if (ExplicitPosition == nullptr)
		{
			sVar4 = FUN_004c02a0(Person, DAT_005d9200[(TileX & 0xff) * 0x100 + (TileY & 0xff)], &Frame.local_6c, &Frame.local_68, 1);
			if (sVar4 == 0)
			{
				goto LAB_004c4ce1;
			}
			FUN_004c4e10(Person, HostObject);
			FUN_004c7020(Person, TileX, TileY, Frame.local_6c, Frame.local_68);
			iVar8 = reinterpret_cast<int32_t>(Person->Carrier);
			if (iVar8 != 0)
			{
				iVar10 = Person->WalkSpeed;
				Frame.local_58 = Person->WorldX - *reinterpret_cast<int32_t*>(iVar8 + 0x18);
				Frame.local_50 = Person->WorldZ - *reinterpret_cast<int32_t*>(iVar8 + 0x20);
				goto LAB_004c4c85;
			}
			iVar8 = FUN_004c82c0(Person->WorldX, Person->WorldY, Person->WorldZ);
		}
		else
		{
			if ((Person->SpawnFlags & 0x80) == 0)
			{
				iVar8 = DAT_00506aec;
				if ((Person->SpawnFlags & 0x200) != 0)
				{
					iVar8 = DAT_00506aec * 2;
				}
				iVar8 = FUN_0046c4bf(Person->WalkSpeed, iVar8);
			}
			else
			{
				iVar8 = 0x80000;
			}
			iVar8 = FUN_004c9000(0, ExplicitPosition[0], ExplicitPosition[1], ExplicitPosition[2], iVar8 * 2, 0, 0);
			if (iVar8 != 0)
			{
				goto LAB_004c4ce1;
			}
			FUN_004c4e10(Person, HostObject);
			FUN_004c6fa0(Person, ExplicitPosition[0], ExplicitPosition[1], ExplicitPosition[2]);
			iVar8 = reinterpret_cast<int32_t>(Person->Carrier);
			if (iVar8 != 0)
			{
				iVar10 = Person->WalkSpeed;
				Frame.local_58 = Person->WorldX - *reinterpret_cast<int32_t*>(iVar8 + 0x18);
				Frame.local_50 = Person->WorldZ - *reinterpret_cast<int32_t*>(iVar8 + 0x20);
				goto LAB_004c4c85;
			}
			iVar8 = FUN_004c82c0(Person->WorldX, Person->WorldY, Person->WorldZ);
		}

LAB_004c4cb7:
		Person->WorldY = iVar8 + 0x30000;
		FUN_004c4070(Person, BehaviorClass, SpawnMode, InitialStateOrEvent);
	}

LAB_004c4cda:
	sStack_6e = 1;

LAB_004c4ce1:
	if (sStack_6e == 1)
	{
		Person->CachedTileX = Person->TileX;
		Person->CachedTileY = Person->TileY;

		RawProcAtVtablePlus8 = Person->VTable[2];
		RawProcAtVtablePlus8();
		RawProcAtVtablePlus8();

		Person->ActiveFlag = 1;
		Person->RenderActiveFlag = 1;

		if (Person->State == 0)
		{
			DAT_0058dc1c = DAT_0058dc1c + 1;
		}
		else
		{
			_DAT_0058dc1a = _DAT_0058dc1a + 1;
		}

		if (Person->BoundAnimClip == 0)
		{
			FUN_004c68f0(Person, 0x4e6f4d6f);
		}

		FUN_004c78c0(Person);

		Frame.local_54 = Person->WorldY;
		Frame.local_58 = Person->WorldX;
		Frame.local_50 = Person->WorldZ;

		for (iVar8 = 0x10; iVar8 != 0; iVar8 = iVar8 + -1)
		{
			aiStack_40[0x10 - iVar8] = Person->Transform[0x10 - iVar8];
		}

		FUN_004704d1(Person->RenderNode, &Frame.local_58, 3);

		sVar4 = Person->FigureId;
		if ((sVar4 != 32000) && (DAT_0058dc3e < sVar4))
		{
			DAT_0058dc3e = static_cast<int32_t>(sVar4);
		}
	}

	return sStack_6e;

	while (true)
	{
		sVar4 = static_cast<int16_t>(Frame.local_68) + 1;
		Frame.local_68 = static_cast<int32_t>((uint32_t(Frame.local_68) & 0xffff0000u) | uint16_t(sVar4));
		if (9 < sVar4)
		{
			break;
		}

LAB_004c49a6:
		FUN_004c9220(CONCAT22_EXACT(sStack_70, local_72), sStack_70);
		uVar6 = static_cast<uint16_t>(sStack_70 - static_cast<int16_t>(uVar12));
		uVar5 = static_cast<uint16_t>(static_cast<int16_t>(uVar6) >> 0x0f);
		uVar7 = static_cast<uint16_t>(local_72 - static_cast<int16_t>(uVar14));
		uVar2 = static_cast<uint16_t>(static_cast<int16_t>(uVar7) >> 0x0f);
		if ((0x10 < static_cast<int32_t>(static_cast<int16_t>((uVar6 ^ uVar5) - uVar5)) + static_cast<int32_t>(static_cast<int16_t>((uVar7 ^ uVar2) - uVar2))) &&
			(sVar4 = FUN_004c40a0(4, CONCAT22_EXACT(sStack_70, local_72), sStack_70), sVar4 != 0))
		{
			goto LAB_004c4a42;
		}

		if (((((local_74 ^ uVar11) - uVar11) & 1) ^ uVar11) == uVar11)
		{
			local_72 = local_72 + -1;
		}
		else
		{
			sStack_70 = sStack_70 + -1;
		}

		if ((local_72 < 0) || (sStack_70 < 0))
		{
			break;
		}
	}

LAB_004c4a49:
	if (!bVar3)
	{
		goto LAB_004c4ce1;
	}

	FUN_004c7020(Person, static_cast<uint16_t>(uVar14), static_cast<uint16_t>(uVar12), static_cast<int32_t>(Frame.local_5c), Frame.local_6c);
	iVar8 = reinterpret_cast<int32_t>(Person->Carrier);
	if (iVar8 == 0)
	{
		iVar10 = FUN_004c82c0(Person->WorldX, Person->WorldY, Person->WorldZ);
	}
	else
	{
		Frame.local_68 = Person->WorldX - *reinterpret_cast<int32_t*>(iVar8 + 0x18);
		Frame.local_64 = 0;
		Frame.local_60 = Person->WorldZ - *reinterpret_cast<int32_t*>(iVar8 + 0x20);
		iVar8 = FUN_0046d770(*reinterpret_cast<int32_t*>(iVar8 + 8), reinterpret_cast<void*>(iVar8 + 0x24), &Frame.local_68, Person->WalkSpeed);
		iVar10 = FUN_004c82c0(Person->WorldX, Person->WorldY, Person->WorldZ);
		iVar10 = iVar10 + iVar8;
	}

	Person->WorldY = iVar10 + 0x30000;
	FUN_004c4070(Person, BehaviorClass, SpawnMode, InitialStateOrEvent);

	Frame.local_54 = InitialStateOrEvent;
	Frame.local_58 = 0x1e;
	if (SpawnMode != 4)
	{
		Frame.local_58 = 0;
	}
	Frame.local_50 = static_cast<int32_t>(Person->TileX);
	local_4c = static_cast<int32_t>(Person->TileY);
	FUN_004a89c0(&Frame.local_58);

	if (SpawnMode == 4)
	{
		Frame.local_50 = static_cast<int32_t>(local_72);
		local_4c = static_cast<int32_t>(sStack_70);
		Frame.local_54 = InitialStateOrEvent;
		Frame.local_58 = 0;
		FUN_004a89c0(&Frame.local_58);
		Person->Mode4PathFlag = 0;
	}
	else
	{
		Person->ModeSearchDelay = 100;
	}

	goto LAB_004c4cda;

LAB_004c4a42:
	bVar3 = true;
	goto LAB_004c4a49;
}

// REVERSED_FUNCTION: ::0x004c4190 (0x004c4190)