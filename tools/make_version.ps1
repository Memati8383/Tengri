# Generates the version header that res\tengri.rc includes.
#
# The version lives in exactly one place -- src\brand.hpp -- because a second copy in
# the .rc file is a second chance to forget. It was forgotten once: a tag was cut for
# v1.0.1 while the resource still said 1.0.0, and the release was only caught because the
# CI compares the two after building.
#
# A response file for rc.exe would avoid the extra file, but the values are quoted C
# strings with spaces in them and quoting inside an rc response file is its own source of
# silent trouble. A generated header has no such layer.
#
# Output: build\obj\version.h (build output, deliberately not tracked)

param(
    [string]$Brand  = 'src\brand.hpp',
    [string]$Out    = 'build\obj\version.h'
)

$ErrorActionPreference = 'Stop'

$text = [System.IO.File]::ReadAllText((Resolve-Path $Brand).Path)

function Get-Const([string]$name) {
    # Matches:  constexpr const char*    kVersion = "1.0.0";
    $m = [regex]::Match($text, $name + '\s*=\s*"([^"]*)"\s*;')
    if (-not $m.Success) { throw "$name bulunamadi ($Brand)" }
    return $m.Groups[1].Value
}

$major = Get-Const 'kVersionMajor'
$minor = Get-Const 'kVersionMinor'
$patch = Get-Const 'kVersionPatch'
$ver   = Get-Const 'kVersion'
$pub   = Get-Const 'kPublisher'
$desc  = Get-Const 'kDescription'

# The three parts have to spell the same number as the combined string, or Explorer and
# the About box end up disagreeing about what build this is.
if ("$major.$minor.$patch" -ne $ver) {
    throw "Surum parcalari tutarsiz: kVersionMajor/Minor/Patch = $major.$minor.$patch, kVersion = $ver"
}

# A backslash or a double quote would end the string literal early and produce a resource
# that fails to compile with a confusing error, so reject them here instead.
foreach ($pair in @(@('kVersion', $ver), @('kPublisher', $pub), @('kDescription', $desc))) {
    if ($pair[1] -match '["\\]') { throw "$($pair[0]) icinde tirnak veya ters egik cizgi var" }
}

# CMake passes an absolute path, build.bat a relative one. Join-Path mangles the
# absolute case into something the filesystem rejects, so root the check first.
$outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path (Get-Location) $Out }

$outDir = Split-Path -Parent $outPath
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }

$content = @"
// tools/make_version.ps1 tarafindan uretilir -- elle düzenleme.
//
// Surumun tek kaynagi src/brand.hpp'dir. Bu dosya her derlemede yeniden yazilir,
// boylece VERSIONINFO kaynagi kaynak koddan ayrilamaz.

#define TENGRI_VER_MAJOR $major
#define TENGRI_VER_MINOR $minor
#define TENGRI_VER_PATCH $patch

#define TENGRI_VERSION_STR      "$ver"
#define TENGRI_PUBLISHER_STR    "$pub"
#define TENGRI_DESCRIPTION_STR  "$desc"
"@

# The here-string above ends on the last #define with no line terminator, and rc's
# preprocessor treats that as an unterminated file (RC1004).
$content += "`r`n"

# BOM is required, not cosmetic. rc.exe is invoked without /utf8, so it reads this
# header in the system ANSI code page unless a BOM tells it otherwise; without one
# a publisher name carrying the dotted Turkish capital renders as "TENGRI" or, if
# the code page disagrees, as a garbled double-byte sequence. Same rule as
# res\tengri.rc.
[System.IO.File]::WriteAllText($outPath, $content, (New-Object System.Text.UTF8Encoding $true))
Write-Output ("    surum basligi yazildi: {0}  ({1})" -f $outPath, $ver)