# Drives the TENGRI window with synthetic mouse input and captures the screen.
#
# Used to produce the screenshots in docs/screenshots. Input goes to the window with
# SetCursorPos + mouse_event rather than PostMessage, because ImGui reads the real cursor
# position each frame; a posted WM_MOUSEMOVE with a coordinate does move ImGui's state but
# the surrounding hit-testing in a custom-drawn UI is far more reliable with the actual
# pointer. Every click is expressed as an offset from the client area so the same script
# works regardless of where the window is placed or what DPI it is on.
param(
  [string]$Exe = "build\TENGRI_shot.exe"
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

Add-Type -TypeDefinition @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;

public static class Driver
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string lpClassName, string lpWindowName);

    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int max);
    delegate bool EnumWindowsProc(IntPtr h, IntPtr lParam);

    // The island window is a WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT popup.
    // FindWindowW skips it (and a higher-IL window is invisible to a medium-IL caller
    // anyway), so the only reliable lookup is to enumerate every top-level window, keep
    // the ones owned by the target process, and match the class suffix. GetClassName is a
    // read, not a message, so UIPI does not block it across integrity levels.
    public static IntPtr FindWindowByClassSuffix(uint targetPid, string suffix)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) =>
        {
            uint pid;
            GetWindowThreadProcessId(h, out pid);
            if (pid != targetPid) return true;
            var sb = new System.Text.StringBuilder(256);
            GetClassName(h, sb, sb.Capacity);
            if (sb.ToString().EndsWith(suffix)) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern IntPtr SetActiveWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);

    [StructLayout(LayoutKind.Sequential)]
    public struct INPUT { public uint type; public InputUnion U; }

    [StructLayout(LayoutKind.Explicit)]
    public struct InputUnion
    {
        [FieldOffset(0)] public MOUSEINPUT mi;
        [FieldOffset(0)] public KEYBDINPUT ki;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MOUSEINPUT
    {
        public int dx, dy;
        public uint mouseData, dwFlags, time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; }

    const uint INPUT_MOUSE = 0, INPUT_KEYBOARD = 1;
    const uint MOUSEEVENTF_MOVE = 0x0001, MOUSEEVENTF_LEFTDOWN = 0x0002, MOUSEEVENTF_LEFTUP = 0x0004;
    const uint MOUSEEVENTF_ABSOLUTE = 0x8000, MOUSEEVENTF_VIRTUALDESK = 0x4000;
    const uint KEYEVENTF_UNICODE = 0x0004, KEYEVENTF_KEYUP = 0x0002;

    // ImGui's text input reads WM_CHAR, which a SendInput KEYEVENTF_UNICODE pair
    // produces without needing a hardware scan code. The field also carries
    // CharsUppercase, so lowercase in the string is fine.
    public static void TypeText(string s)
    {
        foreach (char c in s)
        {
            INPUT[] pair = new INPUT[2];
            pair[0].type = INPUT_KEYBOARD;
            pair[0].U.ki = new KEYBDINPUT();
            pair[0].U.ki.wScan = (ushort)c;
            pair[0].U.ki.dwFlags = KEYEVENTF_UNICODE;
            pair[1].type = INPUT_KEYBOARD;
            pair[1].U.ki = new KEYBDINPUT();
            pair[1].U.ki.wScan = (ushort)c;
            pair[1].U.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            Send(pair);
            System.Threading.Thread.Sleep(35);
        }
    }

    // SetForegroundWindow is refused outright when the calling process is not the
    // foreground process. The documented way around it is to attach this thread's input
    // queue to the foreground window's, call SetForegroundWindow while attached, then
    // detach. Without this the window stays behind whatever is on top and the capture
    // records the wrong pixels.
    public static bool ForceForeground(IntPtr hwnd)
    {
        IntPtr fg = GetForegroundWindow();
        uint fgThread = GetWindowThreadProcessId(fg, IntPtr.Zero);
        uint myThread = GetCurrentThreadId();

        if (fgThread != myThread) AttachThreadInput(myThread, fgThread, true);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        SetActiveWindow(hwnd);   // returns HWND, not bool, so it cannot be OR-ed in
        if (fgThread != myThread) AttachThreadInput(myThread, fgThread, false);

        System.Threading.Thread.Sleep(400);
        return GetForegroundWindow() == hwnd;
    }

    // Normalised to 0..65535 against the virtual desktop, which is what
    // MOUSEEVENTF_ABSOLUTE expects. Sending a move first matters: ImGui only knows an
    // item is hovered once it has seen a WM_MOUSEMOVE at the new position, so a press
    // without a preceding move lands on whatever the previous hover was.
    static INPUT MouseAbs(int x, int y, uint flags)
    {
        int vw = GetSystemMetrics(78);   // SM_CXVIRTUALSCREEN
        int vh = GetSystemMetrics(79);   // SM_CYVIRTUALSCREEN
        int vx = GetSystemMetrics(76);   // SM_XVIRTUALSCREEN
        int vy = GetSystemMetrics(77);   // SM_YVIRTUALSCREEN

        INPUT i = new INPUT();
        i.type = INPUT_MOUSE;
        i.U.mi = new MOUSEINPUT();
        i.U.mi.dwFlags = flags | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        i.U.mi.dx = (int)(((x - vx) * 65535.0) / Math.Max(1, vw - 1));
        i.U.mi.dy = (int)(((y - vy) * 65535.0) / Math.Max(1, vh - 1));
        return i;
    }

    static void Send(params INPUT[] inputs)
    {
        SendInput((uint)inputs.Length, inputs, Marshal.SizeOf(typeof(INPUT)));
    }

    public static void MoveTo(int x, int y)
    {
        Send(MouseAbs(x, y, MOUSEEVENTF_MOVE));
        System.Threading.Thread.Sleep(150);
    }

    public static void Click(int x, int y)
    {
        MoveTo(x, y);
        Send(MouseAbs(x, y, MOUSEEVENTF_LEFTDOWN));
        System.Threading.Thread.Sleep(70);
        Send(MouseAbs(x, y, MOUSEEVENTF_LEFTUP));
        System.Threading.Thread.Sleep(220);
    }

    // Moves the pointer without clicking. The particle field and the light glow react to
    // the cursor, so a screenshot taken with the pointer parked over the content area
    // shows the effect; one taken with it at the far corner does not.
    public static void Hover(int x, int y)
    {
        MoveTo(x, y);
        System.Threading.Thread.Sleep(200);
    }

    // Presses, moves in steps, then releases. Used for the page scrollbar: the root
    // window carries NoScrollWithMouse, so the wheel deliberately does nothing and the
    // only way down the page is to drag the scrollbar itself.
    public static void Drag(int x1, int y1, int x2, int y2)
    {
        MoveTo(x1, y1);
        System.Threading.Thread.Sleep(150);
        Send(MouseAbs(x1, y1, MOUSEEVENTF_LEFTDOWN));
        System.Threading.Thread.Sleep(120);

        int steps = 24;
        for (int s = 1; s <= steps; s++)
        {
            int x = x1 + (x2 - x1) * s / steps;
            int y = y1 + (y2 - y1) * s / steps;
            Send(MouseAbs(x, y, MOUSEEVENTF_MOVE));
            System.Threading.Thread.Sleep(25);
        }

        System.Threading.Thread.Sleep(120);
        Send(MouseAbs(x2, y2, MOUSEEVENTF_LEFTUP));
        System.Threading.Thread.Sleep(300);
    }
}
"@ -ReferencedAssemblies System.Drawing, System.Windows.Forms

