#pragma once

namespace lang
{
    enum Id
    {
        EN = 0,
        TR = 1,
    };

    void        Set(Id id);
    Id          Current();
    const char* Get(int key);

    // Dil tercihi lisans dosyasının yanında saklanır; bu olmazsa her açılışta
    // seçim unutulur ve arayüz yine başlangıç diline dönerdi.
    void Load();
    void Save();
}

namespace S
{
    // Tabloların boyutları, aşağıdaki blok başlangıçları bunlara bağlı olduğu
    // için enum'dan önce tanımlanır. Metin dizileriyle birebir tutarlı olmalıdır.
    constexpr int kTweakCatCount = 7;   // tweak kategorileri (Performans .. Gecikme)
    constexpr int kTweakRows     = 8;   // kategori başına anahtar sayısı
    constexpr int kNetRows       = 6;   // ağ tweak sayısı

    enum Key
    {
        SecureLoader, Tagline, LicenseKey, Generate, RememberMe,
        ActivateLicense, DemoMode, AnyKeyAccepted, ContactingServer,
        ActivationFailed, SignedOut,

        Dashboard, Cleaner, Tweaks, Network, Settings, SystemInfo,
        DashOverview, CleanDesc, TweakDesc, NetDesc, SettDesc, SysInfoDesc,

        Processor, Memory, Storage, Health, Excellent, Good, NeedsAttention,
        LogicalThreads, OfGBInUse, FreeOfGB,
        ProcessorLoad, LiveLast30s, QuickOptimize, OneClickEvery,
        HealthScore, Optimizing, OptimizeNow,
        SystemOptimized,

        System, Subscription, OperatingSystem, Graphics, Computer, User, Uptime,
        Plan, Status, Active, Expires, HWID,

        ReadyToScan, SelectCategories, ScanComplete, JunkFound, ReadyToClean,
        AllClean, Spotless, Freed, Scan, Rescan, CleanNow, Categories,

        TempFiles, BrowserCache, WinUpdateCache, RecycleBin, PrefetchData,
        SystemLogs, ThumbnailCache, CrashDumps, ShaderCache, DeliveryOpt,
        TempFilesDesc, BrowserCacheDesc, WinUpdateCacheDesc, RecycleBinDesc,
        PrefetchDesc, SystemLogsDesc, ThumbnailDesc, CrashDumpsDesc,
        ShaderCacheDesc, DeliveryOptDesc,

        Performance, Gaming, Privacy, Visual, Games, FiveM, Delay,
        Reset, DefaultsRestored, TweaksApplied,
        NeedAdmin, StartupEnabled, TrayUnavailable,

        RamOptimization, RamOptDesc, InstalledRam, CurrentProfile,
        ApplyRamProfile, RamProfileApplied, RamRestartNote, Custom,

        Latency, RoundTrip, Simulated, Jitter, PingUnavailable,
        DnsProvider, ResolverUsed, FlushDnsCache, DnsUpdated, DnsFlushed,
        Connection, ActiveAdapter,
        Optimizations,

        VisualEffects, TuneBg, Particles, ConnectionLines, MouseInteraction,
        TopLight, LightSweep, ParticleCount, ParticleSpeed,
        General, AppBehaviour, RememberLicense, Notifications,
        LaunchStartup, MinimizeToTray, Account, SignOut,
        Language,

        HardwareInfo, SoftwareInfo, SecurityInfo,
        CPUName, CPUCores, CPUThreads, CPUClock,
        GPUName, GPUVRAM, GPUDriver,
        RAMTotal,
        RAMSpeed, RAMSlots, Motherboard, BIOSVersion, BIOSMode,
        SecureBoot, Enabled, Disabled, NotSupported,
        Virtualization, InstallDate, DirectXVersion,
        DisplayResolution, SystemLocale,
        // Yazılım kartı satırları. Sıra, lang.cpp'deki g_en / g_tr
        // tablolarındaki "Monitor", "TPM", ... bloğunun GELDIĞI yere denk
        // düşer.
        //
        // Bu blok ilk kez yanlış yere (LicenseShort'tan sonraya) eklenmişti.
        // Tablolar konumsaldır; iki hâli `static_assert` yalnız BOYUT
        // karşılaştırdığı için derleme sessiz geçti, uygulama ise onlarca
        // metni yanlış gösterdi (balon "License", RAM yuvaları "Windows
        // notifications" gibi). Kayma ancak tablo girdilerini sırayla
        // dökerek görülebiliyor.
        Monitor, TPM, TPMYes, TPMNo, TpmPresentYes, TpmReadyYes, TpmReadyNo,
        RAMModulesFmt, TrayHintBalloon,

        // sonradan eklenenler: bildirim metinleri, kategori başlıkları ve ağ satır
        // etiketleri; sıraları metin tablolarıyla birlikte korunmalıdır
        ApplyChanges, NoChanges,
        UndoChanges, UndoConfirm, UndoDone, UndoNoBackup, BackupTaken,
        SignedOutMsg, ActivatedMsg,
        OptimizedDesc,
        DefaultsRestoredDesc,
        NothingChangedFmt, AdminFailedFmt, RestartFmt,
        AdapterName, LocalIp, GatewayName,
        Skipped, ScanningFmt, CleaningFmt,
        DnsFlushedDesc,
        UnsupportedTweak,

        PleaseEnterKey, WelcomeBack, TweaksEnabledFmt,
        LoadingStep1, LoadingStep2, LoadingStep3, LoadingStep4,
        AutomaticDns,
        PlanLifetime, PlanDemo, ExpiresNever, LicenseKeyError,

