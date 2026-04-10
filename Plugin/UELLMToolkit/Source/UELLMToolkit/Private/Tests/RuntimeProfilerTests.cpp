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

#endif // WITH_DEV_AUTOMATION_TESTS
