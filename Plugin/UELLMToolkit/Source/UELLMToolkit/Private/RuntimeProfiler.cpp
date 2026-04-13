// Copyright Natali Caggiano. All Rights Reserved.

#include "RuntimeProfiler.h"
#include "SceneAnalyzer.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonValue.h"
#include "EngineUtils.h"
#include "RHIStats.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Misc/DateTime.h"

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
// Lifecycle — Start / Stop / Tick
// ============================================================

bool FRuntimeProfiler::Start(float InIntervalMs, const FString& InTargetDevice, float InSpikeThreshold)
{
    if (bIsActive)
    {
        return false; // Already profiling
    }

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

    // Enable stat unit for accurate per-thread timing
    if (GEngine)
    {
        GEngine->Exec(nullptr, TEXT("stat unit"));
    }

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

    // Disable stat unit
    if (GEngine)
    {
        GEngine->Exec(nullptr, TEXT("stat unit"));
    }

    ProcessResults();
}

int32 FRuntimeProfiler::GetSampleCount() const
{
    return CurrentSession.Samples.Num();
}

double FRuntimeProfiler::GetElapsedSeconds() const
{
    if (bIsActive)
    {
        return FPlatformTime::Seconds() - CurrentSession.StartTimeSeconds;
    }
    return CurrentSession.EndTimeSeconds - CurrentSession.StartTimeSeconds;
}

const FProfileAnalysis* FRuntimeProfiler::GetLastAnalysis() const
{
    return LastAnalysis.IsValid() ? LastAnalysis.Get() : nullptr;
}

// ============================================================
// Tick — Sample Capture
// ============================================================

bool FRuntimeProfiler::TickSample(float /*DeltaTime*/)
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

// ============================================================
// PIE End Callback
// ============================================================

void FRuntimeProfiler::OnPIEEnded(bool /*bIsSimulating*/)
{
    if (bIsActive)
    {
        Stop();
    }
}

// ============================================================
// Process Results
// ============================================================

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

// ============================================================
// Sample Capture
// ============================================================

FProfileSample FRuntimeProfiler::CaptureSample(UWorld* PIEWorld) const
{
    FProfileSample Sample;

    // Frame timing — FApp::GetDeltaTime() always works
    float DeltaSeconds = FApp::GetDeltaTime();
    Sample.FrameTimeMs = DeltaSeconds * 1000.0f;

    // Thread times via FStatUnitData (populated after stat unit is enabled)
    UGameViewportClient* Viewport = PIEWorld ? PIEWorld->GetGameViewport() : nullptr;
    if (Viewport)
    {
        const FStatUnitData* StatData = Viewport->GetStatUnitData();
        if (StatData && StatData->RawFrameTime > 0.0f)
        {
            // Use more accurate stat unit data when available
            Sample.FrameTimeMs    = StatData->RawFrameTime;
            Sample.GameThreadMs   = StatData->RawGameThreadTime;
            Sample.RenderThreadMs = StatData->RawRenderThreadTime;
            Sample.GPUTimeMs      = StatData->RawGPUFrameTime[0];
        }
    }

    // Draw calls and triangles from RHI globals (arrays indexed by GPU in UE 5.7)
    Sample.DrawCalls = GNumDrawCallsRHI[0];
    Sample.TrianglesRendered = GNumPrimitivesDrawnRHI[0];

    // Memory
    FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
    Sample.MemoryUsedMB = static_cast<float>(MemStats.UsedPhysical) / (1024.0f * 1024.0f);

    // Player and camera position
    APlayerController* PC = PIEWorld ? PIEWorld->GetFirstPlayerController() : nullptr;
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
    if (PIEWorld && PIEWorld->GetCurrentLevel())
    {
        Sample.SubLevelName = PIEWorld->GetCurrentLevel()->GetOuter()->GetName();
    }

    return Sample;
}

