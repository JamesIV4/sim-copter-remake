#if WITH_DEV_AUTOMATION_TESTS

#include "Ground/SimCopterFigureAdjustments.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterFigureAdjustmentsTest,
	"SimCopter.Figures.Adjustments", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterFigureAdjustmentsTest::RunTest(const FString& Parameters)
{
	FSimCopterFigureAdjustments Art;
	FString Error;
	TestTrue(TEXT("Reads exact part names including spaces"), Art.Parse(
		TEXT(R"({"version":1,"figures":{"pilot":{"Ne0 ":{"offset":[1,-2,3],"scale":[2,0.5,1]}}}})"), Error));
	const auto* Parts = Art.Figures.Find(TEXT("pilot"));
	if (!TestNotNull(TEXT("Pilot overrides"), Parts)) return false;
	const auto* Part = Parts->Find(TEXT("Ne0 "));
	if (!TestNotNull(TEXT("Untrimmed part key"), Part)) return false;
	TestTrue(TEXT("Older files default to visible"), Part->bVisible);
	TestTrue(TEXT("Axis scaling about center and calibrated offset"),
		Part->Apply(FVector(12, 24, 36), FVector(10, 20, 30), 2).Equals(FVector(16, 18, 42)));
	TestTrue(TEXT("Offset scales with population height"),
		Part->Apply(FVector(24, 48, 72), FVector(20, 40, 60), 4).Equals(FVector(32, 36, 84)));
	TestTrue(TEXT("Identity preserves original geometry"),
		FSimCopterFigurePartAdjustment().Apply(FVector(1,2,3), FVector(4,5,6), 7).Equals(FVector(1,2,3)));
	const auto Axes = FSimCopterFigurePieceAxes::Stroke(FVector::ZeroVector, FVector(5,7,9), 0.6f);
	FSimCopterFigurePartAdjustment Local;
	Local.Scale = FVector(2,0.5,1.4);
	for (const FVector Edge : {Axes.X, Axes.Y, Axes.Z})
	{
		const FVector Changed = Local.Apply(Edge, FVector::ZeroVector, 1, Axes);
		TestTrue(TEXT("Rotated piece edges keep their direction"), FVector::CrossProduct(Edge, Changed).IsNearlyZero());
	}
	TestTrue(TEXT("Local coordinate round trip"), Axes.ToLocal(Axes.ToFigure(FVector(2,3,4))).Equals(FVector(2,3,4)));
	const FVector Normal = FVector::CrossProduct(Axes.Y, Axes.Z).GetSafeNormal();
	TestTrue(TEXT("Normal stays perpendicular to scaled edge"), FMath::IsNearlyZero(FVector::DotProduct(
		Axes.ScaleNormal(Normal, Local.Scale), Local.Apply(Axes.Y, FVector::ZeroVector, 1, Axes))));
	TestFalse(TEXT("Rejects collapsed or inverted geometry"), Art.Parse(
		TEXT(R"({"version":1,"figures":{"pilot":{"Ne0 ":{"offset":[0,0,0],"scale":[0,1,1]}}}})"), Error));
	TestTrue(TEXT("Failed parse preserves prior overrides"), Art.Figures.Contains(TEXT("pilot")));
	TestFalse(TEXT("Rejects future format"), Art.Parse(TEXT(R"({"version":2,"figures":{}})"), Error));
	TestTrue(TEXT("Empty file clears overrides"), Art.Parse(TEXT(R"({"version":1,"figures":{}})"), Error));
	TestEqual(TEXT("No overrides"), Art.Figures.Num(), 0);
	TestTrue(TEXT("Reads hidden part"), Art.Parse(TEXT(R"({"version":1,"figures":{"pilot":{"New ":{"offset":[0,0,0],"scale":[1,1,1],"visible":false}}}})"), Error));
	TestFalse(TEXT("Hidden flag retained"), Art.Figures[TEXT("pilot")][TEXT("New ")].bVisible);
	TestFalse(TEXT("Rejects nonboolean visibility"), Art.Parse(TEXT(R"({"version":1,"figures":{"pilot":{"New ":{"offset":[0,0,0],"scale":[1,1,1],"visible":0}}}})"), Error));
	return true;
}
#endif
