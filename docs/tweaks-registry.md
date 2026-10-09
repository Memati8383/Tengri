# İnce ayarların dokunduğu registry konumları

Bu dosya `tools\make_tweak_table.ps1` ile üretilir — elle düzenleme.
Kaynak: `src\core\tweaks.cpp` ve `src\core\regpack.cpp`. Kod değişirse tablo
yeniden üretilir, böylece buradaki liste kodun yazdığı şeyden ayrılamaz.

| # | Ayar | Kategori | Registry konumu |
|---|------|----------|-----------------|
| 1 | Maksimum güç performans planı | Performans | komut: powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c<br>komut: powercfg /setactive 381b4222-f694-41f0-9685-ff5bb260df2e |
| 2 | Arka plan uygulamaları kapat | Performans | HKCU\Software\Microsoft\Windows\CurrentVersion\BackgroundAccessApplications<br>deger: GlobalUserDisabled |
| 3 | Görsel efektleri optimize et | Performans | HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\VisualEffects<br>deger: VisualFXSetting |
| 4 | SysMain'i kapat | Performans | komut: sc config SysMain start= disabled<br>komut: sc stop SysMain<br>komut: sc config SysMain start= auto |
| 5 | Hibernation'ı kapat | Performans | komut: powercfg /hibernate off<br>komut: powercfg /hibernate on |
| 6 | Sistem duyarlılığı | Performans | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile<br>deger: SystemResponsiveness |
| 7 | Güç kısıtlaması kapat | Performans | HKLM\SYSTEM\CurrentControlSet\Control\Power\PowerThrottling<br>deger: PowerThrottlingOff |
| 8 | Bellek yönetimi | Performans | HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management<br>deger: DisablePagingExecutive |
| 9 | Oyun Modu | Oyun | HKCU\Software\Microsoft\GameBar<br>deger: AutoGameModeEnabled |
| 10 | Tam ekran optimizasyonları | Oyun | HKCU\System\GameConfigStore<br>deger: GameDVR_FSEBehaviorMode |
| 11 | Donanım GPU zamanlaması | Oyun | HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers<br>deger: HwSchMode |
| 12 | Xbox Oyun Çubuğu'nu kaldır | Oyun | HKCU\System\GameConfigStore<br>HKCU\Software\Microsoft\Windows\CurrentVersion\GameDVR<br>deger: GameDVR_Enabled<br>deger: AppCaptureEnabled |
| 13 | Ham fare girişi | Oyun | HKCU\Control Panel\Mouse<br>HKCU\Control Panel\Mouse<br>deger: MouseSpeed<br>deger: MouseThreshold1<br>deger: MouseThreshold2<br>deger: MouseSpeed<br>deger: MouseThreshold1<br>deger: MouseThreshold2 |
| 14 | 0.5 ms zamanlayıcı çözünürlüğü | Oyun | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile<br>deger: SystemResponsiveness |
| 15 | Yüksek CPU önceliği | Oyun | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games<br>deger: Priority |
| 16 | Oyun Modu (eski oyunlar) | Oyun | HKCU\Software\Microsoft\GameBar<br>deger: AllowAutoGameMode |
| 17 | Telemetriyi kapat | Gizlilik | HKLM\SOFTWARE\Policies\Microsoft\Windows\DataCollection<br>deger: AllowTelemetry |
| 18 | Etkinlik geçmişini kapat | Gizlilik | HKLM\SOFTWARE\Policies\Microsoft\Windows\System<br>HKLM\SOFTWARE\Policies\Microsoft\Windows\System<br>deger: EnableActivityFeed<br>deger: PublishUserActivities |
| 19 | Reklam kimliğini kapat | Gizlilik | HKCU\Software\Microsoft\Windows\CurrentVersion\AdvertisingInfo<br>deger: Enabled |
| 20 | Konum izlemeyi kapat | Gizlilik | deger: Value<br>deger: Value |
| 21 | Cortana'yı kapat | Gizlilik | HKLM\SOFTWARE\Policies\Microsoft\Windows\Windows Search<br>deger: AllowCortana |
| 22 | Geri bildirim isteklerini engelle | Gizlilik | HKCU\Software\Microsoft\Siuf\Rules<br>deger: NumberOfSIUFInPeriod |
| 23 | Özelleştirilmiş deneyimler | Gizlilik | HKCU\Software\Microsoft\Windows\CurrentVersion\Privacy<br>deger: TailoredExperiencesWithDiagnosticDataEnabled |
| 24 | Hata raporlamayı kapat | Gizlilik | HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting<br>deger: Disabled |
| 25 | Animasyonları kapat | Görsel | HKCU\Control Panel\Desktop\WindowMetrics<br>deger: MinAnimate<br>deger: UserPreferencesMask |
| 26 | Saydamlığı kapat | Görsel | HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize<br>deger: EnableTransparency |
| 27 | Klasik sağ tık menüsü | Görsel | (yok) |
| 28 | Dosya uzantıları göster | Görsel | HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced<br>deger: HideFileExt |
| 29 | Gizli dosyaları göster | Görsel | HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced<br>deger: Hidden |
| 30 | Görev çubuğu aramasını gizle | Görsel | HKCU\Software\Microsoft\Windows\CurrentVersion\Search<br>deger: SearchboxTaskbarMode |
| 31 | Kilit ekranı ipuçlarını kapat | Görsel | HKCU\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager<br>deger: RotatingLockScreenOverlayEnabled |
| 32 | Başlangıç gecikmesini kapat | Görsel | HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Serialize<br>deger: StartupDelayInMSec |
| 33 | Oyun görevi önceliği | Oyunlar | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games<br>deger: GPU Priority<br>deger: Priority<br>deger: Scheduling Category<br>deger: SFIO Priority |
| 34 | Game DVR'ı kapat | Oyunlar | HKCU\System\GameConfigStore<br>HKLM\SOFTWARE\Policies\Microsoft\Windows\GameDVR<br>deger: GameDVR_Enabled<br>deger: GameDVR_FSEBehaviorMode<br>deger: AllowGameDVR |
| 35 | Oyunlar için saat hızı | Oyunlar | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games<br>deger: Affinity<br>deger: Background Only<br>deger: Clock Rate |
| 36 | Çekirdek başına GPU DPC | Oyunlar | HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers<br>HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\Power<br>HKLM\SYSTEM\CurrentControlSet\Services\nvlddmkm<br>HKLM\SYSTEM\CurrentControlSet\Services\nvlddmkm\NVAPI<br>HKLM\SYSTEM\CurrentControlSet\Services\nvlddmkm\Global\NVTweak<br>deger: RmGpsPsEnablePerCpuCoreDpc<br>deger: RmGpsPsEnablePerCpuCoreDpc<br>deger: RmGpsPsEnablePerCpuCoreDpc<br>deger: RmGpsPsEnablePerCpuCoreDpc<br>deger: RmGpsPsEnablePerCpuCoreDpc |
| 37 | NVIDIA iş parçacığı önceliği | Oyunlar | HKLM\SYSTEM\CurrentControlSet\Services\nvlddmkm\Parameters<br>deger: ThreadPriority |
| 38 | Sayfa belleğini kapat | Oyunlar | HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management<br>deger: DisablePagingExecutive |
| 39 | Büyük sistem önbelleği | Oyunlar | HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management<br>deger: LargeSystemCache |
| 40 | G/Ç sayfa kilidi sınırı | Oyunlar | HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management<br>deger: IoPageLockLimit<br>deger: PoolUsageMaximum<br>deger: SecondLevelDataCache<br>deger: SessionPoolSize<br>deger: SessionViewSize<br>deger: PagedPoolSize<br>deger: PagedPoolQuota<br>deger: NonPagedPoolQuota<br>deger: NonPagedPoolSize<br>deger: SystemPages |
| 41 | FiveM görev yükseltmesi | FiveM | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games<br>deger: Affinity<br>deger: Background Only<br>deger: Clock Rate<br>deger: GPU Priority<br>deger: Priority<br>deger: Scheduling Category<br>deger: SFIO Priority |
| 42 | Sistem duyarlılığı 0 | FiveM | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile<br>deger: SystemResponsiveness |
| 43 | Ağ kısıtlamasını kapat | FiveM | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile<br>deger: NetworkThrottlingIndex |
| 44 | Menü gösterim gecikmesi 0 | FiveM | HKCU\Control Panel\Desktop<br>deger: MenuShowDelay |
| 45 | Hızlı uygulama sonlandırma | FiveM | HKCU\Control Panel\Desktop<br>deger: AutoEndTasks<br>deger: HungAppTimeout<br>deger: WaitToKillAppTimeout |
| 46 | Düşük kanca zaman aşımı | FiveM | HKCU\Control Panel\Desktop<br>deger: LowLevelHooksTimeout |
| 47 | Servis kapatma zaman aşımı | FiveM | HKCU\Control Panel\Desktop<br>HKLM\SYSTEM\CurrentControlSet\Control<br>deger: WaitToKillServiceTimeout<br>deger: WaitToKillServiceTimeout |
| 48 | Pencere sürüklemesini kapat | FiveM | HKCU\Control Panel\Desktop<br>deger: DragFullWindows<br>deger: ForegroundLockTimeout |
| 49 | Genel zamanlayıcı çözünürlüğü | Gecikme | HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\kernel<br>deger: GlobalTimerResolutionRequests |
| 50 | DPC gözetim bekçisi kayması | Gecikme | HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\kernel<br>deger: DpcWatchdogProfileOffset<br>deger: SeTokenSingletonAttributesConfig |
| 51 | Özel durum zinciri doğrulaması | Gecikme | HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\kernel<br>deger: DisableExceptionChainValidation<br>deger: KernelSEHOPEnabled |
| 52 | Kesme yönlendirmesini kapat | Gecikme | HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\kernel<br>deger: InterruptSteeringDisabled<br>deger: obcaseinsensitive |
| 53 | Win32 öncelik ayrımı | Gecikme | HKLM\SYSTEM\CurrentControlSet\Control\PriorityControl<br>deger: Win32PrioritySeparation |
| 54 | Güvenilirlik zaman damgası 0 | Gecikme | HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Reliability<br>deger: TimeStampInterval |
| 55 | LanmanServer gecikme düzeltmesi | Gecikme | HKLM\SYSTEM\CurrentControlSet\services\LanmanServer\Parameters<br>deger: autodisconnect<br>deger: Size<br>deger: EnableOplocks<br>deger: IRPStackSize<br>deger: SharingViolationDelay<br>deger: SharingViolationRetries |
| 56 | Fare girişi gecikme düzeltmesi | Gecikme | HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\PrecisionTouchPad<br>HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management<br>deger: AAPThreshold<br>deger: FeatureSettings |
| 57 | Valorant süreç önceliği | Valorant | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\VALORANT-Win64-Shipping.exe\PerfOptions<br>deger: CpuPriorityClass<br>deger: IoPriority<br>deger: PagePriority |
| 58 | Riot istemcisini arka plana | Valorant | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\RiotClientServices.exe\PerfOptions<br>HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\RiotClientUx.exe\PerfOptions<br>HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\RiotClientUxRender.exe\PerfOptions<br>deger: CpuPriorityClass<br>deger: IoPriority<br>deger: CpuPriorityClass<br>deger: IoPriority<br>deger: CpuPriorityClass<br>deger: IoPriority |
| 59 | Vanguard'ı arka plana | Valorant | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\vgc.exe\PerfOptions<br>HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\vgtray.exe\PerfOptions<br>deger: CpuPriorityClass<br>deger: IoPriority<br>deger: CpuPriorityClass<br>deger: IoPriority |
| 60 | Tam ekran optimizasyonlarını kapat | Valorant | HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers\VALORANT-Win64-Shipping.exe<br>deger: ~ DISABLEDXMAXIMIZEDWINDOWEDMODE HIGHDPIAWARE |
| 61 | Oyun görevi zamanlaması | Valorant | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games<br>deger: Priority<br>deger: Scheduling Category<br>deger: SFIO Priority<br>deger: GPU Priority |
| 62 | Düşük kanca zaman aşımı | Valorant | HKCU\Control Panel\Desktop<br>deger: LowLevelHooksTimeout |
| 63 | Ağ kısıtlamasını kapat | Valorant | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile<br>deger: NetworkThrottlingIndex |
| 64 | Sistem duyarlılığı 0 | Valorant | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile<br>deger: SystemResponsiveness |
| 65 | CS2 süreç önceliği | CS2 | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\cs2.exe\PerfOptions<br>deger: CpuPriorityClass<br>deger: IoPriority<br>deger: PagePriority |
| 66 | Steam istemcisini arka plana | CS2 | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\steam.exe\PerfOptions<br>deger: CpuPriorityClass<br>deger: IoPriority |
| 67 | Steam web yardımcısını arka plana | CS2 | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\steamwebhelper.exe\PerfOptions<br>deger: CpuPriorityClass<br>deger: IoPriority |
| 68 | Steam geç yükleme kapat | CS2 | HKCU\Software\Valve\Steam<br>deger: NoLazyLoading |
| 69 | Oyun görevi zamanlaması | CS2 | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games<br>deger: Priority<br>deger: Scheduling Category<br>deger: SFIO Priority<br>deger: GPU Priority |
| 70 | Oyun Modu | CS2 | HKCU\Software\Microsoft\GameBar<br>deger: AutoGameModeEnabled<br>deger: AllowAutoGameMode |
| 71 | Game DVR'ı kapat | CS2 | HKCU\System\GameConfigStore<br>HKCU\Software\Microsoft\Windows\CurrentVersion\GameDVR<br>deger: GameDVR_Enabled<br>deger: GameDVR_FSEBehaviorMode<br>deger: AppCaptureEnabled |
| 72 | Ham fare girişi | CS2 | HKCU\Control Panel\Mouse<br>deger: MouseSpeed<br>deger: MouseThreshold1<br>deger: MouseThreshold2 |
| 73 | Fortnite süreç önceliği | Fortnite | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\FortniteClient-Win64-Shipping.exe\PerfOptions<br>deger: CpuPriorityClass<br>deger: IoPriority<br>deger: PagePriority |
| 74 | Epic launcher'ı arka plana | Fortnite | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\EpicGamesLauncher.exe\PerfOptions<br>HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\EpicPortalLauncher.exe\PerfOptions<br>deger: CpuPriorityClass<br>deger: IoPriority<br>deger: CpuPriorityClass<br>deger: IoPriority |
| 75 | Epic servislerini arka plana | Fortnite | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\EpicWebHelper.exe\PerfOptions<br>HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\EOS.exe\PerfOptions<br>deger: CpuPriorityClass<br>deger: IoPriority<br>deger: CpuPriorityClass<br>deger: IoPriority |
| 76 | Tam ekran optimizasyonlarını kapat | Fortnite | HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers\FortniteClient-Win64-Shipping.exe<br>deger: ~ DISABLEDXMAXIMIZEDWINDOWEDMODE HIGHDPIAWARE |
| 77 | Oyun görevi zamanlaması | Fortnite | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games<br>deger: Priority<br>deger: Scheduling Category<br>deger: SFIO Priority<br>deger: GPU Priority |
| 78 | Oyun Modu | Fortnite | HKCU\Software\Microsoft\GameBar<br>deger: AutoGameModeEnabled<br>deger: AllowAutoGameMode |
| 79 | Ham fare girişi | Fortnite | HKCU\Control Panel\Mouse<br>deger: MouseSpeed<br>deger: MouseThreshold1<br>deger: MouseThreshold2 |
| 80 | Sistem duyarlılığı 0 | Fortnite | HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile<br>deger: SystemResponsiveness |