$script:client = @{}

function Get-Win {
    $p = Get-Process TENGRI_shot -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $p) { throw "TENGRI_shot is not running" }
    $r = New-Object Driver+RECT
    [void][Driver]::GetClientRect($p.MainWindowHandle, [ref]$r)
    return $p
}

# Brings the app forward and REPORTS whether it actually got there. The boolean is
# load-bearing: SetForegroundWindow is refused when the caller is not foreground,
# and the only honest way to know is to read GetForegroundWindow back.
function Focus-App {
    $p = Get-Win
    # Only un-minimize. Calling SW_RESTORE unconditionally also un-maximizes, which
    # silently reverted the window back to its default size partway through a capture.
    if ([Driver]::IsIconic($p.MainWindowHandle)) {
        [void][Driver]::ShowWindow($p.MainWindowHandle, 9)   # SW_RESTORE
    }
    $ok = [Driver]::ForceForeground($p.MainWindowHandle)
    Start-Sleep -Milliseconds 500
    return $ok
}

# Clicks at a client-relative offset: Click-At 300 420
function Click-At([int]$cx, [int]$cy) {
    $p = Get-Win
    $w = New-Object Driver+RECT
    [void][Driver]::GetWindowRect($p.MainWindowHandle, [ref]$w)
    $null = Focus-App
    $x = $w.Left + $cx
    $y = $w.Top + $cy
    [Driver]::Click($x, $y)
}

