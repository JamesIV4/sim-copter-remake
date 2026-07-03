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
			City.Difficulty = Section->GetInt(TEXT("Ctrl0"));
			City.Weights[0] = Section->GetInt(TEXT("Ctrl1"));
			City.Weights[1] = Section->GetInt(TEXT("Ctrl2"));
			City.Weights[2] = Section->GetInt(TEXT("Ctrl3"));
			City.Weights[3] = Section->GetInt(TEXT("Ctrl4"));
			City.Weights[4] = Section->GetInt(TEXT("Ctrl5"));
			City.Weights[5] = Section->GetInt(TEXT("Ctrl6"));
			City.Weights[6] = Section->GetInt(TEXT("Ctrl7"));
			City.DayOrNight = Section->GetInt(TEXT("Ctrl8"));
			City.PointsNeeded = Section->GetInt(TEXT("Ctrl9"));
			City.MoneyEarned = Section->GetInt(TEXT("Ctrl10"));
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
	
	int32 TotalActive = ActiveCount + BackgroundCount;

	if (TypeMask == TYPE_PlaneCrash)
	{
		return CreateEventAt(-1, -1, TypeMask);
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
					if (CreatedId != -1) return CreatedId;
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
					if (CreatedId != -1) return CreatedId;
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
				if (CreatedId != -1) return CreatedId;
			}
		}
	}
	else if (TypeMask == TYPE_TrainCrash)
	{
		return CreateEventAt(-1, -1, TypeMask);
	}
	else if (TypeMask == TYPE_TrainRescue)
	{
		for (int i = 0; i < 5; ++i)
		{
			int32 TX, TY;
			if (TryPickRandomTileNearCamera(TX, TY))
			{
				int32 CreatedId = CreateEventAt(TX, TY, TypeMask);
				if (CreatedId != -1) return CreatedId;
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
				if (CreatedId != -1) return CreatedId;
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
				if (CreatedId != -1) return CreatedId;
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
				if (CreatedId != -1) return CreatedId;
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
					if (CreatedId != -1) return CreatedId;
				}
			}
		}
	}
	else if (TypeMask == TYPE_Ufo)
	{
		return CreateEventAt(-1, -1, TypeMask);
	}

	if (TotalActive < ActiveCount + BackgroundCount)
	{
		bRerollRequested = 1;
		ConsecutivePlaceFailures = 0;
	}
	else
	{
		ConsecutivePlaceFailures++;
	}

	return -1;
}

uint8 FSimCopterMissionSystem::GetXbldPropertyFlags(int32 BlockId)
{
    // FUN_0049a4d0 translates BlockId to a pointer in the XBLD property array
    // Xbld property table is 256 bytes long. Bit 4 (0x04) is the mission-building flag.
    // For now we rely on the World interface or a mocked array. 
    // We will assume World handles it if we delegate, but we need to do it here.
    return 0; // TODO: properly expose property table from World
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
	}
	else
	{
		ActiveCount++;
		SpawnCountdown = Tuning.EasyInterval;
	}
	PostAnnouncementVoice(Record);
}

void FSimCopterMissionSystem::PostAnnouncementVoice(const FSimCopterMissionRecord& Record)
{
	// Handled by UI system
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
		Rec.Category = CAT_Background;
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
			}
		}
		if (!bSpawned)
		{
			ReleaseFailedRecord(RecIndex);
			return -1;
		}
		Rec.Name = FString::Printf(TEXT("Fire Rescue #%d"), TypeSerials[14]);
		TypeSerials[14]++;
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

		// Traffic jam expiry (90 seconds = 0x5a0000)
		if ((Rec.TypeMask & TYPE_TrafficJam) != 0)
		{
			if (Rec.TimeAccum > 0x5a0000)
			{
				if (World)
				{
					World->EndTrafficJam(Rec.EventId);
				}
				// CompleteMission will be handled by the core later when the cars are cleared.
				// Wait! The original game clears the jam flag, which lets the cars drive away.
				// Then the cars driving away clears the bounding box and the jam is considered resolved!
				Rec.TypeMask &= ~TYPE_TrafficJam; // Prevent re-expiring. Actually, original clears bit 0 (Active bit).
				// wait, original does puVar5[0x13] = puVar5[0x13] & 0xfffffffe; which is clearing the active bit!
				DeactivateRecord(i); 
			}
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
		if (EarnedPoints != 0) PostTypedUiMessage(8, RecPtr, Event.EventId, TextId, EarnedPoints, EarnedPoints < 0);
		if (EarnedCash != 0) PostTypedUiMessage(9, RecPtr, Event.EventId, TextId, EarnedCash, EarnedCash < 0);
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

	if (EarnedPoints < 1) VoiceId = 0x60;
	else if (World && VoiceId != -1) World->PlayRadioVoice(VoiceId, 0x96);

	if (EarnedCash < 0) EarnedCash = 0;
	if (EarnedPoints != 0) AddScore(EarnedPoints);
	if (EarnedCash != 0) AddCash(EarnedCash);

	PostTypedUiMessage(6, &Rec, Rec.EventId, EarnedPoints, EarnedCash, false);
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

void FSimCopterMissionSystem::PostTypedUiMessage(int32 Kind, const FSimCopterMissionRecord* Record, int32 EventId, int32 ValueA, int32 ValueB, bool bNegative)
{
	if (World)
	{
		FSimCopterMissionUiMessage Msg;
		Msg.Kind = Kind;
		Msg.EventId = EventId;
		Msg.TextId = ValueA; // The original used TextId mapping, simplified here for now
		Msg.ValueA = ValueA;
		Msg.ValueB = ValueB;
		Msg.bNegative = bNegative;
		World->OnUiMessage(Msg);
	}
}

const FSimCopterMissionRecord* FSimCopterMissionSystem::FindRecord(int32 EventId) const
{
	int32 Idx = FindRecordIndex(EventId);
	return (Idx != INDEX_NONE) ? &Records[Idx] : nullptr;
}

void FSimCopterMissionSystem::DeactivateRecord(int32 RecordIndex)
{
	if (RecordIndex >= 0 && RecordIndex < Records.Num())
	{
		Records[RecordIndex].bActive = false;
		Records[RecordIndex].TypeMask = 0;
		ActiveCount--;
		if (ActiveCount < 0) ActiveCount = 0;
	}
}

} // namespace SimCopterMissions
