// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Scene performance analysis.
 * Read-only tool that analyzes per-actor mesh stats and generates optimization recommendations.
 *
 * Operations:
 *   - analyze_scene: Full scene analysis with bottleneck ranking and recommendations
 *   - list_traces: List available .utrace files for future CSV-based analysis
 */
class FMCPTool_ScenePerformance : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("unreal_scene_performance");
		Info.Description = TEXT(
			"Analyze scene performance by inspecting per-actor mesh data.\n\n"
			"Operations:\n"
			"- 'analyze_scene': Scans all actors, gathers triangle counts, LODs, materials, "
			"shadow settings, estimates rendering cost, validates data sanity, and returns "
			"a ranked report of bottleneck actors with specific optimization recommendations.\n"
			"- 'list_traces': Lists available .utrace files in the profiling directory.\n\n"
			"This is a read-only analysis tool — it does not modify any assets or actors."
		);
		Info.Parameters = {
			FMCPToolParameter(TEXT("operation"), TEXT("string"),
				TEXT("Operation: 'analyze_scene' or 'list_traces'"), true),
			FMCPToolParameter(TEXT("limit"), TEXT("number"),
				TEXT("For 'analyze_scene': Max bottleneck actors to return (default 20, max 500)"), false),
			FMCPToolParameter(TEXT("sort_by"), TEXT("string"),
				TEXT("For 'analyze_scene': Ranking metric — 'triangles', 'draw_calls', 'materials', or 'estimated_cost' (default)"), false),
			FMCPToolParameter(TEXT("include_stats"), TEXT("boolean"),
				TEXT("For 'analyze_scene': Capture live aggregate stats via console commands (default true)"), false),
			FMCPToolParameter(TEXT("class_filter"), TEXT("string"),
				TEXT("For 'analyze_scene': Only analyze actors of this class (e.g., 'StaticMeshActor')"), false),
			FMCPToolParameter(TEXT("max_actors"), TEXT("number"),
				TEXT("For 'analyze_scene': Max actors to scan — safety limit for large scenes (default 10000)"), false),
			FMCPToolParameter(TEXT("path_filter"), TEXT("string"),
				TEXT("For 'list_traces': Subdirectory filter within Saved/Profiling/"), false)
		};
		Info.Annotations = FMCPToolAnnotations::ReadOnly();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult HandleAnalyzeScene(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult HandleListTraces(const TSharedRef<FJsonObject>& Params);
};