        // Sanallaştırma durumları: donanım bilgisi bu bloğa bir sıra numarası
        // verir, etiket çizim anında çözülür; böylece dil değişimi anında yansır.
        VirtFirmware, VirtVbs, VirtHyperV, VirtDisabled,
        SecureBootNotSupported, BiosUefi, BiosLegacy,

        OpenSource, About, Version, LicenseLine,
        DemoLicenseNote, ElevationNote, Close, Repository,
        // Hakkında sayfası: alt başlık, bağlantı bölümü başlığı ve kaynak düğmesi.
        AboutDesc, LinksHeading, SourceCode, NoticesHeading, LicenseShort,

        // Windows bildirimleri: ayar etiketleri ve toast düğmeleri.
        WindowsNotifications, QuietMode,
        NotifBtnView, NotifBtnUndo, NotifBtnRetry, NotifBtnOk,
        WorkingOn,

        // Yeni sekmeler: Başlangıç Yöneticisi ve Hizmet Optimizatörü. Bu blok
        // FlatEnd'den hemen önce olmalı ki tweak/NET blok başlangıçları
        // kaymasın.
        Startup, Services,
        StartupDesc, ServicesDesc,
        StartupEmpty, ServicesEmpty,
        ColName, ColPublisher, ColScope, ColCommand, ColState, ColStartType,
        ScopeUserRun, ScopeUserRunOnce, ScopeMachineRun, ScopeMachineRunOnce,
        ImpactLow, ImpactMedium, ImpactHigh, ImpactUnknown,
        StAutomatic, StAutoDelayed, StManual, StDisabled, StUnknown, StRunning,
        BtnEnable, BtnDisable, BtnRemove, BtnRefresh, BtnRescan,
        BtnApplyGame, BtnRestoreGame,
        GameProfileApplied, GameProfileRestored, ProtectionDisabled,
        StartupAdminRequired, ServicesAdminRequired,
        StartupSelf, ScopeTooltip,
        SeveritySafe, SeverityCaution, SeverityRisky,
        FilterAll,

        // Otomatik sürüm denetimi (Ayarlar > Güncellemeler kartı, Hakkında
        // rozeti ve bildirimler). FlatEnd'den önce olmalı ki tweak blok
        // başlangıçları kaymasın.
        UpdatesHeading, AutoUpdateCheck, AutoUpdateCheckDesc,
        UpdateChecking, UpdateCurrent, UpdateAvailableFmt,
        UpdateNeverChecked, LastCheckFmt, JustNow,
        BtnCheckNow, BtnDownload, BtnApplyUpdate, BtnCancelDownload,
        UpdateDownloadingFmt, UpdateVerifying, UpdateReadyFmt, UpdateRestartNote,
        UpdateNotWritableFmt, UpdateFailedFmt, UpdateSizeFmt,
        FailNetwork, FailHttp, FailNotFound, FailBadManifest,
        FailTrustHost, FailIo, FailHashMismatch, FailNotWritable,
        UpdateFoundToast, UpdateFoundBodyFmt,
        UpdateReadyToast, UpdateReadyBodyFmt,
        BadgeUpdateAvailable, BadgeUpdateCurrent, BadgeUpdateChecking,

        // Sistem tepsisi: kapatma davranışı ayarı ve sağ tık menüsü başlıkları.
        // FlatEnd'den hemen önce olmalı (aşağıdaki blok başlangıçları buradan
        // türetiliyor).
        CloseToTray, CloseToTrayDesc, TrayOpen, TrayGoTo, TrayExit,

        // sonradan eklenenler: bildirim metinleri, kategori başlıkları ve ağ satır
        // başlangıçları buradan türetildiği için, sonrasına yeni bir düz anahtar
        // eklenmesi bütün tweak etiketlerini sessizce kaydırırdı. Daha önce de tam
        // olarak bu yüzden, hakkında sayfası metinleri buraya değil Repository'nin
        // altına eklenmişti.
        FlatEnd,

        // Blok tabloları: her biri tek tek sayılmış yüzlerce değer yerine, sıraya
        // göre adreslenen birer metin dizisidir. Bir tweak yalnızca kategori ve
        // satır bilgisini taşır; etiket yine de çizim anında çözülür, hiçbir yere
        // sabitlenmiş metin yazılmaz. Tweak kategorileri eklenip çıkarıldığında
        // sadece yukarıdaki boyut sabitleri güncellenir.
        //
        // Bu dört değer sıradan artışla numaralanamaz: enum arka arkaya kaldığı
        // hâlde tablolar 124 metin uzunluğunda olduğu için _COUNT, blok
        // girdilerini Get() içindeki sınır kontrolünün dışında bırakırdı.
        TweakNames = FlatEnd,                               // 7 kategori x 8 satır
        TweakDescs = TweakNames + kTweakCatCount * kTweakRows,
        NetNames   = TweakDescs + kTweakCatCount * kTweakRows,
        NetDescs   = NetNames + kNetRows,
        _COUNT     = NetDescs + kNetRows
    };

    // Tweak etiketleri (kategori, satır) çiftiyle adreslenir. Yardımcı adları Key
    // sonekiyle birlikte kullanılır; çünkü TweakDesc ve NetDesc adları zaten sekme
    // başlıklarına ait.
    inline int TweakNameKey(int cat, int row) { return TweakNames + cat * kTweakRows + row; }
    inline int TweakDescKey(int cat, int row) { return TweakDescs + cat * kTweakRows + row; }
    inline int NetNameKey(int row)            { return NetNames + row; }
    inline int NetDescKey(int row)            { return NetDescs + row; }
}

#define L(k) lang::Get(S::k)
