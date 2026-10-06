#pragma once

namespace cleaner
{
    struct ScanResult { double sizeMB = 0.0; int fileCount = 0; };
    ScanResult Scan(int category);
    double     Clean(int category);
}
