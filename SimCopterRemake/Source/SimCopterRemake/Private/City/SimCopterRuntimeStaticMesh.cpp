// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterRuntimeStaticMesh.h"

#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"

namespace SimCopterRuntimeStaticMesh
{

UStaticMesh* Build(
	UObject* Outer,
	const TArray<FSimCopterRuntimeMeshSection>& Sections,
	bool bWithComplexCollision)
{
	int32 TotalVertices = 0;
	int32 TotalTriangles = 0;
	for (const FSimCopterRuntimeMeshSection& Section : Sections)
	{
		TotalVertices += Section.Vertices.Num();
		TotalTriangles += Section.Triangles.Num() / 3;
	}
	if (TotalVertices == 0 || TotalTriangles == 0)
	{
		return nullptr;
	}

	FMeshDescription MeshDescription;
	FStaticMeshAttributes Attributes(MeshDescription);
	Attributes.Register();

	TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> InstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> InstanceTangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> InstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector4f> InstanceColors = Attributes.GetVertexInstanceColors();
	TVertexInstanceAttributesRef<FVector2f> InstanceUVs = Attributes.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

	// Channel 0 is the face UV, channel 1 the atlas cell the city materials read.
	InstanceUVs.SetNumChannels(2);

	MeshDescription.ReserveNewVertices(TotalVertices);
	MeshDescription.ReserveNewVertexInstances(TotalVertices);
	MeshDescription.ReserveNewTriangles(TotalTriangles);
	MeshDescription.ReserveNewPolygonGroups(Sections.Num());

	TArray<FStaticMaterial> StaticMaterials;
	StaticMaterials.Reserve(Sections.Num());

	TArray<FVertexInstanceID> SectionInstances;
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		const FSimCopterRuntimeMeshSection& Section = Sections[SectionIndex];
		if (Section.Vertices.Num() == 0 || Section.Triangles.Num() < 3)
		{
			continue;
		}

		const FName SlotName(*FString::Printf(TEXT("Slot_%d"), StaticMaterials.Num()));
		const FPolygonGroupID PolygonGroup = MeshDescription.CreatePolygonGroup();
		SlotNames[PolygonGroup] = SlotName;
		StaticMaterials.Add(FStaticMaterial(Section.Material, SlotName, SlotName));

		// One vertex instance per source vertex: the city builder already emits unshared
		// vertices per face, so each one carries exactly one normal and needs no splitting.
		SectionInstances.Reset(Section.Vertices.Num());
		for (int32 VertexIndex = 0; VertexIndex < Section.Vertices.Num(); ++VertexIndex)
		{
			const FVertexID VertexID = MeshDescription.CreateVertex();
			VertexPositions[VertexID] = FVector3f(Section.Vertices[VertexIndex]);

			const FVertexInstanceID InstanceID = MeshDescription.CreateVertexInstance(VertexID);
			InstanceNormals[InstanceID] = Section.Normals.IsValidIndex(VertexIndex)
				? FVector3f(Section.Normals[VertexIndex])
				: FVector3f::ZAxisVector;

			if (Section.Tangents.IsValidIndex(VertexIndex))
			{
				const FProcMeshTangent& Tangent = Section.Tangents[VertexIndex];
				InstanceTangents[InstanceID] = FVector3f(Tangent.TangentX);
				InstanceBinormalSigns[InstanceID] = Tangent.bFlipTangentY ? -1.0f : 1.0f;
			}
			else
			{
				InstanceTangents[InstanceID] = FVector3f::XAxisVector;
				InstanceBinormalSigns[InstanceID] = 1.0f;
			}

			InstanceColors[InstanceID] = Section.VertexColors.IsValidIndex(VertexIndex)
				? FVector4f(
					Section.VertexColors[VertexIndex].R,
					Section.VertexColors[VertexIndex].G,
					Section.VertexColors[VertexIndex].B,
					Section.VertexColors[VertexIndex].A)
				: FVector4f(1.0f, 1.0f, 1.0f, 1.0f);

			InstanceUVs.Set(
				InstanceID,
				0,
				Section.UV0.IsValidIndex(VertexIndex) ? FVector2f(Section.UV0[VertexIndex]) : FVector2f::ZeroVector);
			InstanceUVs.Set(
				InstanceID,
				1,
				Section.UV1.IsValidIndex(VertexIndex) ? FVector2f(Section.UV1[VertexIndex]) : FVector2f::ZeroVector);

			SectionInstances.Add(InstanceID);
		}

		for (int32 Index = 0; Index + 2 < Section.Triangles.Num(); Index += 3)
		{
			const int32 A = Section.Triangles[Index];
			const int32 B = Section.Triangles[Index + 1];
			const int32 C = Section.Triangles[Index + 2];
			if (!SectionInstances.IsValidIndex(A) || !SectionInstances.IsValidIndex(B) || !SectionInstances.IsValidIndex(C))
			{
				continue;
			}
			// A face the source emitted twice for backfaces produces two triangles over the
			// same corners; skip only genuinely degenerate ones.
			if (A == B || B == C || A == C)
			{
				continue;
			}
			MeshDescription.CreateTriangle(
				PolygonGroup,
				{ SectionInstances[A], SectionInstances[B], SectionInstances[C] });
		}
	}

	if (MeshDescription.Triangles().Num() == 0)
	{
		return nullptr;
	}

	UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, NAME_None, RF_Transient);
	Mesh->NeverStream = true;
	// Required for the runtime triangle-mesh cook below: without it the body setup cannot read
	// the index buffer back and complex collision silently comes out empty.
	Mesh->bAllowCPUAccess = bWithComplexCollision;
	Mesh->SetStaticMaterials(StaticMaterials);

	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	// bFastBuild is mandatory outside the editor and is what keeps the supplied normals,
	// tangents and colours intact instead of recomputing them.
	BuildParams.bFastBuild = true;
	BuildParams.bCommitMeshDescription = false;
	BuildParams.bMarkPackageDirty = false;
	BuildParams.bBuildSimpleCollision = false;
	BuildParams.bAllowCpuAccess = bWithComplexCollision;

	const TArray<const FMeshDescription*> MeshDescriptions{ &MeshDescription };
	if (!Mesh->BuildFromMeshDescriptions(MeshDescriptions, BuildParams))
	{
		return nullptr;
	}

	if (bWithComplexCollision)
	{
		if (UBodySetup* BodySetup = Mesh->GetBodySetup())
		{
			// Cooked once here and shared by every instance placed from this model, so adding
			// a building costs a transform and demolishing one costs nothing.
			BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
			BodySetup->bDoubleSidedGeometry = true;
			BodySetup->CreatePhysicsMeshes();
		}
	}

	return Mesh;
}

} // namespace SimCopterRuntimeStaticMesh
