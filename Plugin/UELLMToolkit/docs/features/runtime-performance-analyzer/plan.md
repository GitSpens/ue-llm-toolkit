# Runtime Performance Analyzer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a new `runtime_profiler` MCP tool that collects real-time performance metrics during PIE gameplay, correlates spikes with visible scene content via frustum queries, and generates actionable recommendations adjusted for target device budgets.

**Architecture:** Ticker-based sampling during PIE (FTSTicker at configurable interval), auto-stop via FEditorDelegates::EndPIE, 3-pass analysis pipeline (summary stats, spike detection with frustum-based spatial correlation, recommendation generation). Follows existing plugin pattern: utility class (FRuntimeProfiler) does the work, tool class (MCPTool_RuntimeProfiler) handles JSON dispatch.

**Tech Stack:** UE 5.7.4 C++, FTSTicker, FEditorDelegates, FStatUnitData, view frustum math, FSceneAnalyzer (reused), MCP tool framework.

**Spec:** `docs/features/runtime-performance-analyzer/spec.md`

---

### Task 1: Device Budget Data Structure

**Files:**
- Create: `Source/UELLMToolkit/Private/DeviceBudgets.h`
- Test: `Source/UELLMToolkit/Private/Tests/RuntimeProfilerTests.cpp`

- [ ] **Step 1: Write failing test for device budget lookup**

Create the test file with the first test:

```cpp
// Source/UELLMToolkit/Private/Tests/RuntimeProfilerTests.cpp

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "DeviceBudgets.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDeviceBudgetLookupTest,
    "UnrealClaude.RuntimeProfiler.DeviceBudgetLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FDeviceBudgetLookupTest::RunTest(const FString& Parameters)
{
    // Known device should return valid budget
    FDeviceBudget Quest3Budget = FDeviceBudget::Get(TEXT("quest_3_standalone"));
    TestTrue("Quest 3 budget is valid", Quest3Budget.bValid);
    TestEqual("Quest 3 max draw calls", Quest3Budget.MaxDrawCalls, 200);
    TestEqual("Quest 3 triangle budget", Quest3Budget.TriangleBudget, 750000);
    TestEqual("Quest 3 texture memory MB", Quest3Budget.TextureMemoryMB, 2048);
    TestFalse("Quest 3 no raytracing", Quest3Budget.bSupportsRaytracing);

    // PC high should have larger budgets
    FDeviceBudget PCHighBudget = FDeviceBudget::Get(TEXT("pc_high"));
    TestTrue("PC high budget is valid", PCHighBudget.bValid);
    TestTrue("PC high draw calls > Quest 3", PCHighBudget.MaxDrawCalls > Quest3Budget.MaxDrawCalls);
    TestTrue("PC high supports raytracing", PCHighBudget.bSupportsRaytracing);

    // Unknown device should return invalid
    FDeviceBudget UnknownBudget = FDeviceBudget::Get(TEXT("nonexistent_device"));
    TestFalse("Unknown device is invalid", UnknownBudget.bValid);

    // All known presets should be retrievable
    TArray<FString> AllNames = FDeviceBudget::GetAllDeviceNames();
    TestTrue("At least 4 presets exist", AllNames.Num() >= 4);
    TestTrue("Contains quest_3_standalone", AllNames.Contains(TEXT("quest_3_standalone")));
    TestTrue("Contains pc_high", AllNames.Contains(TEXT("pc_high")));

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: Run test to verify it fails**

Build the plugin in UE Editor (compile button or Ctrl+Alt+F11). Expected: compile error — `DeviceBudgets.h` not found.

- [ ] **Step 3: Implement DeviceBudgets.h**

```cpp
// Source/UELLMToolkit/Private/DeviceBudgets.h

#pragma once

#include "CoreMinimal.h"

/**
 * Hardware budget thresholds for a target device.
 * Used by the runtime profiler to contextualize recommendations.
 */
struct FDeviceBudget
{
    /** Whether this is a valid/known device preset */
    bool bValid = false;

    /** Display name for the device */
    FString DisplayName;

    /** Maximum recommended draw calls per frame */
    int32 MaxDrawCalls = 0;

    /** Maximum recommended triangles rendered per frame */
    int32 TriangleBudget = 0;

    /** Maximum texture memory in MB */
    int32 TextureMemoryMB = 0;

    /** Whether the device supports dynamic shadows */
    bool bSupportsDynamicShadows = true;

    /** Whether the device supports raytracing */
    bool bSupportsRaytracing = false;

    /** Look up a device budget by name. Returns invalid budget if not found. */
    static FDeviceBudget Get(const FString& DeviceName)
    {
        const TMap<FString, FDeviceBudget>& Presets = GetPresets();
        const FDeviceBudget* Found = Presets.Find(DeviceName.ToLower());
        if (Found)
        {
            return *Found;
        }
        return FDeviceBudget(); // bValid = false
    }

    /** Get all known device preset names. */
    static TArray<FString> GetAllDeviceNames()
    {
        TArray<FString> Names;
        GetPresets().GetKeys(Names);
        return Names;
    }

private:
    static FDeviceBudget MakePreset(const FString& InDisplayName, int32 InDrawCalls, int32 InTriangles, int32 InTexMemMB, bool bInDynShadows, bool bInRT)
    {
        FDeviceBudget B;
        B.bValid = true;
        B.DisplayName = InDisplayName;
        B.MaxDrawCalls = InDrawCalls;
        B.TriangleBudget = InTriangles;
        B.TextureMemoryMB = InTexMemMB;
        B.bSupportsDynamicShadows = bInDynShadows;
        B.bSupportsRaytracing = bInRT;
        return B;
    }

    static const TMap<FString, FDeviceBudget>& GetPresets()
    {
        static TMap<FString, FDeviceBudget> Presets = []()
        {
            TMap<FString, FDeviceBudget> P;
            P.Add(TEXT("quest_3_standalone"), MakePreset(TEXT("Quest 3 Standalone"),   200,   750000,  2048, false, false));
            P.Add(TEXT("mobile"),             MakePreset(TEXT("Mobile"),                300,   500000,  1536, false, false));
            P.Add(TEXT("pc_mid"),             MakePreset(TEXT("PC Mid-Range"),         2000,  5000000,  6144, true,  false));
            P.Add(TEXT("pc_high"),            MakePreset(TEXT("PC High-End"),          5000, 10000000, 12288, true,  true));
            return P;
        }();
        return Presets;
    }
};
```

- [ ] **Step 4: Run test to verify it passes**

Build and run the automation test `UnrealClaude.RuntimeProfiler.DeviceBudgetLookup`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UELLMToolkit/Private/DeviceBudgets.h Source/UELLMToolkit/Private/Tests/RuntimeProfilerTests.cpp
git commit -m "feat: add device budget presets for runtime profiler"
```

---

### Task 2: Profile Sample and Session Data Structures

**Files:**
- Create: `Source/UELLMToolkit/Private/RuntimeProfiler.h`
- Modify: `Source/UELLMToolkit/Private/Tests/RuntimeProfilerTests.cpp`

- [ ] **Step 1: Write failing test for profile sample and session**

Append to `RuntimeProfilerTests.cpp`:

```cpp
#include "RuntimeProfiler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FProfileSessionBasicTest,
    "UnrealClaude.RuntimeProfiler.ProfileSessionBasic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FProfileSessionBasicTest::RunTest(const FString& Parameters)
{
    FProfileSession Session;
    Session.TargetDevice = TEXT("quest_3_standalone");
    Session.LevelName = TEXT("/Game/Maps/TestLevel");

    // Add synthetic samples
    for (int32 i = 0; i < 10; ++i)
    {
        FProfileSample Sample;
        Sample.FrameTimeMs = 16.6f + (i == 5 ? 40.0f : 0.0f); // Spike at sample 5
        Sample.GameThreadMs = 8.0f;
        Sample.RenderThreadMs = 10.0f;
        Sample.GPUTimeMs = 12.0f + (i == 5 ? 30.0f : 0.0f);
        Sample.DrawCalls = 400 + (i == 5 ? 3000 : 0);
        Sample.TrianglesRendered = 500000 + (i == 5 ? 1000000 : 0);
        Sample.MemoryUsedMB = 3000.0f;
        Sample.TexturePoolUsedMB = 1500.0f;
        Sample.PlayerLocation = FVector(100.0f * i, 0.0f, 0.0f);
        Sample.CameraLocation = Sample.PlayerLocation + FVector(0, -200, 100);
        Sample.CameraRotation = FRotator(-15.0f, 0.0f, 0.0f);
        Sample.TimestampSeconds = i * 0.2;
        Session.Samples.Add(Sample);
    }

    TestEqual("Session has 10 samples", Session.Samples.Num(), 10);
    TestEqual("Spike sample frame time", Session.Samples[5].FrameTimeMs, 56.6f);
    TestEqual("Target device stored", Session.TargetDevice, TEXT("quest_3_standalone"));

    return true;
}
```

