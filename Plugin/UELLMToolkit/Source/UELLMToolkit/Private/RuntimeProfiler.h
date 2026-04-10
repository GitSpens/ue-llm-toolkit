#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "DeviceBudgets.h"
#include "Containers/Ticker.h"

class UWorld;

struct FProfileSample
{
    float FrameTimeMs = 0.0f;
    float GameThreadMs = 0.0f;
    float RenderThreadMs = 0.0f;
    float GPUTimeMs = 0.0f;
    int32 DrawCalls = 0;
    int32 TrianglesRendered = 0;
    float MemoryUsedMB = 0.0f;
    float TexturePoolUsedMB = 0.0f;
    FVector PlayerLocation = FVector::ZeroVector;
    FVector CameraLocation = FVector::ZeroVector;
    FRotator CameraRotation = FRotator::ZeroRotator;
    FMatrix ViewProjectionMatrix = FMatrix::Identity;
    FString SubLevelName;
    double TimestampSeconds = 0.0;
};

struct FMetricSummary
{
    float Min = 0.0f;
    float Avg = 0.0f;
    float Max = 0.0f;
    float P95 = 0.0f;
    static FMetricSummary Compute(const TArray<float>& Values);
};

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

struct FProfileAnalysis
{
    float DurationSeconds = 0.0f;
    int32 SampleCount = 0;
    FString LevelName;
    FString TargetDevice;
    FMetricSummary FPS;
    FMetricSummary FrameTimeMs;
    FMetricSummary DrawCallsSummary;
    FMetricSummary TrianglesSummary;
    FMetricSummary MemoryMBSummary;
    FString BottleneckThread;
    TArray<FSpikeCluster> Spikes;
    TArray<FString> Recommendations;
    TArray<FString> DeviceWarnings;
    FString OutputDir;
};

struct FProfileSession
{
    FString TargetDevice;
    FString LevelName;
    float SpikeThreshold = 2.0f;
    TArray<FProfileSample> Samples;
    double StartTimeSeconds = 0.0;
    double EndTimeSeconds = 0.0;
};

class FRuntimeProfiler
{
public:
    FRuntimeProfiler();
    ~FRuntimeProfiler();

    bool Start(float InIntervalMs, const FString& InTargetDevice, float InSpikeThreshold);
    void Stop();
    bool IsActive() const { return bIsActive; }
    int32 GetSampleCount() const;
    double GetElapsedSeconds() const;
    const FProfileAnalysis* GetLastAnalysis() const;

    // Analysis passes (public for testability)
    static void ComputeSummary(const FProfileSession& Session, FProfileAnalysis& OutAnalysis);
    static void DetectSpikes(const FProfileSession& Session, UWorld* World, FProfileAnalysis& OutAnalysis);
    static void GenerateRecommendations(FProfileAnalysis& OutAnalysis, const FDeviceBudget& Budget);
    static FString WriteResultsToDisk(const FProfileSession& Session, const FProfileAnalysis& Analysis);
    static TSharedPtr<FJsonObject> AnalysisToJson(const FProfileAnalysis& Analysis, bool bFullDetail);

private:
    bool TickSample(float DeltaTime);
    void OnPIEEnded(bool bIsSimulating);
    void ProcessResults();
    FProfileSample CaptureSample(UWorld* PIEWorld) const;
    static void CorrelateWithScene(const FProfileSample& Sample, UWorld* World, FSpikeCluster& OutCluster);

    bool bIsActive = false;
    float IntervalSeconds = 0.2f;
    FTSTicker::FDelegateHandle TickerHandle;
    FDelegateHandle EndPIEHandle;
    double LastSampleTime = 0.0;
    FProfileSession CurrentSession;
    TSharedPtr<FProfileAnalysis> LastAnalysis;
    TWeakObjectPtr<UWorld> CachedPIEWorld;
};