function Hover-At([int]$cx, [int]$cy) {
    $p = Get-Win
    $w = New-Object Driver+RECT
    [void][Driver]::GetWindowRect($p.MainWindowHandle, [ref]$w)
    [Driver]::Hover($w.Left + $cx, $w.Top + $cy)
}

# Types into whatever field currently holds ImGui's text focus. The license box
# must be clicked first to focus it; SendInput goes to the foreground window, so
# the app is raised before typing.
function Type-Text([string]$S) {
    $null = Focus-App
    [Driver]::TypeText($S)
}

# Drag expressed in client coordinates: Drag-At 955 120 955 520
function Drag-At([int]$x1, [int]$y1, [int]$x2, [int]$y2) {
    $p = Get-Win
    $w = New-Object Driver+RECT
    [void][Driver]::GetWindowRect($p.MainWindowHandle, [ref]$w)
    Focus-App
    [Driver]::Drag($w.Left + $x1, $w.Top + $y1, $w.Left + $x2, $w.Top + $y2)
}

# Captures just the app window, so the screenshot is not full of whatever else is on the
# desktop.
#
# CopyFromScreen grabs whatever is physically on top, so a capture taken while
# another window holds the foreground silently records THAT window instead - the
# file is written, the size looks plausible, and the only symptom is a wrong
# picture in docs/screenshots. So the foreground is confirmed to be the app before
# every capture, with a few retries; if it can never be confirmed we fail loudly
# rather than save the wrong thing.
function Save-Shot([string]$Path) {
    $p = Get-Win

    $ready = $false
    for ($i = 0; $i -lt 6 -and -not $ready; $i++) {
        if (-not (Focus-App)) { Start-Sleep -Milliseconds 600 }
        $ready = [Driver]::GetForegroundWindow() -eq $p.MainWindowHandle
        if (-not $ready) { Start-Sleep -Milliseconds 500 }
    }
    if (-not $ready) {
        throw "uygulama one ge(em)iyor; ekran goruntusu alinmadi ($Path). Baska bir pencere odagi cekmis olurdu."
    }
    Start-Sleep -Milliseconds 500

    $w = New-Object Driver+RECT
    [void][Driver]::GetWindowRect($p.MainWindowHandle, [ref]$w)
    $width  = $w.Right - $w.Left
    $height = $w.Bottom - $w.Top
    if ($width -le 0 -or $height -le 0) { throw "bad window rect" }

    $bmp = New-Object System.Drawing.Bitmap $width, $height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($w.Left, $w.Top, 0, 0, $bmp.Size)
    $g.Dispose()
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output ("saved {0} ({1}x{2})" -f $Path, $width, $height)
}

function Start-App {
    $existing = Get-Process TENGRI_shot -ErrorAction SilentlyContinue
    if ($existing) { $existing | Stop-Process -Force; Start-Sleep -Seconds 2 }
    Start-Process -FilePath $Exe -WorkingDirectory (Get-Location) | Out-Null
    Start-Sleep -Seconds 9
    $p = Get-Win
    [void][Driver]::SetForegroundWindow($p.MainWindowHandle)
    Start-Sleep -Milliseconds 800
    return $p
}

# --- yakalamanin gercekten dogru sayfayi gosterdigini denetleyen yardimcilar ----
#
# Betik bir zamanlar dokuz sayfanin tamamina ayni goruntuyu yazdi: kilidi acan
# tiklama hedefini bulamamisti ve hicbir sey bundan suphelenmedi. Dosya vardi,
# boyutu saglamdi, icerik yanlisti. Asagidaki iz karsilastirmasi o sessiz hatayi
# yayindan once yakalar.

