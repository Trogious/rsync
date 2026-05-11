# scripts/verify-static.ps1
#
# Verifies that the given binary depends only on system-provided
# Windows DLLs. Fails the build if it pulls in any Cygwin / MSYS /
# vcruntime / mingw shim DLL.
param([Parameter(Mandatory=$true)][string]$Binary)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Binary)) {
    throw "Binary not found: $Binary"
}

$raw = & dumpbin /DEPENDENTS $Binary
$deps = $raw |
    Where-Object { $_ -match '\.dll$' } |
    ForEach-Object { $_.Trim().ToLower() }

# Exact-match allowlist of system DLLs we expect on Win10/11.
$allowedExact = @(
    'kernel32.dll', 'kernelbase.dll', 'ntdll.dll',
    'advapi32.dll', 'user32.dll',     'ws2_32.dll',
    'crypt32.dll',  'secur32.dll',    'bcrypt.dll',
    'iphlpapi.dll', 'userenv.dll',    'shell32.dll',
    'ole32.dll',    'oleaut32.dll',   'rpcrt4.dll',
    'ucrtbase.dll'
)
# Wildcard allow: api-ms-win-* are the UCRT API set forwarders.
$allowedPattern = 'api-ms-win-*'

# Patterns that signal a broken static build.
$forbiddenPatterns = @(
    'cyg*',           # Cygwin
    'msys-*',         # MSYS2 mingw runtime
    'lib*-*.dll',     # Various GNU/MinGW shims
    'vcruntime*.dll', # /MD linkage instead of /MT
    'msvcp*.dll',     # C++ runtime
    'msvcr*.dll'      # Legacy CRT
)

$bad = @()
foreach ($d in $deps) {
    foreach ($p in $forbiddenPatterns) {
        if ($d -like $p) {
            $bad += "FORBIDDEN: $d (matches $p)"
        }
    }
    $ok = $allowedExact -contains $d
    if (-not $ok -and ($d -like $allowedPattern)) { $ok = $true }
    if (-not $ok) {
        $bad += "UNKNOWN:   $d (not on allowlist; review PORTING.md)"
    }
}

if ($bad.Count -gt 0) {
    Write-Error "Static-link verification FAILED:`n$($bad -join "`n")"
    exit 1
}

Write-Host "OK: $Binary depends only on allowed Windows system DLLs."
$deps | ForEach-Object { Write-Host "  - $_" }
