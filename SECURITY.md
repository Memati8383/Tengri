# Güvenlik

## Antivirüs uyarısı alıyorsan

TENGRI antivirüs ve Windows SmartScreen tarafından işaretlenirse bu beklenen bir durumdur ve
TENGRI'ye özgü bir hata değildir.

**Neden oluyor.** TENGRI üç şey yapıyor:

1. Açılışta **yönetici yetkisi** istiyor (`requireAdministrator`)
2. `HKEY_LOCAL_MACHINE` altına **registry değeri yazıyor**
3. Geçici `.reg` dosyalarını içe aktarmak için **`reg.exe` çağırıyor**

Bu üç davranışın üçü birden, bir "registry temizleyici / sistem optimizer" zararlı
yazılımının davranış imzasıyla örtüşüyor. Sezgisel motorlar davranış imzasına bakar, dosyanın
imzalı olup olmadığına değil. TENGRI **imzasız** dağıtılıyor, bu yüzden tespitler daha da
geçerli.

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

TENGRI hiçbir veriyi göndermez. Trafik çıkışı yoktur; makine parmak izi hesaplanır ama
yalnızca ekranda gösterilir ve hiçbir yere iletilmez. Lisans ekranı bir demodur ve sunucuya
hiçbir istek yapmaz.

## Kapsam dışı

Ayar olarak değiştirilen registry değerlerinden doğan veri kaybı bu projenin sorumluluğu
dışındadır. Ayarları uygulamadan önce geri yükleme noktası oluştur.