#include "diskanalyzer.hpp"

// windows.h makroları std::min/std::max'ı bozuyor.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <bcrypt.h>
#include <sddl.h>      // ConvertSidToStringSidA / ConvertStringSidToSidA / LookupAccountSidA
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <deque>
#include <iterator>
#include <thread>
#include <unordered_map>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

namespace disk
{
    namespace
    {
        uint16_t Rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
        uint32_t Rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
        uint64_t Rd64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }

        std::string ToLowerAscii(std::string s)
        {
            for (char& c : s)
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            return s;
        }

        // Dosya adından küçük harfli uzantı (son '.', en fazla 12 karakter,
        // yalnız alfanümerik/+-_# imli); bozukta boş.
        std::string ExtOf(const std::string& name)
        {
            size_t dot = name.find_last_of('.');
            if (dot == std::string::npos || dot + 1 >= name.size()) return {};
            std::string e = ToLowerAscii(name.substr(dot + 1));
            if (e.size() > 12) return {};
            for (char c : e)
            {
                bool okChar = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                              c == '+' || c == '-' || c == '_' || c == '#';
                if (!okChar) return {};
            }
            return e;
        }

        // Harfli sürücünün kökündeki sistem ağaçları: silme teklif edilmeyenler.
        bool IsProtectedTopName(const std::string& lowerUtf8)
        {
            static const char* kNames[] = {
                "windows", "$recycle.bin", "system volume information", "recovery",
            };
            for (const char* n : kNames) if (lowerUtf8 == n) return true;
            return false;
        }

        uint64_t RoundUpClusters(uint64_t bytes, uint64_t cluster)
        {
            if (bytes == 0) return 0;
            if (cluster == 0) return bytes;
            return ((bytes + cluster - 1) / cluster) * cluster;
        }

        std::wstring VolumeOf(const std::wstring& path)
        {
            if (path.size() >= 2 && path[1] == L':') return path.substr(0, 3);
            return {};
        }

        bool LooksLikeVolumeRoot(const std::wstring& t)
        {
            return t.size() == 3 && t[1] == L':' && (t[2] == L'\\' || t[2] == L'/');
        }

        constexpr uint32_t kAttrStdInfo  = 0x10;
        constexpr uint32_t kAttrFileName = 0x30;
        constexpr uint32_t kAttrData     = 0x80;
        constexpr uint32_t kAttrEnd      = 0xFFFFFFFF;
        constexpr uint32_t kRootRecordIdx = 5;   // "$" kök dizini
        // $Secure: sabit MFT kaydı. Adla açmak (C:\$Secure:$SDS) yöneticiyken de
        // ERROR_ACCESS_DENIED veriyor; akış yalnızca bu kaydın UZANTI
        // kayıtlarındaki adlandırılmış $DATA parçalarından okunabiliyor (ölçülen).
        constexpr uint64_t kSecureRecordIdx = 9;

        // Öznitelik uzunluğu (u32 başlık +0x04). Bu alanın ÜST 16 biti de çöp
        // taşıyabiliyor: ölçülen kayıt idx=570015'te $DATA @0x1F8 için
        // len=0x0026_0048 okundu; doğrusu 0x48 (0x1F8 + 0x48 + 0x8 bitiş =
        // 0x248 = kaydın used alanı). Uzunluk bozuk okununca yürüyüş
        // "off+len > recLen" ile kopuyor ve $DATA HİÇ YOK sayılıyordu: dosyanın
        // boyutu 0 kalıyordu (ölçülen 10.704 düğüm; örnekler WinSxS
        // manifestoları, .etl loglar, WindowsApps .png'ler). MFT kayıt boyu en
        // fazla 4096 olduğundan her öznitelik 16 bite sığar; üstü zaten 0 olan
        // kayıtlarda maske etkisizdir.
        inline uint32_t AttrLen(const uint8_t* rec, uint32_t off)
        {
            return Rd32(rec + off + 4) & 0xFFFFu;
        }

