// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Misc/AutomationTest.h"
#include "UI/SimCopterFlapLayout.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlapLayoutTest,
	"SimCopter.UI.FlapLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterFlapLayoutTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterFlapLayout;

	const TArrayView<const FFlap> Flaps = GetFlaps();
	TestEqual(TEXT("FUN_004127d0 builds four flaps"), Flaps.Num(), 4);
	if (Flaps.Num() != 4)
	{
		return false;
	}

	// The pages and the masks FUN_004127d0 tests, in the order it builds them.
	{
		const TCHAR* const ExpectedPages[] = {
			TEXT("FLAP0.BMP"), TEXT("FLAP1.BMP"), TEXT("FLAP2.BMP"), TEXT("FLAP3.BMP") };
		const int32 ExpectedMasks[] = { 0x11, 0x02, 0x04, 0x08 };

		int32 UnionMask = 0;
		for (int32 Index = 0; Index < Flaps.Num(); ++Index)
		{
			TestEqual(
				*FString::Printf(TEXT("Flap %d draws the right page"), Index),
				FString(Flaps[Index].PageFileName),
				FString(ExpectedPages[Index]));
			TestEqual(
				*FString::Printf(TEXT("Flap %d is gated on the right equipment bits"), Index),
				Flaps[Index].EquipmentMask,
				ExpectedMasks[Index]);
			UnionMask |= Flaps[Index].EquipmentMask;
		}

		// Every purchasable tool reaches the cockpit: nothing can be bought that has no flap.
		TestEqual(
			TEXT("The four flaps between them cover every career equipment bit"),
			UnionMask,
			SimCopterHelicopterRegistry::AllCareerEquipmentBits);
	}

	// Each control's tool has to be one the flap it sits on is gated by, or a bought tool could
	// leave a live button on a flap that is not shown.
	for (int32 Index = 0; Index < Flaps.Num(); ++Index)
	{
		for (const FButton& Button : Flaps[Index].Buttons)
		{
			const int32 Bit = SimCopterHelicopterRegistry::GetToolCareerBit(Button.Tool);
			TestTrue(
				*FString::Printf(
					TEXT("Flap %d's '%s' belongs to a tool the flap is gated by"),
					Index,
					GetActionName(Button.Action)),
				Bit != 0 && (Flaps[Index].EquipmentMask & Bit) != 0);
		}
	}

	// Every click box and every sprite has to land on the page; a rect off the edge would be a
	// button the player can never hit.
	for (const FFlap& Flap : Flaps)
	{
		for (const FButton& Button : Flap.Buttons)
		{
			const TCHAR* Name = GetActionName(Button.Action);
			TestTrue(
				*FString::Printf(TEXT("'%s' click box is on the page"), Name),
				Button.Hit.Min.X >= 0 && Button.Hit.Min.Y >= 0 &&
					Button.Hit.Max.X <= PageWidth && Button.Hit.Max.Y <= PageHeight &&
					Button.Hit.Width() > 0 && Button.Hit.Height() > 0);
			TestTrue(
				*FString::Printf(TEXT("'%s' pressed sprite is on the page"), Name),
				Button.ArtOrigin.X >= 0 && Button.ArtOrigin.Y >= 0 &&
					Button.ArtOrigin.X + Button.Art.PressedFrame.Width() <= PageWidth &&
					Button.ArtOrigin.Y + Button.Art.PressedFrame.Height() <= PageHeight);
		}
	}

	// The water flap's four rectangles, exactly as FUN_00454ee0 case 0 writes them.
	{
		const FFlap& Water = Flaps[0];
		TestEqual(TEXT("The water flap has four controls"), Water.Buttons.Num(), 4);
		if (Water.Buttons.Num() == 4)
		{
			TestEqual(TEXT("Bucket raise box"), Water.Buttons[0].Hit, FIntRect(102, 10, 119, 24));
			TestEqual(TEXT("Bucket lower box"), Water.Buttons[1].Hit, FIntRect(102, 24, 119, 39));
			TestEqual(TEXT("Bucket dump box"), Water.Buttons[2].Hit, FIntRect(77, 4, 98, 26));
			TestEqual(TEXT("Cannon box"), Water.Buttons[3].Hit, FIntRect(10, 14, 32, 36));
		}
	}

	// Both rockers: the two halves must meet with no gap and no overlap, and between them take
	// the whole 17x29 sprite - one arrowhead each, so only the half under the cursor lights.
	{
		struct FRocker { const TCHAR* Name; int32 FlapIndex; int32 RaiseIndex; int32 LowerIndex; };
		const FRocker Rockers[] = {
			{ TEXT("bucket"), 0, 0, 1 },
			{ TEXT("harness"), 2, 0, 1 },
		};

		for (const FRocker& Rocker : Rockers)
		{
			const FFlap& Flap = Flaps[Rocker.FlapIndex];
			if (!Flap.Buttons.IsValidIndex(Rocker.LowerIndex))
			{
				continue;
			}
			const FButton& Raise = Flap.Buttons[Rocker.RaiseIndex];
			const FButton& Lower = Flap.Buttons[Rocker.LowerIndex];

			// The two click boxes have to abut, or there would be a dead row between the
			// arrowheads. Their combined height is not the sprite's: FUN_00454ee0 gives the
			// bucket's rocker exactly the sprite's 29 rows and pads the harness's to 31.
			TestEqual(
				*FString::Printf(TEXT("The %s rocker's click boxes meet"), Rocker.Name),
				Raise.Hit.Max.Y,
				Lower.Hit.Min.Y);

			// The sprite the two halves lay down is the whole 29-row rocker.
			const int32 SpriteTop = Raise.ArtOrigin.Y;
			const int32 SpriteBottom = Lower.ArtOrigin.Y + Lower.Art.PressedFrame.Height();
			TestEqual(
				*FString::Printf(TEXT("The %s rocker's sprite is 29 rows tall"), Rocker.Name),
				SpriteBottom - SpriteTop,
				29);

			// ...and the boxes have to cover it, or part of the rocker would not be clickable.
			TestTrue(
				*FString::Printf(TEXT("The %s rocker's click boxes cover its sprite"), Rocker.Name),
				Raise.Hit.Min.Y <= SpriteTop && Lower.Hit.Max.Y >= SpriteBottom);

			// Sprite halves: the upper takes rows 0..13 of the pressed frame, the lower 14..28.
			TestEqual(
				*FString::Printf(TEXT("The %s rocker's upper sprite is the frame's top half"), Rocker.Name),
				Raise.Art.PressedFrame,
				FIntRect(17, 0, 34, 14));
			TestEqual(
				*FString::Printf(TEXT("The %s rocker's lower sprite is the frame's bottom half"), Rocker.Name),
				Lower.Art.PressedFrame,
				FIntRect(17, 14, 34, 29));

			// ...and the two sprites have to be laid down back to back, or the arrowheads would
			// not line up with the one printed on the page.
			TestEqual(
				*FString::Printf(TEXT("The %s rocker's sprite halves are contiguous"), Rocker.Name),
				Lower.ArtOrigin.Y,
				Raise.ArtOrigin.Y + Raise.Art.PressedFrame.Height());
			TestEqual(
				*FString::Printf(TEXT("The %s rocker's sprite halves share a column"), Rocker.Name),
				Lower.ArtOrigin.X,
				Raise.ArtOrigin.X);
		}
	}

	// The megaphone and the tear gas launcher share one button rectangle - the same octagon in
	// the same place on both pages.
	{
		const FFlap& Megaphone = Flaps[1];
		const FFlap& TearGas = Flaps[3];
		TestEqual(TEXT("The megaphone flap has one control"), Megaphone.Buttons.Num(), 1);
		TestEqual(TEXT("The tear gas flap has one control"), TearGas.Buttons.Num(), 1);
		if (Megaphone.Buttons.Num() == 1 && TearGas.Buttons.Num() == 1)
		{
			TestEqual(TEXT("Megaphone box"), Megaphone.Buttons[0].Hit, FIntRect(77, 9, 102, 40));
			TestEqual(TEXT("The tear gas box is the same rectangle"),
				TearGas.Buttons[0].Hit, Megaphone.Buttons[0].Hit);
			TestEqual(TEXT("...and the sprite sits in the same place"),
				TearGas.Buttons[0].ArtOrigin, Megaphone.Buttons[0].ArtOrigin);
		}
	}

	return true;
}
