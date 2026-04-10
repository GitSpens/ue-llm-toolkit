// Copyright Natali Caggiano. All Rights Reserved.

#include "SceneAnalyzer.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Engine/GameViewportClient.h"
#include "PhysicsEngine/BodySetup.h"
#include "Dom/JsonValue.h"

FSceneAnalysisResult FSceneAnalyzer::AnalyzeScene(
	UWorld* World,
	const FString& ClassFilter,
	int32 MaxActors)
{
	FSceneAnalysisResult Result;
	TSet<FString> UniqueMeshPaths;
	int32 ActorCount = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (ActorCount >= MaxActors) break;
		AActor* Actor = *It;
		if (!Actor || Actor->IsPendingKillPending()) continue;
		if (!ClassFilter.IsEmpty() && !Actor->GetClass()->GetName().Contains(ClassFilter)) continue;
		Result.TotalActors++;
		ActorCount++;

		FActorMeshStats Stats;
		if (GatherActorMeshStats(Actor, Stats))
		{
			Stats.EstimatedCost = CalculateEstimatedCost(Stats);
			Stats.Recommendations = GenerateActorRecommendations(Stats);

			Result.MeshActors++;
			Result.TotalTriangles += Stats.TriangleCount;
			Result.TotalEstimatedDrawCalls += Stats.EstimatedDrawCalls;
			Result.TotalMaterials += Stats.MaterialCount;

			if (!Stats.MeshPath.IsEmpty())
			{
				UniqueMeshPaths.Add(Stats.MeshPath);
			}

			if (Stats.LODCount <= 1)
			{
				Result.ActorsWithoutLODs++;
			}

			Result.AllActorStats.Add(MoveTemp(Stats));
		}
	}

	Result.UniqueMeshes = UniqueMeshPaths.Num();
	Result.OverallRecommendations = GenerateOverallRecommendations(Result);
	Result.SanityCheck = RunSanityChecks(Result);

	return Result;
}

bool FSceneAnalyzer::GatherActorMeshStats(AActor* Actor, FActorMeshStats& OutStats)
{
	OutStats.ActorLabel = Actor->GetActorLabel();
	OutStats.ActorClass = Actor->GetClass()->GetName();
	OutStats.Location = Actor->GetActorLocation();

	bool bHasMesh = false;

	TArray<UStaticMeshComponent*> StaticMeshComps;
	Actor->GetComponents<UStaticMeshComponent>(StaticMeshComps);
	for (UStaticMeshComponent* Comp : StaticMeshComps)
	{
		if (!Comp) continue;
		UStaticMesh* Mesh = Comp->GetStaticMesh();
		if (!Mesh) continue;
		bHasMesh = true;
		if (OutStats.MeshPath.IsEmpty()) OutStats.MeshPath = Mesh->GetPathName();

		OutStats.LODCount = FMath::Max(OutStats.LODCount, Mesh->GetNumLODs());
		OutStats.MaterialCount += Comp->GetNumMaterials();
		OutStats.bShadowCasting |= Comp->CastShadow;

		// Get triangle/vertex count from render data
		FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
		if (RenderData && RenderData->LODResources.Num() > 0)
		{
			const FStaticMeshLODResources& LOD0 = RenderData->LODResources[0];
			OutStats.TriangleCount += LOD0.GetNumTriangles();
			OutStats.VertexCount += LOD0.GetNumVertices();
			OutStats.MeshSectionCount += LOD0.Sections.Num();
		}

		// Check for instanced mesh components
		if (UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Comp))
		{
			OutStats.InstanceCount = FMath::Max(OutStats.InstanceCount, ISM->GetInstanceCount());
		}
	}

	// Gather from all skeletal mesh components
	TArray<USkeletalMeshComponent*> SkelMeshComps;
	Actor->GetComponents<USkeletalMeshComponent>(SkelMeshComps);

	for (USkeletalMeshComponent* Comp : SkelMeshComps)
	{
		if (!Comp)
		{
			continue;
		}

		USkeletalMesh* Mesh = Comp->GetSkeletalMeshAsset();
		if (!Mesh)
		{
			continue;
		}

		bHasMesh = true;

		if (OutStats.MeshPath.IsEmpty())
		{
			OutStats.MeshPath = Mesh->GetPathName();
		}

		OutStats.LODCount = FMath::Max(OutStats.LODCount, Mesh->GetLODNum());
		OutStats.MaterialCount += Comp->GetNumMaterials();
		OutStats.bShadowCasting |= Comp->CastShadow;

		// Get triangle/vertex count from render data
		FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
		if (RenderData && RenderData->LODRenderData.Num() > 0)
		{
			const FSkeletalMeshLODRenderData& LOD0 = RenderData->LODRenderData[0];
			OutStats.TriangleCount += LOD0.GetTotalFaces();
			OutStats.VertexCount += LOD0.GetNumVertices();
			OutStats.MeshSectionCount += LOD0.RenderSections.Num();
		}
	}

	if (!bHasMesh)
	{
		return false;
	}

	// Collision info
	OutStats.CollisionComplexity = GetCollisionComplexityString(Actor);
	OutStats.bHasCollision = !OutStats.CollisionComplexity.IsEmpty()
		&& OutStats.CollisionComplexity != TEXT("none");

	// Estimated draw calls = sections * instance count
	OutStats.EstimatedDrawCalls = OutStats.MeshSectionCount * FMath::Max(1, OutStats.InstanceCount);

	return true;
}

