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
	else if (TypeMask == TYPE_Medevac || TypeMask == TYPE_RescuePeople)
	{
		for (int i = 0; i < 5; ++i)
		{
			int32 TX, TY;
			if (TryPickRandomTileNearCamera(TX, TY))
			{
				uint8 XbldId = World ? World->GetXbldTileId(TX, TY) : 0;
				if (XbldId > 0x6f && XbldId < 0xdc && XbldId != 0xd1 && XbldId != 0xd2 && XbldId != 0xd3)
				{
					int32 CreatedId = CreateEventAt(TX, TY, TypeMask);
					if (CreatedId != -1) return ReturnCreation(CreatedId);
				}
			}
		}
	}
	else if (TypeMask == TYPE_BoatRescue || TypeMask == TYPE_Transport)
	{
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
	else if (TypeMask == TYPE_TrainRescue)
	{
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
	else if (TypeMask == TYPE_CriminalA || TypeMask == TYPE_CarFireEvent)
	{
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
	else if (TypeMask == TYPE_TrafficJam || TypeMask == TYPE_Riot)
	{
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
	else if (TypeMask == TYPE_SpeederEvent || TypeMask == TYPE_CriminalCar)
	{
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
	else if (TypeMask == TYPE_CriminalC || TypeMask == TYPE_FireRescue)
	{
		for (int i = 0; i < 5; ++i)
		{
			int32 TX, TY;
			if (TryPickRandomTileNearCamera(TX, TY))
			{
				uint8 XbldId = World ? World->GetXbldTileId(TX, TY) : 0;
				uint8 Props = GetXbldPropertyFlags(XbldId);
				if ((Props & 4) != 0 && XbldId != 0xd1 && XbldId != 0xd2 && XbldId != 0xd3)
				{
					int32 CreatedId = CreateEventAt(TX, TY, TypeMask);
					if (CreatedId != -1) return ReturnCreation(CreatedId);
				}
			}
		}
	}
	else if (TypeMask == TYPE_Ufo)
	{
		return ReturnCreation(CreateEventAt(-1, -1, TypeMask));
	}

	NoteCreationResult(false);
	return -1;
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
	for (int32 i = 0; i < MaxRecords; ++i)
	{
		if (!Records[i].bActive)
		{
			Records[i] = FSimCopterMissionRecord();
			return i;
		}
	}
	return INDEX_NONE;
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
	if ((TypeMask & TYPE_Riot) != 0) return TEXT("Riot");
	if ((TypeMask & TYPE_FireRescue) != 0) return TEXT("Fire Rescue");
	if ((TypeMask & TYPE_BoatRescue) != 0) return TEXT("Boat Rescue");
	if ((TypeMask & TYPE_TrainRescue) != 0) return TEXT("Train Rescue");
	if ((TypeMask & TYPE_TrainCrash) != 0) return TEXT("Train Crash");
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

		bool bSpawned = false;
		for (int32 i = 0; i < 10; ++i)
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
		int32 OutX, OutY;
		if (!World || !World->TryActivateBoatRescue(Rec.EventId, Tuning.BaseMissionTimer, OutX, OutY))
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.TileX = OutX;
		Rec.TileY = OutY;
		FindDefaultDestinationTile(Rec.TileX, Rec.TileY, Rec.SecondaryX, Rec.SecondaryY);
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
		int32 OutX, OutY;
		if (!World || !World->TryActivateTrainRescue(Rec.EventId, Tuning.BaseMissionTimer, OutX, OutY))
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.TileX = OutX;
		Rec.TileY = OutY;
		FindDefaultDestinationTile(Rec.TileX, Rec.TileY, Rec.SecondaryX, Rec.SecondaryY);
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
	Rec.bSuppressCompletionRewards = true;
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

		if (Rec.Category == CAT_Background || Rec.Category == CAT_ExpireSilently)
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

bool FSimCopterMissionSystem::IgniteBuilding(int32 FireObjectIndex, int32 TileX, int32 TileY, int32 EventId, int32 Flags)
{
	int32 NumFlames = 3; 
	bool bSpawned = false;
	for (int32 i = 0; i < NumFlames; ++i)
	{
		bSpawned |= SpawnFlame(FireObjectIndex, TileX, TileY, 0, 0, Flags, EventId, 0);
	}
	return bSpawned;
}

bool FSimCopterMissionSystem::SpawnFlame(int32 FireObjectIndex, int32 TileX, int32 TileY, int32 OffsetX, int32 OffsetZ, int32 AxisFlag, int32 EventId, int32 Flags)
{
	for (int32 i = 0; i < MaxFlames; ++i)
	{
		if (!Flames[i].bActive)
		{
			Flames[i].bActive = true;
			Flames[i].TileX = TileX;
			Flames[i].TileY = TileY;
			Flames[i].EventId = EventId;
			Flames[i].FireObjectIndex = FireObjectIndex;
			Flames[i].BurnCountdown = 0x200000;
			
			PostEvent(EVT_FlameCreated, EventId, 1, false);
			ActiveFlameCount++;
			
			if (FireObjectIndex != -1)
			{
				FireObjects[FireObjectIndex].FlameCount++;
			}
			return true;
		}
	}
	return false;
}

void FSimCopterMissionSystem::RemoveFlame(int32 FlameIndex, bool bDoused)
{
	if (FlameIndex < 0 || FlameIndex >= MaxFlames) return;
	FSimCopterFlame& Flame = Flames[FlameIndex];
	if (!Flame.bActive) return;
	
	Flame.bActive = false;
	ActiveFlameCount--;
	
	int32 EvtCode = bDoused ? EVT_FlameDoused : EVT_FlameExpired;
	PostEvent(EvtCode, Flame.EventId, 1, false);
	
	if (Flame.FireObjectIndex != -1)
	{
		FireObjects[Flame.FireObjectIndex].FlameCount--;
		if (FireObjects[Flame.FireObjectIndex].FlameCount <= 0)
		{
			FireObjects[Flame.FireObjectIndex].bActive = false;
			PostEvent(EVT_ObjectCaughtFire, Flame.EventId, 1, false);
		}
	}
}

void FSimCopterMissionSystem::UpdateFires()
{
	if (ActiveFlameCount <= 0) return;

	for (int32 i = 0; i < MaxFlames; ++i)
	{
		if (Flames[i].bActive)
		{
			Flames[i].BurnCountdown -= FrameDeltaEma;
			
			if (Flames[i].BurnCountdown <= 0)
			{
				int32 r = Rand.Rand();
				if ((r % 100) < 5) 
				{
					SpreadFireFrom(Flames[i]);
				}
				RemoveFlame(i, false);
			}
		}
	}
}

int32 FSimCopterMissionSystem::DouseAt(int32 WorldX1616, int32 WorldY1616, int32 /*WorldZ1616*/)
{
	// World units are 16.16 with a tile = 0x400000 (64.0 units), so tile = world >> 22.
	return DouseAtTile(WorldX1616 >> 22, WorldY1616 >> 22);
}

int32 FSimCopterMissionSystem::DouseAtTile(int32 TileX, int32 TileY)
{
	if (ActiveFlameCount <= 0)
	{
		return 0;
	}

	// Fire Radius (Fire Parms, ~43.9 world units) is under one tile, but the bucket aim is
	// coarse, so douse flames on the target tile and the immediately adjacent tiles.
	constexpr int32 RadiusTiles = 1;

	// Douse rate: ~64.0 (0x400000) flame-health per second, frame-rate independent via the
	// sim's own smoothed frame delta. A freshly-lit flame (BurnCountdown 0x200000 = 32.0) is
	// out after ~0.5s of sustained water. FrameDeltaEma can be 0 before the first tick.
	const int32 DouseChunk = FrameDeltaEma > 0
		? static_cast<int32>((static_cast<int64>(0x400000) * FrameDeltaEma) >> 16)
		: 0x40000;

	int32 InRange = 0;
	for (int32 i = 0; i < MaxFlames; ++i)
	{
		FSimCopterFlame& Flame = Flames[i];
		if (!Flame.bActive)
		{
			continue;
		}
		if (FMath::Abs(Flame.TileX - TileX) > RadiusTiles || FMath::Abs(Flame.TileY - TileY) > RadiusTiles)
		{
			continue;
		}

		++InRange;
		Flame.BurnCountdown -= DouseChunk;
		if (Flame.BurnCountdown <= 0)
		{
			RemoveFlame(i, /*bDoused*/ true);
		}
	}
	return InRange;
}

void FSimCopterMissionSystem::SpreadFireFrom(const FSimCopterFlame& Flame)
{
	uint32 r = Rand.Rand();
	int32 dx = ((r >> 15) & 3) - 1;
	int32 dy = (((r >> 15) ^ ((r >> 15) & 3)) & 3) - 1; 

	int32 NewX = Flame.TileX + dx;
	int32 NewY = Flame.TileY + dy;

	if (Flame.EventId != -1)
	{
		IgniteBuilding(-1, NewX, NewY, Flame.EventId, 0);
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
	for (int32 i = 0; i < MaxRecords; ++i)
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
