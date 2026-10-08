#include "lang.hpp"
#include "brand.hpp"
#include <windows.h>
#include <string>

namespace lang
{
    namespace
    {
        Id g_lang = EN;

        const char* g_en[] =
        {
            "Secure Loader", "Open Source System Optimizer", "LICENSE KEY", "Generate", "Remember me",
            "Activate license", "Demo mode", "any key is accepted", "Contacting license server...",
            "Activation failed", "Signed out",

            "Dashboard", "Cleaner", "Tweaks", "Network", "Settings", "System Info",
            "Overview of your system at a glance",
            "Find and remove junk files to reclaim space",
            "Fine-tune Windows for performance and privacy",
            "Lower latency and optimize your connection",
            "Personalize the look and behaviour of the app",
            "Detailed hardware and software information",

            "Processor", "Memory", "Storage", "Health", "Excellent", "Good", "Needs attention",
            "%d logical threads", "of %.0f GB in use", "free of %.0f GB on C:",
            "Processor load", "Live \xC2\xB7 last 30 seconds", "Quick optimize", "One click, every module",
            "health score", "optimizing", "Optimize now",
            "System optimized",

            "System", "Subscription", "Operating system", "Graphics", "Computer", "User", "Uptime",
            "Plan", "Status", "Active", "Expires", "HWID",

            "Ready to scan", "Select categories below and start a scan.",
            "Scan complete", "Junk found", "ready to clean",
            "All clean", "your system is spotless", "freed", "Scan", "Rescan", "Clean now", "CATEGORIES",

            "Temporary files", "Browser cache", "Windows Update cache", "Recycle Bin", "Prefetch data",
            "System logs", "Thumbnail cache", "Crash dumps", "Shader cache", "Delivery Optimization",
            "User and system temp folders", "Chrome, Edge, Firefox, Opera",
            "Downloaded update packages", "Deleted files waiting to go",
            "Application launch traces", "Event and setup log files",
            "Explorer preview database", "Memory dumps and error reports",
            "DirectX and driver shaders", "Peer-to-peer update cache",

            "Performance", "Gaming", "Privacy", "Visual", "Games", "FiveM", "Delay",
            "Valorant", "CS2", "Fortnite",
            "Reset", "Defaults restored", "Tweaks applied",
            "need admin", "Will start with Windows",
            "The shell refused a tray icon; minimize keeps its normal behaviour",

            "RAM optimization", "Group services into fewer svchost processes",
            "Installed memory", "Current profile",
            "Apply profile", "RAM profile applied", "Restart required to take effect",
            "Custom",

            "Latency", "Round-trip time", "simulated", "jitter", "Waiting for a sample...",
            "DNS provider", "Resolver used by your network adapters", "Flush DNS cache",
            "DNS updated", "DNS cache flushed",
            "Connection", "Active network adapter details",
            "OPTIMIZATIONS",

            "Visual effects", "Tune the background scene",
            "Particles", "Connection lines", "Mouse interaction",
            "Top light", "Light sweep", "Particle count", "Particle speed",
            "General", "Application behaviour", "Remember license", "Notifications",
            "Launch at startup", "Minimize to tray", "Account", "Sign out",
            "Language",

            "Hardware", "Software", "Security",
            "CPU", "Cores", "Threads", "Base clock",
            "GPU", "VRAM", "Driver",
            "Total RAM",
            "RAM speed", "RAM slots",
            "Motherboard", "BIOS version", "BIOS mode",
            "Secure Boot", "Enabled", "Disabled", "Not supported",
            "Virtualization", "Install date", "DirectX",
            "Display", "System locale",
            "Monitor", "TPM", "Present", "Absent",
            "Present, ready", "Present, not activated", "Present, not ready",
            "%d module(s) on %s", "Still running in the notification area - the icon may be hidden behind the arrow",

            "Apply changes", "No changes", "Undo", "Restore the settings from before the last apply?", "Settings restored.", "No backup to restore.", "Backup taken before applying.", "Your session has been closed", "plan - welcome back!",
            "Memory trimmed, caches flushed",
            "All tweaks in this category disabled",
            "Nothing changed - %d tweaks active",
            "%d tweaks active - %d could not be written (run as administrator)",
            "%d tweaks active - restart recommended",
            "Adapter", "Local IP", "Gateway",
            "Skipped", "Scanning %s...", "Cleaning %s...",
            "Resolver cache cleared",
            "Not available on this adapter",

            "Please enter your license key", "Welcome back, ",
            "%d of %d %s tweaks enabled",
            "Establishing secure session", "Verifying license integrity",
            "Loading modules", "Preparing interface",
            "Automatic",
            "Lifetime", "Demo", "Never", "Please enter a license key",

            "Enabled (firmware)", "Enabled (VBS)", "Enabled (Hyper-V)",
            "Disabled in firmware", "Not supported (Legacy BIOS)", "UEFI", "Legacy (BIOS)",

            "OPEN SOURCE", "About", "Version", "License: MIT",
            "This build ships a demo license screen: any key is accepted and nothing is validated against a server.",
            "The Games, FiveM, Delay and RAM categories write to HKLM, so the app requests administrator rights at launch.",
            "Close", "Repository",
            "Version, links and notices", "LINKS", "Source code", "NOTICES", "License",

            // Bildirimler
            "Windows notifications", "Errors only",
            "View results", "Undo", "Retry", "OK", "Working",

            // ---- Startup / Services sekmeleri ----
            "Startup", "Services",
            "Enable or disable programs that launch at sign-in",
            "Tune Windows background services for gaming",
            "No startup entries found", "No services found",
            "Name", "Publisher", "Scope", "Command", "State", "Start type",
            "HKCU \\ Run", "HKCU \\ RunOnce", "HKLM \\ Run", "HKLM \\ RunOnce",
            "HKCU \\ Policy \\ Explorer \\ Run", "HKLM \\ Policy \\ Explorer \\ Run",
            "Startup folder (user)", "Startup folder (all users)",
            "HKCU \\ Winlogon", "HKLM \\ Winlogon",
            "Low", "Medium", "High", "Unknown",
            "Automatic", "Automatic (Delayed)", "Manual", "Disabled", "Unknown", "Running",
            "Enable", "Disable", "Remove", "Refresh", "Rescan",
            "Apply game profile", "Restore defaults",
            "Game profile applied", "Defaults restored",
            "System protection is disabled - open System Properties to enable it",
            "Administrator rights required for HKLM entries",
            "Administrator rights required to change services",
            "TENGRI auto-start", "Where this entry lives in the registry",
            "Differs from the Windows default", "Logon value - listed only, disabling it locks you out",
            "Safe", "Caution", "Risky",
            "All", "System",

            // ---- Updates (otomatik sürüm denetimi) ----
            "Updates", "Automatic update check",
            "Checks GitHub Releases at startup and every 24 hours",
            "Checking...", "You're up to date", "New version available: %s",
            "Not checked yet", "Last check: %d h ago", "Last check: just now",
            "Check now", "Download", "Install and restart", "Cancel",
            "Downloading... %d%%", "Verifying checksum...", "Verified: %s",
            "The app closes and the new version starts.",
            "The app folder isn't writable. The downloaded file stays at: %s",
            "Update check failed: %s", "%s of %s",
            "Network error", "Server error", "No update manifest in this release",
            "Malformed update manifest", "Untrusted download host", "File error",
            "Checksum mismatch", "Folder isn't writable",
            "Update available", "Version %s has been released.",
            "Update downloaded", "Version %s is verified and ready to install.",
            "UPDATE AVAILABLE", "UP TO DATE", "CHECKING",

            // ---- System tray: close behaviour + context menu ----
            "Keep running when closed",
            "The close button hides the window; the app keeps running in the tray",
            "Show window", "Go to page", "Exit",

            // ---- TweakNames: 10 kategori x 8 satır ----
            // Performans
            "Ultimate power plan", "Disable background apps", "Optimize visual effects",
            "Disable SysMain", "Disable hibernation", "System responsiveness",
            "Disable power throttling", "Memory management",
            // Oyun
            "Game Mode", "Fullscreen optimizations", "Hardware GPU scheduling",
            "Disable Xbox Game Bar", "Raw mouse input", "Timer resolution 0.5 ms",
            "High CPU priority", "Game Mode (legacy titles)",
            // Gizlilik
            "Disable telemetry", "Disable activity history", "Disable advertising ID",
            "Disable location tracking", "Disable Cortana", "Block feedback requests",
            "Tailored experiences", "Disable error reporting",
            // Görsel
            "Disable animations", "Disable transparency", "Classic context menu",
            "Show file extensions", "Show hidden files", "Hide taskbar search",
            "Disable lock screen tips", "Disable startup delay",
            // Oyunlar
            "Game task priority", "Disable Game DVR", "Games clock rate",
            "Per-core GPU DPC", "NVIDIA thread priority", "Disable paging executive",
            "Large system cache", "IO page lock limit",
            // FiveM
            "FiveM task boost", "System responsiveness 0", "Network throttling off",
            "Menu show delay 0", "Fast app termination", "Low level hooks timeout",
            "Service kill timeout", "Disable full window drag",
            // Gecikme
            "Global timer resolution", "DPC watchdog offset", "Exception chain validation",
            "Disable interrupt steering", "Win32 priority separation", "Reliability timestamp 0",
            "LanmanServer delay fix", "Mouse input delay fix",

            // Valorant
            "Valorant process priority", "Riot client to background", "Vanguard to background",
            "Disable fullscreen optimizations", "Game task scheduling", "Low level hooks timeout",
            "Network throttling off", "System responsiveness 0",
            // CS2
            "CS2 process priority", "Steam client to background", "Steam web helper to background",
            "Disable Steam lazy loading", "Game task scheduling", "Game Mode",
            "Disable Game DVR", "Raw mouse input",
            // Fortnite
            "Fortnite process priority", "Epic launcher to background", "Epic services to background",
            "Disable fullscreen optimizations", "Game task scheduling", "Game Mode",
            "Raw mouse input", "System responsiveness 0",

            // ---- TweakDescs: aynı düzen ----
            "Unlock the hidden power scheme", "Stop UWP apps running idle",
            "Keep smoothing, drop the rest", "Stop Superfetch disk thrashing",
            "Free hiberfil.sys disk space", "Prioritize foreground tasks",
            "Keep cores at full clock", "Tune paging and cache sizes",

            "Prioritize games for resources", "Disable for exclusive fullscreen",
            "Lower latency frame scheduling", "Remove overlay and DVR capture",
            "Turn off pointer acceleration", "Tighter frame pacing",
            "Boost the active game process", "Apply Game Mode to older executables",

            "Stop diagnostic data uploads", "Do not log your timeline",
            "No personalized ad tracking", "Block system-wide location",
            "Turn off the voice assistant", "Never ask for feedback",
            "Disable usage-based tips", "Do not send crash reports",

            "Instant window transitions", "Solid taskbar and menus",
            "Full right-click menu", "Always show .exe, .txt, ...",
            "Reveal hidden folders", "Reclaim taskbar space",
            "Clean lock screen", "Launch startup apps instantly",

            "GPU 8 / CPU 6 / high scheduling", "Turn off recording and FSE mode",
            "10000 ticks, no background cap", "Spread driver DPCs across cores",
            "Raise nvlddmkm to priority 31", "Keep kernel code in physical RAM",
            "Favour file cache over working set", "1 MB lock limit, bigger L2 hint",

            "Full game task profile for FiveM", "Give 100% of CPU to foreground",
            "Remove the 10 packet/ms cap", "Instant menus, no fade-in wait",
            "Shorter hung-app timeouts", "1000 ms instead of 5000 ms",
            "Shut services down faster", "Lighter window movement",

            "Honour 0.5 ms timer requests", "Relax the DPC watchdog profile",
            "Skip SEHOP chain checks", "Keep interrupts on their core",
            "0x28 - short, fixed quantums", "Stop reliability sampling writes",
            "No sharing-violation wait", "AAP threshold + feature settings",

            "CPU, IO and page priority 1/1/6", "Give the lobby client idle priority",
            "vgc and vgtray off the critical path", "Removes the borderless DWM swap",
            "GPU 8 / CPU 6 / high scheduling", "1000 ms instead of 5000 ms",
            "Remove the 10 packet/ms cap", "Give 100% of CPU to foreground",

            "Raise cs2.exe above the scheduler", "Steam menus stop competing for CPU",
            "Web helper pages stop competing for I/O", "Load the full library at startup",
            "GPU 8 / CPU 6 / high scheduling", "Windows schedules the game for you",
            "Remove overlay and DVR capture", "Turn off pointer acceleration",

            "CPU, IO and page priority 1/1/6", "Launcher downloads yield to the game",
            "EOS and web helper yield to the game", "Removes the borderless DWM swap",
            "GPU 8 / CPU 6 / high scheduling", "Windows schedules the game for you",
            "Turn off pointer acceleration", "Give 100% of CPU to foreground",

            // ---- NetNames / NetDescs: 6 satır ----
            "TCP auto-tuning", "Disable Nagle's algorithm", "Network throttling index",
            "QoS packet scheduler", "Large send offload", "Interrupt moderation",

            "Optimal receive window scaling", "Lower latency for small packets",
            "Remove multimedia throttling", "Reserve no bandwidth for QoS",
            "Disable LSO on adapters", "Per-adapter; not settable globally",
        };