- [ ] **Step 2: Run test to verify it fails**

Build. Expected: compile error — `RuntimeProfiler.h` not found.

- [ ] **Step 3: Implement data structures in RuntimeProfiler.h**

```cpp
// Source/UELLMToolkit/Private/RuntimeProfiler.h

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "DeviceBudgets.h"
#include "Containers/Ticker.h"

class UWorld;

/**
 * A single profiling sample captured during PIE gameplay.
 */
struct FProfileSample
{
    // Frame timing
    float FrameTimeMs = 0.0f;
    float GameThreadMs = 0.0f;
    float RenderThreadMs = 0.0f;
    float GPUTimeMs = 0.0f;

    // Rendering
    int32 DrawCalls = 0;
    int32 TrianglesRendered = 0;

    // Memory
    float MemoryUsedMB = 0.0f;
    float TexturePoolUsedMB = 0.0f;

    // Spatial context
    FVector PlayerLocation = FVector::ZeroVector;
    FVector CameraLocation = FVector::ZeroVector;
    FRotator CameraRotation = FRotator::ZeroRotator;
    FMatrix ViewProjectionMatrix = FMatrix::Identity;
    FString SubLevelName;

    // Timing
    double TimestampSeconds = 0.0;
};

/**
 * Statistical summary for a single metric.
 */
struct FMetricSummary
{
    float Min = 0.0f;
    float Avg = 0.0f;
    float Max = 0.0f;
    float P95 = 0.0f;

    static FMetricSummary Compute(const TArray<float>& Values);
};

/**
 * An actor identified during spatial correlation of a spike.
 */
struct FSpikeActor
{
    FString ActorName;
    FString ActorClass;
    int32 TriangleCount = 0;
    int32 EstimatedDrawCalls = 0;
    int32 MaterialCount = 0;
    int32 LODCount = 0;
    bool bShadowCasting = false;
};

/**
 * A cluster of consecutive spike frames with spatial correlation data.
 */
struct FSpikeCluster
{
    int32 FrameStart = 0;
    int32 FrameEnd = 0;
    double TimeStartSeconds = 0.0;
    double TimeEndSeconds = 0.0;
    FVector Location = FVector::ZeroVector;
    float AvgFrameTimeMs = 0.0f;
    int32 TotalDrawCalls = 0;
    int32 TotalTrianglesInFrustum = 0;
    TArray<FSpikeActor> ActorsInFrustum;
    TArray<FSpikeActor> ShadowCastersBehindCamera;
    FString DeltaFromSmooth;
};

/**
 * Complete analysis result from a profiling session.
 */
struct FProfileAnalysis
{
    // Session metadata
    float DurationSeconds = 0.0f;
    int32 SampleCount = 0;
    FString LevelName;
    FString TargetDevice;

    // Summary stats
    FMetricSummary FPS;
    FMetricSummary FrameTimeMs;
    FMetricSummary DrawCallsSummary;
    FMetricSummary TrianglesSummary;
    FMetricSummary MemoryMBSummary;
    FString BottleneckThread;

    // Spikes
    TArray<FSpikeCluster> Spikes;

    // Recommendations
    TArray<FString> Recommendations;
    TArray<FString> DeviceWarnings;

    // Output
    FString OutputDir;
};

/**
 * A full profiling session — samples + metadata.
 */
struct FProfileSession
{
    FString TargetDevice;
    FString LevelName;
    float SpikeThreshold = 2.0f;
    TArray<FProfileSample> Samples;
    double StartTimeSeconds = 0.0;
    double EndTimeSeconds = 0.0;
};

/**
 * Runtime performance profiler.
 * Collects metrics during PIE via ticker, auto-stops on PIE end,
 * runs 3-pass analysis, and writes results to disk.
 */
class FRuntimeProfiler
{
public:
    FRuntimeProfiler();
    ~FRuntimeProfiler();

    /** Start profiling. Hooks ticker and PIE end delegate. */
    bool Start(float InIntervalMs, const FString& InTargetDevice, float InSpikeThreshold);

    /** Stop profiling manually (normally auto-stops on PIE end). */
    void Stop();

    /** Check if profiling is currently active. */
    bool IsActive() const { return bIsActive; }

    /** Get current session status. */
    int32 GetSampleCount() const;
    double GetElapsedSeconds() const;

    /** Get the last completed analysis. Returns nullptr if none available. */
    const FProfileAnalysis* GetLastAnalysis() const;

    // === Analysis passes (public for testability) ===

    /** Pass 1: Compute statistical summary from samples. */
    static void ComputeSummary(const FProfileSession& Session, FProfileAnalysis& OutAnalysis);

    /** Pass 2: Detect spikes and correlate with scene content. */
    static void DetectSpikes(const FProfileSession& Session, UWorld* World, FProfileAnalysis& OutAnalysis);

    /** Pass 3: Generate recommendations from analysis. */
    static void GenerateRecommendations(FProfileAnalysis& OutAnalysis, const FDeviceBudget& Budget);

    /** Write analysis results to disk as JSON. */
    static FString WriteResultsToDisk(const FProfileSession& Session, const FProfileAnalysis& Analysis);

    /** Convert analysis to JSON for MCP response. */
    static TSharedPtr<FJsonObject> AnalysisToJson(const FProfileAnalysis& Analysis, bool bFullDetail);

private:
    bool TickSample(float DeltaTime);
    void OnPIEEnded(bool bIsSimulating);
    void ProcessResults();
    FProfileSample CaptureSample(UWorld* PIEWorld) const;

    bool bIsActive = false;
    float IntervalSeconds = 0.2f;
    FTSTicker::FDelegateHandle TickerHandle;
    FDelegateHandle EndPIEHandle;
    double LastSampleTime = 0.0;

    FProfileSession CurrentSession;
    TSharedPtr<FProfileAnalysis> LastAnalysis;

    /** Cached PIE world — set on first successful sample. */
    TWeakObjectPtr<UWorld> CachedPIEWorld;
};
```

- [ ] **Step 4: Run test to verify it passes**

