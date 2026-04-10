// Copyright Natali Caggiano. All Rights Reserved.

/**
 * Automation tests for the Scene Performance Analyzer tool.
 * Tests tool metadata, parameter validation, cost estimation logic,
 * recommendation generation, sanity checks, and operation dispatch.
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "MCP/Tools/MCPTool_ScenePerformance.h"
#include "SceneAnalyzer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#if WITH_DEV_AUTOMATION_TESTS

// ===== Tool Info Tests =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_GetInfo,
	"UELLMToolkit.ScenePerformance.GetInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_GetInfo::RunTest(const FString& Parameters)
{
	FMCPTool_ScenePerformance Tool;
	FMCPToolInfo Info = Tool.GetInfo();

	TestEqual("Tool name", Info.Name, TEXT("unreal_scene_performance"));
	TestTrue("Description not empty", !Info.Description.IsEmpty());
	TestTrue("Has parameters", Info.Parameters.Num() > 0);

	// Check required 'operation' parameter exists
	bool bHasOperation = false;
	for (const FMCPToolParameter& Param : Info.Parameters)
	{
		if (Param.Name == TEXT("operation"))
		{
			bHasOperation = true;
			TestTrue("operation should be required", Param.bRequired);
		}
	}
	TestTrue("Should have 'operation' parameter", bHasOperation);

	// Verify read-only annotation
	TestTrue("Should be read-only", Info.Annotations.bReadOnlyHint);

	return true;
}

// ===== Dispatch Tests =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_UnknownOperation,
	"UELLMToolkit.ScenePerformance.UnknownOperation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_UnknownOperation::RunTest(const FString& Parameters)
{
	FMCPTool_ScenePerformance Tool;
	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("invalid_op"));

	FMCPToolResult Result = Tool.Execute(Params);

	TestTrue("Should be error", !Result.bSuccess);
	TestTrue("Error mentions unknown operation",
		Result.Message.Contains(TEXT("Unknown operation")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_MissingOperation,
	"UELLMToolkit.ScenePerformance.MissingOperation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_MissingOperation::RunTest(const FString& Parameters)
{
	FMCPTool_ScenePerformance Tool;
	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();

	FMCPToolResult Result = Tool.Execute(Params);

	TestTrue("Should be error", !Result.bSuccess);
	TestTrue("Error mentions missing required parameter",
		Result.Message.Contains(TEXT("Missing required parameter")) ||
		Result.Message.Contains(TEXT("required")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_AnalyzeScene_InvalidSortBy,
	"UELLMToolkit.ScenePerformance.AnalyzeScene.InvalidSortBy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_AnalyzeScene_InvalidSortBy::RunTest(const FString& Parameters)
{
	FMCPTool_ScenePerformance Tool;
	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("analyze_scene"));
	Params->SetStringField(TEXT("sort_by"), TEXT("invalid"));

	FMCPToolResult Result = Tool.Execute(Params);

	TestTrue("Should be error", !Result.bSuccess);
	TestTrue("Error mentions invalid sort_by",
		Result.Message.Contains(TEXT("Invalid sort_by value")));

	return true;
}

// ===== Cost Estimation Tests =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_CostEstimation_Basic,
	"UELLMToolkit.ScenePerformance.CostEstimation.Basic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_CostEstimation_Basic::RunTest(const FString& Parameters)
{
	// Simple mesh: 10k triangles, 2 materials, 2 sections, no shadow, has LODs
	FActorMeshStats Stats;
	Stats.TriangleCount = 10000;
	Stats.MaterialCount = 2;
	Stats.MeshSectionCount = 2;
	Stats.bShadowCasting = false;
	Stats.LODCount = 3;

	float Cost = FSceneAnalyzer::CalculateEstimatedCost(Stats);

	// Expected: (10000/1000) * 2 * 2 * 1.0 * 1.0 = 40.0
	TestEqual("Basic cost calculation", Cost, 40.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_CostEstimation_ShadowMultiplier,
	"UELLMToolkit.ScenePerformance.CostEstimation.ShadowMultiplier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_CostEstimation_ShadowMultiplier::RunTest(const FString& Parameters)
{
	FActorMeshStats Stats;
	Stats.TriangleCount = 10000;
	Stats.MaterialCount = 2;
	Stats.MeshSectionCount = 2;
	Stats.bShadowCasting = true;
	Stats.LODCount = 3;

	float Cost = FSceneAnalyzer::CalculateEstimatedCost(Stats);

	// Expected: (10000/1000) * 2 * 2 * 1.5 = 60.0
	TestEqual("Shadow casting adds 1.5x multiplier", Cost, 60.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_CostEstimation_NoLODPenalty,
	"UELLMToolkit.ScenePerformance.CostEstimation.NoLODPenalty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_CostEstimation_NoLODPenalty::RunTest(const FString& Parameters)
{
	// High-poly mesh with no LODs and shadow casting
	FActorMeshStats Stats;
	Stats.TriangleCount = 52000;
	Stats.MaterialCount = 3;
	Stats.MeshSectionCount = 3;
	Stats.bShadowCasting = true;
	Stats.LODCount = 0;

	float Cost = FSceneAnalyzer::CalculateEstimatedCost(Stats);

	// Expected: (52000/1000) * 3 * 3 * 1.5 * 2.0 = 52 * 9 * 3.0 = 1404.0
	TestEqual("No LOD penalty on high-poly mesh", Cost, 1404.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_CostEstimation_LowPolyNoLODNoPenalty,
	"UELLMToolkit.ScenePerformance.CostEstimation.LowPolyNoLODNoPenalty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_CostEstimation_LowPolyNoLODNoPenalty::RunTest(const FString& Parameters)
{
	// Low-poly mesh with no LODs — should NOT get the 2x penalty
	FActorMeshStats Stats;
	Stats.TriangleCount = 1000;
	Stats.MaterialCount = 1;
	Stats.MeshSectionCount = 1;
	Stats.bShadowCasting = false;
	Stats.LODCount = 0;

	float Cost = FSceneAnalyzer::CalculateEstimatedCost(Stats);

	// Expected: (1000/1000) * 1 * 1 * 1.0 * 1.0 = 1.0 (no penalty, triangles <= 5000)
	TestEqual("Low-poly mesh without LODs gets no penalty", Cost, 1.0f);

	return true;
}

// ===== Recommendation Tests =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_Recommendations_NoLODs,
	"UELLMToolkit.ScenePerformance.Recommendations.NoLODs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_Recommendations_NoLODs::RunTest(const FString& Parameters)
{
	FActorMeshStats Stats;
	Stats.TriangleCount = 52000;
	Stats.LODCount = 0;
	Stats.MaterialCount = 1;
	Stats.MeshSectionCount = 1;

	TArray<FString> Recs = FSceneAnalyzer::GenerateActorRecommendations(Stats);

	bool bHasLODRec = false;
	for (const FString& Rec : Recs)
	{
		if (Rec.Contains(TEXT("Add LODs")))
		{
			bHasLODRec = true;
			break;
		}
	}
	TestTrue("Should recommend adding LODs for high-poly mesh", bHasLODRec);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_Recommendations_HighMaterials,
	"UELLMToolkit.ScenePerformance.Recommendations.HighMaterials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_Recommendations_HighMaterials::RunTest(const FString& Parameters)
{
	FActorMeshStats Stats;
	Stats.TriangleCount = 20000;
	Stats.LODCount = 3;
	Stats.MaterialCount = 5;
	Stats.MeshSectionCount = 5;

	TArray<FString> Recs = FSceneAnalyzer::GenerateActorRecommendations(Stats);

	bool bHasMaterialRec = false;
	for (const FString& Rec : Recs)
	{
		if (Rec.Contains(TEXT("material count")))
		{
			bHasMaterialRec = true;
			break;
		}
	}
	TestTrue("Should recommend merging materials on high-poly mesh", bHasMaterialRec);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_Recommendations_ComplexCollision,
	"UELLMToolkit.ScenePerformance.Recommendations.ComplexCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_Recommendations_ComplexCollision::RunTest(const FString& Parameters)
{
	FActorMeshStats Stats;
	Stats.TriangleCount = 30000;
	Stats.LODCount = 3;
	Stats.MaterialCount = 1;
	Stats.MeshSectionCount = 1;
	Stats.CollisionComplexity = TEXT("complex_as_simple");

	TArray<FString> Recs = FSceneAnalyzer::GenerateActorRecommendations(Stats);

	bool bHasCollisionRec = false;
	for (const FString& Rec : Recs)
	{
		if (Rec.Contains(TEXT("complex as simple")))
		{
			bHasCollisionRec = true;
			break;
		}
	}
	TestTrue("Should recommend simple collision for high-poly mesh", bHasCollisionRec);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_Recommendations_LowPolyNoRecs,
	"UELLMToolkit.ScenePerformance.Recommendations.LowPolyNoRecs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_Recommendations_LowPolyNoRecs::RunTest(const FString& Parameters)
{
	FActorMeshStats Stats;
	Stats.TriangleCount = 500;
	Stats.LODCount = 2;
	Stats.MaterialCount = 1;
	Stats.MeshSectionCount = 1;
	Stats.InstanceCount = 1;
	Stats.bShadowCasting = false;

	TArray<FString> Recs = FSceneAnalyzer::GenerateActorRecommendations(Stats);

	TestEqual("Low-poly well-configured mesh should have no recommendations", Recs.Num(), 0);

	return true;
}

// ===== Sanity Check Tests =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_SanityCheck_EmptyScene,
	"UELLMToolkit.ScenePerformance.SanityCheck.EmptyScene",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_SanityCheck_EmptyScene::RunTest(const FString& Parameters)
{
	FSceneAnalysisResult Result;
	Result.TotalActors = 0;
	Result.MeshActors = 0;

	FSanityCheck Check = FSceneAnalyzer::RunSanityChecks(Result);

	TestFalse("Empty scene should not be valid", Check.bValid);
	TestTrue("Should have warnings", Check.Warnings.Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_SanityCheck_NoMeshActors,
	"UELLMToolkit.ScenePerformance.SanityCheck.NoMeshActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_SanityCheck_NoMeshActors::RunTest(const FString& Parameters)
{
	FSceneAnalysisResult Result;
	Result.TotalActors = 50;
	Result.MeshActors = 0;

	FSanityCheck Check = FSceneAnalyzer::RunSanityChecks(Result);

	TestTrue("Should still be valid", Check.bValid);
	bool bHasNoMeshWarning = false;
	for (const FString& Warning : Check.Warnings)
	{
		if (Warning.Contains(TEXT("No actors with mesh components")))
		{
			bHasNoMeshWarning = true;
			break;
		}
	}
	TestTrue("Should warn about no mesh actors", bHasNoMeshWarning);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_SanityCheck_ZeroTriangles,
	"UELLMToolkit.ScenePerformance.SanityCheck.ZeroTriangles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_SanityCheck_ZeroTriangles::RunTest(const FString& Parameters)
{
	FSceneAnalysisResult Result;
	Result.TotalActors = 10;
	Result.MeshActors = 5;
	Result.TotalTriangles = 0;

	FSanityCheck Check = FSceneAnalyzer::RunSanityChecks(Result);

	TestFalse("Zero triangles with mesh actors should be invalid", Check.bValid);

	return true;
}

// ===== ResultToJson Tests =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_ResultToJson_Structure,
	"UELLMToolkit.ScenePerformance.ResultToJson.Structure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_ResultToJson_Structure::RunTest(const FString& Parameters)
{
	FSceneAnalysisResult Result;
	Result.TotalActors = 100;
	Result.MeshActors = 10;
	Result.TotalTriangles = 500000;
	Result.TotalEstimatedDrawCalls = 50;
	Result.TotalMaterials = 20;
	Result.UniqueMeshes = 5;
	Result.ActorsWithoutLODs = 3;

	TSharedPtr<FJsonObject> Json = FSceneAnalyzer::ResultToJson(Result, TEXT("estimated_cost"), 20, true);

	TestTrue("Has scene_summary", Json->HasField(TEXT("scene_summary")));
	TestTrue("Has live_stats", Json->HasField(TEXT("live_stats")));
	TestTrue("Has sanity_check", Json->HasField(TEXT("sanity_check")));
	TestTrue("Has bottlenecks", Json->HasField(TEXT("bottlenecks")));
	TestTrue("Has overall_recommendations", Json->HasField(TEXT("overall_recommendations")));

	// Verify summary values
	const TSharedPtr<FJsonObject>* Summary;
	if (Json->TryGetObjectField(TEXT("scene_summary"), Summary))
	{
		TestEqual("total_actors",
			static_cast<int32>((*Summary)->GetNumberField(TEXT("total_actors"))), 100);
		TestEqual("mesh_actors",
			static_cast<int32>((*Summary)->GetNumberField(TEXT("mesh_actors"))), 10);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_ResultToJson_NoStats,
	"UELLMToolkit.ScenePerformance.ResultToJson.NoStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_ResultToJson_NoStats::RunTest(const FString& Parameters)
{
	FSceneAnalysisResult Result;
	Result.TotalActors = 10;

	TSharedPtr<FJsonObject> Json = FSceneAnalyzer::ResultToJson(Result, TEXT("estimated_cost"), 20, false);

	TestFalse("Should not have live_stats when excluded", Json->HasField(TEXT("live_stats")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_ResultToJson_LimitBottlenecks,
	"UELLMToolkit.ScenePerformance.ResultToJson.LimitBottlenecks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_ResultToJson_LimitBottlenecks::RunTest(const FString& Parameters)
{
	FSceneAnalysisResult Result;
	Result.TotalActors = 10;
	Result.MeshActors = 10;
	Result.TotalTriangles = 100000;

	// Add 10 actors
	for (int32 i = 0; i < 10; ++i)
	{
		FActorMeshStats Stats;
		Stats.ActorLabel = FString::Printf(TEXT("Actor_%d"), i);
		Stats.ActorClass = TEXT("StaticMeshActor");
		Stats.TriangleCount = (i + 1) * 1000;
		Stats.MaterialCount = 1;
		Stats.MeshSectionCount = 1;
		Stats.LODCount = 2;
		Stats.EstimatedCost = static_cast<float>((i + 1) * 1000) / 1000.0f;
		Result.AllActorStats.Add(Stats);
	}

	TSharedPtr<FJsonObject> Json = FSceneAnalyzer::ResultToJson(Result, TEXT("estimated_cost"), 5, false);

	const TArray<TSharedPtr<FJsonValue>>* Bottlenecks;
	if (Json->TryGetArrayField(TEXT("bottlenecks"), Bottlenecks))
	{
		TestTrue("Bottlenecks should be limited to 5", Bottlenecks->Num() <= 5);
	}

	return true;
}

// ===== Operation Alias Tests =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScenePerformance_OperationAlias_Analyse,
	"UELLMToolkit.ScenePerformance.OperationAlias.Analyse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FScenePerformance_OperationAlias_Analyse::RunTest(const FString& Parameters)
{
	// "analyse_scene" should alias to "analyze_scene" and hit sort_by validation
	// (not return "unknown operation")
	FMCPTool_ScenePerformance Tool;
	TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("operation"), TEXT("analyse_scene"));
	Params->SetStringField(TEXT("sort_by"), TEXT("bad_value"));

	FMCPToolResult Result = Tool.Execute(Params);

	// If alias worked, we get sort_by error (not unknown operation error)
	TestTrue("Should be error", !Result.bSuccess);
	TestTrue("Should hit sort_by validation, not unknown operation",
		Result.Message.Contains(TEXT("Invalid sort_by value")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
