// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UWorld;
class AActor;

/**
 * Per-actor mesh statistics gathered during scene analysis.
 */
struct FActorMeshStats
{
	FString ActorLabel;
	FString ActorClass;
	FVector Location = FVector::ZeroVector;
	FString MeshPath;
	int32 TriangleCount = 0;
	int32 VertexCount = 0;
	int32 LODCount = 0;
	int32 MaterialCount = 0;
	int32 MeshSectionCount = 0;
	bool bShadowCasting = false;
	bool bHasCollision = false;
	FString CollisionComplexity;
	int32 InstanceCount = 1;
	int32 EstimatedDrawCalls = 0;
	float EstimatedCost = 0.0f;
	TArray<FString> Recommendations;
};

/**
 * Live aggregate stats captured from console commands.
 */
struct FLiveStats
{
	bool bCaptured = false;
	float FrameTimeMs = 0.0f;
	float GameThreadMs = 0.0f;
	float RenderThreadMs = 0.0f;
	float GPUTimeMs = 0.0f;
	int32 DrawCalls = 0;
	int32 MeshDrawCalls = 0;
};

/**
 * Sanity check result for the analysis.
 */
struct FSanityCheck
{
	bool bValid = true;
	TArray<FString> Warnings;
};

/**
 * Complete scene analysis result.
 */
struct FSceneAnalysisResult
{
	int32 TotalActors = 0;
	int32 MeshActors = 0;
	int64 TotalTriangles = 0;
	int32 TotalEstimatedDrawCalls = 0;
	int32 TotalMaterials = 0;
	int32 UniqueMeshes = 0;
	int32 ActorsWithoutLODs = 0;

	TArray<FActorMeshStats> AllActorStats;
	FLiveStats LiveStats;
	FSanityCheck SanityCheck;
	TArray<FString> OverallRecommendations;
};

/**
 * Utility class for scene performance analysis.
 * Gathers mesh stats, estimates cost, generates recommendations.
 */
class FSceneAnalyzer
{
public:
	/** Analyze all actors in the world and produce a complete result. */
	static FSceneAnalysisResult AnalyzeScene(
		UWorld* World,
		const FString& ClassFilter,
		int32 MaxActors);

	/** Gather mesh stats for a single actor. Returns false if actor has no mesh components. */
	static bool GatherActorMeshStats(AActor* Actor, FActorMeshStats& OutStats);

	/** Calculate estimated cost for an actor based on its mesh stats. */
	static float CalculateEstimatedCost(const FActorMeshStats& Stats);

	/** Generate per-actor recommendations based on mesh stats. */
	static TArray<FString> GenerateActorRecommendations(const FActorMeshStats& Stats);

	/** Generate scene-wide recommendations from the full analysis result. */
	static TArray<FString> GenerateOverallRecommendations(const FSceneAnalysisResult& Result);

	/** Run sanity checks on the analysis result. */
	static FSanityCheck RunSanityChecks(const FSceneAnalysisResult& Result);

	/** Capture live aggregate stats via console commands. */
	static FLiveStats CaptureLiveStats(UWorld* World);

	/** Convert a complete analysis result to JSON. */
	static TSharedPtr<FJsonObject> ResultToJson(
		const FSceneAnalysisResult& Result,
		const FString& SortBy,
		int32 Limit,
		bool bIncludeStats);

	/** Convert a single actor's stats to JSON. */
	static TSharedPtr<FJsonObject> ActorStatsToJson(const FActorMeshStats& Stats);

private:
	/** Sort actor stats by the given metric. */
	static void SortActorStats(
		TArray<FActorMeshStats>& Stats,
		const FString& SortBy);

	/** Get collision complexity as a readable string. */
	static FString GetCollisionComplexityString(AActor* Actor);
};
