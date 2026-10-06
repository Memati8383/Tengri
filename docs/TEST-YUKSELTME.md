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

## Bir şey ters giderse

Yedek klasörü silinmediği sürece ayarları geri getirebilirsin:

```bat
reg import "%LOCALAPPDATA%\TENGRI\backups\<klasor>\<dosya>.reg"
```

Elle de yapabilirsin — yedekler sıradan `.reg` dosyalarıdır.