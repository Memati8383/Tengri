# Bildirimler

[MIT lisansı](LICENSE) yalnızca bu depodaki TENGRİ kaynak kodunu kapsar. Bu
dosyada, o lisansın dokunmadığı iki şey bildirilir: içimize derlenen
bağımlılıklar ve uygulamanın ne olmadığı.

## Üçüncü taraf bileşenler

| Bileşen | Lisans | Nerede |
|---|---|---|
| **Dear ImGui** (arayüz kütüphanesi) | MIT, Copyright (c) 2014-2026 Omar Cornut | `third_party/imgui/LICENSE.txt` — submodule olarak kendi dizininde durur |
| **Inter** yazı tipi (exe içine gömülü) | SIL Open Font License 1.1 | Lisans metni depoda: `res/fonts/Inter-OFL.txt`. Üç ağırlık alt kümelenip `src/core/font_data.cpp` içindeki üretilmiş tabloya gömülür |
| **Windows SDK / MSVC araç seti** | Microsoft lisanslarına tabi | Yalnızca derleme sırasında kullanılır, depoda kaynağı yok |

Bunların lisansları kendi metinleriyle yürürlükte kalır; TENGRİ'nin MIT lisansı
onları ne değiştirir ne de genişletir. ImGui'yi kaynak kodundan alan bir derleme
yaparsan, o dizindeki `LICENSE.txt`'i de koruman gerekir.

## Uygulamanın ne olmadığı

Lisans metnindeki "THE SOFTWARE IS PROVIDED AS IS" cümlesi bu proje için
sıradan bir formalite değil, tam da durduğumuz yer:

- TENGRİ bir **sistem aracıdır**, zararlı yazılım değildir — ama registry'ye
  `HKLM` altına yazar, `reg.exe` çağırır ve hizmet yapılandırmasını değiştirir.
  Bu, sezgisel antivirüs motorlarının imzasız çalıştırılabilirlerde işaretlediği
  davranış kümesidir. Ayrıntı: [SECURITY.md](SECURITY.md)
- Lisans **ekranı bir demodur**: her anahtarı kabul eder, sunucuya hiçbir
  doğrulama yapmaz, hiçbir telemetri göndermez. Bu, yazılımın "lisanssız"
  olduğu anlamına gelmez; yukarıdaki MIT lisansı tüm depoya uygulanır.
- Makine parmak izi hesaplanır, hiçbir yere gönderilmez.

## Ticari ad

"TENGRİ" ve kurt logosu bu projenin adıdır; MIT lisansı ticari marka hakkı
vermez. Logonun veya adın kullanımına izin gereken bir şey yapacaksan önce sor.
