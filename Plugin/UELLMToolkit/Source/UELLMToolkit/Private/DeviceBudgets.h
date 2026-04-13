#pragma once

#include "CoreMinimal.h"

struct FDeviceBudget
{
    bool bValid = false;
    FString DisplayName;
    int32 MaxDrawCalls = 0;
    int32 TriangleBudget = 0;
    int32 TextureMemoryMB = 0;
    bool bSupportsDynamicShadows = true;
    bool bSupportsRaytracing = false;

    static FDeviceBudget Get(const FString& DeviceName)
    {
        const TMap<FString, FDeviceBudget>& Presets = GetPresets();
        const FDeviceBudget* Found = Presets.Find(DeviceName.ToLower());
        if (Found)
        {
            return *Found;
        }
        return FDeviceBudget();
    }

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
