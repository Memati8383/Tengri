#pragma once

// SvcHostSplitThresholdInKB ayar şablonları.
// Eşiği kurulu RAM miktarına çıkarmak, Windows'un servisleri her servis için ayrı
// sürece bölmek yerine daha az sayıda svchost.exe altında toplamasını sağlar.
namespace ram
{
    int         PresetCount();
    int         PresetGB(int index);
    const char* PresetName(int index);

    // Kurulu RAM miktarı, GB cinsinden. Karşılaştırma yapılabilsin diye tam sayıya yuvarlanır.
    int DetectedGB();

    // Registry'de o an yazılı olan şablonun indeksi; eşleşen yoksa -1.
    int CurrentPreset();

    // Seçilen şablonu uygular; indeks -1 ise Windows varsayılanına döner.
    bool Apply(int index);
}