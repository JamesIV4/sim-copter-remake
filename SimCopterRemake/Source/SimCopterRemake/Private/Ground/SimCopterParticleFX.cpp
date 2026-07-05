// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterParticleFX.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

USimCopterParticleFXComponent::USimCopterParticleFXComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Prefer the dedicated translucent card material; fall back to the lit vertex-colour material
	// used everywhere else so cards still render (opaque) if the FX material has not been baked.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FxMaterialFinder(
		TEXT("/Game/Materials/M_SimCopterParticleFX.M_SimCopterParticleFX"));
	if (FxMaterialFinder.Succeeded())
	{
		CardMaterial = FxMaterialFinder.Object;
	}
	else
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> FallbackFinder(
			TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor"));
		if (FallbackFinder.Succeeded())
		{
			CardMaterial = FallbackFinder.Object;
		}
	}
}

void USimCopterParticleFXComponent::OnRegister()
{
	Super::OnRegister();

	if (MeshComponent == nullptr)
	{
		MeshComponent = NewObject<UProceduralMeshComponent>(this, TEXT("ParticleCards"));
		MeshComponent->SetupAttachment(this);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetCanEverAffectNavigation(false);
		MeshComponent->bUseAsyncCooking = false;
		MeshComponent->SetCastShadow(false);
		MeshComponent->RegisterComponent();
		UMaterialInterface* Material = CardMaterialOverride != nullptr ? CardMaterialOverride.Get() : CardMaterial.Get();
		if (Material != nullptr)
		{
			MeshComponent->SetMaterial(0, Material);
		}
	}
}

void USimCopterParticleFXComponent::SpawnCard(const FVector& World, const FVector& VelocityCmPerSec,
	float RiseCmPerSec, float SizeCm, const FLinearColor& Color, float LifeSeconds)
{
	if (Particles.Num() >= MaxParticles)
	{
		return;
	}
	FCard Card;
	Card.Position = World;
	Card.Velocity = VelocityCmPerSec;
	Card.Rise = RiseCmPerSec;
	Card.Size = SizeCm;
	Card.Life = FMath::Max(LifeSeconds, 0.05f);
	Card.Color = Color;
	Particles.Add(Card);
}

void USimCopterParticleFXComponent::SpawnRing(const FVector& World, int32 Count, float SpeedCmPerSec,
	float RiseCmPerSec, float SizeCm, const FLinearColor& Color, float LifeSeconds)
{
	Count = FMath::Clamp(Count, 1, 32);
	for (int32 i = 0; i < Count; ++i)
	{
		const float Angle = (2.0f * PI * static_cast<float>(i)) / static_cast<float>(Count);
		const FVector Velocity(FMath::Cos(Angle) * SpeedCmPerSec, FMath::Sin(Angle) * SpeedCmPerSec, 0.0f);
		SpawnCard(World, Velocity, RiseCmPerSec, SizeCm, Color, LifeSeconds);
	}
}

FVector USimCopterParticleFXComponent::GetCameraLocation() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (const APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
			{
				return CameraManager->GetCameraLocation();
			}
		}
	}
	return GetComponentLocation();
}

void USimCopterParticleFXComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Particles.Num() == 0)
	{
		if (MeshComponent != nullptr && MeshComponent->GetNumSections() > 0)
		{
			MeshComponent->ClearAllMeshSections();
		}
		return;
	}

	for (int32 i = Particles.Num() - 1; i >= 0; --i)
	{
		FCard& Card = Particles[i];
		Card.Age += DeltaTime;
		if (Card.Age >= Card.Life)
		{
			Particles.RemoveAtSwap(i);
			continue;
		}
		Card.Position += Card.Velocity * DeltaTime;
		Card.Position.Z += Card.Rise * DeltaTime;
	}

	RebuildMesh(GetCameraLocation());
}

void USimCopterParticleFXComponent::RebuildMesh(const FVector& CameraLocation)
{
	if (MeshComponent == nullptr)
	{
		return;
	}
	if (Particles.Num() == 0)
	{
		MeshComponent->ClearAllMeshSections();
		return;
	}

	// One camera-facing quad per card, expressed in the component's local space.
	const FTransform WorldToLocal = GetComponentTransform().Inverse();

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	const int32 Num = Particles.Num();
	Vertices.Reserve(Num * 4);
	Triangles.Reserve(Num * 6);
	Normals.Reserve(Num * 4);
	UVs.Reserve(Num * 4);
	Colors.Reserve(Num * 4);
	Tangents.Reserve(Num * 4);

	for (const FCard& Card : Particles)
	{
		// Billboard basis: face the camera, keep the card roughly upright.
		FVector Forward = (CameraLocation - Card.Position);
		if (!Forward.Normalize())
		{
			Forward = FVector::UpVector;
		}
		FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		if (!Right.Normalize())
		{
			Right = FVector::RightVector;
		}
		const FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();

		const float LifeAlpha = 1.0f - FMath::Clamp(Card.Age / Card.Life, 0.0f, 1.0f);
		FLinearColor Color = Card.Color;
		Color.A *= LifeAlpha; // fade out over life (translucent material reads .A)

		const float Half = Card.Size;
		const FVector Center = Card.Position;
		const FVector V0 = WorldToLocal.TransformPosition(Center - Right * Half - Up * Half);
		const FVector V1 = WorldToLocal.TransformPosition(Center + Right * Half - Up * Half);
		const FVector V2 = WorldToLocal.TransformPosition(Center + Right * Half + Up * Half);
		const FVector V3 = WorldToLocal.TransformPosition(Center - Right * Half + Up * Half);

		const int32 Base = Vertices.Num();
		Vertices.Add(V0);
		Vertices.Add(V1);
		Vertices.Add(V2);
		Vertices.Add(V3);

		const FVector LocalNormal = WorldToLocal.TransformVectorNoScale(Forward);
		for (int32 n = 0; n < 4; ++n)
		{
			Normals.Add(LocalNormal);
			Colors.Add(Color);
			Tangents.Add(FProcMeshTangent(WorldToLocal.TransformVectorNoScale(Right), false));
		}
		UVs.Add(FVector2D(0.0f, 1.0f));
		UVs.Add(FVector2D(1.0f, 1.0f));
		UVs.Add(FVector2D(1.0f, 0.0f));
		UVs.Add(FVector2D(0.0f, 0.0f));

		Triangles.Add(Base + 0);
		Triangles.Add(Base + 1);
		Triangles.Add(Base + 2);
		Triangles.Add(Base + 0);
		Triangles.Add(Base + 2);
		Triangles.Add(Base + 3);
	}

	MeshComponent->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	UMaterialInterface* Material = CardMaterialOverride != nullptr ? CardMaterialOverride.Get() : CardMaterial.Get();
	if (Material != nullptr)
	{
		MeshComponent->SetMaterial(0, Material);
	}
}
