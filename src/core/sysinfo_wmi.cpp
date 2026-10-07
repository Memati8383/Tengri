#include "sysinfo_wmi.hpp"
#include "lang.hpp"
#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>

#pragma comment(lib, "wbemuuid.lib")   // CoInitializeSecurity, IWbemLocator

// WMI ile donanım bilgisi.
//
// Neden WMI: RAM modülünün yuvası/markası/hızı yalnızca SMBIOS tip 17'de,
// monitörün EDID'si ve TPM'nin durumu ise registry'de hiç bulunmuyor. Windows'un
// kendi ayrıştırıcısı bu tabloları zaten okuyor; elle SMBIOS çözümlemek yerine
// ona sormak hem daha kısa hem de üretici sapmalarında daha doğru.
//
// Dikkat: WMI sorguları yüzlerce milisaniye sürebilir ve ilk çağrıda hizmet
// başlatır. Bu yüzden tek seferlik doldurma arka planda çalışır; arayüz
// Get() ile boş da olsa anlık değer okur ve sonradan dolar. Alanlar yazılırken
// bir tutamç alınır, yarım okunmuş bir yapı görülmez.

namespace syswmi
{
    namespace
    {
        std::mutex      g_mutex;
        Result          g_result;
        std::atomic<bool> g_running{ false };
        std::thread     g_thread;

        // Firmware'in gerçek değeri yazmadığı durumda WMI'nin döndürdüğü
        // yer tutucular. Bunları olduğu gibi göstermek "seri numarası var"
        // yanılgısı yaratır; oysa değer yoktur.
        bool IsPlaceholder(const std::wstring& v)
        {
            if (v.empty()) return true;
            static const wchar_t* kBad[] = {
                L"Default string", L"To Be Filled By O.E.M.", L"To be filled by O.E.M.",
                L"Not Specified", L"NotApplicable", L"None", L"N/A", L"unknown",
                L"Unknown", L"Base Board Serial Number", L"System Serial Number",
                L"OEM", L"0", L"0x0",
            };
            for (const wchar_t* b : kBad)
            {
                if (::_wcsicmp(v.c_str(), b) == 0)
                    return true;
            }
            // Tamamı sıfır olan bir seri de yer tutucudur.
            bool allZero = true;
            for (const wchar_t c : v)
            {
                if (c != L'0' && c != L'-' && c != L'_' && c != L' ' && c != L'\0')
                {
                    allZero = false;
                    break;
                }
            }
            return allZero;
        }

