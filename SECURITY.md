# Güvenlik

## Antivirüs uyarısı alıyorsan

TENGRI antivirüs ve Windows SmartScreen tarafından işaretlenirse bu beklenen bir durumdur ve
TENGRI'ye özgü bir hata değildir.

**Neden oluyor.** TENGRI üç şey yapıyor:

1. Ayar yazacağı zaman **yönetici yetkisi** istiyor. Uygulamanın kendisi
   `asInvoker` açılır (`res/app.manifest`) — yalnızca bilgi ekranlarını görmek UAC
   istemez. HKLM'e dokunacak bir ayar seçildiğinde süreç `runas` ile kendini
   yeniden başlatır ve o tek işi yükseltilmiş çocuk yapar. Sezgisel motorlar bu
   kendin-yükseltme davranışını imzasız bir exe'de görür.
2. `HKEY_LOCAL_MACHINE` altına **doğrudan registry değeri yazıyor** —
   `RegCreateKeyExW` + `RegSetValueExW` (`src/core/regwrite.hpp`, tek yazıcı).
3. Hizmet başlangıç türünü `ChangeServiceConfigW` ile değiştiriyor ve `powercfg`,
   `sc`, `netsh`, `net` alt süreçlerini **System32'den tam yolla** çağırıyor
   (bkz. `src/core/syspath.hpp`).

Bu üç davranışın üçü birden, bir "registry temizleyici / sistem optimizer" zararlı
yazılımının davranış imzasıyla örtüşüyor. Sezgisel motorlar davranış imzasına bakar, dosyanın
imzalı olup olmadığına değil. TENGRI **imzasız** dağıtılıyor, bu yüzden tespitler daha da
geçerli.

> **Eski sürümlerde bu üçüncü madde farklıydı:** geçici `.reg` dosyaları içe aktarmak için
> `reg.exe` çağrılıyordu. O yol kaldırıldı; artık hiçbir registry yazma işlemi dosya ya da
> alt süreç üzerinden gitmiyor.

**Ne yapabilirsin**

- **Güvenmiyorsan kaynaktan derle.** Yazdığı her registry anahtarı kaynakta görünür ve
  README'de listelenmiştir. Derlemek tek bir komut: `build.bat`
- **SmartScreen uyarısını geçmek için** indirilen dosyaya sağ tıkla → Özellikler →
  "Engelle" kutusundaki tik'i kaldır → Uygula. Windows imzasız dosyalarda bu yolu
  gösterir.
- **Kendi antivirüsünde istisna ekle**, kararı sana bırakıyoruz.

## Raporlama

Bir açık bulursan veya şüpheli davranış görürsen GitHub Issues üzerinden yazabilirsin. Bu
bir genel kullanılabilirlik aracı; güvenlik açığı bildirimi için Issues uygundur çünkü
proje küçük ve açık kaynak.

Bulduğun şeyi anlatırken şunları ekle:

- Windows sürümü
- TENGRI sürümü (Hakkında sayfasında yazıyor)
- Adım adım ne olduğu
- Beklediğin davranış neydi

## Veri gizliliği

