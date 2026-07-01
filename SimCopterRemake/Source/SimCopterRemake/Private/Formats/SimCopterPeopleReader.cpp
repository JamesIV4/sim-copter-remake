// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/SimCopterPeopleReader.h"

#include "Formats/SimCopterDougContainerInternal.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace SimCopterDoug;

const TArray<int32>& FPeopleBehaviorModel::GetStateProgramIds()
{
	// DAT_0058de80, built by FUN_004c3010: person state -> BHAV program id. States 0..0x14;
	// unlisted states use 600.
	static const TArray<int32> Table = {
		600,  // 0  ambient default
		700,  // 1
		700,  // 2
		850,  // 3
		750,  // 4
		801,  // 5
		800,  // 6
		1400, // 7
		1401, // 8
		805,  // 9
		1300, // 10 criminal set
		1301, // 11
		1302, // 12
		1303, // 13
		1402, // 14
		810,  // 15
		666,  // 16
		443,  // 17
		444,  // 18
		700,  // 19
		600,  // 20
	};
	return Table;
}

int16 FPeopleBehaviorModel::GetStateLoopFlag(int32 StateIndex)
{
	// FUN_004c7090's loop-flag switch (person+0x14a).
	switch (StateIndex)
	{
	case 3: case 10: case 11: case 12: case 13:
		return 0;
	case 7: case 8:
		return 1;
	default:
		return -2;
	}
}

FString FSimCopterPeopleReader::ResolvePeoplePath(const FString& OriginalGameRoot)
{
	const TCHAR* Candidates[] = {TEXT("X/people.df"), TEXT("X/People.df"), TEXT("X/PEOPLE.DF")};
	for (const TCHAR* Candidate : Candidates)
	{
		const FString Path = FPaths::Combine(OriginalGameRoot, Candidate);
		if (FPaths::FileExists(Path))
		{
			return Path;
		}
	}
	return FString();
}

bool FSimCopterPeopleReader::LoadFromFile(const FString& FilePath, FPeopleBehaviorModel& OutModel, FString& OutError)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		OutError = FString::Printf(TEXT("Failed to read '%s'."), *FilePath);
		return false;
	}
	return LoadFromBytes(FileData, OutModel, OutError);
}

bool FSimCopterPeopleReader::LoadFromBytes(const TArray<uint8>& FileData, FPeopleBehaviorModel& OutModel, FString& OutError)
{
	OutModel = FPeopleBehaviorModel();

	FDougDirectory Dir;
	if (!ParseDirectory(FileData, Dir, OutError))
	{
		return false;
	}

	const TArray<FDirEntry>* Entries = Dir.Sections.Find(TEXT("BHAV"));
	if (Entries == nullptr || Entries->Num() == 0)
	{
		OutError = TEXT("No BHAV section found (is this people.df?).");
		return false;
	}

	for (const FDirEntry& Entry : *Entries)
	{
		int64 Payload = 0;
		uint32 Length = 0;
		if (!ResolveChunk(FileData, Dir, Entry, Payload, Length, OutError))
		{
			return false;
		}
		if (Length < 2)
		{
			continue;
		}
		const int32 RecordCount = ReadU16BE(FileData, Payload);
		if (2 + RecordCount * 12 > int32(Length))
		{
			OutError = FString::Printf(TEXT("BHAV %s declares %d records but chunk is %u bytes."),
				*Entry.Name, RecordCount, Length);
			return false;
		}

		FBhavProgram Program;
		Program.Id = Entry.Id;
		Program.Name = Entry.Name;
		Program.Records.Reserve(RecordCount);
		for (int32 RecordIndex = 0; RecordIndex < RecordCount; ++RecordIndex)
		{
			const int64 At = Payload + 2 + int64(RecordIndex) * 12;
			FBhavRecord Record;
			Record.Token = ReadU16BE(FileData, At);
			Record.TrueNext = int8(FileData[At + 2]);
			Record.FalseNext = int8(FileData[At + 3]);
			for (int32 ArgIndex = 0; ArgIndex < 4; ++ArgIndex)
			{
				Record.Args[ArgIndex] = ReadU16BE(FileData, At + 4 + ArgIndex * 2);
			}
			Program.Records.Add(Record);
		}
		OutModel.ProgramsById.Add(Program.Id, MoveTemp(Program));
	}

	if (OutModel.ProgramsById.Num() == 0)
	{
		OutError = TEXT("people.df contained no behavior programs.");
		return false;
	}
	return true;
}