float FSceneAnalyzer::CalculateEstimatedCost(const FActorMeshStats& Stats)
{
	float Cost = (Stats.TriangleCount / 1000.0f)
		* FMath::Max(1, Stats.MaterialCount)
		* FMath::Max(1, Stats.MeshSectionCount);

	if (Stats.bShadowCasting)
	{
		Cost *= 1.5f;
	}

	if (Stats.LODCount <= 1 && Stats.TriangleCount > 5000)
	{
		Cost *= 2.0f;
	}

	return Cost;
}

TArray<FString> FSceneAnalyzer::GenerateActorRecommendations(const FActorMeshStats& Stats)
{
	TArray<FString> Recs;

	// No LODs on high-poly mesh
	if (Stats.LODCount <= 1 && Stats.TriangleCount > 5000)
	{
		int32 ExpectedLODs = Stats.TriangleCount > 50000 ? 4 : (Stats.TriangleCount > 20000 ? 3 : 2);
		Recs.Add(FString::Printf(
			TEXT("Add LODs — mesh has %dk triangles but %d LOD(s). Expected %d LODs for this poly count."),
			Stats.TriangleCount / 1000, Stats.LODCount, ExpectedLODs));
	}

	// High material count on high-poly mesh
	if (Stats.MaterialCount > 2 && Stats.TriangleCount > 10000)
	{
		Recs.Add(FString::Printf(
			TEXT("High material count (%d) on high-poly mesh — consider merging materials to reduce draw calls."),
			Stats.MaterialCount));
	}

	// Shadow casting on small/medium meshes with many instances
	if (Stats.bShadowCasting && Stats.InstanceCount > 50)
	{
		Recs.Add(FString::Printf(
			TEXT("Shadow casting enabled on instanced mesh with %d instances — consider disabling shadows or using distance-based shadow culling."),
			Stats.InstanceCount));
	}

	// Complex collision on high-poly mesh
	if (Stats.CollisionComplexity == TEXT("complex_as_simple") && Stats.TriangleCount > 10000)
	{
		Recs.Add(FString::Printf(
			TEXT("Using 'complex as simple' collision on %dk triangle mesh — switch to simple collision for better physics performance."),
			Stats.TriangleCount / 1000));
	}

	// Very high triangle count
	if (Stats.TriangleCount > 100000)
	{
		Recs.Add(FString::Printf(
			TEXT("Extremely high poly count (%dk triangles) — consider decimating or replacing with a lower-poly version."),
			Stats.TriangleCount / 1000));
	}

	return Recs;
}

TArray<FString> FSceneAnalyzer::GenerateOverallRecommendations(const FSceneAnalysisResult& Result)
{
	TArray<FString> Recs;

	// LOD recommendation
	if (Result.ActorsWithoutLODs > 0)
	{
		int64 NoLODTriangles = 0;
		for (const FActorMeshStats& Stats : Result.AllActorStats)
		{
			if (Stats.LODCount <= 1)
			{
				NoLODTriangles += Stats.TriangleCount;
			}
		}
		Recs.Add(FString::Printf(
			TEXT("%d mesh actors have no LODs, totaling %lldK triangles — adding LODs could reduce distant rendering cost significantly."),
			Result.ActorsWithoutLODs, NoLODTriangles / 1000));
	}

	// Material instancing recommendation
	if (Result.UniqueMeshes > 0 && Result.TotalMaterials > Result.MeshActors * 2)
	{
		Recs.Add(FString::Printf(
			TEXT("%d materials across %d mesh actors — consider material instancing to reduce shader permutations."),
			Result.TotalMaterials, Result.MeshActors));
	}

	// Complex collision on high-poly meshes
	int32 ComplexCollisionCount = 0;
	for (const FActorMeshStats& Stats : Result.AllActorStats)
	{
		if (Stats.CollisionComplexity == TEXT("complex_as_simple") && Stats.TriangleCount > 10000)
		{
			ComplexCollisionCount++;
		}
	}
	if (ComplexCollisionCount > 0)
	{
		Recs.Add(FString::Printf(
			TEXT("%d actors use 'complex as simple' collision on meshes with >10k triangles — switch to simple collision."),
			ComplexCollisionCount));
	}

	// High total draw call estimate
	if (Result.TotalEstimatedDrawCalls > 2000)
	{
		Recs.Add(FString::Printf(
			TEXT("Estimated %d draw calls — consider mesh merging, HLOD, or instanced rendering to reduce draw call count."),
			Result.TotalEstimatedDrawCalls));
	}

	return Recs;
}

