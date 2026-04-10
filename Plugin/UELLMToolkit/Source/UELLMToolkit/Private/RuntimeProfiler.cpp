// Copyright Natali Caggiano. All Rights Reserved.

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

// ============================================================
// FMetricSummary
// ============================================================

FMetricSummary FMetricSummary::Compute(const TArray<float>& Values)
{
    FMetricSummary Summary;
    if (Values.Num() == 0)
    {
        return Summary;
    }

    TArray<float> Sorted = Values;
    Sorted.Sort();

    Summary.Min = Sorted[0];
    Summary.Max = Sorted.Last();

    double Sum = 0.0;
    for (float V : Sorted)
    {
        Sum += V;
    }
    Summary.Avg = static_cast<float>(Sum / Sorted.Num());

    const int32 P95Index = FMath::FloorToInt(0.95f * (Sorted.Num() - 1));
    Summary.P95 = Sorted[P95Index];

    return Summary;
}

// ============================================================
// FRuntimeProfiler — constructor / destructor
// ============================================================

FRuntimeProfiler::FRuntimeProfiler() {}

FRuntimeProfiler::~FRuntimeProfiler()
{
    if (bIsActive)
    {
        Stop();
    }
}

// ============================================================
// Stub implementations (Tasks 6+ will flesh these out)
// ============================================================

bool FRuntimeProfiler::Start(float /*InIntervalMs*/, const FString& /*InTargetDevice*/, float /*InSpikeThreshold*/)
{
    return false;
}

void FRuntimeProfiler::Stop() {}

int32 FRuntimeProfiler::GetSampleCount() const
{
    return CurrentSession.Samples.Num();
}

double FRuntimeProfiler::GetElapsedSeconds() const
{
    return 0.0;
}

const FProfileAnalysis* FRuntimeProfiler::GetLastAnalysis() const
{
    return LastAnalysis.IsValid() ? LastAnalysis.Get() : nullptr;
}

bool FRuntimeProfiler::TickSample(float /*DeltaTime*/)
{
    return false;
}

void FRuntimeProfiler::OnPIEEnded(bool /*bIsSimulating*/) {}

void FRuntimeProfiler::ProcessResults() {}

FProfileSample FRuntimeProfiler::CaptureSample(UWorld* /*PIEWorld*/) const
{
    return FProfileSample();
}

FString FRuntimeProfiler::WriteResultsToDisk(const FProfileSession& /*Session*/, const FProfileAnalysis& /*Analysis*/)
{
    return FString();
}

TSharedPtr<FJsonObject> FRuntimeProfiler::AnalysisToJson(const FProfileAnalysis& /*Analysis*/, bool /*bFullDetail*/)
{
    return MakeShared<FJsonObject>();
}

// ============================================================
// Pass 1: ComputeSummary
// ============================================================

void FRuntimeProfiler::ComputeSummary(const FProfileSession& Session, FProfileAnalysis& OutAnalysis)
{
    TArray<float> FrameTimes, GameTimes, RenderTimes, GPUTimes, DrawCallsArr, TrianglesArr, MemoryArr, FPSArr;

    for (const FProfileSample& S : Session.Samples)
    {
        FrameTimes.Add(S.FrameTimeMs);
        GameTimes.Add(S.GameThreadMs);
        RenderTimes.Add(S.RenderThreadMs);
        GPUTimes.Add(S.GPUTimeMs);
        DrawCallsArr.Add(static_cast<float>(S.DrawCalls));
        TrianglesArr.Add(static_cast<float>(S.TrianglesRendered));
        MemoryArr.Add(S.MemoryUsedMB);

        if (S.FrameTimeMs > 0.0f)
        {
            FPSArr.Add(1000.0f / S.FrameTimeMs);
        }
    }

    OutAnalysis.FrameTimeMs        = FMetricSummary::Compute(FrameTimes);
    OutAnalysis.DrawCallsSummary   = FMetricSummary::Compute(DrawCallsArr);
    OutAnalysis.TrianglesSummary   = FMetricSummary::Compute(TrianglesArr);
    OutAnalysis.MemoryMBSummary    = FMetricSummary::Compute(MemoryArr);
    OutAnalysis.FPS                = FMetricSummary::Compute(FPSArr);

    // Compute per-thread averages so we can name the bottleneck
    FMetricSummary GameSummary   = FMetricSummary::Compute(GameTimes);
    FMetricSummary RenderSummary = FMetricSummary::Compute(RenderTimes);
    FMetricSummary GPUSummary    = FMetricSummary::Compute(GPUTimes);

    if (GPUSummary.Avg >= GameSummary.Avg && GPUSummary.Avg >= RenderSummary.Avg)
    {
        OutAnalysis.BottleneckThread = TEXT("gpu");
    }
    else if (RenderSummary.Avg >= GameSummary.Avg)
    {
        OutAnalysis.BottleneckThread = TEXT("render");
    }
    else
    {
        OutAnalysis.BottleneckThread = TEXT("game");
    }

    OutAnalysis.SampleCount = Session.Samples.Num();
    OutAnalysis.LevelName   = Session.LevelName;
    OutAnalysis.TargetDevice = Session.TargetDevice;

    if (Session.Samples.Num() > 1)
    {
        OutAnalysis.DurationSeconds = static_cast<float>(
            Session.Samples.Last().TimestampSeconds - Session.Samples[0].TimestampSeconds);
    }
    else
    {
        OutAnalysis.DurationSeconds = 0.0f;
    }
}

