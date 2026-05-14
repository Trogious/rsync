# scripts/smoke-test.ps1
#
# Runs a few minimal checks against rsync.exe:
#   1. --version exits 0
#   2. --help    exits 0
#   3. Local-to-local copy round-trips a single file
#   4. --daemon  exits with RERR_UNSUPPORTED (4)
#   5. rsync://  exits with RERR_UNSUPPORTED (4)
#
# Does NOT test remote (SSH) transfers — that needs a Linux target.
param([Parameter(Mandatory=$true)][string]$Binary)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Binary)) {
    throw "Binary not found: $Binary"
}

# PowerShell's call operator (`&`) won't run a relative-path executable
# unless we prefix it with `.\` or hand it an absolute path. Resolve
# whatever the caller passed (e.g. "rsync.exe" from CI) into the full
# path so & $Binary just works downstream.
$Binary = (Resolve-Path $Binary).Path

Write-Host '== --version =='
& $Binary --version
if ($LASTEXITCODE -ne 0) { throw "rsync --version failed (exit $LASTEXITCODE)" }

Write-Host '== --help =='
& $Binary --help | Out-Null
if ($LASTEXITCODE -ne 0) { throw "rsync --help failed (exit $LASTEXITCODE)" }

Write-Host '== local-to-local copy =='
$src = Join-Path $env:TEMP "rsync-smoke-src-$([guid]::NewGuid().Guid)"
$dst = Join-Path $env:TEMP "rsync-smoke-dst-$([guid]::NewGuid().Guid)"
New-Item -ItemType Directory $src | Out-Null
'test content' | Out-File "$src\file.txt"

& $Binary -av "$src\" "$dst\"
if ($LASTEXITCODE -ne 0) { throw "rsync local copy failed (exit $LASTEXITCODE)" }
if (-not (Test-Path "$dst\file.txt")) { throw 'destination file missing' }

# Compare contents. Use .NET directly: when PowerShell 7 is installed
# alongside, its v7 Utility module sometimes shadows the v3.1 one that
# Windows PowerShell 5.1 needs for Get-FileHash, and the cmdlet then
# disappears.
function Get-Sha256Hex([string]$Path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
        finally { $stream.Dispose() }
    } finally { $sha.Dispose() }
}
$srcHash = Get-Sha256Hex "$src\file.txt"
$dstHash = Get-Sha256Hex "$dst\file.txt"
if ($srcHash -ne $dstHash) {
    throw "Round-trip hash mismatch: $srcHash != $dstHash"
}
Remove-Item -Recurse -Force $src, $dst

Write-Host '== --daemon rejected =='
# PowerShell's $ErrorActionPreference = Stop turns native-command stderr
# into a terminating error even with 2>$null in some shell versions.
# Use cmd /c to keep rsync's stderr from tripping the PS error policy.
cmd /c "`"$Binary`" --daemon 2>nul"
if ($LASTEXITCODE -ne 4) {
    Write-Warning "Expected exit 4 (RERR_UNSUPPORTED) for --daemon, got $LASTEXITCODE"
} else {
    Write-Host 'OK: --daemon -> RERR_UNSUPPORTED'
}

Write-Host '== rsync:// URL rejected =='
cmd /c "`"$Binary`" rsync://example.invalid/mod/path . 2>nul"
if ($LASTEXITCODE -ne 4) {
    Write-Warning "Expected exit 4 (RERR_UNSUPPORTED) for rsync://, got $LASTEXITCODE"
} else {
    Write-Host 'OK: rsync:// -> RERR_UNSUPPORTED'
}

Write-Host ''
Write-Host 'Smoke tests passed.'
