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

#endif // WITH_DEV_AUTOMATION_TESTS
