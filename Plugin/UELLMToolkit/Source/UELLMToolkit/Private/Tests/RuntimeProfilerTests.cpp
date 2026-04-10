#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "DeviceBudgets.h"
#include "RuntimeProfiler.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDeviceBudgetLookupTest,
    "UnrealClaude.RuntimeProfiler.DeviceBudgetLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FDeviceBudgetLookupTest::RunTest(const FString& Parameters)
{
    FDeviceBudget Quest3Budget = FDeviceBudget::Get(TEXT("quest_3_standalone"));
    TestTrue("Quest 3 budget is valid", Quest3Budget.bValid);
    TestEqual("Quest 3 max draw calls", Quest3Budget.MaxDrawCalls, 200);
    TestEqual("Quest 3 triangle budget", Quest3Budget.TriangleBudget, 750000);
    TestEqual("Quest 3 texture memory MB", Quest3Budget.TextureMemoryMB, 2048);
    TestFalse("Quest 3 no raytracing", Quest3Budget.bSupportsRaytracing);

    FDeviceBudget PCHighBudget = FDeviceBudget::Get(TEXT("pc_high"));
    TestTrue("PC high budget is valid", PCHighBudget.bValid);
    TestTrue("PC high draw calls > Quest 3", PCHighBudget.MaxDrawCalls > Quest3Budget.MaxDrawCalls);
    TestTrue("PC high supports raytracing", PCHighBudget.bSupportsRaytracing);

    FDeviceBudget UnknownBudget = FDeviceBudget::Get(TEXT("nonexistent_device"));
    TestFalse("Unknown device is invalid", UnknownBudget.bValid);

    TArray<FString> AllNames = FDeviceBudget::GetAllDeviceNames();
    TestTrue("At least 4 presets exist", AllNames.Num() >= 4);
    TestTrue("Contains quest_3_standalone", AllNames.Contains(TEXT("quest_3_standalone")));
    TestTrue("Contains pc_high", AllNames.Contains(TEXT("pc_high")));

    return true;
}

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

    for (int32 i = 0; i < 10; ++i)
    {
        FProfileSample Sample;
        Sample.FrameTimeMs = 16.6f + (i == 5 ? 40.0f : 0.0f);
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

    TArray<float> Empty;
    FMetricSummary EmptySummary = FMetricSummary::Compute(Empty);
    TestEqual("Empty min", EmptySummary.Min, 0.0f);
    return true;
}

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

    for (int32 i = 0; i < 20; ++i)
    {
        FProfileSample S;
        S.FrameTimeMs = 16.6f;
        S.GameThreadMs = 8.0f;
        S.RenderThreadMs = 10.0f;
        S.GPUTimeMs = 14.0f;
        S.DrawCalls = 500;
        S.TrianglesRendered = 400000;
        S.MemoryUsedMB = 3000.0f;
        S.TimestampSeconds = i * 0.2;
        Session.Samples.Add(S);
    }
    Session.Samples[10].FrameTimeMs = 55.0f;
    Session.Samples[10].GPUTimeMs = 48.0f;

    FProfileAnalysis Analysis;
    FRuntimeProfiler::ComputeSummary(Session, Analysis);

    TestEqual("Sample count", Analysis.SampleCount, 20);
    TestTrue("Min frame time near 16.6", FMath::IsNearlyEqual(Analysis.FrameTimeMs.Min, 16.6f, 0.1f));
    TestTrue("Max frame time near 55.0", FMath::IsNearlyEqual(Analysis.FrameTimeMs.Max, 55.0f, 0.1f));
    TestEqual("Bottleneck is GPU", Analysis.BottleneckThread, TEXT("gpu"));
    TestTrue("Max FPS near 60", Analysis.FPS.Max > 55.0f);
    TestTrue("Min FPS near 18", Analysis.FPS.Min < 20.0f);
    return true;
}

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

    for (int32 i = 0; i < 30; ++i)
    {
        FProfileSample S;
        S.TimestampSeconds = i * 0.2;
        S.PlayerLocation = FVector(100.0f * i, 0.0f, 0.0f);
        S.CameraLocation = S.PlayerLocation;
        S.CameraRotation = FRotator::ZeroRotator;

        if (i >= 10 && i <= 12)
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

    FProfileAnalysis Analysis;
    FRuntimeProfiler::ComputeSummary(Session, Analysis);
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

    TestTrue("Has recommendations", Analysis.Recommendations.Num() > 0);
    TestTrue("Has device warnings", Analysis.DeviceWarnings.Num() > 0);

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

#endif // WITH_DEV_AUTOMATION_TESTS
