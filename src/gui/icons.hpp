#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"

// ImDrawList ile çizilen vektör ikonlar (ikon fontu gerektirmez).
enum class Icon
{
    None,
    Dashboard, Cleaner, Tweaks, Network, Settings,
    Key, User, Logout, Close, Minimize, Check,
    Eye, EyeOff, Cpu, Memory, Disk, Shield, Bolt,
    Info, Warning, Error, Alert, Search, Refresh,
    Monitor, Globe, Lock, Chip,
    Instagram, GitHub, Code,
};

namespace icons
{
    // c = merkez, s = kutu boyutu, th = çizgi kalınlığı (0 = otomatik)
    void Draw(ImDrawList* dl, Icon icon, const ImVec2& c, float s, ImU32 col, float th = 0.0f);
}