FSanityCheck FSceneAnalyzer::RunSanityChecks(const FSceneAnalysisResult& Result)
{
	FSanityCheck Check;

	if (Result.TotalActors == 0)
	{
		Check.bValid = false;
		Check.Warnings.Add(TEXT("Level contains no actors — is a level loaded?"));
	}
	else if (Result.MeshActors == 0)
	{
		Check.Warnings.Add(TEXT("No actors with mesh components found in level."));
	}

	if (Result.TotalTriangles == 0 && Result.MeshActors > 0)
	{
		Check.bValid = false;
		Check.Warnings.Add(TEXT("Mesh actors found but total triangle count is 0 — render data may not be loaded."));
	}

	// Check for suspiciously uniform data
	if (Result.AllActorStats.Num() > 5)
	{
		bool bAllSameTriCount = true;
		int32 FirstTri = Result.AllActorStats[0].TriangleCount;
		for (int32 i = 1; i < Result.AllActorStats.Num(); ++i)
		{
			if (Result.AllActorStats[i].TriangleCount != FirstTri)
			{
				bAllSameTriCount = false;
				break;
			}
		}
		if (bAllSameTriCount)
		{
			Check.Warnings.Add(TEXT("All mesh actors have identical triangle counts — data may be static or synthetic."));
		}
	}

	return Check;
}

FLiveStats FSceneAnalyzer::CaptureLiveStats(UWorld* World)
{
	FLiveStats Stats;
	if (!GEngine || !World)
	{
		return Stats;
	}

	UGameViewportClient* Viewport = World->GetGameViewport();
	if (!Viewport)
	{
		return Stats;
	}
	const FStatUnitData* StatData = Viewport->GetStatUnitData();
	if (StatData)
	{
		Stats.FrameTimeMs = StatData->RawFrameTime;
		Stats.GameThreadMs = StatData->RawGameThreadTime;
		Stats.RenderThreadMs = StatData->RawRenderThreadTime;
		Stats.GPUTimeMs = StatData->RawGPUFrameTime[0];
		Stats.bCaptured = true;
	}
	return Stats;
}

FString FSceneAnalyzer::GetCollisionComplexityString(AActor* Actor)
{
	TArray<UStaticMeshComponent*> MeshComps;
	Actor->GetComponents<UStaticMeshComponent>(MeshComps);
	for (UStaticMeshComponent* Comp : MeshComps)
	{
		if (!Comp || Comp->GetCollisionEnabled() == ECollisionEnabled::NoCollision) continue;
		UStaticMesh* Mesh = Comp->GetStaticMesh();
		if (!Mesh) continue;
		UBodySetup* BodySetup = Mesh->GetBodySetup();
		if (BodySetup && BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple)
		{
			return TEXT("complex_as_simple");
		}
		return TEXT("simple");
	}
	return TEXT("none");
}

void FSceneAnalyzer::SortActorStats(TArray<FActorMeshStats>& Stats, const FString& SortBy)
{
	if (SortBy == TEXT("triangles"))
	{
		Stats.Sort([](const FActorMeshStats& A, const FActorMeshStats& B)
		{
			return A.TriangleCount > B.TriangleCount;
		});
	}
	else if (SortBy == TEXT("draw_calls"))
	{
		Stats.Sort([](const FActorMeshStats& A, const FActorMeshStats& B)
		{
			return A.EstimatedDrawCalls > B.EstimatedDrawCalls;
		});
	}
	else if (SortBy == TEXT("materials"))
	{
		Stats.Sort([](const FActorMeshStats& A, const FActorMeshStats& B)
		{
			return A.MaterialCount > B.MaterialCount;
		});
	}
	else // estimated_cost (default)
	{
		Stats.Sort([](const FActorMeshStats& A, const FActorMeshStats& B)
		{
			return A.EstimatedCost > B.EstimatedCost;
		});
	}
}