Build and run `UnrealClaude.RuntimeProfiler.ProfileSessionBasic`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UELLMToolkit/Private/RuntimeProfiler.h Source/UELLMToolkit/Private/Tests/RuntimeProfilerTests.cpp
git commit -m "feat: add profile sample and session data structures"
```

---

### Task 3: Statistical Summary (Analysis Pass 1)

**Files:**
- Create: `Source/UELLMToolkit/Private/RuntimeProfiler.cpp`
- Modify: `Source/UELLMToolkit/Private/Tests/RuntimeProfilerTests.cpp`

- [ ] **Step 1: Write failing test for statistical summary**

Append to `RuntimeProfilerTests.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FProfileSummaryTest,
    "UnrealClaude.RuntimeProfiler.SummaryComputation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FProfileSummaryTest::RunTest(const FString& Parameters)
{
    FProfileSession Session;
    Session.TargetDevice = TEXT("pc_high");
    Session.LevelName = TEXT("/Game/Maps/Test");

    // Create 20 samples with known values
    for (int32 i = 0; i < 20; ++i)
    {
        FProfileSample S;
        S.FrameTimeMs = 16.6f; // 60fps baseline
        S.GameThreadMs = 8.0f;
        S.RenderThreadMs = 10.0f;
        S.GPUTimeMs = 14.0f;   // GPU is highest = bottleneck
        S.DrawCalls = 500;
        S.TrianglesRendered = 400000;
        S.MemoryUsedMB = 3000.0f;
        S.TimestampSeconds = i * 0.2;
        Session.Samples.Add(S);
    }

    // Make one sample a spike
    Session.Samples[10].FrameTimeMs = 55.0f;
    Session.Samples[10].GPUTimeMs = 48.0f;

    FProfileAnalysis Analysis;
    FRuntimeProfiler::ComputeSummary(Session, Analysis);

    TestEqual("Sample count", Analysis.SampleCount, 20);
    TestTrue("Min frame time near 16.6", FMath::IsNearlyEqual(Analysis.FrameTimeMs.Min, 16.6f, 0.1f));
    TestTrue("Max frame time near 55.0", FMath::IsNearlyEqual(Analysis.FrameTimeMs.Max, 55.0f, 0.1f));
    TestEqual("Bottleneck is GPU", Analysis.BottleneckThread, TEXT("gpu"));

    // FPS should be inverse of frame time
    TestTrue("Max FPS near 60", Analysis.FPS.Max > 55.0f);
    TestTrue("Min FPS near 18", Analysis.FPS.Min < 20.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMetricSummaryComputeTest,
    "UnrealClaude.RuntimeProfiler.MetricSummaryCompute",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FMetricSummaryComputeTest::RunTest(const FString& Parameters)
{
    TArray<float> Values = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};
    FMetricSummary Summary = FMetricSummary::Compute(Values);

    TestEqual("Min", Summary.Min, 10.0f);
    TestEqual("Max", Summary.Max, 50.0f);
    TestTrue("Avg near 30", FMath::IsNearlyEqual(Summary.Avg, 30.0f, 0.1f));
    TestTrue("P95 >= 40", Summary.P95 >= 40.0f);

    // Empty array should return zeroes
    TArray<float> Empty;
    FMetricSummary EmptySummary = FMetricSummary::Compute(Empty);
    TestEqual("Empty min", EmptySummary.Min, 0.0f);

    return true;
}
```

- [ ] **Step 2: Run test to verify it fails**

Build. Expected: linker error — `FMetricSummary::Compute` and `FRuntimeProfiler::ComputeSummary` not defined.

- [ ] **Step 3: Implement RuntimeProfiler.cpp with MetricSummary::Compute and ComputeSummary**

```cpp
// Source/UELLMToolkit/Private/RuntimeProfiler.cpp

#include "RuntimeProfiler.h"
#include "SceneAnalyzer.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonValue.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/PlayerController.h"

// ============================================================================
// FMetricSummary
// ============================================================================

FMetricSummary FMetricSummary::Compute(const TArray<float>& Values)
{
    FMetricSummary Summary;
    if (Values.IsEmpty())
    {
        return Summary;
    }

    TArray<float> Sorted = Values;
    Sorted.Sort();

    Summary.Min = Sorted[0];
    Summary.Max = Sorted.Last();

    float Total = 0.0f;
    for (float V : Sorted)
    {
        Total += V;
    }
    Summary.Avg = Total / static_cast<float>(Sorted.Num());

    // P95: value at the 95th percentile index
    int32 P95Index = FMath::FloorToInt32(0.95f * (Sorted.Num() - 1));
    Summary.P95 = Sorted[P95Index];

    return Summary;
}

// ============================================================================
// FRuntimeProfiler — Constructor / Destructor
// ============================================================================

FRuntimeProfiler::FRuntimeProfiler()
{
}

FRuntimeProfiler::~FRuntimeProfiler()
{
    if (bIsActive)
    {
        Stop();
    }
}

// ============================================================================
// Pass 1 — Statistical Summary
// ============================================================================

void FRuntimeProfiler::ComputeSummary(const FProfileSession& Session, FProfileAnalysis& OutAnalysis)
{
    OutAnalysis.SampleCount = Session.Samples.Num();
    OutAnalysis.LevelName = Session.LevelName;
    OutAnalysis.TargetDevice = Session.TargetDevice;

    if (Session.Samples.IsEmpty())
    {
        return;
    }

    OutAnalysis.DurationSeconds = static_cast<float>(
        Session.Samples.Last().TimestampSeconds - Session.Samples[0].TimestampSeconds);

    // Collect per-metric arrays
    TArray<float> FrameTimes, GameThreadTimes, RenderThreadTimes, GPUTimes;
    TArray<float> FPSValues, DrawCallValues, TriangleValues, MemoryValues;

    FrameTimes.Reserve(Session.Samples.Num());
    for (const FProfileSample& S : Session.Samples)
    {
        FrameTimes.Add(S.FrameTimeMs);
        GameThreadTimes.Add(S.GameThreadMs);
        RenderThreadTimes.Add(S.RenderThreadMs);
        GPUTimes.Add(S.GPUTimeMs);
        FPSValues.Add(S.FrameTimeMs > 0.0f ? 1000.0f / S.FrameTimeMs : 0.0f);
        DrawCallValues.Add(static_cast<float>(S.DrawCalls));
        TriangleValues.Add(static_cast<float>(S.TrianglesRendered));
        MemoryValues.Add(S.MemoryUsedMB);
    }

    OutAnalysis.FrameTimeMs = FMetricSummary::Compute(FrameTimes);
    OutAnalysis.FPS = FMetricSummary::Compute(FPSValues);
    OutAnalysis.DrawCallsSummary = FMetricSummary::Compute(DrawCallValues);
    OutAnalysis.TrianglesSummary = FMetricSummary::Compute(TriangleValues);
    OutAnalysis.MemoryMBSummary = FMetricSummary::Compute(MemoryValues);

    // Determine bottleneck thread: which has highest average time
    FMetricSummary GTSummary = FMetricSummary::Compute(GameThreadTimes);
    FMetricSummary RTSummary = FMetricSummary::Compute(RenderThreadTimes);
    FMetricSummary GPUSummary = FMetricSummary::Compute(GPUTimes);

    if (GPUSummary.Avg >= RTSummary.Avg && GPUSummary.Avg >= GTSummary.Avg)
    {
        OutAnalysis.BottleneckThread = TEXT("gpu");
    }
    else if (RTSummary.Avg >= GTSummary.Avg)
    {
        OutAnalysis.BottleneckThread = TEXT("render_thread");
    }
    else
    {
        OutAnalysis.BottleneckThread = TEXT("game_thread");
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Build and run `UnrealClaude.RuntimeProfiler.SummaryComputation` and `UnrealClaude.RuntimeProfiler.MetricSummaryCompute`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UELLMToolkit/Private/RuntimeProfiler.cpp Source/UELLMToolkit/Private/Tests/RuntimeProfilerTests.cpp
git commit -m "feat: implement statistical summary (analysis pass 1)"
```

---

### Task 4: Spike Detection and Spatial Correlation (Analysis Pass 2)

**Files:**
- Modify: `Source/UELLMToolkit/Private/RuntimeProfiler.cpp`
- Modify: `Source/UELLMToolkit/Private/Tests/RuntimeProfilerTests.cpp`

- [ ] **Step 1: Write failing test for spike detection**

Append to `RuntimeProfilerTests.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpikeDetectionTest,
    "UnrealClaude.RuntimeProfiler.SpikeDetection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FSpikeDetectionTest::RunTest(const FString& Parameters)
{
    FProfileSession Session;
    Session.TargetDevice = TEXT("pc_high");
    Session.SpikeThreshold = 2.0f;

    // 20 normal samples at ~16.6ms, then 3 consecutive spike samples, then 7 normal
    for (int32 i = 0; i < 30; ++i)
    {
        FProfileSample S;
        S.TimestampSeconds = i * 0.2;
        S.PlayerLocation = FVector(100.0f * i, 0.0f, 0.0f);
        S.CameraLocation = S.PlayerLocation;
        S.CameraRotation = FRotator::ZeroRotator;

        if (i >= 10 && i <= 12) // Spike cluster at samples 10-12
        {
            S.FrameTimeMs = 50.0f;
            S.GPUTimeMs = 40.0f;
            S.DrawCalls = 4000;
            S.TrianglesRendered = 2000000;
        }
        else
        {
            S.FrameTimeMs = 16.6f;
            S.GPUTimeMs = 12.0f;
            S.DrawCalls = 400;
            S.TrianglesRendered = 400000;
        }
        S.GameThreadMs = 8.0f;
        S.RenderThreadMs = 10.0f;
        S.MemoryUsedMB = 3000.0f;
        Session.Samples.Add(S);
    }

    // Compute summary first (needed for spike threshold calculation)
    FProfileAnalysis Analysis;
    FRuntimeProfiler::ComputeSummary(Session, Analysis);

    // Pass nullptr for World — skip frustum queries in unit test
    FRuntimeProfiler::DetectSpikes(Session, nullptr, Analysis);

    TestTrue("At least 1 spike cluster detected", Analysis.Spikes.Num() >= 1);
    if (Analysis.Spikes.Num() > 0)
    {
        const FSpikeCluster& Spike = Analysis.Spikes[0];
        TestEqual("Spike starts at frame 10", Spike.FrameStart, 10);
        TestEqual("Spike ends at frame 12", Spike.FrameEnd, 12);
        TestTrue("Spike avg frame time > 40ms", Spike.AvgFrameTimeMs > 40.0f);
        TestTrue("Spike location near sample 10 position",
            FVector::Dist(Spike.Location, FVector(1000.0f, 0.0f, 0.0f)) < 200.0f);
    }

    return true;
}
```

- [ ] **Step 2: Run test to verify it fails**

Build. Expected: linker error — `FRuntimeProfiler::DetectSpikes` not defined.

- [ ] **Step 3: Implement DetectSpikes in RuntimeProfiler.cpp**

Add to `RuntimeProfiler.cpp` after ComputeSummary:

```cpp
// ============================================================================
// Pass 2 — Spike Detection & Spatial Correlation
// ============================================================================

void FRuntimeProfiler::DetectSpikes(const FProfileSession& Session, UWorld* World, FProfileAnalysis& OutAnalysis)
{
    if (Session.Samples.IsEmpty() || OutAnalysis.FrameTimeMs.Avg <= 0.0f)
    {
        return;
    }

    const float SpikeThresholdMs = OutAnalysis.FrameTimeMs.Avg * Session.SpikeThreshold;

    // Identify spike frames
    TArray<bool> IsSpike;
    IsSpike.SetNum(Session.Samples.Num());
    for (int32 i = 0; i < Session.Samples.Num(); ++i)
    {
        IsSpike[i] = Session.Samples[i].FrameTimeMs > SpikeThresholdMs;
    }

    // Group consecutive spikes into clusters
    int32 ClusterStart = -1;
    for (int32 i = 0; i <= Session.Samples.Num(); ++i)
    {
        bool bCurrentIsSpike = (i < Session.Samples.Num()) && IsSpike[i];

        if (bCurrentIsSpike && ClusterStart < 0)
        {
            ClusterStart = i;
        }
        else if (!bCurrentIsSpike && ClusterStart >= 0)
        {
            // End of cluster — build FSpikeCluster
            FSpikeCluster Cluster;
            Cluster.FrameStart = ClusterStart;
            Cluster.FrameEnd = i - 1;
            Cluster.TimeStartSeconds = Session.Samples[ClusterStart].TimestampSeconds;
            Cluster.TimeEndSeconds = Session.Samples[i - 1].TimestampSeconds;

            // Compute averages and find representative position (midpoint of cluster)
            float TotalFrameTime = 0.0f;
            int32 TotalDrawCalls = 0;
            int32 MidIndex = ClusterStart + (i - 1 - ClusterStart) / 2;
            for (int32 j = ClusterStart; j < i; ++j)
            {
                TotalFrameTime += Session.Samples[j].FrameTimeMs;
                TotalDrawCalls += Session.Samples[j].DrawCalls;
            }
            int32 ClusterSize = i - ClusterStart;
            Cluster.AvgFrameTimeMs = TotalFrameTime / static_cast<float>(ClusterSize);
            Cluster.TotalDrawCalls = Session.Samples[MidIndex].DrawCalls;
            Cluster.TotalTrianglesInFrustum = Session.Samples[MidIndex].TrianglesRendered;
            Cluster.Location = Session.Samples[MidIndex].PlayerLocation;

            // Spatial correlation — only if we have a valid world
            if (World)
            {
                CorrelateWithScene(Session.Samples[MidIndex], World, Cluster);

                // Delta analysis: compare with previous smooth period
                if (ClusterStart > 0)
                {
                    const FProfileSample& SmoothSample = Session.Samples[FMath::Max(0, ClusterStart - 1)];
                    int32 DrawCallDelta = Session.Samples[MidIndex].DrawCalls - SmoothSample.DrawCalls;
                    int32 TriDelta = Session.Samples[MidIndex].TrianglesRendered - SmoothSample.TrianglesRendered;
                    Cluster.DeltaFromSmooth = FString::Printf(
                        TEXT("%+d draw calls, %+d triangles vs previous smooth frame"),
                        DrawCallDelta, TriDelta);
                }
            }

            OutAnalysis.Spikes.Add(MoveTemp(Cluster));
            ClusterStart = -1;
        }
    }
}
```

- [ ] **Step 4: Add the CorrelateWithScene helper**

This is the frustum-based spatial query. Add to `RuntimeProfiler.h` as a private static method declaration:

```cpp
    /** Correlate a spike sample with visible scene content via frustum query. */
    static void CorrelateWithScene(const FProfileSample& Sample, UWorld* World, FSpikeCluster& OutCluster);
```

Add implementation to `RuntimeProfiler.cpp`:

```cpp
// ============================================================================
// Frustum-Based Spatial Correlation
// ============================================================================

void FRuntimeProfiler::CorrelateWithScene(const FProfileSample& Sample, UWorld* World, FSpikeCluster& OutCluster)
{
    if (!World)
    {
        return;
    }

    // Build a view frustum from the camera transform
    // Use a standard perspective projection (90 FOV, 16:9 aspect)
    const float FOV = 90.0f;
    const float AspectRatio = 16.0f / 9.0f;
    const float NearClip = 10.0f;
    const float FarClip = 50000.0f;

    FMatrix ViewMatrix = FLookAtMatrix(Sample.CameraLocation,
        Sample.CameraLocation + Sample.CameraRotation.Vector(),
        FVector::UpVector);

    FMatrix ProjectionMatrix = FReversedZPerspectiveMatrix(
        FMath::DegreesToRadians(FOV * 0.5f), AspectRatio, 1.0f, NearClip);

    FConvexVolume ViewFrustum;
    GetViewFrustumBounds(ViewFrustum, ViewMatrix * ProjectionMatrix, true);

    int32 TotalTrisInFrustum = 0;

    // Query all actors with primitive components
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor || Actor->IsPendingKillPending())
        {
            continue;
        }

        // Check if any primitive component is in the frustum
        bool bInFrustum = false;
        TArray<UPrimitiveComponent*> PrimitiveComps;
        Actor->GetComponents<UPrimitiveComponent>(PrimitiveComps);
        for (UPrimitiveComponent* Prim : PrimitiveComps)
        {
            if (Prim && ViewFrustum.IntersectBox(Prim->Bounds.Origin, Prim->Bounds.BoxExtent))
            {
                bInFrustum = true;
                break;
            }
        }

        if (!bInFrustum)
        {
            // Check if it's a shadow caster behind the camera but within shadow distance
            bool bIsShadowCaster = false;
            for (UPrimitiveComponent* Prim : PrimitiveComps)
            {
                if (Prim && Prim->CastShadow)
                {
                    float Distance = FVector::Dist(Actor->GetActorLocation(), Sample.CameraLocation);
                    if (Distance < 5000.0f) // Reasonable shadow distance
                    {
                        bIsShadowCaster = true;
                        break;
                    }
                }
            }

            if (!bIsShadowCaster)
            {
                continue;
            }

            // Gather stats for shadow caster
            FActorMeshStats MeshStats;
            if (FSceneAnalyzer::GatherActorMeshStats(Actor, MeshStats))
            {
                FSpikeActor SpikeActor;
                SpikeActor.ActorName = MeshStats.ActorLabel;
                SpikeActor.ActorClass = MeshStats.ActorClass;
                SpikeActor.TriangleCount = MeshStats.TriangleCount;
                SpikeActor.EstimatedDrawCalls = MeshStats.EstimatedDrawCalls;
                SpikeActor.MaterialCount = MeshStats.MaterialCount;
                SpikeActor.LODCount = MeshStats.LODCount;
                SpikeActor.bShadowCasting = true;
                OutCluster.ShadowCastersBehindCamera.Add(MoveTemp(SpikeActor));
            }
            continue;
        }

        // In frustum — gather mesh stats
        FActorMeshStats MeshStats;
        if (FSceneAnalyzer::GatherActorMeshStats(Actor, MeshStats))
        {
            FSpikeActor SpikeActor;
            SpikeActor.ActorName = MeshStats.ActorLabel;
            SpikeActor.ActorClass = MeshStats.ActorClass;
            SpikeActor.TriangleCount = MeshStats.TriangleCount;
            SpikeActor.EstimatedDrawCalls = MeshStats.EstimatedDrawCalls;
            SpikeActor.MaterialCount = MeshStats.MaterialCount;
            SpikeActor.LODCount = MeshStats.LODCount;
            SpikeActor.bShadowCasting = MeshStats.bShadowCasting;
            TotalTrisInFrustum += MeshStats.TriangleCount;
            OutCluster.ActorsInFrustum.Add(MoveTemp(SpikeActor));
        }
    }

    OutCluster.TotalTrianglesInFrustum = TotalTrisInFrustum;

    // Sort actors by triangle count descending (most expensive first)
    OutCluster.ActorsInFrustum.Sort([](const FSpikeActor& A, const FSpikeActor& B)
    {
        return A.TriangleCount > B.TriangleCount;
    });

    // Cap at top 20 actors to keep results manageable
    if (OutCluster.ActorsInFrustum.Num() > 20)
    {
        OutCluster.ActorsInFrustum.SetNum(20);
    }
}
```

- [ ] **Step 5: Run tests to verify they pass**

Build and run `UnrealClaude.RuntimeProfiler.SpikeDetection`. Expected: PASS (spatial correlation skipped because World is nullptr in unit test, but spike grouping logic is verified).

- [ ] **Step 6: Commit**

```bash
git add Source/UELLMToolkit/Private/RuntimeProfiler.h Source/UELLMToolkit/Private/RuntimeProfiler.cpp Source/UELLMToolkit/Private/Tests/RuntimeProfilerTests.cpp
git commit -m "feat: implement spike detection and spatial correlation (analysis pass 2)"
```

---

### Task 5: Recommendation Generation (Analysis Pass 3)

**Files:**
- Modify: `Source/UELLMToolkit/Private/RuntimeProfiler.cpp`
- Modify: `Source/UELLMToolkit/Private/Tests/RuntimeProfilerTests.cpp`

- [ ] **Step 1: Write failing test for recommendation generation**

Append to `RuntimeProfilerTests.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRecommendationTest,
    "UnrealClaude.RuntimeProfiler.RecommendationGeneration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FRecommendationTest::RunTest(const FString& Parameters)
{
    FProfileAnalysis Analysis;
    Analysis.TargetDevice = TEXT("quest_3_standalone");
    Analysis.FrameTimeMs = {16.6f, 25.0f, 55.0f, 42.0f};
    Analysis.FPS = {18.0f, 42.0f, 60.0f, 24.0f};
    Analysis.DrawCallsSummary = {180.0f, 800.0f, 3800.0f, 1200.0f};
    Analysis.TrianglesSummary = {120000.0f, 900000.0f, 1800000.0f, 1200000.0f};
    Analysis.MemoryMBSummary = {2800.0f, 3100.0f, 3400.0f, 3200.0f};
    Analysis.BottleneckThread = TEXT("gpu");

    // Add a spike with actors
    FSpikeCluster Spike;
    Spike.FrameStart = 10;
    Spike.FrameEnd = 15;
    Spike.Location = FVector(1200, -500, 100);
    Spike.AvgFrameTimeMs = 50.0f;
    Spike.TotalDrawCalls = 3800;
    Spike.TotalTrianglesInFrustum = 1500000;

    FSpikeActor HighPolyActor;
    HighPolyActor.ActorName = TEXT("Rock_Cliff_01");
    HighPolyActor.TriangleCount = 800000;
    HighPolyActor.LODCount = 1;
    HighPolyActor.EstimatedDrawCalls = 24;
    HighPolyActor.MaterialCount = 8;
    HighPolyActor.bShadowCasting = true;
    Spike.ActorsInFrustum.Add(HighPolyActor);

    Analysis.Spikes.Add(Spike);

    FDeviceBudget Quest3Budget = FDeviceBudget::Get(TEXT("quest_3_standalone"));
    FRuntimeProfiler::GenerateRecommendations(Analysis, Quest3Budget);

    // Should have recommendations about the spike
    TestTrue("Has recommendations", Analysis.Recommendations.Num() > 0);

    // Should have device warnings since Quest 3 budget is 200 draw calls and we peak at 3800
    TestTrue("Has device warnings", Analysis.DeviceWarnings.Num() > 0);

    // Check that recommendations reference specific data
    bool bFoundLocationRef = false;
    bool bFoundActorRef = false;
    for (const FString& Rec : Analysis.Recommendations)
    {
        if (Rec.Contains(TEXT("1200"))) bFoundLocationRef = true;
        if (Rec.Contains(TEXT("Rock_Cliff_01"))) bFoundActorRef = true;
    }
    TestTrue("Recommendation references spike location", bFoundLocationRef);
    TestTrue("Recommendation references actor name", bFoundActorRef);

    return true;
}
```

- [ ] **Step 2: Run test to verify it fails**

Build. Expected: linker error — `FRuntimeProfiler::GenerateRecommendations` not defined.

- [ ] **Step 3: Implement GenerateRecommendations**

Add to `RuntimeProfiler.cpp`:

```cpp
// ============================================================================
// Pass 3 — Recommendation Generation
// ============================================================================

void FRuntimeProfiler::GenerateRecommendations(FProfileAnalysis& OutAnalysis, const FDeviceBudget& Budget)
{
    // === Per-spike recommendations ===
    for (const FSpikeCluster& Spike : OutAnalysis.Spikes)
    {
        FString LocationStr = FString::Printf(TEXT("(%.0f, %.0f, %.0f)"),
            Spike.Location.X, Spike.Location.Y, Spike.Location.Z);

        float SpikeFPS = Spike.AvgFrameTimeMs > 0.0f ? 1000.0f / Spike.AvgFrameTimeMs : 0.0f;

        // High-poly actors with no LODs
        TArray<FString> NoLODActors;
        for (const FSpikeActor& Actor : Spike.ActorsInFrustum)
        {
            if (Actor.TriangleCount > 100000 && Actor.LODCount <= 1)
            {
                NoLODActors.Add(FString::Printf(TEXT("%s (%dK tris)"),
                    *Actor.ActorName, Actor.TriangleCount / 1000));
            }
        }
        if (NoLODActors.Num() > 0)
        {
            OutAnalysis.Recommendations.Add(FString::Printf(
                TEXT("FPS dropped to %.0f near %s — %d actor(s) have high poly counts with no LODs: %s. Add LOD chains."),
                SpikeFPS, *LocationStr, NoLODActors.Num(), *FString::Join(NoLODActors, TEXT(", "))));
        }

        // High draw call count
        if (Spike.TotalDrawCalls > 2000)
        {
            // Count actors with many unique materials
            int32 HighMatActors = 0;
            for (const FSpikeActor& Actor : Spike.ActorsInFrustum)
            {
                if (Actor.MaterialCount > 4)
                {
                    HighMatActors++;
                }
            }
            if (HighMatActors > 0)
            {
                OutAnalysis.Recommendations.Add(FString::Printf(
                    TEXT("Draw calls peak at %d near %s. %d actor(s) use many unique materials — consider material instancing or merging."),
                    Spike.TotalDrawCalls, *LocationStr, HighMatActors));
            }
        }

        // General spike with no specific cause identified
        if (NoLODActors.Num() == 0 && Spike.TotalDrawCalls <= 2000)
        {
            OutAnalysis.Recommendations.Add(FString::Printf(
                TEXT("FPS dropped to %.0f near %s (avg frame time %.1fms). %s is the bottleneck. Investigate actors in this area."),
                SpikeFPS, *LocationStr, Spike.AvgFrameTimeMs, *OutAnalysis.BottleneckThread));
        }
    }

    // === Overall bottleneck recommendation ===
    if (OutAnalysis.BottleneckThread == TEXT("gpu"))
    {
        OutAnalysis.Recommendations.Add(TEXT("GPU is the primary bottleneck across the session. Focus on reducing triangle counts, draw calls, and shader complexity."));
    }
    else if (OutAnalysis.BottleneckThread == TEXT("render_thread"))
    {
        OutAnalysis.Recommendations.Add(TEXT("Render thread is the primary bottleneck. Reduce draw calls via mesh merging, instancing, or LOD aggressiveness."));
    }
    else if (OutAnalysis.BottleneckThread == TEXT("game_thread"))
    {
        OutAnalysis.Recommendations.Add(TEXT("Game thread is the primary bottleneck. Check tick-heavy actors, complex collision, and Blueprint complexity."));
    }

    // === Device-specific warnings ===
    if (Budget.bValid)
    {
        if (OutAnalysis.DrawCallsSummary.Avg > static_cast<float>(Budget.MaxDrawCalls))
        {
            OutAnalysis.DeviceWarnings.Add(FString::Printf(
                TEXT("Average draw calls (%.0f) exceed %s budget (%d)."),
                OutAnalysis.DrawCallsSummary.Avg, *Budget.DisplayName, Budget.MaxDrawCalls));
        }
        else if (OutAnalysis.DrawCallsSummary.P95 > static_cast<float>(Budget.MaxDrawCalls))
        {
            // Calculate percentage of frames over budget
            OutAnalysis.DeviceWarnings.Add(FString::Printf(
                TEXT("Draw calls exceed %s budget (%d) at P95 (%.0f)."),
                *Budget.DisplayName, Budget.MaxDrawCalls, OutAnalysis.DrawCallsSummary.P95));
        }

        if (OutAnalysis.TrianglesSummary.Avg > static_cast<float>(Budget.TriangleBudget))
        {
            OutAnalysis.DeviceWarnings.Add(FString::Printf(
                TEXT("Average triangle count (%.0f) exceeds %s budget (%d)."),
                OutAnalysis.TrianglesSummary.Avg, *Budget.DisplayName, Budget.TriangleBudget));
        }

        if (OutAnalysis.MemoryMBSummary.Max > static_cast<float>(Budget.TextureMemoryMB))
        {
            OutAnalysis.DeviceWarnings.Add(FString::Printf(
                TEXT("Peak memory (%.0f MB) exceeds %s texture memory budget (%d MB)."),
                OutAnalysis.MemoryMBSummary.Max, *Budget.DisplayName, Budget.TextureMemoryMB));
        }

        if (!Budget.bSupportsDynamicShadows)
        {
            // Check if any spike actors have shadow casting enabled
            for (const FSpikeCluster& Spike : OutAnalysis.Spikes)
            {
                for (const FSpikeActor& Actor : Spike.ActorsInFrustum)
                {
                    if (Actor.bShadowCasting)
                    {
                        OutAnalysis.DeviceWarnings.Add(FString::Printf(
                            TEXT("Dynamic shadows detected — %s has limited or no dynamic shadow support. Consider baked lighting."),
                            *Budget.DisplayName));
                        goto DoneShadowCheck; // Only need one warning
                    }
                }
            }
            DoneShadowCheck:;
        }
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Build and run `UnrealClaude.RuntimeProfiler.RecommendationGeneration`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UELLMToolkit/Private/RuntimeProfiler.cpp Source/UELLMToolkit/Private/Tests/RuntimeProfilerTests.cpp
git commit -m "feat: implement recommendation generation (analysis pass 3)"
```

---

### Task 6: Profiler Lifecycle (Start/Stop/Sample)

**Files:**
- Modify: `Source/UELLMToolkit/Private/RuntimeProfiler.cpp`

- [ ] **Step 1: Implement Start/Stop/CaptureSample/Tick/OnPIEEnded**

Add to `RuntimeProfiler.cpp`:

```cpp
// ============================================================================
// Lifecycle — Start / Stop / Tick
// ============================================================================

bool FRuntimeProfiler::Start(float InIntervalMs, const FString& InTargetDevice, float InSpikeThreshold)
{
    if (bIsActive)
    {
        return false; // Already profiling
    }

    // Validate device
    FDeviceBudget Budget = FDeviceBudget::Get(InTargetDevice);
    // Allow unknown devices — just won't have device warnings

    CurrentSession = FProfileSession();
    CurrentSession.TargetDevice = InTargetDevice;
    CurrentSession.SpikeThreshold = InSpikeThreshold;
    IntervalSeconds = FMath::Max(InIntervalMs / 1000.0f, 0.05f); // Minimum 50ms

    // Try to find PIE world
    UWorld* PIEWorld = nullptr;
    if (GEditor && GEditor->IsPlaySessionInProgress())
    {
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.WorldType == EWorldType::PIE && Context.World())
            {
                PIEWorld = Context.World();
                break;
            }
        }
    }

    if (PIEWorld)
    {
        CachedPIEWorld = PIEWorld;
        CurrentSession.LevelName = PIEWorld->GetMapName();
    }

    // Hook PIE end
    EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FRuntimeProfiler::OnPIEEnded);

    // Hook ticker
    CurrentSession.StartTimeSeconds = FPlatformTime::Seconds();
    LastSampleTime = 0.0;
    bIsActive = true;

    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FRuntimeProfiler::TickSample),
        IntervalSeconds);

    UE_LOG(LogTemp, Log, TEXT("[RuntimeProfiler] Started — interval=%.0fms device=%s threshold=%.1fx"),
        InIntervalMs, *InTargetDevice, InSpikeThreshold);

    return true;
}

