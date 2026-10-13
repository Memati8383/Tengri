// Gecici dogrulama araci: diskanalyzer'in MFT motorunu gercek C: uzerinde
// calistirip motor secimini, boyut dogrulugunu ve sureyi OLCER. Salt okunur;
// hicbir dosya yaratmaz/silmez. Build: _engine_probe_build.bat
#define WIN32_LEAN_AND_MEAN
#include "../src/core/diskanalyzer.hpp"

#include <algorithm>
#include <aclapi.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sddl.h>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    FILE* g_out = nullptr;

    void Line(const char* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(g_out, fmt, ap);
        va_end(ap);
        fputc('\n', g_out);
        fflush(g_out);
    }

    uint16_t Rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
    uint32_t Rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
    uint64_t Rd64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }

    std::string Lower(std::string s)
    {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        return s;
    }

    struct Geom
    {
        HANDLE                 vol       = INVALID_HANDLE_VALUE;
        uint64_t               clusBytes = 0;
        uint64_t               mftOff    = 0;
        uint32_t               recSize   = 0;
        std::vector<disk::DataRun> runs;                    // $MFT kosu listesi
        std::vector<uint64_t>  runLogicalStart;             // her kosunun MFT icindeki bayt baslangici
    };

    // Kayit indeksi -> disk konumu. $MFT parcali olabilir: kosular VCN sirasiyla
    // gelir ama LCN'leri ardisik olmak zorunda degil. Duz mftOff + idx*adim
    // varsayimi ilk kosunun ilerisinde yanlis kayit okutur (olculen .pak.DATA
    // dokumlerinde imza bozuk geldi).
    bool IndexToPos(const Geom& g, uint64_t idx, uint64_t& out)
    {
        if (!g.recSize) return false;
        const uint64_t log = idx * g.recSize;
        for (size_t i = 0; i < g.runs.size(); ++i)
        {
            const uint64_t span = g.runs[i].len * g.clusBytes;
            if (log < g.runLogicalStart[i] + span)
            {
                out = g.runs[i].lcn * g.clusBytes + (log - g.runLogicalStart[i]);
                return true;
            }
        }
        return false;
    }

    // Scanner::ReadBoot'un adim adim kopyasi (geometry + kayit adimi olcumu).
    bool OpenGeom(Geom& g)
    {
        g.vol = CreateFileW(L"\\\\.\\C:", GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
        if (g.vol == INVALID_HANDLE_VALUE)
        { Line("geom: ham bolum acilamadi err=%lu", GetLastError()); return false; }

        uint8_t* b = (uint8_t*)VirtualAlloc(nullptr, 1u << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!b) { CloseHandle(g.vol); g.vol = INVALID_HANDLE_VALUE; return false; }
        LARGE_INTEGER zero; zero.QuadPart = 0;
        SetFilePointerEx(g.vol, zero, nullptr, FILE_BEGIN);
        DWORD got = 0;
        if (!ReadFile(g.vol, b, 512, &got, nullptr))
        { Line("geom: boot okunamadi err=%lu", GetLastError()); VirtualFree(b, 0, MEM_RELEASE); CloseHandle(g.vol); g.vol = INVALID_HANDLE_VALUE; return false; }
        g.clusBytes = (uint64_t)Rd16(b + 11) * b[13];
        g.mftOff    = Rd64(b + 0x30) * g.clusBytes;
        Line("geom: bytesPerSec=%u secPerClus=%u clusBytes=%llu mftLcn=%llu mftOff=%llu",
             Rd16(b + 11), b[13], (unsigned long long)g.clusBytes,
             (unsigned long long)Rd64(b + 0x30), (unsigned long long)g.mftOff);

        uint32_t span = (uint32_t)(g.clusBytes * 16);
        if (span > (1u << 20)) span = 1u << 20;
        LARGE_INTEGER li; li.QuadPart = (LONGLONG)g.mftOff;
        SetFilePointerEx(g.vol, li, nullptr, FILE_BEGIN);
        DWORD got2 = 0;
        if (!ReadFile(g.vol, b, span, &got2, nullptr))
        { Line("geom: mft basi okunamadi err=%lu", GetLastError()); VirtualFree(b, 0, MEM_RELEASE); CloseHandle(g.vol); g.vol = INVALID_HANDLE_VALUE; return false; }
        Line("geom: mft basi %u bayt okundu, imza=%.4s", got2, (const char*)b);
        int hit = 0, miss = 0;
        for (uint32_t p = 0; p + 0x28 <= span; p += 1024)
            if (Rd32(b + p) == 0x454C4946u) ++hit; else ++miss;
        const uint32_t stride = disk::MeasureRecStride(b, span);
        Line("geom: 1024 adimda FILE=%d baska=%d  olculen stride=%u  baslik alloc=0x%X",
             hit, miss, stride, Rd32(b + 0x1C));
        for (uint32_t k = 0; k < 4; ++k)
        {
            const uint8_t* rec = b + (size_t)k * 1024;
            Line("  kayit[%u] imza=%.4s flags=0x%X attrsOff=0x%X used=0x%X alloc=0x%X idx@0x2C=%u",
                 k, (const char*)rec, Rd16(rec + 0x16), Rd16(rec + 0x14),
                 Rd32(rec + 0x18), Rd32(rec + 0x1C), Rd32(rec + 0x2C));
        }
        VirtualFree(b, 0, MEM_RELEASE);
        g.recSize = stride;
        if (!g.recSize) return false;

        // $MFT kosu listesi: kayit 0'in adsiz non-resident $DATA'si.
        {
            uint8_t* m = (uint8_t*)VirtualAlloc(nullptr, span, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!m) return false;
            LARGE_INTEGER li; li.QuadPart = (LONGLONG)g.mftOff;
            SetFilePointerEx(g.vol, li, nullptr, FILE_BEGIN);
            DWORD gm = 0;
            const bool gotRuns = ReadFile(g.vol, m, span, &gm, nullptr) && gm == span;
            if (gotRuns)
            {
                bool abs = false;
                for (uint32_t off = Rd16(m + 0x14); off + 0x40 <= span;)
                {
                    const uint32_t type = Rd32(m + off), len = Rd32(m + off + 4);
                    if (type == 0xFFFFFFFFu || len < 0x40 || off + len > span) break;
                    if (type == 0x80 && m[off + 8] == 1 && m[off + 0x09] == 0)
                    {
                        uint32_t r = 0x40;
                        if (disk::NonResidentRunsOffset(m + off, len, r) &&
                            disk::DecodeDataRuns(m + off + r, len - r, abs, g.runs) && !abs &&
                            !g.runs.empty()) break;
                        g.runs.clear();
                    }
                    off += len;
                }
            }
            VirtualFree(m, 0, MEM_RELEASE);
        }
        uint64_t acc = 0, tot = 0;
        for (const auto& r : g.runs) { g.runLogicalStart.push_back(acc); acc += r.len * g.clusBytes; }
        for (const auto& r : g.runs) tot += r.len * g.clusBytes;
        Line("kosu: adet=%zu toplam=%llu bayt  ilk.lcn=%llu (mftLcn=%llu)  bitisik=%s",
             g.runs.size(), (unsigned long long)tot,
             g.runs.empty() ? 0ull : (unsigned long long)g.runs[0].lcn,
             (unsigned long long)(g.mftOff / (g.clusBytes ? g.clusBytes : 1)),
             g.runs.size() == 1 ? "EVET" : "HAYIR");
        for (size_t i = 0; i < g.runs.size() && i < 16; ++i)
            Line("   kosu[%zu] lcn=%llu len=%llu_kume mantiksal=%llu dogal=%llu",
                 i, (unsigned long long)g.runs[i].lcn, (unsigned long long)g.runs[i].len,
                 (unsigned long long)g.runLogicalStart[i],
                 (unsigned long long)(g.runs[i].lcn * g.clusBytes));
        return !g.runs.empty();
    }

    void Win32Sizes(const wchar_t* path, uint64_t& real, uint64_t& alloc)
    {
        real = alloc = (uint64_t)-1;
        WIN32_FILE_ATTRIBUTE_DATA d{};
        if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d)) return;
        real = ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
        DWORD hi = 0;
        DWORD lo = GetCompressedFileSizeW(path, &hi);
        if (lo == INVALID_FILE_SIZE && GetLastError() != 0) alloc = real;
        else alloc = ((uint64_t)hi << 32) | lo;
    }

    // Bir kaydun tüm öznitelik zincirini dok (tip, boyut, nonRes, id, $DATA alanları).
    void DumpAttrs(const Geom& g, const uint8_t* rec, const char* tag)
    {
        Line("  %s imza=%.4s flags=0x%X used=0x%X alloc=0x%X baseRef=0x%llX attrsOff=0x%X",
             tag, (const char*)rec, Rd16(rec + 0x16), Rd32(rec + 0x18), Rd32(rec + 0x1C),
             (unsigned long long)Rd64(rec + 0x20), Rd16(rec + 0x14));
        uint32_t off = Rd16(rec + 0x14);
        for (int guard = 0; guard < 24 && off + 8 <= g.recSize; ++guard)
        {
            const uint32_t type = Rd32(rec + off), len = Rd32(rec + off + 4) & 0xFFFFu;
            if (type == 0xFFFFFFFFu || len < 8 || off + len > g.recSize) break;
            Line("     @0x%03X type=0x%02X len=0x%X nonRes=%u nameLen=%u id=0x%X aFlags=0x%X",
                 off, type, len, rec[off + 8], rec[off + 0x09], Rd16(rec + off + 0x0E),
                 Rd16(rec + off + 0x0C));
            {
                const uint32_t vOff = Rd16(rec + off + 0x14);
                const uint32_t base = rec[off + 8] ? 0x10u
                                      : (vOff >= 0x18 && off + vOff <= g.recSize ? vOff : 0x18u);
                std::string hx;
                for (uint32_t i = base; i < base + 32 && i < len && i < g.recSize - off; i += 4)
                {
                    char q[16];
                    std::snprintf(q, sizeof(q), " %08X", Rd32(rec + off + i));
                    hx += q;
                }
                Line("        govde+0x%X:%s", base, hx.c_str());
            }
            if (type == 0x80 && rec[off + 8] && off + 0x40 <= g.recSize)
                Line("        DATA vcn@+10=%llu vcnSayi@+18=%llu runOff@+20=%u alloc@+28=%llu real@+30=%llu init@+38=%llu",
                     (unsigned long long)Rd64(rec + off + 0x10), (unsigned long long)Rd64(rec + off + 0x18),
                     Rd16(rec + off + 0x20), (unsigned long long)Rd64(rec + off + 0x28),
                     (unsigned long long)Rd64(rec + off + 0x30), (unsigned long long)Rd64(rec + off + 0x38));
            if (type == 0x80 && !rec[off + 8])
                Line("        DATA(resident) valLen=%u valOff=0x%X", Rd32(rec + off + 0x10), Rd16(rec + off + 0x14));
            if (type == 0x30)
            {
                const uint16_t fvOff = Rd16(rec + off + 0x14);
                if (fvOff >= 0x18 && off + fvOff + 0x42 <= g.recSize)
                {
                    const uint8_t* f = rec + off + fvOff;
                    const uint8_t nl = f[0x40], ns = f[0x41];
                    std::wstring nm;
                    for (uint8_t k = 0; k < nl && 0x42 + (size_t)k * 2 + 2 <= (size_t)(len - fvOff); ++k)
                        nm += (wchar_t)Rd16(f + 0x42 + (size_t)k * 2);
                    Line("        FN parent=0x%llX alloc=%llu real=%llu ns=%u attr=0x%X ad=%ls",
                         (unsigned long long)Rd64(f + 0x00), (unsigned long long)Rd64(f + 0x28),
                         (unsigned long long)Rd64(f + 0x30), ns, Rd32(f + 0x38), nm.c_str());
                }
            }
            if (type == 0x10)
            {
                const uint16_t svOff = Rd16(rec + off + 0x14);
                if (svOff >= 0x18 && off + svOff + 0x34 <= g.recSize)
                {
                    const uint8_t* s = rec + off + svOff;
                    Line("        SI attr=0x%X sahip=%u guvenlik=%u zaman=0x%llX",
                         Rd32(s + 0x20), Rd32(s + 0x30), Rd32(s + 0x34),
                         (unsigned long long)Rd64(s + 0x08));
                }
            }
            off += len;
        }
        // Zincir neden koptu ve orada ham olarak ne duruyor: $DATA'siz görünen
        // kayıtların gerçek nedenini ölçmek için (used, kopuk ofsetinden büyükse
        // kayıtta okunmamış baytlar var demektir).
        {
            std::string hx;
            for (uint32_t i = off; i + 4 <= off + 64 && i + 4 <= g.recSize; i += 4)
            {
                char q[16];
                std::snprintf(q, sizeof(q), " %08X", Rd32(rec + i));
                hx += q;
            }
            Line("        kopuk@0x%03X used=0x%X type=0x%X len=0x%X ham%s",
                 off, Rd32(rec + 0x18), Rd32(rec + off), Rd32(rec + off + 4), hx.c_str());
        }
    }

    // Düğüm bayraklarının doğruluk kaynağı: $SI +0x20 ile $FN +0x38 ikisi de
    // okunur ve karşılaştırılır (1219 örnek: Win32 maskesine SI 1179, FN 1178
    // uydu -> motor SI'ı tercih ediyor). GetFileAttributesExW AYNI yolda bu
    // makinede farklı maske döndürebiliyor (ölçülen Containers katmanında
    // 0x2020 vs FindFirstFile 0x3620), bu yüzden karşılaştırma bulmaca API'siyle
    // yani walk motorunun kullandığı çağrıyla yapılır.
    bool Win32AttrTime(const wchar_t* path, DWORD& attrs, uint32_t& mtime)
    {
        WIN32_FIND_DATAW d{};
        HANDLE h = FindFirstFileW(path, &d);
        if (h == INVALID_HANDLE_VALUE) return false;
        FindClose(h);
        attrs = d.dwFileAttributes;
        mtime = disk::UnixSeconds(((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32)
                                  | d.ftLastWriteTime.dwLowDateTime);
        return true;
    }

    // Kaydın iki bayrak kaynağını birlikte okur: $STANDARD_INFORMATION +0x20 ve
    // ilk $FILE_NAME +0x38. Motor hangisini seçerse Win32'ye daha çok uydugunu
    // sayimla secmek icin; reparse noktalarinda hedefin degil baglantinin kendi
    // kaydi okunur (FILE_FLAG_OPEN_REPARSE_POINT), cunku FindFirstFileW da
    // baglantiyi dokur.
    bool AttrMasksForPath(const Geom& g, const std::wstring& wpath,
                          uint32_t& siMask, uint32_t& fnMask)
    {
        siMask = fnMask = 0;
        HANDLE f = CreateFileW(wpath.c_str(), FILE_READ_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                               nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION bi{};
        const BOOL ok = GetFileInformationByHandle(f, &bi);
        CloseHandle(f);
        if (!ok) return false;
        uint64_t bytePos = 0;
        if (!IndexToPos(g, ((((uint64_t)bi.nFileIndexHigh << 32) | bi.nFileIndexLow)
                            & 0x0000FFFFFFFFFFFFull), bytePos)) return false;
        const uint64_t aligned = (bytePos / g.clusBytes) * g.clusBytes;
        const uint32_t in      = (uint32_t)(bytePos - aligned);
        const uint32_t cap     = (uint32_t)(g.clusBytes + g.recSize);
        uint8_t* b = (uint8_t*)VirtualAlloc(nullptr, cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!b) return false;
        LARGE_INTEGER li; li.QuadPart = (LONGLONG)aligned;
        SetFilePointerEx(g.vol, li, nullptr, FILE_BEGIN);
        DWORD got = 0;
        const bool good = ReadFile(g.vol, b, cap, &got, nullptr) && got >= in + g.recSize;
        if (good)
        {
            const uint8_t* rec = b + in;
            if (std::memcmp(rec, "FILE", 4) == 0)
            {
                for (uint32_t off = Rd16(rec + 0x14), guard = 0;
                     guard < 24 && off + 8 <= g.recSize; ++guard)
                {
                    const uint32_t type = Rd32(rec + off);
                    const uint32_t len  = Rd32(rec + off + 4) & 0xFFFFu;
                    if (type == 0xFFFFFFFFu || len < 8 || off + len > g.recSize) break;
                    const uint32_t vOff = Rd16(rec + off + 0x14);
                    if (vOff >= 0x18 && off + vOff + 0x3C <= g.recSize)
                    {
                        const uint8_t* v = rec + off + vOff;
                        if (type == 0x10 && !siMask) siMask = Rd32(v + 0x20);
                        if (type == 0x30 && !fnMask) fnMask = Rd32(v + 0x38);
                    }
                    off += len;
                }
            }
        }
        VirtualFree(b, 0, MEM_RELEASE);
        return good && (siMask || fnMask);
    }

    // Win32'nin bu yola ne dedigi: klasor mü, sikistirilmis mi, reparse mi,
    // 8.3 karsiligi ne. Boyutu olmayan/uyduruk boyutlu MFT kayitlarinin
    // gercek dogasi burada aydinlanir.
    void Win32Facts(const std::wstring& wpath)
    {
        WIN32_FIND_DATAW d{};
        HANDLE h = FindFirstFileW(wpath.c_str(), &d);
        if (h == INVALID_HANDLE_VALUE)
        { Line("  win32: FindFirstFileW yok err=%lu", GetLastError()); return; }
        FindClose(h);
        Line("  win32: attr=0x%X dir=%u compressed=%u sparse=%u reparse=%u "
             "gercekAd=%ls kisaAd=%ls boyut=%llu",
             d.dwFileAttributes,
             (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1u : 0u,
             (d.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) ? 1u : 0u,
             (d.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) ? 1u : 0u,
             (d.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | 0x40000000u)) ? 1u : 0u,
             d.cFileName, d.cAlternateFileName,
             (unsigned long long)(((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow));
    }

    // Bir dosyanin MFT kaydini FRN'den konumlandirip ham alanlari dok. Uzantı
    // kayıtları ($ATTRIBUTE_LIST'li dosyalar: sıkıştırılmış/parçalı) taban
    // kaydın hemen ardına gelir; onları da aynı tamponla dok, böylece $DATA'nın
    // toplam mı yoksa parça boyutu mu taşıdığı ölçülür.
    void DumpRecordForPath(const Geom& g, const std::wstring& wpath)
    {
        HANDLE f = CreateFileW(wpath.c_str(), FILE_READ_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) { Line("dump: acilamadi %ls err=%lu", wpath.c_str(), GetLastError()); return; }
        BY_HANDLE_FILE_INFORMATION bi{};
        BOOL ok = GetFileInformationByHandle(f, &bi);
        uint64_t frn = ok ? (((uint64_t)bi.nFileIndexHigh << 32) | (uint64_t)bi.nFileIndexLow) : 0;
        CloseHandle(f);
        if (!ok) { Line("dump: bilgi alinamadi %ls", wpath.c_str()); return; }

        const uint64_t idx = frn & 0x0000FFFFFFFFFFFFull;
        uint64_t bytePos = 0;
        if (!IndexToPos(g, idx, bytePos))
        { Line("dump: indeks kosularin disinda idx=%llu", (unsigned long long)idx); return; }
        const uint64_t aligned = (bytePos / g.clusBytes) * g.clusBytes;
        const uint32_t in      = (uint32_t)(bytePos - aligned);

        const uint32_t capBytes = (uint32_t)(g.clusBytes * 8);   // 8 küme ileri oku
        uint8_t* b = (uint8_t*)VirtualAlloc(nullptr, capBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!b) return;
        LARGE_INTEGER li; li.QuadPart = (LONGLONG)aligned;
        SetFilePointerEx(g.vol, li, nullptr, FILE_BEGIN);
        DWORD got = 0;
        if (!ReadFile(g.vol, b, capBytes, &got, nullptr) || got < g.recSize)
        { Line("dump: kayit okunamadi idx=%llu err=%lu", (unsigned long long)idx, GetLastError()); VirtualFree(b, 0, MEM_RELEASE); return; }
        const uint32_t recs = (got - in) / g.recSize;

        uint64_t wr = 0, wa = 0;
        Win32Sizes(wpath.c_str(), wr, wa);
        Line("dump: %ls", wpath.c_str());
        Line("      frn=0x%llX idx=%llu  win32 real=%llu compressed=%llu",
             (unsigned long long)frn, (unsigned long long)idx,
             (unsigned long long)wr, (unsigned long long)wa);
        Win32Facts(wpath);
        char tag[64];
        std::snprintf(tag, sizeof(tag), "TABAN   idx=%llu", (unsigned long long)idx);
        DumpAttrs(g, b + in, tag);
        uint32_t extSeen = 0;
        for (uint32_t j = 1; j < recs && extSeen < 6; ++j)
        {
            const uint8_t* r = b + in + (size_t)j * g.recSize;
            if (Rd32(r) != 0x454C4946u) continue;                    // "FILE"
            if ((Rd64(r + 0x20) & 0x0000FFFFFFFFFFFFull) != idx) continue;
            std::snprintf(tag, sizeof(tag), "UZANTI  idx=%llu", (unsigned long long)(idx + j));
            DumpAttrs(g, r, tag);
            ++extSeen;
        }
        VirtualFree(b, 0, MEM_RELEASE);
    }

    int Descend(const disk::Scanner& sc, const char* const* parts, size_t n)
    {
        const auto& nodes = sc.Nodes();
        const auto& pool  = sc.Pool();
        int cur = sc.Root();
        for (size_t i = 0; i < n && cur >= 0 && cur < (int)nodes.size(); ++i)
        {
            const std::string want = Lower(parts[i]);
            int found = -1;
            for (int c = nodes[cur].firstChild; c >= 0 && c < (int)nodes.size(); c = nodes[c].nextSib)
            {
                const disk::Node& nd = nodes[c];
                if (nd.nameOff + nd.nameLen > pool.size()) continue;
                std::string got(pool.data() + nd.nameOff, nd.nameLen);
                if (Lower(got) == want) { found = c; break; }
            }
            cur = found;
        }
        return cur;
    }

    // ---- #9 gunu: $Secure:$SDS olcumu --------------------------------------
    // Aday anahtar iki: $SI +0x30 (owner id) ve $SI +0x34 (security id).
    // Hangisi $SDS girdilerine düşüyor varsayılmıyor: tek MFT geçişinde ikisi
    // de sayaçlanır. $SDS akışı taban kayıtta DEĞİL (ölçüldü: kayıt 9'da
    // yalnızca $SI + $INDEX_ROOT + $FN + iki $ATTRIBUTE_LIST var) ve adla açma
    // yöneticiyken de err=5 veriyor; akış baseRef==9 uzantı kayıtlarındaki
    // adlandırılmış ($SDS, nameLen=4) non-resident $DATA parçalarından
    // VCN sırasında birleştirilir.

    // Koşu listesinden bayt akışı okur. NO_BUFFERING: tampon küme hizalı olmalı,
    // bu yüzden VirtualAlloc tamponu kullanılır.
    bool ReadRunStream(const Geom& g, const std::vector<disk::DataRun>& runs,
                       uint64_t wantBytes, std::vector<uint8_t>& out)
    {
        out.clear();
        if (runs.empty() || !g.clusBytes) return false;
        const uint32_t chunkClus = 64;
        const uint32_t capBytes  = chunkClus * (uint32_t)g.clusBytes;
        uint8_t* buf = (uint8_t*)VirtualAlloc(nullptr, capBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!buf) return false;
        uint64_t logical = 0;
        bool ok = true;
        for (const auto& r : runs)
        {
            for (uint64_t c = 0; c < r.len; c += chunkClus)
            {
                const uint64_t take = std::min<uint64_t>(chunkClus, r.len - c);
                LARGE_INTEGER li; li.QuadPart = (LONGLONG)((r.lcn + c) * g.clusBytes);
                SetFilePointerEx(g.vol, li, nullptr, FILE_BEGIN);
                DWORD got = 0;
                const uint32_t bytes = (uint32_t)(take * g.clusBytes);
                if (!ReadFile(g.vol, buf, bytes, &got, nullptr) || got != bytes)
                { ok = false; goto done; }
                const uint64_t room = wantBytes > logical ? wantBytes - logical : 0;
                const uint32_t add  = (uint32_t)std::min<uint64_t>(room, bytes);
                out.insert(out.end(), buf, buf + add);
                logical += bytes;
                if (logical >= wantBytes) goto done;
            }
        }
    done:
        VirtualFree(buf, 0, MEM_RELEASE);
        return ok;
    }

    std::string SidStr(PSID psid)
    {
        if (!psid || !IsValidSid(psid)) return std::string();
        LPSTR s = nullptr;
        if (!ConvertSidToStringSidA(psid, &s)) return std::string();
        std::string out = s ? s : "";
        LocalFree(s);
        return out;
    }

    // SID -> hesap adı. #9'un tembel çözümü bu çağrıyı arka plan iş
    // parçacığında yapacak; burada hem başarı hem hata yolu ölçülür.
    std::string SidToName(const std::string& sidStr)
    {
        if (sidStr.empty()) return std::string("-");
        PSID sid = nullptr;
        if (!ConvertStringSidToSidA(sidStr.c_str(), &sid)) return std::string("cevrilemedi");
        // İki çağrılı kullanım: boyut NULL tamponla sorulur (API ancak o zaman
        // gereken uzunluğu yazar), sonra tam büyük lütfen çözülür. Küçük ama
        // dolu bir tampon vermek ERROR_MORE_DATA'ya rağmen taşmaya yol açabiliyor
        // — ölçülen çökme sebebi buydu.
        DWORD cn = 0, cd = 0;
        SID_NAME_USE use{};
        LookupAccountSidA(nullptr, sid, nullptr, &cn, nullptr, &cd, &use);
        if (!cn || !cd)
        {
            LocalFree(sid);
            return std::string("boyut-bilinmiyor");
        }
        std::vector<char> name(cn + 4, 0), dom(cd + 4, 0);
        DWORD cn2 = cn + 4, cd2 = cd + 4;
        std::string out;
        if (LookupAccountSidA(nullptr, sid, name.data(), &cn2, dom.data(), &cd2, &use))
        { out = dom.data(); out += '\\'; out += name.data(); }
        else
        { char b[32]; std::snprintf(b, sizeof b, "err=%lu", GetLastError()); out = b; }
        LocalFree(sid);
        return out;
    }

    // Bir yolun Win32 sahiplik SID'i, çağıranın kendi tamponunda. Bilinçli olarak
    // GetNamedSecurityInfoW KULLANILMIYOR: onun döndürdüğü SID'i LocalFree() ile
    // serbest bırakmak bu build'de STATUS_HEAP_CORRUPTION veriyor (ölçülen);
    // GetFileSecurityW + GetSecurityDescriptorOwner ise API'nin ayırdığı belleği
    // hiç görmüyor.
    bool Win32OwnerSid(const wchar_t* path, std::string& out)
    {
        out.clear();
        DWORD need = 0;
        if (!GetFileSecurityW(path, OWNER_SECURITY_INFORMATION, nullptr, 0, &need) &&
            GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return false;
        if (!need) return false;
        std::vector<uint8_t> buf(need + 64, 0);
        if (!GetFileSecurityW(path, OWNER_SECURITY_INFORMATION, buf.data(),
                              (DWORD)buf.size(), &need))
            return false;
        PSID own = nullptr;
        BOOL def = FALSE;
        if (!GetSecurityDescriptorOwner((PSECURITY_DESCRIPTOR)buf.data(), &own, &def)) return false;
        out = SidStr(own);
        return !out.empty();
    }

    // Yığın bozulması serbest bırakma/ayırmada patlar; neden-yer karışmasın diye
    // küçük bir ayır/serbest çiftiyle "şu ana kadar yığın temiz mi" sorulur.
    void HeapProbe(const char* where)
    {
        void* p = std::malloc(64);
        std::free(p);
        Line("  yigin temiz: %s", where);
    }

    // $SI'ın iki sahip alanını birden okur: +0x30 owner id, +0x34 security id.
    // Ham alan yürüyüşü (motor ayrıştırıcısı değil), böylece motorun seçtiği
    // anahtardan bağımsız ölçüm yapılır.
    bool SiOwnerIds(const uint8_t* rec, uint32_t recSize, uint32_t& owner, uint32_t& sec)
    {
        owner = sec = 0;
        for (uint32_t off = Rd16(rec + 0x14), guard = 0;
             guard < 24 && off + 8 <= recSize; ++guard)
        {
            const uint32_t type = Rd32(rec + off);
            const uint32_t len  = Rd32(rec + off + 4) & 0xFFFFu;
            if (type == 0xFFFFFFFFu || len < 8 || off + len > recSize) break;
            if (type == 0x10 && rec[off + 8] == 0)
            {
                const uint32_t vo = Rd16(rec + off + 0x14);
                if (vo >= 0x18 && off + vo + 0x38 <= recSize)
                {
                    owner = Rd32(rec + off + vo + 0x30);
                    sec   = Rd32(rec + off + vo + 0x34);
                    return true;
                }
            }
            off += len;
        }
        return false;
    }

    // Bir yolun MFT kaydından iki sahip alanı (FRN -> konum -> $SI).
    bool SidsOfPath(const Geom& g, const std::wstring& wpath,
                    uint32_t& ownerId, uint32_t& secId)
    {
        ownerId = secId = 0;
        HANDLE f = CreateFileW(wpath.c_str(), FILE_READ_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION bi{};
        const BOOL ok = GetFileInformationByHandle(f, &bi);
        CloseHandle(f);
        if (!ok) return false;
        uint64_t bytePos = 0;
        if (!IndexToPos(g, ((((uint64_t)bi.nFileIndexHigh << 32) | bi.nFileIndexLow)
                            & 0x0000FFFFFFFFFFFFull), bytePos)) return false;
        const uint64_t aligned = (bytePos / g.clusBytes) * g.clusBytes;
        const uint32_t in      = (uint32_t)(bytePos - aligned);
        const uint32_t cap     = (uint32_t)(g.clusBytes + g.recSize);
        uint8_t* b = (uint8_t*)VirtualAlloc(nullptr, cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!b) return false;
        LARGE_INTEGER li; li.QuadPart = (LONGLONG)aligned;
        SetFilePointerEx(g.vol, li, nullptr, FILE_BEGIN);
        DWORD got = 0;
        bool res = false;
        if (ReadFile(g.vol, b, cap, &got, nullptr) && got >= in + g.recSize)
            res = SiOwnerIds(b + in, (uint32_t)g.recSize, ownerId, secId);
        VirtualFree(b, 0, MEM_RELEASE);
        return res;
    }

    // Uzantı kaydın adlandırılmış non-resident $DATA parçaları: ad uzunluğu 4
    // UTF-16 birim = "$SDS". Mantıksal küme aralığı (lowVcn..highVcn) ve koşu
    // listesi döner; birleştirme VCN sırasında göre yapılır.
    struct SdsFrag
    {
        uint32_t                 recIdx = 0;
        uint64_t                 lowVcn = 0, highVcn = 0;
        uint64_t                 alloc = 0, real = 0, init = 0;
        uint64_t                 runClus = 0;
        std::vector<disk::DataRun> runs;
    };

    void CollectSdsFrag(const Geom& g, const uint8_t* rec, uint32_t recIdx,
                        std::vector<SdsFrag>& frags)
    {
        for (uint32_t off = Rd16(rec + 0x14), guard = 0;
             guard < 24 && off + 8 <= g.recSize; ++guard)
        {
            const uint32_t type = Rd32(rec + off);
            const uint32_t len  = Rd32(rec + off + 4) & 0xFFFFu;
            if (type == 0xFFFFFFFFu || len < 8 || off + len > g.recSize) break;
            if (type == 0x80 && rec[off + 8] == 1 && rec[off + 0x09] == 4 &&
                off + 0x40 <= g.recSize)
            {
                uint32_t rr = 0;
                bool abs = false;
                std::vector<disk::DataRun> runs;
                if (disk::NonResidentRunsOffset(rec + off, len, rr) &&
                    disk::DecodeDataRuns(rec + off + rr, len - rr, abs, runs) && !abs)
                {
                    SdsFrag fr;
                    fr.recIdx  = recIdx;
                    fr.lowVcn  = Rd64(rec + off + 0x10);
                    fr.highVcn = Rd64(rec + off + 0x18);
                    fr.alloc   = Rd64(rec + off + 0x28) & 0x0000FFFFFFFFFFFFull;
                    fr.real    = Rd64(rec + off + 0x30) & 0x0000FFFFFFFFFFFFull;
                    fr.init    = Rd64(rec + off + 0x38) & 0x0000FFFFFFFFFFFFull;
                    for (const auto& r : runs) fr.runClus += r.len;
                    fr.runs = std::move(runs);
                    frags.push_back(std::move(fr));
                }
            }
            off += len;
        }
    }

    // $SDS girdi düzeni tahminle değil aramayla bulunur:
    //   (a) akış başındaki başlık boyu (H) ve "sonraki girdi" uzunluk alanının
    //       göreli ofseti (L, u16/u32) adaylaştırılır; bir aday ancak yürüyüş
    //       TAM olarak akışın sonunda biterse geçerlidir;
    //   (b) etiketli veri kullanılır: Win32 sahibinin (GetNamedSecurityInfoW)
    //       SID'i ham bayt olarak akışta aranır, bulunduğu girdinin başlangıcına
    //       göre ofset S olur; aynı girdide security id değerini taşıyan u32
    //       ofsetleri K adaylarıdır. Tüm etiketlerde aynı (K,S) çıkarsa düzen
    //       ölçülmüştür — dokümandan değil, bu hacimden.
    struct SecPair
    {
        std::wstring         path;
        uint32_t             secId = 0, ownerId = 0;
        std::string          sid, name;
        std::vector<uint8_t> bin;          // SID'in ham baytları (EqualSid için)
    };

    std::vector<size_t> FindAll(const std::vector<uint8_t>& hay, const std::vector<uint8_t>& pat)
    {
        std::vector<size_t> at;
        if (pat.empty() || hay.size() < pat.size()) return at;
        for (size_t i = 0; i + pat.size() <= hay.size(); ++i)
            if (std::memcmp(hay.data() + i, pat.data(), pat.size()) == 0) at.push_back(i);
        return at;
    }

    // Yürüyüş sonucu: girdi başlangıçları ARTIK atılmıyor, nerede ve neden
    // durduğu raporlanıyor. Kuyruk (alloc 2.924.544 > real 2.920.532) yüzünden
    // "akış tam bitmeli" testi bütün adayları çöpe atıyordu (ölçülen: aday=0).
    struct Walk
    {
        std::vector<size_t> starts;
        size_t      off = 0;   // yürüyüşün durduğu bayt
        std::string why;       // durma nedeni (ASCII)
        unsigned long long min = 0, max = 0, sum = 0;
    };

    // İlk girdi başı `anchor`; girdi uzunluk alanı başlığa göre `L` ofsetinde
    // (u16 veya u32).
    Walk WalkSds(const std::vector<uint8_t>& sds, size_t anchor, uint32_t L, bool u16)
    {
        Walk w;
        const size_t n = sds.size();
        w.off = anchor;
        if (anchor >= n) { w.why = "anchor-disi"; return w; }
        w.starts.push_back(anchor);
        for (size_t guard = 0; guard < 2000000; ++guard)
        {
            const size_t la = w.off + L;
            if (la + (u16 ? 2u : 4u) > n) { w.why = "len-alani-disari"; return w; }
            const unsigned long long len = u16 ? Rd16(sds.data() + la)
                                               : Rd32(sds.data() + la);
            if (len < 0x20) { w.why = "len-kucuk-" + std::to_string(len); return w; }
            if (len > 0x40000) { w.why = "len-buyuk-" + std::to_string(len); return w; }
            if (w.off + len > n) { w.why = "len-akisi-asti-" + std::to_string(len); return w; }
            if (!w.min || len < w.min) w.min = len;
            if (len > w.max) w.max = len;
            w.sum += len;
            w.off += (size_t)len;
            if (w.off >= n) { w.why = "bitis"; return w; }
            w.starts.push_back(w.off);
        }
        w.why = "guard";
        return w;
    }

    uint32_t EntryLen(const std::vector<uint8_t>& sds, size_t start, uint32_t L, bool u16)
    {
        return u16 ? Rd16(sds.data() + start + L) : Rd32(sds.data() + start + L);
    }

    // Düzenin bayıtlardan okunması: başlık dökümü + etiket SID'lerinin konum
    // farkları + secId alanının SID'e göre mesafesi. Bunlar olmadan (H,L,K,S)
    // araması kör; önceki deneme aday=0 üretti.
    void SdsEvidence(const std::vector<uint8_t>& sds, const std::vector<SecPair>& want,
                     const std::vector<std::vector<size_t>>& atList)
    {
        Line("sds-kanit: baslik kelimeleri (u32, little-endian)");
        for (size_t i = 0; i < 0x200 && i + 4 <= sds.size(); i += 0x20)
        {
            std::string hx;
            for (uint32_t q = 0; q < 0x20; q += 4)
            { char s[16]; std::snprintf(s, sizeof s, " %08X", Rd32(sds.data() + i + q)); hx += s; }
            Line("  +0x%04X:%s", (unsigned)i, hx.c_str());
        }
        for (size_t li = 0; li < want.size(); ++li)
        {
            const SecPair& w = want[li];
            const std::vector<size_t>& at = atList[li];
            if (at.size() < 2)
            { Line("  kanit sec=%u bayt=%zu konum=%zu (fark histogrami yok)", w.secId,
                   w.bin.size(), at.size()); continue; }
            std::map<size_t, unsigned long long> gap;
            for (size_t i = 1; i < at.size(); ++i) ++gap[at[i] - at[i - 1]];
            std::vector<std::pair<size_t, unsigned long long>> gs(gap.begin(), gap.end());
            std::sort(gs.begin(), gs.end(),
                      [](const std::pair<size_t, unsigned long long>& a,
                         const std::pair<size_t, unsigned long long>& b) { return a.second > b.second; });
            std::string s;
            for (size_t i = 0; i < gs.size() && i < 8; ++i)
            { char b[32]; std::snprintf(b, sizeof b, " %llu:%llu", (unsigned long long)gs[i].first,
                                        gs[i].second); s += b; }
            Line("  kanit sec=%u bayt=%zu konum=%zu fark(adet):%s", w.secId, w.bin.size(),
                 at.size(), s.c_str());

            // SID'den geriye: hangi mesafede secId değeri duruyor?
            std::map<long long, unsigned long long> dd;
            const size_t cap = at.size() < 40 ? at.size() : 40;
            for (size_t i = 0; i < cap; ++i)
                for (long long d = 4; d <= 0x200; d += 4)
                {
                    const long long p = (long long)at[i] - d;
                    if (p < 0) break;
                    if (Rd32(sds.data() + p) == w.secId) ++dd[d];
                }
            std::vector<std::pair<long long, unsigned long long>> ds(dd.begin(), dd.end());
            std::sort(ds.begin(), ds.end(),
                      [](const std::pair<long long, unsigned long long>& a,
                         const std::pair<long long, unsigned long long>& b) { return a.second > b.second; });
            std::string t;
            for (size_t i = 0; i < ds.size() && i < 8; ++i)
            { char b[32]; std::snprintf(b, sizeof b, " -%llu:%llu", (unsigned long long)ds[i].first,
                                        ds[i].second); t += b; }
            Line("  kanit sec=%u sid-gerisinde-secId(adet):%s", w.secId,
                 t.empty() ? std::string(" YOK").c_str() : t.c_str());
        }
    }

    // ---- $SDS girdi düzeni: öz-ofset denetimli tarama -----------------------
    // Uzunluk-alanı yürüyüşü (H,L) hiçbir aday üretmedi (ölçülen: aday=0) ve
    // etiket oylaması da yapılamaz: SID'in girdi başına göre ofseti SABİT değil,
    // SD içindeki owner ofsetiyle birlikte büyüyor (ölçülen: kernel32 SID'inin
    // gerisinde secId taşıyan konumların histogramı neredeyse boş). Bayıt dökümü
    // başka bir yapısal sabit gösteriyor: +0x08 = girdinin akış içindeki KENDİ
    // bayt ofseti (ölçülen 0x00->0, 0x80->0x80, 0x100->0x100, 0x180->0x180).
    // Motor bu denetimi her 8'li ofste uygular; 2,9 MB akışta ~365 bin okuma,
    // yani yürüyüş zorunda kalmadan girdi sınırları.
    struct SdsEnt
    {
        size_t   off;
        uint32_t id, f10, ownerOff, groupOff;
        size_t   sidPos, groupPos;
    };

    void FindSdsLayout(const std::vector<uint8_t>& sds, const std::vector<SecPair>& want,
                       const std::map<uint32_t, uint64_t>& secHist)
    {
        const size_t n = sds.size();
        // Her etiketin SID ham baytı bir kez aranır: konumlar hem kanıt
        // dökümünde hem de aşağıdaki doğrulamada kullanılıyor.
        std::vector<std::vector<size_t>> atList;
        for (const SecPair& w : want) atList.push_back(FindAll(sds, w.bin));
        SdsEvidence(sds, want, atList);

        // Yürüyüş teşhisi: neden aday kalmadığının kanıtı akışa yazılır.
        {
            std::vector<Walk> best;
            for (uint32_t L = 4; L <= 0x80; L += 4)
                for (int u = 0; u < 2; ++u)
                {
                    Walk w = WalkSds(sds, 0, L, u != 0);
                    size_t ins = best.size();
                    while (ins > 0 && best[ins - 1].starts.size() < w.starts.size()) --ins;
                    best.insert(best.begin() + (long)ins, std::move(w));
                    if (best.size() > 5) best.resize(5);
                }
            Line("sds-yuruyus: en-iyi-5 (akis=%zu bayt)", n);
            for (size_t i = 0; i < best.size(); ++i)
                Line("  L deneme girdi=%zu bitti=0x%llX boyut=[%llu..%llu] neden=%s",
                     best[i].starts.size(), (unsigned long long)best[i].off, best[i].min,
                     best[i].max, best[i].why.c_str());
        }

        // Tarama.
        std::vector<SdsEnt> ents;
        unsigned long long sidBad = 0, revBad = 0, grpBad = 0, orderBad = 0, idLo = 0;
        uint32_t prevId = 0;
        const DWORD tScan = GetTickCount();
        for (size_t e = 0; e + 0x1C <= n; e += 8)
        {
            if (Rd32(sds.data() + e + 0x08) != (uint32_t)e) continue;
            const uint32_t id = Rd32(sds.data() + e + 0x04);
            if (id < 0x100) { ++idLo; continue; }
            if (sds.data()[e + 0x14] != 1) { ++revBad; continue; }
            const uint32_t oo = Rd32(sds.data() + e + 0x18);
            const uint32_t go = Rd32(sds.data() + e + 0x1C);
            const size_t sidPos = e + 0x14 + oo;
            if (sidPos + 8 > n || !IsValidSid((PSID)(sds.data() + sidPos))) { ++sidBad; continue; }
            const size_t grpPos = e + 0x14 + go;
            if (grpPos + 8 > n || !IsValidSid((PSID)(sds.data() + grpPos))) ++grpBad;
            if (id < prevId) ++orderBad;
            prevId = id;
            ents.push_back({ e, id, Rd32(sds.data() + e + 0x10), oo, go, sidPos, grpPos });
        }
        Line("sds-tara: girdi=%zu tarama=%lu ms okuma=%llu adim=8 id<0x100=%llu revDegil=%llu "
             "sidBozuk=%llu grupBozuk=%llu idArtmadi=%llu son=0x%llX akis=%zu",
             ents.size(), (unsigned long)(GetTickCount() - tScan),
             (unsigned long long)(n / 8), idLo, revBad, sidBad, grpBad, orderBad,
             ents.empty() ? 0u : (unsigned long long)ents.back().off,
             n);
        if (ents.empty())
        { Line("sds-duzen: oz-ofset denetimi hic girdi bulamadi -> motor $SDS'ten sahip cozemez"); return; }

        // Adım kuralı: +0x10 alanı girdi uzunluğu mu? Yürüyüş motoru gerekir mi?
        {
            unsigned long long sMin = 0xFFFFFFFFull, sMax = 0, mul8 = 0, eqF10 = 0, eqR8 = 0, eqF8 = 0;
            for (size_t i = 1; i < ents.size(); ++i)
            {
                const size_t d = ents[i].off - ents[i - 1].off;
                const uint32_t f = ents[i - 1].f10;
                if (d < sMin) sMin = d;
                if (d > sMax) sMax = d;
                if (d % 8 == 0) ++mul8;
                if (d == f) ++eqF10;
                if (d == ((f + 7u) & ~7u)) ++eqR8;
                if (d == (size_t)f + 8) ++eqF8;
            }
            Line("sds-adim: aralik=[%llu..%llu] 8e-kat=%llu/%zu | +0x10 esit=%llu yuvarlanmis=%llu arti8=%llu",
                 sMin, sMax, mul8, ents.size() - 1, eqF10, eqR8, eqF8);
        }

        // Harita: security id -> Win32 sahibi olan SID. Motorun #9'da kuracağı
        // tablonun gerçek boyutu ve tekil olup olmadığı burada ölçülüyor.
        std::unordered_map<uint32_t, std::string> bySec;
        unsigned long long dup = 0;
        for (const SdsEnt& en : ents)
        {
            const std::string s = SidStr((PSID)(sds.data() + en.sidPos));
            if (s.empty()) continue;
            if (!bySec.emplace(en.id, s).second) ++dup;
        }
        for (size_t i = 0; i < ents.size() && i < 4; ++i)
        {
            const SdsEnt& en = ents[i];
            const std::string o = SidStr((PSID)(sds.data() + en.sidPos));
            const std::string g = en.groupPos + 8 <= n
                                      ? SidStr((PSID)(sds.data() + en.groupPos)) : std::string();
            std::string hx;
            for (uint32_t q = 0; q < 0x14; q += 4)
            { char t[16]; std::snprintf(t, sizeof t, " %08X", Rd32(sds.data() + en.off + q)); hx += t; }
            Line("  girdi[%zu] 0x%06X ham:%s id=%u f10=0x%X ownerOff=0x%X grupOff=0x%X",
                 i, (unsigned)en.off, hx.c_str(), en.id, en.f10, en.ownerOff, en.groupOff);
            Line("     owner=%s (%s) grup=%s (%s)", o.c_str(), SidToName(o).c_str(),
                 g.c_str(), SidToName(g).c_str());
        }
        Line("sds: girdi=%zu benzersiz-id=%zu tekrar=%llu", ents.size(), bySec.size(), dup);

        // Kapsama: hacimde sayaçlanan security id'lerin ne kadarı tablodaki?
        {
            unsigned long long hitIds = 0, hitFiles = 0, totFiles = 0;
            for (const auto& kv : secHist)
            {
                totFiles += kv.second;
                if (bySec.count(kv.first)) { ++hitIds; hitFiles += kv.second; }
            }
            Line("sds kapsama: secId=%llu/%llu dosya=%llu/%llu", hitIds,
                 (unsigned long long)secHist.size(), hitFiles, totFiles);
            std::vector<std::pair<uint32_t, uint64_t>> top(secHist.begin(), secHist.end());
            std::sort(top.begin(), top.end(),
                      [](const std::pair<uint32_t, uint64_t>& a,
                         const std::pair<uint32_t, uint64_t>& b) { return a.second > b.second; });
            for (size_t i = 0; i < top.size() && i < 6; ++i)
            {
                const auto it = bySec.find(top[i].first);
                const std::string sid = it == bySec.end() ? std::string("YOK") : it->second;
                Line("  sahipler: sec=%u dosya=%llu sid=%s ad=%s", top[i].first,
                    (unsigned long long)top[i].second, sid.c_str(), SidToName(sid).c_str());
            }
        }

        // Etiketlerle son doğrulama: secId -> tablo -> SID == Win32 sahibi?
        for (const SecPair& w : want)
        {
            const auto it = bySec.find(w.secId);
            const std::string got = it == bySec.end() ? std::string("YOK") : it->second;
            Line("  dogrulama %ls: siOwner=%u siSecId=%u win32=%s sds=%s eslesme=%s ad=%s",
                 w.path.c_str(), w.ownerId, w.secId, w.sid.c_str(), got.c_str(),
                 (!w.sid.empty() && w.sid == got) ? "EVET" : "HAYIR", w.name.c_str());
        }
    }

    // Yolun Win32 sahibi: GetFileSecurityW + GetSecurityDescriptorOwner. Tüm
    // tamponlar bizde; API'den dönen bir bellek serbest bırakılmıyor. Ölçülen:
    // GetNamedSecurityInfoW'ın owner SID'i için LocalFree() bu build'de
    // STATUS_HEAP_CORRUPTION (0xC0000374) veriyor — free/free2 modları.
    bool OwnerSidOfPath(const wchar_t* p, std::vector<uint8_t>& bin, std::string& sid)
    {
        sid.clear(); bin.clear();
        DWORD need = 0;
        if (!GetFileSecurityW(p, OWNER_SECURITY_INFORMATION, nullptr, 0, &need)
            && GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
        if (!need) return false;
        bin.assign(need, 0);
        DWORD cap = (DWORD)bin.size();
        if (!GetFileSecurityW(p, OWNER_SECURITY_INFORMATION,
                              (PSECURITY_DESCRIPTOR)bin.data(), cap, &need)) return false;
        PSID own = nullptr;
        BOOL dflt = FALSE;
        if (!GetSecurityDescriptorOwner((PSECURITY_DESCRIPTOR)bin.data(), &own, &dflt)) return false;
        if (!own || !IsValidSid(own)) return false;
        const DWORD lb = GetLengthSid(own);
        if (lb < 8 || lb > 1024) return false;
        sid = SidStr(own);
        std::vector<uint8_t> copy((const uint8_t*)own, (const uint8_t*)own + lb);
        bin.swap(copy);
        return !sid.empty();
    }

    void MeasureSds(const Geom& g)
    {
        Line("=== $Secure:$SDS olcumu ===");
        if (g.vol == INVALID_HANDLE_VALUE || !g.recSize) { Line("sds: jeometri yok"); return; }

        // Kayıt 9 = $Secure (sabit MFT kayıtları: 0 $MFT, 3 $Volume, 9 $Secure).
        uint64_t bytePos = 0;
        if (!IndexToPos(g, 9, bytePos)) { Line("sds: kayit 9 kosularin disinda"); return; }
        const uint64_t aligned = (bytePos / g.clusBytes) * g.clusBytes;
        const uint32_t in      = (uint32_t)(bytePos - aligned);
        const uint32_t cap     = (uint32_t)(g.clusBytes + g.recSize);
        uint8_t* rec = (uint8_t*)VirtualAlloc(nullptr, cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!rec) return;
        LARGE_INTEGER li; li.QuadPart = (LONGLONG)aligned;
        SetFilePointerEx(g.vol, li, nullptr, FILE_BEGIN);
        DWORD got = 0;
        if (!ReadFile(g.vol, rec, cap, &got, nullptr) || got < in + g.recSize)
        { Line("sds: kayit 9 okunamadi err=%lu", GetLastError()); VirtualFree(rec, 0, MEM_RELEASE); return; }
        DumpAttrs(g, rec + in, "$SECURE");

        // TEK GEÇİŞ: $Secure'ın uzantı parçaları toplanır ve $SI'ın iki sahip
        // alanı birlikte sayaçlanır. Koşu listesi kovalamak yerine tüm $MFT bir
        // kez okunur (256 kümelik bloklar, ayrıştırma yok) → ek maliyet ihmal
        // edilebilir; hangi alanın gerçek anahtar olduğu sayıyla seçilir.
        std::vector<SdsFrag> frags;
        std::map<uint32_t, uint64_t> secHist;
        uint64_t sigRecs = 0, siRecs = 0, ownerNonZero = 0, extOf9 = 0;
        const DWORD tPass = GetTickCount();
        {
            const uint32_t chunkClus = 256;
            const uint32_t cb = chunkClus * (uint32_t)g.clusBytes;
            uint8_t* blk = (uint8_t*)VirtualAlloc(nullptr, cb, MEM_COMMIT | MEM_RESERVE,
                                                  PAGE_READWRITE);
            if (!blk) { VirtualFree(rec, 0, MEM_RELEASE); return; }
            uint64_t logical = 0;
            for (const auto& run : g.runs)
            {
                for (uint64_t c = 0; c < run.len; c += chunkClus)
                {
                    const uint64_t take = std::min<uint64_t>(chunkClus, run.len - c);
                    const uint32_t bytes = (uint32_t)(take * g.clusBytes);
                    LARGE_INTEGER l3; l3.QuadPart = (LONGLONG)((run.lcn + c) * g.clusBytes);
                    SetFilePointerEx(g.vol, l3, nullptr, FILE_BEGIN);
                    DWORD g3 = 0;
                    if (!ReadFile(g.vol, blk, bytes, &g3, nullptr) || g3 < bytes)
                    { logical += take * g.clusBytes; c = run.len; break; }
                    for (uint32_t k = 0; (uint64_t)k + g.recSize <= g3; k += (uint32_t)g.recSize)
                    {
                        const uint8_t* rr = blk + k;
                        if (std::memcmp(rr, "FILE", 4) != 0) continue;
                        ++sigRecs;
                        const uint64_t base = Rd64(rr + 0x20) & 0x0000FFFFFFFFFFFFull;
                        const uint32_t idx = (uint32_t)((logical + c * g.clusBytes + k) / g.recSize);
                        if (base == 9) { ++extOf9; CollectSdsFrag(g, rr, idx, frags); continue; }
                        if (base != 0 || !(Rd16(rr + 0x16) & 1)) continue;
                        uint32_t ow = 0, sec = 0;
                        if (!SiOwnerIds(rr, (uint32_t)g.recSize, ow, sec)) continue;
                        ++siRecs;
                        if (ow) ++ownerNonZero;
                        ++secHist[sec];
                    }
                    logical += take * g.clusBytes;
                }
            }
            VirtualFree(blk, 0, MEM_RELEASE);
        }
        VirtualFree(rec, 0, MEM_RELEASE);
        Line("sds: gecis=%lu ms FILE=%llu uzanti9=%llu siOk=%llu owner!=0=%llu benzersiz-secId=%zu",
             (unsigned long)(GetTickCount() - tPass), (unsigned long long)sigRecs,
             (unsigned long long)extOf9, (unsigned long long)siRecs,
             (unsigned long long)ownerNonZero, secHist.size());
        HeapProbe("MFT gecisi sonrasi");

        // Dağılım: harita boyutu tipik olarak bu kadar küçükse id -> SID tablosu
        // bellekte ucuzdur. En çok kullanılan id'ler dosya sayısıyla.
        {
            std::vector<std::pair<uint32_t, uint64_t>> top(secHist.begin(), secHist.end());
            std::sort(top.begin(), top.end(),
                      [](const std::pair<uint32_t, uint64_t>& a,
                         const std::pair<uint32_t, uint64_t>& b) { return a.second > b.second; });
            std::string s;
            for (size_t i = 0; i < top.size() && i < 10; ++i)
            { char b[32]; std::snprintf(b, sizeof b, " %u:%llu", top[i].first,
                                        (unsigned long long)top[i].second); s += b; }
            Line("sds: en-cok-secId (dosya sayisi):%s", s.c_str());
        }

        // Parçalar VCN sırasında dizilir; koşu listeleri bu sırayla
        // birleştirilirse tek mantıksal akış elde edilir (aşağıdaki okuyucu
        // listeyi sırayla tüketiyor).
        std::vector<disk::DataRun> sdsRuns;
        uint64_t streamLen = 0;
        {
            std::sort(frags.begin(), frags.end(),
                      [](const SdsFrag& a, const SdsFrag& b) { return a.lowVcn < b.lowVcn; });
            uint64_t nextVcn = 0, totalClus = 0;
            bool bitisik = true;
            for (const auto& f : frags)
            {
                const uint64_t need = f.highVcn + 1 - f.lowVcn;
                Line("  parca idx=%u vcn=[%llu..%llu] beklenenKume=%llu kosuKume=%llu kosu=%zu alloc=%llu real=%llu init=%llu",
                     f.recIdx, (unsigned long long)f.lowVcn, (unsigned long long)f.highVcn,
                     (unsigned long long)need, (unsigned long long)f.runClus, f.runs.size(),
                     (unsigned long long)f.alloc, (unsigned long long)f.real,
                     (unsigned long long)f.init);
                if (f.lowVcn != nextVcn || f.runClus != need) bitisik = false;
                nextVcn = f.highVcn + 1;
                totalClus += need;
                if (f.real > streamLen) streamLen = f.real;
                sdsRuns.insert(sdsRuns.end(), f.runs.begin(), f.runs.end());
            }
            Line("sds: parca=%zu bitisik=%s kume=%llu bayt=%llu kosu=%zu streamLen=%llu",
                 frags.size(), bitisik ? "EVET" : "HAYIR", (unsigned long long)totalClus,
                 (unsigned long long)(totalClus * g.clusBytes), sdsRuns.size(),
                 (unsigned long long)streamLen);
        }
        if (sdsRuns.empty() || !streamLen)
        {
            Line("sds: akis yok -> baseRef==9 kayitlarinda adlandirilmis $DATA bulunamadi");
            return;
        }

        std::vector<uint8_t> sds;
        const DWORD tRead = GetTickCount();
        const bool ok = ReadRunStream(g, sdsRuns, streamLen, sds);
        const DWORD msRead = (unsigned long)(GetTickCount() - tRead);
        if (!ok)
        { Line("sds: akis okunamadi (istenen=%llu)", (unsigned long long)streamLen); return; }
        Line("sds: okunan=%zu bayt (istenen=%llu) okuma=%lu ms", sds.size(),
             (unsigned long long)streamLen, msRead);
        if (sds.size() < 0x40) { Line("sds: akis kisa"); return; }

        std::string hx;
        for (size_t i = 0; i < 64; i += 4)
        { char q[16]; std::snprintf(q, sizeof q, " %08X", Rd32(sds.data() + i)); hx += q; }
        Line("sds baslik:%s", hx.c_str());
        HeapProbe("baslik dokumu sonrasi");

        // Etiketli veri: Win32 sahibi bilinen dosyalar. Düzen arayışı bunlarla
        // puanlanır; farklı sahipli yollar seçilir ki tek rastlantı kazananı
        // belirlemesin.
        const wchar_t* paths[] = {
            L"C:\\Windows\\System32\\kernel32.dll",
            L"C:\\Windows\\System32\\drivers\\etc\\hosts",
            L"C:\\Users\\Public\\tgengine\\engine.txt",
            L"C:\\Windows\\explorer.exe",
            L"C:\\Windows\\System32\\lsass.exe",
            L"C:\\Users" };
        std::vector<SecPair> want;
        for (size_t i = 0; i < sizeof paths / sizeof *paths; ++i)
        {
            const wchar_t* p = paths[i];
            SecPair w;
            w.path = p;
            if (!OwnerSidOfPath(p, w.bin, w.sid))
            { Line("  etiket[%zu] %ls: Win32 sahibi alinamadi (err=%lu) -> elendi",
                   i, p, GetLastError()); continue; }
            w.name = SidToName(w.sid);
            if (!SidsOfPath(g, p, w.ownerId, w.secId))
            { Line("     %ls: MFT kaydi okunamadi -> elendi", p); continue; }
            if (w.secId == 0)
            { Line("     %ls: secId=0 (siOwner=%u) -> anahtar tasimiyor, elendi", p, w.ownerId); continue; }
            // Aynı (secId, SID) çifti iki kez oy vermesin: kanıt bağımsız olmalı.
            bool dup = false;
            for (const SecPair& e : want)
                if (e.secId == w.secId && e.sid == w.sid) { dup = true; break; }
            if (dup) { Line("     %ls: tekrar etiket (secId=%u) -> atlandi", p, w.secId); continue; }
            Line("  etiket[%zu] %ls: secId=%u siOwner=%u sid=%s ad=%s bayt=%zu",
                 i, p, w.secId, w.ownerId, w.sid.c_str(), w.name.c_str(), w.bin.size());
            want.push_back(std::move(w));
        }
        HeapProbe("etiket toplama sonrasi");
        Line("sds: etiket=%zu", want.size());
        FindSdsLayout(sds, want, secHist);
    }
}

int main(int argc, char** argv)
{
    g_out = stderr;
    if (argc > 1) { FILE* f = fopen(argv[1], "w"); if (f) g_out = f; }
    // "sds" ikinci argümanı: motor taramasını atla, yalnızca $Secure ölçümü.
    // Geçiş ~1 saniye sürüyor; düzen arayışını tarama beklemeden tekrarlamak
    // için kullanılır.
    const bool onlySds = argc > 2 && std::strcmp(argv[2], "sds") == 0;

    // "free"/"free2": GetNamedSecurityInfoW ile dönen SID'i serbest bırakma
    // sırası çöküyor mu? Ayraç: free = dogrudan birak, free2 = ara bir
    // ConvertSidToStringSidA + LocalFree'sinden sonra birak.
    if (argc > 2 && std::strcmp(argv[2], "free") == 0)
    {
        PSID own = nullptr;
        const DWORD gr = GetNamedSecurityInfoW(L"C:\\Windows\\System32\\kernel32.dll",
                                               SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                               &own, nullptr, nullptr, nullptr, nullptr);
        Line("free: aclErr=%lu own=%p gecerli=%d", gr, (void*)own, own ? IsValidSid(own) : 0);
        if (own) LocalFree(own);
        Line("free: birakildi, yigin temiz");
        return 0;
    }
    if (argc > 2 && std::strcmp(argv[2], "free2") == 0)
    {
        PSID own = nullptr;
        const DWORD gr = GetNamedSecurityInfoW(L"C:\\Windows\\System32\\kernel32.dll",
                                               SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                               &own, nullptr, nullptr, nullptr, nullptr);
        Line("free2: aclErr=%lu own=%p", gr, (void*)own);
        const std::string s = SidStr(own);
        Line("free2: sid=%s ad=%s", s.c_str(), SidToName(s).c_str());
        if (own) LocalFree(own);
        Line("free2: birakildi, yigin temiz");
        return 0;
    }

    Line("=== MFT motoru: gercek C: uzerinde olcum (salt okunur) ===");
    Geom g;
    OpenGeom(g);
    if (onlySds)
    {
        MeasureSds(g);
        if (g.vol != INVALID_HANDLE_VALUE) CloseHandle(g.vol);
        return 0;
    }

    disk::Scanner sc;
    // #10 ölçüm kancası: "… <out> <threads 1-8>" — threads dilim sayısı.
    // Yapılandırma satırı çıktıya yazılır ki koşular karışmasın.
    int thr = 1;
    if (argc > 2) { int v = std::atoi(argv[2]); if (v >= 1 && v <= 8) thr = v; }
    sc.SetMftThreads(thr);
    Line("yapilandirma: threads=%d", thr);
    const DWORD t0 = GetTickCount();
    if (!sc.Start(L"C:\\")) { Line("Start reddedildi (mesgul?)"); return 2; }
    sc.Join();
    const DWORD tookMs = GetTickCount() - t0;

    disk::Progress pr;
    sc.GetProgress(pr);
    Line("durum=%d motor=%d sure=%lu ms  (motor: 1 Mft, 2 Walk)",
         (int)pr.state, (int)pr.engine, (unsigned long)tookMs);
    Line("olen: okunan=%llu / toplam=%llu bayt, ogeler=%llu, alloc=%llu",
         (unsigned long long)pr.bytesRead, (unsigned long long)pr.bytesTotal,
         (unsigned long long)pr.items, (unsigned long long)pr.allocBytes);
    if (pr.state != disk::State::Done) { Line("tarama tamamlanmadi"); return 3; }

    uint64_t fileBytes = 0, allocBytes = 0, files = 0, dirs = 0;
    uint64_t absurd = 0, maxAlloc = 0, zeroAllocNonEmpty = 0, zeroSize = 0;
    uint64_t allocLtReal = 0, orphanFlags = 0, reachable = 0, sizeAbsurd = 0;
    size_t   nodeCount = 0;
    std::vector<int> biggest;
    {
        sc.Lock();
        const auto& nodes = sc.Nodes();
        nodeCount = nodes.size();
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const disk::Node& n = nodes[i];
            if (n.orphan) ++orphanFlags;
            if (n.dir) { ++dirs; continue; }
            ++files;
            fileBytes  += n.size;
            allocBytes += n.alloc;
            if (n.alloc > (1ull << 40)) ++absurd;
            if (n.size > (1ull << 40)) ++sizeAbsurd;
            if (n.alloc > maxAlloc) maxAlloc = n.alloc;
            if (n.size > 0 && n.alloc == 0) ++zeroAllocNonEmpty;
            if (n.size == 0) ++zeroSize;
            if (n.size > 0 && n.alloc < n.size) ++allocLtReal;
        }
        // 1 TB ustesi kabul edilemezlerin en buyuk 4 u.
        for (size_t i = 0; i < nodes.size() && biggest.size() < 4; ++i)
            if (!nodes[i].dir && nodes[i].alloc > (1ull << 40)) biggest.push_back((int)i);
        // ulasilabilirlik: kokten child/sib zinciriyle gorulen dugum sayisi.
        std::vector<int> stack;
        if (sc.Root() >= 0) stack.push_back(sc.Root());
        while (!stack.empty())
        {
            const int cur = stack.back(); stack.pop_back();
            ++reachable;
            if (cur < 0 || cur >= (int)nodes.size()) continue;
            for (int c = nodes[cur].firstChild; c >= 0 && c < (int)nodes.size(); c = nodes[c].nextSib)
                stack.push_back(c);
        }
        sc.Unlock();
    }
    Line("dugum=%zu dosya=%llu klasor=%llu", nodeCount, (unsigned long long)files, (unsigned long long)dirs);
    Line("kokten ulasilabilir=%llu / dugum=%zu   orphan-bayrakli=%llu",
         (unsigned long long)reachable, nodeCount, (unsigned long long)orphanFlags);
    Line("dosya toplami: mantiksal=%llu tahsis=%llu", (unsigned long long)fileBytes, (unsigned long long)allocBytes);
    Line("en buyuk tahsis=%llu  1TB-ustesi: tahsis=%llu boyut=%llu  boyutu0=%llu  tahsisi0-ama-dolgu=%llu  tahsis<boyut=%llu",
         (unsigned long long)maxAlloc, (unsigned long long)absurd, (unsigned long long)sizeAbsurd,
         (unsigned long long)zeroSize, (unsigned long long)zeroAllocNonEmpty,
         (unsigned long long)allocLtReal);

    ULARGE_INTEGER total{}, freec{}, avail{};
    if (GetDiskFreeSpaceExW(L"C:\\", &avail, &total, &freec))
        Line("bolim: toplam=%llu bos=%llu kullanilan=%llu",
             (unsigned long long)total.QuadPart, (unsigned long long)freec.QuadPart,
             (unsigned long long)(total.QuadPart - freec.QuadPart));

    // En buyuk 10 dosya: yol + Win32 karsilastirmasi. Agacin buyukluk sirasi ve
    // tahsis alanlarinin saglamligi birlikte dogrulanir.
    struct Big { uint64_t alloc, size; int idx; };
    std::vector<Big> ranked;
    {
        sc.Lock();
        const auto& nodes = sc.Nodes();
        ranked.reserve(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i)
            if (!nodes[i].dir) ranked.push_back({ nodes[i].alloc, nodes[i].size, (int)i });
        sc.Unlock();
    }
    const size_t topN = ranked.size() < 10 ? ranked.size() : 10;
    std::partial_sort(ranked.begin(), ranked.begin() + topN, ranked.end(),
                      [](const Big& a, const Big& b) { return a.alloc > b.alloc; });
    Line("en buyuk 10 dosya (tahsis sirasiyla):");
    for (size_t r = 0; r < ranked.size() && r < 10; ++r)
    {
        std::wstring p;
        if (!sc.BuildPath(ranked[r].idx, p) || p.empty())
        { Line("   (yol kurulamadi) idx=%d", ranked[r].idx); continue; }
        uint64_t wr = 0, wa = 0;
        Win32Sizes(p.c_str(), wr, wa);
        Line("   mft.alloc=%llu mft.size=%llu | win32 real=%llu comp=%llu  acilabildi=%s  %ls",
             (unsigned long long)ranked[r].alloc, (unsigned long long)ranked[r].size,
             (unsigned long long)wr, (unsigned long long)wa,
             (wr == (uint64_t)-1) ? "HAYIR" : "EVET", p.c_str());
    }

    // Bilinen dosyalar: MFT boyutu ile Win32 boyutu birebir ayni olmali ve
    // System32 yollari agacta bulunmali (hard-link bag secimi regresyon bekcesi).
    const char* pKernel[] = { "windows", "system32", "kernel32.dll" };
    const char* pNtdll[]  = { "windows", "system32", "ntdll.dll" };
    const char* pExp[]    = { "windows", "explorer.exe" };
    const char* pHost[]   = { "windows", "system32", "drivers", "etc", "hosts" };
    struct { const char* const* parts; size_t n; const wchar_t* w; } cases[] = {
        { pKernel, 3, L"C:\\Windows\\System32\\kernel32.dll" },
        { pNtdll,  3, L"C:\\Windows\\System32\\ntdll.dll" },
        { pExp,    2, L"C:\\Windows\\explorer.exe" },
        { pHost,   5, L"C:\\Windows\\System32\\drivers\\etc\\hosts" },
    };
    for (auto& cs : cases)
    {
        sc.Lock();
        int idx = Descend(sc, cs.parts, cs.n);
        disk::Node n{};
        std::string name;
        if (idx >= 0) { n = sc.Nodes()[idx]; name = sc.NodeNameLocked(idx); }
        sc.Unlock();
        uint64_t wr = 0, wa = 0;
        Win32Sizes(cs.w, wr, wa);
        if (idx < 0) { Line("eksik dugum: %ls  (win32 real=%llu)", cs.w, (unsigned long long)wr); continue; }
        Line("%s  mft.size=%llu mft.alloc=%llu | win32 real=%llu compressed=%llu  %s",
             name.c_str(), (unsigned long long)n.size, (unsigned long long)n.alloc,
             (unsigned long long)wr, (unsigned long long)wa,
             (n.size == wr) ? "TAM" : "FARKLI");
    }

    // Orneklem dogrulamasi: her 400. dosya dugumu Win32 ile karsilastirilir.
    // Boyut zincirinin (DATA -> FN konsensusu -> alt 48 bit maskesi) tum
    // hacim uzerindeki dogruluk orani tahminle degil, sayimla olculur. Ayni
    // ornekte bayraklar ve degisim zamani da dogrulanir.
    std::vector<std::wstring> zeroDumps;
    std::vector<std::wstring> flagDumps;   // Win32'in maskesine ayki duen dugumler
    {
        struct Sm { int idx; uint64_t size; uint32_t mtime; uint16_t flags; uint32_t owner; };
        std::vector<Sm> sample;
        sc.Lock();
        const auto& nodes = sc.Nodes();
        uint64_t t[8] = {0,0,0,0,0,0,0,0};
        const uint16_t bits[8] = { disk::kFlagCompressed, disk::kFlagSparse,
                                   disk::kFlagEncrypted,  disk::kFlagReparse,
                                   disk::kFlagOffline,    disk::kFlagAds,
                                   disk::kFlagHardLink,   disk::kFlagSizeUnknown };
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const disk::Node& n = nodes[i];
            if (n.dir) continue;
            for (int b = 0; b < 8; ++b) if (n.flags & bits[b]) ++t[b];
            if (i % 400 == 0) sample.push_back({ (int)i, n.size, n.mtime, n.flags, n.owner });
        }
        sc.Unlock();
        Line("bayrak sayimi: sikisik=%llu seyrek=%llu sifreli=%llu reparse=%llu "
             "cevrimdisi=%llu ADS=%llu hardlink=%llu boyutOlculemedi=%llu "
             "(motor sayaci=%llu)",
             (unsigned long long)t[0], (unsigned long long)t[1], (unsigned long long)t[2],
             (unsigned long long)t[3], (unsigned long long)t[4], (unsigned long long)t[5],
             (unsigned long long)t[6], (unsigned long long)t[7],
             (unsigned long long)sc.UnknownSizeFiles());
        // #9 ölçümü: düğüm.owner artık $SDS sözlüğünün 1-bazlı indeksi. Tablonun
        // büyüklüğü (bellek maliyeti), kapsamı ve en yaygın sahipler ölçülür.
        {
            sc.Lock();
            const auto& nds = sc.Nodes();
            const auto& ow  = sc.Owners();
            std::map<uint32_t, uint64_t> oc;
            uint64_t noOwner = 0, oobIdx = 0;
            for (size_t i = 0; i < nds.size(); ++i)
            {
                if (nds[i].dir) continue;
                if (!nds[i].owner) { ++noOwner; continue; }
                if (nds[i].owner > ow.size()) { ++oobIdx; continue; }
                ++oc[nds[i].owner];
            }
            size_t named = 0, attempted = 0;
            for (const auto& o : ow) { if (!o.name.empty()) ++named; if (o.attempted) ++attempted; }
            sc.Unlock();
            Line("sahip: tablo=%zu (adli=%zu denenen=%zu) benzersiz-kullanim=%zu "
                 "sahipsiz=%llu tablo-disi-indeks=%llu",
                 ow.size(), named, attempted, oc.size(),
                 (unsigned long long)noOwner, (unsigned long long)oobIdx);
            std::vector<std::pair<uint64_t, uint32_t>> top;
            for (auto& kv : oc) top.push_back({ kv.second, kv.first });
            std::partial_sort(top.begin(), top.begin() + (top.size() < 8 ? top.size() : 8),
                              top.end(), [](const std::pair<uint64_t,uint32_t>& a,
                                            const std::pair<uint64_t,uint32_t>& b)
                              { return a.first > b.first; });
            sc.Lock();
            const auto& own = sc.Owners();
            for (size_t i = 0; i < top.size() && i < 8; ++i)
            {
                const disk::Owner& o = own[top[i].second - 1];
                Line("   idx=%u secId=%u dosya=%llu sid=%s ad=%s",
                     top[i].second, o.secId, (unsigned long long)top[i].first,
                     o.sid.c_str(), o.name.empty() ? "-" : o.name.c_str());
            }
            sc.Unlock();
        }
        long ok = 0, diff = 0, gone = 0, flagDiff = 0, timeDiff = 0, timeZero = 0;
        long flagLayer = 0, flagOther = 0, flagMftExtra = 0, flagWin32Extra = 0;
        long kaynakOku = 0, fnUydu = 0, siUydu = 0, kaynakFark = 0, siKazandi = 0, fnKazandi = 0;
        long ownOk = 0, ownMatch = 0, ownWinFail = 0, ownSahipsiz = 0, ownDump = 0;
        long stResolved = 0, stPending = 0, stFailed = 0, stNone = 0;
        std::vector<int> ownWatch;      // tembel cozumu izlenen dugumler
        for (auto& s : sample)
        {
            std::wstring p;
            if (!sc.BuildPath(s.idx, p) || p.empty()) { ++gone; continue; }
            uint64_t wr = 0, wa = 0;
            Win32Sizes(p.c_str(), wr, wa);
            if (wr == (uint64_t)-1) { ++gone; continue; }      // tarama sonrasi degismis
            DWORD wAttr = 0; uint32_t wTime = 0;
            if (Win32AttrTime(p.c_str(), wAttr, wTime))
            {
                // ADS ve hardlink Win32 bulamasindan sorulmaz: yalnizca MFT
                // tarafi bilir. Karsilastirma sikisik/seyrek/sifre/reparse/
                // cevrimdisi bitleriyle sinirli tutulur.
                const uint16_t mask = disk::kFlagCompressed | disk::kFlagSparse |
                                      disk::kFlagEncrypted  | disk::kFlagReparse  |
                                      disk::kFlagOffline;
                const uint16_t want = disk::NodeFlags(wAttr, 0, 1, false) & mask;
                // Kaynagi ayrica olc: ayni maske $FN'den ve $SI'den okununca
                // Win32'e hangisi daha cok uyuyor? Motorun secimi bu sayimla
                // dogrulanir, tahminle degil.
                uint32_t siM = 0, fnM = 0;
                if (AttrMasksForPath(g, p, siM, fnM))
                {
                    const uint16_t aFn = disk::NodeFlags(fnM, 0, 1, false) & mask;
                    const uint16_t aSi = disk::NodeFlags(siM, 0, 1, false) & mask;
                    ++kaynakOku;
                    if (aFn == want) ++fnUydu;
                    if (aSi == want) ++siUydu;
                    if (aFn != aSi)  ++kaynakFark;
                    if (aFn != want && aSi == want) ++siKazandi;
                    if (aSi != want && aFn == want) ++fnKazandi;
                }
                if ((s.flags & mask) != want)
                {
                    if (flagDiff < 8)
                        Line("   BAYRAK mft=0x%X win32=0x%X attr=0x%X  %ls",
                             s.flags, want, wAttr, p.c_str());
                    // Containers katmanı dosyaları ayrı bir vaka: katman sürücüsü
                    // yol üzerine sentetik bir Win32 maskesi koyuyor. Uyumsuzluğun
                    // kaynağını sayımla ayırmak için katman/katman-dışı ve yön
                    // (fazla bit MFT'te mi Win32'de mi) ayrı sayılır.
                    const bool layer = p.find(L"Containers") != std::wstring::npos;
                    if (layer) ++flagLayer; else ++flagOther;
                    if ((s.flags & mask) & ~want) ++flagMftExtra;
                    if (want & ~(s.flags & mask)) ++flagWin32Extra;
                    if ((!layer && flagDumps.size() < 3) || flagDumps.size() < 5)
                        flagDumps.push_back(p);
                    ++flagDiff;
                }
                if (!s.mtime) ++timeZero;
                else if (s.mtime != wTime)
                {
                    if (timeDiff < 8)
                        Line("   ZAMAN mft=%u win32=%u  %ls", s.mtime, wTime, p.c_str());
                    ++timeDiff;
                }
            }
            if (s.size == wr) ++ok;
            else { if (diff < 12) Line("   FARKLI mft=%llu win32=%llu  %ls",
                                       (unsigned long long)s.size, (unsigned long long)wr, p.c_str());
                   if (s.size == 0 && wr > 0 && zeroDumps.size() < 3) zeroDumps.push_back(p);
                   ++diff; }

            // #9 sahiplik dogrulamasi: tablo indeksinin HAM SID'i, ayni yolun
            // Win32 sahiplik SID'iyle birebir karsilastirilir (ad cozumu ayrica
            // olculur). Cozum istemeden once tablodan okunur; OwnerOfLocked
            // bekleyenleri arka plan is parcasina havale eder.
            {
                std::string engSid, lbl;
                disk::OwnerState ost = disk::kOwnerNone;
                sc.Lock();
                {
                    const auto& ow = sc.Owners();
                    if (s.owner && s.owner <= ow.size()) engSid = ow[s.owner - 1].sid;
                    ost = sc.OwnerOfLocked(s.idx, lbl);
                }
                sc.Unlock();
                switch (ost)
                {
                case disk::kOwnerNone:     ++stNone;     break;
                case disk::kOwnerResolved: ++stResolved; break;
                case disk::kOwnerFailed:   ++stFailed;   break;
                case disk::kOwnerPending:
                    ++stPending;
                    if (ownWatch.size() < 300) ownWatch.push_back(s.idx);
                    break;
                }
                if (engSid.empty()) { ++ownSahipsiz; }
                else
                {
                    std::string winSid;
                    if (Win32OwnerSid(p.c_str(), winSid))
                    {
                        ++ownOk;
                        if (winSid == engSid) ++ownMatch;
                        else if (ownDump < 5)
                        { ++ownDump;
                          Line("   SAHIP mft=%s win32=%s  %ls", engSid.c_str(), winSid.c_str(), p.c_str()); }
                    }
                    else ++ownWinFail;
                }
            }
        }
        Line("ornekleme: dugum=%zu  eslesen=%ld  farkli=%ld  okunamadi=%ld  "
             "bayrakFarklisi=%ld (katman=%ld diger=%ld mftFazla=%ld win32Fazla=%ld) "
             "zamanFarklisi=%ld zamanYok=%ld",
             sample.size(), ok, diff, gone, flagDiff, flagLayer, flagOther,
             flagMftExtra, flagWin32Extra, timeDiff, timeZero);
        Line("bayrak kaynagi: okunan=%ld  FN-uydu=%ld SI-uydu=%ld kaynakFarki=%ld  "
             "FN-hataliyken-SI-dogru=%ld SI-hataliyken-FN-dogru=%ld",
             kaynakOku, fnUydu, siUydu, kaynakFark, siKazandi, fnKazandi);

        // #9 sahiplik sonucu: SID eslesmesi (tablo dogrulugu) + cozum durumları.
        Line("sahip ornekleme: sid-karsilastirilan=%ld eslesen=%ld win32-okunamadi=%ld "
             "sahipsiz=%ld | durum: cozulmus=%ld bekleyen=%ld cozulemedi=%ld yok=%ld",
             ownOk, ownMatch, ownWinFail, ownSahipsiz, stResolved, stPending, stFailed, stNone);
        if (!ownWatch.empty())
        {
            // Havale edilenler arka planda cozuluyor; kac tanesinin kaldigi ve
            // bekleme suresi olculur (arayuz bunu beklemez, ham SID gosterir).
            const DWORD t0w = GetTickCount();
            long still = 0;
            for (int k = 0; k < 40; ++k)
            {
                still = 0;
                sc.Lock();
                for (int idx : ownWatch) { std::string l; if (sc.OwnerOfLocked(idx, l) == disk::kOwnerPending) ++still; }
                sc.Unlock();
                if (!still) break;
                Sleep(250);
            }
            Line("sahip cozumu: izlenen=%zu bekleyen-kaldi=%ld  %lu ms icinde",
                 ownWatch.size(), still, (unsigned long)(GetTickCount() - t0w));
        }
        {
            long adKarsilan = 0, adUydu = 0;
            sc.Lock();
            const auto& ow = sc.Owners();
            for (size_t i = 0; i < ow.size() && i < 25; ++i)
            {
                if (ow[i].name.empty()) continue;
                ++adKarsilan;
                const std::string w = SidToName(ow[i].sid);
                if (w == ow[i].name) ++adUydu;
                else Line("   AD tablo=%s win32=%s secId=%u", ow[i].name.c_str(), w.c_str(), ow[i].secId);
            }
            sc.Unlock();
            Line("sahip adi: cozulmus-karsilasilan=%ld ayni=%ld", adKarsilan, adUydu);
        }
    }

    // #9 dayanağı: $SDS düzenini ve owner id eşlemesini ölç.
    MeasureSds(g);

    // Ham alan dokumu: kontrol (dogru olculen) + kabul edilemez boyutlular.
    if (g.vol != INVALID_HANDLE_VALUE && g.recSize && !g.runs.empty())
    {
        DumpRecordForPath(g, L"C:\\Windows\\System32\\kernel32.dll");
        DumpRecordForPath(g, L"C:\\Windows\\System32\\drivers\\etc\\hosts");
        // En buyuk ilk 3 dugum: boyutlarinin kaynaktaki ham degerleri gorunsun.
        for (size_t r = 0; r < ranked.size() && r < 3; ++r)
        {
            std::wstring p;
            if (sc.BuildPath(ranked[r].idx, p) && !p.empty())
            { Line("dogum: siradaki buyuk[%zu]", r); DumpRecordForPath(g, p); }
        }
        for (int nodeIdx : biggest)
        {
            std::wstring p;
            if (sc.BuildPath(nodeIdx, p) && !p.empty() && p.find(L':') != std::wstring::npos)
                DumpRecordForPath(g, p);
        }
        // Boyutu 0 okunan ama Win32'in dolu oldugunu soyledigi ornekler: $DATA
        // gercekten uzanti kaydinda mi, yoksa tabanda mi ama baska bir yerdedir?
        for (auto& p : zeroDumps) { Line("dogum: sifir-oku"); DumpRecordForPath(g, p); }
        // Bayrak uyusmazliginin kaynagi: kayitta $FN +0x38 gercekten ne tasiyor?
        for (auto& p : flagDumps) { Line("dogum: bayrak-farki"); DumpRecordForPath(g, p); }
        CloseHandle(g.vol);
    }
    return 0;
}
