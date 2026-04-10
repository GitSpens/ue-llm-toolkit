// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

class FRuntimeProfiler;

class FMCPTool_RuntimeProfiler : public FMCPToolBase
{
public:
    FMCPTool_RuntimeProfiler();
    virtual ~FMCPTool_RuntimeProfiler();

    virtual FMCPToolInfo GetInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("runtime_profiler");
        Info.Description = TEXT(
            "Profile runtime performance during PIE gameplay.\n\n"
            "Operations:\n"
            "- 'start_profiling': Begin collecting performance metrics. Attaches to running PIE "
            "or starts PIE if not running. Profiling auto-stops when PIE ends.\n"
            "- 'get_status': Check if profiling is active, duration, and sample count.\n"
            "- 'get_results': Retrieve the last profiling session's analysis — summary stats, "
            "spike detection with frustum-based spatial correlation, and actionable recommendations "
            "adjusted for the target device budget.\n\n"
            "Workflow: start_profiling → user plays game → stop PIE → get_results\n\n"
            "Metrics captured: frame timing (FPS, game/render/GPU thread times), "
            "rendering (draw calls, triangles rendered), memory (total, texture streaming), "
            "and spatial context (player position, camera frustum).\n\n"
            "Spike analysis correlates FPS drops with actors visible in the camera frustum, "
            "identifies high-poly actors without LODs, excessive draw calls, and generates "
            "specific fix recommendations referencing actor names and locations."
        );
        Info.Parameters = {
            FMCPToolParameter(TEXT("operation"), TEXT("string"),
                TEXT("Operation: 'start_profiling', 'get_status', or 'get_results'"), true),
            FMCPToolParameter(TEXT("interval_ms"), TEXT("number"),
                TEXT("start_profiling: Sampling interval in ms (default 200, min 50)"), false),
            FMCPToolParameter(TEXT("target_device"), TEXT("string"),
                TEXT("start_profiling: Device preset for recommendation thresholds — "
                    "'quest_3_standalone', 'mobile', 'pc_mid', 'pc_high' (default)"), false),
            FMCPToolParameter(TEXT("spike_threshold"), TEXT("number"),
                TEXT("start_profiling: Multiplier over avg frame time to flag as spike (default 2.0)"), false),
            FMCPToolParameter(TEXT("detail_level"), TEXT("string"),
                TEXT("get_results: 'summary' (default) or 'full' (includes per-spike actor breakdowns)"), false)
        };
        Info.Annotations = FMCPToolAnnotations::Modifying();
        return Info;
    }

    virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
    FMCPToolResult HandleStartProfiling(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult HandleGetStatus(const TSharedRef<FJsonObject>& Params);
    FMCPToolResult HandleGetResults(const TSharedRef<FJsonObject>& Params);

    TSharedPtr<FRuntimeProfiler> Profiler;
};
