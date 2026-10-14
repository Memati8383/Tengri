#include "tweakinfo.hpp"
#include "regtext.hpp"
#include "tweak_keys.hpp"

#include <cstring>

namespace tweakinfo
{
    int Count(int cat, int idx)
    {
        int n = 0;
        for (int i = 0; i < tweakkeys::kCount; ++i)
            if (tweakkeys::kEntries[i].cat == cat && tweakkeys::kEntries[i].idx == idx)
                ++n;
        return n;
    }

    bool Get(int cat, int idx, int i, Item* out)
    {
        if (!out || i < 0) return false;
        int seen = 0;
        for (int e = 0; e < tweakkeys::kCount; ++e)
        {
            const tweakkeys::Entry& k = tweakkeys::kEntries[e];
            if (k.cat != cat || k.idx != idx) continue;
            if (seen++ != i) continue;
            out->key        = k.path;
            out->valueNames = k.values;
            return true;
        }
        return false;
    }

    int OwnersOf(const wchar_t* expandedKey, const wchar_t* name,
                 int* cats, int* idxs, int max)
    {
        if (!expandedKey || !name) return 0;
        int found = 0;
        for (int e = 0; e < tweakkeys::kCount; ++e)
        {
            const tweakkeys::Entry& k = tweakkeys::kEntries[e];
            const std::wstring full = regtext::ExpandKey(k.path);
            if (_wcsicmp(full.c_str(), expandedKey) != 0) continue;

            // Deger adi büyük/küçük harf duyarlidir: yedek kapisi da ayni
            // karsilastirmayi kullanir, ikisi farkli davranirsa bir dosya
            // arayuzde "bilinen" görünup geri alimda reddedilirdi.
            for (const wchar_t* const* v = k.values; v && *v; ++v)
            {
                if (wcscmp(name, *v) != 0) continue;
                if (cats && idxs && found < max) { cats[found] = k.cat; idxs[found] = k.idx; }
                ++found;
                break;
            }
        }
        return found;
    }

    int EntryCount() { return tweakkeys::kCount; }

    void AllAllowed(std::vector<regtext::Allowed>* out)
    {
        if (!out) return;
        out->clear();
        for (int e = 0; e < tweakkeys::kCount; ++e)
        {
            const tweakkeys::Entry& k = tweakkeys::kEntries[e];
            for (const wchar_t* const* v = k.values; v && *v; ++v)
                out->push_back({ k.path, *v });
        }
    }
}
