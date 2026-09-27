#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GroundTraceTools.generated.h"

UENUM(BlueprintType)
enum class EGroundTraceMode : uint8
{
	All,
	OnlyTags,
	IgnoreTags
};

UENUM(BlueprintType)
enum class EGroundTraceShape : uint8
{
	Sphere,
	Line
};

UENUM(BlueprintType)
enum class EGroundTraceFilterType : uint8
{
	ObjectType,
	TraceChannel
};

/** Per-query filtering settings. Transform, maximum distance, sphere radius, and debug drawing are call arguments. */
USTRUCT(BlueprintType)
struct FGroundTraceFilterSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground Trace")
	EGroundTraceMode TraceMode = EGroundTraceMode::All;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground Trace")
	TArray<FName> Tags = { FName(TEXT("CameraGround")) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground Trace")
	EGroundTraceShape TraceShape = EGroundTraceShape::Line;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground Trace")
	EGroundTraceFilterType FilterType = EGroundTraceFilterType::ObjectType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground Trace")
	TEnumAsByte<ECollisionChannel> ObjectType = ECC_WorldStatic;

	// This project's GameTraceChannel1 is registered as CarRigGround in DefaultEngine.ini.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_GameTraceChannel1;
};

namespace GroundTraceTools
{
	// Reuse one per caller for sequential game-thread calls. Never share across concurrent
	// or reentrant calls. Reset preserves capacity; no actors or tag decisions are cached.
	struct FTraceScratch
	{
		TArray<FHitResult> Hits;
	};
	/**
	 * Game-thread only. Trace from TF's location along its rotated local -Z (scale ignored).
	 * FilterSettings selects query type, Object Type / Trace Channel, shape, and Actor Tags.
	 * Uses query-enabled components and complex collision.
	 * MaxDistance limits center travel. Sphere returns Hit.Distance + Radius, which can
	 * exceed MaxDistance. This matches Line on perpendicular planes without initial overlap,
	 * but is not an exact surface distance on slopes. Initial sphere overlaps return Radius.
	 * Returns 0 on no match / invalid input; a line hit at the origin also returns 0.
	 * Debug drawing lasts one frame. Call every frame for continuous visualization.
	 */
	inline float GetDistance(
		const UWorld* World,
		const FTransform& TF,
		float MaxDistance,
		const FGroundTraceFilterSettings& FilterSettings,
		float Radius,
		bool bShowDebug,
		FTraceScratch* Scratch = nullptr)
	{
		if (!IsValid(World) || !FMath::IsFinite(MaxDistance) || MaxDistance <= 0.0f
			|| !TF.IsValid()
		|| (FilterSettings.TraceMode != EGroundTraceMode::All
				&& FilterSettings.TraceMode != EGroundTraceMode::OnlyTags
				&& FilterSettings.TraceMode != EGroundTraceMode::IgnoreTags)
			|| (FilterSettings.TraceShape != EGroundTraceShape::Line
				&& FilterSettings.TraceShape != EGroundTraceShape::Sphere)
			|| (FilterSettings.FilterType != EGroundTraceFilterType::ObjectType
				&& FilterSettings.FilterType != EGroundTraceFilterType::TraceChannel))
		{
			return 0.0f;
		}

		const bool bSphere = FilterSettings.TraceShape == EGroundTraceShape::Sphere;
		if (bSphere && (!FMath::IsFinite(Radius) || Radius < 0.0f))
		{
			return 0.0f;
		}

		const FVector Start = TF.GetLocation();
		const FVector End = Start - TF.GetUnitAxis(EAxis::Z) * MaxDistance;
		const FCollisionObjectQueryParams Objects(FilterSettings.ObjectType.GetValue());
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GroundTraceTools), false);
		Params.bTraceComplex = true;
		Params.bFindInitialOverlaps = true;
		const FCollisionShape Shape = FCollisionShape::MakeSphere(bSphere ? Radius : 0.0f);
		// Match the engine's zero-size sweep fallback, including its distance semantics.
		const bool bSweep = bSphere && !Shape.IsNearlyZero();
		FHitResult SelectedHit;
		bool bFound = false;
		const auto TraceSingle = [&]()
		{
			if (FilterSettings.FilterType == EGroundTraceFilterType::TraceChannel)
			{
				return bSweep
					? World->SweepSingleByChannel(SelectedHit, Start, End, FQuat::Identity,
						FilterSettings.TraceChannel.GetValue(), Shape, Params)
					: World->LineTraceSingleByChannel(SelectedHit, Start, End,
						FilterSettings.TraceChannel.GetValue(), Params);
			}
			return bSweep
				? World->SweepSingleByObjectType(SelectedHit, Start, End, FQuat::Identity, Objects, Shape, Params)
				: World->LineTraceSingleByObjectType(SelectedHit, Start, End, Objects, Params);
		};
		const auto Accepts = [&](const FHitResult& Hit)
		{
			const AActor* Actor = Hit.GetActor();
			const bool bMatchesTag = IsValid(Actor) && FilterSettings.Tags.ContainsByPredicate(
				[Actor](const FName& Tag) { return Actor->ActorHasTag(Tag); });
			return FilterSettings.TraceMode == EGroundTraceMode::OnlyTags ? bMatchesTag : !bMatchesTag;
		};

		if (FilterSettings.TraceMode == EGroundTraceMode::All
			|| (FilterSettings.TraceMode == EGroundTraceMode::IgnoreTags && FilterSettings.Tags.IsEmpty()))
		{
			bFound = TraceSingle();
		}
		else if (!FilterSettings.Tags.IsEmpty())
		{
			if (FilterSettings.FilterType == EGroundTraceFilterType::TraceChannel)
			{
				// Channel multi traces stop at the first blocking hit. Keep querying past
				// rejected actors so arbitrary Actor Tag filters still find deeper matches.
				while (TraceSingle())
				{
					if (Accepts(SelectedHit))
					{
						bFound = true;
						break;
					}
					if (const AActor* Actor = SelectedHit.GetActor(); IsValid(Actor))
					{
						Params.AddIgnoredActor(Actor);
					}
					else if (const UPrimitiveComponent* Component = SelectedHit.GetComponent(); IsValid(Component))
					{
						Params.AddIgnoredComponent(Component);
					}
					else
					{
						break;
					}
				}
			}
			else
			{
				// Object multi queries collect candidates beyond blockers in one query.
				TArray<FHitResult> LocalHits;
				TArray<FHitResult>& Hits = Scratch ? Scratch->Hits : LocalHits;
				Hits.Reset();
				if (bSweep)
				{
					World->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, Objects, Shape, Params);
				}
				else
				{
					World->LineTraceMultiByObjectType(Hits, Start, End, Objects, Params);
				}

				for (const FHitResult& Hit : Hits)
				{
					if ((!bFound || Hit.Distance < SelectedHit.Distance) && Accepts(Hit))
					{
						SelectedHit = Hit;
						bFound = true;
					}
				}
			}
		}

		if (bShowDebug)
		{
			const FVector TraceAxis = (End - Start).GetSafeNormal();
			const FVector BottomOffset = bSweep ? TraceAxis * Radius : FVector::ZeroVector;
			const FVector DebugStart = Start + BottomOffset;
			const FVector DebugEnd = End + BottomOffset;
			const FVector DebugHitLocation = bFound ? SelectedHit.Location + BottomOffset : DebugStart;
			DrawDebugLine(World, bFound ? DebugHitLocation : DebugStart, DebugEnd,
				FColor::Red, false, 0.0f, 0, 1.0f);
			if (bSweep)
			{
				DrawDebugCapsule(World, (Start + End) * 0.5, MaxDistance * 0.5f + Radius,
					Radius, FQuat::FindBetweenNormals(FVector::UpVector, TraceAxis), FColor::Red, false, 0.0f);
				DrawDebugSphere(World, Start, Radius, 16, FColor::Cyan, false, 0.0f);
			}
			if (bFound)
			{
				DrawDebugLine(World, DebugStart, DebugHitLocation, FColor::Green, false, 0.0f, 0, 2.0f);
				DrawDebugPoint(World, SelectedHit.ImpactPoint, 28.0f, FColor::Yellow, false, 0.0f);
				if (bSweep)
				{
					DrawDebugSphere(World, SelectedHit.Location, Radius, 16, FColor::Green, false, 0.0f);
				}
			}
		}

		return bFound ? SelectedHit.Distance + (bSweep ? Radius : 0.0f) : 0.0f;
	}
}
