// Disk analizi modülü testleri.
//
// Yan-etki sözleşmesi (test_pure / test_update ile aynı):
//   - GERÇEK DİSKTEN OKUMA YOK. MFT ham bölüm okunmaz; ParseMftRecord,
//     MeasureRecStride ve DecodeDataRuns bellekte kurulmuş sahte kayıtlarla
//     sınanır. Start() yalnızca GetTempPath altındaki kendi geçici ağacımızda
//     çağrılır (walk motoru; sürücü kökü hedeflenmez, dolayısıyla MFT açılmaz).
//   - DOSYA SİLME YOK. DeletePaths/ShowInExplorer hiç çağrılmıyor.
//   - DOSYA: yalnızca GetTempPath altında benzersiz adla kendi önbellek
//     dosyamız yazılır ve sonunda silinir. Kullanıcı verisine dokunulmaz.
//
// Doğrulanan mantık: MFT kayıt ayrıştırma (boyut önceliği, gölge kayıt, FRN
// maskelemesi), data run çözme (negatif delta, sıkıştırma işareti), squarified
// treemap (alan korumu, girdi indeks eşlemesi), önbellek biçiminin sınır
// denetimleri (bozuk/hususi dosya reddi) ve UTF dönüşümleri.

#include "../src/core/diskanalyzer.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

static int g_fail = 0;
static int g_pass = 0;

static void Check(bool ok, const char* what)
{
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("  BASARISIZ: %s\n", what); }
}

static void CheckU(uint64_t got, uint64_t want, const char* what)
{
    if (got == want) ++g_pass;
    else { ++g_fail; std::printf("  BASARISIZ: %s (beklenen %llu, gelen %llu)\n",
                                 what, (unsigned long long)want, (unsigned long long)got); }
}

// ---------------------------------------------------------------- sahte kayıt
static void Put16(uint8_t* p, uint16_t v) { std::memcpy(p, &v, 2); }
static void Put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
static void Put64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, 8); }

// Yerinde (resident) öznitelik başlığı: type, toplam uzunluk, valOff=0x18, value.
static uint32_t PutAttr(uint8_t* rec, uint32_t off, uint32_t type,
                        const uint8_t* val, uint32_t valLen, uint8_t nonRes = 0)
{
    uint32_t len = 0x18 + valLen;
    Put32(rec + off + 0x00, type);
    Put32(rec + off + 0x04, len);
    rec[off + 0x08] = nonRes;
    rec[off + 0x09] = 0;                       // ad yok
    Put32(rec + off + 0x10, valLen);
    Put16(rec + off + 0x14, 0x18);
    if (val && nonRes == 0) std::memcpy(rec + off + 0x18, val, valLen);
    return len;
}

// $STANDARD_INFORMATION gövdesi (0x48) boyut TAŞIMAZ: 0x30 owner, 0x34 security
// id ($Secure:$SDS indeksi), 0x38 kota. Boyut sanmak dosyayı TB'na çıkarır.
// +0x08 değiştirilme FILETIME, +0x20 Win32 öznitelik maskesi (maske için asıl
// kaynak SI'dır; $FILE_NAME +0x38 yalnızca SI okunamazken yedek).
static void MakeSI(uint8_t* val, uint32_t owner, uint32_t secId, uint64_t quota,
                   uint64_t mtimeFt = 0, uint32_t attrs = 0x20)
{
    std::memset(val, 0, 0x48);
    Put64(val + 0x08, mtimeFt);
    Put32(val + 0x20, attrs);
    Put32(val + 0x30, owner);
    Put32(val + 0x34, secId);
    Put64(val + 0x38, quota);
}

// $FILE_NAME gövdesi: parent 0x00, alloc 0x28, real 0x30, nameLen 0x40, ns 0x41, ad 0x42.
static uint32_t MakeFN(uint8_t* val, uint64_t parentFrn, uint64_t alloc, uint64_t real,
                       const wchar_t* name, uint8_t ns = 1)
{
    uint32_t valLen = 0x42;
    std::memset(val, 0, valLen);
    Put64(val + 0x00, parentFrn);
    Put64(val + 0x28, alloc);
    Put64(val + 0x30, real);
    int nl = (int)std::wcslen(name);
    val[0x40] = (uint8_t)nl;
    val[0x41] = ns;                           // 1 = Win32, 2 = DOS 8.3, 3 = Win32+DOS
    for (int k = 0; k < nl; ++k) Put16(val + 0x42 + k * 2, (uint16_t)name[k]);
    valLen += (uint32_t)nl * 2;
    return valLen;
}

// Non-resident $DATA başlığı (0x40). Koşu listesi ofset alanının yeri build'e
// göre değişiyor (ölçülen 0x20, klasik belge 0x10); tahsis +0x28 ve gerçek
// +0x30 ikisinde de aynı — ayrıştırıcı o ikisini okur, +0x20'yi (geçerli veri
// uzunluğu) değil.
static uint32_t PutNonResData(uint8_t* rec, uint32_t off, uint32_t runOffAt,
                              uint16_t runListOff, uint64_t alloc, uint64_t real)
{
    std::memset(rec + off, 0, 0x40);
    Put32(rec + off + 0x00, 0x80);
    Put32(rec + off + 0x04, 0x40);
    rec[off + 0x08] = 1;                      // non-resident
    rec[off + 0x09] = 0;                      // adsız akış
    Put16(rec + off + runOffAt, runListOff);
    Put64(rec + off + 0x28, alloc);
    Put64(rec + off + 0x30, real);
    return 0x40;
}

// Sahte MFT bloğu için tek kayıt imzası: "FILE" + attrsOff.
static void StampFile(uint8_t* p, uint16_t attrsOff)
{
    std::memset(p, 0, 0x40);
    std::memcpy(p, "FILE", 4);
    Put16(p + 0x14, attrsOff);
}

static std::vector<uint8_t> MakeRecord(uint16_t flags, uint64_t selfFrn, uint64_t baseRef)
{
    std::vector<uint8_t> rec(1024, 0);
    std::memcpy(rec.data(), "FILE", 4);
    Put16(rec.data() + 0x14, 0x28);             // attrsOff
    Put16(rec.data() + 0x16, flags);
    Put32(rec.data() + 0x18, 0x300);            // used
    Put32(rec.data() + 0x1C, 1024);             // kaydın tahsisi = yerinde dosyanın maliyeti
    Put64(rec.data() + 0x20, baseRef);
    Put16(rec.data() + 0x28, 1);                // nextAttrId
    Put32(rec.data() + 0x2C, (uint32_t)selfFrn); // kendi indeksi (konum türetmesinin doğrusu)
    return rec;
}

// ---------------------------------------------------------------- birim testler

static void TestParseFile()
{
    // Boyut zinciri: non-resident $DATA -> $FILE_NAME -> 0. SI boyut taşımadığı
    // için owner/security/kota alanları asla boyut olamaz (regresyon bekçisi).
    auto rec = MakeRecord(0x0001, (5ull << 48) | 42ull, 0);
    uint8_t si[0x48]; MakeSI(si, 0, 3800, 0);
    uint32_t off = 0x28;
    off += PutAttr(rec.data(), off, 0x10, si, 0x48);
    // FRN: üst 16 = sıra numarası, ALT 48 = kayıt indeksi. parent 16 olmalı, 7 değil.
    uint8_t fn[0x80];
    uint32_t fnLen = MakeFN(fn, (7ull << 48) | 16ull, 0, 0, L"rapor.txt");
    off += PutAttr(rec.data(), off, 0x30, fn, fnLen);
    off += PutNonResData(rec.data(), off, 0x20, 0x40, 16384, 12345);
    Put32(rec.data() + off, 0xFFFFFFFF);

    disk::MftFileInfo fi;
    Check(disk::ParseMftRecord(rec.data(), 1024, fi), "parse: dosya kaydi cozuldu");
    Check(fi.inUse && !fi.isDir && fi.hasName, "parse: bayraklar (inUse, dosya, adli)");
    CheckU(fi.size, 12345, "parse: boyut DATA +0x30 gercek alanindan");
    CheckU(fi.alloc, 16384, "parse: tahsis DATA +0x28 alanindan");
    Check(fi.size != 16320875724800ull, "parse: SI owner|security<<32 boyut olarak okunmadi");
    CheckU(fi.parent, 16, "parse: parent FRN alt 48 bit maske");
    Check(fi.name == L"rapor.txt", "parse: Win32 ad");
}

static void TestParseDirAndDeleted()
{
    auto rec = MakeRecord(0x0003, 11, 0);
    uint32_t off = 0x28;
    uint8_t fn[0x80];
    uint32_t fnLen = MakeFN(fn, 1, 0, 0, L"Windows");
    off += PutAttr(rec.data(), off, 0x30, fn, fnLen);
    Put32(rec.data() + off, 0xFFFFFFFF);
    disk::MftFileInfo fi;
    Check(disk::ParseMftRecord(rec.data(), 1024, fi) && fi.isDir && fi.hasName,
          "parse: klasor kaydi");

    auto del = MakeRecord(0x0000, 12, 0);        // kullanımda değil
    Check(disk::ParseMftRecord(del.data(), 1024, fi) && !fi.inUse && !fi.hasName,
          "parse: silinmis kayit atlama sinyali");

    auto bad = MakeRecord(0x0001, 13, 0);
    std::memcpy(bad.data(), "XXXX", 4);
    Check(!disk::ParseMftRecord(bad.data(), 1024, fi), "parse: sihir bayti yanlisi reddedildi");
}

static void TestSiFields()
{
    // $STANDARD_INFORMATION gövdesi Node'un ek alanlarını besler: +0x08 değişim
    // zamanı (FILETIME → unix saniye), +0x34 security id ($SDS sözlük anahtarı),
    // +0x20 öznitelik maskesi. Boyut zincirine dokunmaz.
    //
    // +0x30 "owner id" okunMAZ: ölçülen hacimde 719.699 kullanımda kayıttan
    // yalnızca 5'inde sıfır dışıydı; tek kullanılabilir anahtar +0x34.
    const uint64_t kEpoc = 116444736000000000ull;
    auto rec = MakeRecord(0x0001, 20, 0);
    uint8_t si[0x48];
    MakeSI(si, 265, 3800, 0, kEpoc + 1700000000ull * 10000000ull, 0x800);
    uint32_t off = 0x28;
    off += PutAttr(rec.data(), off, 0x10, si, 0x48);
    uint8_t fn[0x80];
    uint32_t fnLen = MakeFN(fn, 11, 4096, 1000, L"a.txt", 3);
    Put32(fn + 0x38, 0x20);                  // yedek kaynak: bağın kendi maskesi
    off += PutAttr(rec.data(), off, 0x30, fn, fnLen);
    Put32(rec.data() + off, 0xFFFFFFFF);

    disk::MftFileInfo fi;
    Check(disk::ParseMftRecord(rec.data(), 1024, fi), "si: kayit cozuldu");
    CheckU(fi.mtime, 1700000000u, "si: degisim zamani unix saniyeye cevrildi");
    CheckU(fi.owner, 3800u, "si: security id +0x34'tan okundu");
    Check(fi.owner != 265u, "si: owner id alani bilincli olarak yok sayildi");
    // Kaynak tercihinin ölçüsü: 1219 örnekli hacim sayımında Win32 maskesine
    // $SI 1179, $FN 1178 uydu; tek farklı vakada doğru olan $SI'dı.
    CheckU(fi.attrs, 0x800u, "si: Win32 maskesinde $SI yetkili");
    Check(!fi.dataOk && !fi.sizeUnknown, "si: boyut FN'den geldi, ölçülememiş sayılmadı");
    CheckU(fi.size, 1000, "si: boyut zinciri SI'den etkilenmedi");

    // $SI yoksa (bozuk/kısa gövde) maskenin $FILE_NAME'den gelmesi gerekir.
    auto nosi = MakeRecord(0x0001, 24, 0);
    off = 0x28;
    fnLen = MakeFN(fn, 11, 4096, 1000, L"b.txt", 3);
    Put32(fn + 0x38, 0x820);
    off += PutAttr(nosi.data(), off, 0x30, fn, fnLen);
    Put32(nosi.data() + off, 0xFFFFFFFF);
    disk::MftFileInfo fi2;
    Check(disk::ParseMftRecord(nosi.data(), 1024, fi2) && fi2.hasName,
          "si-yok: kayit cozuldu");
    CheckU(fi2.attrs, 0x820u, "si-yok: maskeyi $FILE_NAME yedek verdi");
    Check(fi2.mtime == 0 && fi2.owner == 0, "si-yok: zaman/sahip SI'dan gelir, burada bos");
}

