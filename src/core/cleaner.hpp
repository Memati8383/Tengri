#pragma once
#include <cstdint>

namespace cleaner
{
    struct ScanResult { double sizeMB = 0.0; int fileCount = 0; };
    ScanResult Scan(int category);
    double     Clean(int category);

    // Kategori 8 (Shader Cache) için ayrıntılı alt-maske. Varsayılan Scan/Clean(8)
    // çağrısı Shader_Default kombinasyonunu kullanır; Steam motor önbelleği
    // kapsam dışı tutulur çünkü silinmesi oyunu yeniden derleme nedeniyle
    // ilk açılışta yavaşlatır.
    enum ShaderMask : uint32_t
    {
        Shader_D3D         = 1u << 0,   // %LOCALAPPDATA%\D3DSCache
        Shader_NVIDIA      = 1u << 1,   // NVIDIA\DXCache, GLCache, NV_Cache
        Shader_AMD         = 1u << 2,   // AMD\DxCache, GLCache, VkCache
        Shader_Intel       = 1u << 3,   // Intel\ShaderCache
        Shader_SteamEngine = 1u << 4,   // Steam shadercache (kullanıcı onayıyla)

        Shader_Default = Shader_D3D | Shader_NVIDIA | Shader_AMD | Shader_Intel,
        Shader_All     = Shader_Default | Shader_SteamEngine,
    };

    ScanResult ScanShader(uint32_t mask);
    double     CleanShader(uint32_t mask);
}