        inline bool RecHeaderOk(const uint8_t* rec, uint32_t recLen, uint32_t& attrsOff, uint16_t& flags)
        {
            if (recLen < 0x38) return false;
            if (std::memcmp(rec, "FILE", 4) != 0) return false;
            attrsOff = Rd16(rec + 0x14);
            flags    = Rd16(rec + 0x16);
            return attrsOff >= 0x28 && attrsOff < recLen;
        }
    } // namespace

    int DefaultMftThreadCount()
    {
        // GetNativeSystemInfo: 32-bit çalışırken de tüm mantıksal çekirdekleri sayar.
        SYSTEM_INFO si; GetNativeSystemInfo(&si);
        const int n = (int)si.dwNumberOfProcessors;
        return n < 1 ? 1 : (n > 8 ? 8 : n);
    }

    namespace {

        bool ReadAt(HANDLE h, uint64_t offBytes, void* buf, uint32_t bytes)
        {
            LARGE_INTEGER li; li.QuadPart = (LONGLONG)offBytes;
            if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) return false;
            DWORD got = 0;
            return ReadFile(h, buf, bytes, &got, nullptr) != FALSE && got == bytes;
        }

        struct BootInfo
        {
            uint32_t bytesPerSec = 512;
            uint32_t secPerClus  = 8;
            uint64_t mftClusOff  = 0;
            uint32_t recSize     = 1024;
            bool     ok          = false;
        };

        // $Boot +0x40 yeni Windows yapılarında kayıt boyunu taşımıyor (bu
        // makinede ölçülen 246). Adım, $MFT başlangıcında MeasureRecStride ile
        // iki ardışık "FILE" imzasından ölçülür.
        BootInfo ReadBoot(HANDLE h)
        {
            BootInfo b;
            constexpr uint32_t cap = 1u << 20;         // adım ölçümü: ilk 1 MB yeterli
            uint8_t* buf = (uint8_t*)VirtualAlloc(nullptr, cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!buf) return b;
            if (ReadAt(h, 0, buf, 512))
            {
                b.bytesPerSec = Rd16(buf + 11);
                b.secPerClus  = buf[13];
                b.mftClusOff  = Rd64(buf + 0x30);
            }
            const uint64_t clusBytes = (uint64_t)b.bytesPerSec * b.secPerClus;
            if (b.mftClusOff && clusBytes >= 512 && clusBytes <= cap)
            {
                uint32_t span = (uint32_t)std::min<uint64_t>(clusBytes * 16, cap);
                if (ReadAt(h, b.mftClusOff * clusBytes, buf, span))
                {
                    b.recSize = MeasureRecStride(buf, span);
                    b.ok      = b.recSize != 0;
                }
            }
            VirtualFree(buf, 0, MEM_RELEASE);
            return b;
        }

        // $MFT (kayıt 0) adsız non-resident $DATA koşularını çözer.
        bool MftRunsFromRecord0(const uint8_t* rec, uint32_t recLen, std::vector<DataRun>& runs)
        {
            uint32_t attrsOff = 0; uint16_t flags = 0;
            if (!RecHeaderOk(rec, recLen, attrsOff, flags)) return false;
            uint32_t off = attrsOff;
            while (off + 0x40 <= recLen)
            {
                uint32_t type = Rd32(rec + off);
                if (type == kAttrEnd) break;
                uint32_t len = AttrLen(rec, off);
                if (len < 0x40 || off + len > recLen) break;
                uint8_t  nonRes  = rec[off + 8];
                uint8_t  nameLen = rec[off + 0x09];
                if (type == kAttrData && nonRes && nameLen == 0)
                {
                    bool abs = false;
                    uint32_t r = 0x40;
                    if (!NonResidentRunsOffset(rec + off, len, r)) { runs.clear(); }
                    else if (DecodeDataRuns(rec + off + r, len - r, abs, runs) && !abs) return true;
                    runs.clear();
                }
                off += len;
            }
            return false;
        }

        // $Secure:$SDS parçası: taban kaydın uzantısındaki adlandırılmış
        // non-resident $DATA başlığından çözülen koşu listesi + VCN aralığı.
        struct SdsPart
        {
            uint64_t             lowVcn = 0, highVcn = 0;
            uint64_t             real   = 0;   // akışın mantıksal toplam boyu (parça tam raporu)
            uint64_t             clus   = 0;   // bu parçanın koşularının kapsadığı küme
            std::vector<DataRun> runs;
        };

        // Kayıttaki "$SDS" adlı non-resident $DATA başlığını çözer. Ad denetimi
        // zorunlu: $Secure'ın uzantılarında başka adlı akış da bulunabiliyor.
        bool CollectSdsPart(const uint8_t* rec, uint32_t recLen, std::vector<SdsPart>& parts)
        {
            uint32_t attrsOff = 0; uint16_t flags = 0;
            if (!RecHeaderOk(rec, recLen, attrsOff, flags)) return false;
            bool hit = false;
            for (uint32_t off = attrsOff, guard = 0;
                 guard < 24 && off + 0x40 <= recLen; ++guard)
            {
                const uint32_t type = Rd32(rec + off);
                const uint32_t len  = AttrLen(rec, off);
                if (type == kAttrEnd || len < 0x40 || off + len > recLen) break;
                if (type == kAttrData && rec[off + 8] == 1 && rec[off + 0x09] == 4)
                {
                    const uint32_t nameOff = Rd16(rec + off + 0x0A);
                    if (off + nameOff + 8 <= recLen
                        && std::memcmp(rec + off + nameOff, "$\0S\0D\0S\0", 8) == 0)
                    {
                        uint32_t r = 0x40; bool abs = false;
                        std::vector<DataRun> runs;
                        if (NonResidentRunsOffset(rec + off, len, r)
                            && DecodeDataRuns(rec + off + r, len - r, abs, runs) && !abs)
                        {
                            SdsPart p;
                            p.lowVcn  = Rd64(rec + off + 0x10);
                            p.highVcn = Rd64(rec + off + 0x18);
                            p.real    = Rd64(rec + off + 0x30) & 0x0000FFFFFFFFFFFFull;
                            for (const auto& rr : runs) p.clus += rr.len;
                            p.runs = std::move(runs);
                            parts.push_back(std::move(p));
                            hit = true;
                        }
                    }
                }
                off += len;
            }
            return hit;
        }

        // Parçaları VCN sırasında birleştirir ve akışı ham bölümden okur.
        // Bitişik olmayan ya da küme sayısı beklenenle uyuşmayan liste reddedilir:
        // yarım akış, yarım sahip tablosu demek; eksik sahip arayüzde zaten "-"
        // olarak görünüyor, yanlış ad göstermek yerine hiç göstermemek yeğlenir.
        bool ReadSdsStream(HANDLE vol, const std::vector<SdsPart>& parts, uint64_t clusBytes,
                           uint8_t* buf, uint32_t bufBytes, std::vector<uint8_t>& out)
        {
            if (!clusBytes || !buf || !bufBytes) return false;
            std::vector<SdsPart> ps = parts;
            std::sort(ps.begin(), ps.end(),
                      [](const SdsPart& a, const SdsPart& b) { return a.lowVcn < b.lowVcn; });
            uint64_t nextVcn = 0, streamLen = 0, totalClus = 0;
            std::vector<DataRun> runs;
            for (const SdsPart& p : ps)
            {
                if (p.highVcn < p.lowVcn) return false;
                const uint64_t need = p.highVcn + 1 - p.lowVcn;
                if (p.lowVcn != nextVcn || p.clus != need) return false;
                nextVcn = p.highVcn + 1;
                totalClus += need;
                if (p.real > streamLen) streamLen = p.real;
                runs.insert(runs.end(), p.runs.begin(), p.runs.end());
            }
            if (!streamLen || streamLen > totalClus * clusBytes) return false;
            out.assign((size_t)streamLen, 0);
            const uint64_t step = bufBytes / clusBytes;
            if (!step) return false;              // tek okuma bir kümeden küçük: ilerlenemez
            uint64_t done = 0;
            for (const DataRun& r : runs)
                for (uint64_t c = 0; c < r.len; c += step)
                {
                    const uint64_t take  = std::min<uint64_t>(r.len - c, step);
                    const uint32_t bytes = (uint32_t)(take * clusBytes);
                    if (!ReadAt(vol, (r.lcn + c) * clusBytes, buf, bytes)) return false;
                    const uint64_t copy = std::min<uint64_t>(bytes, streamLen - done);
                    if (copy) std::memcpy(out.data() + done, buf, (size_t)copy);
                    done += copy;
                    if (done >= streamLen) return true;
                }
            return done >= streamLen;
        }

        // SID'nin metin biçimi. ConvertSidToStringSidA'nın ayırdığı bellek
        // LocalFree ile iade edilir; ölçülen çukur başkası: GetNamedSecurityInfoW
        // ile dönen SID'i serbest bırakmak bu build'de STATUS_HEAP_CORRUPTION
        // veriyor, o yüzden motor o API'yi hiç çağırmıyor.
        bool SidToString(const uint8_t* sid, std::string& out)
        {
            out.clear();
            if (!IsValidSid((PSID)(void*)sid)) return false;
            LPSTR s = nullptr;
            if (!ConvertSidToStringSidA((PSID)(void*)sid, &s) || !s) return false;
            out = s;
            LocalFree(s);
            return !out.empty();
        }

        // SHA-256 bağlamı iş parçacığı başına bir tane yaşar. Dosya başına
        // sağlayıcı açıp 1 MB tampon tahsis etmek yüz binlerce adayda okumadan
        // daha pahalıya geliyor.
        struct ShaCtx
        {
            BCRYPT_ALG_HANDLE alg = nullptr;
            std::vector<uint8_t> hashObj, digest, buf;
            ShaCtx() = default;
            ~ShaCtx() { if (alg) BCryptCloseAlgorithmProvider(alg, 0); }
            ShaCtx(const ShaCtx&) = delete;

            bool Ready()
            {
                if (alg) return true;
                if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
                { alg = nullptr; return false; }
                ULONG objLen = 0, hashLen = 0, res = 0;
                if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &res, 0) != 0) return false;
                if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,   (PUCHAR)&hashLen, sizeof(hashLen), &res, 0) != 0) return false;
                hashObj.resize(objLen);
                digest.resize(hashLen ? hashLen : 32);
                buf.resize(1u << 20);
                return true;
            }

            // limit != ~0ull: yalnızca ilk `limit` baytı hash'lenir. Okuma
            // yarıda kesilirse (dosya silindi/kilitli) Digest yine de üretilir;
            // adaylar boyutla da ayrıldığı için bu eksik bir eşleşme üretir,
            // yanlış bir eşleşme değil.
            bool HashFile(const std::wstring& path, uint64_t limit, std::string& hashOut,
                          uint32_t* hardLinks = nullptr, const std::atomic<bool>* cancel = nullptr)
            {
                hashOut.clear();
                if (hardLinks) *hardLinks = 0;
                if (!Ready()) return false;
                HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                                       FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
                if (h == INVALID_HANDLE_VALUE) return false;
                if (hardLinks)
                {
                    BY_HANDLE_FILE_INFORMATION bi{};
                    if (GetFileInformationByHandle(h, &bi)) *hardLinks = bi.nNumberOfLinks;
                }
                BCRYPT_HASH_HANDLE hh = nullptr;
                bool ok = false;
                do
                {
                    if (BCryptCreateHash(alg, &hh, hashObj.data(), (ULONG)hashObj.size(), nullptr, 0, 0) != 0) break;
                    uint64_t doneB = 0;
                    bool aborted = false;
                    for (;;)
                    {
                        // İptal, tek bir dosyanın okuması sırasında da denetlenir:
                        // aksi halde "Durdur" düğmesi iş parçacığının bitmesini
                        // bekler ve arayüz birkaç saniye donardı.
                        if (cancel && cancel->load()) { aborted = true; break; }
                        uint32_t want = (uint32_t)buf.size();
                        if (limit != ~0ull && doneB + want > limit) want = (uint32_t)(limit - doneB);
                        DWORD got = 0;
                        if (!ReadFile(h, buf.data(), want, &got, nullptr) || got == 0) break;
                        if (BCryptHashData(hh, buf.data(), got, 0) != 0) break;
                        doneB += got;
                        if (limit != ~0ull && doneB >= limit) break;
                    }
                    if (aborted) break;
                    if (BCryptFinishHash(hh, digest.data(), (ULONG)digest.size(), 0) != 0) break;
                    ok = true;
                } while (false);
                if (hh) BCryptDestroyHash(hh);
                CloseHandle(h);
                if (ok) hashOut.assign((const char*)digest.data(), digest.size());
                return ok;
            }
        };

        struct DupCand { std::wstring path; int node = -1; uint64_t size = 0; };

        // Adayları çok iş parçacığıyla hash'ler. İş paylaşımı atomik imleçten
        // yapılır; out[i] girdi sırasına karşılık gelir ve boşsa dosya okunamıştır.
        // Tek iş parçacıklı önek hash'i sürücü çapında yüz binlerce adayda
        // saatler sürüyor ve arayüzü "takıldı" izlenimi veriyordu.
        //
        // skipHardLinks: birden çok adı olan (hard-link'li) dosya elek dışı bırakı-
        // lır. WinSxS gibi ağaçlarda yüz binlerce "kopya" böyle görünür; bir adı
        // silmek yer açmadığı için boşa giden alan değildir.
        void HashAll(std::vector<DupCand>& cands, std::vector<std::string>& out, uint64_t limit,
                     const std::atomic<bool>& cancel, std::atomic<uint64_t>& done, bool skipHardLinks)
        {
            const size_t n = cands.size();
            out.assign(n, std::string());
            if (!n) return;
            std::atomic<size_t> cursor{ 0 };
            unsigned hc = std::thread::hardware_concurrency();
            size_t nw = std::max<size_t>(4, std::min<size_t>(16, hc ? (size_t)hc : 8));
            if (nw > n) nw = n;
            auto fn = [&]
            {
                ShaCtx ctx;
                std::string h;
                for (;;)
                {
                    if (cancel.load()) return;
                    const size_t i = cursor.fetch_add(1);
                    if (i >= n) return;
                    uint32_t links = 0;
                    ctx.HashFile(cands[i].path, limit, h, skipHardLinks ? &links : nullptr, &cancel);
                    if (links > 1) h.clear();
                    out[i].swap(h);
                    done.fetch_add(1, std::memory_order_relaxed);
                }
            };
            std::vector<std::thread> ts;
            ts.reserve(nw);
            for (size_t i = 0; i < nw; ++i) ts.emplace_back(fn);
            for (auto& t : ts) t.join();
        }

        struct MftEntry { uint64_t recIdx = 0; MftFileInfo info; };
        using MftExtSizes = std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>>;

        // Bir okuma diliminin tamponunu kayıtlara ayırır: düğüm adayları
        // entries'e, uzantı kayıtlarının boyutları extSizes'e, $SDS parçaları
        // sdsParts'e yazılır. Durum taşımaz ve çağıranın tamponunu tutmaz, bu
        // yüzden hem seri taban hem her dilim işçisi AYNI fonksiyonu çağırır.
        void ParseMftChunk(const uint8_t* buf, const MftChunk& ch, uint32_t recSize,
                           uint64_t clusBytes, std::vector<MftEntry>& entries,
                           MftExtSizes& extSizes, std::vector<SdsPart>& sdsParts)
        {
            if (!recSize) return;
            const uint32_t recs = ch.bytes / recSize;     // kısmi son kayıt okunmaz
            for (uint32_t k = 0; k < recs; ++k)
            {
                const uint8_t* rec = buf + (size_t)k * recSize;
                uint32_t attrsOff = 0; uint16_t rflags = 0;
                if (!RecHeaderOk(rec, recSize, attrsOff, rflags)) continue;
                if (!(rflags & 0x0001)) continue;
                MftFileInfo fi;
                if (!ParseMftRecord(rec, recSize, fi)) continue;
                if (!fi.inUse) continue;
                if (fi.baseRef != 0)
                {
                    // Uzantı parçası: düğüm olarak ağaca asılmaz, boyutu taban
                    // kayda yazılır.
                    if (fi.baseRef == kSecureRecordIdx) CollectSdsPart(rec, recSize, sdsParts);
                    if (!fi.isDir && (fi.size || fi.alloc))
                    {
                        auto& s = extSizes[fi.baseRef];
                        s.first  = std::max(s.first,  fi.size);
                        s.second = std::max(s.second, fi.alloc);
                    }
                    continue;
                }
                if (!fi.hasName) continue;
                MftEntry e;
                // Kayıt indeksi MANTIKSAL konumdan türetilir: koşu listesi VCN
                // sırasıyla gelir ama LCN'ler ardışık olmayabilir; disk
                // konumundan türetmek ölçülen 710.929 düğümün 504.339'unu yetim
                // bırakıyordu.
                e.recIdx = (ch.logical + (uint64_t)k * recSize) / recSize;
                e.info   = std::move(fi);
                if (!e.info.isDir)
                    e.info.alloc = PlausibleAlloc(e.info.size, e.info.alloc, clusBytes);
                entries.push_back(std::move(e));
            }
        }
    }

    // =================================================================== dönüşümler

    std::string Utf16ToUtf8(const wchar_t* s, int cch)
    {
        if (cch <= 0) return {};
        int need = WideCharToMultiByte(CP_UTF8, 0, s, cch, nullptr, 0, nullptr, nullptr);
        if (need <= 0) return {};
        std::string out((size_t)need, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s, cch, out.data(), need, nullptr, nullptr);
        return out;
    }

    std::wstring Utf8ToUtf16(const char* s, size_t len)
    {
        if (len == 0) return {};
        int need = MultiByteToWideChar(CP_UTF8, 0, s, (int)len, nullptr, 0);
        if (need <= 0) return {};
        std::wstring out((size_t)need, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s, (int)len, out.data(), need);
        return out;
    }

    // Non-resident başlıkta koşu listesi ofsetinin yeri yapıya göre değişiyor:
    // klasik belge +0x10 derken bu makinede ölçülen değer +0x20 (=0x40); orada
    // +0x10..0x17 sıfır. Hangi aday geçerli bir ofset + makul koşu başlık baytı
    // veriyorsa o seçilir; hiçbiri vermezse 0x40 varsayılır.
    bool NonResidentRunsOffset(const uint8_t* attr, uint32_t attrLen, uint32_t& out)
    {
        for (uint32_t f : { 0x20u, 0x10u })
        {
            uint32_t r = Rd16(attr + f);
            if (r > 0x18 && r < attrLen && attr[r] != 0 && (attr[r] & 0x0F) != 0 &&
                ((attr[r] >> 4) & 0x0F) <= 8) { out = r; return true; }
        }
        out = 0x40;
        return 0x40 < attrLen;
    }

    // =================================================================== MFT kayıt ayrıştırma

    // Aday adım, katlarındaki imza yoklamasından geçer. YOKLAMA ORANLA yapılır:
    // $MFT'in başında hiç kullanılmamış (hepsi sıfır) kayıtlar var — ölçülen ilk
    // 32 KB'ta 24 kayıt FILE, 8 kayıt sıfır. "Hepsi" deseydik doğru adım (1024)
    // reddedilip 12288 gibi bir katman seçiliyordu.
    uint32_t MeasureRecStride(const uint8_t* buf, uint32_t span)
    {
        constexpr uint32_t kFile = 0x454C4946u;      // "FILE"
        if (!buf || span < 0x400 || Rd32(buf) != kFile) return 0;
        for (uint32_t step = 0x200; step * 8 <= span; ++step)
        {
            uint32_t checked = 0, good = 0;
            for (uint32_t p = step; p + 0x28 <= span && checked < 64; p += step)
            {
                uint16_t aOff = Rd16(buf + p + 0x14);
                ++checked;
                if (Rd32(buf + p) == kFile && aOff >= 0x28 && aOff < step) ++good;
            }
            // Az 8 yoklama ve üçte iki çoğunluk: yanlış adaylar ilk adımda elenir,
            // doğru adım sıfır kayıtlara rağmen geçer.
            if (checked >= 8 && good * 3 >= checked * 2) return step;
        }
        return 0;
    }

    // FRN ve boyut alanlarının ÜST 16 biti bu makinede çöp taşıyabiliyor
    // (ölçülen, $DATA'sız kayıtlar): MpDetours.dll $FILE_NAME gerçek
    // = 1688849860596640 = (6 << 48) | 332704, EppManifest.dll
    // = 1125899907902784 = (4 << 48) | 1060160; alt 48 bitin ikisi de Win32'in
    // bildirdiği gerçek boyuta birebir eşit. $DATA tahsisinde de aynı kirlilik
    // var: vbc.rsp = 0xA0000_00001000, yani (yuksek << 48) | 4096. Gerçek bir
    // dosya 2^48 baytı geçemez (ölçülen bölümün tamamı 999 GB), bu yüzden tüm
    // FRN/boyut okumaları alt 48 bite maskelenir.
    inline uint64_t Low48(uint64_t v) { return v & 0x0000FFFFFFFFFFFFull; }

    // Non-resident $DATA tahsis alanının (u64 +0x28) yüksek 32 biti bu makinede
    // bozuk olabiliyor: ölçülen vbc.rsp = 0xA0000_00001000 (mantıksal 1467,
    // doğru tahsis 4096), csc.rsp = 0x430000_00001000. Düşük 32 bit doğruydu.
    // Sağlam kural geometriden gelir: sıkıştırılmamış ve seyrek olmayan bir
    // dosya, mantıksal boyutunun küme katına yuvarlanmış halini kaplar — tahsis
    // real ile real+clusBytes arasındadır. Aralığın dışındaki her değer (bozuk
    // yüksek bit, dolu ama tahsisi 0 görünen kayıt) yuvarlamayla değiştirilir.
    // real'den küçük tahsis meşrudur: sıkıştırılmış (compressed) ve seyrek
    // (sparse) dosyalar daha az yer kaplar, ona dokunulmaz.
    uint64_t PlausibleAlloc(uint64_t real, uint64_t raw, uint64_t clusBytes)
    {
        const uint64_t need = RoundUpClusters(real, clusBytes);
        if (real == 0)   return raw <= clusBytes ? raw : need;
        if (raw == 0)    return need;                       // ölçülen 2395 "tahsisi0-ama-dolgu"
        if (raw < real)  return raw;                         // sıkışık / seyrek
        if (raw - real < clusBytes || raw == need) return raw;
        return need;
    }

    // $STANDARD_INFORMATION / $FILE_NAME FILETIME'i (1 Ocak 1601'den itibaren
    // 100 ns birimleri) unix saniyeye çevirir. Kayıtlarda çöp/0 zaman görülüyor
    // (ölçülen: bazı sistem dosyalarında tüm FILETIME alanları 0) ve uint32
    // alan 2106'da taşıyor; iki uç da doyurulur.
    uint32_t UnixSeconds(uint64_t ft)
    {
        constexpr uint64_t kEpoc = 116444736000000000ull;   // 1970-01-01, 100 ns
        if (ft < kEpoc) return 0;
        const uint64_t sec = (ft - kEpoc) / 10000000ull;
        return sec > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)sec;
    }

    uint16_t NodeFlags(uint32_t a, uint8_t ads, uint8_t links, bool sizeUnknown)
    {
        uint16_t f = 0;
        if (a & FILE_ATTRIBUTE_COMPRESSED)    f |= kFlagCompressed;
        if (a & FILE_ATTRIBUTE_SPARSE_FILE)   f |= kFlagSparse;
        if (a & FILE_ATTRIBUTE_ENCRYPTED)     f |= kFlagEncrypted;
        if (a & FILE_ATTRIBUTE_REPARSE_POINT) f |= kFlagReparse;
        if (a & FILE_ATTRIBUTE_OFFLINE)       f |= kFlagOffline;
        if (ads   != 0)                       f |= kFlagAds;
        if (links >  1)                       f |= kFlagHardLink;
        if (sizeUnknown)                      f |= kFlagSizeUnknown;
        return f;
    }

    bool ParseMftRecord(const uint8_t* rec, uint32_t recLen, MftFileInfo& out)
    {
        out = MftFileInfo{};
        uint32_t attrsOff = 0; uint16_t flags = 0;
        if (!RecHeaderOk(rec, recLen, attrsOff, flags)) return false;
        out.inUse = (flags & 0x0001) != 0;
        if (!out.inUse) return true;                      // silinmiş kayıt: normal, atlanır
        out.isDir = (flags & 0x0002) != 0;

        bool     fnOk = false;    uint64_t fnParent = 0;
        bool     dataOk = false;  uint64_t dataReal = 0, dataAlloc = 0;
        int      fnScore = -1;
        // $DATA'sız kayıtlarda boyut kaynağı $FILE_NAME bağları. Bağlar aynı
        // dosyanın kopyaları olduğu için boyutlarının eşit olması gerekir;
        // eşit değilse en küçük sıfır-olmayan doğru kabul edilir (ölçülen
        // cy.pak: bağlar 1178030 ve 1858030 veriyor, Win32 1178030).
        bool     fnSizeOk = false; uint64_t fnBestReal = 0, fnBestAlloc = 0;
        // $STANDARD_INFORMATION tek örnek: zaman + sahip + öznitelik maskesi.
        bool     siOk = false;
        // Bayrak kaynağı iki kopya: $SI +0x20 ve her $FILE_NAME bağının +0x38'i.
        // Hacim örnekleme ölçümü (1219 dosya) Win32 maskesine $SI'ın 1179, $FN'ın
        // 1178 uyduğunu gösterdi: $SI otoriterken 1 dosya kazanılıyor, $FN
        // otoriterken 0. Bu yüzden $SI birincil, $SI okunamazsa $FN yedek.
        // Hangi özniteliğin önce geldiği kayıt sırasına bağlı olduğu için ikisi
        // ayrı tutulup döngü sonunda seçilir.
        uint32_t siAttrs = 0, fnAttrs = 0;

        uint32_t off = attrsOff;
        while (off + 8 <= recLen)
        {
            uint32_t type = Rd32(rec + off);
            if (type == kAttrEnd) break;
            uint32_t len = AttrLen(rec, off);
            if (len < 8 || off + len > recLen) break;
            uint8_t  nonRes  = rec[off + 8];
            uint8_t  nameLen = rec[off + 0x09];
            uint16_t valOff  = Rd16(rec + off + 0x14);
            uint32_t valLen  = Rd32(rec + off + 0x10);

            if (type == kAttrFileName && !nonRes && valOff >= 0x18 &&
                off + valOff + 0x42 <= recLen)
            {
                const uint8_t* f  = rec + off + valOff;
                uint32_t       body = std::min<uint32_t>(valLen, len - valOff);
                uint8_t  nl = f[0x40];
                uint8_t  ns = f[0x41];
                if (body >= (uint32_t)(0x42 + (size_t)nl * 2) && nl > 0)
                {
                    // 1/3 = Win32 adı (ölçülen: yeni dosyalar ns=3); 2 = DOS 8.3,
                    // 0 = POSIX. Hard-link'li kayıtlarda birden çok $FILE_NAME var
                    // ve her biri farklı ebeveyne asılır: ölçülen kernel32.dll'de
                    // ns=3 kopya WinSxS ebeveynli ve boyutları 0, ns=0 kopya
                    // System32 ebeveynli ve boyutları doluydu. Bir kayıt ağaçta tek
                    // yere asıldığı için hangi bağın seçildiği görünür yolu
                    // değiştirir. Sıra: DOS 8.3 en son, sonra boyut taşıyan, sonra
                    // Win32.
                    //
                    // BOYUT NOTU: $DATA'sız (tamamı sıkıştırılmış/seyrek/parçalı,
                    // $DATA'sı uzantı kaydında) kayıtlarda $FILE_NAME boyut
                    // alanları ÇÖP TAŞIYABİLİR. Ölçülen üç vaka:
                    //   cy.pak  ns=0 gerçek=1178030  | cy.pak.DATA ns=0 gerçek=40250921670801838
                    //   MpDetours.dll ns=2 332704    | ns=0 1688849860596640
                    //   EppManifest.dll ns=2 1060160 | ns=0 1125899907902784
                    // Kirlilik üst 16 bit: 40250921670801838 = (143<<48)|1858030,
                    // 1688849860596640 = (6<<48)|332704, 1125899907902784 =
                    // (4<<48)|1060160 — alt 48 bitin ikisi Win32'i doğruluyor.
                    // Maskeden sonra kalan ayrışmayı bağlar arası en küçük
                    // sıfır-olmayan değer çözer (aşağıdaki fnBestReal).
                    ++out.links;
                    const uint64_t candAlloc = Low48(Rd64(f + 0x28));
                    const uint64_t candReal  = Low48(Rd64(f + 0x30));
                    const bool win32 = (ns == 1 || ns == 3);
                    const bool candSizes = candReal != 0 || candAlloc != 0;
                    const int  score = (ns != 2 ? 4 : 0) + (candSizes ? 2 : 0) + (win32 ? 1 : 0);
                    if (!fnSizeOk || (candReal != 0 && (fnBestReal == 0 || candReal < fnBestReal)))
                    {
                        fnBestReal = candReal;
                        fnBestAlloc = candAlloc;
                        fnSizeOk = true;
                    }
                    if (!fnOk || score > fnScore)
                    {
                        fnParent = Rd64(f + 0x00);
                        fnScore  = score;
                        // Yedek bayrak kaynağı: $SI yoksa bu bağın +0x38 maskesi
                        // kullanılır (tercih ölçümü yorumda).
                        if (body >= 0x3C) fnAttrs = Rd32(f + 0x38);
                        out.name.clear();
                        out.name.reserve(nl);
                        for (uint8_t k = 0; k < nl; ++k)                    // hizasız okuma güvenli
                            out.name.push_back((wchar_t)Rd16(f + 0x42 + (size_t)k * 2));
                        out.nameNs = ns;
                        fnOk = true;
                    }
                }
            }
            else if (!out.isDir && type == kAttrData)
            {
                if (nameLen != 0)
                {
                    // Adlı $DATA = alternate data stream. Sayı ağaçta tek bit
                    // olarak taşınır (ADS'lerin kendisi listelenmez).
                    if (out.ads < 255) ++out.ads;
                }
                else if (!dataOk && !nonRes)
                {
                    // Yerinde dosya MFT kaydının içinde durur; diskte kapladığı
                    // kendi tahsis ettiği kayıttır, değer uzunluğu değil.
                    dataReal  = valLen;
                    dataAlloc = std::max<uint64_t>(valLen, Rd32(rec + 0x1C));
                    dataOk    = true;
                }
                else if (off + 0x40 <= recLen)
                {
                    // Non-resident başlıkta tahsis +0x28, gerçek +0x30 (ölçülen).
                    dataReal  = Low48(Rd64(rec + off + 0x30));
                    dataAlloc = Low48(Rd64(rec + off + 0x28));
                    dataOk    = true;
                }
            }
            else if (type == kAttrStdInfo && !nonRes && valOff >= 0x18 &&
                     off + valOff + 0x34 <= recLen && !siOk)
            {
                // $STANDARD_INFORMATION gövdesi: +0x08 değiştirilme FILETIME,
                // +0x20 Win32 öznitelik maskesi, +0x30 sahip kimliği, +0x34
                // security id ($SDS anahtarı). Boyut TAŞIMAZ (regresyon bekçisi
                // testte). Anahtar olarak +0x34: +0x30 bu hacimde 719.699
                // kullanımda kaydın yalnızca 5'inde sıfır dışı (ölçülen).
                const uint8_t* si   = rec + off + valOff;
                uint32_t       body = std::min<uint32_t>(valLen, len - valOff);
                out.mtime = body >= 0x10 ? UnixSeconds(Rd64(si + 0x08)) : 0;
                out.owner = body >= 0x38 ? Rd32(si + 0x34) : 0;
                if (body >= 0x24) siAttrs = Rd32(si + 0x20);
                siOk = true;
            }
            off += len;
        }

        // Bayrak kaynağı tercihi: $SI okunabildiyse onun maskesi, değilse yukarıda
        // seçilen $FILE_NAME bağının maskesi kullanılır.
        out.attrs = siOk ? siAttrs : fnAttrs;

        // Boyut zinciri: $DATA (adli kaydın kendi akışı) -> $FILE_NAME
        // konsensüsü -> 0. $DATA varsa ad olmasa bile boyut raporlanır; adını
        // taşımayan kayıtlar uzantı parçalarıdır ve RunMft onları taban kayda
        // yazar (bkz. baseRef).
        uint64_t baseRef = Low48(Rd64(rec + 0x20));
        out.baseRef = baseRef;
        out.dataOk  = dataOk;
        if (!out.isDir)
        {
            if (dataOk)                                   { out.size = dataReal;   out.alloc = dataAlloc;   }
            else if (fnSizeOk && (fnBestReal || fnBestAlloc))
                                                          { out.size = fnBestReal; out.alloc = fnBestAlloc; }
            else if (baseRef != 0) return true;           // uzantı parçası: gölge kalır
            // Boyut hiçbir kaynaktan gelmedi. BOŞ dosya ile "ölçülemedi" ayrı
            // şeylerdir: gerçek boş dosyada adsız resident $DATA vardır (valLen=0)
            // ve dataOk true'dur. $FILE_NAME bağlarının 0/0 göstermesi de yeni
            // oluşmuş dosyalarda ölçülen gerçek bir durum (ölçülen: alloc=real=0),
            // dolayısıyla sıfire yalnızca hiç boyut taşımayan kayıt işaretlenir.
            out.sizeUnknown = !dataOk && !fnBestReal && !fnBestAlloc;
        }
        if (!fnOk) return true;                           // adı yok: ağaçta yeri yok
        out.parent  = Low48(fnParent);                    // FRN alt 48 bit = kayıt indeksi
        out.hasName = true;
        return true;
    }

    // =================================================================== $SDS sözlüğü

    size_t BuildSdsOwners(const std::vector<uint8_t>& sds, std::vector<Owner>& owners,
                          std::unordered_map<uint32_t, uint32_t>& idToIndex)
    {
        owners.clear();
        idToIndex.clear();
        const size_t n = sds.size();
        uint32_t   prevId = 0;
        for (size_t e = 0; e + 0x1C <= n; e += 8)
        {
            // Girdi başının yapısal imzası: +0x08 kendi akış ofsetini taşır.
            if (Rd32(sds.data() + e + 0x08) != (uint32_t)e) continue;
            const uint32_t id = Rd32(sds.data() + e + 0x04);
            if (id < 0x100) continue;            // ölçülen: ilk security id 256
            if (sds[e + 0x14] != 1) continue;    // yerdeğiştirmeli SD revizyonu
            const size_t sidPos = e + 0x14 + Rd32(sds.data() + e + 0x18);
            std::string  sid;
            // IsValidSid yalnızca başa bakar; süb-otoriteler akışın dışına taşıyorsa
            // ConvertSidToStringSidA sınır dışı okur. Uzunluk burada denetlenir.
            const PSID psid = (PSID)(void*)(sds.data() + sidPos);
            if (sidPos + 8 > n || !IsValidSid(psid) || sidPos + GetLengthSid(psid) > n) continue;
            if (!SidToString(sds.data() + sidPos, sid)) continue;
            // id'ler artan sırada diziliyor (ölçülen: 3.545 girdide 0 ihlal);
            // gerileme öz-ofset denetiminin rastlantı yakaladığını gösterir.
            if (id < prevId) continue;
            prevId = id;
            Owner o;
            o.secId = id;
            o.sid   = std::move(sid);
            if (!idToIndex.emplace(id, (uint32_t)owners.size() + 1u).second) continue;
            owners.push_back(std::move(o));
        }
        return owners.size();
    }

    // =================================================================== data run

    bool DecodeDataRuns(const uint8_t* p, uint32_t len, bool& absolute, std::vector<DataRun>& out)
    {
        absolute = false;
        uint32_t i = 0;
        int64_t cur = 0;
        while (i < len)
        {
            uint8_t hdr = p[i];
            if (hdr == 0) return !out.empty();        // bitiş
            uint8_t lenB = hdr & 0x0F, offB = (uint8_t)((hdr >> 4) & 0x0F);
            if (lenB == 0 || i + 1u + lenB + offB > len) return false;
            uint64_t rl = 0;
            for (uint8_t k = 0; k < lenB; ++k) rl |= (uint64_t)p[i + 1 + k] << (8 * k);
            i += 1u + lenB;
            if (offB)
            {
                int64_t ro = 0;
                for (uint8_t k = 0; k < offB; ++k) ro |= (int64_t)p[i + k] << (8 * k);
                if (ro & (int64_t(1) << (8 * (uint32_t)offB - 1))) ro -= (int64_t(1) << (8 * (uint32_t)offB));
                i += offB;
                cur += ro;
                out.push_back({ (uint64_t)cur, rl });
            }
            else
            {
                // uzunluk + 0 konum: sparse/compression işareti; mutlak konum
                // akışı bozulur, çağıran desteklemez.
                absolute = true;
                return false;
            }
        }
        return false;                                  // bitiş baytı görülemedi
    }

    // =================================================================== MFT dilim planı

    bool PlanMftChunks(const std::vector<DataRun>& runs, uint64_t clusBytes,
                       uint32_t clusPerChunk, std::vector<MftChunk>& out)
    {
        out.clear();
        if (!clusBytes || !clusPerChunk) return false;
        const uint64_t chunkBytes = clusPerChunk * clusBytes;
        MftChunk cur;
        for (const DataRun& r : runs)
        {
            for (uint64_t c = 0; c < r.len; )
            {
                const uint64_t freeB = chunkBytes - cur.bytes;
                const uint64_t haveB = (r.len - c) * clusBytes;
                const uint64_t takeB = freeB < haveB ? freeB : haveB;
                cur.pieces.push_back({ (r.lcn + c) * clusBytes, cur.bytes, (uint32_t)takeB });
                cur.bytes = (uint32_t)(cur.bytes + takeB);   // chunkBytes küçüktür, taşmaz
                c         += takeB / clusBytes;
                if (cur.bytes == chunkBytes)
                {
                    const uint64_t next = cur.logical + chunkBytes;
                    out.push_back(std::move(cur));
                    cur = MftChunk{};
                    cur.logical = next;
                }
            }
        }
        if (cur.bytes) out.push_back(std::move(cur));
        return !out.empty();
    }

    // =================================================================== treemap

    void LayoutTreemap(const std::vector<uint64_t>& sizes, double w, double h, std::vector<Tile>& out)
    {
        out.clear();
        struct Item { uint64_t sz; size_t id; };
        std::vector<Item> items;
        items.reserve(sizes.size());
        uint64_t total = 0;
        for (size_t i = 0; i < sizes.size(); ++i)
            if (sizes[i] > 0) { items.push_back({ sizes[i], i }); total += sizes[i]; }
        if (total == 0 || items.empty() || w <= 0.0 || h <= 0.0) return;
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.sz > b.sz; });

        const double scale = (double)w * (double)h / (double)total;
        // out girdi indeksine göre adreslenir (boyutı 0 olan girdiler {0,0,0,0}
        // kalır); çağıranan kutuyu içeriğin sırasıyla eşleştirebilmesi için şart.
        out.assign(sizes.size(), Tile{});

        size_t i = 0;
        double x = 0.0, y = 0.0, rw = w, rh = h;       // kalan dikdörtgen
        while (i < items.size() && rw > 0.0 && rh > 0.0)
        {
            const bool horiz = rw >= rh;               // şerit kısa kenara dizilir
            const double side = horiz ? rh : rw;
            std::vector<size_t> row;                   // items indeksleri
            double rowSum = 0.0;
            auto worst = [&](const std::vector<size_t>& r, double s) -> double
            {
                double s2 = s * s * scale * scale;
                double mx = 0.0, mn = 1e300;
                for (size_t k = 0; k < r.size(); ++k)
                {
                    double a = (double)items[r[k]].sz * scale;
                    mx = std::max(mx, a); mn = std::min(mn, a);
                }
                if (r.empty() || mn <= 0.0 || s2 <= 0.0) return 1e300;
                const double side2 = side * side;
                return std::max(side2 * mx / s2, s2 / (side2 * mn));
            };
            while (i < items.size())
            {
                double s = rowSum + (double)items[i].sz;
                if (row.empty()) { row.push_back(i); rowSum = s; ++i; continue; }
                row.push_back(i);
                double wNew = worst(row, s);
                row.pop_back();
                double wOld = worst(row, rowSum);
                if (wNew <= wOld) { row.push_back(i); rowSum = s; ++i; }
                else break;                            // i yerinde kalır
            }
            double rowThick = side > 0.0 ? rowSum * scale / side : 0.0;
            if (rowThick <= 0.0) rowThick = horiz ? rw : rh;
            // kayan nokta sürüklemesi şeridi kalan dikdörtgeni taşıracak kadar
            // büyütmesin; son şerit tam olarak arta kalanı kaplar.
            rowThick = std::min(rowThick, horiz ? rw : rh);
            if (i >= items.size()) rowThick = horiz ? rw : rh;
            double pos = horiz ? y : x;
            const double posEnd = pos + side;
            for (size_t k = 0; k < row.size(); ++k)
            {
                double cellLen = rowThick > 0.0 ? (double)items[row[k]].sz * scale / rowThick : 0.0;
                cellLen = std::min(cellLen, posEnd - pos);   // taşırmayı kes
                Tile t{};
                if (horiz) { t.x = x;   t.w = rowThick; t.y = pos; t.h = cellLen; }
                else       { t.x = pos; t.w = cellLen;  t.y = y;   t.h = rowThick; }
                pos += cellLen;
                out[items[row[k]].id] = t;
            }
            if (horiz) { x += rowThick; rw -= rowThick; }
            else       { y += rowThick; rh -= rowThick; }
        }
    }

    // =================================================================== kaynak listesi

    std::vector<Source> ListSources()
    {
        std::vector<Source> v;
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; ++i)
        {
            if (!(mask & (1u << i))) continue;
            wchar_t root[4] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
            UINT t = GetDriveTypeW(root);
            if (t == DRIVE_NO_ROOT_DIR || t == DRIVE_UNKNOWN) continue;
            wchar_t label[256] = L"", fs[64] = L"";
            GetVolumeInformationW(root, label, 256, nullptr, nullptr, nullptr, fs, 64);
            Source s;
            s.path   = root;                            // ağ sürücüsü de harfle gezilebilir
            s.volume = (t != DRIVE_REMOTE);
            s.ntfs   = _wcsicmp(fs, L"NTFS") == 0;
            wchar_t disp[8];
            swprintf(disp, 8, L"%c:", (wchar_t)(L'A' + i));
            std::wstring lbl = label[0] ? label : L"";
            if (t == DRIVE_REMOTE) lbl = lbl.empty() ? std::wstring(L"(a\u011F)") : lbl;
            else if (t == DRIVE_REMOVABLE) lbl = lbl.empty() ? std::wstring(L"(USB)") : lbl;
            s.label = std::wstring(disp) + (lbl.empty() ? L"" : L" " + lbl);
            v.push_back(s);
        }
        return v;
    }

    // =================================================================== Scanner çekirdeği

    Scanner::~Scanner() { Join(); }

    void Scanner::Lock() const   { mtx_.lock(); }
    void Scanner::Unlock() const { mtx_.unlock(); }

    std::wstring Scanner::RootPathLocked() const { return rootPath_; }

    std::string Scanner::NodeNameLocked(int n) const
    {
        if (n < 0 || n >= (int)nodes_.size()) return {};
        const Node& nd = nodes_[n];
        if ((size_t)nd.nameOff + nd.nameLen > pool_.size()) return {};
        return std::string(pool_.data() + nd.nameOff, nd.nameLen);
    }

    bool Scanner::BuildPathLocked(int node, std::wstring& out) const
    {
        if (node < 0 || node >= (int)nodes_.size()) return false;
        std::vector<std::string> parts;
        int i = node;
        while (i >= 0 && i < (int)nodes_.size())
        {
            const Node& nd = nodes_[i];
            if (nd.parent < 0) break;                   // kök düğümü: ad kök yolda var
            parts.push_back(std::string(pool_.data() + nd.nameOff, nd.nameLen));
            i = nd.parent;
            if (parts.size() > 512) return false;       // bozuk zincir koruması
        }
        std::wstring acc = rootPath_;
        if (!acc.empty() && acc.back() != L'\\') acc += L'\\';
        for (auto it = parts.rbegin(); it != parts.rend(); ++it)
        {
            if (it != parts.rbegin()) acc += L'\\';
            acc += Utf8ToUtf16(it->data(), it->size());
        }
        out = std::move(acc);
        return true;
    }

    bool Scanner::BuildPath(int node, std::wstring& out) const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return BuildPathLocked(node, out);
    }

    void Scanner::GetProgress(Progress& p) const
    {
        p.state      = state_.load();
        p.engine     = engine_.load();
        p.items      = items_.load();
        p.allocBytes = allocBytes_.load();
        p.bytesRead  = mftRead_.load();
        p.bytesTotal = mftTotal_.load();
        if (p.engine == Engine::Walk)
        {
            std::lock_guard<std::mutex> lk(mtx_);
            p.current = currentDir_;
        }
    }

    int Scanner::AddNode(int parent, const std::string& name, bool dir,
                         uint64_t size, uint64_t alloc,
                         uint32_t mtime, uint32_t owner, uint16_t flags)
    {
        Node nd;
        nd.parent  = parent;
        nd.size    = size;
        nd.alloc   = alloc;
        nd.dir     = dir ? 1 : 0;
        nd.mtime   = mtime;
        nd.owner   = owner;
        nd.flags   = flags;
        nd.nameOff = (uint32_t)pool_.size();
        nd.nameLen = (uint32_t)name.size();
        pool_.insert(pool_.end(), name.begin(), name.end());
        int idx = (int)nodes_.size();
        nodes_.push_back(nd);
        if (parent >= 0 && parent < idx)
        {
            nodes_[idx].nextSib          = nodes_[parent].firstChild;
            nodes_[parent].firstChild    = idx;
        }
        if (!dir)
        {
            InternExtLocked(idx);
            items_.fetch_add(1, std::memory_order_relaxed);
        }
        // canlı yukarı toplama: ebeveyn zinciri (kök -1 ile biter)
        uint64_t addSize = dir ? 0 : size;
        for (int p = parent; p >= 0 && p < (int)nodes_.size(); )
        {
            nodes_[p].alloc += alloc;
            nodes_[p].size  += addSize;
            p = nodes_[p].parent;
        }
        if (alloc) allocBytes_.fetch_add(alloc, std::memory_order_relaxed);
        return idx;
    }

    void Scanner::InternExtLocked(int nodeIdx)
    {
        Node& nd = nodes_[nodeIdx];
        std::string ext = ExtOf(std::string(pool_.data() + nd.nameOff, nd.nameLen));
        if (ext.empty()) return;
        for (size_t i = 0; i < exts_.size(); ++i)
        {
            if (exts_[i].ext == ext)
            {
                nd.ext = (int32_t)i;
                exts_[i].alloc += nd.alloc;
                exts_[i].files += 1;
                return;
            }
        }
        ExtStat st; st.ext = ext; st.alloc = nd.alloc; st.files = 1;
        exts_.push_back(std::move(st));
        nd.ext = (int32_t)exts_.size() - 1;
    }

    void Scanner::Finish(State s)
    {
        busy_.store(false);
        state_.store(s);
    }

    bool Scanner::Start(const std::wstring& target)
    {
        if (Busy() || target.empty()) return false;
        // Eski yinelenen taraması önce durdurulur ve bekleme, havuz temizliğinden
        // ÖNCE yapılır: worker_.join() eski RunDup'un ağaca bakmasını engeller.
        // Tersi sırada join, az önce temizlenmiş düğüm havuzu üzerinde çalışan eski
        // işçinin yeni ağaca yazıp yarım sonuç yayımlamasına yol açardı.
        dupCancel_.store(true);
        if (dupWorker_.joinable()) dupWorker_.join();
        cancel_.store(false);
        dupCancel_.store(false);
        items_.store(0); allocBytes_.store(0);
        mftRead_.store(0); mftTotal_.store(0);
        unknownSize_.store(0);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            nodes_.clear(); pool_.clear(); exts_.clear(); dups_.clear();
            // Eski taramanın sahip indeksleri yenisiyle anlamsız: kuyruk da
            // birlikte boşaltılır ve nesil artar — yolda olan API çağrısının sonucu
            // yeni tabloya düşmez.
            owners_.clear(); ownerPending_.clear(); ++ownerGen_;
            root_ = -1; rootPath_ = target; currentDir_.clear();
        }
        dupState_.store(DupState::Idle);
        dupPhase_.store(0);
        if (worker_.joinable())    worker_.join();
        busy_.store(true);
        state_.store(State::Scanning);

        bool volume = LooksLikeVolumeRoot(target);
        bool ntfs = false;
        if (volume)
        {
            wchar_t fs[64] = L"";
            GetVolumeInformationW(target.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fs, 64);
            ntfs = _wcsicmp(fs, L"NTFS") == 0;
        }
        engine_.store(volume && ntfs ? Engine::Mft : Engine::Walk);
        worker_ = std::thread([this, target, volume] {
            std::wstring volRoot = volume ? VolumeOf(target) : std::wstring();
            if (volume) RunMft(target, volRoot);
            else        RunWalk(target);
        });
        return true;
    }

    void Scanner::Cancel()
    {
        cancel_.store(true);
        dupCancel_.store(true);
        { std::lock_guard<std::mutex> ql(qMtx_); }   // wait öncesi/sonrası yarışını kapatır
        qCv_.notify_all();
        Join();
        State s = state_.load();
        if (s == State::Scanning) state_.store(State::Cancelled);
        if (dupState_.load() == DupState::Running) dupState_.store(DupState::Cancelled);
    }

    void Scanner::Join()
    {
        if (worker_.joinable())    worker_.join();
        for (auto& t : walkWorkers_) if (t.joinable()) t.join();
        walkWorkers_.clear();
        if (dupWorker_.joinable()) dupWorker_.join();
        StopOwnerWorker();
    }

    // Sahip adları arka planda çözülür: LookupAccountSid etki alanı sorusu
    // gönderebilir, çağrı saniyelerce sürebilir. Bu yüzden API ağaç kilidi mtx_
    // TUTULMADAN çağrılır; sonuç yazılırken kilit kısa süreyle yeniden alınır.
    // mtx_ tutulurken çağrılır (ownerStop_ ve tablo aynı kilidi paylaşır).
    void Scanner::EnsureOwnerWorker()
    {
        if (ownerWorker_.joinable()) return;
        ownerStop_ = false;
        ownerWorker_ = std::thread(&Scanner::RunOwnerWorker, this);
    }

    void Scanner::StopOwnerWorker()
    {
        { std::lock_guard<std::mutex> lk(mtx_); ownerStop_ = true; }
        ownerCv_.notify_all();
        if (ownerWorker_.joinable()) ownerWorker_.join();
    }

    void Scanner::RunOwnerWorker()
    {
        for (;;)
        {
            uint32_t   idx = 0;
            uint64_t   gen = 0;
            std::string sid;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                ownerCv_.wait(lk, [&] { return ownerStop_ || !ownerPending_.empty(); });
                if (ownerStop_) return;
                idx = ownerPending_.back();
                ownerPending_.pop_back();
                if (idx >= owners_.size()) continue;
                gen = ownerGen_;
                sid = owners_[idx].sid;
            }
            std::string name;
            PSID psid = nullptr;
            if (ConvertStringSidToSidA(sid.c_str(), &psid) && psid)
            {
                // Boyutlar NULL tamponla sorulur: ölçülen, tek seferde dolgu
                // paylı tamponla çağırmak ERROR_MORE_DATA (122) döndürebiliyor.
                DWORD cn = 0, cd = 0; SID_NAME_USE use{};
                if (!LookupAccountSidA(nullptr, psid, nullptr, &cn, nullptr, &cd, &use)
                    && GetLastError() == ERROR_INSUFFICIENT_BUFFER && cn && cd)
                {
                    std::vector<char> nm(cn + 4, 0), dom(cd + 4, 0);
                    DWORD n2 = cn + 4, d2 = cd + 4;
                    if (LookupAccountSidA(nullptr, psid, nm.data(), &n2, dom.data(), &d2, &use)
                        && n2 && d2)
                    {
                        name  = dom.data();
                        name += '\\';
                        name += nm.data();
                    }
                }
                LocalFree(psid);
            }
            std::lock_guard<std::mutex> lk(mtx_);
            if (gen == ownerGen_ && idx < owners_.size())
            {
                owners_[idx].name     = std::move(name);
                owners_[idx].attempted = true;
            }
        }
    }

    OwnerState Scanner::OwnerOfLocked(int node, std::string& label)
    {
        label.clear();
        if (node < 0 || node >= (int)nodes_.size()) return kOwnerNone;
        const uint32_t idx = nodes_[node].owner;
        if (!idx || idx > owners_.size()) return kOwnerNone;
        Owner& o = owners_[idx - 1];
        if (!o.name.empty()) { label = o.name; return kOwnerResolved; }
        if (!o.queued)
        {
            o.queued = true;
            ownerPending_.push_back(idx - 1);
            // İşçi Join()/Cancel() ile kapanmış olabilir (ölçülen: kapanmış işçiye
            // yazılan kuyruk sonsuza dek bekleyen kalır). Çağrı yeri zaten mtx_
            // tutuyor, bu yüzden yeniden başlatma da o kilitle korunuyor.
            EnsureOwnerWorker();
            ownerCv_.notify_all();
        }
        label = o.sid;
        return o.attempted ? kOwnerFailed : kOwnerPending;
    }

    // ---------------------------------------------------------------- MFT motoru

    bool Scanner::MftVolumeAvailable(const std::wstring& volumeRoot, HANDLE& outHandle)
    {
        std::wstring name = L"\\\\.\\" + volumeRoot.substr(0, 2);
        // Erişim maskesi 0 olan tek "sorgu" açılışı verir: CreateFile başarılı
        // olur ama her ReadFile ERROR_ACCESS_DENIED döner (ölçülen err=5).
        // Ham bölümden $MFT okumak GENERIC_READ ister; yönetici yetkisi gerekir.
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;     // yetki yok / harfli sürücü değil
        outHandle = h;
        return true;
    }

    void Scanner::RunMft(const std::wstring& target, const std::wstring& volumeRoot)
    {
        HANDLE vol = INVALID_HANDLE_VALUE;
        if (!MftVolumeAvailable(volumeRoot, vol)) { RunWalk(target); return; }
        BootInfo boot = ReadBoot(vol);
        if (!boot.ok) { CloseHandle(vol); RunWalk(target); return; }
        const uint64_t clusBytes = (uint64_t)boot.bytesPerSec * boot.secPerClus;

        uint32_t clusPerChunk = (uint32_t)((4ull << 20) / clusBytes);
        if (clusPerChunk == 0) clusPerChunk = 1;
        const uint32_t chunkBytes = (uint32_t)(clusPerChunk * clusBytes);
        uint8_t* buf = (uint8_t*)VirtualAlloc(nullptr, chunkBytes,
                                              MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!buf) { CloseHandle(vol); Finish(State::Failed); return; }

        std::vector<DataRun> runs;
        {
            if (!ReadAt(vol, boot.mftClusOff * clusBytes, buf, (uint32_t)clusBytes) ||
                !MftRunsFromRecord0(buf, boot.recSize, runs))
            {
                VirtualFree(buf, 0, MEM_RELEASE);
                CloseHandle(vol);
                RunWalk(target);
                return;
            }
        }
        uint64_t totalBytes = 0;
        for (const auto& r : runs) totalBytes += r.len * clusBytes;
        mftTotal_.store(totalBytes);

        std::vector<MftEntry> entries;
        entries.reserve(256 * 1024);

        // Uzantı kayıtları (baseRef != 0) kendi başına bir dosya değildir: aynı
        // dosyanın ikinci/üçüncü öznitelik parçalarıdır. Boyutları taban kayıt
        // indeksine yığılır ve yalnızca tabanın kendi boyutu yoksa kullanılır
        // (ölçülen: WinSxS manifestoları, ReShade .asm/.cso'ları — taban kayıtta
        // $DATA hiç yok, akış uzantıda).
        MftExtSizes extSizes;                                 // baseRef -> (real, alloc)

        // $SDS parçaları aynı geçişte toplanır: uzantı kayıtları zaten okunuyor,
        // ek maliyet bir başlık yürüyüşü kadar (ölçülen tüm geçiş ~0,8 s).
        std::vector<SdsPart> sdsParts;
        std::vector<Owner>                ownerTab;
        std::unordered_map<uint32_t, uint32_t> secIdx;
        bool                          ownersReady = false;

        // Düğümdeki sahip alanı tablonun 1-bazlı indeksidir. Tablo akış okunup
        // kurulana kadar (geçişin SONU) her düğüm 0 taşır: arayüz "-" gösterir,
        // canlı yayınlarda uydurma sahip yok.
        auto ownerIdx = [&](uint32_t secId) -> uint32_t {
            if (!ownersReady) return 0;
            auto it = secIdx.find(secId);
            return it == secIdx.end() ? 0 : it->second;
        };

        // Canlı yayın: her ~400K kayıtta ağaç yeniden kurulur (link + toplam).
        auto publish = [&] {
            if (entries.empty()) return;
            for (size_t i = 0; i < entries.size(); ++i)
            {
                MftFileInfo& inf = entries[i].info;
                if (inf.isDir || inf.size || inf.alloc) continue;
                auto x = extSizes.find(entries[i].recIdx);
                if (x == extSizes.end()) continue;
                inf.size        = x->second.first;
                inf.alloc       = PlausibleAlloc(inf.size, x->second.second, clusBytes);
                inf.sizeUnknown = false;    // boyut uzantı kaydından geldi
            }
            // parentRec -> giriş indeksi çocuk haritası (BFS için)
            std::unordered_map<uint64_t, std::vector<size_t>> kids;
            kids.reserve(entries.size());
            size_t rootPos = (size_t)-1;
            for (size_t i = 0; i < entries.size(); ++i)
            {
                if (entries[i].recIdx == kRootRecordIdx && entries[i].info.isDir && rootPos == (size_t)-1)
                    rootPos = i;
                else
                    kids[entries[i].info.parent].push_back(i);
            }
            if (rootPos == (size_t)-1) return;

            std::lock_guard<std::mutex> lk(mtx_);
            nodes_.clear(); pool_.clear(); exts_.clear();
            root_ = -1;
            items_.store(0); allocBytes_.store(0);

            wchar_t drv = (target.size() >= 1) ? (wchar_t)toupper((unsigned char)target[0]) : L'C';
            std::string rootName(1, (char)drv); rootName += ":";
            std::wstring rp = target;
            if (!rp.empty() && rp.back() != L'\\') rp += L'\\';
            rootPath_ = rp;
            const MftFileInfo& ri = entries[rootPos].info;
            root_ = AddNode(-1, rootName, true, 0, 0, ri.mtime, ownerIdx(ri.owner),
                            NodeFlags(ri.attrs, ri.ads, ri.links, false));

            // Boyutu hiçbir kaynaktan okunamayan dosya sayısı: ağaç her
            // yayında baştan kurulduğu için sayım da her yayında sıfırlanır.
            uint64_t unknown = 0;
            auto attach = [&](size_t i, int parentIdx, bool orphan) -> int {
                const MftEntry& e = entries[i];
                std::string name = Utf16ToUtf8(e.info.name.data(), (int)e.info.name.size());
                bool prot = false;
                if (parentIdx == root_ && e.info.isDir)
                    prot = IsProtectedTopName(ToLowerAscii(name));
                else if (parentIdx >= 0 && parentIdx < (int)nodes_.size())
                    prot = nodes_[parentIdx].protectedDir != 0;
                int idx = AddNode(parentIdx, name, e.info.isDir, e.info.size, e.info.alloc,
                                  e.info.mtime, ownerIdx(e.info.owner),
                                  NodeFlags(e.info.attrs, e.info.ads, e.info.links,
                                            e.info.sizeUnknown));
                if (prot)   nodes_[idx].protectedDir = 1;
                if (orphan) nodes_[idx].orphan = 1;
                if (!e.info.isDir && (nodes_[idx].flags & kFlagSizeUnknown)) ++unknown;
                return idx;
            };

            std::vector<int> nodeOf(entries.size(), -1);
            nodeOf[rootPos] = root_;
            std::vector<size_t> stack{ rootPos };
            while (!stack.empty())
            {
                size_t cur = stack.back(); stack.pop_back();
                auto it = kids.find(entries[cur].recIdx);
                if (it == kids.end()) continue;
                for (size_t c : it->second)
                {
                    if (nodeOf[c] >= 0 || c == rootPos) continue;
                    nodeOf[c] = attach(c, nodeOf[cur], false);
                    stack.push_back(c);
                }
            }
            // ebeveyn kaydı hiç gelmeyen girişler: kayıp veri olmasın diye köke asılır.
            for (size_t i = 0; i < entries.size(); ++i)
                if (nodeOf[i] < 0 && i != rootPos) nodeOf[i] = attach(i, root_, true);
            unknownSize_.store(unknown);
        };

        // $MFT mantıksal dilimlere bölünür. Seri taban ve dilim işçileri AYNI
        // dilim planını ve AYNI ParseMftChunk'ı kullanır: ölçüm adildi, tek iş
        // parçacıklı overlapped prefetch adayı da bu yüzden aynı planla yarıştı
        // ve kaybetti (yol silindi).
        std::vector<MftChunk> chunks;
        if (!PlanMftChunks(runs, clusBytes, clusPerChunk, chunks))
        {
            VirtualFree(buf, 0, MEM_RELEASE);
            CloseHandle(vol);
            RunWalk(target);
            return;
        }

        auto readPieces = [&](HANDLE h, const MftChunk& ch, uint8_t* b)
        {
            for (const MftPiece& p : ch.pieces)
                if (!ReadAt(h, p.phys, b + p.bufOff, p.bytes)) return false;
            return true;
        };

        int threads = mftThreads_;
        if (threads < 1) threads = 1;
        else if (threads > 8) threads = 8;

        // ---- dilim şeması: dilimler iş parçacıkları arasında bölüşülür. Her işçi
        // kendi kolundan ve kendi tamponundan okur; sonuçları kısa bir kilit
        // altında ortak biriktiriciye akıtır. Ağaç kurulumu (publish) hep ana iş
        // parçacığında kalır, böylece yayın sırası tek owned olur.
        auto runSlices = [&](size_t want) -> int
        {
            const size_t n   = chunks.size();
            const size_t w   = std::min(want, n);
            const size_t per = (n + w - 1) / w;
            if (w < 2 || per == 0) return -1;

            std::mutex             smx;
            std::vector<MftEntry>  se;
            MftExtSizes            sx;
            std::vector<SdsPart>   sp;
            std::atomic<bool>      anyFail{ false };

            auto spill = [&](std::vector<MftEntry>& le, MftExtSizes& lx, std::vector<SdsPart>& ls)
            {
                if (le.empty() && lx.empty() && ls.empty()) return;
                std::lock_guard<std::mutex> lk(smx);
                se.insert(se.end(), std::make_move_iterator(le.begin()), std::make_move_iterator(le.end()));
                le.clear();
                for (auto& kv : lx)
                {
                    auto& d = sx[kv.first];
                    d.first  = std::max(d.first,  kv.second.first);
                    d.second = std::max(d.second, kv.second.second);
                }
                lx.clear();
                sp.insert(sp.end(), std::make_move_iterator(ls.begin()), std::make_move_iterator(ls.end()));
                ls.clear();
            };
            auto drain = [&]
            {
                std::lock_guard<std::mutex> lk(smx);
                entries.insert(entries.end(), std::make_move_iterator(se.begin()), std::make_move_iterator(se.end()));
                se.clear();
                for (auto& kv : sx)
                {
                    auto& d = extSizes[kv.first];
                    d.first  = std::max(d.first,  kv.second.first);
                    d.second = std::max(d.second, kv.second.second);
                }
                sx.clear();
                sdsParts.insert(sdsParts.end(), std::make_move_iterator(sp.begin()), std::make_move_iterator(sp.end()));
                sp.clear();
            };
            auto worker = [&](size_t beg, size_t end)
            {
                HANDLE h = INVALID_HANDLE_VALUE;
                if (!MftVolumeAvailable(volumeRoot, h)) { anyFail.store(true); return; }
                uint8_t* b = (uint8_t*)VirtualAlloc(nullptr, chunkBytes,
                                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (!b) { CloseHandle(h); anyFail.store(true); return; }
                std::vector<MftEntry> le; MftExtSizes lx; std::vector<SdsPart> ls;
                le.reserve(64 * 1024);
                for (size_t i = beg; i < end; ++i)
                {
                    if (cancel_.load()) break;
                    if (!readPieces(h, chunks[i], b)) { anyFail.store(true); break; }
                    mftRead_.fetch_add(chunks[i].bytes, std::memory_order_relaxed);
                    ParseMftChunk(b, chunks[i], boot.recSize, clusBytes, le, lx, ls);
                    if (le.size() >= 60000) spill(le, lx, ls);
                }
                spill(le, lx, ls);
                VirtualFree(b, 0, MEM_RELEASE);
                CloseHandle(h);
            };

            std::vector<std::thread> ts;
            ts.reserve(w);
            for (size_t k = 1; k < w; ++k)
            {
                const size_t beg = k * per, end = std::min(n, beg + per);
                if (beg < end) ts.emplace_back(worker, beg, end);
            }
            // 0. aralık ana iş parçacığında yürür; arada akıtılanlar boşaltılır
            // ki canlı yayın tek iş parçacığı tabanındaki gibi aksın.
            const size_t end0 = std::min(n, per);
            for (size_t i = 0; i < end0; ++i)
            {
                if (cancel_.load()) break;
                if (!readPieces(vol, chunks[i], buf)) { anyFail.store(true); break; }
                mftRead_.fetch_add(chunks[i].bytes, std::memory_order_relaxed);
                ParseMftChunk(buf, chunks[i], boot.recSize, clusBytes, entries, extSizes, sdsParts);
                if ((i & 7) == 7) drain();
                if (entries.size() >= 400000) { publish(); entries.reserve(400000); }
            }
            for (auto& t : ts) t.join();
            drain();
            if (!entries.empty()) publish();
            return anyFail.load() ? 0 : 1;
        };

        bool done = false;
        if (threads > 1)
        {
            if (runSlices((size_t)threads) >= 0) done = true;
        }
        if (!done)
        {
            // tek iş parçacığı tabanı: seri dilim döngüsü (dilim şeması
            // kurulamazsa — yetki/tampon — buraya düşer).
            for (const MftChunk& ch : chunks)
            {
                if (cancel_.load()) break;
                if (!readPieces(vol, ch, buf)) break;
                mftRead_.fetch_add(ch.bytes, std::memory_order_relaxed);
                ParseMftChunk(buf, ch, boot.recSize, clusBytes, entries, extSizes, sdsParts);
                if (entries.size() >= 400000) { publish(); entries.reserve(400000); }
            }
        }
        // $SDS akışı bölüm kolu kapanmadan okunur: parçalar tek geçişte
        // toplandı, birleştirme VCN sırasında. Tablo kurulduktan SONRA bir kez
        // daha yayınlanır ki düğümlerdeki sahip indeksleri gerçek olsun.
        {
            std::vector<uint8_t> sds;
            const uint32_t bufBytes = (uint32_t)((size_t)clusPerChunk * clusBytes);
            if (!sdsParts.empty()
                && ReadSdsStream(vol, sdsParts, clusBytes, buf, bufBytes, sds))
            {
                ownersReady = BuildSdsOwners(sds, ownerTab, secIdx) != 0;
            }
        }
        VirtualFree(buf, 0, MEM_RELEASE);
        CloseHandle(vol);
        if (cancel_.load()) { Finish(State::Cancelled); return; }
        publish();
        {
            std::lock_guard<std::mutex> lk(mtx_);
            owners_ = std::move(ownerTab);
            ownerPending_.clear();
            ++ownerGen_;
            if (!owners_.empty()) EnsureOwnerWorker();
        }
        // readFail yalnızca kısmi sonuç üretir; kısmi ağaç yine de geçerlidir.
        Finish(State::Done);
    }

    // ---------------------------------------------------------------- walk motoru

    void Scanner::RunWalk(const std::wstring& target)
    {
        engine_.store(Engine::Walk);
        std::wstring norm = target;
        while (norm.size() > 3 && (norm.back() == L'\\' || norm.back() == L'/')) norm.pop_back();
        if (norm.size() == 2 && norm[1] == L':') norm += L'\\';    // "C:" -> "C:\"

        uint32_t secPerClus = 0, bytesPerSec = 0;
        {
            std::wstring vol = VolumeOf(norm);
            DWORD sc = 0, bs = 0;
            if (!vol.empty() && GetDiskFreeSpaceW(vol.c_str(), &sc, &bs, nullptr, nullptr)
                && sc && bs)
            {
                secPerClus = sc; bytesPerSec = bs;
                clusterBytes_ = (uint64_t)sc * bs;
            }
            else
                clusterBytes_ = 4096;
        }

        std::wstring leaf = norm;
        size_t s = leaf.find_last_of(L"\\/");
        if (s != std::wstring::npos && s > 1) leaf = leaf.substr(s + 1);
        std::string rootName;
        if (!leaf.empty()) rootName = Utf16ToUtf8(leaf.data(), (int)leaf.size());
        else if (norm.size() >= 2 && norm[1] == L':')
            rootName = std::string(1, (char)toupper((unsigned char)norm[0])) + ":";
        else
            rootName = Utf16ToUtf8(norm.data(), (int)norm.size());

        {
            std::lock_guard<std::mutex> lk(mtx_);
            rootPath_ = norm;
            // Kök düğümün mtime/özniteliği walk'ta ayrıca sorulmaz; 0 =
            // bilinmiyor olarak kalır (MFT kökü kendi kaydından aldığı için
            // doludur).
            root_ = AddNode(-1, rootName, true, 0, 0);
            currentDir_ = norm;
        }

        struct DirJob { std::wstring path; int dirIdx; };
        // FindFirstFileW'ın döndürdüğü, düğüm kurarken gereken her şey: ad,
        // boyut, değişim zamanı ve öznitelik maskesi.
        struct WalkFile { std::string name; uint64_t size, alloc; uint32_t mtime; uint16_t flags; };
        struct WalkDir  { std::wstring path; std::string name; uint32_t mtime; uint16_t flags; };
        std::deque<DirJob> dq;
        int pending = 1;                                  // bekleyen dizin sayısı
        bool stop = false;
        dq.push_back({ norm, root_ });

        auto workerFn = [&] {
            for (;;)
            {
                DirJob job;
                {
                    std::unique_lock<std::mutex> ql(qMtx_);
                    qCv_.wait(ql, [&] { return cancel_.load() || stop || !dq.empty(); });
                    if (cancel_.load() || stop) return;
                    job = dq.front(); dq.pop_front();
                }
                { std::lock_guard<std::mutex> lk(mtx_); currentDir_ = job.path; }

                bool prot = false;
                std::wstring pattern = job.path;
                if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
                pattern += L"*";
                std::vector<WalkFile> files;
                std::vector<WalkDir>  dirs;
                { std::lock_guard<std::mutex> lk(mtx_);
                  if (job.dirIdx >= 0 && job.dirIdx < (int)nodes_.size())
                      prot = nodes_[job.dirIdx].protectedDir != 0; }

                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        if (cancel_.load()) break;
                        std::wstring nm = fd.cFileName;
                        if (nm == L"." || nm == L"..") continue;
                        bool isDir   = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                        bool reparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                        std::string nm8 = Utf16ToUtf8(nm.data(), (int)nm.size());
                        // Walk'ta ADS sayısı ve hard-link sayısı sorulmaz (dosya
                        // başına ek açma/GetFileInformationByHandle çağrısı
                        // gerektirir): o iki bayrak yalnız MFT motorunda dolar.
                        const uint16_t fl = NodeFlags(fd.dwFileAttributes, 0, 1, false);
                        const uint32_t mt = UnixSeconds(((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32)
                                                        | fd.ftLastWriteTime.dwLowDateTime);
                        if (isDir)
                        {
                            if (reparse) continue;        // junction/symlink: döngü riski, atla
                            std::wstring sub = pattern;
                            sub.pop_back();               // '*'
                            sub += nm;
                            dirs.push_back({ std::move(sub), std::move(nm8), mt, fl });
                        }
                        else if (reparse) continue;       // symlink dosya: hedefi sayma
                        else
                        {
                            uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                            files.push_back({ std::move(nm8), sz,
                                              RoundUpClusters(sz, clusterBytes_), mt, fl });
                        }
                    } while (FindNextFileW(h, &fd));
                    FindClose(h);
                }
                if (cancel_.load()) return;

                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    for (auto& f : files)
                    {
                        int idx = AddNode(job.dirIdx, f.name, false, f.size, f.alloc, f.mtime, 0, f.flags);
                        if (prot) nodes_[idx].protectedDir = 1;
                    }
                }
                {
                    std::lock_guard<std::mutex> ql(qMtx_);
                    for (auto& d : dirs)
                    {
                        bool p2 = prot ||
                            (job.dirIdx == root_ && IsProtectedTopName(ToLowerAscii(d.name)));
                        int idx;
                        {
                            std::lock_guard<std::mutex> lk(mtx_);
                            idx = AddNode(job.dirIdx, d.name, true, 0, 0, d.mtime, 0, d.flags);
                            if (p2) nodes_[idx].protectedDir = 1;
                        }
                        dq.push_back({ d.path, idx });
                        ++pending;
                    }
                    --pending;
                    if (pending == 0) stop = true;
                }
                qCv_.notify_all();
            }
        };

        unsigned hc = std::thread::hardware_concurrency();
        size_t nWorkers = std::max<size_t>(4, std::min<size_t>(16, hc ? (size_t)hc : 8));
        for (size_t i = 0; i < nWorkers; ++i) walkWorkers_.emplace_back(workerFn);
        qCv_.notify_all();
        for (auto& t : walkWorkers_) if (t.joinable()) t.join();
        walkWorkers_.clear();

        Finish(cancel_.load() ? State::Cancelled : State::Done);
    }

    // ---------------------------------------------------------------- duplicates

    bool Scanner::StartDuplicates()
    {
        if (state_.load() != State::Done) return false;
        if (dupState_.load() == DupState::Running) return false;
        dupCancel_.store(false);
        dupPhase_.store(1);
        dupDone_.store(0); dupTotal_.store(0);
        dupState_.store(DupState::Running);
        if (dupWorker_.joinable()) dupWorker_.join();
        dupWorker_ = std::thread([this] { RunDup(); });
        return true;
    }

    void Scanner::RunDup()
    {
        std::vector<DupCand> cands;
        dupTotal_.store(0); dupDone_.store(0);
        // 1. faz iki parçaya bölünür: aday listesi kilit altında çıkarılır (düz
        // tarama, hızlı), yol çözümlemesi kilitten ÇIKTIKTAN sonra küçük kümele-
        // rle yapılır. Tek kilide yayılmış bir çözümleme sürücü başına dakikalar
        // sürebilir ve o süre boyunca arayüz Lock() beklerdi.
        std::vector<std::pair<int, uint64_t>> want;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            std::unordered_map<uint64_t, std::vector<int>> bySize;
            for (int i = 0; i < (int)nodes_.size(); ++i)
            {
                const Node& nd = nodes_[i];
                if (nd.dir || nd.protectedDir || nd.size < 4096) continue;
                bySize[nd.size].push_back(i);
            }
            for (auto& kv : bySize)
                if (kv.second.size() > 1)
                    for (int idx : kv.second) want.push_back({ idx, kv.first });
        }
        if (dupCancel_.load()) { dupState_.store(DupState::Cancelled); return; }
        dupTotal_.store((uint64_t)want.size());

        for (size_t i = 0; i < want.size(); i += 4096)
        {
            if (dupCancel_.load()) { dupState_.store(DupState::Cancelled); return; }
            const size_t e = i + 4096 < want.size() ? i + 4096 : want.size();
            std::lock_guard<std::mutex> lk(mtx_);
            for (size_t k = i; k < e; ++k)
            {
                DupCand c; c.node = want[k].first; c.size = want[k].second;
                if (BuildPathLocked(c.node, c.path) && !c.path.empty())
                    cands.push_back(std::move(c));
            }
            dupDone_.store((uint64_t)e);
        }
        want.clear();
        if (dupCancel_.load()) { dupState_.store(DupState::Cancelled); return; }

        // 2. faz — örnek hash: dosyanın ilk 4 KB'ı (bir küme). Boyut çakışması
        // tek başına aday olmak için çok zayıf bir eleydi; sürücüde yüz binlerce
        // dosya aynı boyutu paylaşır ve hepsini tam okumak taramayı bitirilemez
        // kılıyordu. Tek küme okuma, eşleşmeyenleri neredeyse bedeline eler.
        dupPhase_.store(2);
        dupTotal_.store((uint64_t)cands.size()); dupDone_.store(0);
        std::vector<std::string> sample;
        HashAll(cands, sample, 4u * 1024u, dupCancel_, dupDone_, true);
        if (dupCancel_.load()) { dupState_.store(DupState::Cancelled); return; }
        std::unordered_map<std::string, std::vector<size_t>> bySample;
        for (size_t i = 0; i < cands.size(); ++i)
            if (!sample[i].empty())
                bySample[sample[i] + (char)6 + std::to_string(cands[i].size)].push_back(i);
        sample.clear(); sample.shrink_to_fit();
        std::vector<DupCand> pool2;
        for (auto& kv : bySample)
            if (kv.second.size() > 1)
                for (size_t i : kv.second) pool2.push_back(std::move(cands[i]));
        cands.clear();
        bySample.clear();

        if (pool2.empty())
        {
            std::lock_guard<std::mutex> lk(mtx_); dups_.clear();
            dupState_.store(DupState::Done);
            return;
        }

        // 3. faz — tam hash: yalnızca örnekçe hâlâ çakışanlar diskin tamamını okur.
        dupPhase_.store(3);
        dupTotal_.store((uint64_t)pool2.size()); dupDone_.store(0);
        std::vector<std::string> full;
        HashAll(pool2, full, ~0ull, dupCancel_, dupDone_, false);
        if (dupCancel_.load()) { dupState_.store(DupState::Cancelled); return; }
        std::unordered_map<std::string, std::vector<const DupCand*>> byFull;
        for (size_t i = 0; i < pool2.size(); ++i)
            if (!full[i].empty())
                byFull[full[i] + (char)6 + std::to_string(pool2[i].size)].push_back(&pool2[i]);
        std::vector<DupGroup> groups;
        for (auto& kv : byFull)
        {
            if (kv.second.size() < 2) continue;
            DupGroup grp;
            grp.size   = kv.second.front()->size;
            grp.wasted = grp.size * (kv.second.size() - 1);
            std::sort(kv.second.begin(), kv.second.end(),
                      [](const DupCand* a, const DupCand* b) { return a->path < b->path; });
            for (const DupCand* c : kv.second) grp.nodes.push_back(c->node);
            groups.push_back(std::move(grp));
        }
        std::sort(groups.begin(), groups.end(), [](const DupGroup& a, const DupGroup& b) {
            return a.wasted > b.wasted;
        });
        {
            std::lock_guard<std::mutex> lk(mtx_);
            dups_ = std::move(groups);
        }
        dupState_.store(DupState::Done);
    }

    void Scanner::CancelDuplicates()
    {
        dupCancel_.store(true);
        if (dupWorker_.joinable()) dupWorker_.join();
        if (dupState_.load() == DupState::Running) dupState_.store(DupState::Cancelled);
    }

    // ---------------------------------------------------------------- önbellek

    std::wstring Scanner::DefaultCachePath()
    {
        wchar_t base[MAX_PATH] = L"";
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base)))
        {
            std::wstring d = std::wstring(base) + L"\\TENGRI";
            CreateDirectoryW(d.c_str(), nullptr);
            return d + L"\\diskscan.tgds";
        }
        return L"diskscan.tgds";
    }

    namespace
    {
        constexpr uint32_t kCacheMagic = 0x53444754;   // "TGDS"
        // v2: düğüm başına mtime + owner + NodeFlag bayrakları eklendi.
        // v3: owner alanı $SDS sözlük indeksine dönüştü (1-bazlı, 0 = yok);
        // indekslerin anlamlı olması için sözlüğün kendisi de dosyaya yazılıyor.
        // v1/v2 dosyalar bu alanları taşımıyordu; okuma reddedilir (bkz. #11:
        // eski dosya sessizce silinir).
        constexpr uint32_t kCacheVer   = 3;

        void WrI32(std::vector<char>& v, int32_t x) { const char* p = (const char*)&x; v.insert(v.end(), p, p + 4); }
        void WrI64(std::vector<char>& v, int64_t x) { const char* p = (const char*)&x; v.insert(v.end(), p, p + 8); }
        bool RdI32(const char*& p, const char* end, int32_t& x) { if (end - p < 4) return false; std::memcpy(&x, p, 4); p += 4; return true; }
        bool RdI64(const char*& p, const char* end, int64_t& x) { if (end - p < 8) return false; std::memcpy(&x, p, 8); p += 8; return true; }
    }

    bool Scanner::PurgeIncompatibleCache(const std::wstring& path)
    {
        // Yalnız başlık okunur: ağaç yüklenmez, gövde denetlenmez.
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;          // yok / açılamaz
        int32_t head[2] = { 0, 0 };
        DWORD got = 0;
        const bool rd = ReadFile(h, head, sizeof head, &got, nullptr) && got == sizeof head;
        CloseHandle(h);
        if (!rd) return false;                                // kısa dosya: bozuk, silme nedeni değil
        if ((uint32_t)head[0] != kCacheMagic) return false;   // yabancı dosya: bize ait değil
        if ((uint32_t)head[1] == kCacheVer)  return false;    // güncel sürüm
        return DeleteFileW(path.c_str()) != FALSE;            // v1/v2: yeniden taramayla doğar
    }

    bool Scanner::SaveCache(const std::wstring& path) const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (nodes_.empty()) return false;
        std::vector<char> b;
        b.reserve(nodes_.size() * 64 + pool_.size() + 4096);
        WrI32(b, (int32_t)kCacheMagic);
        WrI32(b, (int32_t)kCacheVer);
        WrI32(b, (int32_t)engine_.load());
        WrI32(b, (int32_t)root_);
        {
            std::string rp = Utf16ToUtf8(rootPath_.data(), (int)rootPath_.size());
            WrI32(b, (int32_t)rp.size());
            b.insert(b.end(), rp.begin(), rp.end());
        }
        WrI64(b, (int64_t)nodes_.size());
        for (const Node& n : nodes_)
        {
            WrI32(b, n.parent); WrI32(b, n.firstChild); WrI32(b, n.nextSib);
            WrI64(b, (int64_t)n.size); WrI64(b, (int64_t)n.alloc);
            WrI32(b, (int32_t)n.nameOff); WrI32(b, (int32_t)n.nameLen);
            WrI32(b, n.ext);
            WrI32(b, (n.dir ? 1 : 0) | (n.protectedDir ? 2 : 0) | (n.orphan ? 4 : 0));
            WrI32(b, (int32_t)n.mtime);
            WrI32(b, (int32_t)n.owner);
            WrI32(b, n.flags);
        }
        WrI64(b, (int64_t)pool_.size());
        b.insert(b.end(), pool_.begin(), pool_.end());
        WrI32(b, (int32_t)exts_.size());
        for (const ExtStat& e : exts_)
        {
            WrI32(b, (int32_t)e.ext.size());
            b.insert(b.end(), e.ext.begin(), e.ext.end());
            WrI64(b, (int64_t)e.alloc); WrI64(b, (int64_t)e.files);
        }
        // v3: $SDS sözlüğü. Düğüm.owner bu tabloyun 1-bazlı indeksidir; tablo
        // olmadan indeksler çöp okur, bu yüzden her taramada yazılır (boşsa 0).
        // Çözülmüş ad da taşınır: SID→ad LookupAccountSidA ister ve cache'ten
        // yüklemede tekrar çözülmesi gerekmez.
        WrI32(b, (int32_t)owners_.size());
        for (const Owner& o : owners_)
        {
            WrI32(b, (int32_t)o.secId);
            WrI32(b, (int32_t)o.sid.size());
            b.insert(b.end(), o.sid.begin(), o.sid.end());
            WrI32(b, (int32_t)o.name.size());
            b.insert(b.end(), o.name.begin(), o.name.end());
            WrI32(b, o.attempted ? 1 : 0);
        }
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        bool ok = true;
        size_t off = 0;
        while (off < b.size())
        {
            DWORD wr = 0;
            uint32_t take = (uint32_t)std::min<size_t>(1u << 22, b.size() - off);
            if (!WriteFile(h, b.data() + off, take, &wr, nullptr) || wr != take) { ok = false; break; }
            off += take;
        }
        CloseHandle(h);
        return ok;
    }

    bool Scanner::LoadCache(const std::wstring& path)
    {
        if (Busy()) return false;
        // Yinelenen taraması sürerken ağaç takas edilmez: worker düğüm indekslerini
        // tutuyor, değişen ağaçta geçersiz kalırlar.
        if (dupState_.load() == DupState::Running) return false;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        std::vector<char> b;
        DWORD sz = GetFileSize(h, nullptr);
        if (sz == INVALID_FILE_SIZE || sz > 800u * 1024u * 1024u) { CloseHandle(h); return false; }
        b.resize(sz);
        DWORD got = 0;
        bool rd = ReadFile(h, b.data(), sz, &got, nullptr) && got == sz;
        CloseHandle(h);
        if (!rd || b.size() < 28) return false;

        const char* p = b.data(); const char* end = p + b.size();
        int32_t magic = 0, ver = 0, eng = 0, root = 0, rpLen = 0;
        if (!RdI32(p, end, magic) || magic != (int32_t)kCacheMagic) return false;
        if (!RdI32(p, end, ver) || ver != (int32_t)kCacheVer) return false;
        if (!RdI32(p, end, eng) || !RdI32(p, end, root) || !RdI32(p, end, rpLen)) return false;
        if (rpLen < 0 || end - p < rpLen) return false;
        std::string rpUtf8(p, (size_t)rpLen); p += rpLen;
        int64_t nodeCount = 0;
        if (!RdI64(p, end, nodeCount) || nodeCount <= 0 || nodeCount > 20000000) return false;
        std::vector<Node> nodes((size_t)nodeCount);
        for (int64_t i = 0; i < nodeCount; ++i)
        {
            Node& n = nodes[(size_t)i];
            int32_t fl = 0, no = 0, nl = 0, mt = 0, ow = 0, fg = 0;
            int64_t szv = 0, al = 0;
            if (!RdI32(p, end, n.parent) || !RdI32(p, end, n.firstChild) || !RdI32(p, end, n.nextSib)) return false;
            if (!RdI64(p, end, szv) || !RdI64(p, end, al)) return false;
            if (!RdI32(p, end, no) || !RdI32(p, end, nl)) return false;
            if (!RdI32(p, end, n.ext) || !RdI32(p, end, fl)) return false;
            if (!RdI32(p, end, mt) || !RdI32(p, end, ow) || !RdI32(p, end, fg)) return false;
            n.size = (uint64_t)szv; n.alloc = (uint64_t)al;
            n.nameOff = (uint32_t)no; n.nameLen = (uint32_t)nl;
            n.dir = (uint8_t)(fl & 1); n.protectedDir = (uint8_t)(fl >> 1 & 1); n.orphan = (uint8_t)(fl >> 2 & 1);
            n.mtime = (uint32_t)mt; n.owner = (uint32_t)ow; n.flags = (uint16_t)(fg & 0xFFFF);
        }
        int64_t poolLen = 0;
        if (!RdI64(p, end, poolLen) || poolLen < 0 || end - p < poolLen) return false;
        std::vector<char> pool(p, p + poolLen); p += poolLen;
        int32_t extCount = 0;
        if (!RdI32(p, end, extCount) || extCount < 0) return false;
        std::vector<ExtStat> exts((size_t)extCount);
        for (int32_t i = 0; i < extCount; ++i)
        {
            int32_t elen = 0; int64_t ea = 0, ef = 0;
            if (!RdI32(p, end, elen) || elen < 0 || end - p < elen) return false;
            exts[i].ext.assign(p, (size_t)elen); p += elen;
            if (!RdI64(p, end, ea) || !RdI64(p, end, ef)) return false;
            exts[i].alloc = (uint64_t)ea; exts[i].files = (uint64_t)ef;
        }
        int32_t ownerCount = 0;
        if (!RdI32(p, end, ownerCount) || ownerCount < 0 || ownerCount > 4000000) return false;
        std::vector<Owner> owners((size_t)ownerCount);
        for (int32_t i = 0; i < ownerCount; ++i)
        {
            Owner& o = owners[(size_t)i];
            int32_t secId = 0, sidLen = 0, nameLen = 0, att = 0;
            if (!RdI32(p, end, secId)) return false;
            if (!RdI32(p, end, sidLen) || sidLen < 0 || end - p < sidLen) return false;
            o.sid.assign(p, (size_t)sidLen); p += sidLen;
            if (!RdI32(p, end, nameLen) || nameLen < 0 || end - p < nameLen) return false;
            o.name.assign(p, (size_t)nameLen); p += nameLen;
            if (!RdI32(p, end, att)) return false;
            o.secId  = (uint32_t)secId;
            o.attempted = att != 0;
        }
        // sağlama: indeksler ve havuz sınırları (bozuk/hususi dosyaya karşı)
        for (const Node& n : nodes)
        {
            if (n.parent >= (int)nodes.size() || n.firstChild >= (int)nodes.size() ||
                n.nextSib >= (int)nodes.size())
                return false;
            if ((uint64_t)n.nameOff + n.nameLen > pool.size()) return false;
            if (n.ext >= (int32_t)exts.size()) return false;
            if (n.owner > owners.size()) return false;
        }
        if (root < 0 || root >= (int)nodes.size()) return false;
        uint64_t it = 0, al = 0, unk = 0;
        for (const Node& n : nodes)
            if (!n.dir) { ++it; al += (uint64_t)n.alloc; if (n.flags & kFlagSizeUnknown) ++unk; }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            nodes_ = std::move(nodes); pool_ = std::move(pool); exts_ = std::move(exts);
            owners_ = std::move(owners); ownerPending_.clear(); ++ownerGen_;
            root_ = root; rootPath_ = Utf8ToUtf16(rpUtf8.data(), rpUtf8.size());
            dups_.clear();
            if (!owners_.empty()) EnsureOwnerWorker();
        }
        engine_.store((Engine)eng);
        items_.store(it); allocBytes_.store(al);
        unknownSize_.store(unk);
        dupState_.store(DupState::Idle);
        dupPhase_.store(0);
        state_.store(State::Done);
        return true;
    }

    // ---------------------------------------------------------------- eylemler

    bool Scanner::DeletePaths(const std::vector<std::wstring>& paths, bool recycle, DeleteResult& res)
    {
        res = DeleteResult{};
        res.attempted = (int)paths.size();
        if (paths.empty()) return true;
        HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        IFileOperation* op = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&op));
        bool ok = false;
        if (SUCCEEDED(hr) && op)
        {
            FILEOP_FLAGS f = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
            if (recycle) f |= FOFX_RECYCLEONDELETE;
            op->SetOperationFlags(f);
            bool any = false;
            for (const auto& p : paths)
            {
                IShellItem* item = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(p.c_str(), nullptr, IID_PPV_ARGS(&item))))
                {
                    if (SUCCEEDED(op->DeleteItem(item, nullptr))) any = true;
                    item->Release();
                }
            }
            if (any)
            {
                HRESULT hrp = op->PerformOperations();
                ok = SUCCEEDED(hrp);
                if (!ok)
                {
                    BOOL ab = FALSE;
                    op->GetAnyOperationsAborted(&ab);
                    res.aborted = ab == TRUE;
                    wchar_t buf[160];
                    swprintf(buf, 160, L"Silme ba\u015Far\u0131s\u0131z (0x%08X)", (unsigned)hrp);
                    res.error = buf;
                }
            }
            op->Release();
        }
        else
        {
            wchar_t buf[160];
            swprintf(buf, 160, L"IFileOperation ba\u015Fat\u0131lamad\u0131 (0x%08X)", (unsigned)hr);
            res.error = buf;
        }
        if (hrInit == S_OK || hrInit == S_FALSE) CoUninitialize();
        return ok;
    }

    void Scanner::ShowInExplorer(const std::wstring& path)
    {
        std::wstring args = L"/select,\"" + path + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }

    void Scanner::OpenPath(const std::wstring& path)
    {
        ShellExecuteW(nullptr, nullptr, path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}
