#include "Ground/SimCopterFigureAdjustments.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FSimCopterFigurePieceAxes FSimCopterFigurePieceAxes::Stroke(const FVector& A, const FVector& B, float DepthRatio)
{
	FSimCopterFigurePieceAxes Axes;
	const FVector Delta = B - A;
	Axes.Z = Delta.Size() > KINDA_SMALL_NUMBER ? Delta.GetSafeNormal() : FVector::UpVector;
	FVector U = FVector::CrossProduct(Axes.Z, FVector::UpVector);
	if (!U.Normalize()) U = FVector::CrossProduct(Axes.Z, FVector::ForwardVector).GetSafeNormal();
	FVector V = FVector::CrossProduct(Axes.Z, U);
	U.X *= DepthRatio;
	V.X *= DepthRatio;
	Axes.X = V.GetSafeNormal();
	Axes.Y = U.GetSafeNormal();
	return Axes;
}

FVector FSimCopterFigurePieceAxes::ToLocal(const FVector& Point) const
{
	const double Determinant = FVector::DotProduct(X, FVector::CrossProduct(Y, Z));
	return FVector(FVector::DotProduct(Point, FVector::CrossProduct(Y, Z)),
		FVector::DotProduct(Point, FVector::CrossProduct(Z, X)),
		FVector::DotProduct(Point, FVector::CrossProduct(X, Y))) / Determinant;
}

FVector FSimCopterFigurePieceAxes::ToFigure(const FVector& Point) const
{
	return X * Point.X + Y * Point.Y + Z * Point.Z;
}

FVector FSimCopterFigurePieceAxes::ScaleNormal(const FVector& Normal, const FVector& Scale) const
{
	// Inverse transpose, also correct for the already-oblique inferred cross-section.
	const double Determinant = FVector::DotProduct(X, FVector::CrossProduct(Y, Z));
	return ((FVector::CrossProduct(Y, Z) * (FVector::DotProduct(X, Normal) / Scale.X)
		+ FVector::CrossProduct(Z, X) * (FVector::DotProduct(Y, Normal) / Scale.Y)
		+ FVector::CrossProduct(X, Y) * (FVector::DotProduct(Z, Normal) / Scale.Z)) / Determinant).GetSafeNormal();
}

FVector FSimCopterFigurePartAdjustment::Apply(const FVector& Vertex, const FVector& Center, float CmPerUnit,
	const FSimCopterFigurePieceAxes& Axes) const
{
	return Center + Axes.ToFigure(Axes.ToLocal(Vertex - Center) * Scale) + Offset * CmPerUnit;
}

bool FSimCopterFigureAdjustments::Parse(const FString& Json, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* Models = nullptr;
	double Version = 0;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()
		|| !Root->TryGetNumberField(TEXT("version"), Version) || Version != 1
		|| !Root->TryGetObjectField(TEXT("figures"), Models))
	{
		OutError = TEXT("Expected figure adjustments version 1 and a figures object.");
		return false;
	}
	FSimCopterFigureAdjustments Parsed;
	for (const auto& Model : (*Models)->Values)
	{
		if (Model.Value->Type != EJson::Object) { OutError = TEXT("Invalid figure object."); return false; }
		for (const auto& Part : Model.Value->AsObject()->Values)
		{
			if (Part.Value->Type != EJson::Object) { OutError = TEXT("Invalid part object."); return false; }
			FSimCopterFigurePartAdjustment Adjustment;
			if (Part.Value->AsObject()->HasField(TEXT("visible"))
				&& (Part.Value->AsObject()->TryGetField(TEXT("visible"))->Type != EJson::Boolean
					|| !Part.Value->AsObject()->TryGetBoolField(TEXT("visible"), Adjustment.bVisible)))
			{
				OutError = TEXT("Part visibility must be true or false.");
				return false;
			}
			auto ReadVector = [&](const TCHAR* Key, FVector& Out, bool bScale)
			{
				const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
				if (!Part.Value->AsObject()->TryGetArrayField(Key, Values) || Values->Num() != 3) return false;
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					double Number = 0;
					if (!(*Values)[Axis]->TryGetNumber(Number) || !FMath::IsFinite(Number)
						|| (bScale ? Number < 0.01 || Number > 100 : FMath::Abs(Number) > 10000)) return false;
					Out[Axis] = Number;
				}
				return true;
			};
			if (!ReadVector(TEXT("offset"), Adjustment.Offset, false) || !ReadVector(TEXT("scale"), Adjustment.Scale, true))
			{
				OutError = FString::Printf(TEXT("Invalid offset/scale for %s/%s."), *Model.Key, *Part.Key);
				return false;
			}
			Parsed.Figures.FindOrAdd(FString(Model.Key)).Add(FString(Part.Key), Adjustment);
		}
	}
	Figures = MoveTemp(Parsed.Figures);
	OutError.Reset();
	return true;
}

const FSimCopterFigureAdjustments& FSimCopterFigureAdjustments::Get()
{
	// Loaded once: all cached animation meshes in a session must use the same art revision.
	static const FSimCopterFigureAdjustments Adjustments = []
	{
		FSimCopterFigureAdjustments Result;
		FString Json, Error;
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Config/FigureAdjustments.json"));
		if (FFileHelper::LoadFileToString(Json, *Path) && !Result.Parse(Json, Error))
		{
			UE_LOG(LogTemp, Warning, TEXT("Figure adjustments ignored: %s"), *Error);
		}
		return Result;
	}();
	return Adjustments;
}