        std::string Clean(const std::wstring& v)
        {
            if (IsPlaceholder(v)) return {};
            std::wstring t = v;
            while (!t.empty() && (t.back() == L' ' || t.back() == L'.'))
                t.pop_back();
            std::string s;
            const int n = WideCharToMultiByte(CP_UTF8, 0, t.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (n > 1)
            {
                s.resize(static_cast<size_t>(n - 1));
                WideCharToMultiByte(CP_UTF8, 0, t.c_str(), -1, s.data(), n, nullptr, nullptr);
            }
            return s;
        }

        std::wstring PropStr(IWbemClassObject* o, const wchar_t* name)
        {
            VARIANT v;
            VariantInit(&v);
            std::wstring out;
            if (SUCCEEDED(o->Get(name, 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR && v.bstrVal)
                out = v.bstrVal;
            VariantClear(&v);
            return out;
        }

        long PropNum(IWbemClassObject* o, const wchar_t* name, long fallback = 0)
        {
            VARIANT v;
            VariantInit(&v);
            long out = fallback;
            if (SUCCEEDED(o->Get(name, 0, &v, nullptr, nullptr)))
            {
                switch (v.vt)
                {
                case VT_I4:   out = v.lVal;  break;
                case VT_UI4:  out = (long)v.ulVal; break;
                case VT_I2:   out = v.iVal;  break;
                case VT_UI2:  out = v.uiVal; break;
                case VT_I8:   out = (long)v.llVal; break;
                case VT_UI8:  out = (long)v.ullVal; break;
                case VT_BSTR: if (v.bstrVal) out = _wtoi(v.bstrVal); break;
                default: break;
                }
            }
            VariantClear(&v);
            return out;
        }

        // Genel sorgu yardımcısı: tek sınıftan tüm örnekleri toplar.
        IEnumWbemClassObject* Query(IWbemServices* svc, const wchar_t* wql)
        {
            IEnumWbemClassObject* en = nullptr;
            if (FAILED(svc->ExecQuery(_bstr_t("WQL"), _bstr_t(wql),
                                      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                      nullptr, &en)))
                return nullptr;
            return en;
        }

        void QueryRam(IWbemServices* svc, Result& out)
        {
            IEnumWbemClassObject* en = Query(svc,
                L"SELECT DeviceLocator, BankLabel, SMBIOSMemoryType, Capacity, "
                L"Speed, ConfiguredClockSpeed, Manufacturer, PartNumber, SerialNumber "
                L"FROM Win32_PhysicalMemory");
            if (!en) return;

            IWbemClassObject* obj = nullptr;
            ULONG got = 0;
            while (en->Next(1000, 1, &obj, &got) == S_OK && obj)
            {
                RamModule mod;
                mod.slot       = Clean(PropStr(obj, L"DeviceLocator"));
                mod.channel    = Clean(PropStr(obj, L"BankLabel"));
                mod.partNumber = Clean(PropStr(obj, L"PartNumber"));
                mod.serial     = Clean(PropStr(obj, L"SerialNumber"));

                // SMBIOSMemoryType: 26 = DDR4, 34 = DDR5, 24 = DDR3, 20 = DDR2
                switch (PropNum(obj, L"SMBIOSMemoryType", 0))
                {
                case 34: mod.type = "DDR5"; break;
                case 26: mod.type = "DDR4"; break;
                case 24: mod.type = "DDR3"; break;
                case 20: mod.type = "DDR2"; break;
                default: break;
                }

                const long capMB = PropNum(obj, L"Capacity", 0);
                if (capMB > 0)
                {
                    char buf[32];
                    if (capMB >= 1024) snprintf(buf, sizeof(buf), "%.0f GB", capMB / 1024.0);
                    else                 snprintf(buf, sizeof(buf), "%ld MB", capMB);
                    mod.capacity = buf;
                }

                // ConfiguredClockSpeed calisan hiz, Speed ise JEDEC taban hiz.
                const long cfg = PropNum(obj, L"ConfiguredClockSpeed", 0);
                const long jed = PropNum(obj, L"Speed", 0);
                if (cfg > 0)
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%ld MHz", cfg);
                    mod.speed = buf;
                }
                // Ayniysa ikinci deger tekrar edilmez.
                if (jed > 0 && jed != cfg)
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%ld MHz", jed);
                    mod.jedecSpeed = buf;
                }

                if (!mod.capacity.empty())
                    out.ram.push_back(mod);

                obj->Release();
                obj = nullptr;
            }

            // Özet satır: modüller hep aynı hızda çalışıyorsa tek değer.
            if (!out.ram.empty())
            {
                const std::string s = out.ram.front().speed;
                bool same = true;
                for (const RamModule& m : out.ram)
                    if (m.speed != s) { same = false; break; }
                if (!s.empty())
                {
                    if (same || out.ram.size() == 1)
                    {
                        out.ramSpeed = s;
                        if (!out.ram.front().jedecSpeed.empty())
                            out.ramSpeed += " (JEDEC " + out.ram.front().jedecSpeed + ")";
                    }
                    else
                    {
                        out.ramSpeed = s + " / " + std::to_string(out.ram.size()) + " modül";
                    }
                }
                out.ramSlots = std::to_string(out.ram.size()) + " dolu";
            }
        }

        void QueryMonitor(IWbemServices* svc, Result& out)
        {
            // WmiMonitorID root\WMI'de yer alir; varsayilan root\CIMV2'de DEGIL.
            // Yanlis ad alani sorgulanirsa sinif sessizce hic satir dondurmez ve
            // monitor satiri hic cizilmez. Olcülerek duzeltildi.
            IEnumWbemClassObject* en = Query(svc,
                L"SELECT ManufacturerName, UserFriendlyName, SerialNumberID "
                L"FROM WmiMonitorID IN \"\\\\.\\root\\WMI\"");
            if (!en) return;

            IWbemClassObject* obj = nullptr;
            ULONG got = 0;
            while (en->Next(1000, 1, &obj, &got) == S_OK && obj)
            {
                VARIANT v;
                VariantInit(&v);

                // Dizi bicimi olcülerek DOGRULANDI: ilk eleman UZUNLUK DEGIL,
                // ilk karakterin kendisidir ("MAG 244F" -> 77,65,71,32,50,52,52,70).
                // Belgelenen "ilk eleman uzunluktur" varsayimi bu makinede gecerli
                // degil. NUL'a kadar okumak iki bicimde de dogru sonuc verir.
                auto fromUsart = [&](const wchar_t* prop, std::string* dst) {
                    if (!dst->empty()) return;
                    VariantClear(&v);
                    VariantInit(&v);
                    if (FAILED(obj->Get(prop, 0, &v, nullptr, nullptr))) return;
                    if (v.vt != (VT_ARRAY | VT_UI2) || !v.parray) return;

                    const USHORT* p = (const USHORT*)v.parray->pvData;
                    const ULONG   n = v.parray->rgsabound[0].cElements;
                    if (!p || n == 0) return;

                    std::wstring w;
                    for (ULONG i = 0; i < n; ++i)
                    {
                        const USHORT c = p[i];
                        if (c == 0) break;                 // NUL = dizi sonu
                        if (c < 0x20 || c > 0x7E) break;   // baska kodlama: dur
                        w.push_back((wchar_t)c);
                    }
                    *dst = Clean(w);
                };

                std::string vendor, model;
                fromUsart(L"ManufacturerName", &vendor);
                fromUsart(L"UserFriendlyName", &model);

                if (!vendor.empty()) out.monitorVendor = vendor;
                // Birden fazla monitor varsa ilki yeterli; hepsini gostermek
                // bu satirda okunmuyabilir olurdu.
                if (out.monitorName.empty() && !model.empty())
                    out.monitorName = model;

                obj->Release();
                obj = nullptr;
            }
            en->Release();
        }

        void QueryTpm(IWbemServices* svc, Result& out)
        {
            // Win32_Tpm yalnizca makinede WMI'den gorunur bir TPM varsa satir
            // dondurur. Donmediyse TPM yoktur demektir; panel o satiri hic cizmez.
            // (Olcum: bu makinede her iki ad alaninda da 0 satir donuyor, o yuzden
            //  panelde TPM satiri gorunmemesi beklenen davranistir.)
            IEnumWbemClassObject* en = Query(svc,
                L"SELECT IsEnabled_InitialValue, IsActivated_InitialValue, SpecVersion "
                L"FROM Win32_Tpm");
            if (!en) return;

            IWbemClassObject* obj = nullptr;
            ULONG got = 0;
            if (en->Next(1000, 1, &obj, &got) == S_OK && obj)
            {
                out.tpmPresent = "Yes";
                VARIANT v;
                VariantInit(&v);
                // IsActivated_InitialValue: true = etkin
                if (SUCCEEDED(obj->Get(L"IsActivated_InitialValue", 0, &v, nullptr, nullptr)) &&
                    v.vt == VT_BOOL)
                    out.tpmReady = v.boolVal ? "Yes" : "No";
                VariantClear(&v);

                const long sv = PropNum(obj, L"SpecVersion", 0);
                if (sv > 0) out.tpmSpec = std::to_string(sv) + ".0";

                obj->Release();
            }
            en->Release();
        }

        void GatherAll()
        {
            Result local;
            local.ok = true;

            IWbemLocator*  loc = nullptr;
            IWbemServices* svc = nullptr;

            HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const bool weStartedCom = SUCCEEDED(hrCo);

            hrCo = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                                       RPC_C_AUTHN_LEVEL_DEFAULT,
                                       RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
            // RPC_E_TOO_LATE yalnızca "zaten yapıldı" demektir, hata değildir.

            CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&loc));
            if (loc && SUCCEEDED(loc->ConnectServer(_bstr_t("ROOT\\CIMV2"), nullptr, nullptr, nullptr,
                                             0, nullptr, nullptr, &svc)))
            {
                // CoSetProxyBlanket: varsayılan olarak yerel olarak çalışan bir
                // hizmete bağlanırken DCOM kimlik doğrulaması gerekebilir ve
                // makine hesabı yetkisi olmayan ortamlarda bağlantı düşer.
                CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                                  RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                                  nullptr, EOAC_NONE);
                QueryRam(svc, local);
                QueryMonitor(svc, local);
                QueryTpm(svc, local);
            }
            else
            {
                local.ok = false;
            }

            if (svc)  svc->Release();
            if (loc)  loc->Release();
            if (weStartedCom) CoUninitialize();

            std::lock_guard<std::mutex> lock(g_mutex);
            g_result = std::move(local);
        }

        void Worker()
        {
            // WMI'nin ilk bağlantısı yavaştır; bu yüzden yarım kalan bir sorguya
            // karşı tekrar deneme yapılır, ama sonsuza kadar değil.
            for (int attempt = 0; attempt < 3 && g_running.load(); ++attempt)
            {
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    if (g_result.ok || !g_result.ram.empty()) break;
                }
                GatherAll();
                if (g_result.ok) break;
                Sleep(400 * (attempt + 1));
            }
            g_running.store(false);
        }
    }

    void StartAsync()
    {
        if (g_running.exchange(true)) return;
        g_thread = std::thread(Worker);
    }

    void Shutdown()
    {
        g_running.store(false);
        if (g_thread.joinable())
            g_thread.join();
    }

    const Result& Get()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_result;
    }
}