void FRuntimeProfiler::Stop()
{
    if (!bIsActive)
    {
        return;
    }

    bIsActive = false;
    CurrentSession.EndTimeSeconds = FPlatformTime::Seconds();

    // Unhook ticker
    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }

    // Unhook PIE delegate
    if (EndPIEHandle.IsValid())
    {
        FEditorDelegates::EndPIE.Remove(EndPIEHandle);
        EndPIEHandle.Reset();
    }

    UE_LOG(LogTemp, Log, TEXT("[RuntimeProfiler] Stopped — %d samples collected over %.1fs"),
        CurrentSession.Samples.Num(),
        CurrentSession.EndTimeSeconds - CurrentSession.StartTimeSeconds);

    ProcessResults();
}

int32 FRuntimeProfiler::GetSampleCount() const
{
    return CurrentSession.Samples.Num();
}

double FRuntimeProfiler::GetElapsedSeconds() const
{
    if (!bIsActive)
    {
        return CurrentSession.EndTimeSeconds - CurrentSession.StartTimeSeconds;
    }
    return FPlatformTime::Seconds() - CurrentSession.StartTimeSeconds;
}

const FProfileAnalysis* FRuntimeProfiler::GetLastAnalysis() const
{
    return LastAnalysis.IsValid() ? LastAnalysis.Get() : nullptr;
}

