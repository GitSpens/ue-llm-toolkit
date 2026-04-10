// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_ScenePerformance.h"
#include "SceneAnalyzer.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

// ============================================================================
// Execute — dispatch to operation handlers
// ============================================================================

FMCPToolResult FMCPTool_ScenePerformance::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
	{
		return Error.GetValue();
	}

	Operation = Operation.ToLower();

	static const TMap<FString, FString> OpAliases = {
		{TEXT("analyze"), TEXT("analyze_scene")},
		{TEXT("analyse_scene"), TEXT("analyze_scene")},
		{TEXT("analyse"), TEXT("analyze_scene")},
		{TEXT("traces"), TEXT("list_traces")}
	};
	Operation = ResolveOperationAlias(Operation, OpAliases);

	if (Operation == TEXT("analyze_scene"))
	{
		return HandleAnalyzeScene(Params);
	}
	else if (Operation == TEXT("list_traces"))
	{
		return HandleListTraces(Params);
	}

	return UnknownOperationError(Operation, {TEXT("analyze_scene"), TEXT("list_traces")});
}

// ============================================================================
// HandleAnalyzeScene
// ============================================================================

FMCPToolResult FMCPTool_ScenePerformance::HandleAnalyzeScene(const TSharedRef<FJsonObject>& Params)
{
	// Validate editor context
	UWorld* World = nullptr;
	if (auto Error = ValidateEditorContext(World))
	{
		return Error.GetValue();
	}

	// Extract parameters
	int32 Limit = ExtractOptionalNumber<int32>(Params, TEXT("limit"), 20);
	Limit = FMath::Clamp(Limit, 1, 500);

	FString SortBy = ExtractOptionalString(Params, TEXT("sort_by"), TEXT("estimated_cost"));
	SortBy = SortBy.ToLower();

	// Validate sort_by
	static const TSet<FString> ValidSortValues = {
		TEXT("triangles"), TEXT("draw_calls"), TEXT("materials"), TEXT("estimated_cost")
	};
	if (!ValidSortValues.Contains(SortBy))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Invalid sort_by value '%s'. Must be one of: triangles, draw_calls, materials, estimated_cost"),
			*SortBy));
	}

	bool bIncludeStats = ExtractOptionalBool(Params, TEXT("include_stats"), true);
	FString ClassFilter = ExtractOptionalString(Params, TEXT("class_filter"));
	int32 MaxActors = ExtractOptionalNumber<int32>(Params, TEXT("max_actors"), 10000);
	MaxActors = FMath::Clamp(MaxActors, 1, 50000);

	// Run analysis
	FSceneAnalysisResult Result = FSceneAnalyzer::AnalyzeScene(World, ClassFilter, MaxActors);

	// Capture live stats if requested
	if (bIncludeStats)
	{
		Result.LiveStats = FSceneAnalyzer::CaptureLiveStats(World);

		// Add sanity warning if live stats suggest throttled state
		if (Result.LiveStats.bCaptured && Result.LiveStats.FrameTimeMs > 100.0f)
		{
			Result.SanityCheck.Warnings.Add(
				TEXT("Frame time >100ms — editor may be throttled or unfocused. Live stats may be inaccurate."));
		}
	}

	// Convert to JSON
	TSharedPtr<FJsonObject> ResponseData = FSceneAnalyzer::ResultToJson(Result, SortBy, Limit, bIncludeStats);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Analyzed %d actors (%d with mesh components)"),
			Result.TotalActors, Result.MeshActors),
		ResponseData);
}

// ============================================================================
// HandleListTraces
// ============================================================================

FMCPToolResult FMCPTool_ScenePerformance::HandleListTraces(const TSharedRef<FJsonObject>& Params)
{
	FString PathFilter = ExtractOptionalString(Params, TEXT("path_filter"));
	FString ProfilingDir = FPaths::ProfilingDir();

	// Check if profiling directory exists
	if (!IFileManager::Get().DirectoryExists(*ProfilingDir))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("No profiling directory found at '%s'. Run a trace session in Unreal Insights first."),
			*ProfilingDir));
	}

	// Apply path filter
	FString SearchDir = ProfilingDir;
	if (!PathFilter.IsEmpty())
	{
		SearchDir = FPaths::Combine(ProfilingDir, PathFilter);
		if (!IFileManager::Get().DirectoryExists(*SearchDir))
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Subdirectory '%s' not found in profiling directory"),
				*PathFilter));
		}
	}

	// Find .utrace files
	TArray<FString> TraceFiles;
	IFileManager::Get().FindFilesRecursive(
		TraceFiles, *SearchDir, TEXT("*.utrace"), true, false);

	TSharedPtr<FJsonObject> ResponseData = MakeShared<FJsonObject>();
	ResponseData->SetStringField(TEXT("trace_dir"), ProfilingDir);

	TArray<TSharedPtr<FJsonValue>> TracesArray;
	for (const FString& FilePath : TraceFiles)
	{
		TSharedPtr<FJsonObject> TraceJson = MakeShared<FJsonObject>();

		FString Filename = FPaths::GetCleanFilename(FilePath);
		TraceJson->SetStringField(TEXT("filename"), Filename);
		TraceJson->SetStringField(TEXT("path"), FilePath);

		// Get file size
		int64 FileSize = IFileManager::Get().FileSize(*FilePath);
		TraceJson->SetNumberField(TEXT("size_mb"),
			static_cast<double>(FileSize) / (1024.0 * 1024.0));

		// Get creation time
		FDateTime ModTime = IFileManager::Get().GetTimeStamp(*FilePath);
		TraceJson->SetStringField(TEXT("created"), ModTime.ToIso8601());

		TracesArray.Add(MakeShared<FJsonValueObject>(TraceJson));
	}

	ResponseData->SetArrayField(TEXT("traces"), TracesArray);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Found %d trace files"), TraceFiles.Num()),
		ResponseData);
}