// ============================================================
// Pass 2: DetectSpikes
// ============================================================

void FRuntimeProfiler::CorrelateWithScene(const FProfileSample& Sample, UWorld* World, FSpikeCluster& OutCluster)
{
    if (!World)
    {
        return;
    }

    // Build a view matrix from camera location/rotation
    FVector CamPos = Sample.CameraLocation;
    FRotationMatrix RotMat(Sample.CameraRotation);
    FVector Forward = RotMat.GetScaledAxis(EAxis::X);
    FVector Up      = RotMat.GetScaledAxis(EAxis::Z);

    FMatrix ViewMatrix = FLookAtMatrix(CamPos, CamPos + Forward, Up);

    // 90-degree FOV, 16:9 aspect ratio, reversed-Z projection
    const float HalfFOVRad = FMath::DegreesToRadians(45.0f); // half of 90
    const float AspectRatio = 16.0f / 9.0f;
    const float NearPlane   = 10.0f;
    FMatrix ProjMatrix = FReversedZPerspectiveMatrix(HalfFOVRad, AspectRatio, 1.0f, NearPlane);

    FMatrix ViewProjMatrix = ViewMatrix * ProjMatrix;

    FConvexVolume ViewFrustum;
    GetViewFrustumBounds(ViewFrustum, ViewProjMatrix, false);

    TArray<FSpikeActor> CandidateActors;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor || Actor->IsPendingKillPending())
        {
            continue;
        }

        TArray<UPrimitiveComponent*> PrimComponents;
        Actor->GetComponents<UPrimitiveComponent>(PrimComponents);

        bool bInFrustum = false;
        for (UPrimitiveComponent* Prim : PrimComponents)
        {
            if (!Prim)
            {
                continue;
            }
            if (ViewFrustum.IntersectBox(Prim->Bounds.Origin, Prim->Bounds.BoxExtent))
            {
                bInFrustum = true;
                break;
            }
        }

        if (bInFrustum)
        {
            FActorMeshStats MeshStats;
            if (FSceneAnalyzer::GatherActorMeshStats(Actor, MeshStats))
            {
                FSpikeActor SA;
                SA.ActorName          = MeshStats.ActorLabel;
                SA.ActorClass         = MeshStats.ActorClass;
                SA.TriangleCount      = MeshStats.TriangleCount;
                SA.EstimatedDrawCalls = MeshStats.EstimatedDrawCalls;
                SA.MaterialCount      = MeshStats.MaterialCount;
                SA.LODCount           = MeshStats.LODCount;
                SA.bShadowCasting     = MeshStats.bShadowCasting;
                CandidateActors.Add(SA);
            }
        }
        else
        {
            // Check for shadow casters behind camera within 5000 units
            FVector ToActor = Actor->GetActorLocation() - CamPos;
            float DotForward = FVector::DotProduct(ToActor.GetSafeNormal(), Forward);
            float Distance   = ToActor.Size();

            if (DotForward < 0.0f && Distance <= 5000.0f)
            {
                FActorMeshStats MeshStats;
                if (FSceneAnalyzer::GatherActorMeshStats(Actor, MeshStats) && MeshStats.bShadowCasting)
                {
                    FSpikeActor SA;
                    SA.ActorName          = MeshStats.ActorLabel;
                    SA.ActorClass         = MeshStats.ActorClass;
                    SA.TriangleCount      = MeshStats.TriangleCount;
                    SA.EstimatedDrawCalls = MeshStats.EstimatedDrawCalls;
                    SA.MaterialCount      = MeshStats.MaterialCount;
                    SA.LODCount           = MeshStats.LODCount;
                    SA.bShadowCasting     = true;
                    OutCluster.ShadowCastersBehindCamera.Add(SA);
                }
            }
        }
    }

    // Sort frustum actors by triangle count descending, cap at 20
    CandidateActors.Sort([](const FSpikeActor& A, const FSpikeActor& B)
    {
        return A.TriangleCount > B.TriangleCount;
    });

    if (CandidateActors.Num() > 20)
    {
        CandidateActors.SetNum(20);
    }

    OutCluster.ActorsInFrustum = MoveTemp(CandidateActors);

    // Accumulate triangle count from frustum actors
    int32 FrustumTris = 0;
    for (const FSpikeActor& SA : OutCluster.ActorsInFrustum)
    {
        FrustumTris += SA.TriangleCount;
    }
    OutCluster.TotalTrianglesInFrustum = FrustumTris;
}

