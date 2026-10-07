#pragma once
#include <string>
#include <vector>

// WMI ile okunan alanlar.
//
// Neden registry değil: bazı bilgiler registry'de hiç yansımaz. RAM modülünün
// yuvası (DIMM konumu), markası ve hızı yalnızca SMBIOS tip 17 tablosundadır;
// monitörün EDID'si ve TPM'nin durumu da registry'de tutulmaz. Önceden bu
// alanlar "okunamıyor" diye hiç gösterilmiyordu; WMI aynı veriyi Windows'un
// kendi ayrıştırıcısı üzerinden verir.
//
// Sorgular bir kez, Gather() sırasında arka planda bir kez çalıştırılır.
namespace syswmi
{
    struct RamModule
    {
        std::string slot;       // "DIMM_A1", "DIMM 0"
        std::string channel;    // "A", "B" — yalnızca kanal etiketi varsa
        std::string type;       // DDR4 / DDR5
        std::string capacity;   // "16 GB"
        std::string speed;      // "6000 MHz" — çalışan hız
        std::string jedecSpeed; // "4800 MHz" — JEDEC taban hızı
        std::string partNumber; // "6000 Series"
        std::string serial;
    };

    struct Result
    {
        bool        ok = false;          // sorgular çalıştı mı
        std::string error;                // çalışmadıysa neden

        std::vector<RamModule> ram;
        std::string ramSpeed;             // tek satırlık özet, ör. "6000 MHz (JEDEC 4800 MHz)"
        std::string ramSlots;             // dolu yuva / toplam, ör. "2 / 4"

        std::string monitorName;          // "MAG 244F"
        std::string monitorVendor;        // "MSI"

        std::string tpmPresent;           // "Var" / "Yok" — dilde çözülür
        std::string tpmReady;             // "Hazır" / "Hazır değil"
        std::string tpmSpec;              // "2.0"
    };

    // Arka planda tek seferlik doldurma. Uygulama açılışında bir kez çağrılır;
    // Get() sonucu beklemez, sorgu bitene kadar alanlar boş kalır.
    void StartAsync();
    void Shutdown();

    const Result& Get();
}