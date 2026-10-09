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
            "Processor load", "Live · last 30 seconds", "Quick optimize", "One click, every module",
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

        // Türkçe metinler ham UTF-8 olarak yazılır (/utf-8 ile derlenir);
        // \x kaçışı kullanmayın - ardından gelen a-f harfi kaçışa yutulur.
        const char* g_tr[] =
        {
            "Güvenli Yükleyici", "Açık Kaynak Sistem Optimize Edici",
            "LİSANS ANAHTARI", "Oluştur", "Beni hatırla",
            "Lisansı etkinleştir", "Demo modu",
            "herhangi bir anahtar kabul edilir",
            "Lisans sunucusuna bağlanıyor...",
            "Etkinleştirme başarısız",
            "Çıkış yapıldı",

            "Gösterge Paneli", "Temizleyici", "İnce Ayarlar",
            "Ağ", "Ayarlar", "Sistem Bilgisi",
            "Sisteminize genel bir bakış",
            "Gereksiz dosyaları bulup temizleyin",
            "Windows'ı performans için optimize edin",
            "Gecikmeyi düşürün, bağlantıyı optimize edin",
            "Uygulamanın görünüm ve davranışını özelleştirin",
            "Detaylı donanım ve yazılım bilgisi",

            "İşlemci", "Bellek", "Depolama", "Sağlık",
            "Mükemmel", "İyi", "İlgiye ihtiyaç var",
            "%d mantıksal iş parçacığı",
            "%.0f GB kullanımda", "C: üzerinde %.0f GB boş",
            "İşlemci yükü",
            "Canlı · son 30 saniye",
            "Hızlı optimizasyon", "Tek tıkla, tüm modüller",
            "sağlık puanı", "optimize ediliyor", "Şimdi optimize et",
            "Sistem optimize edildi",

            "Sistem", "Abonelik", "İşletim sistemi", "Grafik", "Bilgisayar",
            "Kullanıcı", "Çalışma süresi",
            "Plan", "Durum", "Aktif", "Bitiş", "HWID",

            "Taramaya hazır", "Aşağıdan kategorileri seçip tarayın.",
            "Tarama tamamlandı", "Gereksiz dosya bulundu", "temizlemeye hazır",
            "Temiz", "sisteminiz tertemiz", "temizlendi",
            "Tara", "Yeniden tara", "Şimdi temizle", "KATEGORİLER",

            "Geçici dosyalar", "Tarayıcı önbelleği",
            "Windows Update önbelleği", "Geri Dönüşüm Kutusu",
            "Önyükleme verileri", "Sistem günlükleri",
            "Küçük resim önbelleği",
            "Çökme dökümleri", "Gölgelendirici önbelleği",
            "Teslim Optimizasyonu",
            "Kullanıcı ve sistem geçici klasörleri",
            "Chrome, Edge, Firefox, Opera",
            "İndirilen güncelleme paketleri",
            "Silinmeyi bekleyen dosyalar",
            "Uygulama başlatma izleri",
            "Olay ve kurulum günlük dosyaları",
            "Gezgin önizleme veritabanı",
            "Bellek dökümleri ve hata raporları",
            "DirectX ve sürücü gölgelendiricileri",
            "Eşler arası güncelleme önbelleği",

            "Performans", "Oyun", "Gizlilik", "Görsel", "Oyunlar", "FiveM", "Gecikme",
            "Valorant", "CS2", "Fortnite",
            "Sıfırla",
            "Varsayılanlar geri yüklendi",
            "İnce ayarlar uygulandı",
            "yönetici gerekli",
            "Windows ile birlikte başlatılacak",
            "Kabuk tepsi simgesini reddetti; küçültme normal davranışını sürdürür",

            "RAM optimizasyonu", "Servisleri daha az svchost işlemine grupla",
            "Kurulu bellek", "Geçerli profil",
            "Profili uygula", "RAM profili uygulandı",
            "Etkili olması için yeniden başlatma gerekir",
            "Özel",

            "Gecikme", "Gidiş-dönüş süresi",
            "simüle", "titreme", "Örnek bekleniyor...",
            "DNS sağlayıcısı",
            "Ağ adaptörlerinizin kullandığı çözümleyici",
            "DNS önbelleğini temizle",
            "DNS güncellendi", "DNS önbelleği temizlendi",
            "Bağlantı",
            "Aktif ağ adaptörü detayları",
            "OPTİMİZASYONLAR",

            "Görsel efektler", "Arka plan sahnesini ayarla",
            "Parçacıklar", "Bağlantı çizgileri",
            "Fare etkileşimi",
            "Üst ışık", "Işık tarama",
            "Parçacık sayısı", "Parçacık hızı",
            "Genel", "Uygulama davranışı",
            "Lisansı hatırla", "Bildirimler",
            "Başlangıçta çalıştır",
            "Tepsi simgesine küçült",
            "Hesap", "Çıkış yap",
            "Dil",

            "Donanım", "Yazılım", "Güvenlik",
            "İşlemci", "Çekirdekler", "İş Parçacıkları",
            "Temel saat hızı",
            "Ekran kartı", "VRAM", "Sürücü",
            "Toplam RAM",
            "RAM hızı", "RAM yuvaları",
            "Anakart", "BIOS sürümü", "BIOS modu",
            "Güvenli Önyükleme",
            "Etkin", "Devre dışı", "Desteklenmiyor",
            "Sanallaştırma", "Kurulum tarihi", "DirectX",
            "Ekran çözünürlüğü",
            "Sistem dili",
            "Monitör", "TPM", "Var", "Yok",
            "Var, hazır", "Var, etkin değil", "Var, hazır değil",
            "%d modül / %s", "Hâlâ arka planda çalışıyor — simge okun arkasında gizli olabilir",

            "Değişiklikleri uygula", "Değişiklik yok", "Geri al", "Son uygulama öncesi değişiklikler geri yüklensin mi?", "Ayarlar geri alındı.", "Geri alınacak bir yedek yok.", "Ayarlar uygulanmadan önce yedek alındı.", "Oturumunuz kapatıldı", "plan - tekrar hoş geldiniz!",
            "Bellek kısaltıldı, önbellekler temizlendi",
            "Bu kategorideki tüm ayarlar kapatıldı",
            "Değişiklik yok - %d ayar etkin",
            "%d ayar etkin - %d yazılamadı (yönetici olarak çalıştırın)",
            "%d ayar etkin - yeniden başlatma önerilir",
            "Bağlantı adaptörü", "Yerel IP", "Ağ geçidi",
            "Atlandı", "%s taranıyor...", "%s temizleniyor...",
            "Çözücü önbelleği temizlendi",
            "Bu bağlantı adaptöründe kullanılamaz",

            "Lütfen lisans anahtarınızı girin", "Tekrar hoş geldiniz, ",
            "%d / %d %s ayar etkin",
            "Güvenli oturum kuruluyor", "Lisans bütünlüğü doğrulanıyor",
            "Modüller yükleniyor", "Arayüz hazırlanıyor",
            "Otomatik",
            "Ömürlük", "Demo", "Hiçbir zaman", "Lütfen bir lisans anahtarı girin",

            "Etkin (donanım)", "Etkin (VBS)", "Etkin (Hyper-V)",
            "Donanımda kapalı", "Desteklenmiyor (Legacy BIOS)", "UEFI", "Legacy (BIOS)",

            "AÇIK KAYNAK", "Hakkında", "Sürüm", "Lisans: MIT",
            "Bu derleme demo lisans ekranı içerir: her anahtar kabul edilir ve hiçbir şey sunucuya doğrulanmaz.",
            "Uygulama normal kullanıcı olarak açılır; HKLM'ye yazan bir iş istendiğinde kendini \"runas\" ile yeniden başlatır ve o işi yüksek yetkiyle yapar.",
            "Kapat", "Depo",
            "Sürüm, bağlantılar ve bildirimler", "BAĞLANTILAR", "Kaynak kodu", "BİLGİLER", "Lisans",
            "Windows bildirimleri", "Sadece hatalar",
            "Sonuçları gör", "Geri al", "Tekrar dene", "Tamam", "Çalışıyor",

            // ---- Startup / Services sekmeleri ----
            "Başlatma", "Hizmetler",
            "Oturum açıldığında çalışan programları yönet",
            "Windows arka plan hizmetlerini oyun için ayarla",
            "Başlatma öğesi bulunamadı", "Hizmet bulunamadı",
            "Ad", "Yayıncı", "Kapsam", "Komut", "Durum", "Başlangıç türü",
            "HKCU \\ Run", "HKCU \\ RunOnce", "HKLM \\ Run", "HKLM \\ RunOnce",
            "HKCU \\ Policy \\ Explorer \\ Run", "HKLM \\ Policy \\ Explorer \\ Run",
            "Başlangıç klasörü (kullanıcı)", "Başlangıç klasörü (tüm kullanıcılar)",
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
            "Görsel efektleri optimize et", "SysMain'i kapat", "Hibernation'ı kapat",
            "Sistem duyarlılığı", "Güç kısıtlaması kapat",
            "Bellek yönetimi",
            // Oyun
            "Oyun Modu", "Tam ekran optimizasyonları", "Donanım GPU zamanlaması",
            "Xbox Oyun Çubuğu'nu kaldır", "Ham fare girişi", "0.5 ms zamanlayıcı çözünürlüğü",
            "Yüksek CPU önceliği", "Oyun Modu (eski oyunlar)",
            // Gizlilik
            "Telemetriyi kapat", "Etkinlik geçmişini kapat", "Reklam kimliğini kapat",
            "Konum izlemeyi kapat", "Cortana'yı kapat", "Geri bildirim isteklerini engelle",
            "Özelleştirilmiş deneyimler", "Hata raporlamayı kapat",
            // Görsel
            "Animasyonları kapat", "Saydamlığı kapat", "Klasik sağ tık menüsü",
            "Dosya uzantıları göster", "Gizli dosyaları göster",
            "Görev çubuğu aramasını gizle", "Kilit ekranı ipuçlarını kapat",
            "Başlangıç gecikmesini kapat",
            // Oyunlar
            "Oyun görevi önceliği", "Game DVR'ı kapat", "Oyunlar için saat hızı",
            "Çekirdek başına GPU DPC", "NVIDIA iş parçacığı önceliği",
            "Sayfa belleğini kapat", "Büyük sistem önbelleği",
            "G/Ç sayfa kilidi sınırı",
            // FiveM
            "FiveM görev yükseltmesi", "Sistem duyarlılığı 0", "Ağ kısıtlamasını kapat",
            "Menü gösterim gecikmesi 0", "Hızlı uygulama sonlandırma",
            "Düşük kanca zaman aşımı", "Servis kapatma zaman aşımı",
            "Pencere sürüklemesini kapat",
            // Gecikme
            "Genel zamanlayıcı çözünürlüğü", "DPC gözetim bekçisi kayması",
            "Özel durum zinciri doğrulaması", "Kesme yönlendirmesini kapat",
            "Win32 öncelik ayrımı", "Güvenilirlik zaman damgası 0",
            "LanmanServer gecikme düzeltmesi", "Fare girişi gecikme düzeltmesi",
            // Valorant
            "Valorant süreç önceliği", "Riot istemcisini arka plana",
            "Vanguard'ı arka plana", "Tam ekran optimizasyonlarını kapat",
            "Oyun görevi zamanlaması", "Düşük kanca zaman aşımı",
            "Ağ kısıtlamasını kapat", "Sistem duyarlılığı 0",
            // CS2
            "CS2 süreç önceliği", "Steam istemcisini arka plana",
            "Steam web yardımcısını arka plana", "Steam geç yükleme kapat",
            "Oyun görevi zamanlaması", "Oyun Modu",
            "Game DVR'ı kapat", "Ham fare girişi",
            // Fortnite
            "Fortnite süreç önceliği", "Epic launcher'ı arka plana",
            "Epic servislerini arka plana", "Tam ekran optimizasyonlarını kapat",
            "Oyun görevi zamanlaması", "Oyun Modu",
            "Ham fare girişi", "Sistem duyarlılığı 0",

            // ---- TweakDescs: aynı düzen ----
            "Gizli güç performans planını aç", "Boşta çalışan UWP uygulamalarını durdur",
            "Yumuşatma korunur, gerisi kapatılır", "Superfetch'in diski meşgul etmesini durdur",
            "hiberfil.sys disk alanını boşalt", "Ön plan işlerine öncelik ver",
            "Çekirdekleri tam hızda tut", "Sayfalandırma ve önbellek boyutlarını ayarla",

            "Oyunlara kaynak önceliği ver", "Münhasır tam ekran için kapat",
            "Daha düşük kare zamanlaması", "Kaplama ve DVR kaydını kaldır",
            "İşaret hızlandırmasını kapat", "Daha sıkı kare zamanlaması",
            "Etkin oyun sürecini hızlandır", "Eski yürütülebilirler için Oyun Modu",

            "Tanılama verisi yüklemelerini durdur", "Zaman çizelgesini kaydetme",
            "Kişiselleştirilmiş reklam takibi yok", "Sistem genelinde konumu engelle",
            "Sesli asistanı kapat", "Asla geri bildirim isteme",
            "Kullanım tabanlı ipuçlarını kapat", "Çökme raporlarını gönderme",

            "Anında pencere geçişleri", "Dolu görev çubuğu ve menüler",
            "Tam sağ tık menüsü", ".exe, .txt vb. dosyaları daima göster",
            "Gizli klasörleri aç", "Görev çubuğu alanını geri kazan",
            "Kilit ekranını temizle", "Başlangıç uygulamalarını anında başlat",

            "GPU 8 / CPU 6 / yüksek zamanlama", "Kayıt ve FSE kipini kapat",
            "10000 tik, arka plan sınırı yok", "Sürücü DPC'lerini çekirdeklere dağıt",
            "nvlddmkm'yi öncelik 31'e çıkar", "Çekirdek kodunu fiziksel bellekte tut",
            "Çalışma seti yerine dosya önbelleğini tercih et", "1 MB kilit sınırı, daha büyük L2 ipucu",

            "FiveM için tam oyun görevi profili", "CPU'nun %100'ünü ön plan işlerine",
            "Paket/ms sınırını kaldır", "Anında menüler, bekleme yok",
            "Daha kısa asılı uygulama zaman aşımları", "5000 ms yerine 1000 ms",
            "Servisleri daha hızlı kapat", "Daha hafif pencere hareketi",

            "0.5 ms zamanlayıcı isteklerini kabul et", "DPC gözetim profilini gevşet",
            "SEHOP zincir kontrollerini atla", "Kesmeleri kendi çekirdeğinde tut",
            "0x28 - kısa, sabit kuantumlar", "Güvenilirlik örnekleme yazma işlemlerini durdur",
            "Paylaşım ihlali beklemesi yok", "AAP eşiği + özellik ayarları",

            "CPU, G/Ç ve sayfa önceliği 1/1/6", "Lob istemcisine boşta önceliği ver",
            "vgc ve vgtray kritik yoldan çıkar", "Kenarsız DWM değişimini kaldır",
            "GPU 8 / CPU 6 / yüksek zamanlama", "5000 ms yerine 1000 ms",
            "10 paket/ms sınırını kaldır", "CPU'nun %100'ünü ön plan işlerine",

            "cs2.exe'yi zamanlayıcı üstüne çıkar", "Steam menülerinin CPU için yarışmasını bırak",
            "Web yardımcı sayfalarının G/Ç için yarışmasını bırak", "Kütüphaneyi açılışta tam yükle",
            "GPU 8 / CPU 6 / yüksek zamanlama", "Oyunu Windows zamanlasın",
            "Kaplama ve DVR kaydını kaldır", "İşaret hızlandırmasını kapat",

            "CPU, G/Ç ve sayfa önceliği 1/1/6", "Launcher indirmeleri oyuna yol versin",
            "EOS ve web yardımcıları oyuna yol versin", "Kenarsız DWM değişimini kaldır",
            "GPU 8 / CPU 6 / yüksek zamanlama", "Oyunu Windows zamanlasın",
            "İşaret hızlandırmasını kapat", "CPU'nun %100'ünü ön plan işlerine",

            // ---- NetNames / NetDescs: 6 satır ----
            "TCP otomatik ayarlama", "Nagle algoritmasını kapat", "Ağ kısıtlama dizini",
            "QoS paket zamanlayıcısı", "Büyük gönderme boşaltımı", "Kesme yumuşatma",

            "En iyi alma penceresi ölçeklendirmesi", "Küçük paketler için daha düşük gecikme",
            "Çoklu ortam kısıtlamasını kaldır", "QoS için bant genişliği ayırma",
            "Adaptörlerde LSO'yu kapat", "Adaptöre özel, genel olarak ayarlanamaz",
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
