# Yükseltme ve yedekleme — elle test

Bu dosya, kodla doğrulanamayan iki yolu elle denemek için: **yükseltme devri**
(`asInvoker` → `runas`) ve **yedekleme/geri alma**. Otomatik testler saf mantığı
kapsıyor; süreçler arası gerçek çağrıyı yalnızca sen çalıştırabilirsin.

## Hazırlık

```bat
build.bat
```

Sonra `build\TENGRI.exe` çalıştır. Kurulum gerekmez.

## Test 1 — açılış yönetici istememeli

Uygulamayı aç. **UAC çıkmamalı.**

Sonra bir ayar açıp "Değişiklikleri uygula"ya basmadan kapat. Yine UAC çıkmamalı.

Doğru: uygulama normal kullanıcı olarak açılır.

## Test 2 — uygulama UAC istemeli ve yedeği almalı

1. **İnce Ayarlar** → **Performans** kategorisi
2. **SysMain'i kapat** anahtarını aç
3. **Değişiklikleri uygula**'ya bas
4. **UAC çıkmalı.** Onayla.

Sonra yedek klasörü oluşmuş olmalı:

```bat
dir "%LOCALAPPDATA%\TENGRI\backups"
```

İçinde `YYYYAAGG-SS-DDSS` adlı bir klasör ve `0-3.reg` gibi dosyalar olmalı.

Değerin gerçekten yazıldığını doğrula:

```bat
reg query "HKLM\SYSTEM\CurrentControlSet\Services\SysMain" /v Start
```

`0x4` görmelisin.

## Test 3 — geri al

Aynı ayarda **Geri al**'ya bas. Yine UAC çıkmalı, onayla.

```bat
reg query "HKLM\SYSTEM\CurrentControlSet\Services\SysMain" /v Start
```

Varsayılan değeri (`0x2`) görmelisin.

## Test 4 — UAC reddi

**Değişiklikleri uygula**'ya bas, UAC çıkana kadar **Hayır** de.

Uygulama **kapanmamalı**, "yönetici gerekli" uyarısı göstermeli. Yedek klasörü
boş kalmamalı.

## Test 5 — yönetici gerektirmeyen işler

- **Ayarlar → Başlangıçta çalıştır**: UAC **çıkmamalı** (HKCU\Run)
- **Temizleyici**: UAC **çıkmamalı**

## Test 6 — geri yükleme noktası

Aracın yedeği yalnızca kendi dokunduğu anahtarları saklar. Sistemde yapacağın
başka değişiklikler için ayrıca nokta oluştur:

`Windows + R` → `sysdm.cpl` → **Sistem koruma** → **Oluştur**

## Test 7 — güncelleme akışı

Otomatik testler WinHTTP'ye ve gerçek dosya takasına dokunmaz (`test_update` ağ istemi
açmaz). Bu yüzey elle denenir — **kendi exe'ni siler**, bu yüzden bir kopya üzerinde
yapılması önerilir.

1. **Denetim**: Ayarlar → Güncellemeler → **Şimdi denetle**. Güncel sürümde sonuç
   "Güncel" olmalı; Hakkında'daki rozet de aynı şeyi söylemeli.
2. **Kapalıyken istek yok**: anahtarı kapat, uygulamayı yeniden başlat, 24 saat
   beklemesi gerekiyor — açılışta istek gitmemeli. İstersen `LastCheck` değerini
   silip (`HKCU\Software\TENGRI\Update`) anahtar kapalıyken **Şimdi denetle**'in
   hâlâ çalıştığını doğrula.
3. **İndirme**: yeni bir sürüm yayınlandığında **İndir** → yüzde ilerlemesi, yarısında
   **Vazgeç** → `%TEMP%\TENGRI\Update` klasörü boş kalmalı.
4. **Sağlama kapısı**: indirilen dosyanın boyutu ve SHA-256'sı `latest.json` ile
   eşleşmeli. Eşleşmezse kart hata gösterir ve parça silinir.
5. **Takas**: **Kur ve yeniden başlat** → eski exe silinir, yenisi aynı yola geçer,
   uygulama `--tengri-relaunch` ile açılır. İkinci pencere açılmamalı (tek örnek
   kilidi yeni örneğe devredilmeli).
6. **Yazılamayan klasör**: exe'yi salt-okunur bir konuma (örn. `Program Files`) koy,
   indirmeyi dene → UAC **çıkmamalı**, kart dosyanın `%TEMP%` içindeki tam yolunu
   göstermeli.

> Adım 5 geri alınabilir değil: eski exe silinir. Denemeyi `build\` altındaki bir
> kopya üzerinde yap, Releases'tan indirdiğin dosya üzerinde değil.

## Bir şey ters giderse

Yedek klasörü silinmediği sürece ayarları geri getirebilirsin:

```bat
reg import "%LOCALAPPDATA%\TENGRI\backups\<klasor>\<dosya>.reg"
```

Elle de yapabilirsin — yedekler sıradan `.reg` dosyalarıdır.