static void TestSizeUnknown()
{
    // $DATA yok, $FILE_NAME boyutları 0, taban kayıt (baseRef=0): düğüm ağaçta
    // kalır ama 0'ı "ölçülemedi" diye işaretler (ölçülen: bu hacimde 5 dosya;
    // ağaçtaki 9.103 sıfır boyutlunun çoğu gerçek boş dosya).
    auto rec = MakeRecord(0x0001, 21, 0);
    uint32_t off = 0x28;
    uint8_t fn[0x80];
    uint32_t fnLen = MakeFN(fn, 11, 0, 0, L"ghost.etl", 3);
    off += PutAttr(rec.data(), off, 0x30, fn, fnLen);
    Put32(rec.data() + off, 0xFFFFFFFF);
    disk::MftFileInfo fi;
    Check(disk::ParseMftRecord(rec.data(), 1024, fi) && fi.hasName,
          "boyut: olculemeyen dosya adiyla dagacta");
    CheckU(fi.size, 0, "boyut: olculemeyenin boyutu 0");
    Check(fi.sizeUnknown && !fi.dataOk, "boyut: bilinmiyor isareti kondu");

    // Gerçek BOŞ dosya ayrı bir şeydir: adsız resident $DATA (valLen=0) boyutu
    // bilir; "ölçülemedi" değildir.
    auto empty = MakeRecord(0x0001, 22, 0);
    off = 0x28;
    fnLen = MakeFN(fn, 11, 0, 0, L"zero.txt", 3);
    off += PutAttr(empty.data(), off, 0x30, fn, fnLen);
    uint8_t d[8]; std::memset(d, 0, sizeof d);
    off += PutAttr(empty.data(), off, 0x80, d, 0);
    Put32(empty.data() + off, 0xFFFFFFFF);
    disk::MftFileInfo fe;
    Check(disk::ParseMftRecord(empty.data(), 1024, fe) && fe.hasName,
          "bos: resident 0 baytlik DATA cozuldu");
    CheckU(fe.size, 0, "bos: boyut 0");
    Check(fe.dataOk && !fe.sizeUnknown, "bos: bos dosya olculemedi sayilmaz");

    // Adlı $DATA = alternate stream: sayılır, tek bite iner.
    auto ads = MakeRecord(0x0001, 23, 0);
    off = 0x28;
    fnLen = MakeFN(fn, 11, 0, 0, L"withads.txt", 3);
    off += PutAttr(ads.data(), off, 0x30, fn, fnLen);
    uint32_t dOff = off;
    off += PutAttr(ads.data(), off, 0x80, d, 8);
    ads.data()[dOff + 0x09] = 8;                   // adlı akış işaretı
    Put32(ads.data() + off, 0xFFFFFFFF);
    disk::MftFileInfo fa;
    Check(disk::ParseMftRecord(ads.data(), 1024, fa) && fa.hasName, "ads: adli akisli kayit cozuldu");
    Check(fa.ads == 1, "ads: adli DATA sayildi");
    Check(!fa.dataOk, "ads: adli akis boyut kaynagi olmadı");
    Check((disk::NodeFlags(fa.attrs, fa.ads, fa.links, false) & disk::kFlagAds) != 0,
          "ads: bayrak NodeFlags'a tasiyor");
}

static void TestNodeRecordShape()
{
    // Unix saniye dönüşümü: epok öncesi ve 0 → 0, 2106 üstü doyurulur.
    const uint64_t kEpoc = 116444736000000000ull;
    CheckU(disk::UnixSeconds(0), 0, "zaman: 0 FILETIME bos kaldi");
    CheckU(disk::UnixSeconds(kEpoc - 1), 0, "zaman: 1970 oncesi 0");
    CheckU(disk::UnixSeconds(kEpoc), 0, "zaman: epok noktasi 0");
    CheckU(disk::UnixSeconds(kEpoc + 10000000ull), 1, "zaman: 100 ns adim -> saniye");
    CheckU(disk::UnixSeconds(kEpoc + 1700000000ull * 10000000ull), 1700000000u,
           "zaman: olcumlu saniye");
    CheckU(disk::UnixSeconds(0xFFFFFFFFFFFFFFFFull), 0xFFFFFFFFu,
           "zaman: uint32 tasimasi 0xFFFFFFFF'a doyuruldu");

    CheckU(disk::NodeFlags(0, 0, 1, false), 0, "bayrak: bos maske bos");
    CheckU(disk::NodeFlags(FILE_ATTRIBUTE_COMPRESSED, 0, 1, false), disk::kFlagCompressed,
           "bayrak: compressed");
    CheckU(disk::NodeFlags(FILE_ATTRIBUTE_SPARSE_FILE, 0, 1, false), disk::kFlagSparse,
           "bayrak: sparse");
    CheckU(disk::NodeFlags(FILE_ATTRIBUTE_ENCRYPTED, 0, 1, false), disk::kFlagEncrypted,
           "bayrak: encrypted");
    CheckU(disk::NodeFlags(FILE_ATTRIBUTE_REPARSE_POINT, 0, 1, false), disk::kFlagReparse,
           "bayrak: reparse");
    CheckU(disk::NodeFlags(FILE_ATTRIBUTE_OFFLINE, 0, 1, false), disk::kFlagOffline,
           "bayrak: offline");
    CheckU(disk::NodeFlags(0, 2, 1, false), disk::kFlagAds, "bayrak: adli akis ADS");
    CheckU(disk::NodeFlags(0, 0, 2, false), disk::kFlagHardLink, "bayrak: iki bag hardlink");
    CheckU(disk::NodeFlags(0, 0, 1, true), disk::kFlagSizeUnknown, "bayrak: boyut olculemedi");
    CheckU(disk::NodeFlags(FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_ENCRYPTED, 1, 3, true),
           disk::kFlagCompressed | disk::kFlagEncrypted | disk::kFlagAds |
           disk::kFlagHardLink | disk::kFlagSizeUnknown,
           "bayrak: birden çok bit birlikte");

    // Düğümün boyu bellek bütçesinin dayanağı: 56 bayt, 64'e kaçmıyor.
    CheckU(sizeof(disk::Node), 56, "dugum: Node 56 bayt");
    // Boş maske ile DIRECTORY bayrağı karışmasın: dizinler dir alanından ayrılır.
    CheckU(disk::NodeFlags(FILE_ATTRIBUTE_DIRECTORY, 0, 1, false), 0,
           "bayrak: directory biti NodeFlag'e tasinmaz");
}

static void TestParseShadow()
{
    // Uzantı kaydı: baseRef != 0, SI yok, FN boyutları sıfır, $DATA yok → adsız kalır.
    auto rec = MakeRecord(0x0001, 60, 99);
    uint32_t off = 0x28;
    uint8_t fn[0x80];
    uint32_t fnLen = MakeFN(fn, 42, 0, 0, L":$DATA");
    off += PutAttr(rec.data(), off, 0x30, fn, fnLen);
    Put32(rec.data() + off, 0xFFFFFFFF);
    disk::MftFileInfo fi;
    Check(disk::ParseMftRecord(rec.data(), 1024, fi) && !fi.hasName,
          "parse: golge uzanti kaydi adsiz birakilir");

    // Sözleşme: adsız uzantı kaydı boyutunu ve taban indeksini raporlar;
    // RunMft bunu taban kaydın boş boyutunu doldurmak için kullanır.
    auto ext = MakeRecord(0x0001, 61, 99);
    off = 0x28;
    off += PutNonResData(ext.data(), off, 0x20, 0x40, 4096, 190);
    Put32(ext.data() + off, 0xFFFFFFFF);
    Check(disk::ParseMftRecord(ext.data(), 1024, fi) && !fi.hasName,
          "parse: adsiz uzanti kaydi cozulur");
    CheckU(fi.baseRef, 99, "uzanti: taban indeks baseRef+0x20'den");
    CheckU(fi.size, 190, "uzanti: DATA boyutu adsiz kayitta da raporlanir");
    CheckU(fi.alloc, 4096, "uzanti: DATA tahsisi raporlanir");
}

static void TestParseDataClassicLayout()
{
    // Klasik belge yerleşimi: koşu ofseti +0x10, +0x18 başlangıç VCN,
    // +0x20 geçerli veri uzunluğu. Tahsis/gerçek yine +0x28/+0x30'da; +0x20'ye
    // bakmak parçalanmış dosyada yanlış sayı verir.
    auto rec = MakeRecord(0x0001, 71, 0);
    uint8_t fn[0x80];
    uint32_t off = 0x28;
    uint32_t fnLen = MakeFN(fn, 3, 0, 0, L"eski.bin");
    off += PutAttr(rec.data(), off, 0x30, fn, fnLen);
    uint32_t d = off;
    off += PutNonResData(rec.data(), off, 0x10, 0x40, 8192, 4096);
    Put64(rec.data() + d + 0x20, 999999);           // geçerli veri uzunluğu (decoy)
    Put32(rec.data() + off, 0xFFFFFFFF);
    disk::MftFileInfo fi;
    Check(disk::ParseMftRecord(rec.data(), 1024, fi) && fi.hasName, "parse: klasik DATA yerlesimi kabul");
    CheckU(fi.size, 4096, "parse: klasik yerlesim gercek boyut (+0x20 degil)");
    CheckU(fi.alloc, 8192, "parse: klasik yerlesim tahsis");
}

static void TestParseResidentData()
{
    // Yerinde $DATA: mantıksal boyut değer uzunluğu; disk maliyeti ise içinde
    // durduğu MFT kaydının tahsisidir (1024), 200 bayt değil.
    auto rec = MakeRecord(0x0001, 72, 0);
    uint8_t fn[0x80];
    uint32_t off = 0x28;
    uint32_t fnLen = MakeFN(fn, 3, 0, 0, L"kucuk.txt");
    off += PutAttr(rec.data(), off, 0x30, fn, fnLen);
    uint8_t payload[200]; std::memset(payload, 'x', sizeof(payload));
    off += PutAttr(rec.data(), off, 0x80, payload, sizeof(payload));
    Put32(rec.data() + off, 0xFFFFFFFF);
    disk::MftFileInfo fi;
    Check(disk::ParseMftRecord(rec.data(), 1024, fi) && fi.hasName, "parse: yerinde DATA adi korur");
    CheckU(fi.size, 200, "parse: yerinde DATA boyut = deger uzunlugu");
    CheckU(fi.alloc, 1024, "parse: yerinde DATA tahsis = kaydin tahsisi");
}

