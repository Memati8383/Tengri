#pragma once
#include <string>

namespace sysdetail
{
    // Bunlar metin değil durum olarak bildirilir. İfade dili tablolarında durur ve panel
    // çizilirken çözülür; böylece bir kez Gather() sırasında donmuş bir etiket kalmaz ve
    // dil değişikliği tutarlı olur.
    enum class SecureBootState { Unknown, Enabled, Disabled, Unsupported };
    enum class VirtState      { Disabled, Firmware, Vbs, HyperV };

    struct Info
    {
        int    cpuCores       = 0;
        int    cpuThreads     = 0;
        std::string cpuClock;

        std::string gpuVram;
        std::string gpuDriver;

        std::string ramTotal;

        std::string motherboard;
        std::string biosVersion;
        bool        biosLegacy = false;   // 0 = UEFI, 1 = Legacy BIOS; wording resolved at draw time

        SecureBootState secureBoot = SecureBootState::Unknown;
        VirtState      virtState  = VirtState::Disabled;

        std::string installDate;
        std::string directX;
        std::string displayRes;
        std::string systemLocale;
        // NOT: WMI alanları (RAM hızı/yuvası, monitör, TPM) burada DEĞİL.
        // Bu yapı bir kez doldurulup önbelleklenir, WMI sorgusu ise sonradan
        // tamamlanır; kopyalansalardı hep boş kalırlardı. Panel her kare
        // syswmi::Get()'i doğrudan okur.
    };

    void        Gather();
    const Info& Get();
}