void FRuntimeProfiler::DetectSpikes(const FProfileSession& Session, UWorld* World, FProfileAnalysis& OutAnalysis)
{
    const int32 NumSamples = Session.Samples.Num();
    if (NumSamples == 0)
    {
        return;
    }

    const float Threshold = OutAnalysis.FrameTimeMs.Avg * Session.SpikeThreshold;

    // Identify spike frames
    TArray<bool> IsSpikeFrame;
    IsSpikeFrame.SetNumZeroed(NumSamples);
    for (int32 i = 0; i < NumSamples; ++i)
    {
        IsSpikeFrame[i] = (Session.Samples[i].FrameTimeMs > Threshold);
    }

    // Group consecutive spike frames into clusters
    int32 i = 0;
    while (i < NumSamples)
    {
        if (!IsSpikeFrame[i])
        {
            ++i;
            continue;
        }

        // Start of a new cluster
        int32 ClusterStart = i;
        int32 ClusterEnd   = i;
        while (ClusterEnd + 1 < NumSamples && IsSpikeFrame[ClusterEnd + 1])
        {
            ++ClusterEnd;
        }

        FSpikeCluster Cluster;
        Cluster.FrameStart       = ClusterStart;
        Cluster.FrameEnd         = ClusterEnd;
        Cluster.TimeStartSeconds = Session.Samples[ClusterStart].TimestampSeconds;
        Cluster.TimeEndSeconds   = Session.Samples[ClusterEnd].TimestampSeconds;

        // Compute average frame time and total draw calls across the cluster
        double SumFrameTime = 0.0;
        int32  SumDrawCalls = 0;
        for (int32 j = ClusterStart; j <= ClusterEnd; ++j)
        {
            SumFrameTime += Session.Samples[j].FrameTimeMs;
            SumDrawCalls += Session.Samples[j].DrawCalls;
        }
        const int32 ClusterLen = ClusterEnd - ClusterStart + 1;
        Cluster.AvgFrameTimeMs = static_cast<float>(SumFrameTime / ClusterLen);
        Cluster.TotalDrawCalls = SumDrawCalls;

        // Location = midpoint sample's PlayerLocation
        const int32 MidIndex = ClusterStart + (ClusterEnd - ClusterStart) / 2;
        Cluster.Location = Session.Samples[MidIndex].PlayerLocation;

        // Delta analysis: compare spike midpoint with the last smooth frame before the cluster
        if (ClusterStart > 0)
        {
            const FProfileSample& SmoothSample = Session.Samples[ClusterStart - 1];
            const FProfileSample& SpikeSample  = Session.Samples[MidIndex];

            float DeltaFrameTime = SpikeSample.FrameTimeMs - SmoothSample.FrameTimeMs;
            int32 DeltaDrawCalls = SpikeSample.DrawCalls   - SmoothSample.DrawCalls;
            int32 DeltaTris      = SpikeSample.TrianglesRendered - SmoothSample.TrianglesRendered;

            Cluster.DeltaFromSmooth = FString::Printf(
                TEXT("+%.1fms frame time, %+d draw calls, %+d triangles vs preceding smooth frame"),
                DeltaFrameTime, DeltaDrawCalls, DeltaTris);
        }

        // Frustum correlation (only when World is available)
        if (World)
        {
            CorrelateWithScene(Session.Samples[MidIndex], World, Cluster);
        }

        OutAnalysis.Spikes.Add(Cluster);

        i = ClusterEnd + 1;
    }
}

// ============================================================
// Pass 3: GenerateRecommendations
// ============================================================