// ============================================================================
// Tick — Sample Capture
// ============================================================================

bool FRuntimeProfiler::TickSample(float DeltaTime)
{
    if (!bIsActive)
    {
        return false; // Remove ticker
    }

    // Find PIE world if not cached
    UWorld* PIEWorld = CachedPIEWorld.Get();
    if (!PIEWorld)
    {
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.WorldType == EWorldType::PIE && Context.World())
            {
                PIEWorld = Context.World();
                CachedPIEWorld = PIEWorld;
                CurrentSession.LevelName = PIEWorld->GetMapName();
                break;
            }
        }
    }

    if (!PIEWorld)
    {
        return true; // Keep ticking, PIE might start soon
    }

    FProfileSample Sample = CaptureSample(PIEWorld);
    Sample.TimestampSeconds = FPlatformTime::Seconds() - CurrentSession.StartTimeSeconds;
    CurrentSession.Samples.Add(MoveTemp(Sample));

    return true; // Keep ticking
}

FProfileSample FRuntimeProfiler::CaptureSample(UWorld* PIEWorld) const
{
    FProfileSample Sample;

    // Frame timing via FStatUnitData
    UGameViewportClient* Viewport = PIEWorld->GetGameViewport();
    if (Viewport)
    {
        const FStatUnitData* StatData = Viewport->GetStatUnitData();
        if (StatData)
        {
            Sample.FrameTimeMs = StatData->RawFrameTime;
            Sample.GameThreadMs = StatData->RawGameThreadTime;
            Sample.RenderThreadMs = StatData->RawRenderThreadTime;
            Sample.GPUTimeMs = StatData->RawGPUFrameTime[0];
        }
    }

    // Rendering stats — draw calls and triangles from viewport stats
    // These are available via the RHI stats when stat commands are enabled
    #if STATS
    if (FGameThreadStatsData* StatsData = FLatestGameThreadStatsData::Get().Latest)
    {
        // Try to read common stat group values
        // Fallback: use GNumDrawCallsRHI if available
    }
    #endif

    // Use globally accessible RHI counters as primary source
    extern ENGINE_API int32 GNumDrawCallsRHI;
    Sample.DrawCalls = GNumDrawCallsRHI;

    // Player and camera position
    APlayerController* PC = PIEWorld->GetFirstPlayerController();
    if (PC)
    {
        if (APawn* Pawn = PC->GetPawn())
        {
            Sample.PlayerLocation = Pawn->GetActorLocation();
        }

        FVector ViewLoc;
        FRotator ViewRot;
        PC->GetPlayerViewPoint(ViewLoc, ViewRot);
        Sample.CameraLocation = ViewLoc;
        Sample.CameraRotation = ViewRot;
    }

    // Sublevel tracking
    if (PIEWorld->GetCurrentLevel())
    {
        Sample.SubLevelName = PIEWorld->GetCurrentLevel()->GetOuter()->GetName();
    }

    return Sample;
}

