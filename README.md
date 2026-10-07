# TENGRI — Sistem Optimize Edici

C++ ile yazılmış bir Windows sistem optimize edici. .NET yok, Electron yok — tek bir
~1.5 MB yerel çalıştırılabilir dosya, DirectX 11 üzerinde çizilen kendi monokrom
arayüzüyle.

Yazdığı her registry anahtarı kaynakta görünür: 56 anahtarın dokunduğu her yol
[docs/tweaks-registry.md](docs/tweaks-registry.md) içinde liste halinde. Bu tablo kaynaktan üretilir,
elle yazılmaz. İkna dosyasına güvenmek istemiyorsan kendin derle.

<div align="center">
  <img src="docs/screenshots/01-dashboard.png" alt="TENGRI gösterge paneli" width="820" />
</div>

## Gereksinimler

| | |
|---|---|
| İşletim sistemi | Windows 10 ve Windows 11 (x64) |
| Mimari | 64-bit |
| Disk | ~2 MB |
| Ek bağımlılık | Yok — .NET, Python veya çalışma zamanı gerekmez |
| Yönetici | Yalnızca ayar uygularken gerekir; açılışta gerekmez |

Windows 10 1809'dan eskisi desteklenmiyor. Bazı anahtarlar (`HwSchMode`,
`PowerThrottling`) yalnızca Windows 10 1903 ve sonrasında vardır; daha eski bir sürümde
açılıp kapatılsa bile etkisi görünmez.

## Kapsam dışılar

Bu birkaç şeyi **yapmaz**, ve yapmadığını açıkça söylemek bu aracın bir parçası:

- **Antivirüsü, güvenliği veya Windows güncellemesini değiştirmez.** Tek bir antivirüs
  ya da güvenlik özelliğini kapatmaz, hiçbir dosyayı karantinaya almaz.
- **Telemetri göndermez.** Lisans ekranı bir demodur; sunucuya hiçbir istek yapılmaz.
  Makine parmak izi hesaplanır ama hiçbir yere gönderilmez.
