#pragma once
#include <string>

// 1.1.1.1 adresine gerçek ICMP tur-çevrim ölçümü. Ayrılmış bir iş parçacığında
// çalışır ve arayüz iş parçacığından yoklanır, dolayısıyla hiçbir kareyi bloklamaz;
// ilk örnek düşene kadar false döner.
namespace network
{
    // İki örnek arasındaki hedef süre. Sondaç bu değere göre uyur, grafik de
    // kaymasını bu değere göre animasyonlar; ikisi ayrı ayrı yazılırsa grafik
    // örnekten daha sık ya da daha seyrek kayar. İkisi de buradan okur.
    constexpr double kLatencyIntervalSec = 2.0;

    // Bu kadar süredir örnek gelmezse sondaj ölmüş sayılır. Arayüz donmuş bir
    // değeri göstermeye devam etmek, ölçtüğünü iddia etmekten daha kötüdür.
    constexpr double kLatencyStaleSec = 12.0;

    void StartLatencyProbe();
    void StopLatencyProbe();
    bool PollLatency(float* ms);

    // "Başlangıçta çalıştır" için HKCU\...\Run girdisi. Yazılan hâlini geri okuyup onu
    // döndürür, böylece anahtar registry'den ayrı düşemez.
    bool SetRunAtStartup(const wchar_t* exe_path, bool enable);
    bool GetRunAtStartup(const wchar_t* exe_path);

    bool FlushDns();
    // 0 = Otomatik, ardından Cloudflare / Google / Quad9
    bool SetDns(int provider);

    // Her anahtar buradan okunabilir ya da doğrulanabilir değildir; okunamayanlar
    // sessizce hiçbir işe yaramak yerine arayüzde gri gösterilir.
    bool IsSupported(int idx);
    bool ReadTweak(int idx);
    bool ApplyTweak(int idx, bool on);

    std::string AdapterName();
    std::string LocalIP();
    std::string GatewayIP();
}