TENGRI kendi ürettiği hiçbir veriyi sunucuya göndermez. Dışarı giden isteklerin tamamı
dört tanedir ve README'deki
[Ağ kullanımı](README.md#ağ-kullanımı) bölümünde satır satır listelenir:

- **Sürüm denetimi (iki istek)**: `github.com` üzerinden `latest.json`, ardından yalnızca sen
  **İndir** dediğinde o bildirimin adresinden exe. İstekler sabit GET'lerdir; gövde, çerez, sorgu
  parametresi yoktur.
- **Ağ ekranındaki gecikme ölçümü**: hedefi sabit `1.1.1.1` olan ICMP echo, yük alanı sabit
  bir işaretçi yazı. Kullanıcı verisi taşımaz.
- **Medya Adası'nın söz araması**: çalan parça varsa `https://lrclib.net/api/...` adresine
  bir GET gider ve sorgu parametreleri **şarkının adını, sanatçısını, albümünü ve süresini**
  taşır. Bu, projenin üçüncü bir tarafa içerik taşıyan tek isteğidir. Ayarlar → Medya Adası →
  **Senkronize sözler** ile kapatılabilir (kapalıyken istek hiç gitmez), adanın kendisi de
  **Adayı göster** ile kapatılır.

Makine parmak izi hesaplanır ama yalnızca ekranda maskelenmiş haliyle gösterilir; hiçbir
isteğe eklenmez. Lisans ekranı bir demodur ve sunucuya hiçbir istek yapmaz.

> **Adanın okuduğu şey.** Ada, Windows'un medya oturumu API'sinden (SMTC) **yalnızca
> Spotify oturumunun** parça bilgisini ve kapağını okur; diğer oynatıcıların oturumları
> listelenmez. Ses seviyesi de oynatıcının kendi oturumu üzerinden yönetilir. Okunan
> bilgi ekranda çizilir ve kapatılmışsa hiçbir yere gitmez — söz araması yukarıdaki
> tek istisnadır.

## Güncelleme kanalına nasıl yaklaşılmalı

Uygulama yeni sürümü GitHub Releases'tan öğrenir ve indirdiği dosyayı bildirilen SHA-256
ile doğrular; uyuşmazsa dosyayı siler. Ayrıca indirme adresi https ve `github.com` /
`*.githubusercontent.com` dışında ise reddedilir.

Bu kapının garanti kapsamını net söylemek gerekir: özet, exe ile **ayrı bir varlıkta**
taşındığı için bozuk/yarım indirmeyi ve adresin başka bir dosyaya çevrilmesini yakalar,
ama ikisi de aynı depodan geldiği için **deposu ele geçirilmiş bir yayını yakalamaz**.
Bir saldırı senaryosunda saldırgan depo yazma erişimine sahipse hem exe'i hem `latest.json`ı
yeniden üretebilir.

Bu yüzden güncelleme akışı üç tasarımla sınırlı tutuldu: otomatik indirme yok, kurulum ayrı
bir onay ister, eski exe'in yerine yeni dosya konur. Kendi şüphen varsa `git tag` + yerel
derleme yolunu kullan; doğrulamanın en güçlü hali bu.

## Yerel girdilerin güven sınırı

Uygulamanın okuduğu her yer yönetici olarak yazan çocuğa girebilir. Sınırlar şöyle
çizildi:

- **Yedek klasörü** `%LOCALAPPDATA%\...\backups`. Aynı kullanıcıda koşan herhangi
  bir program oraya kendi `.reg` dosyasını koyabilir; dosyaların kökenini kanıtlayan
  güvenilir bir kayıt yok (bizim tuttuğumuz bir liste de aynı program tarafından
  değiştirilebilirdi). Bu yüzden `backup::Restore()` içeriği iki sabitle daraltır:
  dosya adı üretilen tabloda gerçek bir `(kategori,sıra)` çifti olmak zorunda, ve
  dosyanın içindeki **tüm** anahtar satırları o çiftin anahtarlarından birinin
  kendisi ya da alt anahtarı olmak zorunda. Alt anahtar hoşgörüsü keyfi değil:
  `reg.exe` dışa aktarma çıktısı bir anahtarı alırken alt anahtarlarını da aynı dosyaya
  yazabiliyor (bu makinede beş ayar birden fazla anahtara yazıyor, bir yedek 14 427 satır
  alt anahtar taşıyor). Klasöre bırakılmış yabancı bir dosya en fazla uygulamanın
  zaten yedeklediği alt ağaca dokunabilir; IFEO, hizmet ya da `Run` gibi rastgele
  bir HKLM anahtarına yazamaz. `--apply` üzerinden gelen klasör adı yalnızca rakam
  ve tire taşıyabilir.
- **Başlangıç girdisi yedekleri** kendi hive'ında durur: makine kapsamı yedekler
  HKLM'de, kullanıcı kapsamı yedekler HKCU'da. Geri açma HKCU'dan okunan bir
  komutu HKLM\Run'a **yazmaz**; aksi, aynı kullanıcıdaki bir zararlıya yönetici
  yetkisiyle kalıcı başlangıç girdisi kurdurmak olurdu. Eski sürümlerin HKCU'da
  bıraktığı makine yedekleri bu yüzden listede görünmez ve geri açılamaz; komut
  `HKCU\Software\TENGRI\StartupDisabled\...` altında durmaya devam eder, silinmez.
- **Bildirim tıklamaları** (`WM_COPYDATA`) tek örnek kilidiyle sınırlıdır ve
  Windows'un UIPI sınırı düşük yetkililik seviyelerini zaten engeller. Geri alma
  (`CmdUndo`) yazmadan önce ekranda onay ister.
- **Alt süreçler** daima System32 tam yoluyla çağrılır; `srclient.dll` de aynı
  kurala uyuyor. Yalın ad vermek, arama uygulamanın kendi (portable kurulumda
  kullanıcı-yazılabilir) klasöründen başladığı için, oraya bırakılmış programı
  yönetici olarak çalıştırmak olurdu.

## Kapsam dışı

Ayar olarak değiştirilen registry değerlerinden doğan veri kaybı bu projenin sorumluluğu
dışındadır. Ayarları uygulamadan önce geri yükleme noktası oluştur.