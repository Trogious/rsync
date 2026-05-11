# scripts/bootstrap-windows.ps1
# One-time setup for the build environment on Windows.
# Run from an elevated PowerShell prompt.
$ErrorActionPreference = 'Stop'

# 1. Install MSYS2 (only used as the autotools shell at build time; not a runtime dep)
if (-not (Test-Path 'C:\msys64\usr\bin\bash.exe')) {
    Write-Host 'Installing MSYS2...'
    $url = 'https://github.com/msys2/msys2-installer/releases/latest/download/msys2-x86_64-latest.exe'
    $exe = "$env:TEMP\msys2-installer.exe"
    Invoke-WebRequest -Uri $url -OutFile $exe
    Start-Process -FilePath $exe -ArgumentList 'install --root C:\msys64 --confirm-command' -Wait
}

# 2. Update MSYS2 and install required packages
& 'C:\msys64\usr\bin\bash.exe' -lc 'pacman -Syu --noconfirm'
& 'C:\msys64\usr\bin\bash.exe' -lc 'pacman -S --noconfirm base-devel autotools make tar curl python git patch sed gawk gperf bison flex texinfo'

# 3. Visual Studio Build Tools must be installed separately with the
#    "Desktop development with C++" workload (provides cl.exe, lib.exe,
#    link.exe, mt.exe). See: https://aka.ms/vs/17/release/vs_buildtools.exe

# 4. Bootstrap vcpkg
if (-not (Test-Path 'C:\vcpkg\vcpkg.exe')) {
    git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
    & 'C:\vcpkg\bootstrap-vcpkg.bat' -disableMetrics
}

Write-Host ''
Write-Host 'Bootstrap complete.'
Write-Host 'Next: open "x64 Native Tools Command Prompt for VS 2022" and run:'
Write-Host '  git submodule update --init --recursive'
Write-Host '  C:\vcpkg\vcpkg.exe install --triplet x64-windows-static --x-manifest-root=vcpkg'
Write-Host '  powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1'
