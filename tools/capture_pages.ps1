. .\tools\shoot.ps1
$p = Get-Win
Write-Output ("start: {0}" -f $p.MainWindowTitle)

# Sidebar rows, in client coordinates: the nav list starts at y=128 and each item is 44px.
$pages = @(
    @{ Name = '03-tweaks';      Y = 216 },
    @{ Name = '04-network';     Y = 260 },
    @{ Name = '05-system-info'; Y = 304 },
    @{ Name = '06-settings';    Y = 348 }
)

foreach ($page in $pages) {
    Click-At 100 $page.Y
    Start-Sleep -Milliseconds 1400
    # Park the pointer over the content area so the particle field and the top light glow
    # are in a representative state rather than reacting to the last click.
    Hover-At 600 400
    Start-Sleep -Milliseconds 900
    Save-Shot ("docs\screenshots\{0}.png" -f $page.Name)
}

Write-Output "done"