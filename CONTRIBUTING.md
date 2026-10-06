# Katkı rehberi

TENGRI tek kişilik bir proje, ama açık kaynak. Hata bildirimi, çeviri düzeltmesi veya
kod katkısı memnuniyetle karşılanır.

## Kurma

Arayüz kütüphanesi bir submodule olarak gelir; ilk klonlamada beraber indirilmesi
gerekir:

```bat
git clone --recurse-submodules https://github.com/Memati8383/Tengri.git
cd Tengri
build.bat
```

Submodule'ı unuttuysan `build.bat` durur, `git submodule update --init --recursive` ile
tamamla. Depoyu GitHub'dan indirdiysen submodule boş gelir ve derleme tamamlanmaz.

Çıktı `build\TENGRI.exe`. CMake de çalışır:

```bat
cmake -B build
cmake --build build --config Release
```

## Ne değiştirilebilir

| Alan | Nerede | Not |
|------|--------|-----|
| İnce ayarlar (56 anahtar) | `src\core\tweaks.cpp` | Her anahtar iki yerde tanımlıdır: okuma (`ReadXxx`) ve yazma (`ApplyXxx`). Biri değişirken diğeri değişmezse düğme yanlış durum gösterir. |
| Kayıt defteri paketleri | `src\core\regpack.cpp` | 4-6. kategoriler. Her ayarın etkin ve kapalı olmak için ayrı bir gövdesi vardır; kapalı gövde hiçbir şey yapmaz, belgelenmiş varsayılanı geri yükler. |
| Metinler (TR/EN) | `src\core\lang.cpp` | Her iki tablo da aynı uzunlukta olmalı. `static_assert`'ler bunu derleme zamanında zorlar. |
| Arayüz | `src\app.cpp`, `src\gui\` | |
| Sürüm, yollar, bağlantılar | `src\brand.hpp` | Tek yer. Yeniden markalamak istersen burası. |

## Arka plan

- **Sürüm tek kaynaktadır.** `src\brand.hpp` içindeki `kVersion*` sabitlerinden
  `build\obj\version.h` üretilir, `res\tengri.rc` onu okur. `.rc` dosyasındaki sürümü
  elle değiştirme — üretilen başlık her derlemede yeniden yazılır.
- **Registry tablosu üretilir.** `docs\tweaks-registry.md`, `tools\make_tweak_table.ps1`
  ile `tweaks.cpp` ve `regpack.cpp`'den üretilir. Elle düzenleme; kod değişince
  `build.bat` yeniden yazar.
- **Paylaşılan registry değerleri vardır.** `SystemResponsiveness`, `DisablePagingExecutive`
  ve `GameDVR_FSEBehaviorMode` birden fazla anahtar tarafından sahiplenilir. `IsShared()`
  bunları işaretler; yeni bir anahtar mevcut bir değere dokunursa buraya da eklenmelidir.
  Aksi hâlde bir anahtar geri alındığında ötekisi sessizce kapanır.
- **Yorumlar Türkçedir.** Açıklama eklerken de aynı dilde yaz; kod tabanında tek dil
  kullanılıyor.

## Göndermeden önce

```bat
build.bat
git diff --stat
```

- Derleme hatasız ve uyarısız geçmeli (`/W4`).
- `docs\tweaks-registry.md` değiştiyse üretilmiş olmalı — elle yazılmış fark yok.
- Yeni metin eklediysen `g_en` ve `g_tr` tablolarının ikisini de güncelle.

## Hata bildirimi

Açık bir başlık kullan: ne yapmaya çalıştığın, ne oldu, ne olmasını bekliyordun. Sürüm
numarasını (Hakkında sayfasında görünür) ve Windows sürümünü yaz. Ekran görüntüsü
yardımcı olur.

## Güvenlik

Bir düzeltme göndermeden önce şunu sor: değişiklik geri alınabilir mi? Yazılan her
registry değeri için geri alma yolu olmalı. Geri alma yolu olmayan bir ayar ekliyorsan
bunu açıklamasıyla birlikte belirt.

Depodaki hiçbir değişiklik antivirüsü tetiklemese de — ImGui ve `reg.exe` kullanan
paketlenmiş araçlar sık tetiklenir. `SECURITY.md` bu konuyu ayrıntılı açıklıyor.

## Lisans

Katkın MIT Lisansıyla dağıtılır. Gönderdiğin kodun da aynı lisansla uyumlu olması
gerekir.