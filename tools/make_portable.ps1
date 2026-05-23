param(
    [string]$PackageName = "RecuClassic-portable-win64"
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Dist = Join-Path $Root "dist"
$PackageDir = Join-Path $Dist $PackageName
$ZipPath = Join-Path $Dist "$PackageName.zip"

$GuiExe = Join-Path $Root "bin\recu-classic.exe"
$CliExe = Join-Path $Root "bin\recu-cli.exe"
if (-not (Test-Path $GuiExe)) { throw "Missing $GuiExe. Build first with make all." }
if (-not (Test-Path $CliExe)) { throw "Missing $CliExe. Build first with make all." }

if (Test-Path $PackageDir) { Remove-Item -LiteralPath $PackageDir -Recurse -Force }
if (Test-Path $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }

New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageDir "config") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageDir "reports") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageDir "logs") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageDir "recovered") | Out-Null

Copy-Item -LiteralPath $GuiExe -Destination (Join-Path $PackageDir "recu-classic.exe")
Copy-Item -LiteralPath $CliExe -Destination (Join-Path $PackageDir "recu-cli.exe")
if (Test-Path (Join-Path $Root "README.md")) {
    Copy-Item -LiteralPath (Join-Path $Root "README.md") -Destination (Join-Path $PackageDir "README.md")
}

@'
[settings]
'@ | Set-Content -LiteralPath (Join-Path $PackageDir "config\settings.ini") -Encoding ASCII

@'
@echo off
start "" "%~dp0recu-classic.exe"
'@ | Set-Content -LiteralPath (Join-Path $PackageDir "Start Recu Classic.cmd") -Encoding ASCII

Compress-Archive -LiteralPath $PackageDir -DestinationPath $ZipPath -Force

Write-Host "Portable folder: $PackageDir"
Write-Host "Portable zip:    $ZipPath"