        // Türkçe metinler: \x kaçışlarının ardından gelen a-f onaltılık karakterlerinden
        // ayırmak için dize birleştirmesi kullanılıyor, bu yüzden bir kelime birden
        // çok parçaya bölünmüş görünebilir.
        const char* g_tr[] =
        {
            "G\xC3\xBC" "venli Y\xC3\xBC" "kleyici", "A\xC3\x87" "\xC4\xB0K Kaynak Sistem Optimize Edici",
            "L\xC4\xB0SANS ANAHTARI", "Olu\xC5\x9Ftur", "Beni hat\xC4\xB1rla",
            "Lisans\xC4\xB1 etkinle\xC5\x9Ftir", "Demo modu",
            "herhangi bir anahtar kabul edilir",
            "Lisans sunucusuna ba\xC4\x9Flan\xC4\xB1l\xC4\xB1yor...",
            "Etkinle\xC5\x9Ftirme ba\xC5\x9F" "ar\xC4\xB1s\xC4\xB1z",
            "\xC3\x87\xC4\xB1k\xC4\xB1\xC5\x9F yap\xC4\xB1ld\xC4\xB1",

            "G\xC3\xB6sterge Paneli", "Temizleyici", "\xC4\xB0nce Ayarlar",
            "A\xC4\x9F", "Ayarlar", "Sistem Bilgisi",
            "Sisteminize genel bir bak\xC4\xB1\xC5\x9F",
            "Gereksiz dosyalar\xC4\xB1 bulup temizleyin",
            "Windows'\xC4\xB1 performans i\xC3\xA7in optimize edin",
            "Gecikmeyi d\xC3\xBC\xC5\x9F\xC3\xBC" "r\xC3\xBC" "n, ba\xC4\x9Flant\xC4\xB1y\xC4\xB1 optimize edin",
            "Uygulaman\xC4\xB1n g\xC3\xB6r\xC3\xBC" "n\xC3\xBC" "m ve davran\xC4\xB1\xC5\x9F\xC4\xB1n\xC4\xB1 \xC3\xB6" "zelle\xC5\x9Ftirin",
            "Detayl\xC4\xB1 donan\xC4\xB1m ve yaz\xC4\xB1l\xC4\xB1m bilgisi",

            "\xC4\xB0\xC5\x9Flemci", "Bellek", "Depolama", "Sa\xC4\x9Fl\xC4\xB1k",
            "M\xC3\xBC" "kemmel", "\xC4\xB0yi", "\xC4\xB0lgiye ihtiya\xC3\xA7 var",
            "%d mant\xC4\xB1ksal i\xC5\x9F par\xC3\xA7" "a" "c\xC4\xB1\xC4\x9F\xC4\xB1",
            "%.0f GB kullan\xC4\xB1mda", "C: \xC3\xBC" "zerinde %.0f GB bo\xC5\x9F",
            "\xC4\xB0\xC5\x9Flemci y\xC3\xBC" "k\xC3\xBC",
            "Canl\xC4\xB1 \xC2\xB7 son 30 saniye",
            "H\xC4\xB1zl\xC4\xB1 optimizasyon", "Tek t\xC4\xB1kla, t\xC3\xBC" "m mod\xC3\xBC" "ller",
            "sa\xC4\x9Fl\xC4\xB1k puan\xC4\xB1", "optimize ediliyor", "\xC5\x9Eimdi optimize et",
            "Sistem optimize edildi",

            "Sistem", "Abonelik", "\xC4\xB0\xC5\x9Fletim sistemi", "Grafik", "Bilgisayar",
            "Kullan\xC4\xB1" "c\xC4\xB1", "\xC3\x87" "al\xC4\xB1\xC5\x9Fma s\xC3\xBCresi",
            "Plan", "Durum", "Aktif", "Biti\xC5\x9F", "HWID",

            "Taramaya haz\xC4\xB1r", "A\xC5\x9F" "a\xC4\x9F\xC4\xB1" "dan kategorileri se\xC3\xA7ip taray\xC4\xB1n.",
            "Tarama tamamland\xC4\xB1", "Gereksiz dosya bulundu", "temizlemeye haz\xC4\xB1r",
            "Temiz", "sisteminiz tertemiz", "temizlendi",
            "Tara", "Yeniden tara", "\xC5\x9Eimdi temizle", "KATEGOR\xC4\xB0LER",

            "Ge\xC3\xA7ici dosyalar", "Taray\xC4\xB1" "c\xC4\xB1 \xC3\xB6nbelle\xC4\x9Fi",
            "Windows Update \xC3\xB6nbelle\xC4\x9Fi", "Geri D\xC3\xB6n\xC3\xBC\xC5\x9F\xC3\xBCm Kutusu",
            "\xC3\x96ny\xC3\xBC" "kleme verileri", "Sistem g\xC3\xBC" "nl\xC3\xBC" "kleri",
            "K\xC3\xBC\xC3\xA7\xC3\xBC" "k resim \xC3\xB6nbelle\xC4\x9Fi",
            "\xC3\x87\xC3\xB6kme d\xC3\xB6k\xC3\xBCmleri", "G\xC3\xB6lgelendiri" "ci \xC3\xB6nbelle\xC4\x9Fi",
            "Teslim Optimizasyonu",
            "Kullan\xC4\xB1" "c\xC4\xB1 ve sistem ge\xC3\xA7i" "ci klas\xC3\xB6rleri",
            "Chrome, Edge, Firefox, Opera",
            "\xC4\xB0ndirilen g\xC3\xBC" "n" "celleme paketleri",
            "Silinmeyi bekleyen dosyalar",
            "Uygulama ba\xC5\x9Flatma izleri",
            "Olay ve kurulum g\xC3\xBC" "nl\xC3\xBC" "k dosyalar\xC4\xB1",
            "Gezgin \xC3\xB6nizleme veritaban\xC4\xB1",
            "Bellek d\xC3\xB6k\xC3\xBCmleri ve hata raporlar\xC4\xB1",
            "DirectX ve s\xC3\xBC" "r\xC3\xBC" "c\xC3\xBC g\xC3\xB6lgelendiri" "cileri",
            "E\xC5\x9Fler aras\xC4\xB1 g\xC3\xBC" "n" "celleme \xC3\xB6nbelle\xC4\x9Fi",

            "Performans", "Oyun", "Gizlilik", "G\xC3\xB6rsel", "Oyunlar", "FiveM", "Gecikme",
            "Valorant", "CS2", "Fortnite",
            "S\xC4\xB1" "f\xC4\xB1rla",
            "Varsay\xC4\xB1lanlar geri y\xC3\xBC" "klendi",
            "\xC4\xB0n" "ce ayarlar uyguland\xC4\xB1",
            "y\xC3\xB6neti" "ci gerekli",
            "Windows ile birlikte ba\xC5\x9Flat\xC4\xB1lacak",
            "Kabuk tepsi simgesini reddetti; k\xC3\xBC\xC3\xA7\xC3\xBCk etiketi normal davran\xC4\xB1\xC5\x9F\xC4\xB1n\u0131 s\xC3\xBCrd\xC3\xBCr",

            "RAM optimizasyonu", "Servisleri daha az svchost i\xC5\x9Flemine grupla",
            "Kurulu bellek", "Ge\xC3\xA7" "erli profil",
            "Profili uygula", "RAM profili uyguland\xC4\xB1",
            "Etkili olmas\xC4\xB1 i\xC3\xA7in yeniden ba\xC5\x9Flatma gerekir",
            "\xC3\x96zel",

            "Gecikme", "Gidi\xC5\x9F-d\xC3\xB6n\xC3\xBC\xC5\x9F s\xC3\xBCresi",
            "sim\xC3\xBCle", "titreme", "\xC3\x96rnek bekleniyor...",
            "DNS sa\xC4\x9Flay\xC4\xB1" "c\xC4\xB1s\xC4\xB1",
            "A\xC4\x9F" " " "a" "dapt\xC3\xB6rlerinizin kulland\xC4\xB1\xC4\x9F\xC4\xB1 \xC3\xA7\xC3\xB6z\xC3\xBCmleyici",
            "DNS \xC3\xB6nbelle\xC4\x9Fini temizle",
            "DNS g\xC3\xBC" "n" "cellendi", "DNS \xC3\xB6nbelle\xC4\x9Fi temizlendi",
            "Ba\xC4\x9Flant\xC4\xB1",
            "Aktif a\xC4\x9F" " " "a" "dapt\xC3\xB6r\xC3\xBC detaylar\xC4\xB1",
            "OPT\xC4\xB0M\xC4\xB0ZASYONLAR",

            "G\xC3\xB6rsel efektler", "Arka plan sahnesini ayarla",
            "Par\xC3\xA7" "a" "c\xC4\xB1klar", "Ba\xC4\x9Flant\xC4\xB1 \xC3\xA7izgileri",
            "Fare etkile\xC5\x9Fimi",
            "\xC3\x9Cst \xC4\xB1\xC5\x9F\xC4\xB1k", "I\xC5\x9F\xC4\xB1k tarama",
            "Par\xC3\xA7" "a" "c\xC4\xB1k say\xC4\xB1s\xC4\xB1", "Par\xC3\xA7" "a" "c\xC4\xB1k h\xC4\xB1z\xC4\xB1",
            "Genel", "Uygulama davran\xC4\xB1\xC5\x9F\xC4\xB1",
            "Lisans\xC4\xB1 hat\xC4\xB1rla", "Bildirimler",
            "Ba\xC5\x9Flang\xC4\xB1" "c" "ta \xC3\xA7" "al\xC4\xB1\xC5\x9Ft\xC4\xB1r",
            "Tepsi simgesine k\xC3\xBC\xC3\xA7\xC3\xBC" "lt",
            "Hesap", "\xC3\x87\xC4\xB1k\xC4\xB1\xC5\x9F yap",
            "Dil",

            "Donan\xC4\xB1m", "Yaz\xC4\xB1l\xC4\xB1m", "G\xC3\xBC" "venlik",
            "\xC4\xB0\xC5\x9Flemci", "\xC3\x87" "ekirdekler", "\xC4\xB0\xC5\x9F Par\xC3\xA7" "a" "c\xC4\xB1klar\xC4\xB1",
            "Temel saat h\xC4\xB1z\xC4\xB1",
            "Ekran kart\xC4\xB1", "VRAM", "S\xC3\xBC" "r\xC3\xBC" "c\xC3\xBC",
            "Toplam RAM",
            "RAM h\xC4\xB1z\xC4\xB1", "RAM yuvalar\xC4\xB1",
            "Anakart", "BIOS s\xC3\xBC" "r\xC3\xBCm\xC3\xBC", "BIOS modu",
            "G\xC3\xBC" "venli \xC3\x96ny\xC3\xBC" "kleme",
            "Etkin", "Devre d\xC4\xB1\xC5\x9F\xC4\xB1", "Desteklenmiyor",
            "Sanalla\xC5\x9Ft\xC4\xB1rma", "Kurulum tarihi", "DirectX",
            "Ekran \xC3\xA7\xC3\xB6z\xC3\xBC" "n\xC3\xBCrl\xC3\xBC\xC4\x9F\xC3\xBC",
            "Sistem dili",
            "Monit\xC3\xB6r", "TPM", "Var", "Yok",
            "Var, haz\xC4\xB1r", "Var, etkin de\xC4\x9Fil", "Var, haz\xC4\xB1r de\xC4\x9Fil",
            "%d mod\xC3\xBCl / %s", "H\xC3\xA2l\xC3\xA2 arka planda \xC3\x87" "al\xC4\xB1\xC5\x9F\xC4\xB1yor \xE2\x80\x94 simge okun arkas\xC4\xB1nda gizli olabilir",

            "De\xC4\xB1\xC5\x9Fi\xC5\x9Flikleri uygula", "De\xC4\xB1\xC5\x9Fi\xC5\x9F yok", "Geri al", "Son uygulama \xC3\xB6ncesi de\xC4\xB1\xC5\x9Fler geri y\xC3\xBCklensin mi?", "Ayarlar geri al\xC4\xB1nd\xC4\xB1.", "Geri al\xC4\xB1nacak bir yedek yok.", "Ayarlar uygulanmadan \xC3\xB6nce yedek al\xC4\xB1nd\xC4\xB1.", "Oturumunuz kapat\xC4\xB1ld\xC4\xB1", "plan - tekrar ho\xC5\x9Fgeldiniz!",
            "Bellek k\xC4\xB1salt\xC4\xB1ld\xC4\xB1, \xC3\xB6nbellekler temizlendi",
            "Bu kategorideki t\xC3\xBCm ayarlar kapat\xC4\xB1ld\xC4\xB1",
            "De\xC4\xB1\xC5\x9Fi\xC5\x9Flik yok - %d ayar etkin",
            "%d ayar etkin - %d yaz\xC4\xB1lamad\xC4\xB1 (y\xC3\xB6netici olarak \xC3\xA7" "al\xC4\xB1\xC5\x9Ft\xC4\xB1r\xC4\xB1n)",
            "%d ayar etkin - yeniden ba\xC5\x9Flat\xC4\xB1r\xC4\xB1ma \xC3\xB6nerilir",
            "Ba\xC4\xB1nt\xC4\xB1 adapt\xC3\xB6r\xC3\xBC", "Yerel IP", "A\xC4\xB1 ge\xC3\xA7it",
            "Atland\xC4\xB1", "%s taran\xC4\xB1yor...", "%s temizleniyor...",
            "\xC3\x87\xC3\xB6z\xC3\xBC" "c\xC3\xBC \xC3\xB6nbelle\xC3\x87i temizlendi",
            "Bu ba\xC4\xB1nt\xC4\xB1 adapt\xC3\xB6r\xC3\xBCnde kullan\xC4\xB1lamaz",

            "L\xC3\xBCtfen lisans anahtar\xC4\xB1n\xC4\xB1z\xC4\xB1 girin", "Tekrar ho\xC5\x9Fgeldiniz, ",
            "%d / %d %s ayar etkin",
            "G\xC3\xBCvenli oturum kuruluyor", "Lisans b\xC3\xBCt\xC3\xBCnl\xC3\xBC\xC4\xB1\xC4\xB1 do\xC4\x9Frulan\xC4\xB1yor",
            "Mod\xC3\xBCller y\xC3\xBCkleniyor", "Aray\xC3\xBCz haz\xC4\xB1rlan\xC4\xB1yor",
            "Otomatik",
            "\xC3\x96m\xC3\xBCrl\xC3\xBCk", "Demo", "Hi\xC3\xA7" "bir zaman", "L\xC3\xBCtfen bir lisans anahtar\xC4\xB1 girin",

            "Etkin (donan\xC4\xB1m)", "Etkin (VBS)", "Etkin (Hyper-V)",
            "Donan\xC4\xB1mda kapal\xC4\xB1", "Desteklenmiyor (Legacy BIOS)", "UEFI", "Legacy (BIOS)",

            "A\xC3\x87IK KAYNAK", "Hakk\xC4\xB1nda", "S\xC3\xBCr\xC3\xBCm", "Lisans: MIT",
            "Bu derleme demo lisans ekran\xC4\xB1 i\xC3\xA7" "erir: her anahtar kabul edilir ve hi\xC3\xA7" "bir \xC5\x9F" "ey sunucuya do\xC4\x9Frulanmaz.",
            "Uygulama normal kullan\xC4\xB1" "c\xC4\xB1 olarak a\xC3\xA7\xC4\xB1l\xC4\xB1r; HKLM'ye yazan bir i\xC5\x9F istendi\xC4\x9Finde kendini \x22runas\x22 ile yeniden ba\xC5\x9Flat\xC4\xB1r ve o i\xC5\x9Fi y\xC3\xBCksek yetkiyle yapar.",
            "Kapat", "Depo",
            "S\xC3\xBCr\xC3\xBCm, ba\xC4\x9Flant\xC4\xB1lar ve bildirimler", "BA\xC4\x9ELANTILAR", "Kaynak kodu", "B\xC4\xB0LG\xC4\xB0LER", "Lisans",
            "Windows bildirimleri", "Sadece hatalar",
            "Sonu\xC4\xB1lar\xC4\xB1 g\xC3\xB6r", "Geri al", "Tekrar dene", "Tamam", "\xC3\x87" "al\xC4\xB1\xC4\xB1\xC5\x9F\xC4\xB1yor",

            // ---- Startup / Services sekmeleri ----
            // Ham UTF-8: kaynak /utf-8 ile derleniyor. \xNN kaçış dizileri,
            // hemen ardından gelen a-f gibi bir onaltılık harf olduğunda
            // MSVC'de '\x9Fa' gibi taşan tek bir kaçış olarak okunup bozulur.
            "Başlatma", "Hizmetler",
            "Oturum açıldığında çalışan programları yönet",
            "Windows arka plan hizmetlerini oyun için ayarla",
            "Başlatma öğesi bulunamadı", "Hizmet bulunamadı",
            "Ad", "Yayıncı", "Kapsam", "Komut", "Durum", "Başlangıç türü",
            "HKCU \\ Run", "HKCU \\ RunOnce", "HKLM \\ Run", "HKLM \\ RunOnce",
            "HKCU \\ Policy \\ Explorer \\ Run", "HKLM \\ Policy \\ Explorer \\ Run",
            "Başlangıç klas\xC3\xB6r\xC3\xBC (kullanıcı)", "Başlangıç klas\xC3\xB6r\xC3\xBC (tüm kullanıcılar)",
            "HKCU \\ Winlogon", "HKLM \\ Winlogon",
            "Düşük", "Orta", "Yüksek", "Bilinmiyor",
            "Otomatik", "Otomatik (Gecikmeli)", "Elle", "Devre dışı", "Bilinmiyor", "Çalışıyor",
            "Etkinleştir", "Devre dışı bırak", "Kaldır", "Yenile", "Tara",
            "Oyun profilini uygula", "Varsayılanlara dön",
            "Oyun profili uygulandı", "Varsayılanlar geri yüklendi",
            "Sistem koruması kapalı - Sistem Özellikleri'nden aç",
            "HKLM kayıtları için yönetici yetkisi gerekir",
            "Hizmetleri değiştirmek için yönetici yetkisi gerekir",
            "TENGRİ otobaşlatma", "Kaydın registry'de bulunduğu konum",
            "Windows varsayılanından farklı", "Oturum açma değeri - yalnız listelenir, kapatılırsa kilitlenirsiniz",
            "Güvenli", "Dikkat", "Riskli",
            "Tümü", "Sistem",

            // ---- Güncellemeler (otomatik sürüm denetimi) ----
            "Güncellemeler", "Otomatik güncelleme denetimi",
            "GitHub Releases'i açılışta ve her 24 saatte bir denetler",
            "Denetleniyor...", "Sürümünüz güncel", "Yeni sürüm mevcut: %s",
            "Henüz denetlenmedi", "Son denetim: %d sa önce", "Son denetim: az önce",
            "Şimdi denetle", "İndir", "Kur ve yeniden başlat", "Vazgeç",
            "İndiriliyor... %d%%", "Sağlama değeri doğrulanıyor...", "Doğrulandı: %s",
            "Uygulama kapanır, yeni sürüm açılır.",
            "Uygulama klasörüne yazılamıyor. İndirilen dosya şurada kalıyor: %s",
            "Güncelleme denetimi başarısız: %s", "%s / %s",
            "Ağ hatası", "Sunucu hatası", "Bu sürümde güncelleme bildirimi yok",
            "Bozuk güncelleme bildirimi", "Güvenilmeyen indirme sunucusu", "Dosya hatası",
            "Sağlama değeri uyuşmuyor", "Klasör yazmaya kapalı",
            "Güncelleme var", "%s sürümü yayınlandı.",
            "Güncelleme indirildi", "%s sürümü doğrulandı, kurmaya hazır.",
            "GÜNCELLEME MEVCUT", "GÜNCEL", "DENENİYOR",

            // ---- Sistem tepsisi: kapatma davranışı + sağ tık menüsü ----
            "Kapatınca arka planda çalışmaya devam et",
            "Kapat düğmesi pencereyi gizler; uygulama tepside çalışmayı sürdürür",
            "Pencereyi aç", "Sayfaya git", "Çık",

            // ---- TweakNames: 10 kategori x 8 satır ----
            // Performans
            "Maksimum güç performans planı", "Arka plan uygulamaları kapat",
            "G\xC3\xB6rsel efektleri optimize et", "SysMain'i kapat", "Hibernation'\xC4\xB1 kapat",
            "Sistem duyarl\xC4\xB1l\xC4\xB1\u011F\xC4\xB1", "G\xC3\xBC\xC3\xA7 kısıtlamas\xC4\xB1 kapat",
            "Bellek y\xC3\xB6netimi",
            // Oyun
            "Oyun Modu", "Tam ekran optimizasyonlar\xC4\xB1", "Donan\xC4\xB1m GPU zamanlamas\xC4\xB1",
            "Xbox Oyun \xC3\x87ubuk\xC4\xB1n\xC3\xBC kald\xC4\xB1r", "Ham fare giri\xC5\x9Fi", "0.5 ms zamanlay\xC4\xB1\xC3\xA7 \xC3\xA7\xC3\xB6z\xC3\xBCn\xC3\xBC\u011F",
            "Y\xC3\xBCksek CPU \xC3\xB6nceli\xC4\x9Fi", "Oyun Modu (eski oyunlar)",
            // Gizlilik
            "Telemetriyi kapat", "Etkinlik ge\xC3\xA7mişini kapat", "Reklam kimli\xC4\x9Fini kapat",
            "Konum izlemeyi kapat", "Cortana'y\xC4\xB1 kapat", "Geri bildirim isteklerini engelle",
            "\xC3\x96zelle\xC5\x9Ftirilmi\xC5\x9F deneyimler", "Hata raporlamay\xC4\xB1 kapat",
            // Görsel
            "Animasyonlar\xC4\xB1 kapat", "\xC3\x96zelli\xC4\x9Fi kapat", "Klasik sa\xC4\x9F t\xC4\xB1kl\xC3\xBC men\xC3\xBC",
            "Dosya uzant\xC4\xB1lar\xC4\xB1 g\xC3\xB6ster", "Gizli dosyalar\xC4\xB1 g\xC3\xB6ster",
            "G\xC3\xB6rev \xC3\xA7ubuk aramas\xC4\xB1n\xC4\xB1 gizle", "Kilit ekran\xC4\xB1 ipu\xC3\xA7lar\xC4\xB1 kapat",
            "Ba\xC5\x9Flang\xC4\xB1 gecikmesini kapat",
            // Oyunlar
            "Oyun g\xC3\xB6revi \xC3\xB6nceli\xC4\x9Fi", "Game DVR'\xC4\xB1 kapat", "Oyunlar i\xC3\xA7in saat h\xC4\xB1z\xC4\xB1",
            "\xC3\x87" "ekirdek ba\xC4\xB1na GPU DPC", "NVIDIA i\xC3\xB6k par\xC3\xA7" "ac\xC4\xB1k \xC3\xB6nceli\u011Fi",
            "Sayfa belle\xC4\x9Fini kapat", "B\xC3\xBCy\xC3\xBCk sistem \xC3\x96nbelle\xC4\x9Fi",
            "G/Ç \xC3\x87 sayfa kilidi s\xC4\xB1n\xC4\xB1r\xC4\xB1",
            // FiveM
            "FiveM g\xC3\xB6revi boost'u", "Sistem duyarl\xC4\xB1l\xC4\xB1\u011F\xC4\xB1 0", "A\xC4\x9F kısıtlamay\xC4\xB1 kapat",
            "Men\xC3\xBC g\xC3\xB6sterim gecikmesi 0", "H\xC4\xB1zl\xC4\xB1 uygulama sonland\xC4\xB1rma",
            "D\xC3\xBC\xC5\x9F\xC3\xBCk kanca zaman a\xC5\x9F\xC4\xB1m\xC4\xB1", "Servis kapatma zaman a\xC5\x9F\xC4\xB1m\xC4\xB1",
            "Pencere s\xC3\xBCr\xC3\xBCklemesini kapat",
            // Gecikme
            "Genel zamanlay\xC4\xB1\xC3\xA7 \xC3\xA7\xC3\xB6z\xC3\xBCn\xC3\xBC\u011F\xC3\xBC", "DPC g\xC3\xB6zet bek\xC3\xA7i kaymas\xC4\xB1",
            "\xC3\x96zel durum zinciri do\xC4\x9Frulamas\xC4\xB1", "Kesme y\xC3\xB6nlendirmesini kapat",
            "Win32 \xC3\xB6ncelik ayr\xC4\xB1m\xC4\xB1", "G\xC3\xBCvenilirlik zaman damgas\xC4\xB1 0",
            "LanmanServer gecikme d\xC3\xBCzeltmesi", "Fare giri\xC5\x9Fi gecikme d\xC3\xBCzeltmesi",
            // Valorant
            "Valorant s\xC3\xBCre\xC3\xA7 \xC3\xB6nceli\xC4\x9F" "i", "Riot istemcisini arka plana",
            "Vanguard'\xC4\xB1 arka plana", "Tam ekran optimizasyonlar\xC4\xB1n\xC4\xB1 kapat",
            "Oyun g\xC3\xB6revi zamanlamas\xC4\xB1", "D\xC3\xBC\xC5\x9F\xC3\xBCk kanca zaman a\xC5\x9F\xC4\xB1m\xC4\xB1",
            "A\xC4\x9F k\xC4\xB1s\xC4\xB1tlamay\xC4\xB1 kapat", "Sistem duyarl\xC4\xB1l\xC4\xB1\u011F\xC4\xB1 0",
            // CS2
            "CS2 s\xC3\xBCre\xC3\xA7 \xC3\xB6nceli\xC4\x9F" "i", "Steam istemcisini arka plana",
            "Steam web yard\xC4\xB1mc\xC4\xB1s\xC4\xB1n\xC4\xB1 arka plana", "Steam ge\xC3\xA7 y\xC3\xBCkleme kapat",
            "Oyun g\xC3\xB6revi zamanlamas\xC4\xB1", "Oyun Modu",
            "Game DVR'\xC4\xB1 kapat", "Ham fare giri\xC5\x9Fi",
            // Fortnite
            "Fortnite s\xC3\xBCre\xC3\xA7 \xC3\xB6nceli\xC4\x9F" "i", "Epic launcher'\xC4\xB1 arka plana",
            "Epic servislerini arka plana", "Tam ekran optimizasyonlar\xC4\xB1n\xC4\xB1 kapat",
            "Oyun g\xC3\xB6revi zamanlamas\xC4\xB1", "Oyun Modu",
            "Ham fare giri\xC5\x9Fi", "Sistem duyarl\xC4\xB1l\xC4\xB1\u011F\xC4\xB1 0",

            // ---- TweakDescs: aynı düzen ----
            "Gizli g\xC3\xBC\xC3\xA7 performans plan\xC4\xB1n\xC4\xB1 a\xC3\xA7", "Bo\xC5\x9Fta \xC3\xA7" "al\xC4\xB1\xC5\x9F" "an UWP uygulamalar\xC4\xB1n\xC4\xB1 durdur",
            "Yumu\xC3\xA7" "atma korunur, gerisi kapat\xC4\xB1l\xC4\xB1r", "Superfetch disk me\xC5\x9Fgaletmesini durdur",
            "hiberfil.sys disk alan\xC4\xB1n\xC4\xB1 bo\xC5\x9F" "alt", "\xC3\x96n plan i\xC5\x9Flerine \xC3\xB6ncelik ver",
            "\xC3\x87" "ekirdekleri tam h\xC4\xB1zda tut", "Sayfaland\xC4\xB1rma ve \xC3\xB6nbellek boyutlar\xC4\xB1n\xC4\xB1 ayarla",

            "Oyunlara kaynak \xC3\xB6nceli\u011Fi ver", "Tam ekran d\xC3\xB1\xC5\x9F modu i\xC3\xA7in kapat",
            "Daha d\xC3\xBC\xC5\x9Fk kare zamanlamas\xC4\xB1", "Kaplama ve DVR kayd\xC4\xB1n\xC4\xB1 kald\xC4\xB1r",
            "\xC4\xB0\xC5\x9F" "aret h\xC4\xB1zland\xC4\xB1rmas\xC4\xB1n\xC4\xB1 kapat", "Daha s\xC4\xB1k\u0131 kare pacalama",
            "Etkin oyun s\xC3\xBCrecini h\xC4\xB1zland\xC4\xB1r", "Eski \xC5\x9F\xC3\xBCr\xC3\xBClebilirlere Oyun Modu",

            "Tan\xC4\xB1lama verisi y\xC3\xBCklemelerini durdur", "Zaman \xC3\xA7izelgenini kaydetme",
            "Ki\xC5\x9Fiselle\xC5\x9Firilmi\xC5\x9F reklam izleme yok", "Sistem genelinde konumu engelle",
            "Sesli asistan\xC4\xB1 kapat", "Asla geri bildirim isteme",
            "Kullan\xC4\xB1m tabanl\xC4\xB1 ipu\xC3\xA7lar\xC4\xB1n\xC4\xB1 kapat", "\xC3\x87\xC3\xB6kme raporlar\xC4\xB1n\xC4\xB1 g\xC3\xB6nderme",

            "An\xC4\xB1nda pencere ge\xC3\xA7i\u015Fleri", "Dolu \xC3\xA7ubuk ve men\xC3\xBCler",
            "Tam sa\xC4\x9F t\u0131kl\xC4\xB1 men\xC3\xBCs\xC3\xBC", ".exe, .txt vb. dosyalar\xC4\xB1 daima g\xC3\xB6ster",
            "Gizli klas\xC3\xB6rleri a\xC3\xA7", "\xC3\x87ubuk alan\xC4\xB1n\xC4\xB1 geri kazan",
            "Kilit ekran\xC4\xB1n\xC4\xB1 temizle", "Ba\xC5\x9Flang\xC4\xB1 uygulamalar\xC4\xB1n\xC4\xB1 an\xC4\xB1nda ba\xC5\x9Flat",

            "GPU 8 / CPU 6 / y\xC3\xBCksek zamanlama", "Kay\xC4\xB1t ve FSE kipini kapat",
            "10000 tik, arka plan s\xC3\xBCn\xC4\xB1r\xC3\xBC yok", "S\xC3\xBCr\xC3\xBC" "c\xC3\xBC DPC'leri \xC3\xA7" "ekirdeklere da\xC4\x9Ft\xC4\xB1r",
            "nvlddmkm'yi \xC3\xB6ncelik 31'e \xC3\xA7\xC4\xB1kar", "\xC3\x87" "ekirdek kodunu fiziksel bellekte tut",
            "\xC3\x87" "al\xC4\xB1\xC5\x9Fma seti yerine dosya \xC3\xB6nbelle\xC4\x9Fini tercih et", "1 MB kilit s\xC4\xB1n\xC4\xB1r\xC4\xB1, daha b\xC3\xBCy\xC3\xBCk L2 ipucu",

            "FiveM i\xC3\xA7in tam oyun g\xC3\xB6revi profili", "CPU'nun %100'\xC3\xBCn\xC3\xBC \xC3\xB6n plan i\xC5\x9Flerine",
            "Paket/ms ba\xC4\x9F cap\xC4\xB1n\xC4\xB1 kald\xC4\xB1r", "An\xC4\xB1nda men\xC3\xBCler, bekleme yok",
            "Daha k\xC4\xB1sa as\xC4\xB1l\xC4\xB1 uygulama zaman a\xC5\x9F\xC4\xB1mlar\xC4\xB1", "5000 ms yerine 1000 ms",
            "Servisleri daha h\xC4\xB1zl\xC4\xB1 kapat", "Daha hafif pencere hareketi",

            "0.5 ms zamanlay\xC4\xB1\xC3\xA7 isteklerini kabul et", "DPC g\xC3\xB6zet profilini gev\xC5\x9Flet",
            "SEHOP zincir kontrollerini atla", "Kesmeleri kendi \xC3\xA7" "ekirde\xC4\x9Finde tut",
            "0x28 - k\xC4\xB1sa, sabit kuantumlar", "G\xC3\xBCvenilirlik \xC3\xB6rnekleme yaz\xC4\xB1m\xC4\xB1n\xC4\xB1 durdur",
            "Payla\xC5\x9F\xC4\xB1m ihlali beklemesi yok", "AAP e\xC5\x9Fi\xC4\x9Fi + \xC3\xB6zellik ayarlar\xC4\xB1",

            "CPU, G/\xC3\x87 ve sayfa \xC3\xB6nceli\xC4\x9F" "i 1/1/6", "Lob istemcisine bo\xC5\x9Fta \xC3\xB6nceli\xC4\x9F" "i",
            "vgc ve vgtray kritik yoldan \xC3\xA7\xC4\xB1kar", "Kenarl\xC4\xB1kl\xC4\xB1 DWM de\xC4\x9F\xC4\xB1\xC5\x9Fini kald\xC4\xB1r",
            "GPU 8 / CPU 6 / y\xC3\xBCksek zamanlama", "5000 ms yerine 1000 ms",
            "10 paket/ms ba\xC4\x9F cap\xC4\xB1n\xC4\xB1 kald\xC4\xB1r", "CPU'nun %100'\xC3\xBCn\xC3\xBC \xC3\xB6n plan i\xC5\x9Flerine",

            "cs2.exe'yi zamanlay\xC4\xB1" "c\xC4\xB1 \xC3\xBCst\xC3\xBCne \xC3\xA7\xC4\xB1kar", "Steam men\xC3\xBCleri CPU i\xC3\xA7in yar\xC4\xB1\xC5\x9Fmas\xC4\xB1n\xC4\xB1 b\xC3\xB6r",
            "Web yard\xC4\xB1mc\xC4\xB1 sayfalar\xC4\xB1 G/\xC3\x87 i\xC3\xA7in yar\xC4\xB1\xC5\x9Fmas\xC4\xB1n\xC4\xB1 b\xC3\xB6r", "K\xC3\xBCt\xC3\xBCphaneyi a\xC3\xA7\xC4\xB1l\xC4\xB1\u015Fta tam y\xC3\xBCkle",
            "GPU 8 / CPU 6 / y\xC3\xBCksek zamanlama", "Oyunu Windows zamanlas\xC4\xB1n",
            "Kaplama ve DVR kayd\xC4\xB1n\xC4\xB1 kald\xC4\xB1r", "\xC4\xB0\xC5\x9F" "aret h\xC4\xB1zland\xC4\xB1rmas\xC4\xB1n\xC4\xB1 kapat",

            "CPU, G/\xC3\x87 ve sayfa \xC3\xB6nceli\xC4\x9F" "i 1/1/6", "Launcher indirmeleri oyuna y\xC3\xBCnsin",
            "EOS ve web yard\xC4\xB1mc\xC4\xB1 oyuna y\xC3\xBCnsin", "Kenarl\xC4\xB1kl\xC4\xB1 DWM de\xC4\x9F\xC4\xB1\xC5\x9Fini kald\xC4\xB1r",
            "GPU 8 / CPU 6 / y\xC3\xBCksek zamanlama", "Oyunu Windows zamanlas\xC4\xB1n",
            "\xC4\xB0\xC5\x9F" "aret h\xC4\xB1zland\xC4\xB1rmas\xC4\xB1n\xC4\xB1 kapat", "CPU'nun %100'\xC3\xBCn\xC3\xBC \xC3\xB6n plan i\xC5\x9Flerine",

            // ---- NetNames / NetDescs: 6 satır ----
            "TCP otomatik ayarlama", "Nagle algoritmas\xC4\xB1n\xC4\xB1 kapat", "A\xC4\x9F k\xC4\xB1s\xC4\xB1tlama dizini",
            "QoS paket zamanlay\xC4\xB1" "c\xC4\xB1", "B\xC3\xBCy\xC3\xBCk g\xC3\xB6nderme \xC3\xBCst\xC3\xBCn\xC3\xBC\u011F", "Kesme yumu\xC5\x9F" "atma",

            "En iyi alma penceresi \xC3\xB6l\xC3\xA7" "eklendirme", "K\xC3\xBC\xC3\xA7\xC3\xBCk paketler i\xC3\xA7in daha d\xC3\xBC\xC5\x9Fk gecikme",
            "\xC3\x87oklu ortam k\xC4\xB1s\xC4\xB1tlamas\xC4\xB1n\xC4\xB1 kald\xC4\xB1r", "QoS i\xC3\xA7in bant geni\xC5\x9Fi\xC4\x9Fi ay\xC4\xB1rma",
            "Adapt\xC3\xB6rlerde LSO'yu kapat", "Adapt\xC3\xB6re \xC3\xB6zel, genel olarak ayarlanamaz",
        };