void FRuntimeProfiler::GenerateRecommendations(FProfileAnalysis& OutAnalysis, const FDeviceBudget& Budget)
{
    // Per-spike recommendations
    for (const FSpikeCluster& Spike : OutAnalysis.Spikes)
    {
        FString LocationStr = FString::Printf(
            TEXT("%.0f, %.0f, %.0f"),
            Spike.Location.X, Spike.Location.Y, Spike.Location.Z);

        // LOD recommendations for high-poly actors without LOD chains
        for (const FSpikeActor& Actor : Spike.ActorsInFrustum)
        {
            if (Actor.TriangleCount > 100000 && Actor.LODCount <= 1)
            {
                OutAnalysis.Recommendations.Add(FString::Printf(
                    TEXT("Add LOD chain to '%s' (%s) near (%s): %d triangles with %d LOD level(s). "
                         "High-poly meshes without LODs are a common cause of GPU spikes."),
                    *Actor.ActorName, *Actor.ActorClass, *LocationStr,
                    Actor.TriangleCount, Actor.LODCount));
            }
        }

        // Material instancing recommendation when draw calls are high
        if (Spike.TotalDrawCalls > 2000)
        {
            for (const FSpikeActor& Actor : Spike.ActorsInFrustum)
            {
                if (Actor.MaterialCount > 4)
                {
                    OutAnalysis.Recommendations.Add(FString::Printf(
                        TEXT("Consider material instancing for '%s' near (%s): %d materials contribute "
                             "to %d total draw calls during spike. Merging or instancing materials "
                             "reduces draw call overhead."),
                        *Actor.ActorName, *LocationStr,
                        Actor.MaterialCount, Spike.TotalDrawCalls));
                    break; // One recommendation per spike for this category is enough
                }
            }
        }

        // General fallback if no specific actor was identified
        bool bHasSpecificRec = false;
        for (const FString& Rec : OutAnalysis.Recommendations)
        {
            if (Rec.Contains(LocationStr))
            {
                bHasSpecificRec = true;
                break;
            }
        }

        if (!bHasSpecificRec)
        {
            OutAnalysis.Recommendations.Add(FString::Printf(
                TEXT("Performance spike near (%s): %.1fms avg frame time. "
                     "Bottleneck thread: %s. Consider profiling this area in RenderDoc or Unreal Insights."),
                *LocationStr, Spike.AvgFrameTimeMs, *OutAnalysis.BottleneckThread));
        }
    }

    // Overall bottleneck recommendation
    OutAnalysis.Recommendations.Add(FString::Printf(
        TEXT("Primary bottleneck: %s thread. Focus optimisation efforts on %s-bound workloads."),
        *OutAnalysis.BottleneckThread, *OutAnalysis.BottleneckThread));

    // Device budget warnings
    if (Budget.bValid)
    {
        if (OutAnalysis.DrawCallsSummary.Avg > static_cast<float>(Budget.MaxDrawCalls))
        {
            OutAnalysis.DeviceWarnings.Add(FString::Printf(
                TEXT("Average draw calls (%.0f) exceed %s budget (%d). "
                     "Reduce draw calls or use GPU instancing."),
                OutAnalysis.DrawCallsSummary.Avg, *Budget.DisplayName, Budget.MaxDrawCalls));
        }

        if (OutAnalysis.DrawCallsSummary.P95 > static_cast<float>(Budget.MaxDrawCalls))
        {
            OutAnalysis.DeviceWarnings.Add(FString::Printf(
                TEXT("P95 draw calls (%.0f) exceed %s budget (%d). "
                     "Worst-case draw call count is too high for this device."),
                OutAnalysis.DrawCallsSummary.P95, *Budget.DisplayName, Budget.MaxDrawCalls));
        }

        if (OutAnalysis.TrianglesSummary.Avg > static_cast<float>(Budget.TriangleBudget))
        {
            OutAnalysis.DeviceWarnings.Add(FString::Printf(
                TEXT("Average triangle count (%.0f) exceeds %s budget (%d). "
                     "Use LODs and mesh simplification to reduce geometry."),
                OutAnalysis.TrianglesSummary.Avg, *Budget.DisplayName, Budget.TriangleBudget));
        }

        if (OutAnalysis.MemoryMBSummary.Max > static_cast<float>(Budget.TextureMemoryMB))
        {
            OutAnalysis.DeviceWarnings.Add(FString::Printf(
                TEXT("Peak memory usage (%.0f MB) exceeds %s texture memory budget (%d MB). "
                     "Reduce texture resolutions or streaming pool size."),
                OutAnalysis.MemoryMBSummary.Max, *Budget.DisplayName, Budget.TextureMemoryMB));
        }

        if (!Budget.bSupportsDynamicShadows)
        {
            // Check if any spike actor casts shadows — use goto to break out of nested loops
            for (const FSpikeCluster& Spike : OutAnalysis.Spikes)
            {
                for (const FSpikeActor& Actor : Spike.ActorsInFrustum)
                {
                    if (Actor.bShadowCasting)
                    {
                        OutAnalysis.DeviceWarnings.Add(FString::Printf(
                            TEXT("Device '%s' does not support dynamic shadows, but shadow-casting actors "
                                 "(e.g. '%s') were detected during spikes. Disable shadow casting on "
                                 "these actors or bake lighting."),
                            *Budget.DisplayName, *Actor.ActorName));
                        goto DoneShadowCheck;
                    }
                }
            }
            DoneShadowCheck:;
        }
    }
}