// ============================================================================
// PIE End Callback
// ============================================================================

void FRuntimeProfiler::OnPIEEnded(bool bIsSimulating)
{
    if (bIsActive)
    {
        Stop();
    }
}

// ============================================================================
// Process Results
// ============================================================================

void FRuntimeProfiler::ProcessResults()
{
    if (CurrentSession.Samples.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[RuntimeProfiler] No samples collected — skipping analysis."));
        return;
    }

    LastAnalysis = MakeShared<FProfileAnalysis>();

    // Pass 1: Summary
    ComputeSummary(CurrentSession, *LastAnalysis);

    // Pass 2: Spike detection with spatial correlation
    UWorld* EditorWorld = nullptr;
    if (GEditor)
    {
        EditorWorld = GEditor->GetEditorWorldContext().World();
    }
    DetectSpikes(CurrentSession, EditorWorld, *LastAnalysis);

    // Pass 3: Recommendations
    FDeviceBudget Budget = FDeviceBudget::Get(CurrentSession.TargetDevice);
    GenerateRecommendations(*LastAnalysis, Budget);

    // Write to disk
    LastAnalysis->OutputDir = WriteResultsToDisk(CurrentSession, *LastAnalysis);

    UE_LOG(LogTemp, Log, TEXT("[RuntimeProfiler] Analysis complete — %d spikes, %d recommendations, output: %s"),
        LastAnalysis->Spikes.Num(), LastAnalysis->Recommendations.Num(), *LastAnalysis->OutputDir);
}
```

- [ ] **Step 2: Verify the code compiles**

Build the plugin. Expected: PASS (possibly with a warning about `GNumDrawCallsRHI` extern — if so, wrap in `#if` guard or use alternative stat access).

- [ ] **Step 3: Commit**

```bash
git add Source/UELLMToolkit/Private/RuntimeProfiler.cpp
git commit -m "feat: implement profiler lifecycle (start/stop/sample/PIE hooks)"
```

---

### Task 7: JSON Serialization and Disk Output

