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

    const uint INPUT_MOUSE = 0;
    const uint MOUSEEVENTF_MOVE = 0x0001, MOUSEEVENTF_LEFTDOWN = 0x0002, MOUSEEVENTF_LEFTUP = 0x0004;
    const uint MOUSEEVENTF_ABSOLUTE = 0x8000, MOUSEEVENTF_VIRTUALDESK = 0x4000;

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

function Focus-App {
    $p = Get-Win
    # Only un-minimize. Calling SW_RESTORE unconditionally also un-maximizes, which
    # silently reverted the window back to its default size partway through a capture.
    if ([Driver]::IsIconic($p.MainWindowHandle)) {
        [void][Driver]::ShowWindow($p.MainWindowHandle, 9)   # SW_RESTORE
    }
    [Driver]::ForceForeground($p.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 500
}

# Clicks at a client-relative offset: Click-At 300 420
function Click-At([int]$cx, [int]$cy) {
    $p = Get-Win
    $w = New-Object Driver+RECT
    [void][Driver]::GetWindowRect($p.MainWindowHandle, [ref]$w)
    Focus-App
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

# Drag expressed in client coordinates: Drag-At 955 120 955 520
function Drag-At([int]$x1, [int]$y1, [int]$x2, [int]$y2) {
    $p = Get-Win
    $w = New-Object Driver+RECT
    [void][Driver]::GetWindowRect($p.MainWindowHandle, [ref]$w)
    Focus-App
    [Driver]::Drag($w.Left + $x1, $w.Top + $y1, $w.Left + $x2, $w.Top + $y2)
}

# Captures just the app window, so the screenshot is not full of whatever else is on the
# desktop. Falls back to the full screen if the window cannot be isolated.
function Save-Shot([string]$Path) {
    $p = Get-Win
    Focus-App
    Start-Sleep -Milliseconds 700
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