# Pencereyi bellege yakalar. Cagiran Dispose etmek zorunda.
function Grab-Bitmap {
    $p = Get-Win
    $null = Focus-App
    $w = New-Object Driver+RECT
    [void][Driver]::GetWindowRect($p.MainWindowHandle, [ref]$w)
    $bmp = New-Object System.Drawing.Bitmap ($w.Right - $w.Left), ($w.Bottom - $w.Top)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($w.Left, $w.Top, 0, 0, $bmp.Size)
    $g.Dispose()
    return $bmp
}

# Içeriğin kaba izi: 16 x 10 gri örnek. Aynı iz iki sayfada tekrar ediyorsa
# tıklama hedefini bulamamıştır.
#
# (Bir ara giriş ekranı ayrımı menü satırındaki parlak piksel sayısıyla
# yapılıyordu ve yanlış çıktı: parlaklık seçili satıra göre değişiyor, bazı
# gerçek sayfalarda sayı eşiğin altına düşüyordu.)
function Get-BitmapSignature([System.Drawing.Bitmap]$Bmp) {
    $sig = @()
    for ($gy = 0; $gy -lt 10; $gy++) {
        for ($gx = 0; $gx -lt 16; $gx++) {
            $x = [Math]::Min(240 + $gx * 44, $Bmp.Width - 2)
            $y = [Math]::Min(96 + $gy * 54,  $Bmp.Height - 2)
            $c = $Bmp.GetPixel($x, $y)
            $sig += [int](0.299 * $c.R + 0.587 * $c.G + 0.114 * $c.B)
        }
    }
    return ,$sig
}

function Get-ImageSignature([string]$Path) {
    $bmp = New-Object System.Drawing.Bitmap $Path
    $sig = Get-BitmapSignature $bmp
    $bmp.Dispose()
    return ,$sig
}

# Karedeki izi daha once kaydedilmis bir izle karsilastirir. 0-2 ayni kare,
# gercek bir sayfa degisikligi olcuilen 40+ demek. Esik cagiran tarafca
# verilebiliyor: giris ekranini gecme denemesi, sarsinti/hata animasyonunun
# yarattigi kucuk farki ("sayfa degisti" sanma) elemek icin yuksuk esik ister.
function Test-SameAsSignature([int[]]$Reference, [int]$Threshold = 6) {
    $bmp = Grab-Bitmap
    $sig = Get-BitmapSignature $bmp
    $bmp.Dispose()
    return ((Compare-Signature $Reference $sig) -lt $Threshold)
}

# Farklı örnek sayısı. Animasyonlu arka plan yüzünden iki aynı sayfa bile
# birebir aynı çıkmaz; eşik bu yüzden yüksek değil ama sıfır da değil.
function Compare-Signature($a, $b) {
    $d = 0
    for ($i = 0; $i -lt $a.Count; $i++) {
        if ([Math]::Abs($a[$i] - $b[$i]) -gt 12) { $d++ }
    }
    return $d
}

# ---- ada yüzeyi için yardımcıları -------------------------------------------------
#
# Ada ayrı bir pencere (TENGRI_MainWindow_Island), kendi üstünde duran bir karenin
# içine yazılıyor; Save-Shot'un "uygulama önde mi" denetimi burada yanlış ölçü olur,
# çünkü adanın ön planda olması gerekmiyor - WS_EX_TOPMOST ile kendi başına duruyor.
# Bu yüzden kare doğrudan ekran dikdörtgeninden kırpılır ve üretilen görsel
# capture_all.ps1 içinde içerik iziyle denetlenir.

function Get-IslandHwnd([int]$ProcId, [string]$Suffix) {
    $h = [Driver]::FindWindowByClassSuffix([uint32]$ProcId, $Suffix)
    if ($h -eq [IntPtr]::Zero) { throw "ada penceresi bulunamadi (*$Suffix pid=$ProcId); ada kapali ya da pencere sinifi degismis olabilir" }
    return $h
}

function Get-Rect([IntPtr]$Hwnd) {
    $r = New-Object Driver+RECT
    if (-not [Driver]::GetWindowRect($Hwnd, [ref]$r)) { throw "GetWindowRect basarisiz" }
    return @{ Left = $r.Left; Top = $r.Top; Width = ($r.Right - $r.Left); Height = ($r.Bottom - $r.Top) }
}