**Files:**
- Modify: `Source/UELLMToolkit/Private/RuntimeProfiler.cpp`
- Modify: `Source/UELLMToolkit/Private/Tests/RuntimeProfilerTests.cpp`

- [ ] **Step 1: Write failing test for JSON output**

Append to `RuntimeProfilerTests.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAnalysisJsonTest,
    "UnrealClaude.RuntimeProfiler.AnalysisToJson",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FAnalysisJsonTest::RunTest(const FString& Parameters)
{
    FProfileAnalysis Analysis;
    Analysis.SampleCount = 100;
    Analysis.DurationSeconds = 20.0f;
    Analysis.LevelName = TEXT("/Game/Maps/Village");
    Analysis.TargetDevice = TEXT("quest_3_standalone");
    Analysis.BottleneckThread = TEXT("gpu");
    Analysis.FPS = {18.0f, 52.0f, 60.0f, 24.0f};
    Analysis.FrameTimeMs = {16.6f, 19.2f, 55.5f, 41.6f};
    Analysis.DrawCallsSummary = {180.0f, 420.0f, 3800.0f, 1200.0f};
    Analysis.TrianglesSummary = {120000.0f, 450000.0f, 1800000.0f, 1200000.0f};
    Analysis.MemoryMBSummary = {2800.0f, 3100.0f, 3400.0f, 3200.0f};
    Analysis.Recommendations.Add(TEXT("Test recommendation"));
    Analysis.DeviceWarnings.Add(TEXT("Test device warning"));
    Analysis.OutputDir = TEXT("D:/test/output/");

    // Summary mode
    TSharedPtr<FJsonObject> SummaryJson = FRuntimeProfiler::AnalysisToJson(Analysis, false);
    TestTrue("Has session object", SummaryJson->HasField(TEXT("session")));
    TestTrue("Has summary object", SummaryJson->HasField(TEXT("summary")));
    TestTrue("Has recommendations", SummaryJson->HasField(TEXT("recommendations")));
    TestTrue("Has device_warnings", SummaryJson->HasField(TEXT("device_warnings")));
    TestFalse("Summary mode has no spikes array", SummaryJson->HasField(TEXT("spikes")));

    // Full mode
    FSpikeCluster Spike;
    Spike.FrameStart = 10;
    Spike.FrameEnd = 15;
    Spike.Location = FVector(100, 200, 300);
    Spike.AvgFrameTimeMs = 50.0f;
    Spike.TotalDrawCalls = 3000;
    Analysis.Spikes.Add(Spike);

    TSharedPtr<FJsonObject> FullJson = FRuntimeProfiler::AnalysisToJson(Analysis, true);
    TestTrue("Full mode has spikes array", FullJson->HasField(TEXT("spikes")));

    // Verify session fields
    TSharedPtr<FJsonObject> SessionObj = SummaryJson->GetObjectField(TEXT("session"));
    TestTrue("Session has duration", SessionObj->HasField(TEXT("duration_s")));
    TestTrue("Session has target_device", SessionObj->HasField(TEXT("target_device")));

    return true;
}
```

- [ ] **Step 2: Run test to verify it fails**

Build. Expected: linker error — `FRuntimeProfiler::AnalysisToJson` not defined.

- [ ] **Step 3: Implement AnalysisToJson and WriteResultsToDisk**

Add to `RuntimeProfiler.cpp`:

```cpp
// ============================================================================
// JSON Serialization
// ============================================================================

static TSharedPtr<FJsonObject> MetricSummaryToJson(const FMetricSummary& Summary)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetNumberField(TEXT("min"), Summary.Min);
    Obj->SetNumberField(TEXT("avg"), Summary.Avg);
    Obj->SetNumberField(TEXT("max"), Summary.Max);
    Obj->SetNumberField(TEXT("p95"), Summary.P95);
    return Obj;
}

static TSharedPtr<FJsonObject> SpikeActorToJson(const FSpikeActor& Actor)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("name"), Actor.ActorName);
    Obj->SetStringField(TEXT("class"), Actor.ActorClass);
    Obj->SetNumberField(TEXT("triangles"), Actor.TriangleCount);
    Obj->SetNumberField(TEXT("draw_calls"), Actor.EstimatedDrawCalls);
    Obj->SetNumberField(TEXT("materials"), Actor.MaterialCount);
    Obj->SetNumberField(TEXT("lods"), Actor.LODCount);
    Obj->SetBoolField(TEXT("shadow_casting"), Actor.bShadowCasting);
    return Obj;
}

TSharedPtr<FJsonObject> FRuntimeProfiler::AnalysisToJson(const FProfileAnalysis& Analysis, bool bFullDetail)
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

    // Session metadata
    TSharedPtr<FJsonObject> SessionObj = MakeShared<FJsonObject>();
    SessionObj->SetNumberField(TEXT("duration_s"), Analysis.DurationSeconds);
    SessionObj->SetNumberField(TEXT("sample_count"), Analysis.SampleCount);
    SessionObj->SetStringField(TEXT("level"), Analysis.LevelName);
    SessionObj->SetStringField(TEXT("target_device"), Analysis.TargetDevice);
    Root->SetObjectField(TEXT("session"), SessionObj);

    // Summary
    TSharedPtr<FJsonObject> SummaryObj = MakeShared<FJsonObject>();
    SummaryObj->SetObjectField(TEXT("fps"), MetricSummaryToJson(Analysis.FPS));
    SummaryObj->SetObjectField(TEXT("frame_time_ms"), MetricSummaryToJson(Analysis.FrameTimeMs));
    SummaryObj->SetStringField(TEXT("bottleneck_thread"), Analysis.BottleneckThread);
    SummaryObj->SetObjectField(TEXT("draw_calls"), MetricSummaryToJson(Analysis.DrawCallsSummary));
    SummaryObj->SetObjectField(TEXT("triangles_rendered"), MetricSummaryToJson(Analysis.TrianglesSummary));
    SummaryObj->SetObjectField(TEXT("memory_mb"), MetricSummaryToJson(Analysis.MemoryMBSummary));
    Root->SetObjectField(TEXT("summary"), SummaryObj);

    // Spike count
    Root->SetNumberField(TEXT("spike_count"), Analysis.Spikes.Num());

    // Recommendations
    TArray<TSharedPtr<FJsonValue>> RecsArray;
    for (const FString& Rec : Analysis.Recommendations)
    {
        RecsArray.Add(MakeShared<FJsonValueString>(Rec));
    }
    Root->SetArrayField(TEXT("recommendations"), RecsArray);

    // Device warnings
    TArray<TSharedPtr<FJsonValue>> WarningsArray;
    for (const FString& Warning : Analysis.DeviceWarnings)
    {
        WarningsArray.Add(MakeShared<FJsonValueString>(Warning));
    }
    Root->SetArrayField(TEXT("device_warnings"), WarningsArray);

    // Output dir
    Root->SetStringField(TEXT("output_dir"), Analysis.OutputDir);

    // Full detail: add spike breakdowns
    if (bFullDetail && Analysis.Spikes.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> SpikesArray;
        for (const FSpikeCluster& Spike : Analysis.Spikes)
        {
            TSharedPtr<FJsonObject> SpikeObj = MakeShared<FJsonObject>();

            TArray<TSharedPtr<FJsonValue>> FrameRange;
            FrameRange.Add(MakeShared<FJsonValueNumber>(Spike.FrameStart));
            FrameRange.Add(MakeShared<FJsonValueNumber>(Spike.FrameEnd));
            SpikeObj->SetArrayField(TEXT("frame_range"), FrameRange);

            TArray<TSharedPtr<FJsonValue>> TimeRange;
            TimeRange.Add(MakeShared<FJsonValueNumber>(Spike.TimeStartSeconds));
            TimeRange.Add(MakeShared<FJsonValueNumber>(Spike.TimeEndSeconds));
            SpikeObj->SetArrayField(TEXT("time_range_s"), TimeRange);

            TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
            LocObj->SetNumberField(TEXT("x"), Spike.Location.X);
            LocObj->SetNumberField(TEXT("y"), Spike.Location.Y);
            LocObj->SetNumberField(TEXT("z"), Spike.Location.Z);
            SpikeObj->SetObjectField(TEXT("location"), LocObj);

            SpikeObj->SetNumberField(TEXT("avg_frame_time_ms"), Spike.AvgFrameTimeMs);

            TSharedPtr<FJsonObject> CauseObj = MakeShared<FJsonObject>();
            TArray<TSharedPtr<FJsonValue>> FrustumActors;
            for (const FSpikeActor& Actor : Spike.ActorsInFrustum)
            {
                FrustumActors.Add(MakeShared<FJsonValueObject>(SpikeActorToJson(Actor)));
            }
            CauseObj->SetArrayField(TEXT("actors_in_frustum"), FrustumActors);

            TArray<TSharedPtr<FJsonValue>> ShadowActors;
            for (const FSpikeActor& Actor : Spike.ShadowCastersBehindCamera)
            {
                ShadowActors.Add(MakeShared<FJsonValueObject>(SpikeActorToJson(Actor)));
            }
            CauseObj->SetArrayField(TEXT("shadow_casters_behind_camera"), ShadowActors);

            CauseObj->SetNumberField(TEXT("total_draw_calls"), Spike.TotalDrawCalls);
            CauseObj->SetNumberField(TEXT("total_triangles_in_frustum"), Spike.TotalTrianglesInFrustum);
            CauseObj->SetStringField(TEXT("delta_from_smooth"), Spike.DeltaFromSmooth);
            SpikeObj->SetObjectField(TEXT("cause_analysis"), CauseObj);

            SpikesArray.Add(MakeShared<FJsonValueObject>(SpikeObj));
        }
        Root->SetArrayField(TEXT("spikes"), SpikesArray);
    }

    return Root;
}

// ============================================================================
// Disk Output
// ============================================================================

FString FRuntimeProfiler::WriteResultsToDisk(const FProfileSession& Session, const FProfileAnalysis& Analysis)
{
    // Create output directory
    FString Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H%M%S"));
    FString OutputDir = FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("Profiling"), TEXT("RuntimeProfiler"), Timestamp);
    IFileManager::Get().MakeDirectory(*OutputDir, true);

    // Write manifest.json
    {
        TSharedPtr<FJsonObject> Manifest = MakeShared<FJsonObject>();
        Manifest->SetStringField(TEXT("feature"), TEXT("runtime-performance-analyzer"));
        Manifest->SetStringField(TEXT("timestamp"), Timestamp);
        Manifest->SetStringField(TEXT("level"), Session.LevelName);
        Manifest->SetStringField(TEXT("target_device"), Session.TargetDevice);
        Manifest->SetNumberField(TEXT("sample_count"), Session.Samples.Num());
        Manifest->SetNumberField(TEXT("duration_s"), Analysis.DurationSeconds);
        Manifest->SetNumberField(TEXT("spike_count"), Analysis.Spikes.Num());

        FString ManifestStr;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ManifestStr);
        FJsonSerializer::Serialize(Manifest.ToSharedRef(), Writer);
        FFileHelper::SaveStringToFile(ManifestStr, *FPaths::Combine(OutputDir, TEXT("manifest.json")));
    }

    // Write summary.json (the analysis result)
    {
        TSharedPtr<FJsonObject> SummaryJson = AnalysisToJson(Analysis, true);
        FString SummaryStr;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SummaryStr);
        FJsonSerializer::Serialize(SummaryJson.ToSharedRef(), Writer);
        FFileHelper::SaveStringToFile(SummaryStr, *FPaths::Combine(OutputDir, TEXT("summary.json")));
    }

    // Write session.json (raw time-series data)
    {
        TSharedPtr<FJsonObject> SessionJson = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> SamplesArray;
        for (const FProfileSample& S : Session.Samples)
        {
            TSharedPtr<FJsonObject> SObj = MakeShared<FJsonObject>();
            SObj->SetNumberField(TEXT("t"), S.TimestampSeconds);
            SObj->SetNumberField(TEXT("frame_ms"), S.FrameTimeMs);
            SObj->SetNumberField(TEXT("game_ms"), S.GameThreadMs);
            SObj->SetNumberField(TEXT("render_ms"), S.RenderThreadMs);
            SObj->SetNumberField(TEXT("gpu_ms"), S.GPUTimeMs);
            SObj->SetNumberField(TEXT("draw_calls"), S.DrawCalls);
            SObj->SetNumberField(TEXT("triangles"), S.TrianglesRendered);
            SObj->SetNumberField(TEXT("mem_mb"), S.MemoryUsedMB);
            SObj->SetNumberField(TEXT("tex_mb"), S.TexturePoolUsedMB);

            TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
            PosObj->SetNumberField(TEXT("x"), S.PlayerLocation.X);
            PosObj->SetNumberField(TEXT("y"), S.PlayerLocation.Y);
            PosObj->SetNumberField(TEXT("z"), S.PlayerLocation.Z);
            SObj->SetObjectField(TEXT("player_pos"), PosObj);

            SamplesArray.Add(MakeShared<FJsonValueObject>(SObj));
        }
        SessionJson->SetArrayField(TEXT("samples"), SamplesArray);

        FString SessionStr;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SessionStr);
        FJsonSerializer::Serialize(SessionJson.ToSharedRef(), Writer);
        FFileHelper::SaveStringToFile(SessionStr, *FPaths::Combine(OutputDir, TEXT("session.json")));
    }

    return OutputDir;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Build and run `UnrealClaude.RuntimeProfiler.AnalysisToJson`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UELLMToolkit/Private/RuntimeProfiler.cpp Source/UELLMToolkit/Private/Tests/RuntimeProfilerTests.cpp
git commit -m "feat: implement JSON serialization and disk output"
```