static void TestParseFileNameSizes()
{
    // $DATA yoksa zincir $FILE_NAME'e düşer.
    auto rec = MakeRecord(0x0001, 73, 0);
    uint8_t fn[0x80];
    uint32_t off = 0x28;
    uint32_t fnLen = MakeFN(fn, 3, 8192, 4096, L"verify.bin");
    off += PutAttr(rec.data(), off, 0x30, fn, fnLen);
    Put32(rec.data() + off, 0xFFFFFFFF);
    disk::MftFileInfo fi;
    Check(disk::ParseMftRecord(rec.data(), 1024, fi) && fi.hasName, "parse: DATA yoksa ad korunur");
    CheckU(fi.size, 4096, "parse: DATA yoksa FN gercek boyut");
    CheckU(fi.alloc, 8192, "parse: DATA yoksa FN tahsis");

    // Boş dosya: ne DATA ne FN boyutu → 0/0, ama ağaçta adı ile kalır.
    auto zero = MakeRecord(0x0001, 74, 0);
    off = 0x28;
    fnLen = MakeFN(fn, 3, 0, 0, L"bos.txt");
    off += PutAttr(zero.data(), off, 0x30, fn, fnLen);
    Put32(zero.data() + off, 0xFFFFFFFF);
    Check(disk::ParseMftRecord(zero.data(), 1024, fi) && fi.hasName && fi.size == 0 && fi.alloc == 0,
          "parse: bos dosya 0/0 ama adli");

    // Olculen bozukluk (MpDetours.dll, $DATA yok): $FILE_NAME gercek alani
    // (6<<48)|332704 = 1688849860596640. Alt 48 bit Win32'in boyutu.
    auto g = MakeRecord(0x0001, 75, 0);
    off = 0x28;
    fnLen = MakeFN(fn, 3, 0, (6ull << 48) | 332704ull, L"MpDetours.dll", 0);
    off += PutAttr(g.data(), off, 0x30, fn, fnLen);
    Put32(g.data() + off, 0xFFFFFFFF);
    Check(disk::ParseMftRecord(g.data(), 1024, fi) && fi.size == 332704 && fi.alloc == 0,
          "parse: FN boyutunun kirli ust 16 biti maskeyle atilir");
}

// Sahte MFT bloğu: stride aralıklı FILE kayıtları; zeroEvery=4 gibi desenlerde
// arada hiç kullanılmamış (tamamen sıfır) kayıtlar bırakır.
static std::vector<uint8_t> MakeMftSpan(uint32_t span, uint32_t stride, uint32_t zeroEvery)
{
    std::vector<uint8_t> buf(span, 0);
    uint32_t k = 0;
    for (uint32_t p = 0; p + 0x28 <= span; p += stride, ++k)
    {
        if (zeroEvery && k % zeroEvery == zeroEvery - 1) continue;
        StampFile(buf.data() + p, 0x38);
    }
    return buf;
}

static void TestMeasureStride()
{
    std::vector<uint8_t> buf = MakeMftSpan(0x4000, 1024, 0);
    CheckU(disk::MeasureRecStride(buf.data(), 0x4000), 1024, "stride: olculen 1024 adim");

    // Bu makinedeki gerçek şekil: ilk 32 KB'ta her dördüncü kayıt sıfır.
    // "Hepsi FILE" kuralı doğru adımı reddedip 12288 gibi bir kata kaçiyordu.
    std::vector<uint8_t> gappy = MakeMftSpan(0x4000, 1024, 4);
    CheckU(disk::MeasureRecStride(gappy.data(), 0x4000), 1024, "stride: sifir kayitlar adimi ezmez");

    // Tek "FILE" byte'ı yanlış adımı seçtirmemeli: 0x200'de imza var, katları yok.
    StampFile(buf.data() + 0x200, 0x38);
    CheckU(disk::MeasureRecStride(buf.data(), 0x4000), 1024, "stride: tek sahte imza elenir");

    // 512'lik kayıtların katlarında imza duruyorsa 512 seçilir.
    std::vector<uint8_t> b2 = MakeMftSpan(0x4000, 512, 0);
    CheckU(disk::MeasureRecStride(b2.data(), 0x4000), 512, "stride: 512 adim");

    // attrsOff adım taşırırsa tüm adaylar elenir.
    std::vector<uint8_t> b3(0x4000, 0);
    StampFile(b3.data(), 0x38);
    for (uint32_t p = 1024; p + 0x28 <= 0x4000; p += 1024) StampFile(b3.data() + p, 0x2000);
    CheckU(disk::MeasureRecStride(b3.data(), 0x4000), 0, "stride: attrsOff tasirsa red");

    std::vector<uint8_t> b4(0x4000, 0);
    CheckU(disk::MeasureRecStride(b4.data(), 0x4000), 0, "stride: FILE sihir bayti yok");
    CheckU(disk::MeasureRecStride(b2.data(), 0x800), 0, "stride: yoklama sayisi yetmez");
}

static void TestNonResidentRunsOffset()
{
    uint32_t r = 0;
    // Bu buildin ölçülen başlığı: +0x10..0x17 sıfır, koşu ofset alanı +0x20 = 0x40.
    std::vector<uint8_t> a(0x60, 0);
    Put16(a.data() + 0x20, 0x40);
    a[0x40] = 0x21;                       // lenB=1, offB=2 → makul koşu başlığı
    Check(disk::NonResidentRunsOffset(a.data(), 0x60, r) && r == 0x40,
          "runsoff: +0x20 adayi (build 26300)");

    // Klasik belge yerleşimi: aynı değer +0x10'da durur.
    std::vector<uint8_t> b(0x60, 0);
    Put16(b.data() + 0x10, 0x48);
    b[0x48] = 0x11;
    Check(disk::NonResidentRunsOffset(b.data(), 0x60, r) && r == 0x48,
          "runsoff: +0x10 adayi (klasik)");

    // Aday alanlar koşu listesini göstermiyorsa 0x40 varsayılır.
    std::vector<uint8_t> c(0x60, 0);
    Put16(c.data() + 0x20, 0x40);         // ofset geçerli ama kosu baslik bayti 0
    Check(disk::NonResidentRunsOffset(c.data(), 0x60, r) && r == 0x40,
          "runsoff: gecersiz aday 0x40 varsayimina duser");

    // 0x40 baslangic icin yer yoksa reddedilir.
    std::vector<uint8_t> d(0x60, 0);
    Check(!disk::NonResidentRunsOffset(d.data(), 0x40, r), "runsoff: attrLen 0x40 reddi");
}

static void TestDataRuns()
{
    // {başlık, uzunluk baytları, delta baytları} — başlık: low nibble=lenB, high=offB
    std::vector<uint8_t> p = { 0x21, 0x08, 0x10, 0x00,      // len 8, delta +16 → lcn 16
                               0x21, 0x04, 0xF0, 0xFF,      // len 4, delta -16 → lcn 0
                               0x00 };
    std::vector<disk::DataRun> runs; bool abs = false;
    Check(disk::DecodeDataRuns(p.data(), (uint32_t)p.size(), abs, runs) && !abs &&
          runs.size() == 2, "runs: iki kosu cozuldu");
    if (runs.size() == 2)
    {
        CheckU(runs[0].lcn, 16, "runs: mutlak yigilim 1");
        CheckU(runs[0].len, 8,  "runs: uzunluk 1");
        CheckU(runs[1].lcn, 0,  "runs: negatif delta 2");
        CheckU(runs[1].len, 4,  "runs: uzunluk 2");
    }

    // sıkıştırma/sparse işareti: uzunluk var, konum yok → absolute
    std::vector<uint8_t> c = { 0x01, 0x04, 0x00 };
    runs.clear(); abs = false;
    Check(!disk::DecodeDataRuns(c.data(), (uint32_t)c.size(), abs, runs) && abs,
          "runs: sikistirma isareti absolute bayragi");

    // bitiş baytı yok → güvenilmez
    std::vector<uint8_t> t = { 0x11, 0x04, 0x10 };
    runs.clear(); abs = false;
    Check(!disk::DecodeDataRuns(t.data(), (uint32_t)t.size(), abs, runs),
          "runs: bitis bayti eksik reddi");
}

static void TestMftPlan()
{
    std::vector<disk::MftChunk> ch;
    std::vector<disk::DataRun>  runs = { { 192, 64 } };

    // Tek bitişik koşu: 64 küme, dilim 8 küme → 8 tam dilim, tek parça.
    Check(disk::PlanMftChunks(runs, 4096, 8, ch) && ch.size() == 8, "plan: tek kosu sekiz dilim");
    if (ch.size() == 8)
    {
        CheckU(ch[0].logical, 0, "plan: mantiksal baslangic");
        CheckU(ch[3].logical, 3ull * 8 * 4096, "plan: mantiksal adim");
        CheckU(ch[3].bytes, 8u * 4096, "plan: dilim dolu");
        CheckU(ch[3].pieces.size(), 1, "plan: bitişik kosuda tek parca");
        CheckU(ch[3].pieces[0].phys, (192ull + 24) * 4096, "plan: disk konumu");
        CheckU(ch[3].pieces[0].bufOff, 0, "plan: tampon ofseti");
    }

    // Koşu sınırı dilimin ortasında: ilk koşu dilimi dolduramıyor, ikincisi aynı
    // tamponun devamına yazılıyor — sınırda ayrı okuma/parse YOK.
    runs = { { 100, 3 }, { 500, 5 } };
    Check(disk::PlanMftChunks(runs, 4096, 4, ch) && ch.size() == 2, "plan: iki kosu iki dilim");
    if (ch.size() == 2)
    {
        CheckU(ch[0].pieces.size(), 2, "plan: sinirda iki parca");
        CheckU(ch[0].pieces[0].phys, 100ull * 4096, "plan: parca 1 disk");
        CheckU(ch[0].pieces[0].bytes, 3u * 4096, "plan: parca 1 uzunluk");
        CheckU(ch[0].pieces[1].phys, 500ull * 4096, "plan: parca 2 disk");
        CheckU(ch[0].pieces[1].bufOff, 3u * 4096, "plan: parca 2 tampon ofseti");
        CheckU(ch[0].bytes, 4u * 4096, "plan: dilim tampona sigan kadar dolu");
        CheckU(ch[1].pieces.size(), 1, "plan: ikinci dilim tek parca");
        CheckU(ch[1].pieces[0].phys, 501ull * 4096, "plan: ikinci dilim disk konumu");
    }

    // Kısa kuyruk: 5 küme, 4'lük dilim → ikinci dilim eksik doldu.
    runs = { { 0, 5 } };
    Check(disk::PlanMftChunks(runs, 4096, 4, ch) && ch.size() == 2, "plan: kisa kuyruk");
    if (ch.size() == 2)
    {
        CheckU(ch[1].logical, 4ull * 4096, "plan: kuyruk mantiksal konumu");
        CheckU(ch[1].bytes, 4096, "plan: kuyruk eksik dili");
    }

    // Geçersiz girdiler.
    Check(!disk::PlanMftChunks(runs, 0, 4, ch), "plan: kume boyu sifir reddedilir");
    Check(!disk::PlanMftChunks(runs, 4096, 0, ch), "plan: dilim boyu sifir reddedilir");
    std::vector<disk::DataRun> none;
    Check(!disk::PlanMftChunks(none, 4096, 4, ch), "plan: bos kosu listesi bos doner");

    // Değişmez: dilimler mantıksal olarak bitişik, tamponu boşluksuz/örtüşmesiz
    // doldurur ve fiziksel küme sırası koşu sırasıyla birebir aynıdır.
    runs = { { 7, 1 }, { 8, 13 }, { 900, 6 }, { 2000, 40 }, { 31, 2 } };
    const uint64_t clusBytes = 4096;
    Check(disk::PlanMftChunks(runs, clusBytes, 8, ch), "plan: parcali liste kabul");
    std::vector<uint64_t> phys;
    for (const auto& r : runs)
        for (uint64_t k = 0; k < r.len; ++k) phys.push_back((r.lcn + k) * clusBytes);
    bool     tidy = true;
    size_t   pc = 0;
    uint64_t nextLogical = 0;
    for (const auto& c : ch)
    {
        if (c.logical != nextLogical || c.bytes == 0) tidy = false;
        uint32_t off = 0;
        for (const auto& p : c.pieces)
        {
            if (p.bufOff != off || p.bytes == 0 || p.bytes % clusBytes) tidy = false;
            off += p.bytes;
            for (uint32_t k = 0; k < p.bytes / clusBytes; ++k)
                if (pc + k >= phys.size() || phys[pc + k] != p.phys + (uint64_t)k * clusBytes) tidy = false;
            pc += p.bytes / clusBytes;
        }
        if (off != c.bytes) tidy = false;
        nextLogical = c.logical + c.bytes;
    }
    Check(tidy, "plan: parcalar tamponu sirali ve bitsik doldurur");
    CheckU(pc, phys.size(), "plan: toplam kume korundu");
    CheckU(nextLogical, phys.size() * clusBytes, "plan: mantiksal kapalilik tam");
    // Kayıt indeksi mantıksal ofsetten türer; dilim sınırı kayıt adımına kat
    // olduğu için hiçbir kayıt ortada bölünmez.
    Check(!ch.empty() && (ch[0].logical % 1024) == 0 && (ch[ch.size() - 1].logical % 1024) == 0,
          "plan: dilim siniri kayit adimina kat");

    // Ayar sıkı sıkıştırılır: arayüz 1–8 verir, motor dışındaki değeri kabul etmez.
    disk::Scanner sc;
    sc.SetMftThreads(99); CheckU(sc.MftThreads(), 8,  "ayar: thread ust sinira cekildi");
    sc.SetMftThreads(-3); CheckU(sc.MftThreads(), 1,  "ayar: thread alt sinira cekildi");
    sc.SetMftThreads(5);  CheckU(sc.MftThreads(), 5,  "ayar: thread aralik icinde");
    // Arayüz varsayılanı çekirdek sayısından türer ve motorda da geçerli aralıkta
    // kalmalı (1-8); bu makinedeki değer test edilmez, yalnızca sınırı.
    const int d = disk::DefaultMftThreadCount();
    Check(d >= 1 && d <= 8, "varsayilan thread sayisi 1-8 icinde");
}