        // İki tablo konumsaldır: g_en'in N. girdisi, g_tr'nin N. girdisinin çevirisidir.
        // C++ bunu çalışma zamanında zorlamaz — eksik bir dize ancak günler sonra tek
        // bir ayarın yanlış Türkçe etiketiyle ortaya çıkar. _COUNT lang.hpp içinde elle
        // tutulduğu için üçü de birbiriyle örtüşmek zorundadır; bu doğrulamalar,
        // uyuşmazlığı gizemli bir hata olmaktan çıkarıp derleme hatasına dönüştürür.
        static_assert(sizeof(g_en) / sizeof(g_en[0]) == S::_COUNT,
                      "g_en is out of sync with S::_COUNT");
        static_assert(sizeof(g_tr) / sizeof(g_tr[0]) == S::_COUNT,
                      "g_tr is out of sync with S::_COUNT");
        static_assert(sizeof(g_en) / sizeof(g_en[0]) == sizeof(g_tr) / sizeof(g_tr[0]),
                      "EN and TR tables have different lengths");
    }

    void Set(Id id)   { g_lang = (id == TR) ? TR : EN; }
    Id   Current()    { return g_lang; }

    const char* Get(int key)
    {
        if (key < 0 || key >= S::_COUNT) return "???";
        return g_lang == TR ? g_tr[key] : g_en[key];
    }

