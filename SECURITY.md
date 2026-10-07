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

TENGRI kullanıcı verisi göndermez. Dışarı giden isteklerin tamamı iki tanedir ve README'deki
[Ağ kullanımı](README.md#ağ-kullanımı) bölümünde bayt bayt listelenir:

- Sürüm denetimi: `github.com` üzerinden `latest.json` ve yalnızca sen **İndir** dediğinde
  o bildirimin adresinden exe. İstekler sabit GET'lerdir; gövde, çerez, sorgu parametresi
  yoktur.
- Ağ ekranındaki gecikme ölçümü: hedefi sabit `1.1.1.1` olan ICMP echo, yük alanı sabit bir
  işaretçi yazı. Kullanıcı verisi taşımaz.

Makine parmak izi hesaplanır ama yalnızca ekranda maskelenmiş haliyle gösterilir; hiçbir
isteğe eklenmez. Lisans ekranı bir demodur ve sunucuya hiçbir istek yapmaz.

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

## Kapsam dışı

Ayar olarak değiştirilen registry değerlerinden doğan veri kaybı bu projenin sorumluluğu
dışındadır. Ayarları uygulamadan önce geri yükleme noktası oluştur.