# Imleci mutlak ekran koordinatına taşır. Ada hover ile genişliyor; tıklama yok.
function Hover-Abs([int]$X, [int]$Y) {
    [Driver]::Hover($X, $Y)
}

function Save-RectShot([int]$X, [int]$Y, [int]$Width, [int]$Height, [string]$Path) {
    if ($Width -le 0 -or $Height -le 0) { throw "bad crop rect" }
    $bmp = New-Object System.Drawing.Bitmap $Width, $Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($X, $Y, 0, 0, $bmp.Size)
    $g.Dispose()
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output ("saved {0} ({1}x{2})" -f $Path, $Width, $Height)
}

# Yakalama oturumu bittiğinde süreç artık işe yaramaz; bırakmak bir sonraki
# çalıştırmada Start-App'in onu öldürmesine kalmamalı.
function Stop-App {
    $p = Get-Process TENGRI_shot -ErrorAction SilentlyContinue
    if ($p) { $p | Stop-Process -Force; Start-Sleep -Seconds 2 }
}

# Kırpmanın "içerik var mı" ölçüsü: 20 x 8 ızgi örnek, medyan parlaklıktan >12
# farkli olanlarin sayisi. Get-BitmapSignature'in tam tersi bir soruya yanıt
# veriyor: orası aynı karenin iki kez yazılıp yazılmadığınına bakıyor, burası
# karede hiç çizim olup olmadığına. Eşiğin uydurulmaması için eşik-öncesi ölçüm
# capture_all.ps1'de ada açık ve ada kapalı iki kırpma üzerinden yazdırılır.
function Get-FrameSpread([string]$Path) {
    $bmp = New-Object System.Drawing.Bitmap $Path
    $vals = @()
    $mx = [int]($bmp.Width * 0.06); $my = [int]($bmp.Height * 0.10)
    for ($gy = 0; $gy -lt 8; $gy++) {
        for ($gx = 0; $gx -lt 20; $gx++) {
            $x = [int]($mx + ($bmp.Width  - 2 * $mx) * ($gx + 0.5) / 20)
            $y = [int]($my + ($bmp.Height - 2 * $my) * ($gy + 0.5) / 8)
            $x = [Math]::Min([Math]::Max($x, 0), $bmp.Width - 1)
            $y = [Math]::Min([Math]::Max($y, 0), $bmp.Height - 1)
            $c = $bmp.GetPixel($x, $y)
            $vals += [int](0.299 * $c.R + 0.587 * $c.G + 0.114 * $c.B)
        }
    }
    $bmp.Dispose()
    $sorted = @($vals | Sort-Object)
    $n = $sorted.Count
    if ($n % 2 -eq 1) { $med = $sorted[[int]($n / 2)] }
    else { $med = [int](($sorted[$n / 2 - 1] + $sorted[$n / 2]) / 2) }
    $d = 0
    foreach ($v in $vals) { if ([Math]::Abs($v - $med) -gt 12) { $d++ } }
    return $d
}

# Ada karti varlik denetimi. Get-FrameSpread bunu kaldirmaz: kart duz, koyu yesil
# bir panel oldugu icin parlaklik dagilimi ardindaki hareketli masaustunden DAHA
# dar cikabiliyor (olculdu: ada 45 / bos masaustu 70). Bu yuzden denetimi
# masaustunu-yenme varsayimi uzerine degil, temanin kendine has aksani rengine
# bagliyoruz. Spotify adasi ilerleme cubugu, ses dolusu ve duraklatma halkasinda
# doygun yesil tasir; saf bir masaustunde bu piksel neredeyse hic yoktur.
# (olculdu: 10-island.png = 210 yesil ornek, dashboard/masaustu ~0.)
function Get-GreenAccentCount([string]$Path) {
    $bmp = New-Object System.Drawing.Bitmap $Path
    $n = 0
    for ($y = 0; $y -lt $bmp.Height; $y += 2) {
        for ($x = 0; $x -lt $bmp.Width; $x += 2) {
            $c = $bmp.GetPixel($x, $y)
            if ($c.G -gt 120 -and $c.G -gt ($c.R + 40) -and $c.G -gt ($c.B + 40)) { $n++ }
        }
    }
    $bmp.Dispose()
    return $n
}