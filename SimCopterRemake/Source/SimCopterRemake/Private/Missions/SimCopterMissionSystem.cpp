#include "Missions/SimCopterMissionSystem.h"
#include "Formats/SimCopterTweakReader.h"

namespace SimCopterMissions
{

bool FSimCopterMissionSystem::LoadCareerData(const FString& TweakFilePath)
{
	FSimCopterTweakFile TweakFile;
	FString Error;
	if (!FSimCopterTweakReader::LoadTweakFileFromFile(TweakFilePath, TweakFile, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load career data: %s"), *Error);
		return false;
	}

	CareerCities.Empty();

	for (int32 i = 0; i < 30; ++i)
	{
		FString SectionName = FString::Printf(TEXT("City%d"), i);
		if (const FSimCopterTweakSection* Section = TweakFile.FindSection(SectionName))
		{
			FSimCopterCareerCity City;
			City.Difficulty = Section->GetInt(TEXT("Ctrl0_Value"));
			City.Weights[0] = Section->GetInt(TEXT("Ctrl1_Value"));
			City.Weights[1] = Section->GetInt(TEXT("Ctrl2_Value"));
			City.Weights[2] = Section->GetInt(TEXT("Ctrl3_Value"));
			City.Weights[3] = Section->GetInt(TEXT("Ctrl4_Value"));
			City.Weights[4] = Section->GetInt(TEXT("Ctrl5_Value"));
			City.Weights[5] = Section->GetInt(TEXT("Ctrl6_Value"));
			City.Weights[6] = Section->GetInt(TEXT("Ctrl7_Value"));
			City.DayOrNight = Section->GetInt(TEXT("Ctrl8_Value"));
			City.PointsNeeded = Section->GetInt(TEXT("Ctrl9_Value"));
			City.MoneyEarned = Section->GetInt(TEXT("Ctrl10_Value"));
			CareerCities.Add(City);
		}
	}
	
	if (CareerCities.Num() > 0)
	{
		CurrentCityIndex = 0;
		SetCareerCity(CareerCities[0]);
		return true;
	}
	return false;
}

void FSimCopterMissionSystem::BeginSession()
{
	Score = 0;
	Cash = SessionStartingCash;
}

const FSimCopterCareerCity* FSimCopterMissionSystem::GetCareerCityByIndex(int32 Index) const
{
	return CareerCities.IsValidIndex(Index) ? &CareerCities[Index] : nullptr;
}

bool FSimCopterMissionSystem::SelectCareerCity(int32 Index)
{
	if (!CareerCities.IsValidIndex(Index))
	{
		return false;
	}

	CurrentCityIndex = Index;
	SetCareerCity(CareerCities[Index]);
	Score = 0; // FUN_00408210 tail: entering a city clears the city score (session block +0x50)
	return true;
}

void FSimCopterMissionSystem::AdvanceCareerCity()
{
	if (CareerCities.Num() > 0)
	{
		CurrentCityIndex = (CurrentCityIndex + 1) % CareerCities.Num();
		SetCareerCity(CareerCities[CurrentCityIndex]);
		Score = 0; // Reset score for new city
	}
}

void FSimCopterMissionSystem::AdvanceCareerIfComplete()
{
	if (CareerCities.Num() > 0)
	{
		const FSimCopterCareerCity& CurCity = CareerCities[CurrentCityIndex];
		if (CurCity.PointsNeeded > 0 && Score >= CurCity.PointsNeeded)
		{
			Cash += CurCity.MoneyEarned;
			if (World)
			{
				World->PlayUiSound(0x50); // Level complete
			}
			AdvanceCareerCity();
		}
	}
}

// FSimCopterMissionSystem implementation
void FSimCopterMissionSystem::Initialize(ISimCopterMissionWorld* InWorld, uint32 RandSeed)
{
	World = InWorld;
	Rand.Seed(RandSeed);
	Records.SetNum(MaxRecords);
	Flames.SetNum(MaxFlames);
	FireObjects.SetNum(MaxFireObjects);

	Score = 0;
	Cash = 0;
	DifficultyTier = 1;
	FrameDeltaEma = 0;
	SpawnCountdown = 0xb40000;
	EasyIntervalCache = Tuning.EasyInterval;
	MaxEasyWithDifficulty = Tuning.MaxEasy + DifficultyTier;
	ScaledMissionTimer = Tuning.BaseMissionTimer;
	NagInterval = ScaledMissionTimer >> 3;
	PercentRoll = 0;
	bRerollRequested = 1;
	ConsecutivePlaceFailures = 0;
	ActiveCount = 0;
	BackgroundCount = 0;
	NextEventId = 0;
	LifecyclePassCounter = 0;
	FocusRecordIndex = INDEX_NONE;
	ActiveFlameCount = 0;
	SpreadAccumulator = 0;
	for (int32 i = 0; i < 16; ++i)
	{
		TypeSerials[i] = 0;
	}
}

void FSimCopterMissionSystem::SetCareerCity(const FSimCopterCareerCity& City)
{
	CareerCity = City;
	DifficultyTier = CareerCity.Difficulty + 1;
	RebuildCumulativeWeights();
}

void FSimCopterMissionSystem::Tick(float DeltaSeconds)
{
	int32 Delta1616 = FMath::Clamp(static_cast<int32>(DeltaSeconds * 65536.0f), 0, 0x7fffffff);
	FrameDeltaEma = (FrameDeltaEma * 7 + Delta1616) >> 3;

	RunSchedulerOnce();
	UpdateFires();
	UpdateLifecycle();
}

void FSimCopterMissionSystem::RunSchedulerOnce()
{
	UpdateSchedulerCadence();

	if (SpawnCountdown < 0 && (World == nullptr || !World->IsModalUiActive()))
	{
		DispatchWeightedRoll();
	}
}

void FSimCopterMissionSystem::DispatchWeightedRoll()
{
	if (bRerollRequested == 1)
	{
		RebuildCumulativeWeights();
		uint32 UVar6 = Rand.Rand();
		PercentRoll = static_cast<int32>(static_cast<int16>((UVar6 >> 15) << 16 | (UVar6 & 0xffff))) % 100;
	}

	if (PercentRoll < CumulativeWeights[1]) DispatchScheduledType(1); // Fire
	else if (PercentRoll < CumulativeWeights[2]) DispatchScheduledType(2); // Crime
	else if (PercentRoll < CumulativeWeights[3]) DispatchScheduledType(3); // Rescue
	else if (PercentRoll < CumulativeWeights[4]) DispatchScheduledType(4); // Riot
	else if (PercentRoll < CumulativeWeights[5]) DispatchScheduledType(5); // Traffic
	else if (PercentRoll < CumulativeWeights[6]) DispatchScheduledType(6); // MedEvac
	else if (PercentRoll < CumulativeWeights[7]) DispatchScheduledType(7); // Transport
}

bool FSimCopterMissionSystem::RollScheduledMissionNow()
{
	const int32 BeforeCount = ActiveCount + BackgroundCount;

	// A fresh percentage roll, as the scheduler does on the pass after a successful creation.
	bRerollRequested = 1;
	DispatchWeightedRoll();

	return (ActiveCount + BackgroundCount) > BeforeCount;
}

void FSimCopterMissionSystem::UpdateSchedulerCadence()
{
	MaxEasyWithDifficulty = Tuning.MaxEasy + DifficultyTier;
	EasyIntervalCache = Tuning.EasyInterval;
	ScaledMissionTimer = Tuning.BaseMissionTimer;

	switch (DifficultyTier)
	{
	case 1: break;
	case 2: ScaledMissionTimer = static_cast<int32>((static_cast<int64>(Tuning.BaseMissionTimer) * 3 + ((static_cast<int64>(Tuning.BaseMissionTimer) * 3 >> 31) & 3)) >> 2); break;
	case 3: ScaledMissionTimer = (Tuning.BaseMissionTimer * 2) / 3; break;
	case 4: ScaledMissionTimer = Tuning.BaseMissionTimer / 2; break;
	}

	NagInterval = ScaledMissionTimer >> 3;

	if (ActiveCount < MaxEasyWithDifficulty)
	{
		SpawnCountdown = (SpawnCountdown - FrameDeltaEma) + ((ActiveCount - MaxEasyWithDifficulty) + 1) * Tuning.IntervalAdj;
	}
}

void FSimCopterMissionSystem::RebuildCumulativeWeights()
{
	DifficultyTier = CareerCity.Difficulty + 1;
	
	float Sum = CareerCity.Weights[0] + CareerCity.Weights[1] + CareerCity.Weights[2] + 
				CareerCity.Weights[3] + CareerCity.Weights[4] + CareerCity.Weights[5] + CareerCity.Weights[6];
	
	if (Sum < 1.0f)
	{
		for (int i = 1; i <= 7; ++i) CumulativeWeights[i] = 0;
		return;
	}

	for (int i = 0; i < 7; ++i)
	{
		CumulativeWeights[i + 1] = static_cast<int32>((CareerCity.Weights[i] * 100.0f) / Sum);
	}

	for (int i = 2; i <= 7; ++i)
	{
		CumulativeWeights[i] += CumulativeWeights[i - 1];
	}
}

void FSimCopterMissionSystem::DispatchScheduledType(int32 Bucket)
{
	int32 RandVal = Rand.Rand();
	int16 Shf = static_cast<int16>(RandVal >> 15);
	int16 Combined = static_cast<int16>((Shf << 16) | (RandVal & 0xffff));

	if (Bucket == 1) // Fire
	{
		if (DifficultyTier == 2)
		{
			int32 Mod = Combined % 12;
			if (Mod == 0) CreateEventOfType(TYPE_CarFireEvent);
			else if (Mod == 1) CreateEventOfType(TYPE_PlaneCrash);
			else CreateEventOfType(TYPE_BuildingFire);
		}
		else if (DifficultyTier < 3 || DifficultyTier > 4)
		{
			int16 Mask = (static_cast<int16>(RandVal) ^ Shf) - Shf;
			if ((Mask & 7 ^ Shf) != Shf) CreateEventOfType(TYPE_BuildingFire);
			else CreateEventOfType(TYPE_CarFireEvent);
		}
		else
		{
			int32 Mod = Combined % 12;
			if (Mod == 0) CreateEventOfType(TYPE_CarFireEvent);
			else if (Mod == 1) CreateEventOfType(TYPE_PlaneCrash);
			else if (Mod == 2) CreateEventOfType(TYPE_TrainCrash);
			else CreateEventOfType(TYPE_BuildingFire);
		}
	}
	else if (Bucket == 2) // Crime
	{
		if (DifficultyTier == 2)
		{
			int16 Mask = (static_cast<int16>(RandVal) ^ Shf) - Shf;
			if ((Mask & 1 ^ Shf) != Shf) CreateEventOfType(TYPE_CriminalA);
			else CreateEventOfType(TYPE_SpeederEvent);
		}
		else if (DifficultyTier == 4)
		{
			int16 Mask = (static_cast<int16>(RandVal) ^ Shf) - Shf;
			int32 CaseVal = (Mask & 7 ^ Shf) - Shf;
			if (CaseVal == 0) CreateEventOfType(TYPE_CriminalC);
			else if (CaseVal == 1) CreateEventOfType(TYPE_CriminalA);
			else if (CaseVal == 2 || CaseVal == 3) CreateEventOfType(TYPE_SpeederEvent);
			else CreateEventOfType(TYPE_CriminalCar);
		}
		else
		{
			int32 Mod = Combined % 5;
			if (Mod == 0) CreateEventOfType(TYPE_CriminalA);
			else if (Mod == 1) CreateEventOfType(TYPE_SpeederEvent);
			else if (Mod == 2) CreateEventOfType(TYPE_CriminalC);
			else CreateEventOfType(TYPE_CriminalCar);
		}
	}
	else if (Bucket == 3) // Rescue
	{
		if (DifficultyTier == 2)
		{
			int16 Mask = (static_cast<int16>(RandVal) ^ Shf) - Shf;
			if ((Mask & 3 ^ Shf) != Shf) CreateEventOfType(TYPE_FireRescue);
			else CreateEventOfType(TYPE_BoatRescue);
		}
		else if (DifficultyTier == 3)
		{
			int16 Mask = (static_cast<int16>(RandVal) ^ Shf) - Shf;
			int16 Diff = (Mask & 7 ^ Shf);
			if (Diff == Shf) CreateEventOfType(TYPE_FireRescue);
			else if (static_cast<uint16>(Diff - Shf) != 1) CreateEventOfType(TYPE_BoatRescue);
			else CreateEventOfType(TYPE_TrainRescue);
		}
		else if (DifficultyTier == 4)
		{
			int32 Mod = Combined % 5;
			if (Mod == 0) CreateEventOfType(TYPE_FireRescue);
			else if (Mod == 1) CreateEventOfType(TYPE_TrainRescue);
			else CreateEventOfType(TYPE_BoatRescue);
		}
		else CreateEventOfType(TYPE_FireRescue);
	}
	else if (Bucket == 4) CreateEventOfType(TYPE_Riot);
	else if (Bucket == 5) CreateEventOfType(TYPE_TrafficJam);
	else if (Bucket == 6) CreateEventOfType(TYPE_Medevac);
	else if (Bucket == 7) CreateEventOfType(TYPE_Transport);
}

int32 FSimCopterMissionSystem::CreateEventOfType(int32 TypeMask)
{
	bool bClearConsecutive = ConsecutivePlaceFailures > 0x13;
	if (bClearConsecutive) ConsecutivePlaceFailures = 0;
	bRerollRequested = bClearConsecutive ? 1 : 0;

	auto ReturnCreation = [this](int32 CreatedId) -> int32
	{
		NoteCreationResult(CreatedId != -1);
		return CreatedId;
	};

	if (TypeMask == TYPE_PlaneCrash)
	{
		return ReturnCreation(CreateEventAt(-1, -1, TypeMask));
	}
	else if (TypeMask == TYPE_BuildingFire)
	{
		for (int i = 0; i < 10; ++i)
		{
			int32 TX, TY;
			if (TryPickRandomTileNearCamera(TX, TY))
			{
				if (IsFireSuitableTile(World ? World->GetXbldTileId(TX, TY) : 0))
				{
					int32 CreatedId = CreateEventAt(TX, TY, TypeMask);
					if (CreatedId != -1) return ReturnCreation(CreatedId);
				}
			}
		}
	}
	else if (TypeMask == TYPE_Medevac ||
		TypeMask == TYPE_Transport ||
		TypeMask == TYPE_CriminalA ||
		TypeMask == TYPE_SpeederEvent ||
		TypeMask == TYPE_CriminalC)
	{
		// FUN_004a92f0 LAB_004a95ff. Medevac, Transport and all three on-foot crime types share
		// one placement rule: five tries, and the tile has to carry a mission building. Criminals
		// come out of one, so a tile with no building on it - water above all - is never a
		// candidate, which is why an unfiltered pick could put one out in the ocean.
		for (int i = 0; i < 5; ++i)
		{
			int32 TX, TY;
			if (TryPickRandomTileNearCamera(TX, TY))
			{
				if (IsMissionBuildingTile(World ? World->GetXbldTileId(TX, TY) : 0))
				{
					int32 CreatedId = CreateEventAt(TX, TY, TypeMask);
					if (CreatedId != -1) return ReturnCreation(CreatedId);
				}
			}
		}
	}
	else if (TypeMask == TYPE_RescuePeople ||
		TypeMask == TYPE_BoatRescue ||
		TypeMask == TYPE_TrainRescue ||
		TypeMask == TYPE_CarFireEvent ||
		TypeMask == TYPE_TrafficJam ||
		TypeMask == TYPE_Riot ||
		TypeMask == TYPE_CriminalCar)
	{
		// FUN_004a92f0's unfiltered tail. Each of these places something that finds its own
		// ground - a boat on water, a car on the road network, a rioting crowd - so the tile
		// test is left to the creator, and CreateEventAt failing is what costs a try.
		for (int i = 0; i < 5; ++i)
		{
			int32 TX, TY;
			if (TryPickRandomTileNearCamera(TX, TY))
			{
				int32 CreatedId = CreateEventAt(TX, TY, TypeMask);
				if (CreatedId != -1) return ReturnCreation(CreatedId);
			}
		}
	}
	else if (TypeMask == TYPE_TrainCrash)
	{
		return ReturnCreation(CreateEventAt(-1, -1, TypeMask));
	}
	else if (TypeMask == TYPE_FireRescue)
	{
		// The one branch that leaves its loop to create: FUN_004a92f0 jumps to LAB_004a9814 on
		// the first tile that passes, so a creation failure here is not retried.
		for (int i = 0; i < 5; ++i)
		{
			int32 TX, TY;
			if (TryPickRandomTileNearCamera(TX, TY))
			{
				uint8 XbldId = World ? World->GetXbldTileId(TX, TY) : 0;
				uint8 Props = GetXbldPropertyFlags(XbldId);
				if ((Props & 4) != 0 && XbldId != 0xd1 && XbldId != 0xd2 && XbldId != 0xd3)
				{
					return ReturnCreation(CreateEventAt(TX, TY, TypeMask));
				}
			}
		}
	}
	else if (TypeMask == TYPE_Ufo)
	{
		// FUN_004a92f0 has no case for 0x100000 and places nothing; the original's UFO arrives
		// from somewhere else. Kept so the debug mission list can still summon one.
		return ReturnCreation(CreateEventAt(-1, -1, TypeMask));
	}

	NoteCreationResult(false);
	return -1;
}

bool FSimCopterMissionSystem::IsMissionBuildingTile(int32 XbldId)
{
	// FUN_004a92f0 LAB_004a95ff, literally: 0x6f < id < 0xdc, minus the three ids at 0xd1-0xd3.
	const uint8 Id = static_cast<uint8>(XbldId);
	return Id > 0x6f && Id < 0xdc && Id != 0xd1 && Id != 0xd2 && Id != 0xd3;
}

void FSimCopterMissionSystem::NoteCreationResult(bool bCreated)
{
	if (bCreated)
	{
		bRerollRequested = 1;
		ConsecutivePlaceFailures = 0;
	}
	else
	{
		ConsecutivePlaceFailures++;
	}
}

uint8 FSimCopterMissionSystem::GetXbldPropertyFlags(int32 BlockId)
{
	// FUN_0049a4d0 reads the XBLD property table. Until that table is ported,
	// treat ordinary SC2 buildings as mission-capable and keep roads/ruins out.
	const uint8 Id = static_cast<uint8>(BlockId);
	if (Id >= 0x70 && Id < 0xdc && Id != 0xd1 && Id != 0xd2 && Id != 0xd3)
	{
		return 0x04;
	}
	return 0;
}

bool FSimCopterMissionSystem::TryPickRandomTileNearCamera(int32& OutTX, int32& OutTY)
{
	int32 CamX = 64, CamY = 64;
	if (World)
	{
		World->GetCameraTile(CamX, CamY);
	}

	int32 Range = (ConsecutivePlaceFailures + 18) * DifficultyTier + 8;
	
	auto GetOffset = [&]() -> int32 {
		int32 Val1 = Rand.Rand() % Range;
		int32 Val2 = Rand.Rand() % Range;
		int32 MaxVal = FMath::Max(Val1, Val2);
		
		int32 SignRoll = Rand.Rand();
		int16 Shf = static_cast<int16>(SignRoll >> 15);
		if ((((static_cast<uint16>(SignRoll) ^ Shf) - Shf) & 1 ^ Shf) != Shf)
		{
			MaxVal = -MaxVal;
		}
		return MaxVal;
	};

	OutTX = CamX + GetOffset();
	OutTY = CamY + GetOffset();

	if (OutTX < 0 || OutTX > 127 || OutTY < 0 || OutTY > 127)
	{
		int32 R1 = Rand.Rand();
		int16 S1 = static_cast<int16>(R1 >> 15);
		OutTX = static_cast<int32>(static_cast<int16>(((((static_cast<uint16>(R1) ^ S1) - S1) & 0x7f) ^ S1) - S1));

		int32 R2 = Rand.Rand();
		int16 S2 = static_cast<int16>(R2 >> 15);
		OutTY = static_cast<int32>(static_cast<int16>(((((static_cast<uint16>(R2) ^ S2) - S2) & 0x7f) ^ S2) - S2));
	}
	
	return true;
}

bool FSimCopterMissionSystem::IsFireSuitableTile(int32 XbldId)
{
	uint8 Id = static_cast<uint8>(XbldId);
	if ((Id > 0x1c && Id < 0x6c) || Id < 5 || Id == 0xde || Id == 0xf6 || Id == 0xd2 || Id == 0xd3 || Id == 0xd1)
	{
		return false;
	}
	return true;
}

int32 FSimCopterMissionSystem::AllocateRecord()
{
	// Iterate the actual pool size (Records may have been grown past MaxRecords by a debug force).
	for (int32 i = 0; i < Records.Num(); ++i)
	{
		if (!Records[i].bActive)
		{
			Records[i] = FSimCopterMissionRecord();
			return i;
		}
	}
	return INDEX_NONE;
}

void FSimCopterMissionSystem::DebugEnsureFreeRecordSlot()
{
	for (const FSimCopterMissionRecord& Rec : Records)
	{
		if (!Rec.bActive) return;
	}
	// Pool is full: grow it so a forced mission can always allocate. Normal play never approaches
	// MaxRecords (the scheduler caps active missions well below it), so this only affects
	// debug-forced spawns and does not change deterministic behaviour.
	Records.AddDefaulted(4);
}

int32 FSimCopterMissionSystem::DebugForceBuildingFire()
{
	DebugEnsureFreeRecordSlot();
	return CreateEventOfType(TYPE_BuildingFire);
}

int32 FSimCopterMissionSystem::DebugForceCarFire()
{
	DebugEnsureFreeRecordSlot();
	return CreateEventOfType(TYPE_CarFireEvent);
}

void FSimCopterMissionSystem::ReleaseFailedRecord(int32 RecordIndex)
{
	Records[RecordIndex].bActive = false;
	NextEventId--;
}

void FSimCopterMissionSystem::AnnounceCreated(const FSimCopterMissionRecord& Record)
{
	if (Record.Category == CAT_Background)
	{
		BackgroundCount++;
		SpawnCountdown = EasyIntervalCache >> 1;
	}
	else
	{
		ActiveCount++;
		SpawnCountdown = EasyIntervalCache;
	}
	PostTypedUiMessage(5, &Record, Record.EventId, GetTypeTextId(Record.TypeMask), 0, 0, false);
	PostAnnouncementVoice(Record);
}

void FSimCopterMissionSystem::PostAnnouncementVoice(const FSimCopterMissionRecord& Record)
{
	// Handled by UI system
}

int32 FSimCopterMissionSystem::GetTypeTextId(int32 TypeMask)
{
	if ((TypeMask & TYPE_Riot) != 0) return 0x23b;
	if ((TypeMask & TYPE_FireRescue) != 0) return 0x23c;
	if ((TypeMask & TYPE_BoatRescue) != 0) return 0x23d;
	if ((TypeMask & TYPE_TrainRescue) != 0 || (TypeMask & TYPE_TrainCrash) != 0) return 0x23e;
	if ((TypeMask & TYPE_Medevac) != 0) return 0x23f;
	if ((TypeMask & TYPE_Transport) != 0) return 0x240;
	if ((TypeMask & TYPE_BuildingFire) != 0 || (TypeMask & TYPE_PlaneCrash) != 0) return 0x241;
	if ((TypeMask & TYPE_CarFire) != 0) return 0x248;
	if ((TypeMask & TYPE_TrafficJam) != 0) return 0x249;
	if ((TypeMask & TYPE_CriminalCar) != 0) return 0x244;
	if ((TypeMask & TYPE_SpeederEvent) != 0) return 0x245;
	if ((TypeMask & TYPE_CriminalC) != 0) return 0x246;
	if ((TypeMask & TYPE_CriminalA) != 0) return 0x247;
	if ((TypeMask & TYPE_Ufo) != 0) return 0x24a;
	return 0x24b;
}

int32 FSimCopterMissionSystem::GetLocationVoiceId(int32 TileX, int32 TileY)
{
	return -1;
}

const TCHAR* FSimCopterMissionSystem::GetTypeDisplayName(int32 TypeMask)
{
	// The three rescue masks are composites of the victim bit 0x10 (0x90 boat, 0x110 train,
	// 0x80010 fire), so they have to match on every bit: a plain "& mask != 0" test claims the
	// bare 0x100 train crash as a train rescue, and 0x10 as all three.
	if ((TypeMask & TYPE_Riot) != 0) return TEXT("Riot");
	if ((TypeMask & TYPE_FireRescue) == TYPE_FireRescue) return TEXT("Fire Rescue");
	if ((TypeMask & TYPE_BoatRescue) == TYPE_BoatRescue) return TEXT("Boat Rescue");
	if ((TypeMask & TYPE_TrainRescue) == TYPE_TrainRescue) return TEXT("Train Rescue");
	if ((TypeMask & TYPE_TrainCrash) != 0) return TEXT("Train Crash");
	if ((TypeMask & TYPE_RescuePeople) != 0) return TEXT("Rescue");
	if ((TypeMask & TYPE_Medevac) != 0) return TEXT("MedEvac");
	if ((TypeMask & TYPE_Transport) != 0) return TEXT("Transport");
	if ((TypeMask & TYPE_CarFire) != 0) return TEXT("Car Fire");
	if ((TypeMask & TYPE_TrafficJam) != 0) return TEXT("Traffic Jam");
	if ((TypeMask & TYPE_CriminalCar) != 0) return TEXT("Criminal Car");
	if ((TypeMask & TYPE_SpeederEvent) != 0) return TEXT("Speeder");
	if ((TypeMask & TYPE_CriminalC) != 0) return TEXT("Criminal");
	if ((TypeMask & TYPE_CriminalA) != 0) return TEXT("Criminal");
	if ((TypeMask & TYPE_PlaneCrash) != 0) return TEXT("Plane Crash");
	if ((TypeMask & TYPE_BuildingFire) != 0) return TEXT("Building Fire");
	if ((TypeMask & TYPE_Ufo) != 0) return TEXT("UFO");
	return TEXT("Mission");
}

bool FSimCopterMissionSystem::FindNearestHospitalTile(int32 OriginX, int32 OriginY, int32& OutX, int32& OutY) const
{
	if (World == nullptr)
	{
		return false;
	}

	// HO209 = XBLD building id 209 = 0xD1 (a 3x3 hospital). Injured people are delivered here.
	constexpr int32 HospitalXbldId = 0xD1;
	int32 BestX = -1;
	int32 BestY = -1;
	int32 BestDistSq = TNumericLimits<int32>::Max();
	for (int32 Y = 0; Y < 128; ++Y)
	{
		for (int32 X = 0; X < 128; ++X)
		{
			if (World->GetXbldTileId(X, Y) != HospitalXbldId)
			{
				continue;
			}
			const int32 Dx = X - OriginX;
			const int32 Dy = Y - OriginY;
			const int32 DistSq = Dx * Dx + Dy * Dy;
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestX = X;
				BestY = Y;
			}
		}
	}

	if (BestX < 0)
	{
		return false;
	}
	OutX = BestX;
	OutY = BestY;
	return true;
}

bool FSimCopterMissionSystem::FindDefaultDestinationTile(int32 OriginX, int32 OriginY, int32& OutX, int32& OutY) const
{
	auto IsInBounds = [](int32 X, int32 Y) -> bool
	{
		return X >= 0 && X < 128 && Y >= 0 && Y < 128;
	};

	auto IsUsableDestination = [this, &IsInBounds](int32 X, int32 Y) -> bool
	{
		if (!IsInBounds(X, Y))
		{
			return false;
		}

		if (World == nullptr)
		{
			return true;
		}

		const int32 XbldId = World->GetXbldTileId(X, Y);
		return (GetXbldPropertyFlags(XbldId) & 0x04) != 0;
	};

	static const FIntPoint Directions[] = {
		FIntPoint(1, 0),
		FIntPoint(1, 1),
		FIntPoint(0, 1),
		FIntPoint(-1, 1),
		FIntPoint(-1, 0),
		FIntPoint(-1, -1),
		FIntPoint(0, -1),
		FIntPoint(1, -1)
	};

	for (int32 Radius = 14; Radius <= 72; Radius += 6)
	{
		for (const FIntPoint& Direction : Directions)
		{
			const int32 CandidateX = FMath::Clamp(OriginX + Direction.X * Radius, 0, 127);
			const int32 CandidateY = FMath::Clamp(OriginY + Direction.Y * Radius, 0, 127);
			if ((CandidateX != OriginX || CandidateY != OriginY) && IsUsableDestination(CandidateX, CandidateY))
			{
				OutX = CandidateX;
				OutY = CandidateY;
				return true;
			}
		}
	}

	const int32 MirrorX = FMath::Clamp(127 - OriginX, 0, 127);
	const int32 MirrorY = FMath::Clamp(127 - OriginY, 0, 127);
	if (MirrorX != OriginX || MirrorY != OriginY)
	{
		OutX = MirrorX;
		OutY = MirrorY;
		return true;
	}

	return false;
}

int32 FSimCopterMissionSystem::CreateEventAt(int32 TX, int32 TY, int32 TypeMask)
{
	int32 RecIndex = AllocateRecord();
	if (RecIndex == INDEX_NONE) return -1;

	FSimCopterMissionRecord& Rec = Records[RecIndex];
	Rec.bActive = true;
	Rec.TileX = TX;
	Rec.TileY = TY;
	Rec.TypeMask = TypeMask;
	Rec.EventId = NextEventId++;
	Rec.Category = CAT_Active;

	if (TypeMask == TYPE_PlaneCrash)
	{
		Rec.Name = FString::Printf(TEXT("Plane Crash #%d"), TypeSerials[0]);
		TypeSerials[0]++;
		if (World && !World->TryActivatePlaneCrash(Rec.EventId))
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.Category = CAT_Background;
	}
	else if (TypeMask == TYPE_BuildingFire)
	{
		Rec.Name = FString::Printf(TEXT("Building Fire #%d"), TypeSerials[1]);
		TypeSerials[1]++;
		int32 FireObjIndex = AllocateFireObject(TX, TY);
		if (FireObjIndex == -1 || !IgniteBuilding(FireObjIndex, TX, TY, Rec.EventId, 1))
		{
			if (FireObjIndex != -1)
			{
				FireObjects[FireObjIndex].bActive = false;
			}
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
	}
	else if (TypeMask == TYPE_Transport)
	{
		if (World && !World->TryResolveTransportSpawnTile(TX, TY, Rec.TileX, Rec.TileY))
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		TX = Rec.TileX;
		TY = Rec.TileY;

		// FUN_004a7a10's 0x40 branch: `rand() % (DAT_004f9740 + 1) + 1` passengers, where
		// DAT_004f9740 is the difficulty tier (FUN_004a92f0 switches on it 1/2/3). One party of
		// ten every single time was a placeholder.
		bool bSpawned = false;
		const int32 PartySize = (Rand.Rand() % (DifficultyTier + 1)) + 1;
		for (int32 i = 0; i < PartySize; ++i)
		{
			if (World && World->TrySpawnMissionPerson(4, -1, TX, TY, Rec.EventId))
			{
				bSpawned = true;
				Rec.TransportPassengers++;
			}
		}
		if (!bSpawned)
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.Name = FString::Printf(TEXT("Transport #%d"), TypeSerials[3]);
		TypeSerials[3]++;
		FindDefaultDestinationTile(TX, TY, Rec.SecondaryX, Rec.SecondaryY);
	}
	else if (TypeMask == TYPE_Medevac)
	{
		bool bSpawned = false;
		int32 Count = (Rand.Rand() % DifficultyTier) + 1;
		for (int32 i = 0; i < Count; ++i)
		{
			if (World && World->TrySpawnMissionPerson(6, -1, TX, TY, Rec.EventId))
			{
				bSpawned = true;
				Rec.MedevacVictims++;
			}
		}
		if (!bSpawned)
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.Name = FString::Printf(TEXT("MedEvac #%d"), TypeSerials[2]);
		TypeSerials[2]++;
		// Deliver injured people to the nearest hospital; fall back to a generic mission building
		// if the city has no hospital.
		if (!FindNearestHospitalTile(TX, TY, Rec.SecondaryX, Rec.SecondaryY))
		{
			FindDefaultDestinationTile(TX, TY, Rec.SecondaryX, Rec.SecondaryY);
		}
	}
	else if (TypeMask == TYPE_TrainCrash)
	{
		Rec.Name = FString::Printf(TEXT("Train Crash #%d"), TypeSerials[5]);
		TypeSerials[5]++;
		if (World && !World->TryActivateTrainCrash(Rec.EventId))
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.Category = CAT_Background;
	}
	else if (TypeMask == TYPE_BoatRescue)
	{
		// FUN_004a7a10 passes the placer's tile and DAT_00505fc8 (the difficulty-scaled mission
		// timer) straight through, and leaves Secondary/Tertiary at -1: a boat rescue has no
		// delivery tile, only survivors to get out of the water.
		int32 OutX, OutY;
		if (!World || !World->TryActivateBoatRescue(Rec.EventId, ScaledMissionTimer, TX, TY, OutX, OutY))
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.TileX = OutX;
		Rec.TileY = OutY;
		Rec.Name = FString::Printf(TEXT("Boat Rescue #%d"), TypeSerials[4]);
		TypeSerials[4]++;
	}
	else if (TypeMask == TYPE_CriminalA)
	{
		bool bSpawned = false;
		if (World && World->TrySpawnMissionPerson(10, 9, TX, TY, Rec.EventId))
		{
			bSpawned = true;
			Rec.CriminalsCaught = 0;
			Rec.TargetCount = 1;
		}
		if (!bSpawned)
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.Name = FString::Printf(TEXT("Criminal A #%d"), TypeSerials[6]);
		TypeSerials[6]++;
	}
	else if (TypeMask == TYPE_TrainRescue)
	{
		// FUN_004b7fb0(eventId, DAT_00505fc8). Like the boat rescue, no delivery tile.
		int32 OutX, OutY;
		if (!World || !World->TryActivateTrainRescue(Rec.EventId, ScaledMissionTimer, OutX, OutY))
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.TileX = OutX;
		Rec.TileY = OutY;
		Rec.Name = FString::Printf(TEXT("Train Rescue #%d"), TypeSerials[7]);
		TypeSerials[7]++;
	}
	else if (TypeMask == TYPE_TrafficJam)
	{
		int32 OutX, OutY;
		if (!World || !World->TryStartTrafficJam(Rec.EventId, OutX, OutY))
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.TileX = OutX;
		Rec.TileY = OutY;
		Rec.Name = FString::Printf(TEXT("Traffic Jam #%d"), TypeSerials[8]);
		TypeSerials[8]++;
		Rec.Category = CAT_Background;
	}
	else if (TypeMask == TYPE_CarFireEvent)
	{
		int32 OutX, OutY;
		if (!World || !World->TryStartCarFire(Rec.EventId, OutX, OutY))
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.TileX = OutX;
		Rec.TileY = OutY;
		Rec.CarsCrashed = FMath::Max(Rec.CarsCrashed, 1);
		Rec.Name = FString::Printf(TEXT("Car Fire #%d"), TypeSerials[9]);
		TypeSerials[9]++;
	}
	else if (TypeMask == TYPE_SpeederEvent)
	{
		bool bSpawned = false;
		if (World && World->TrySpawnMissionPerson(11, 9, TX, TY, Rec.EventId))
		{
			bSpawned = true;
			Rec.TargetCount = 1;
		}
		if (!bSpawned)
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.Name = FString::Printf(TEXT("Speeder #%d"), TypeSerials[10]);
		TypeSerials[10]++;
	}
	else if (TypeMask == TYPE_Riot)
	{
		int32 Count = (Rand.Rand() % 8) * (DifficultyTier - 2) + 16;
		int32 Spawned = 0;
		for (int32 i = 0; i < Count; ++i)
		{
			if (World && World->TrySpawnMissionPerson(3, -1, TX, TY, Rec.EventId))
			{
				Spawned++;
			}
		}
		if (Spawned < 11)
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.RiotSize = Spawned;
		Rec.TargetCount = 0; // Elapsed nag periods
		Rec.Name = FString::Printf(TEXT("Riot #%d"), TypeSerials[11]);
		TypeSerials[11]++;
	}
	else if (TypeMask == TYPE_CriminalC)
	{
		bool bSpawned = false;
		if (World && World->TrySpawnMissionPerson(12, 9, TX, TY, Rec.EventId))
		{
			bSpawned = true;
			Rec.TargetCount = 1;
		}
		if (!bSpawned)
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.Name = FString::Printf(TEXT("Criminal C #%d"), TypeSerials[12]);
		TypeSerials[12]++;
	}
	else if (TypeMask == TYPE_CriminalCar)
	{
		if (World && !World->TryActivateSpeederCar(Rec.EventId, TX, TY))
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		// FUN_004a7a10's 0x4000 branch writes 1 to the record's +0x94 right after the car is
		// placed. Without it TargetCount stays 0, the crime completion test below reads
		// `0 + 0 < 0` as satisfied, and the mission closes itself on its first update.
		Rec.TargetCount = 1;
		Rec.CriminalsCaught = 0;
		Rec.Name = FString::Printf(TEXT("Criminal Car #%d"), TypeSerials[13]);
		TypeSerials[13]++;
	}
	else if (TypeMask == TYPE_FireRescue)
	{
		int32 Count = (Rand.Rand() % DifficultyTier) + 1;
		bool bSpawned = false;
		for (int32 i = 0; i < Count; ++i)
		{
			if (World && World->TrySpawnMissionPerson(2, -1, TX, TY, Rec.EventId))
			{
				bSpawned = true;
				Rec.RescueVictims++;
			}
		}
		if (!bSpawned)
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.Name = FString::Printf(TEXT("Fire Rescue #%d"), TypeSerials[14]);
		TypeSerials[14]++;
		FindDefaultDestinationTile(TX, TY, Rec.SecondaryX, Rec.SecondaryY);
	}
	else if (TypeMask == TYPE_Ufo)
	{
		Rec.Name = TEXT("UFO");
	}
	else
	{
		ReleaseFailedRecord(RecIndex);
		return -1;
	}

	AnnounceCreated(Rec);
	return Rec.EventId;
}

int32 FSimCopterMissionSystem::CreatePlayerCausedMedevacAt(int32 TileX, int32 TileY)
{
	const int32 RecIndex = AllocateRecord();
	if (RecIndex == INDEX_NONE)
	{
		return -1;
	}

	FSimCopterMissionRecord& Rec = Records[RecIndex];
	Rec.bActive = true;
	Rec.TileX = TileX;
	Rec.TileY = TileY;
	Rec.TypeMask = TYPE_Medevac;
	Rec.EventId = NextEventId++;
	Rec.Category = CAT_Active;
	Rec.MedevacVictims = 1;
	// You hurt them, so you do not get paid for carting them to hospital. The original did pay
	// out, which made deliberately mowing people down and delivering them a profitable strategy;
	// this is a deliberate remake divergence.
	Rec.bSuppressCompletionRewards = true;

	// REMAKE DIVERGENCE: an immediate fine for putting a civilian in hospital. With the completion
	// reward suppressed above, the incentive runs the right way round - hurting someone costs you,
	// and letting them die costs you again, so the cheapest thing you can do is fly carefully and
	// the second cheapest is to get them treated.
	Cash -= PlayerCausedInjuryFine;
	if (World)
	{
		FSimCopterMissionUiMessage Message;
		Message.Kind = 1;
		Message.EventId = Rec.EventId;
		Message.ValueB = -PlayerCausedInjuryFine;
		Message.TypeMask = Rec.TypeMask;
		Message.MissionName = TEXT("Civilian injured");
		Message.bNegative = true;
		World->OnUiMessage(Message);
		World->PlayUiSound(0x1e);
	}
	Rec.Name = FString::Printf(TEXT("MedEvac #%d"), TypeSerials[2]);
	TypeSerials[2]++;

	if (!FindNearestHospitalTile(TileX, TileY, Rec.SecondaryX, Rec.SecondaryY))
	{
		FindDefaultDestinationTile(TileX, TileY, Rec.SecondaryX, Rec.SecondaryY);
	}

	AnnounceCreated(Rec);
	return Rec.EventId;
}

void FSimCopterMissionSystem::AdjustVictimsPickedUp(int32 EventId, int32 Delta)
{
	for (FSimCopterMissionRecord& Rec : Records)
	{
		if (Rec.bActive && Rec.EventId == EventId)
		{
			Rec.VictimsPickedUp = FMath::Max(0, Rec.VictimsPickedUp + Delta);
			return;
		}
	}
}


void FSimCopterMissionSystem::UpdateLifecycle()
{
	for (int32 i = 0; i < Records.Num(); ++i)
	{
		FSimCopterMissionRecord& Rec = Records[i];
		if (!Rec.bActive)
		{
			continue;
		}

		// Advance timer
		Rec.TimeAccum += FrameDeltaEma;

		if (Rec.Category == CAT_CompleteNow)
		{
			CompleteMission(Rec);
			DeactivateRecord(i);
			continue;
		}

		// Traffic jam expiry (90 seconds = 0x5a0000)
		if ((Rec.TypeMask & TYPE_TrafficJam) != 0)
		{
			if (Rec.CarsCleared + Rec.CarsBurned >= Rec.JamCarCount && Rec.JamCarCount > 0)
			{
				CompleteMission(Rec);
				DeactivateRecord(i);
				continue;
			}

			if (Rec.TimeAccum > 0x5a0000)
			{
				if (World)
				{
					World->EndTrafficJam(Rec.EventId);
				}
				DeactivateRecord(i); 
				continue;
			}
		}

		// FUN_004a73e0's `category == 4` arm: retire the record on the spot, no scoring, no
		// completion message. That is how a plane crash whose fire became its own mission gets
		// out of the way (FUN_004b2cd0 posts EVT_SetCategory 4 on the plane's own record).
		if (Rec.Category == CAT_ExpireSilently)
		{
			if ((Rec.TypeMask & TYPE_TrafficJam) != 0 && World)
			{
				World->EndTrafficJam(Rec.EventId);
			}
			DeactivateRecord(i);
			continue;
		}

		if (Rec.Category == CAT_Background)
		{
			continue;
		}

		bool bComplete = true;
		if ((Rec.TypeMask & TYPE_Debris) != 0 && Rec.DebrisDoused + Rec.DebrisExpired + Rec.DebrisCleared < Rec.DebrisCreated)
		{
			bComplete = false;
		}
		if ((Rec.TypeMask & TYPE_BuildingFire) != 0 && Rec.FlamesDoused + Rec.FlamesExpired < Rec.FlamesCreated)
		{
			bComplete = false;
		}
		if ((Rec.TypeMask & TYPE_Medevac) != 0 && Rec.MedevacDelivered + Rec.Casualties < Rec.MedevacVictims)
		{
			bComplete = false;
		}
		if ((Rec.TypeMask & TYPE_RescuePeople) != 0 && Rec.RescueDelivered + Rec.Casualties < Rec.RescueVictims)
		{
			bComplete = false;
		}
		if ((Rec.TypeMask & TYPE_Transport) != 0 && Rec.TransportDelivered + Rec.Casualties + Rec.PassengersLost < Rec.TransportPassengers)
		{
			bComplete = false;
		}
		if ((Rec.TypeMask & TYPE_Riot) != 0 && Rec.RiotersDispersed + Rec.Casualties + Rec.CriminalsCaught + Rec.RiotersCalmed < Rec.RiotSize)
		{
			bComplete = false;
		}
		if ((Rec.TypeMask & TYPE_CarFire) != 0 && Rec.CarsDoused + Rec.CarsBurned < Rec.CarsCrashed)
		{
			bComplete = false;
		}
		if (((Rec.TypeMask & TYPE_CriminalCar) != 0 || (Rec.TypeMask & TYPE_CriminalA) != 0 ||
			(Rec.TypeMask & TYPE_SpeederEvent) != 0 || (Rec.TypeMask & TYPE_CriminalC) != 0) &&
			Rec.CriminalsCaught + Rec.Casualties < Rec.TargetCount)
		{
			bComplete = false;
		}

		if (bComplete)
		{
			CompleteMission(Rec);
			DeactivateRecord(i);
		}
	}
}

int32 FSimCopterMissionSystem::AllocateFireObject(int32 TileX, int32 TileY)
{
	for (int32 i = 0; i < MaxFireObjects; ++i)
	{
		if (!FireObjects[i].bActive)
		{
			FireObjects[i].bActive = true;
			FireObjects[i].TileX = TileX;
			FireObjects[i].TileY = TileY;
			FireObjects[i].FlameCount = 0;
			FireObjects[i].bRescueSpawned = false;
			return i;
		}
	}
	return -1;
}

bool FSimCopterMissionSystem::HasFlameOnTile(int32 TileX, int32 TileY) const
{
	// Stands in for the cell's 0x20 "burning" flag, which FUN_004a48e0 sets on the first
	// flame and FUN_004a4ac0 / FUN_004a50c0 clear when the last one goes.
	for (const FSimCopterFlame& Flame : Flames)
	{
		if (Flame.bActive && Flame.TileX == TileX && Flame.TileY == TileY)
		{
			return true;
		}
	}
	return false;
}

bool FSimCopterMissionSystem::CanIgniteCrashSite(int32 TileX, int32 TileY) const
{
	if (World == nullptr)
	{
		return false;
	}
	return IsFireSuitableTile(World->GetXbldTileId(TileX, TileY)) && !IsAnyFireNear(TileX, TileY);
}

bool FSimCopterMissionSystem::IsAnyFireNear(int32 TileX, int32 TileY) const
{
	// FUN_004a6860 walks an outward square spiral with run lengths 1,1,2,2,...,8,8 plus a
	// final run of 8, stopping at the first burning cell.
	int32 X = TileX;
	int32 Y = TileY;
	int32 Leg = -1;
	int32 RunLength = 0;
	int32 StepX = 0;
	int32 StepY = 0;
	bool bFinalLeg = false;

	while (true)
	{
		++Leg;
		switch (Leg)
		{
		case 0:
		case 4:
			StepX = 0;
			StepY = -1;
			++RunLength;
			Leg = 0;
			break;
		case 1:
			StepX = 1;
			StepY = 0;
			break;
		case 2:
			StepX = 0;
			StepY = 1;
			++RunLength;
			break;
		case 3:
			StepX = -1;
			StepY = 0;
			break;
		}

		if (RunLength == 9)
		{
			bFinalLeg = true;
			RunLength = 8;
		}

		for (int32 Step = 0; Step < RunLength; ++Step)
		{
			X += StepX;
			Y += StepY;
			if (HasFlameOnTile(X, Y))
			{
				return true;
			}
		}

		if (bFinalLeg)
		{
			return false;
		}
	}
}

bool FSimCopterMissionSystem::IgniteBuilding(int32 FireObjectIndex, int32 TileX, int32 TileY, int32 EventId, int32 Flags)
{
	// FUN_004a5340 refuses a cell that is already burning, then re-runs the same XBLD
	// suitability test the placer used.
	if (HasFlameOnTile(TileX, TileY))
	{
		return false;
	}
	if (!IsFireSuitableTile(World != nullptr ? World->GetXbldTileId(TileX, TileY) : 0))
	{
		return false;
	}

	bool bSpawned = false;
	auto Spawn = [&](int32 OffsetX, int32 OffsetZ, int32 AxisFlag)
	{
		bSpawned |= SpawnFlame(
			FireObjectIndex,
			TileX,
			TileY,
			OffsetX,
			OffsetZ,
			AxisFlag,
			EventId,
			Flags);
	};
	auto OneIn = [&](int32 Divisor)
	{
		return static_cast<int32>(Rand.Rand() % static_cast<uint32>(Divisor)) == 0;
	};

	const int32 Footprint = World != nullptr ? World->GetBuildingFootprintSize(TileX, TileY) : 1;
	switch (Footprint)
	{
	case 2:
		Spawn(-0x200000, 0x300000, 2);
		if (OneIn(10)) Spawn(0x200000, 0x300000, 2);
		if (OneIn(5)) Spawn(-0x200000, -0x300000, 4);
		if (OneIn(10)) Spawn(0x200000, -0x300000, 4);
		if (OneIn(7)) Spawn(-0x300000, 0x200000, 0x10);
		if (OneIn(10)) Spawn(0x300000, 0x200000, 8);
		if (OneIn(6)) Spawn(-0x300000, -0x200000, 0x10);
		if (OneIn(10)) Spawn(0x300000, -0x200000, 8);
		break;
	case 3:
		Spawn(0x500000, 0x400000, 8);
		if (OneIn(10)) Spawn(0x500000, 0, 8);
		if (OneIn(3)) Spawn(0x500000, -0x400000, 8);
		if (OneIn(10)) Spawn(-0x500000, 0x400000, 0x10);
		if ((Rand.Rand() & 3u) == 0) Spawn(-0x500000, 0, 0x10);
		if (OneIn(10)) Spawn(-0x500000, -0x400000, 0x10);
		if (OneIn(10)) Spawn(0x400000, 0x500000, 2);
		if ((Rand.Rand() & 7u) == 0) Spawn(0, 0x500000, 2);
		if (OneIn(10)) Spawn(-0x400000, 0x500000, 2);
		if (OneIn(9)) Spawn(0x400000, -0x500000, 4);
		if (OneIn(3)) Spawn(0, -0x500000, 4);
		Spawn(-0x400000, -0x500000, 4);
		break;
	case 4:
		Spawn(0x700000, 0x600000, 8);
		if (OneIn(3)) Spawn(0x700000, 0x200000, 8);
		Spawn(0x700000, -0x200000, 8);
		if (OneIn(5)) Spawn(0x700000, -0x600000, 8);
		if (OneIn(10)) Spawn(-0x700000, 0x600000, 0x10);
		if (OneIn(7)) Spawn(-0x700000, 0x200000, 0x10);
		Spawn(-0x700000, -0x200000, 0x10);
		if (OneIn(7)) Spawn(-0x700000, -0x600000, 0x10);
		if ((Rand.Rand() & 7u) == 0) Spawn(0x600000, 0x700000, 2);
		if (OneIn(10)) Spawn(0x200000, 0x700000, 2);
		if (OneIn(10)) Spawn(-0x200000, 0x700000, 2);
		if ((Rand.Rand() & 3u) == 0) Spawn(-0x600000, 0x700000, 2);
		if (OneIn(10)) Spawn(0x600000, -0x700000, 4);
		if (OneIn(6)) Spawn(0x200000, -0x700000, 4);
		if (OneIn(3)) Spawn(-0x200000, -0x700000, 4);
		Spawn(-0x600000, -0x700000, 4);
		break;
	default:
		if (OneIn(10)) Spawn(0, -0x100000, 0);
		Spawn(0, 0x100000, 0);
		break;
	}

	if (!bSpawned)
	{
		return false;
	}

	// Flags is the original's silent byte. FUN_004a7a10 passes 1 for the ignition that
	// starts the mission - its flames cost the player nothing and it sizes the end award
	// by the footprint - while FUN_004a4fb0's spread ignition passes 0, so every flame a
	// fire spreads into docks the "Flame($)" score penalty as it appears.
	const bool bSilent = (Flags & 1) != 0;
	PostEvent(EVT_StructureIgnited, EventId, 1, bSilent);
	if (bSilent)
	{
		PostEvent(EVT_SetEndMoneyScaled, EventId, Footprint * Tuning.FireEndMoneyPerSize, true);
		PostEvent(EVT_SetEndPointsScaled, EventId, Footprint * Tuning.FireEndPointsPerSize, true);
	}

	if (World != nullptr)
	{
		World->OnBuildingFireIgnited(TileX, TileY, EventId);
	}
	return true;
}

bool FSimCopterMissionSystem::SpawnFlame(int32 FireObjectIndex, int32 TileX, int32 TileY, int32 OffsetX, int32 OffsetZ, int32 AxisFlag, int32 EventId, int32 Flags)
{
	// FUN_004a48e0 takes the first free slot and gives up when the 0x8c-slot table is full.
	int32 SlotIndex = INDEX_NONE;
	for (int32 i = 0; i < MaxFlames; ++i)
	{
		if (!Flames[i].bActive)
		{
			SlotIndex = i;
			break;
		}
	}
	if (SlotIndex == INDEX_NONE || FireObjectIndex < 0 || !FireObjects.IsValidIndex(FireObjectIndex))
	{
		return false;
	}

	// The original bails when the cell's display list is empty - nothing to burn.
	const int32 Footprint = World != nullptr ? World->GetBuildingFootprintSize(TileX, TileY) : 1;
	if (Footprint <= 0)
	{
		return false;
	}
	// +0x94: the object flagged 4 is the building structure the flame climbs. Its top
	// (or, with no such object, the tallest object in the cell) sets the storey height.
	const int32 StructureTop = World != nullptr ? World->GetBuildingTopHeight1616(TileX, TileY) : 0;

	FSimCopterFlame& Flame = Flames[SlotIndex];
	Flame = FSimCopterFlame();
	Flame.bActive = true;
	Flame.TileX = TileX;
	Flame.TileY = TileY;
	Flame.EventId = EventId;
	Flame.FireObjectIndex = FireObjectIndex;
	Flame.GrowthAxisFlags = AxisFlag;
	Flame.PosX = OffsetX;
	Flame.PosY = 0;
	Flame.PosZ = OffsetZ;
	Flame.ClimbTargetObject = StructureTop > 0 ? 1 : 0;
	// Single-cell buildings get a flat 32.0-unit step and never grow; larger ones split
	// the structure into `Footprint` storeys and climb `Footprint - 1` of them.
	Flame.GrowthStep1616 = Footprint < 2 ? 0x200000 : StructureTop / Footprint;
	Flame.GrowthStepsRemaining = Footprint - 1;
	Flame.DouseHealth1616 = GetFlameDouseHealth();
	Flame.BurnCountdown = GetFlameBurnTime();
	Flame.DamageCountdown = 0x8000;

	// Flags is the original's silent byte: the mission's own ignition is free, flames
	// created by a spreading fire are not.
	PostEvent(EVT_FlameCreated, EventId, 1, (Flags & 1) != 0);
	ActiveFlameCount++;
	FireObjects[FireObjectIndex].FlameCount++;

	if (World != nullptr)
	{
		World->OnFlameSpawned(Flame, SlotIndex);
	}
	return true;
}

void FSimCopterMissionSystem::RetireFlame(int32 FlameIndex, int32 EmptyEventCode)
{
	FSimCopterFlame& Flame = Flames[FlameIndex];
	const int32 FireObjectIndex = Flame.FireObjectIndex;
	if (!FireObjects.IsValidIndex(FireObjectIndex))
	{
		return;
	}

	FSimCopterFireObject& FireObject = FireObjects[FireObjectIndex];
	FireObject.FlameCount--;
	if (FireObject.FlameCount > 0)
	{
		return;
	}

	// The building has no flames left: the original clears the cell's 0x20 "burning"
	// flag and reports the outcome (4 = burned out, 6 = saved by water).
	FireObject.bActive = false;
	PostEvent(EmptyEventCode, Flame.EventId, 1, false);

	// FUN_004a4ac0 / FUN_004a50c0: if the mission marker still points at this cell,
	// move it to a surviving flame of the same event so a spread fire keeps a marker.
	const int32 RecordIndex = FindRecordIndex(Flame.EventId);
	if (Records.IsValidIndex(RecordIndex) &&
		Records[RecordIndex].TileX == Flame.TileX &&
		Records[RecordIndex].TileY == Flame.TileY)
	{
		for (int32 i = 0; i < MaxFlames; ++i)
		{
			if (Flames[i].bActive && Flames[i].EventId == Flame.EventId)
			{
				FSimCopterMissionEvent Move;
				Move.Code = EVT_SetPrimaryCoords;
				Move.EventId = Flames[i].EventId;
				Move.X = Flames[i].TileX;
				Move.Y = Flames[i].TileY;
				PostEvent(Move);
				break;
			}
		}
	}
}

void FSimCopterMissionSystem::RemoveFlame(int32 FlameIndex, bool bDoused)
{
	if (FlameIndex < 0 || FlameIndex >= MaxFlames) return;
	FSimCopterFlame& Flame = Flames[FlameIndex];
	if (!Flame.bActive) return;

	const int32 TileX = Flame.TileX;
	const int32 TileY = Flame.TileY;

	Flame.bActive = false;
	ActiveFlameCount--;

	PostEvent(bDoused ? EVT_FlameDoused : EVT_FlameExpired, Flame.EventId, 1, false);

	const bool bWasLastFlame =
		FireObjects.IsValidIndex(Flame.FireObjectIndex) && FireObjects[Flame.FireObjectIndex].FlameCount <= 1;

	// EVT_CellBurnedOut (4) when the fire consumed the building, EVT_ObjectCaughtFire (6)
	// - the "Bldg Saved" award - when water put the last flame out.
	RetireFlame(FlameIndex, bDoused ? EVT_ObjectCaughtFire : EVT_CellBurnedOut);

	if (World != nullptr)
	{
		World->OnFlameRemoved(FlameIndex);
		// FUN_004a5fd0: only the burn-out path demolishes the structure.
		if (bWasLastFlame && !bDoused)
		{
			World->OnBuildingBurnedDown(TileX, TileY, World->GetBuildingFootprintSize(TileX, TileY));
		}
	}
}

void FSimCopterMissionSystem::GrowFlame(int32 FlameIndex)
{
	FSimCopterFlame& Flame = Flames[FlameIndex];

	// FUN_004a4ac0 offers the wall-surface query the position one storey higher; the
	// query re-projects the horizontal axis named by the growth flags onto that wall and
	// fails once the flame passes the top. (Footprint - 1) steps of buildingTop/footprint
	// never reach the top, so the climb is bounded by GrowthStepsRemaining alone.
	Flame.PosY += Flame.GrowthStep1616;
	Flame.GrowthStepsRemaining--;

	// Each storey re-arms the full burn and douse pools.
	Flame.DouseHealth1616 = GetFlameDouseHealth();
	Flame.BurnCountdown = GetFlameBurnTime();

	if (World != nullptr)
	{
		World->OnFlameGrown(Flame, FlameIndex);
	}
}

void FSimCopterMissionSystem::UpdateFires()
{
	if (ActiveFlameCount <= 0) return;

	for (int32 i = 0; i < MaxFlames; ++i)
	{
		if (!Flames[i].bActive)
		{
			continue;
		}

		// People trapped in a burning mission building: once the fire is well along
		// (under (tier * 5 + 15) * 4.0 seconds left) the original raises one 0x80010
		// rescue per fire object, and only above difficulty tier 1.
		if (FireObjects.IsValidIndex(Flames[i].FireObjectIndex))
		{
			FSimCopterFireObject& FireObject = FireObjects[Flames[i].FireObjectIndex];
			if (!FireObject.bRescueSpawned &&
				DifficultyTier > 1 &&
				Flames[i].BurnCountdown < (DifficultyTier * 5 + 15) * 0x40000 &&
				World != nullptr &&
				(GetXbldPropertyFlags(World->GetXbldTileId(Flames[i].TileX, Flames[i].TileY)) & 4) != 0)
			{
				if (CreateEventAt(Flames[i].TileX, Flames[i].TileY, TYPE_FireRescue) != -1)
				{
					FireObject.bRescueSpawned = true;
				}
			}
		}

		Flames[i].BurnCountdown -= FrameDeltaEma;

		if (Flames[i].BurnCountdown >= 1)
		{
			// FUN_004a6370(flame, 6): burn people and objects standing in the flame's
			// bounds, at most once every 0.5 simulated seconds.
			Flames[i].DamageCountdown -= FrameDeltaEma;
			if (Flames[i].DamageCountdown < 1)
			{
				Flames[i].DamageCountdown = 0x8000;
				if (World != nullptr)
				{
					World->DamageInFlameBounds(Flames[i], Flames[i].EventId);
				}
			}

			// The spread clock is a single global accumulator advanced once per active
			// flame per frame, so a large fire reaches the next spread roll sooner. Both
			// the interval and the 1-in-N roll tighten with difficulty.
			SpreadAccumulator += FrameDeltaEma;
			if ((1 - DifficultyTier) * 0x40000 + Tuning.FireSpreadInterval < SpreadAccumulator)
			{
				SpreadAccumulator = 0;
				const int32 Divisor = FMath::Max(1, (1 - DifficultyTier) * 10 + Tuning.FireSpreadProb);
				if (Rand.Rand() % Divisor == 0)
				{
					SpreadFireFrom(Flames[i]);
				}
			}
			continue;
		}

		// The burn elapsed. Climb one storey if the structure has one left, otherwise
		// this flame is done and may take the building with it.
		if (Flames[i].GrowthStepsRemaining < 1)
		{
			RemoveFlame(i, /*bDoused*/ false);
		}
		else if (Flames[i].ClimbTargetObject == 0)
		{
			// No structure to climb: the original zeroes the remaining steps so the
			// flame expires on the next pass.
			Flames[i].GrowthStepsRemaining = 0;
		}
		else
		{
			GrowFlame(i);
		}
	}
}

int32 FSimCopterMissionSystem::DouseAt(int32 WorldX1616, int32 WorldY1616, int32 /*WorldZ1616*/)
{
	// World units are 16.16 with a tile = 0x400000 (64.0 units), so tile = world >> 22
	// and the remainder is the offset from the cell origin.
	return DouseAtLocalOffset(
		WorldX1616 >> 22,
		WorldY1616 >> 22,
		WorldX1616 - ((WorldX1616 >> 22) << 22),
		WorldY1616 - ((WorldY1616 >> 22) << 22),
		0x10000);
}

int32 FSimCopterMissionSystem::DouseAtTile(int32 TileX, int32 TileY)
{
	// No sub-cell impact point available: aim at the cell origin the flame offsets are
	// measured from. Flames further out than Fire Radius (the outer walls of a large
	// building) still need the caller to supply a real offset.
	return DouseAtLocalOffset(TileX, TileY, 0, 0, 0x10000);
}

int32 FSimCopterMissionSystem::DouseAtLocalOffset(int32 TileX, int32 TileY, int32 LocalX1616, int32 LocalZ1616, int32 Strength1616)
{
	if (ActiveFlameCount <= 0)
	{
		return 0;
	}

	// FUN_004a50c0: Fire Radius, widened on the easiest tier.
	const int32 Radius = (1 - DifficultyTier) * 0x80000 + Tuning.FireRadius;
	// ((1 - tier) * 3 + "Douse Mult") scaled by the water particle's strength. The mult
	// is a plain integer and the strength is 16.16, so the product is 16.16 like the
	// health pool it drains.
	const int32 Damage = FMath::Max(
		0,
		static_cast<int32>((static_cast<int64>((1 - DifficultyTier) * 3 + Tuning.FireDouseMult) * Strength1616)));

	// A large building stores all of its flames against one anchor tile, even when authored
	// offsets put the visible flame one or more tiles away. Compare absolute source-runtime
	// coordinates so water landing on that visible flame still reaches it.
	constexpr int64 TileSpan1616 = 0x400000;
	const int64 ImpactGlobalX1616 =
		static_cast<int64>(TileY) * TileSpan1616 + LocalX1616;
	const int64 ImpactGlobalZ1616 =
		-static_cast<int64>(TileX) * TileSpan1616 + LocalZ1616;

	int32 InRange = 0;
	for (int32 i = 0; i < MaxFlames; ++i)
	{
		FSimCopterFlame& Flame = Flames[i];
		if (!Flame.bActive)
		{
			continue;
		}

		const int64 FlameGlobalX1616 =
			static_cast<int64>(Flame.TileY) * TileSpan1616 + Flame.PosX;
		const int64 FlameGlobalZ1616 =
			-static_cast<int64>(Flame.TileX) * TileSpan1616 + Flame.PosZ;
		const int64 DeltaX1616 = FMath::Abs(ImpactGlobalX1616 - FlameGlobalX1616);
		const int64 DeltaZ1616 = FMath::Abs(ImpactGlobalZ1616 - FlameGlobalZ1616);

		// Water on the spatial cell stalls every flame in it: the original pushes each burn
		// countdown out by 3.0s before the range test, so a watched fire stops climbing.
		if (DeltaX1616 < TileSpan1616 && DeltaZ1616 < TileSpan1616)
		{
			Flame.BurnCountdown += 0x30000;
		}

		if (DeltaX1616 >= Radius || DeltaZ1616 >= Radius)
		{
			continue;
		}

		++InRange;
		Flame.DouseHealth1616 -= Damage;
		if (Flame.DouseHealth1616 < 0)
		{
			RemoveFlame(i, /*bDoused*/ true);
		}
	}
	return InRange;
}

void FSimCopterMissionSystem::SpreadFireFrom(const FSimCopterFlame& Flame)
{
	// FUN_004a4fb0 picks one of the four edge neighbours from the table at 0x00505f60:
	// (-1,0) (1,0) (0,-1) (0,1).
	static const int32 SpreadOffsets[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
	const int32 Choice = Rand.Rand() & 3;
	const int32 NewX = Flame.TileX + SpreadOffsets[Choice][0];
	const int32 NewY = Flame.TileY + SpreadOffsets[Choice][1];

	if (Flame.EventId == -1)
	{
		return;
	}

	// Skip cells that are already burning - the original walks the flame table looking
	// for a live flame whose fire object owns this cell.
	for (int32 i = 0; i < MaxFlames; ++i)
	{
		if (Flames[i].bActive &&
			FireObjects.IsValidIndex(Flames[i].FireObjectIndex) &&
			FireObjects[Flames[i].FireObjectIndex].TileX == NewX &&
			FireObjects[Flames[i].FireObjectIndex].TileY == NewY)
		{
			return;
		}
	}

	// The spread cell gets its own fire object, so it burns down (or is saved) on its
	// own schedule while staying part of the same mission.
	const int32 FireObjectIndex = AllocateFireObject(NewX, NewY);
	if (FireObjectIndex == -1)
	{
		return;
	}
	if (!IgniteBuilding(FireObjectIndex, NewX, NewY, Flame.EventId, 0))
	{
		FireObjects[FireObjectIndex].bActive = false;
	}
}

void FSimCopterMissionSystem::PostEvent(const FSimCopterMissionEvent& Event)
{
	int32 Idx = FindRecordIndex(Event.EventId);
	PayIncremental(Event, Idx);

	if (Idx == INDEX_NONE || !Records.IsValidIndex(Idx) || !Records[Idx].bActive)
	{
		return;
	}

	FSimCopterMissionRecord& Rec = Records[Idx];
	switch (Event.Code)
	{
	case EVT_SetPrimaryCoords:
		Rec.TileX = Event.X;
		Rec.TileY = Event.Y;
		break;
	case EVT_FlameCreated:
		Rec.FlamesCreated += Event.Value;
		break;
	case EVT_FlameDoused:
		Rec.FlamesDoused += Event.Value;
		break;
	case EVT_FlameExpired:
		Rec.FlamesExpired += Event.Value;
		break;
	case EVT_CellBurnedOut:
		Rec.CellsBurnedOut += Event.Value;
		break;
	case EVT_StructureIgnited:
		Rec.StructuresIgnited += Event.Value;
		break;
	case EVT_ObjectCaughtFire:
		Rec.ObjectsCaughtFire += Event.Value;
		break;
	case EVT_DebrisCreated:
		if ((Rec.TypeMask & (TYPE_BuildingFire | TYPE_PlaneCrash | TYPE_TrainCrash | TYPE_CarFire | TYPE_Debris)) != 0)
		{
			Rec.TypeMask |= TYPE_Debris;
			Rec.DebrisCreated += Event.Value;
		}
		break;
	case EVT_DebrisExpired:
		Rec.DebrisExpired += Event.Value;
		break;
	case EVT_DebrisDoused:
		Rec.DebrisDoused += Event.Value;
		break;
	case EVT_SetSecondaryCoords:
		Rec.SecondaryX = Event.X;
		Rec.SecondaryY = Event.Y;
		break;
	case EVT_RiotPersonAdded:
		Rec.RiotSize += Event.Value;
		break;
	case EVT_MedevacVictimAdded:
		Rec.TypeMask |= TYPE_Medevac;
		Rec.MedevacVictims += Event.Value;
		break;
	case EVT_TransportPassengerAdded:
		Rec.TypeMask |= TYPE_Transport;
		Rec.TransportPassengers += Event.Value;
		break;
	case EVT_RescueVictimAdded:
		Rec.RescueVictims += Event.Value;
		break;
	case EVT_Unknown0F:
		Rec.Counter90 += Event.Value;
		break;
	case EVT_RescueDelivered:
		Rec.RescueDelivered += Event.Value;
		break;
	case EVT_TransportDelivered:
		Rec.TransportDelivered += Event.Value;
		break;
	case EVT_MedevacDelivered:
		Rec.MedevacDelivered += Event.Value;
		break;
	case EVT_VictimPickedUp:
		Rec.VictimsPickedUp += Event.Value;
		break;
	case EVT_RioterDispersed:
		Rec.RiotersDispersed += Event.Value;
		break;
	case EVT_RioterCalmed:
		Rec.RiotersCalmed += Event.Value;
		break;
	case EVT_Unknown16:
		Rec.CounterB0 += Event.Value;
		break;
	case EVT_PersonDied:
		Rec.Casualties += Event.Value;
		break;
	case EVT_CarCrashed:
		Rec.CarsCrashed += Event.Value;
		break;
	case EVT_JamCarAdded:
		Rec.JamCarCount += Event.Value;
		if (Rec.Category == CAT_Background && Rec.JamCarCount >= 3)
		{
			Rec.Category = CAT_Active;
			Rec.TimeAccum = 0;
			BackgroundCount = FMath::Max(0, BackgroundCount - 1);
			ActiveCount++;
			PostTypedUiMessage(5, &Rec, Rec.EventId, GetTypeTextId(Rec.TypeMask), 0, 0, false);
			PostAnnouncementVoice(Rec);
		}
		break;
	case EVT_CarDoused:
		Rec.CarsDoused += Event.Value;
		break;
	case EVT_CarCleared:
		Rec.CarsCleared += Event.Value;
		break;
	case EVT_CarBurned:
		Rec.CarsBurned += Event.Value;
		break;
	case EVT_PassengerLost:
		Rec.PassengersLost += Event.Value;
		break;
	case EVT_SetCategory:
		if (Rec.Category == CAT_Background && Event.Value != CAT_Background)
		{
			BackgroundCount = FMath::Max(0, BackgroundCount - 1);
			ActiveCount++;
		}
		else if (Rec.Category != CAT_Background && Event.Value == CAT_Background)
		{
			ActiveCount = FMath::Max(0, ActiveCount - 1);
			BackgroundCount++;
		}
		Rec.Category = Event.Value;
		break;
	case EVT_SetTertiaryCoords:
		Rec.TertiaryX = Event.X;
		Rec.TertiaryY = Event.Y;
		break;
	case EVT_DebrisCleared:
		Rec.DebrisCleared += Event.Value;
		break;
	case EVT_CriminalCaught:
		Rec.CriminalsCaught += Event.Value;
		break;
	case EVT_SetEndPointsScaled:
		Rec.EndPointsScaled = Event.Value;
		break;
	case EVT_SetEndMoneyScaled:
		Rec.EndMoneyScaled = Event.Value;
		break;
	case EVT_AdjustTargetCount:
		Rec.TargetCount += Event.Value;
		break;
	default:
		break;
	}
}

void FSimCopterMissionSystem::PostEvent(int32 Code, int32 EventId, int32 Value, bool bSilent)
{
	FSimCopterMissionEvent Evt;
	Evt.Code = Code;
	Evt.EventId = EventId;
	Evt.Value = Value;
	Evt.bSilent = bSilent;
	PostEvent(Evt);
}

void FSimCopterMissionSystem::PayIncremental(const FSimCopterMissionEvent& Event, int32 RecordIndex)
{
	int32 EarnedPoints = 0;
	int32 EarnedCash = 0;
	int32 TextId = -1;
	bool bPostUi = false;

	if (Event.bSilent) return;
	if (RecordIndex != INDEX_NONE && Records[RecordIndex].Category == CAT_Background) return;
	if (RecordIndex != INDEX_NONE && Records[RecordIndex].bSuppressCompletionRewards) return;

	switch(Event.Code)
	{
	case EVT_FlameCreated:
		TextId = 0x3a2;
		EarnedPoints = -(Event.Value * Tuning.FlamePointsPenalty);
		bPostUi = true;
		break;
	case EVT_FlameDoused:
		TextId = 0x3a3;
		EarnedCash = Event.Value * Tuning.FlameDousedMoney;
		bPostUi = true;
		if (World) World->PlayUiSound(0x1e);
		break;
	case EVT_CellBurnedOut:
		TextId = 0x3a4;
		bPostUi = true;
		break;
	case EVT_ObjectCaughtFire:
		TextId = 0x3a5;
		EarnedCash = Event.Value * Tuning.BldgSavedMoney;
		bPostUi = true;
		if (World) World->PlayUiSound(0x1e);
		break;
	case EVT_DebrisDoused:
		TextId = 0x3a6;
		EarnedCash = Event.Value * Tuning.DebrisDousedMoney;
		bPostUi = true;
		if (World) World->PlayUiSound(0x1e);
		break;
	case EVT_RescueDelivered:
		TextId = 0x3a8;
		EarnedCash = Event.Value * Tuning.RescueIncMoneyPerPerson;
		bPostUi = true;
		if (World) World->PlayUiSound(0x1e);
		break;
	case EVT_TransportDelivered:
		TextId = 0x3a9;
		EarnedCash = Event.Value * Tuning.TransportIncMoneyPerPerson;
		bPostUi = true;
		if (World) World->PlayUiSound(0x1e);
		break;
	case EVT_MedevacDelivered:
		TextId = 0x3aa;
		EarnedCash = Event.Value * Tuning.MedevacIncMoneyPerPerson;
		bPostUi = true;
		if (World) World->PlayUiSound(0x1e);
		break;
	case EVT_VictimPickedUp:
		TextId = 0x3ab;
		EarnedCash = Event.Value * Tuning.PickupIncMoneyPerPerson;
		bPostUi = true;
		if (World) World->PlayUiSound(0x1e);
		break;
	case EVT_RioterDispersed:
		TextId = 0x3ac;
		EarnedPoints = Event.Value * 10;
		EarnedCash = Event.Value * 10;
		bPostUi = true;
		if (World) World->PlayUiSound(0x1e);
		break;
	case EVT_PersonDied:
		TextId = 0x3b1;
		if (RecordIndex == INDEX_NONE) EarnedPoints = -Tuning.PersonDiedPointsPenalty;
		bPostUi = true;
		break;
	case EVT_CarDoused:
		TextId = 0x3b6;
		EarnedCash = Event.Value * Tuning.CarDousedMoney;
		bPostUi = true;
		if (World) World->PlayUiSound(0x1e);
		break;
	case EVT_CarCleared:
		TextId = 0x3b7;
		EarnedCash = Event.Value * Tuning.CarFireMoney;
		bPostUi = true;
		if (World) World->PlayUiSound(0x1e);
		break;
	case EVT_CarBurned:
		TextId = 0x3b8;
		EarnedPoints = -(Event.Value * Tuning.CarFirePoints);
		bPostUi = true;
		break;
	case EVT_CrashPenaltyA: EarnedPoints = -100; EarnedCash = -300; bPostUi = true; break;
	case EVT_CrashPenaltyB: EarnedPoints = -100; EarnedCash = -200; bPostUi = true; break;
	case EVT_CrashPenaltyC: EarnedPoints = -100; EarnedCash = -100; bPostUi = true; break;
	case EVT_CrashPenaltyD: EarnedPoints = -50; EarnedCash = -50; bPostUi = true; break;
	case EVT_CrashPenaltyE: EarnedPoints = -100; EarnedCash = -150; bPostUi = true; break;
	case EVT_CrashPenaltyF: EarnedPoints = -100; EarnedCash = -75; bPostUi = true; break;
	case EVT_CrashPenaltyG: EarnedPoints = -200; EarnedCash = -200; bPostUi = true; break;
	default:
		break;
	}

	if (bPostUi)
	{
		if (EarnedPoints != 0) AddScore(EarnedPoints);
		if (EarnedCash != 0) AddCash(EarnedCash);

		const FSimCopterMissionRecord* RecPtr = (RecordIndex != INDEX_NONE) ? &Records[RecordIndex] : nullptr;
		if (EarnedPoints != 0) PostTypedUiMessage(8, RecPtr, Event.EventId, TextId, EarnedPoints, 0, EarnedPoints < 0);
		if (EarnedCash != 0) PostTypedUiMessage(9, RecPtr, Event.EventId, TextId, EarnedCash, 0, EarnedCash < 0);
	}
}

void FSimCopterMissionSystem::PromoteRecordType(int32 EventId, int32 TypeBits)
{
	const int32 Index = FindRecordIndex(EventId);
	if (Index != INDEX_NONE)
	{
		Records[Index].TypeMask |= TypeBits;
	}
}

void FSimCopterMissionSystem::CompleteMission(FSimCopterMissionRecord& Rec)
{
	int32 EarnedPoints = 0;
	int32 EarnedCash = 0;
	int32 VoiceId = -1;

	if ((Rec.TypeMask & TYPE_Debris) != 0)
	{
		int32 Diff = Rec.DebrisDoused - Rec.DebrisExpired;
		EarnedPoints += Tuning.DebrisFirePoints * Diff;
		EarnedCash += Tuning.DebrisFireMoney * Diff;
		VoiceId = 0x5f;
	}
	if ((Rec.TypeMask & TYPE_BuildingFire) != 0)
	{
		uint32 Catch = Rec.ObjectsCaughtFire & 1;
		int32 BldgLeft = 1 - Rec.StructuresIgnited;
		EarnedPoints += (Rec.EndPointsScaled * Catch) - (Tuning.FireEndPointsPenalty * Rec.CellsBurnedOut) + (Tuning.FireEndPointsPenalty * BldgLeft);
		EarnedCash += (Rec.EndMoneyScaled * Catch) - (Tuning.FireEndMoneyPenalty * Rec.CellsBurnedOut) + (Tuning.FireEndMoneyPenalty * BldgLeft);
		VoiceId = 0x5f;
	}
	if ((Rec.TypeMask & TYPE_Medevac) != 0)
	{
		int32 PickupScore = Rec.VictimsPickedUp >> 2;
		EarnedPoints += (Tuning.MedevacEndPointsPerPerson * Rec.MedevacDelivered) - (Tuning.MedevacEndPointsPerPerson * Rec.Casualties) + (Tuning.MedevacEndPointsPerPerson * PickupScore);
		EarnedCash += (Tuning.MedevacEndMoneyPerPerson * Rec.MedevacDelivered) - (Tuning.MedevacEndMoneyPerPerson * Rec.Casualties) + (Tuning.MedevacEndMoneyPerPerson * PickupScore);
		VoiceId = 0x67;
	}
	if ((Rec.TypeMask & TYPE_RescuePeople) != 0)
	{
		int32 Deliv = Rec.RescueDelivered;
		int32 Mult = ((Rec.TypeMask & TYPE_TrainCrash) != 0) ? 2 : 1;
		int32 Pick = ((Rec.TypeMask & TYPE_TrainCrash) != 0) ? Rec.VictimsPickedUp : (Rec.VictimsPickedUp >> 2);
		EarnedPoints += (Tuning.RescueEndPointsPerPerson * Deliv * Mult) + (Tuning.RescueEndPointsPerPerson * Pick) - (Tuning.RescueEndPointsPerPerson * Rec.Casualties);
		EarnedCash += (Tuning.RescueEndMoneyPerPerson * Deliv * Mult) + (Tuning.RescueEndMoneyPerPerson * Pick) - (Tuning.RescueEndMoneyPerPerson * Rec.Casualties);
		VoiceId = ((Rec.TypeMask & TYPE_WaterRescue) == 0) ? 0x68 : 0x67;
	}
	if ((Rec.TypeMask & TYPE_Transport) != 0)
	{
		int32 Pick = Rec.VictimsPickedUp >> 2;
		EarnedPoints += (Tuning.TransportEndPointsPerPerson * Rec.TransportDelivered) - (Tuning.TransportEndPointsPerPerson * Rec.PassengersLost) + (Tuning.TransportEndPointsPerPerson * Pick);
		EarnedCash += (Tuning.TransportEndMoneyPerPerson * Rec.TransportDelivered) - (Tuning.TransportEndMoneyPerPerson * Rec.PassengersLost) + (Tuning.TransportEndMoneyPerPerson * Pick);
		VoiceId = 0x61;
	}
	if ((Rec.TypeMask & TYPE_Riot) != 0)
	{
		if (Rec.TargetCount < 6)
		{
			int32 Nags = 6 - Rec.TargetCount;
			EarnedPoints += (Tuning.RiotEndPoints * Nags) / 6;
			EarnedCash += (Tuning.RiotEndMoney * Nags) / 6;
		}
		VoiceId = 0x66;
	}
	if ((Rec.TypeMask & TYPE_CarFire) != 0)
	{
		EarnedPoints += Tuning.CarFirePoints * Rec.CarsCleared - Tuning.CarFirePoints * Rec.CarsBurned;
		EarnedCash += Tuning.CarFireMoney * Rec.CarsCleared - Tuning.CarFireMoney * Rec.CarsBurned;
		VoiceId = 0x5f;
	}
	if ((Rec.TypeMask & TYPE_TrafficJam) != 0)
	{
		int32 Pts = Tuning.JamEndPoints;
		int32 Mny = Tuning.JamEndMoney;
		if (Rec.Category == CAT_Background)
		{
			Pts >>= 1;
			Mny >>= 1;
		}
		EarnedPoints += Pts;
		EarnedCash += Mny;
		VoiceId = 0x65;
	}
	if ((Rec.TypeMask & TYPE_CriminalCar) != 0 || (Rec.TypeMask & TYPE_CriminalA) != 0 || (Rec.TypeMask & TYPE_SpeederEvent) != 0 || (Rec.TypeMask & TYPE_CriminalC) != 0)
	{
		EarnedPoints += Tuning.CriminalEndPoints;
		EarnedCash += Tuning.CriminalEndMoney;
		VoiceId = 100;
	}
	if ((Rec.TypeMask & TYPE_Speeder) != 0)
	{
		EarnedPoints += Tuning.SpeederEndPoints;
		EarnedCash += Tuning.SpeederEndMoney;
		VoiceId = 99;
	}

	if (Rec.bSuppressCompletionRewards)
	{
		EarnedPoints = 0;
		EarnedCash = 0;
	}

	if (EarnedPoints < 1) VoiceId = 0x60;
	else if (World && VoiceId != -1) World->PlayRadioVoice(VoiceId, 0x96);

	if (EarnedCash < 0) EarnedCash = 0;
	if (EarnedPoints != 0) AddScore(EarnedPoints);
	if (EarnedCash != 0) AddCash(EarnedCash);

	PostTypedUiMessage(6, &Rec, Rec.EventId, GetTypeTextId(Rec.TypeMask), EarnedPoints, EarnedCash, EarnedPoints < 0);
}

int32 FSimCopterMissionSystem::FindRecordIndex(int32 EventId) const
{
	if (EventId == -1) return INDEX_NONE;
	for (int32 i = 0; i < Records.Num(); ++i)
	{
		if (Records[i].bActive && Records[i].EventId == EventId) return i;
	}
	return INDEX_NONE;
}

void FSimCopterMissionSystem::AddScore(int32 Delta)
{
	Score = FMath::Max(0, Score + Delta);
}

void FSimCopterMissionSystem::AddCash(int32 Delta)
{
	Cash = FMath::Max(0, Cash + Delta);
}

void FSimCopterMissionSystem::PostTypedUiMessage(int32 Kind, const FSimCopterMissionRecord* Record, int32 EventId, int32 TextId, int32 ValueA, int32 ValueB, bool bNegative)
{
	if (World)
	{
		FSimCopterMissionUiMessage Msg;
		Msg.Kind = Kind;
		Msg.EventId = EventId;
		Msg.TextId = TextId;
		Msg.ValueA = ValueA;
		Msg.ValueB = ValueB;
		Msg.TypeMask = Record != nullptr ? Record->TypeMask : 0;
		Msg.MissionName = Record != nullptr ? Record->Name : FString();
		Msg.bNegative = bNegative;
		World->OnUiMessage(Msg);
	}
}

const FSimCopterMissionRecord* FSimCopterMissionSystem::FindRecord(int32 EventId) const
{
	int32 Idx = FindRecordIndex(EventId);
	return (Idx != INDEX_NONE) ? &Records[Idx] : nullptr;
}

bool FSimCopterMissionSystem::ClearTrafficJam(int32 EventId)
{
	const int32 Idx = FindRecordIndex(EventId);
	if (Idx == INDEX_NONE)
	{
		return false;
	}

	FSimCopterMissionRecord& Rec = Records[Idx];
	if (!Rec.bActive || (Rec.TypeMask & TYPE_TrafficJam) == 0)
	{
		return false;
	}

	if (World)
	{
		World->EndTrafficJam(EventId); // the jammed cars resume driving
	}
	CompleteMission(Rec);              // award Jam End money/points + announce + radio voice
	DeactivateRecord(Idx);
	return true;
}

void FSimCopterMissionSystem::DeactivateRecord(int32 RecordIndex)
{
	if (RecordIndex >= 0 && RecordIndex < Records.Num())
	{
		const int32 Category = Records[RecordIndex].Category;
		Records[RecordIndex].bActive = false;
		Records[RecordIndex].TypeMask = 0;
		if (Category == CAT_Background)
		{
			BackgroundCount = FMath::Max(0, BackgroundCount - 1);
		}
		else
		{
			ActiveCount = FMath::Max(0, ActiveCount - 1);
		}
	}
}

} // namespace SimCopterMissions