static void TestTreemap()
{
    std::vector<disk::Tile> tiles;

    disk::LayoutTreemap({}, 100, 100, tiles);
    Check(tiles.empty(), "treemap: bos girdi");

    disk::LayoutTreemap({ 0, 0 }, 100, 100, tiles);
    Check(tiles.empty(), "treemap: hepsi sifir");

    disk::LayoutTreemap({ 5 }, 120, 60, tiles);
    Check(tiles.size() == 1 && std::fabs(tiles[0].w - 120) < 1e-6 &&
          std::fabs(tiles[0].h - 60) < 1e-6, "treemap: tek kutu alani kaplar");

    // out[i] girdi indeksine karşılık gelir: {3,0,1,2} → 4 kutu, [1] boş.
    std::vector<uint64_t> sizes = { 3, 0, 1, 2 };
    disk::LayoutTreemap(sizes, 100, 100, tiles);
    Check(tiles.size() == 4, "treemap: girdi indeksi eslemesi boyutu");
    if (tiles.size() == 4)
    {
        Check(tiles[1].w == 0 && tiles[1].h == 0, "treemap: sifir girdi sifir kutu");
        double total = 0, want = 0;
        bool inside = true;
        for (size_t i = 0; i < 4; ++i)
        {
            total += tiles[i].w * tiles[i].h;
            if (tiles[i].x < -1e-9 || tiles[i].y < -1e-9 ||
                tiles[i].x + tiles[i].w > 100 + 1e-6 ||
                tiles[i].y + tiles[i].h > 100 + 1e-6) inside = false;
            if (i != 1) want += (double)sizes[i] / 6.0 * 10000.0;
        }
        Check(std::fabs(total - want) < 1.0, "treemap: alan korumu");
        Check(inside, "treemap: kutular sinir icinde");
        // en büyük girdi [0] en büyük alanı taşımalı
        double a0 = tiles[0].w * tiles[0].h, a2 = tiles[2].w * tiles[2].h,
               a3 = tiles[3].w * tiles[3].h;
        Check(a0 > a3 && a3 > a2, "treemap: alan siralamasi boyutu izler");
    }
}

static void TestUtf()
{
    const wchar_t* src = L"Ğüş İı çÜ  rapor.tmp";
    std::string u8 = disk::Utf16ToUtf8(src, (int)std::wcslen(src));
    std::wstring back = disk::Utf8ToUtf16(u8.data(), u8.size());
    Check(back == src, "utf: Turkce karakter gidi-donus");
    Check(disk::Utf16ToUtf8(L"", 0).empty() && disk::Utf8ToUtf16("", 0).empty(),
          "utf: bos girdi bos cikti");
}

// ---------------------------------------------------------------- önbellek
// ---------------------------------------------------------------- $SDS sözlüğü
//
// Sahte akış, ölçülen düzeni kurar: girdi başının +0x08 alanı KENDİ bayt
// ofsetini taşır (öz-ofset demiri), +0x14 yerdeğiştirmeli SECURITY_DESCRIPTOR
// başlığı (revizyon 1, +0x18'de sahiplik SID'inin SD başına göre ofseti).
// Gerçek diskten okuma yok; SID'ler de bellekte dizilir.

static void PutSid(std::vector<uint8_t>& s, size_t at, uint32_t rid)
{
    for (size_t i = 0; i < 8 + 12; ++i) s[at + i] = 0;
    s[at + 0] = 1;                    // SID_REVISION
    s[at + 1] = 3;                    // süb-otorite sayısı
    s[at + 7] = 5;                    // SECURITY_NT_AUTHORITY (6 bayt, büyük endian)
    Put32(&s[at + 8],  21);
    Put32(&s[at + 12], 1000);
    Put32(&s[at + 16], rid);
}

static void AppendSdsEntry(std::vector<uint8_t>& sds, uint32_t id, uint32_t rid)
{
    const size_t self  = sds.size();
    const size_t sidLen = 8 + 12;
    const size_t body  = 0x20 + sidLen;              // başlık + SD(12) + SID
    sds.resize(self + (body + 7) / 8 * 8, 0);        // girdiler 8 hizalı
    uint8_t* e = sds.data() + self;
    Put32(e + 0x00, 0xA5A5A5A5u);                    // hash: ayrıştırıcı bakmaz
    Put32(e + 0x04, id);
    Put32(e + 0x08, (uint32_t)self);                 // öz-ofset demiri
    Put32(e + 0x10, (uint32_t)body);                 // ölçülen: içeriğin sonu
    e[0x14] = 1;                                     // SD revizyonu
    Put16(e + 0x16, 0x8004);                         // SD kontrol bayrağı
    Put32(e + 0x18, 12);                             // owner SID'i SD başlığına göre
    Put32(e + 0x1C, (uint32_t)(12 + sidLen));        // group SID'i
    PutSid(sds, self + 0x20, rid);
}

static void TestSdsOwners()
{
    std::vector<disk::Owner> owners;
    std::unordered_map<uint32_t, uint32_t> idx;

    Check(disk::BuildSdsOwners({}, owners, idx) == 0 && owners.empty(),
          "sds: bos akista girdi yok");

    std::vector<uint8_t> zeros(512, 0);
    Check(disk::BuildSdsOwners(zeros, owners, idx) == 0,
          "sds: sıfır dolu akıştan girdi uydurulmadi");

    std::vector<uint8_t> sds;
    AppendSdsEntry(sds, 3800, 1001);
    AppendSdsEntry(sds, 3801, 500);
    Check(disk::BuildSdsOwners(sds, owners, idx) == 2, "sds: iki girdi cozuldu");
    Check(owners.size() == 2 && owners[0].secId == 3800u && owners[1].secId == 3801u,
          "sds: security id'ler sirayla tabloya girdi");
    // SID metni Win32'in ürettiği biçimdir: S-1-5-21-1000-<rid>.
    Check(owners[0].sid == "S-1-5-21-1000-1001" && owners[1].sid == "S-1-5-21-1000-500",
          "sds: sahiplik SID'i dogru metne cevrildi");
    Check(owners[0].name.empty() && !owners[0].attempted,
          "sds: ad cozumu tabloya girerken yapilmaz (lazy)");
    Check(idx.size() == 2 && idx[3800] == 1u && idx[3801] == 2u,
          "sds: id -> tablo indeksi 1-bazli");

    // Artan sıra ihlali: ikinci girdi öz-ofset denetimini geçse de geri id'liyse
    // reddedilir (ölçülen 3.545 girdide 0 ihlal; ihlal = yanlış yer tutucusu).
    std::vector<uint8_t> desc;
    AppendSdsEntry(desc, 4000, 1001);
    AppendSdsEntry(desc, 3000, 1002);
    Check(disk::BuildSdsOwners(desc, owners, idx) == 1, "sds: geri giden id reddedildi");

    // Yinelene id: ilk kazanır, ikincisi tabloya girmez.
    std::vector<uint8_t> dup;
    AppendSdsEntry(dup, 3800, 1001);
    AppendSdsEntry(dup, 3800, 1002);
    Check(disk::BuildSdsOwners(dup, owners, idx) == 1 && idx[3800] == 1u,
          "sds: yinelenen id'de ilk girdi kazanir");

    // Ölçülen ilk security id 256: altı meşru değil.
    std::vector<uint8_t> low;
    AppendSdsEntry(low, 0x80, 1001);
    Check(disk::BuildSdsOwners(low, owners, idx) == 0, "sds: 0x100 altı id alinmadi");

    // Öz-ofset demiri kırık akış hiçbir girdi üretmez.
    std::vector<uint8_t> broken;
    AppendSdsEntry(broken, 3800, 1001);
    Put32(broken.data() + 0x08, 999);
    Check(disk::BuildSdsOwners(broken, owners, idx) == 0, "sds: oz-ofseti tutmayan girdi reddi");

    // Yerdeğiştirme baytı 1 değilse orada SD başlığı yoktur.
    std::vector<uint8_t> rev;
    AppendSdsEntry(rev, 3800, 1001);
    rev[0x14] = 2;
    Check(disk::BuildSdsOwners(rev, owners, idx) == 0, "sds: SD revizyonu 1 olmayan girdi reddi");

    // SID'i akışın dışına taşan girdi atlanır (IsValidSid başa bakar, uzunluk
    // ayrıca denetlenir): çökme yok, ilk girdi kalır.
    std::vector<uint8_t> tail = sds;
    tail.resize(84);            // ikinci girdinin başlığı var, SID'inin sonu yok
    Check(disk::BuildSdsOwners(tail, owners, idx) == 1 && owners[0].secId == 3800u,
          "sds: akista tasan SID'li girdi atlandi");
}

static std::wstring TempCachePath()
{
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    wchar_t name[MAX_PATH];
    swprintf(name, MAX_PATH, L"tg_test_%lu.tgds", GetTickCount() ^ (GetCurrentProcessId() << 8));
    return std::wstring(tmp) + name;
}

static void WrI32(std::vector<char>& v, int32_t x) { const char* p = (const char*)&x; v.insert(v.end(), p, p + 4); }
static void WrI64(std::vector<char>& v, int64_t x) { const char* p = (const char*)&x; v.insert(v.end(), p, p + 8); }