TSharedPtr<FJsonObject> FSceneAnalyzer::ActorStatsToJson(const FActorMeshStats& Stats)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();

	Json->SetStringField(TEXT("actor_label"), Stats.ActorLabel);
	Json->SetStringField(TEXT("actor_class"), Stats.ActorClass);

	TSharedPtr<FJsonObject> LocJson = MakeShared<FJsonObject>();
	LocJson->SetNumberField(TEXT("x"), Stats.Location.X);
	LocJson->SetNumberField(TEXT("y"), Stats.Location.Y);
	LocJson->SetNumberField(TEXT("z"), Stats.Location.Z);
	Json->SetObjectField(TEXT("location"), LocJson);

	Json->SetStringField(TEXT("mesh_path"), Stats.MeshPath);
	Json->SetNumberField(TEXT("triangle_count"), Stats.TriangleCount);
	Json->SetNumberField(TEXT("vertex_count"), Stats.VertexCount);
	Json->SetNumberField(TEXT("lod_count"), Stats.LODCount);
	Json->SetNumberField(TEXT("material_count"), Stats.MaterialCount);
	Json->SetNumberField(TEXT("mesh_section_count"), Stats.MeshSectionCount);
	Json->SetBoolField(TEXT("shadow_casting"), Stats.bShadowCasting);
	Json->SetBoolField(TEXT("has_collision"), Stats.bHasCollision);
	Json->SetStringField(TEXT("collision_complexity"), Stats.CollisionComplexity);
	Json->SetNumberField(TEXT("instance_count"), Stats.InstanceCount);
	Json->SetNumberField(TEXT("estimated_draw_calls"), Stats.EstimatedDrawCalls);
	Json->SetNumberField(TEXT("estimated_cost"), Stats.EstimatedCost);

	TArray<TSharedPtr<FJsonValue>> RecsArray;
	for (const FString& Rec : Stats.Recommendations)
	{
		RecsArray.Add(MakeShared<FJsonValueString>(Rec));
	}
	Json->SetArrayField(TEXT("recommendations"), RecsArray);

	return Json;
}

TSharedPtr<FJsonObject> FSceneAnalyzer::ResultToJson(
	const FSceneAnalysisResult& Result,
	const FString& SortBy,
	int32 Limit,
	bool bIncludeStats)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetNumberField(TEXT("total_actors"), Result.TotalActors);
	Summary->SetNumberField(TEXT("mesh_actors"), Result.MeshActors);
	Summary->SetNumberField(TEXT("total_triangles"), Result.TotalTriangles);
	Summary->SetNumberField(TEXT("total_estimated_draw_calls"), Result.TotalEstimatedDrawCalls);
	Summary->SetNumberField(TEXT("total_materials"), Result.TotalMaterials);
	Summary->SetNumberField(TEXT("unique_meshes"), Result.UniqueMeshes);
	Summary->SetNumberField(TEXT("actors_without_lods"), Result.ActorsWithoutLODs);
	Root->SetObjectField(TEXT("scene_summary"), Summary);
	if (bIncludeStats)
	{
		TSharedPtr<FJsonObject> StatsJson = MakeShared<FJsonObject>();
		StatsJson->SetBoolField(TEXT("captured"), Result.LiveStats.bCaptured);
		StatsJson->SetNumberField(TEXT("frame_time_ms"), Result.LiveStats.FrameTimeMs);
		StatsJson->SetNumberField(TEXT("game_thread_ms"), Result.LiveStats.GameThreadMs);
		StatsJson->SetNumberField(TEXT("render_thread_ms"), Result.LiveStats.RenderThreadMs);
		StatsJson->SetNumberField(TEXT("gpu_time_ms"), Result.LiveStats.GPUTimeMs);
		StatsJson->SetNumberField(TEXT("draw_calls"), Result.LiveStats.DrawCalls);
		StatsJson->SetNumberField(TEXT("mesh_draw_calls"), Result.LiveStats.MeshDrawCalls);
		Root->SetObjectField(TEXT("live_stats"), StatsJson);
	}
	TSharedPtr<FJsonObject> SanityJson = MakeShared<FJsonObject>();
	SanityJson->SetBoolField(TEXT("valid"), Result.SanityCheck.bValid);
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	for (const FString& Warning : Result.SanityCheck.Warnings)
	{
		WarningsArray.Add(MakeShared<FJsonValueString>(Warning));
	}
	SanityJson->SetArrayField(TEXT("warnings"), WarningsArray);
	Root->SetObjectField(TEXT("sanity_check"), SanityJson);
	TArray<FActorMeshStats> Sorted = Result.AllActorStats;
	SortActorStats(Sorted, SortBy);
	int32 Count = FMath::Min(Limit, Sorted.Num());

	TArray<TSharedPtr<FJsonValue>> BottlenecksArray;
	for (int32 i = 0; i < Count; ++i)
	{
		BottlenecksArray.Add(MakeShared<FJsonValueObject>(ActorStatsToJson(Sorted[i])));
	}
	Root->SetArrayField(TEXT("bottlenecks"), BottlenecksArray);
	TArray<TSharedPtr<FJsonValue>> OverallRecsArray;
	for (const FString& Rec : Result.OverallRecommendations)
	{
		OverallRecsArray.Add(MakeShared<FJsonValueString>(Rec));
	}
	Root->SetArrayField(TEXT("overall_recommendations"), OverallRecsArray);

	return Root;
}