---

### Task 8: MCP Tool Class and Registration

**Files:**
- Create: `Source/UELLMToolkit/Private/MCP/Tools/MCPTool_RuntimeProfiler.h`
- Create: `Source/UELLMToolkit/Private/MCP/Tools/MCPTool_RuntimeProfiler.cpp`
- Modify: `Source/UELLMToolkit/Private/MCP/MCPToolRegistry.cpp`

- [ ] **Step 1: Create the tool header**

```cpp
// Source/UELLMToolkit/Private/MCP/Tools/MCPTool_RuntimeProfiler.h

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

class FRuntimeProfiler;

/**
 * MCP Tool: Runtime performance profiling during PIE gameplay.
 * Collects real-time metrics, correlates spikes with scene content,
 * and generates actionable recommendations for a target device.
 *
 * Operations:
 *   - start_profiling: Begin collecting metrics (auto-stops on PIE end)
 *   - get_status: Check if profiling is active
 *   - get_results: Retrieve analysis from last session
 */
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
```

- [ ] **Step 2: Create the tool implementation**

```cpp
// Source/UELLMToolkit/Private/MCP/Tools/MCPTool_RuntimeProfiler.cpp

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

    // Validate device name (warn but don't error on unknown)
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

    // Start PIE if not running
    bool bStartedPIE = false;
    if (GEditor && !GEditor->IsPlaySessionInProgress())
    {
        FRequestPlaySessionParams SessionParams;
        GEditor->RequestPlaySession(SessionParams);
        bStartedPIE = true;
    }

    if (!Profiler->Start(IntervalMs, TargetDevice, SpikeThreshold))
    {
        return FMCPToolResult::Error(TEXT("Failed to start profiling."));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("interval_ms"), IntervalMs);
    Data->SetStringField(TEXT("target_device"), TargetDevice);
    Data->SetNumberField(TEXT("spike_threshold"), SpikeThreshold);
    Data->SetBoolField(TEXT("started_pie"), bStartedPIE);

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
```

- [ ] **Step 3: Register the tool in MCPToolRegistry.cpp**

Add the include at the top of `MCPToolRegistry.cpp` with the other tool includes:

```cpp
#include "Tools/MCPTool_RuntimeProfiler.h"
```

Add the registration call inside `RegisterBuiltinTools()`, after the existing `MCPTool_ScenePerformance` registration:

```cpp
RegisterTool(MakeShared<FMCPTool_RuntimeProfiler>());
```

- [ ] **Step 4: Verify it compiles and the tool appears**

Build the plugin. Then verify the tool is registered:

```bash
curl http://localhost:3000/mcp/tools | grep runtime_profiler
```

Expected: `runtime_profiler` appears in the tool list.

- [ ] **Step 5: Commit**

```bash
git add Source/UELLMToolkit/Private/MCP/Tools/MCPTool_RuntimeProfiler.h Source/UELLMToolkit/Private/MCP/Tools/MCPTool_RuntimeProfiler.cpp Source/UELLMToolkit/Private/MCP/MCPToolRegistry.cpp
git commit -m "feat: add runtime_profiler MCP tool with start/status/results operations"
```

---

### Task 9: Update Feature State

**Files:**
- Modify: `docs/features/runtime-performance-analyzer/state.json`

- [ ] **Step 1: Update state.json to reflect implementation complete**

```json
{
  "feature": "runtime-performance-analyzer",
  "branch": "feature/runtime-performance-analyzer",
  "baseBranch": "main",
  "phase": "implemented",
  "created": "2026-04-10",
  "artifacts": {
    "discovery": "docs/features/runtime-performance-analyzer/spec.md",
    "spec": "docs/features/runtime-performance-analyzer/spec.md",
    "verified": true,
    "implemented": true,
    "tested": true
  }
}
```

- [ ] **Step 2: Commit**

```bash
git add docs/features/runtime-performance-analyzer/state.json
git commit -m "docs: update feature state to implemented"
```