    namespace
    {
        // Tercih dosyası, lisans kodu için zaten oluşturulan klasörün içine yazılır; tek
        // baytlık bir tercih için ikinci bir dizin kaydına gerek kalmaz.
        std::wstring PrefPath()
        {
            wchar_t dir[MAX_PATH] = {};
            if (GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH) == 0) return {};
            std::wstring path = std::wstring(dir) + L"\\" + brand::kAppDataFolder + L"\\lang.dat";
            CreateDirectoryW((std::wstring(dir) + L"\\" + brand::kAppDataFolder).c_str(), nullptr);
            return path;
        }
    }

    void Load()
    {
        const std::wstring path = PrefPath();
        if (path.empty()) return;

        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return;

        char buf[8] = {};
        DWORD n = 0;
        const bool ok = ReadFile(f, buf, sizeof(buf) - 1, &n, nullptr) && n >= 1;
        CloseHandle(f);
        if (!ok) return;

        g_lang = (buf[0] == '1') ? TR : EN;
    }

    void Save()
    {
        const std::wstring path = PrefPath();
        if (path.empty()) return;

        HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return;

        const char b = (g_lang == TR) ? '1' : '0';
        DWORD n = 0;
        WriteFile(f, &b, 1, &n, nullptr);
        CloseHandle(f);
    }
}