- **Güncelleme yok.** Çalışma zamanında indirme yok, arka planda kendini yenileme yok.
  (İlk çalıştırmada tek seferlik iki kayıt yazılır — bkz. [Windows bildirimleri](#windows-bildirimleri).)
- **Kullanıcı verisini okumaz.** Yalnızca [registry tablosunda](docs/tweaks-registry.md)
  listelenen anahtarlara yazar, temizleyicide de yalnızca kendi kategorilerinin saydığı
  geçici dosyaları siler.
- **Arka planda sessizce çalışmaz.** Ayar ancak sen "Uygula" dediğinde yazılır.

## Geri alma

Ayarlar gerçek registry değerlerini değiştirir. İki ayrı geri dönüş yolu var:

**1. Aracın kendi yedeği.** "Uygula" düğmesine bastığında dokunulacak registry
anahtarları **önce** dışa aktarılır, tarih damgalı bir klasöre:

```
%LOCALAPPDATA%\TENGRI\backups\YYYYAAGG-SS-DDSS\
```

Sonrasında İnce Ayarlar sayfasındaki **Geri al** düğmesi bu yedeği geri içe aktarır.
Yedekleme yalnızca okuma yaptığı için UAC istemez; geri alma yazdığı için UAC ister.

Yedekler birikmez, her uygulamada yeni bir klasör açılır. Temizlemek istersen
`%LOCALAPPDATA%\TENGRI\backups` klasörünü silmen yeterli.

> **Neden önemli:** "Kapat" işlemi senin değerini geri yüklemez, kodlanmış varsayılanı
> geri yükler. Özelleştirdiğin bir değer varsa yedek olmadan kaybolur. Yedek tam olarak
> "uygulamadan önce ne vardı" halini saklar.

**2. Windows geri yükleme noktası.** Araç yalnızca kendi dokunduğu anahtarları saklar.
Sistemde yapacağın başka değişiklikler için ayrıca bir nokta oluştur:
`Windows + R` → `sysdm.cpl` → **Sistem koruma** → **Oluştur**.

İki yol birbirinin yerine geçmez; ikisini birlikte kullan.

## İstatistikler

<div align="center">
  <a href="https://github.com/Memati8383">
    <img src="https://github-readme-stats.vercel.app/api?username=Memati8383&show_icons=true&locale=tr&rank_icon=github&theme=dark" height="165" alt="GitHub istatistikleri" />
  </a>
  <a href="https://github.com/Memati8383">
    <img src="https://github-readme-stats.vercel.app/api/top-langs?username=Memati8383&layout=compact&locale=tr&theme=dark" height="165" alt="En çok kullanılan diller" />
  </a>
  <a href="https://github.com/Memati8383/Tengri">
    <img src="https://github-readme-stats.vercel.app/api/pin/?username=Memati8383&repo=Tengri&locale=tr&theme=dark" height="165" alt="TENGRI deposu" />
  </a>
</div>

## İndir

Hazır derlemeler [Releases](https://github.com/Memati8383/Tengri/releases) sayfasında.
Kurulum gerekmez, `TENGRI.exe` dosyasını çalıştırman yeterli.

Antivirüs uyarısı alırsan normaldir ve nedeni [SECURITY.md](SECURITY.md)'de açıklanıyor.

## Kaynaktan derle

```bat
git clone --recurse-submodules https://github.com/Memati8383/Tengri.git
cd Tengri
build.bat
```

### Yazı tipi

Arayüz **Inter** kullanır. Uygulamanın gerçekten çizdiği karakterlere (Basic Latin +
Türkçe + noktalama) göre alt kümelenmiş hâli `res/fonts` altında durur, üç ağırlık
toplam 75 KB'dır ve exe içine gömülür — yani font dosyası yanında olmadan da arayüz
doğru çizilir.

Inter, SIL Open Font License 1.1 ile lisanslıdır; tam metin
[`res/fonts/Inter-OFL.txt`](res/fonts/Inter-OFL.txt) içinde. Alt kümeleri
yeniden üretmek için:

```bat
python -m pip install fonttools
.\tools\make_font_subset.ps1 -Archive <Inter-*.zip>
.\tools\make_font_data.ps1
```

Yazı tipi değişikliklerinde tipografi ölçeği `src/gui/theme.hpp` içindedir
(`theme::size`, `theme::track`, `theme::ink`). Çağrı yerlerinde sayı yazma.

---

## Ekran görüntüleri

Giriş ekranı:

![Giriş ekranı](docs/screenshots/00-login.png)

Gösterge paneli — canlı CPU / RAM / disk kullanımı, 30 saniyelik gerçek CPU yükü grafiği
ve canlı durumdan hesaplanan sağlık puanı:

![Gösterge paneli](docs/screenshots/01-dashboard.png)

Temizleyici:

![Temizleyici](docs/screenshots/02-cleaner.png)

İnce ayarlar:

![İnce ayarlar](docs/screenshots/03-tweaks.png)

Ağ:

![Ağ](docs/screenshots/04-network.png)

Sistem bilgisi:

![Sistem bilgisi](docs/screenshots/05-system-info.png)

Ayarlar:

![Ayarlar](docs/screenshots/06-settings.png)

Hakkında:

![Hakkında](docs/screenshots/07-about.png)

---

## Çalıştırmadan önce

Antivirüsünüzün uyarı vermesi beklenir. TENGRI `HKEY_LOCAL_MACHINE` altına yazıyor ve
geçici `.reg` dosyalarını içe aktarmak için `reg.exe` çağırıyor. Bu tam olarak bir "registry temizleyici" zararlı yazılım ailesinin
davranış imzası, bu yüzden sezgisel motorlar imzasız çalıştırılabilirleri işaretliyor.
TENGRI imzasız ve tespitler bu yüzden beklenen bir sonuç. Ayrıntı:
[SECURITY.md](SECURITY.md) ve [Yükseltme](#yükseltme).

Ayar uygularken yönetici yetkisi istenir, ancak uygulamayı açarken ve sistem bilgisi
okurken **hiç sorulmaz**. Ayrıntı: [Yükseltme](#yükseltme).

---

## Özellikler

### Ekranlar

| Ekran | Ne yapar |
|---|---|
| **Gösterge Paneli** | Canlı sistem metrikleri, CPU geçmişi grafiği, sağlık puanı, tek tıkla optimizasyon, sistem ve abonelik özeti |
| **Temizleyici** | 10 kategoride asenkron tarama ve silme, kategori seçimi, tarama/temizleme durumu |
| **İnce Ayarlar** | 7 kategori, 56 anahtar; her anahtar başlangıçta registry'den okunur |
| **Ağ** | Gerçek ICMP gecikme ölçümü, DNS geçişi, bağlantı ayrıntıları, 6 ağ anahtarı |
| **Sistem Bilgisi** | Donanım, yazılım ve güvenlik ayrıntıları |
| **Ayarlar** | Görsel efektler, davranış, dil, hesap, bildirimler |
| **Hakkında** | Sürüm, lisans, uyarılar, sosyal ve kaynak kod bağlantıları |

---

### Gösterge Paneli

| Özellik | Ayrıntı |
|---|---|
| İşlemci yükü | Win32 `GetSystemTimes` üzerinden canlı, çekirdek süresinden boşta süre ayrı tutularak hesaplanır |
| Bellek | `GlobalMemoryStatusEx` ile kullanılan / toplam GB |
| Depolama | Toplam ve boş alan; sağlık puanına girer |
| Sağlık puanı | 0-100; CPU yükü, RAM baskısı ve boş diskten canlı anlık görüntüyle hesaplanır |
| CPU geçmişi | Kayan 30 saniyelik gerçek yük grafiği (rastgele yürüyüş değil) |
| Hızlı optimize et | Çalışma kümesini kırpma + DNS önbelleğini boşaltma |
| Sistem özeti | İşletim sistemi, işlemci, ekran kartı, bilgisayar, kullanıcı, çalışma süresi |
| Abonelik | Plan, durum, bitiş, HWID |

---

### Temizleyici — 10 kategori

| Kategori | Ne siler | Özel davranış |
|---|---|---|
| Geçici dosyalar | Kullanıcı ve sistem geçici klasörleri | — |
| Tarayıcı önbelleği | Chrome, Edge, Firefox, Opera, Brave | **Firefox** profil başına, yalnızca `cache2`; profil kökü (oturumlar, yer imleri, eklentiler) asla silinmez |
| Windows Update önbelleği | İndirilmiş güncelleme paketleri | `wuauserv` ve `BITS` durdurulur, yalnızca biz durdurduysak geri başlatılır |
| Geri Dönüşüm Kutusu | Silinen dosyalar | — |
| Önyükleme verileri | Prefetch | — |
| Sistem günlükleri | Olay günlükleri | — |
| Küçük resim önbelleği | Küçük resim veritabanı | — |
| Çökme dökümleri | Bellek dökümleri ve hata raporları | — |
| Gölgelendirici önbelleği | DirectX ve sürücü önbelleği | — |
| Teslim Optimizasyonu | İndirme dağıtım önbelleği | — |

| Davranış | Ayrıntı |
|---|---|
| Bağlantı güvenliği | Junction ve sembolik bağlantılar (`FILE_ATTRIBUTE_REPARSE_POINT`) yaprak sayılır, içine girilmez; bağlantı döngüsü taramayı sınırsızlaştıramaz |
| İş parçacığı | Tarama ve silme arka plan iş parçacığında, arayüzü bloklamaz |
| Kategori seçimi | Her kategori ayrı onay kutusu; yalnızca işaretlenenler taranır |
| Tutarlılık | Günlük silme seçeneği kapatıldığında tarama da aynı şekilde uygulanır |

---

### İnce Ayarlar — 7 kategori, 56 anahtar

Her anahtar başlangıçta registry'den **gerçek** durumunu okur, böylece arayüz kaydedilmiş
bir ayar dosyası değil, gerçekte uygulanan şeyi gösterir.

**Uygula** yalnızca seçili kategoriye ve yalnızca gerçekten dokunduğun anahtarlara yazar.
Dokunulmamış bir kayıt, başka bir aracın sahip olduğu bir değerin üstüne asla geri
alınmaz. İki anahtarın ortak sahiplendiği değerler etkin olan anahtardan yeniden
dayatılır.

**Yazmadan önce yedek alınır.** Düğmeye bastığında dokunulacak anahtarlar
`%LOCALAPPDATA%\TENGRI\backups` altına `reg export` ile dışa aktarılır; sayfadaki
**Geri al** düğmesi bunu geri içe aktarır. Ayrıntı için [Geri alma](#geri-alma).

#### Performans

| Anahtar | Ne yapar |
|---|---|
| Maksimum güç performans planı | Gizli güç performans planını açar |
| Arka plan uygulamalarını kapat | Boşta çalışan UWP uygulamalarını durdurur |
| Görsel efektleri optimize et | Yumuşatma korunur, geri kalanı kaldırılır |
| SysMain'i kapat | Superfetch'in disk tıkamalarını durdurur |
| Hibernation'ı kapat | `hiberfil.sys` disk alanını boşaltır |
| Sistem duyarlılığı | Ön plan işlerine öncelik verir |
| Güç kısıtlaması kapat | Çekirdekleri tam saat hızında tutar |
| Bellek yönetimi | Sayfalama ve önbellek boyutlarını ayarlar |

#### Oyun

| Anahtar | Ne yapar |
|---|---|
| Oyun modu | Oyunları kaynaklarda önceliklendirir |
| Tam ekran optimizasyonları | Tam ekran dışı modu için devre dışı bırakır |
| Donanım GPU zamanlaması | Kare zamanlaması gecikmesini düşürür |
| Game Bar'ı kapat | Katman ve DVR yakalamasını kaldırır |
| Ham fare girdisi | İşaretçi hızlandırmasını kapatır |
| Zamanlayıcı çözünürlüğü | Daha sıkı kare zamanlaması sağlar |
| CPU önceliği yükselt | Etkin oyun sürecini güçlendirir |
| Oyun modu (eski başlıklar) | Oyun modunu eski çalıştırılabilirlere uygular |

#### Gizlilik

| Anahtar | Ne yapar |
|---|---|
| Telemetriyi kapat | Tanılama verisi yüklemelerini durdurur |
| Etkinlik geçmişini kapat | Zaman çizelgesinin kaydedilmesini engeller |
| Reklam kimliğini kapat | Kişiselleştirilmiş reklam izlemeyi kaldırır |
| Konum izlemeyi kapat | Sistem genelinde konumu engeller |
| Cortana'yı kapat | Sesli asistanı kapatır |
| Geri bildirim isteklerini engelle | Asla geri bildirim istemez |
| Kişiselleştirilmiş deneyimler | Kullanım tabanlı ipuçlarını devre dışı bırakır |
| Hata raporlamayı kapat | Çökme raporlarının gönderilmesini engeller |

#### Görsel

| Anahtar | Ne yapar |
|---|---|
| Animasyonları kapat | Anında pencere geçişleri sağlar |
| Saydamlığı kapat | Düz görev çubuğu ve menüler verir |
| Klasik sağ tık menüsü | Tam sağ tık menüsünü geri getirir |
| Dosya uzantılarını göster | `.exe`, `.txt` vb. dosyaları daima gösterir |
| Gizli dosyaları göster | Gizli klasörleri ortaya çıkarır |
| Görev çubuğu aramasını gizle | Görev çubuğu alanını geri kazanır |
| Kilit ekranı ipuçlarını kapat | Kilit ekranını temizler |
| Başlangıç gecikmesini kapat | Başlangıç uygulamalarını anında başlatır |

#### Oyunlar

| Anahtar | Ne yapar |
|---|---|
| Oyun görev önceliği | GPU 8 / CPU 6 / yüksek zamanlama |
| Game DVR'ı kapat | Kayıt ve FSE kipini kapatır |
| Saat hızı | 10000 tik, arka plan sınırı yok |
| Çekirdek başına GPU DPC | Sürücü DPC'lerini çekirdeklere dağıtır |
| NVIDIA iş parçacığı önceliği | `nvlddmkm`'yi öncelik 31'e çıkarır |
| Sayfalama yürütmesini kapat | Çekirdek kodunu fiziksel bellekte tutar |
| Büyük sistem önbelleği | Dosya önbelleğini çalışma kümesine tercih eder |
| IO sayfa kilit sınırı | 1 MB kilit sınırı, daha büyük L2 ipucu |

#### FiveM

| Anahtar | Ne yapar |
|---|---|
| FiveM görev boost | FiveM için tam oyun görev profili |
| SistemResponsiveness 0 | CPU'nun %100'ünü ön plan işlerine verir |
| Ağ kısıtlamasını kapat | Paket/ms başına 10 paketlik sınırı kaldırır |
| Anlık menüler | Menüler, açılış beklemeye gerek olmadan |
| Hızlı uygulama sonlandırma | Takılan uygulama zaman aşımlarını kısaltır |
| Düşük seviye kanca zaman aşımı | 5000 ms yerine 1000 ms |
| Servis sonlandırma zaman aşımı | Servisleri daha hızlı kapatır |
| Pencere sürüklemesini kapat | Daha hafif pencere hareketi |

#### Gecikme

| Anahtar | Ne yapar |
|---|---|
| Global zamanlayıcı çözünürlüğü | 0.5 ms zamanlayıcı isteklerini kabul eder |
| DPC watchdog profili | DPC gözlet profilini gevşetir |
| İstisna zinciri doğrulaması | SEHOP zincir kontrollerini atlar |
| Kesme yönlendirme | Kesmeleri kendi çekirdeğinde tutar |
| Win32 öncelik ayrımı | 0x28 - kısa, sabit kuantumlar |
| Güvenilirlik zaman damgası | Güvenilirlik örnekleme yazılarını durdurur |
| Paylaşım ihlali beklemesi yok | Paylaşım ihlali beklemesini kaldırır |
| Fare girdi gecikmesi düzeltmesi | AAP eşiği ve özellik ayarları |

---

### Ağ

| Özellik | Ayrıntı |
|---|---|
| Gecikme ölçümü | 1.1.1.1'e gerçek ICMP tur-çevrimi, arka plan iş parçacığında; ilk örnek düşene kadar grafik "bekliyor" der |
| Jitter | Ardışık ölçümler arası değişim |
| DNS sağlayıcısı | Otomatik / Cloudflare / Google / Quad9, `netsh` üzerinden; seçimden sonra çözümleyici boşaltılır |
| DNS önbelleğini temizle | Çözümleyici önbelleğini boşaltır |
| Bağlantı ayrıntıları | Aktif adaptörün adı, yerel IP, ağ geçidi |

**Ağ anahtarları**

| Anahtar | Ne yapar | Durum |
|---|---|---|
| TCP otomatik ayarlama | En iyi alma penceresi ölçeklendirmesi | Etkin |
| Nagle algoritmasını kapat | Küçük paketler için daha düşük gecikme | Etkin |
| Ağ kısıtlama indeksi | QoS paket zamanlayıcısı | Etkin |
| QoS paket zamanlayıcı | QoS için bant genişliği çift yönlü ayarlama | Etkin |
| Büyük gönderme kapatma | Adaptörlerde LSO'yu kapatır (v4 ve v6) | Etkin |
| Kesme moderasyonu | Adaptör sürücüsüne özel, genel karşılığı yok | **Gri** — sessizce işe yaramak yerine devre dışı gösterilir |

Adaptör seçimi döngü, tünel ve sanal NIC'leri yok sayar; bağlantı adları `netsh`'e
ulaşmadan önce temizlenir.

---

### RAM optimizasyon profili

Windows'un servisleri her servis için ayrı `svchost.exe` başlatmak yerine daha az sürece
toplamasını sağlar. 14 hazır profil:

| Profil | | Profil | |
|---|---|---|---|
| Default | 0 GB | 32 GB | 32 |
| 4 GB | 4 | 64 GB | 64 |
| 6 GB | 6 | 96 GB | 96 |
| 8 GB | 8 | 128 GB | 128 |
| 12 GB | 12 | 192 GB | 192 |
| 16 GB | 16 | 256 GB | 256 |
| 24 GB | 24 | 512 GB | 512 |

| Özellik | Ayrıntı |
|---|---|
| Otomatik öneri | Kurulu RAM'e en yakın profil önerilen olarak işaretlenir |
| Varsayılana dönüş | `Default` seçilince sistemin kendi değeri geri yüklenir |
| Canlı okuma | O an yazılı olan profil registry'den okunur |

---

### Sistem Bilgisi

| Alan | Kaynak | Not |
|---|---|---|
| İşlemci | Registry / `__cpuid` | Ad, fiziksel çekirdek, mantıksal iş parçacığı, taban saat |
| Ekran kartı | DXGI + display class anahtarı | Ad, VRAM, **sürücü sürümü** (DirectX registry değerinden değil, display class anahtarından) |
| DirectX | `GetVersionEx` | Sürüm |
| Toplam RAM | `GlobalMemoryStatusEx` | — |
| Anakart | Registry | Model |
| BIOS | Registry | Sürüm, **mod (UEFI / Legacy)** |
| Güvenlik | — | **Secure Boot durumu**, **sanalleştirme durumu** (firmware / VBS / Hyper-V) |
| Diğer | Registry | Windows kurulum tarihi, ekran çözünürlüğü, sistem dili |

RAM hızı ve slot sayısı `N/A` bildirir: bu bilgiler registry'de değil SMBIOS tip 17
tablolarındadır ve görünürlüğü makul ama yanlış bir sayı, dürüst bir boşluktan kötüdür.

---

### Ayarlar

| Bölüm | Seçenek |
|---|---|
| Görsel efektler | Parçacıklar, bağlantı çizgileri, fare etkileşimi, üst ışık, ışık süpürmesi |
| Parçacık ayarları | Parçacık sayısı (kaydırıcı), parçacık hızı (çarpan) |
| Genel | Lisansı hatırla, uygulama içi bildirimler, Windows bildirimleri, sadece hatalar, başlangıçta çalıştır, tepsi simgesine küçült |
| Dil | English / Türkçe — seçim kalıcı olarak saklanır |
| Hesap | Maskelenmiş lisans anahtarı, plan, bitiş, demo lisans bildirimi, çıkış yap |

| Davranış | Ayrıntı |
|---|---|
| Başlangıçta çalıştır | Gerçek bir `HKCU\...\Run` girdisi yazar ve geri okur, böylece anahtar kayamaz |
| Tepsi simgesine küçült | Gerçek kabuk ikonu; sol tık geri getirir, sağ tık Aç / Çıkış menüsü açar. Kabuk ikonu reddederse sıradan küçültmeye düşer |
| Diller | İngilizce ve Türkçe; tüm metinler, tweak etiketleri ve sistem durumları çizim anında çözülür |
| DPI | Ölçekleme tüm boyutlarda uygulanır; monitör değişiminde yeniden ölçeklenir |

---

### Windows bildirimleri

Uzun süren işler, yetki hataları ve ayar değişiklikleri Windows'un kendi bildirim
sistemiyle de haber verilir — uygulama kapalıyken de gelir, Eylem Merkezi'ne düşer.

Üç anahtar birbirinden bağımsızdır:

| Anahtar | Ne kapatır |
|---|---|
| Uygulama içi bildirimler | Pencerenin köşesindeki toaster'ları |
| Windows bildirimleri | Eylem Merkezi'ne düşen toast'ları |
| Sadece hatalar | İkisini birden — yalnızca hata ve uyarılar geçer |

Gönderilen olaylar: uzun süren işlerin sonucu (optimizasyon, tarama, temizlik, ayar
uygulama), yetki/hata uyarıları ve ayar değişiklikleri.

Toast'lar düğme taşır:

| Düğme | Davranış |
|---|---|
| Sonuçları gör | Uygulamayı açar ve **olayın kendi sayfasına** gider (Temizlik, Ağ, İnce Ayarlar…) |
| Geri al | **Önce onay ister.** Onaylanmadan registry'ye hiçbir şey yazılmaz |
| Tekrar dene | Yalnızca gerçekten saklanmış bir iş varsa görünür |
| Tamam | Sistem düğmesi; uygulamayı açmadan kapatır |

> **Geri al düğmesi:** bildirimden tek tıkla registry'ye dokunmak, o anda hangi
> pencereye baktığını bilmediğin bir durumda veri kaybı demektir. Bu yüzden düğme
> önce uygulamayı öne getirir ve onay penceresinde sorar.

**Kurulum.** Windows bildirimleri uygulamayı bir kısayol üzerinden tanır. İlk
çalıştırmada iki kayıt yazılır ve bir daha dokunulmaz:

```
HKCU\Software\Classes\AppUserModelId\TENGRI.SystemOptimizer
%APPDATA%\Microsoft\Windows\Start Menu\Programs\TENGRI.lnk
```

Kısayol elle silinirse yeniden kurulur. Ayarı kapatmak bu kayıtları silmez.

**Ayarı kapatmak** bildirimleri susturur, kayıtları silmez — bu yüzden tekrar açtığında
kurulumun baştan yapılması gerekmez.

> Rahatsız Etmeyin ve gece modu Windows'un kendi kararıdır; araç bu ayarların
> üzerine yazmaz. Sistem susturduğunda susturur, sadece Eylem Merkezi'ne kaydeder.

**Tek örnek.** Toast düğmeleri uygulamayı yeniden başlatarak tetikler (Windows'un
yapabildiği tek yol bu). Bunun iki pencere açmaması için uygulama tek örnek
kilitini tutar: ikinci açılış komutu ilk örneğe iletir ve kendini kapatır.

---

### Hakkında

| Öğe | İçerik |
|---|---|
| Kimlik | Ürün adı, sürüm (çalışan exe'nin sürüm kaynağından okunur), lisans |
| Bağlantılar | Instagram, GitHub ve kaynak kod — gerçek marka siluetli ikonlarla, `ShellExecuteW` ile açılır |
| Uyarılar | Demo lisans açıklaması ve yönetici yetkisi gerekçesi |

---

## Nasıl çalışıyor

Oyunlar, FiveM ve Gecikme kategorileri `src/core/regpack.cpp` içine **doğrudan gömülü
`.reg` gövdeleri** olarak gelir. Hiçbir şey indirilmez, harici klasör gerekmez. Çalışma
anında:

1. `.reg` metni rastgele adlı `%TEMP%\tengri_<pid>_<rastgele>.reg` dosyasına BOM'lu
   UTF-16LE olarak yazılır
2. Gizli pencerede `reg.exe import` ile içe aktarılır
3. Geçici dosya hemen silinir

| Konu | Ayrıntı |
|---|---|
| Geçici dosya güvenliği | `CREATE_NEW \| FILE_FLAG_OPEN_REPARSE_POINT` ve `CryptGenRandom` adı. Uygulama HKLM'ye dokunduğunda yükseltilmiş çalıştığı için tahmin edilebilir bir `%TEMP%` yolu, yerel saldırganın dosyayı önceden oluşturup bir junction'a yönlendirmesine izin verirdi |
| Neden `reg.exe` | `regedit /s` içe aktarma başarısız olsa bile 0 döndürür; bir anahtar sessizce hiçbir işe yaramaz. `reg import` gerçek bir çıkış kodu döndürür |
| Kategori başına tek geçiş | Bir kategorinin gövdeleri tek `.reg` içinde birleştirilir, böylece sekiz ayrı süreç yerine tek içe aktarma olur |
| Geri alma | Her anahtarın hem açma hem geri alma gövdesi vardır; kapatmak önceki değeri geri yükler veya anahtarı siler |

---

## Yükseltme

Manifest `asInvoker` istiyor: uygulama normal kullanıcı olarak açılır. Sistem bilgisini
okumak, temizlik taraması yapmak, registry değerlerini listelemek — hiçbiri yönetici
gerektirmez, dolayısıyla bunlar için UAC çıkmaz.

Yalnızca **gerçekten HKLM'ye yazan** işler yetki ister. Bunlardan biri istendiğinde
süreç kendini `runas` ile yeniden başlatır, o işi yüksek yetkiyle yapar ve çıkar.
Kullanıcı UAC sorusunu bir kez görür; ayarının uygulanmış olduğunu açarak doğrular.

| İş | Yazdığı yer | Yetki |
|----|-------------|-------|
| İnce ayarlar (Oyunlar, FiveM, Gecikme dahil çoğu) | `HKLM` | ister |
| RAM profili | `HKLM` | ister |
| DNS sağlayıcısı | `HKLM` + ağ adaptörü | ister |
| Başlangıçta çalıştır | `HKCU\...\Run` | **istemez** |
| Temizleyici (kendi dosyaları siler) | dosya sistemi | **istemez** |

Ayrı bir yardımcı exe kullanılmıyor: aynı exe yeniden başlatılıyor. Yardımcı exe ikinci
bir uygulama olurdu — ayrı derleme, ayrı sürüm, ayrı antivirüs tetiklemesi.

Bekleyen iş `src\core\elevate.cpp` tarafından onaltılık olarak komut satırına kodlanır
(`--apply=...`), yükseltilmiş süreçte çözülür ve orada uygulanır. Bu kodlama/çözme
gidiş-dönüşü testlerle korunur.

---
## Derleme

```bat
build.bat
```

Betik Visual Studio kurulumunu bulur (`vswhere` sonuç vermediğinde dizin taramasına düşer),
kaynakları derler ve `third_party/` altındaki arayüz kütüphanesini eksikse klonlar.
Çıktı: `build\TENGRI.exe`.

CMake de çalışır:

```bat
cmake -B build
cmake --build build --config Release
```

Not: Uzun yol adlarında CMake'in geçici derleme dosyası sığmayabilir
(`could not be compiled` / `ABI info - failed`). `build.bat` bu sorunu yaşamaz; sorun
uzunluğuysa CMake'i daha kısa bir yolda çalıştır.

**Gereksinimler:** Windows 10/11 x64, *Desktop development with C++* iş yükü olan Visual
Studio.

### Sürümleme ve kimlik

Tüm ürün kimliği — ad, pencere sınıfı, tray metni, `%APPDATA%` klasörü, `Run` değer adı,
geçici dosya öneki, ikon kimliği, bağlantılar — **`src/brand.hpp`** içinde. Yeniden
markalamak tek dosyalık bir düzenleme.

**Sürüm tek yerde yazılır: `src/brand.hpp`.** `tools\make_version.ps1` her derlemede
`build\obj\version.h` üretir, `res\tengri.rc` onu include eder. Böylece `VERSIONINFO`
kaynağı kaynak koddan ayrılamaz. Bu bir kazara yapıldı: bir etiket `v1.0.1` için atıldı,
kaynak kodu `1.0.0` diyordu ve sürüm ancak derledikten sonra fark edildi.

Hakkında sayfası sürümü ikinci bir kopya taşımak yerine `GetFileVersionInfo` ile çalışan
exe'den geri okuyor. Kod tabanında hiçbir yerde derleme tarihi gömülü değil.

`res\tengri.ico`, `tools/make_icon_from_png.ps1` ile `res\tengri-logo.png`'den üretiliyor.
Tek sanat kaynağı bu PNG: pencere simgesi, görev çubuğu, tepsi ikonu, giriş ekranı,
kenar çubuğu ve açılış ekranı hep aynı dosyadan gelir, böylece marka tek yerde
değiştirilir ve hiçbir yerde ayrışamaz.

---

## Proje yapısı

```
src/main.cpp               Kenarlıksız Win32 penceresi, D3D11 aygıtı, yuvarlatılmış köşeler
src/brand.hpp              Ürün kimliği: ad, yollar, registry anahtarları, sürüm, bağlantılar
src/app.cpp                Ekranlar, durum yönetimi, Hakkında sayfası
src/gui/theme.cpp          Renkler, fontlar, DPI ölçekleme
src/gui/fx.cpp             Parçacıklar, yıldız geçişi, üst ışık, ışık süpürme
src/gui/widgets.cpp        Buton, anahtar, onay kutusu, giriş, kaydırıcı, segment, grafik, bildirim
src/gui/icons.cpp          Vektör ikonlar
src/gui/logo.cpp           Tek PNG'den yüklenen marka logosu (D3D11 dokusu)
src/gui/brand_icons.cpp    Hakkında sayfası bağlantı ikonları (üçgenlenmiş, üretilmiş)
src/core/cleaner.cpp       Dosya taraması ve silme
src/core/tweaks.cpp        7 tweak kategorisinin registry okuma/yazma işlemleri
src/core/regpack.cpp       Gömülü .reg gövdeleri + reg.exe ile içe aktarma
src/core/ram.cpp           SvcHostSplitThresholdInKB profilleri
src/core/network.cpp       DNS geçişi, adaptör bilgisi, ağ anahtarları, ICMP ölçümü
src/core/sysinfo.cpp       CPU / RAM / disk / çalışma süresi / HWID
src/core/sysinfo_detail.cpp  Secure Boot, sanallaştırma, BIOS modu, kurulum tarihi
src/core/lang.cpp          İngilizce / Türkçe metin tabloları
src/core/license.cpp       Demo lisans ekranı
src/core/elevate.cpp        Yetki yükseltme: runas ile yeniden başlatma, komut satırı kodlama
src/core/backup.cpp         Uygulama öncesi registry yedeği (reg export) ve geri alma (reg import)
src/core/notify.cpp         Windows bildirimleri: WinRT toast, AUMID kurulumu, komut ayrıştırma
src/tray.cpp               Kabuk tray ikonu
res/tengri.rc              İkon, manifest, VERSIONINFO
res/app.manifest           Yürütme düzeyi, DPI farkındalığı, işletim sistemi uyumluluğu
tools/make_icon.ps1        Uygulama ikonunu üretir
tools/make_brand_icons.ps1 Hakkında sayfası bağlantı ikonlarını üçgenler ve brand_icons.cpp üretir
tools/make_version.ps1     brand.hpp'den sürüm başlığı üretir (VERSIONINFO kaynağı)
tools/shoot.ps1            Ekran görüntüsü almak için yardımcı betikler
tools/make_tweak_table.ps1 tweaks.cpp + regpack.cpp'den registry tablosu ve anahtar başlığı üretir
tools/make_icon_from_png.ps1  PNG kaynaktan çok boyutlu .ico üretir
tools/verify_icon.ps1      Üretilen .ico içindeki tek bir boyutu PNG'ye açar (gözle kontrol)
tests/test_pure.cpp         Registry'ye dokunmayan saf mantık testleri (build.bat çalıştırır)
.github/workflows/         Etiket itildiğinde sürümü temiz kurulumda derleyip yayınlar
docs/release-notes/        Sürüm notları (sürüm başına bir dosya)
SECURITY.md                Antivirüs uyarıları ve güvenlik bildirimi
CONTRIBUTING.md            Katkı rehberi (derleme, neyi nereden değiştirmek, geri alınabilirlik)
docs/tweaks-registry.md    56 anahtarın dokunduğu registry yolları (üretilmiş)
```

### i18n tabloları

`src/core/lang.cpp` aynı uzunlukta olmalı olan iki konumsel metin tablosu tutuyor —
İngilizce ve Türkçe. C++ bunu çalışma anında zorlamıyor, bu yüzden eksik bir giriş çok
sonra tek bir ayarın yanlış etiketi olarak ortaya çıkıyor. Üç `static_assert` tabloları
`S::_COUNT`'a ve birbirine karşı kontrol ediyor, yani uyumsuzluk bir build hatası.

Blok tablosu başlangıcı sabit bir sayı değil, son düz anahtardan türetiliyor. Sabit bir
sayı bir kez kaymıştı ve her tweak etiketini sessizce kaydırmıştı; türetilmiş olması, bir
anahtar eklemenin blokları bozamaması anlamına geliyor.

---

## Notlar

- Bazı ayarların etkili olması için yeniden başlatma gerekir.
- Lisans ekranı bir **demodur**: her anahtar kabul edilir, sunucuya karşı hiçbir doğrulama
  yapılmaz ve hiçbir telemetri yoktur. Hem Hakkında sayfası hem de Hesap kartı bunu
  söyler.
- Makine parmak izi, bilgisayar adı, sistem birim seri numarası ve bir **ürüne özgü tuz**
  üzerinden FNV-1a özeti olarak hesaplanır. Hiçbir yere gönderilmez.
- Bazı anahtarlar birden fazla ayar tarafından paylaşılır (aynı registry değerini ikisi de
  yazar). Uygulama birini kapattığında açık olanın değerini yeniden yazar, ama bu ilişki
  elle tutulan bir listede durur — yeni bir ayar eklerken
  [CONTRIBUTING.md](CONTRIBUTING.md) bunu anlatıyor.

## Sürümleme

Sürüm vermek için tek bir yer değişir, sonra etiket:

```bat
:: src\brand.hpp içindeki kVersionMajor / kVersionMinor / kVersionPatch / kVersion
:: (üç parça birleşik sürümle uyumlu olmalı, yoksa make_version.ps1 derlemeyi durdurur)

git commit -am "Surum 1.0.1"
git tag v1.0.1
git push origin main v1.0.1
```

GitHub Actions etiketi görünce temiz bir kurulumda derleyip sürümü `Releases` sayfasına
ekler. Sürüm notlarını `docs\release-notes\v1.0.1.md` altına koyarsan not olarak
kullanılır; yoksa otomatik olarak `TENGRI v1.0.1` yazılır.

İş akışı, etiketteki sürüm ile exe'in içindeki `VERSIONINFO` sürümünün aynı olduğunu da
kontrol eder — sürüm kaynağı artık tek olduğu için bu, son savunma hattıdır.

## Lisans

[MIT](LICENSE)