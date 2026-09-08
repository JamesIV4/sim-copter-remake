#if WITH_DEV_AUTOMATION_TESTS
#include "City/SimCopterPowerLinePlacement.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/SimCopterOriginalGamePaths.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterPowerLinePlacementTest,
	"SimCopter.City.PowerLineHeight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterPowerLinePlacementTest::RunTest(const FString& Parameters)
{
	FMaxisMeshFile File;
	FString Error;
	const FString Path = FPaths::Combine(SimCopterOriginalGame::ResolveDirectory(TEXT("geo")), TEXT("sim3d2.max"));
	if (!TestTrue(TEXT("Load original pole models"), FMaxisMeshReader::LoadMeshFileFromFile(Path, File, Error))) return false;
	for (int32 Id = 0x55; Id <= 0x5a; ++Id)
	{
		const FMaxisMeshObject* Original = File.FindObjectById(Id);
		if (!TestNotNull(TEXT("Original pole exists"), Original)) return false;
		FMaxisMeshObject Mesh = *Original;
		SimCopterPowerLinePlacement::NormalizeTerrainRelativePole(Mesh);
		int32 MaxY = 0;
		for (int32 I = 0; I < Mesh.Vertices.Num(); ++I)
		{
			const FMaxisMeshVertex& Vertex = Mesh.Vertices[I];
			MaxY = FMath::Max(MaxY, Vertex.Y);
			TestEqual(TEXT("Pole orientation X is preserved"), Vertex.X, Original->Vertices[I].X);
			TestEqual(TEXT("Pole orientation Z is preserved"), Vertex.Z, Original->Vertices[I].Z);
			if (Original->Vertices[I].Y == 0) TestEqual(TEXT("Shaft base stays on terrain"), Vertex.Y, 0);
		}
		TestEqual(TEXT("All six pole models reach the same terrain-relative height"), MaxY, 32 * 65536);
		for (const FMaxisMeshFace& Face : Mesh.Faces)
		{
			if (Face.FaceType != 20) continue;
			for (uint16 Index : Face.VertexIndices)
				TestTrue(TEXT("Wire endpoints stay at crossbar height"), FMath::Abs(Mesh.Vertices[Index].Y - 31 * 65536) <= 1);
		}
	}
	return true;
}
#endif