// Geçerli bir önbellek gövdesi kurar: kök "C:\" + bir klasör + bir dosya.
// ver=1 eski biçimdir (mtime/owner/bayrak alanları yok), ver=2 sahibi ham sayı
// olarak taşır; ikisi de reddedilmeli. ver=3'te owner, dosonaki $SDS sözlüğünün
// 1-bazlı indeksidir.
static std::vector<char> BuildCache(int32_t rootOverride = 0, uint32_t ver = 3)
{
    // Düğüm kaydı: 3x i32 + 2x i64 + 4x i32 + 3x i32 = 56 bayt.
    auto node = [&](std::vector<char>& b, int32_t parent, int32_t firstChild, int32_t nextSib,
                    int64_t size, int64_t alloc, int32_t nameOff, int32_t nameLen, int32_t ext,
                    int32_t flags, int32_t mtime, int32_t owner, int32_t fl)
    {
        WrI32(b, parent); WrI32(b, firstChild); WrI32(b, nextSib);
        WrI64(b, size); WrI64(b, alloc);
        WrI32(b, nameOff); WrI32(b, nameLen); WrI32(b, ext); WrI32(b, flags);
        if (ver >= 2) { WrI32(b, mtime); WrI32(b, owner); WrI32(b, fl); }
    };
    std::vector<char> b;
    WrI32(b, (int32_t)0x53444754);              // TGDS
    WrI32(b, (int32_t)ver);                     // sürüm
    WrI32(b, 1);                                // motor: Mft
    WrI32(b, rootOverride);                     // kök indeksi
    std::string rp = "C:\\";
    WrI32(b, (int32_t)rp.size()); b.insert(b.end(), rp.begin(), rp.end());
    WrI64(b, 3);                                // düğüm sayısı
    node(b, -1, 1, -1, 5, 4096, 0, 0, -1, 1, 0, 0, 0);                       // 0: kök
    node(b, 0, 2, -1, 5, 4096, 0, 1, -1, 1, 0, 3, 0);                         // 1: klasör "x"
    node(b, 1, -1, -1, 5, 4096, 1, 1, -1, 0,                                 // 2: dosya "y"
         1700000000, ver >= 3 ? 1 : 3800, (int32_t)(disk::kFlagAds | disk::kFlagSizeUnknown));
    WrI64(b, 2);                               // havuz
    b.push_back('x'); b.push_back('y');
    WrI32(b, 0);                               // uzantı yok
    if (ver >= 3)
    {
        // $SDS sözlüğü: 3 satır. [0] adı çözülmüş, [1] denenmiş ama çözülememiş,
        // [2] hiç denenmemiş (imece çözümü bunun üzerinden ölçülür).
        auto owner = [&](uint32_t secId, const char* sid, const char* name, int32_t att)
        {
            WrI32(b, (int32_t)secId);
            std::string s = sid; WrI32(b, (int32_t)s.size()); b.insert(b.end(), s.begin(), s.end());
            std::string n = name; WrI32(b, (int32_t)n.size()); b.insert(b.end(), n.begin(), n.end());
            WrI32(b, att);
        };
        WrI32(b, 3);
        owner(3800, "S-1-5-21-1-2-3-1001", "DESKTOP\\Test", 1);
        owner(3801, "S-1-5-21-1-2-3-9999", "", 1);
        owner(256,  "S-1-5-18",            "", 0);
    }
    return b;
}

static bool WriteFile(const std::wstring& path, const std::vector<char>& data)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr) && wr == data.size();
    CloseHandle(h);
    return ok;
}

static void TestCache()
{
    disk::Scanner sc;
    Check(!sc.SaveCache(L"N:\\yok\\deneme.tgds"), "cache: bos agac kaydedilmez");
    Check(disk::Scanner::DefaultCachePath().find(L"diskscan.tgds") != std::wstring::npos,
          "cache: varsayilan yol adi");

    std::wstring p = TempCachePath();

    // Biçim sabitleri: 16B başlık + 4B rpLen + 3B "C:\" + 8B düğüm sayısı
    // + düğüm başına 56B (v2); ardından 8B havuz uzunluğu.
    const size_t poolLenPos = 16 + 4 + 3 + 8 + 3 * 56;

    disk::Scanner sc2;
    Check(WriteFile(p, BuildCache()) && sc2.LoadCache(p), "cache: gecerli dosya yuklendi");
    if (sc2.GetState() == disk::State::Done)
    {
        sc2.Lock();
        Check(sc2.Nodes().size() == 3 && sc2.Root() == 0, "cache: agac ve kok geri yuklendi");
        std::wstring path;
        bool bp = sc2.BuildPathLocked(2, path);
        // v2 alanları: değişim zamanı, sahip indeksi ve NodeFlag bayrakları
        // düğüm başına yazılıp okunur. v3'te sahip artık sözlük indeksidir.
        const disk::Node& f = sc2.Nodes()[2];
        Check(f.mtime == 1700000000u && f.owner == 1u &&
              f.flags == (uint16_t)(disk::kFlagAds | disk::kFlagSizeUnknown),
              "cache: mtime/sahip indeksi/bayraklar ayni geri yuklendi");
        // Sözlük: çözülmuş ad korundu, ikinci satır denendi ama adı yok, üçüncü
        // hiç denenmedi (kuyruk Join sonrası bile işler).
        const auto& ow = sc2.Owners();
        Check(ow.size() == 3 && ow[0].secId == 3800u && ow[0].name == "DESKTOP\\Test" &&
              ow[0].attempted && ow[1].sid == "S-1-5-21-1-2-3-9999" &&
              ow[1].name.empty() && ow[1].attempted &&
              ow[2].sid == "S-1-5-18" && !ow[2].attempted,
              "cache: SDS sozlugu tablo+ad olarak geri yuklendi");
        std::string lbl;
        disk::OwnerState ost = sc2.OwnerOfLocked(2, lbl);
        Check(ost == disk::kOwnerResolved && lbl == "DESKTOP\\Test",
              "cache: cozulmus sahiplik indeksi ada cevrildi");
        std::string lbl0;
        Check(sc2.OwnerOfLocked(0, lbl0) == disk::kOwnerNone && lbl0.empty(),
              "cache: sahibi olmayan dugum cozume gonderilmedi");
        sc2.Unlock();
        Check(bp && path == L"C:\\x\\y", "cache: yol kurma");

        // Tembel cozum iscisi: Join() onu kapatir ama arayuz gezinmeye devam
        // eder (olcülen hata: kapali isciye yazilan kuyruk sonsuza dek bekledi).
        // Ilk istek iscisi yeniden dogurmali ve ad arka planda cozulmeli.
        sc2.Join();
        std::string lblX;
        sc2.Lock();
        disk::OwnerState stPend = sc2.OwnerOfLocked(1, lblX);
        sc2.Unlock();
        Check(stPend == disk::kOwnerPending && lblX == "S-1-5-18",
              "sahip: kapali isciye de istek havale edildi, ham SID donduruldu");
        bool got = false; std::string lblDone;
        for (int k = 0; k < 60 && !got; ++k)
        {
            Sleep(50);
            sc2.Lock();
            got = sc2.OwnerOfLocked(1, lblDone) == disk::kOwnerResolved;
            sc2.Unlock();
        }
        Check(got && lblDone == "NT AUTHORITY\\SYSTEM",
              "sahip: cozum arka planda tamamlandi, isci yeniden basladi");
    }
    else { ++g_fail; std::printf("  BASARISIZ: cache yuklenmedi, dal testleri atlandi\n"); }

    // v1 gövdesi (56B'lik düğüm alanları yok) ve v2 gövdesi (sahip ham sayıydı,
    // sözlük yok) yüklenmez; kullanıcı eski dosyayı taramayla yenilemek zorundadır.
    WriteFile(p, BuildCache(0, 1));
    disk::Scanner scV1;
    Check(!scV1.LoadCache(p) && scV1.GetState() != disk::State::Done,
          "cache: eski surum (v1) reddedildi");

    WriteFile(p, BuildCache(0, 2));
    disk::Scanner scV2;
    Check(!scV2.LoadCache(p) && scV2.GetState() != disk::State::Done,
          "cache: sozluksurum (v2) reddedildi");

    auto badMagic = BuildCache();
    badMagic[0] = 'X';                          // TGDS sihir baytı bozuk
    WriteFile(p, badMagic);
    disk::Scanner sc3;
    Check(!sc3.LoadCache(p), "cache: sihir bayti yanlisi reddi");

    auto badRoot = BuildCache(42);              // kök indeksi düğüm sayısını aşar
    WriteFile(p, badRoot);
    disk::Scanner sc4;
    Check(!sc4.LoadCache(p) && sc4.GetState() != disk::State::Done,
          "cache: kok disi indeks reddi");

    auto trunc = BuildCache();
    trunc.resize(poolLenPos);                   // havuz alanı hiç yok
    WriteFile(p, trunc);
    disk::Scanner sc5;
    Check(!sc5.LoadCache(p), "cache: kirpilmis dosya reddi");

    auto shrunk = BuildCache();
    int64_t zero = 0;
    std::memcpy(shrunk.data() + poolLenPos, &zero, 8);   // havuz 0 ama düğümler ofset taşıyor
    WriteFile(p, shrunk);
    disk::Scanner sc6;
    Check(!sc6.LoadCache(p), "cache: havuz tasma reddi");

    // v3: düğüm.owner sözlük indeksidir; tablo uzunluğunu aşan indeks çöp bir ada
    // çözülmektense dosyanın tamamını reddettirir. Düğüm içindeki yeri: 12+16+16+4.
    auto oob = BuildCache();
    int32_t bigIdx = 99;
    std::memcpy(oob.data() + 31 + 2 * 56 + 48, &bigIdx, 4);
    WriteFile(p, oob);
    disk::Scanner sc7;
    Check(!sc7.LoadCache(p), "cache: sozluk disi sahiplik indeksi reddi");

    auto cutOwner = BuildCache();
    cutOwner.resize(cutOwner.size() - 3);        // son satirin attempted alanı eksik
    WriteFile(p, cutOwner);
    disk::Scanner sc8;
    Check(!sc8.LoadCache(p), "cache: kirpilmis sozluk reddi");

    // #11: uyumsuz önbellek oturum başında SİLİNİR (taramayla yeniden doğar).
    // Yabancı imza, okunamayan başlık ve güncel sürüm yerinde kalır.
    auto exists = [](const std::wstring& f)
    { return GetFileAttributesW(f.c_str()) != INVALID_FILE_ATTRIBUTES; };

    WriteFile(p, BuildCache(0, 1));
    Check(disk::Scanner::PurgeIncompatibleCache(p) && !exists(p), "purge: v1 dosya silindi");

    WriteFile(p, BuildCache(0, 2));
    Check(disk::Scanner::PurgeIncompatibleCache(p) && !exists(p), "purge: v2 dosya silindi");

    WriteFile(p, BuildCache());
    Check(!disk::Scanner::PurgeIncompatibleCache(p) && exists(p), "purge: guncel surum kaldi");
    disk::Scanner scKeep;
    Check(scKeep.LoadCache(p), "purge: silinmeyen guncel dosya hala yukleniyor");

    WriteFile(p, badMagic);
    Check(!disk::Scanner::PurgeIncompatibleCache(p) && exists(p), "purge: yabanci imzaya dokunulmadi");

    std::vector<char> tiny(4, 'T');              // başlık bile değil
    WriteFile(p, tiny);
    Check(!disk::Scanner::PurgeIncompatibleCache(p) && exists(p), "purge: baslik okunamayan kaldi");

    DeleteFileW(p.c_str());
    Check(!disk::Scanner::PurgeIncompatibleCache(p), "purge: dosya yoksa sessiz");

    DeleteFileW(p.c_str());
}

// ---------------------------------------------------------------- geçici ağaç yardımcıları
//
// Canlı yürüyüş (walk) testleri tarayıcı motorunu GERÇEK dosya sistemiyle
// çalıştırır ama yalnızca kendi yarattığımız %TEMP% altındaki izole ağaçta:
// kullanıcı verisine, sürücü köklerine ve Geri Dönüşüm Kutusu'na dokunulmaz.
// Silme testi de aynı ağacın kendi dosyalarını KALICI siler (çöp yok) — test
// bittiğinde ağacın tamamı zaten RemoveTree ile temizleniyor.

