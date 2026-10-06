#pragma once
#include <string>

// Gösterge panelinin kullandığı, yalnızca okunan sistem bilgileri.
namespace sys
{
    struct Snapshot
    {
        float cpu         = 0.0f;   // 0..1
        float ramUsedGB   = 0.0f;
        float ramTotalGB  = 0.0f;
        float ramFrac     = 0.0f;
        float diskFreeGB  = 0.0f;
        float diskTotalGB = 0.0f;
        float diskFrac    = 0.0f;   // kullanılan oran
        int   threads     = 0;
        unsigned long long uptimeSec = 0;
    };

    void            Update();
    const Snapshot& Get();

    std::string UserName();
    std::string ComputerName();
    std::string OsName();
    std::string CpuName();
    std::string GpuName();
    std::string Hwid();
}