// ============================================================
// JSON Serialization
// ============================================================

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
    SessionObj->SetNumberField(TEXT("duration_s"),   Analysis.DurationSeconds);
    SessionObj->SetNumberField(TEXT("sample_count"), Analysis.SampleCount);
    SessionObj->SetStringField(TEXT("level"),        Analysis.LevelName);
    SessionObj->SetStringField(TEXT("target_device"), Analysis.TargetDevice);
    Root->SetObjectField(TEXT("session"), SessionObj);

    // Summary
    TSharedPtr<FJsonObject> SummaryObj = MakeShared<FJsonObject>();
    SummaryObj->SetObjectField(TEXT("fps"),               MetricSummaryToJson(Analysis.FPS));
    SummaryObj->SetObjectField(TEXT("frame_time_ms"),     MetricSummaryToJson(Analysis.FrameTimeMs));
    SummaryObj->SetStringField(TEXT("bottleneck_thread"), Analysis.BottleneckThread);
    SummaryObj->SetObjectField(TEXT("draw_calls"),        MetricSummaryToJson(Analysis.DrawCallsSummary));
    SummaryObj->SetObjectField(TEXT("triangles_rendered"), MetricSummaryToJson(Analysis.TrianglesSummary));
    SummaryObj->SetObjectField(TEXT("memory_mb"),         MetricSummaryToJson(Analysis.MemoryMBSummary));
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

    // Full detail: add per-spike breakdowns
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

            CauseObj->SetNumberField(TEXT("total_draw_calls"),          Spike.TotalDrawCalls);
            CauseObj->SetNumberField(TEXT("total_triangles_in_frustum"), Spike.TotalTrianglesInFrustum);
            CauseObj->SetStringField(TEXT("delta_from_smooth"),         Spike.DeltaFromSmooth);
            SpikeObj->SetObjectField(TEXT("cause_analysis"), CauseObj);

            SpikesArray.Add(MakeShared<FJsonValueObject>(SpikeObj));
        }
        Root->SetArrayField(TEXT("spikes"), SpikesArray);
    }

    return Root;
}

// ============================================================
// Disk Output
// ============================================================

FString FRuntimeProfiler::WriteResultsToDisk(const FProfileSession& Session, const FProfileAnalysis& Analysis)
{
    // Create output directory with timestamp
    FString Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H%M%S"));
    FString OutputDir = FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("Profiling"), TEXT("RuntimeProfiler"), Timestamp);
    IFileManager::Get().MakeDirectory(*OutputDir, true);

    // Write manifest.json
    {
        TSharedPtr<FJsonObject> Manifest = MakeShared<FJsonObject>();
        Manifest->SetStringField(TEXT("feature"),      TEXT("runtime-performance-analyzer"));
        Manifest->SetStringField(TEXT("timestamp"),    Timestamp);
        Manifest->SetStringField(TEXT("level"),        Session.LevelName);
        Manifest->SetStringField(TEXT("target_device"), Session.TargetDevice);
        Manifest->SetNumberField(TEXT("sample_count"), Session.Samples.Num());
        Manifest->SetNumberField(TEXT("duration_s"),   Analysis.DurationSeconds);
        Manifest->SetNumberField(TEXT("spike_count"),  Analysis.Spikes.Num());

        FString ManifestStr;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ManifestStr);
        FJsonSerializer::Serialize(Manifest.ToSharedRef(), Writer);
        FFileHelper::SaveStringToFile(ManifestStr, *FPaths::Combine(OutputDir, TEXT("manifest.json")));
    }

    // Write summary.json (full analysis with spike breakdowns)
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
            SObj->SetNumberField(TEXT("t"),          S.TimestampSeconds);
            SObj->SetNumberField(TEXT("frame_ms"),   S.FrameTimeMs);
            SObj->SetNumberField(TEXT("game_ms"),    S.GameThreadMs);
            SObj->SetNumberField(TEXT("render_ms"),  S.RenderThreadMs);
            SObj->SetNumberField(TEXT("gpu_ms"),     S.GPUTimeMs);
            SObj->SetNumberField(TEXT("draw_calls"), S.DrawCalls);
            SObj->SetNumberField(TEXT("triangles"),  S.TrianglesRendered);
            SObj->SetNumberField(TEXT("mem_mb"),     S.MemoryUsedMB);
            SObj->SetNumberField(TEXT("tex_mb"),     S.TexturePoolUsedMB);

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
