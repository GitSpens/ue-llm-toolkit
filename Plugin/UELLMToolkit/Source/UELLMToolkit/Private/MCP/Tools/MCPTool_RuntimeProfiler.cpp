// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_RuntimeProfiler.h"
#include "RuntimeProfiler.h"
#include "DeviceBudgets.h"
#include "Editor.h"

FMCPTool_RuntimeProfiler::FMCPTool_RuntimeProfiler()
    : Profiler(MakeShared<FRuntimeProfiler>())
{
}

FMCPTool_RuntimeProfiler::~FMCPTool_RuntimeProfiler()
{
}

FMCPToolResult FMCPTool_RuntimeProfiler::Execute(const TSharedRef<FJsonObject>& Params)
{
    FString Operation;
    TOptional<FMCPToolResult> Error;
    if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Error))
    {
        return Error.GetValue();
    }

    Operation = Operation.ToLower();

    static const TMap<FString, FString> OpAliases = {
        {TEXT("start"), TEXT("start_profiling")},
        {TEXT("profile"), TEXT("start_profiling")},
        {TEXT("status"), TEXT("get_status")},
        {TEXT("results"), TEXT("get_results")},
        {TEXT("report"), TEXT("get_results")}
    };
    Operation = ResolveOperationAlias(Operation, OpAliases);

    if (Operation == TEXT("start_profiling"))
    {
        return HandleStartProfiling(Params);
    }
    else if (Operation == TEXT("get_status"))
    {
        return HandleGetStatus(Params);
    }
    else if (Operation == TEXT("get_results"))
    {
        return HandleGetResults(Params);
    }

    return UnknownOperationError(Operation,
        {TEXT("start_profiling"), TEXT("get_status"), TEXT("get_results")});
}

FMCPToolResult FMCPTool_RuntimeProfiler::HandleStartProfiling(const TSharedRef<FJsonObject>& Params)
{
    if (Profiler->IsActive())
    {
        return FMCPToolResult::Error(
            TEXT("Profiling session already active. Stop PIE to end the current session."));
    }

    float IntervalMs = ExtractOptionalNumber<float>(Params, TEXT("interval_ms"), 200.0f);
    FString TargetDevice = ExtractOptionalString(Params, TEXT("target_device"), TEXT("pc_high"));
    float SpikeThreshold = ExtractOptionalNumber<float>(Params, TEXT("spike_threshold"), 2.0f);

    FDeviceBudget Budget = FDeviceBudget::Get(TargetDevice);
    FString DeviceWarning;
    if (!Budget.bValid)
    {
        TArray<FString> ValidDevices = FDeviceBudget::GetAllDeviceNames();
        DeviceWarning = FString::Printf(
            TEXT(" Warning: unknown device '%s' — device-specific recommendations will be skipped. "
                "Valid presets: %s."),
            *TargetDevice, *FString::Join(ValidDevices, TEXT(", ")));
    }

    if (!Profiler->Start(IntervalMs, TargetDevice, SpikeThreshold))
    {
        return FMCPToolResult::Error(TEXT("Failed to start profiling."));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("interval_ms"), IntervalMs);
    Data->SetStringField(TEXT("target_device"), TargetDevice);
    Data->SetNumberField(TEXT("spike_threshold"), SpikeThreshold);

    FString Message = FString::Printf(
        TEXT("Profiling started — sampling every %.0fms, target device: %s. "
            "Play the game and stop PIE when done. Call get_results after PIE ends.%s"),
        IntervalMs, *TargetDevice, *DeviceWarning);

    return FMCPToolResult::Success(Message, Data);
}

FMCPToolResult FMCPTool_RuntimeProfiler::HandleGetStatus(const TSharedRef<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("active"), Profiler->IsActive());
    Data->SetNumberField(TEXT("sample_count"), Profiler->GetSampleCount());
    Data->SetNumberField(TEXT("elapsed_seconds"), Profiler->GetElapsedSeconds());
    Data->SetBoolField(TEXT("results_available"), Profiler->GetLastAnalysis() != nullptr);

    if (Profiler->IsActive())
    {
        return FMCPToolResult::Success(
            FString::Printf(TEXT("Profiling active — %d samples collected over %.1fs"),
                Profiler->GetSampleCount(), Profiler->GetElapsedSeconds()),
            Data);
    }
    else if (Profiler->GetLastAnalysis())
    {
        return FMCPToolResult::Success(
            TEXT("Profiling inactive — results available from last session. Call get_results."),
            Data);
    }
    else
    {
        return FMCPToolResult::Success(
            TEXT("Profiling inactive — no session data available. Call start_profiling first."),
            Data);
    }
}

FMCPToolResult FMCPTool_RuntimeProfiler::HandleGetResults(const TSharedRef<FJsonObject>& Params)
{
    if (Profiler->IsActive())
    {
        return FMCPToolResult::Error(
            TEXT("Profiling in progress. Stop PIE first to generate results."));
    }

    const FProfileAnalysis* Analysis = Profiler->GetLastAnalysis();
    if (!Analysis)
    {
        return FMCPToolResult::Error(
            TEXT("No profiling session found. Call start_profiling first."));
    }

    FString DetailLevel = ExtractOptionalString(Params, TEXT("detail_level"), TEXT("summary"));
    bool bFullDetail = DetailLevel.ToLower() == TEXT("full");

    TSharedPtr<FJsonObject> ResultJson = FRuntimeProfiler::AnalysisToJson(*Analysis, bFullDetail);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Analysis of %d samples over %.1fs — %d spikes detected, %d recommendations."),
            Analysis->SampleCount, Analysis->DurationSeconds,
            Analysis->Spikes.Num(), Analysis->Recommendations.Num()),
        ResultJson);
}