static bool MakeDir(const std::wstring& p) { return CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS; }

static bool WritePattern(const std::wstring& p, uint64_t bytes, uint8_t seed)
{
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::vector<char> blk(65536);
    for (size_t i = 0; i < blk.size(); ++i) blk[i] = (char)(seed + (uint8_t)(i % 7));
    uint64_t done = 0;
    bool ok = true;
    while (done < bytes)
    {
        uint64_t rem = bytes - done;
        DWORD take = rem < 65536ull ? (DWORD)rem : 65536;
        DWORD wr = 0;
        if (!WriteFile(h, blk.data(), take, &wr, nullptr) || wr != take) { ok = false; break; }
        done += take;
    }
    CloseHandle(h);
    return ok;
}

static void RemoveTree(const std::wstring& root)
{
    std::wstring pat = root + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            std::wstring nm = fd.cFileName;
            if (nm == L"." || nm == L"..") continue;
            std::wstring sub = root + L"\\" + nm;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(sub);
            else DeleteFileW(sub.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(root.c_str());
}

static std::wstring TestRoot()
{
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    wchar_t name[64];
    swprintf(name, 64, L"tg_disk_it_%lu", GetCurrentProcessId());
    return std::wstring(tmp) + name;
}

static disk::State WaitScan(disk::Scanner& sc, int maxMs = 30000)
{
    for (int waited = 0; waited < maxMs; waited += 20)
    {
        disk::State s = sc.GetState();
        if (s != disk::State::Scanning) return s;
        Sleep(20);
    }
    return sc.GetState();
}

static disk::DupState WaitDup(disk::Scanner& sc, int maxMs = 30000)
{
    for (int waited = 0; waited < maxMs; waited += 20)
    {
        disk::DupState s = sc.GetDupState();
        if (s != disk::DupState::Running) return s;
        Sleep(20);
    }
    return sc.GetDupState();
}

// Bulunan düğüm indeksi: kök altındaki "a\\b" gibi göreli yol (UTF-8, \\ ayracı).
static int FindNode(disk::Scanner& sc, const std::string& rel)
{
    sc.Lock();
    const auto& nodes = sc.Nodes();
    const auto& pool  = sc.Pool();
    int found = -1;
    for (int i = 0; i < (int)nodes.size(); ++i)
    {
        std::string full;
        int cur = i;
        while (cur >= 0 && cur < (int)nodes.size() && nodes[cur].parent >= 0)
        {
            if (full.size() > 4096) break;
            full = std::string(pool.data() + nodes[cur].nameOff, nodes[cur].nameLen) + "\\" + full;
            cur = nodes[cur].parent;
        }
        while (!full.empty() && (full.back() == '\\')) full.pop_back();
        if (full == rel) { found = i; break; }
    }
    sc.Unlock();
    return found;
}

static void TestWalkScan()
{
    std::wstring root = TestRoot();
    RemoveTree(root);
    if (!MakeDir(root) || !MakeDir(root + L"\\a") || !MakeDir(root + L"\\a\\b") ||
        !MakeDir(root + L"\\emptydir"))
    { ++g_fail; std::printf("  BASARISIZ: walk: gecici agac kurulamadi\n"); return; }
    const uint64_t S1 = 1000, S2 = 2500, S3 = 500, SD = 8192;
    bool filesOk =
        WritePattern(root + L"\\a\\one.txt", S1, 'A') &&
        WritePattern(root + L"\\a\\b\\two.bin", S2, 'B') &&
        WritePattern(root + L"\\c.log", S3, 'C') &&
        WritePattern(root + L"\\dup1.dat", SD, 'D') &&
        WritePattern(root + L"\\dup2.dat", SD, 'D') &&
        WritePattern(root + L"\\dup3.dat", SD, 'D') &&
        WritePattern(root + L"\\unique.dat", SD, 'Z');
    Check(filesOk, "walk: test agaci yazildi");

    // küme yuvarlama beklentisi hedef birimden sorulur (4096 varsayımı kırılgan)
    DWORD spc = 0, bps = 0;
    DWORD freeCl = 0;
    GetDiskFreeSpaceW(root.c_str(), &spc, &bps, &freeCl, nullptr);
    const uint64_t cs = (spc && bps) ? (uint64_t)spc * bps : 4096;
    auto roundUp = [cs](uint64_t v) { return ((v + cs - 1) / cs) * cs; };

    disk::Scanner sc;
    Check(!sc.Start(L""), "walk: bos hedef reddi");
    Check(sc.Start(root), "walk: tarama basladi");
    Check(WaitScan(sc) == disk::State::Done, "walk: tarama tamamlandi");
    Check(sc.UsedEngine() == disk::Engine::Walk, "walk: klasor hedefi MFT denenmez");

    disk::Progress pr; sc.GetProgress(pr);
    CheckU(pr.items, 7, "walk: dosya sayaci");

    // FindNode kendi kilidini alır; dış kilidin İÇİNDE çağrılırsa kilitlenme olur.
    int aIdx = FindNode(sc, "a");
    int bIdx = FindNode(sc, "a\\b");
    int tIdx = FindNode(sc, "a\\one.txt");
    int eIdx = FindNode(sc, "emptydir");
    Check(aIdx >= 0 && bIdx >= 0 && tIdx >= 0 && eIdx >= 0, "walk: beklenen yollar agacta var");

    sc.Lock();
    const auto& nodes = sc.Nodes();
    Check(nodes.size() == 4 + 7, "walk: dugum sayisi (4 klasor + 7 dosya)");
    int rootIdx = sc.Root();
    Check(rootIdx >= 0 && nodes[rootIdx].dir && nodes[rootIdx].parent == -1, "walk: kok dugumu");
    uint64_t wantAlloc = roundUp(S1) + roundUp(S2) + roundUp(S3) + 4 * roundUp(SD);
    CheckU(nodes[rootIdx].alloc, wantAlloc, "walk: kok tahsis toplami (kume yuvarlamali)");
    CheckU(nodes[rootIdx].size, S1 + S2 + S3 + 4 * SD, "walk: kok mantiksal toplam");
    if (aIdx >= 0 && bIdx >= 0 && tIdx >= 0 && eIdx >= 0)
    {
        CheckU(nodes[aIdx].size, S1 + S2, "walk: a/ alt agac toplami b'yi de sayar");
        CheckU(nodes[bIdx].size, S2, "walk: b/ alt agac toplami");
        CheckU(nodes[tIdx].size, S1, "walk: dosya mantiksal boyutu birebir");
        CheckU(nodes[tIdx].alloc, roundUp(S1), "walk: dosya tahsisi kume katidir");
        Check(nodes[eIdx].alloc == 0 && nodes[eIdx].size == 0, "walk: bos klasor sifir sayilir");
        CheckU((uint64_t)nodes[tIdx].parent, (uint64_t)aIdx, "walk: parent bagliligi");
        bool anyProt = false;
        for (const auto& n : nodes) if (n.protectedDir) anyProt = true;
        Check(!anyProt, "walk: gecici adlar korunakli sayilmaz");
        int extDat = -1, extTxt = -1;
        for (int i = 0; i < (int)sc.Exts().size(); ++i)
        {
            if (sc.Exts()[i].ext == "dat") extDat = i;
            if (sc.Exts()[i].ext == "txt") extTxt = i;
        }
        Check(extDat >= 0 && sc.Exts()[extDat].files == 4, "walk: .dat uzanti 4 dosya topladi");
        Check(extTxt >= 0 && sc.Exts()[extTxt].files == 1, "walk: .txt uzanti 1 dosya");
    }
    std::wstring path1;
    bool bp = tIdx >= 0 && sc.BuildPathLocked(tIdx, path1);
    sc.Unlock();
    Check(bp && path1 == root + L"\\a\\one.txt", "walk: BuildPath tam yolu kurar");

    // ---- önbellek turu: gercek tarama agaci yaz/oku ----
    std::wstring cp = root + L"\\tree.tgds";
    Check(sc.SaveCache(cp), "walk: cache kaydedildi");
    disk::Scanner sc2;
    Check(sc2.LoadCache(cp), "walk: cache geri yuklendi");
    sc2.Lock();
    sc.Lock();
    bool sameShape = (sc2.Nodes().size() == sc.Nodes().size()) && sc2.Root() == sc.Root();
    std::wstring rp2 = sc2.RootPathLocked();
    sc.Unlock();
    sc2.Unlock();
    Check(sameShape && rp2 == root, "walk: cache tarama agaciyla ayni sekil");

    // ---- yinelenen tespiti: 3 aynd.dat + 1 benzersiz ----
    Check(sc.StartDuplicates(), "dup: tarama bitti-sonrasi baslatilabilir");
    Check(WaitDup(sc) == disk::DupState::Done, "dup: cozum tamamlandi");
    sc.Lock();
    Check(sc.Dups().size() == 1, "dup: tam olarak tek grup");
    bool dupShape = false, dupFirst = false;
    std::wstring firstPath;
    if (sc.Dups().size() == 1)
    {
        const disk::DupGroup& g = sc.Dups()[0];
        dupShape = g.nodes.size() == 3 && g.size == SD && g.wasted == 2 * SD;
        sc.BuildPathLocked(g.nodes[0], firstPath);
        dupFirst = firstPath.size() >= 8 && firstPath.substr(firstPath.size() - 8) == L"dup1.dat";
    }
    sc.Unlock();
    Check(dupShape, "dup: grup boyutu/iskalanen alan dogru");
    Check(dupFirst, "dup: ilk yol siralama geregi dup1.dat");

    // ---- silme: YALNIZCA kendi yarattigimiz iki dosya, kalici, cop kavrami yok ----
    std::vector<std::wstring> doomed{ root + L"\\dup2.dat", root + L"\\dup3.dat" };
    disk::Scanner::DeleteResult res;
    bool del = disk::Scanner::DeletePaths(doomed, false, res);
    Check(del && res.attempted == 2 && !res.aborted, "delete: iki test dosyasi kalici silindi");
    Check(GetFileAttributesW(doomed[0].c_str()) == INVALID_FILE_ATTRIBUTES &&
          GetFileAttributesW(doomed[1].c_str()) == INVALID_FILE_ATTRIBUTES,
          "delete: dosyalar gercekten yok");

    // ---- iptal dayanikliligi: Cancel join eder, motor yeniden kullanilabilir ----
    sc.Cancel();
    Check(!sc.Busy(), "cancel: meşgul flagi temiz");
    disk::State sAfter = sc.GetState();
    Check(sAfter == disk::State::Cancelled || sAfter == disk::State::Done,
          "cancel: son durum Cancelled ya da tamamlanmissa Done");
    Check(sc.Start(root), "cancel: ayni tarayici yeniden tarayabilir");
    Check(WaitScan(sc) != disk::State::Scanning, "cancel sonrasi ikinci tarama sonuclandi");

    DeleteFileW(cp.c_str());
    RemoveTree(root);
    Check(GetFileAttributesW(root.c_str()) == INVALID_FILE_ATTRIBUTES, "walk: gecici agac temizlendi");
}

// Yinelenen taraması sürerken yeniden tarama: Start() eski işçiyi durdurup bekler,
// sonra havuzu temizler. Eski sırada (önce temizle, sonra join) işçi yeni ağaca
// yazıyor ve arayüz tek kilide takılı kalıyordu.
static void TestRescanDuringDup()
{
    std::wstring root = TestRoot();
    RemoveTree(root);
    if (!MakeDir(root) || !MakeDir(root + L"\\d"))
    { ++g_fail; std::printf("  BASARISIZ: duprescan: gecici agac kurulamadi\n"); return; }
    const uint64_t SD = 8192;
    bool ok = true;
    for (int i = 1; i <= 6; ++i)
    {
        wchar_t nm[32]; swprintf(nm, 32, L"\\d\\f%d.dat", i);
        ok = WritePattern(root + nm, SD, 'Q') && ok;
    }
    Check(ok, "duprescan: test agaci yazildi");

    disk::Scanner sc;
    Check(sc.Start(root), "duprescan: ilk tarama basladi");
    Check(WaitScan(sc) == disk::State::Done, "duprescan: ilk tarama bitti");
    Check(sc.StartDuplicates(), "duprescan: yinelenen taramasi baslatildi");
    const DWORD t0 = GetTickCount();
    Check(sc.Start(root), "duprescan: yinelenen sirasinda yeniden tarama kabul edildi");
    const DWORD took = GetTickCount() - t0;
    Check(WaitScan(sc) == disk::State::Done, "duprescan: ikinci tarama sonuclandi");
    Check(sc.GetDupState() != disk::DupState::Running, "duprescan: yinelenen iscisi durdu");
    sc.Lock();
    Check(sc.Dups().empty(), "duprescan: eski yinelenen sonucu yayilmadi");
    sc.Unlock();
    std::printf("  duprescan: Start() bekleme suresi %lu ms\n", (unsigned long)took);
    Check(took < 5000, "duprescan: yeniden tarama eski isci için beklerken kilitlenmedi");

    RemoveTree(root);
}

// İlk 4 KB'ı aynı, gerisi farklı olan iki dosya: örnek elek ikisini de 3. faza
// taşır, tam hash ayırmalı. Bu, örnek hash'in tek başına grup kanıtı olarak
// kullanılmadığını sabitler.
static bool WriteTwoSeeds(const std::wstring& p, uint64_t head, uint64_t tail,
                          uint8_t seedA, uint8_t seedB)
{
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::vector<char> blk(65536);
    uint64_t done = 0;
    bool ok = true;
    auto fill = [&](uint8_t seed) { for (size_t i = 0; i < blk.size(); ++i) blk[i] = (char)(seed + (uint8_t)(i % 7)); };
    auto put = [&](uint64_t bytes, uint8_t seed) {
        fill(seed);
        while (done < bytes)
        {
            uint64_t rem = bytes - done;
            DWORD take = rem < 65536ull ? (DWORD)rem : 65536;
            DWORD wr = 0;
            if (!WriteFile(h, blk.data(), take, &wr, nullptr) || wr != take) return false;
            done += take;
        }
        return true;
    };
    ok = put(head, seedA) && put(head + tail, seedB);
    CloseHandle(h);
    return ok;
}

static void TestDupSampleNotEnough()
{
    std::wstring root = TestRoot();
    RemoveTree(root);
    if (!MakeDir(root)) { ++g_fail; std::printf("  BASARISIZ: ornek: gecici klasor kurulamadi\n"); return; }
    const uint64_t HEAD = 4096, TAIL = 4096;
    bool ok = WriteTwoSeeds(root + L"\\s1.dat", HEAD, TAIL, 'A', 'B') &&
              WriteTwoSeeds(root + L"\\s2.dat", HEAD, TAIL, 'A', 'C') &&
              WriteTwoSeeds(root + L"\\s3.dat", HEAD, TAIL, 'A', 'B');
    Check(ok, "ornek: ilk kumesi ayniz farkli dosyalar yazildi");

    disk::Scanner sc;
    Check(sc.Start(root), "ornek: tarama basladi");
    Check(WaitScan(sc) == disk::State::Done, "ornek: tarama bitti");
    Check(sc.StartDuplicates(), "ornek: yinelenen basladi");
    const disk::DupState ds = WaitDup(sc);
    Check(ds == disk::DupState::Done, "ornek: yinelenen cozumu tamamlandi");
    sc.Lock();
    const size_t n = sc.Dups().size();
    bool shape = n == 1 && sc.Dups()[0].nodes.size() == 2;
    sc.Unlock();
    Check(shape, "ornek: yalnizca gercek ikili grup kaldi (tam hash ayniligi kontrol etti)");

    RemoveTree(root);
}

// Hard-link'li iki ad aynı içeriği taşır ama birini silmek yer açmaz: yinelenen
// sayilmamalari gerekir. WinSxS gibi ağaçlarda sürücünün "boşa giden yeri"
// aksi halde tamamen link çiftlerinden çıkar.
static void TestDupHardLinkSkipped()
{
    std::wstring root = TestRoot();
    RemoveTree(root);
    if (!MakeDir(root)) { ++g_fail; std::printf("  BASARISIZ: link: gecici klasor kurulamadi\n"); return; }
    const uint64_t SD = 8192;
    bool ok = WritePattern(root + L"\\hl1.dat", SD, 'L');
    ok = CreateHardLinkW((root + L"\\hl2.dat").c_str(), (root + L"\\hl1.dat").c_str(), nullptr) && ok;
    if (!ok)
    {
        // Hard-link hakkı olmayan birim/ağaç: testi zorla başarısız saymak yerine atla.
        std::printf("  link: CreateHardLinkW reddedildi, test atlandi\n");
        RemoveTree(root);
        return;
    }
    Check(true, "link: hard-link cifti kuruldu");

    disk::Scanner sc;
    Check(sc.Start(root), "link: tarama basladi");
    Check(WaitScan(sc) == disk::State::Done, "link: tarama bitti");
    Check(sc.StartDuplicates(), "link: yinelenen basladi");
    Check(WaitDup(sc) == disk::DupState::Done, "link: cozum tamamlandi");
    sc.Lock();
    const size_t n = sc.Dups().size();
    sc.Unlock();
    Check(n == 0, "link: hard-link cifti yinelenen sayilmadi");

    RemoveTree(root);
}

static void TestNewScannerGuards()
{
    disk::Scanner sc;
    Check(sc.GetState() == disk::State::Idle, "guard: yeni tarayici Idle dogar");
    Check(!sc.StartDuplicates(), "guard: tarama yokken dup baslatilamaz");
    Check(!sc.SaveCache(L"N:\\yok.tgds"), "guard: agacsiz SaveCache false");
    sc.Cancel();                     // isci yokken iptal kilitlememeli
    Check(!sc.Busy(), "guard: Idle iken Cancel zararsiz");
}

static void TestCycleGuard()
{
    // 2 düğümlü döngü: parent indeksleri sınır içinde ama zincir sonsuz.
    // BuildPathLocked 512 parçada durmalı; LoadCache bunu kabul eder (indeksler
    // geçerli), tehlike çizim anında tutulur.
    std::vector<char> b;
    WrI32(b, (int32_t)0x53444754); WrI32(b, 3); WrI32(b, 1); WrI32(b, 0);
    std::string rp = "C:\\";
    WrI32(b, (int32_t)rp.size()); b.insert(b.end(), rp.begin(), rp.end());
    WrI64(b, 2);
    WrI32(b, 1); WrI32(b, -1); WrI32(b, -1); WrI64(b, 1); WrI64(b, 4096);
    WrI32(b, 0); WrI32(b, 0); WrI32(b, -1); WrI32(b, 1);
    WrI32(b, 0); WrI32(b, 0); WrI32(b, 0);
    WrI32(b, 0); WrI32(b, -1); WrI32(b, -1); WrI64(b, 1); WrI64(b, 4096);
    WrI32(b, 0); WrI32(b, 0); WrI32(b, -1); WrI32(b, 0);
    WrI32(b, 0); WrI32(b, 0); WrI32(b, 0);
    WrI64(b, 0); WrI32(b, 0);
    WrI32(b, 0);                                  // v3: boş $SDS sözlüğü

    std::wstring p = TestRoot() + L".tgds";
    MakeDir(TestRoot());
    bool wrote = WriteFile(p, b);
    disk::Scanner sc;
    bool loaded = wrote && sc.LoadCache(p);
    Check(loaded, "dongu: gecerli sinirli cache yuklendi");
    if (loaded)
    {
        sc.Lock();
        std::wstring out;
        bool bp = sc.BuildPathLocked(1, out);
        sc.Unlock();
        Check(!bp, "dongu: BuildPath 512 parcada durur, sonsuz donguya girmez");
    }
    DeleteFileW(p.c_str());
    RemoveDirectoryW(TestRoot().c_str());
}

static void TestNamespacePreference()
{
    // Namespace alan ölçümü: ns 1/3 = Win32 adı (yeni dosyalarda 3), 2 = DOS 8.3.
    auto rec = MakeRecord(0x0001, 8, 0);
    uint32_t off = 0x28;
    uint8_t fn[0x80];
    uint32_t l1 = MakeFN(fn, 1, 100, 50, L"SHORTN~1.TX~", 2);        // DOS
    off += PutAttr(rec.data(), off, 0x30, fn, l1);
    uint32_t l2 = MakeFN(fn, 1, 100, 50, L"rapor.txt", 3);           // Win32
    off += PutAttr(rec.data(), off, 0x30, fn, l2);
    Put32(rec.data() + off, 0xFFFFFFFF);
    disk::MftFileInfo fi;
    Check(disk::ParseMftRecord(rec.data(), 1024, fi) && fi.name == L"rapor.txt",
          "FN: Win32 adi DOS adina tercih edilir");

    // Sıra tersine de Win32 kazanmalı (ikinci gelen DOS ilkini ezmemeli).
    auto r4 = MakeRecord(0x0001, 11, 0);
    off = 0x28;
    l1 = MakeFN(fn, 1, 100, 50, L"rapor.txt", 3);
    off += PutAttr(r4.data(), off, 0x30, fn, l1);
    l2 = MakeFN(fn, 1, 100, 50, L"SHORTN~1.TX~", 2);
    off += PutAttr(r4.data(), off, 0x30, fn, l2);
    Put32(r4.data() + off, 0xFFFFFFFF);
    Check(disk::ParseMftRecord(r4.data(), 1024, fi) && fi.name == L"rapor.txt",
          "FN: once gelen Win32 adi DOS ile ezilmez");

    // yalnızca DOS varsa o kullanılır
    auto r2 = MakeRecord(0x0001, 9, 0);
    off = 0x28;
    l1 = MakeFN(fn, 1, 100, 50, L"SHORTN~1.TX~", 2);
    off += PutAttr(r2.data(), off, 0x30, fn, l1);
    Put32(r2.data() + off, 0xFFFFFFFF);
    Check(disk::ParseMftRecord(r2.data(), 1024, fi) && fi.hasName && fi.name == L"SHORTN~1.TX~",
          "FN: tek DOS adi kabul edilir");

    // FN gövdesi eksik (ad tam sigmiyor) → adsız
    auto r3 = MakeRecord(0x0001, 10, 0);
    off = 0x28;
    uint32_t full = MakeFN(fn, 1, 100, 50, L"tam_ad.txt");
    off += PutAttr(r3.data(), off, 0x30, fn, full - 4);   // body kesik
    Put32(r3.data() + off, 0xFFFFFFFF);
    Check(disk::ParseMftRecord(r3.data(), 1024, fi) && !fi.hasName,
          "FN: kirpan govde adı cokurmuyor, dus butunu korur");
}

static void TestPlausibleAlloc()
{
    // Olculen bozukluk: vbc.rsp tahsisi u64+0x28'te 0xA000000001000 (mantiksal
    // 1467, gercek tahsis 4096). Yuksek 32 bit curumuk; geometri kurali duzeltir.
    const uint64_t C = 4096;
    CheckU(disk::PlausibleAlloc(1467, 0xA000000001000ull, C), 4096, "alloc: bozuk yuksek 32 bit yuvarlamaya doner");
    CheckU(disk::PlausibleAlloc(1329, 0x43000000001000ull, C), 4096, "alloc: ikinci bozuk kayit");
    CheckU(disk::PlausibleAlloc(844496, 847872, C), 847872, "alloc: saglam deger oldugu gibi kalir");
    CheckU(disk::PlausibleAlloc(844496, 0, C), 847872, "alloc: dolu dosyanin 0 tahsisi yuvarlanır");
    CheckU(disk::PlausibleAlloc(4096, 4096, C), 4096, "alloc: kume katinda esitlik");
    CheckU(disk::PlausibleAlloc(5ull << 30, 5ull << 30, C), 5ull << 30, "alloc: 4GB ustu saglam deger kirpilmaz");
    // Sikistirilmis/seyrek dosya gercek boyuttan az yer kaplar; ona dokunulmaz.
    CheckU(disk::PlausibleAlloc(100ull << 20, 3ull << 20, C), 3ull << 20, "alloc: seyrek/sikistik deger korunur");
    CheckU(disk::PlausibleAlloc(0, 0, C), 0, "alloc: bos dosya bos");
    // Bos ama yerinde $DATA tasuyan dosya kaydın tahsisini öder (ölçülen 1024).
    CheckU(disk::PlausibleAlloc(0, 1024, C), 1024, "alloc: bos dosyanin kayit tahsisi korunur");
    CheckU(disk::PlausibleAlloc(0, 0xA000000001000ull, C), 0, "alloc: boyutsuz kaydin dev tahsisi silinir");
}

static void TestHardLinkNameChoice()
{
    // Olculen kernel32.dll kaydi: ns=3 bag WinSxS ebeveynli ve alloc/real=0,
    // ns=0 bag System32 ebeveynli ve dolu boyutlu. Tek baga asildigi icin
    // boyut tasayan bag secilmeli; aksi halde System32 agacta kayboluyor.
    auto rec = MakeRecord(0x0001, 0x188, 0);
    uint32_t off = 0x28;
    uint8_t fn[0x80];
    off += PutAttr(rec.data(), off, 0x30, fn, MakeFN(fn, 0x453A, 0, 0, L"kernel32.dll", 3));
    off += PutAttr(rec.data(), off, 0x30, fn, MakeFN(fn, 0x1944, 847872, 844496, L"kernel32.dll", 0));
    off += PutNonResData(rec.data(), off, 0x20, 0x40, 847872, 844496);
    Put32(rec.data() + off, 0xFFFFFFFF);
    disk::MftFileInfo fi;
    Check(disk::ParseMftRecord(rec.data(), 1024, fi) && fi.hasName, "link: iki bagli kayit cozuldu");
    CheckU(fi.parent, 0x1944, "link: boyut tasiyan bagin ebeveyni secilir");
    CheckU(fi.links, 2, "link: bag sayisi raporlanir");
    CheckU(fi.nameNs, 0, "link: secilen bagin namespace'i");
    CheckU(fi.size, 844496, "link: boyut DATA'dan gelir");

    // Sira tersi de ayni sonucu vermeli: once gelen dolu bag, sonra gelen
    // boslu (Win32) bag tarafindan ezilmemeli.
    auto r2 = MakeRecord(0x0001, 0x188, 0);
    off = 0x28;
    off += PutAttr(r2.data(), off, 0x30, fn, MakeFN(fn, 0x1944, 847872, 844496, L"kernel32.dll", 0));
    off += PutAttr(r2.data(), off, 0x30, fn, MakeFN(fn, 0x453A, 0, 0, L"kernel32.dll", 3));
    Put32(r2.data() + off, 0xFFFFFFFF);
    Check(disk::ParseMftRecord(r2.data(), 1024, fi) && fi.parent == 0x1944,
          "link: once gelen dolu bag korunur");

    // Her iki bag de boyutsuza kismis Win32/POSIX karisiminda Win32 kazanir.
    auto r3 = MakeRecord(0x0001, 0x188, 0);
    off = 0x28;
    off += PutAttr(r3.data(), off, 0x30, fn, MakeFN(fn, 0x1944, 0, 0, L"kernel32.dll", 0));
    off += PutAttr(r3.data(), off, 0x30, fn, MakeFN(fn, 0x453A, 0, 0, L"kernel32.dll", 3));
    Put32(r3.data() + off, 0xFFFFFFFF);
    Check(disk::ParseMftRecord(r3.data(), 1024, fi) && fi.parent == 0x453A && fi.nameNs == 3,
          "link: boyutsuz esitlikte Win32 one gecer");
}

static void TestFileNameSizeConsensus()
{
    // Olculen cy.pak kaydi ($DATA yok): baglar 40250921670801838 ve 1178030
    // veriyor; maske sonrasi 1858030 ve 1178030, Win32 ise 1178030. En kucuk
    // sifir-olmayan bag dogruyu verir.
    auto rec = MakeRecord(0x0001, 348483, 0);
    uint32_t off = 0x28;
    uint8_t fn[0x80];
    off += PutAttr(rec.data(), off, 0x30, fn,
                   MakeFN(fn, 0x24D0B, 0, (143ull << 48) | 1858030ull, L"cy.pak.DATA", 0));
    off += PutAttr(rec.data(), off, 0x30, fn, MakeFN(fn, 0x430, 0, 1178030ull, L"cy.pak", 0));
    Put32(rec.data() + off, 0xFFFFFFFF);
    disk::MftFileInfo fi;
    Check(disk::ParseMftRecord(rec.data(), 1024, fi) && fi.hasName, "konsensüs: iki bağ çözüldü");
    CheckU(fi.size, 1178030, "konsensüs: en küçük sıfır-olmayan bağ boyutu kazanır");
    CheckU(fi.links, 2, "konsensüs: bağ sayısı");

    // Sıra tersi de aynı boyutu vermeli.
    auto r2 = MakeRecord(0x0001, 348483, 0);
    off = 0x28;
    off += PutAttr(r2.data(), off, 0x30, fn, MakeFN(fn, 0x430, 0, 1178030ull, L"cy.pak", 0));
    off += PutAttr(r2.data(), off, 0x30, fn,
                   MakeFN(fn, 0x24D0B, 0, (143ull << 48) | 1858030ull, L"cy.pak.DATA", 0));
    Put32(r2.data() + off, 0xFFFFFFFF);
    Check(disk::ParseMftRecord(r2.data(), 1024, fi) && fi.size == 1178030,
          "konsensüs: önce gelen doğru boyut sonra gelen çöpü ezer");

    // Olculen EppManifest.dll: ns=2 bağ 1060160 (doğru), ns=0 bağ (4<<48)|1060160.
    // Ad Win32/POSIX'tan, boyut maskeden gelir; DOS adı asla seçilmez.
    auto r3 = MakeRecord(0x0001, 300, 0);
    off = 0x28;
    off += PutAttr(r3.data(), off, 0x30, fn, MakeFN(fn, 0x47527, 0, 1060160ull, L"EPPMAN~1.DLL", 2));
    off += PutAttr(r3.data(), off, 0x30, fn,
                   MakeFN(fn, 0x5131E, 0, (4ull << 48) | 1060160ull, L"EppManifest.dll", 0));
    Put32(r3.data() + off, 0xFFFFFFFF);
    Check(disk::ParseMftRecord(r3.data(), 1024, fi) && fi.name == L"EppManifest.dll" &&
          fi.size == 1060160, "konsensüs: DOS 8.3 adı seçilmez, boyut maskeyle doğru");

    // $DATA varsa konsensüs onu ezmez: bağ çöp de olsa otorite $DATA'dır.
    auto r4 = MakeRecord(0x0001, 301, 0);
    off = 0x28;
    off += PutAttr(r4.data(), off, 0x30, fn, MakeFN(fn, 3, 0, 999ull, L"gercek.bin", 3));
    off += PutNonResData(r4.data(), off, 0x20, 0x40, 4096, 844496);
    Put32(r4.data() + off, 0xFFFFFFFF);
    Check(disk::ParseMftRecord(r4.data(), 1024, fi) && fi.size == 844496 && fi.alloc == 4096,
          "konsensüs: DATA varken FN boyutu kullanılmaz");

    // Olculen asil hata: idx=570015 kaydinda $DATA @0x1F8 uzunluk alani
    // 0x0026_0048 okunuyordu (dogrusu 0x48; used=0x248 ile dogrulanir).
    // Yürüyüs "off+len > recLen" ile kisaliyor, $DATA HIC YOK sayiliyordu.
    auto r5 = MakeRecord(0x0001, 570015, 0);
    off = 0x28;
    off += PutAttr(r5.data(), off, 0x30, fn, MakeFN(fn, 0x8B29E, 0, 0, L"tur.mp4", 1));
    uint32_t dOff = off;
    off += PutNonResData(r5.data(), off, 0x20, 0x40, 331776, 329288);
    Put32(r5.data() + dOff + 4, 0x00260048u);       // kirli üst 16 bit
    Put32(r5.data() + off, 0xFFFFFFFF);
    Check(disk::ParseMftRecord(r5.data(), 1024, fi) && fi.size == 329288 && fi.alloc == 331776,
          "parse: öznitelik uzunluğunun kirli üst 16 biti yürüyüşü kesemez");
}

static void TestTreemapExtreme()
{
    // 1 dev + 20 toz: alan korunumu ve sinirlar icinde kalma.
    std::vector<uint64_t> sizes{ 1000000000000ull };
    for (int i = 0; i < 20; ++i) sizes.push_back(1);
    std::vector<disk::Tile> tiles;
    disk::LayoutTreemap(sizes, 200.0, 100.0, tiles);
    Check(tiles.size() == 21, "treemap-extreme: girdi eslemesi");
    double area = 0; bool inside = true;
    for (const auto& t : tiles)
    {
        area += t.w * t.h;
        if (t.x < -1e-9 || t.y < -1e-9 || t.x + t.w > 200 + 1e-6 || t.y + t.h > 100 + 1e-6)
            inside = false;
    }
    Check(std::fabs(area - 20000.0) < 1.0, "treemap-extreme: alan korunumu");
    Check(inside, "treemap-extreme: asim yok");
}

static void TestListSources()
{
    auto v = disk::ListSources();
    bool sane = !v.empty();
    for (const auto& s : v)
        if (s.path.empty() || s.label.empty() ||
            !(s.path[1] == L':' || (s.path.size() > 1 && s.path[0] == L'\\')))
            sane = false;
    Check(sane, "sources: her hedefin yolu ve etiketi var");
    for (const auto& s : v)
        if (s.path.size() >= 2 && s.path[1] == L':' && (toupper(s.path[0]) == L'C'))
        {
            Check(s.label.size() >= 2 && s.label[0] == L'C' && s.label[1] == L':',
                  "sources: C etiketi 'C:' ile baslar");
            break;
        }
}

int main()
{
    std::printf("== disk analizi birim testleri ==\n");
    TestParseFile();
    TestParseDirAndDeleted();
    TestParseShadow();
    TestParseDataClassicLayout();
    TestParseResidentData();
    TestParseFileNameSizes();
    TestSiFields();
    TestSizeUnknown();
    TestNodeRecordShape();
    TestNamespacePreference();
    TestHardLinkNameChoice();
    TestFileNameSizeConsensus();
    TestPlausibleAlloc();
    TestMeasureStride();
    TestNonResidentRunsOffset();
    TestDataRuns();
    TestMftPlan();
    TestTreemap();
    TestTreemapExtreme();
    TestUtf();
    TestSdsOwners();
    TestCache();
    TestNewScannerGuards();
    TestCycleGuard();
    TestListSources();
    TestWalkScan();
    TestRescanDuringDup();
    TestDupSampleNotEnough();
    TestDupHardLinkSkipped();
    std::printf("%d basarili, %d basarisiz\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
