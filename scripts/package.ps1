# Erzeugt VoiceTune.ts3_plugin (ZIP mit package.ini + plugins/) zum Installieren per Doppelklick.
# Eigener ZIP-Aufbau mit '/' in den Eintragsnamen: Compress-Archive (PowerShell 5.1) schreibt '\', was der TS3-Installer nicht mag.
param(
    [string]$Dll = (Join-Path $PSScriptRoot '..\build\VoiceTune_win64.dll'),
    [string]$Out = (Join-Path $PSScriptRoot '..\build\VoiceTune.ts3_plugin')
)

if (-not (Test-Path $Dll)) { throw "DLL nicht gefunden: $Dll" }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$ini = @"
Name = VoiceTune
Type = Plugin
Author = Dave Swift X Michael Schulz
Version = 2.0
Platforms = win64
Description = VoiceTune: Your Sound. Your Voice. Your Way. Your Tune! Spielt den Ton einer beliebigen App (Spotify, YouTube ...) oder eigener Audio-Dateien in dein TeamSpeak-Mikrofon - mit Equalizer, Mithoeren, Musikmodus, Opus-Vorschau und Datei-Player.
"@

$Out = [IO.Path]::GetFullPath($Out)
Remove-Item $Out -ErrorAction SilentlyContinue
$zip = [IO.Compression.ZipFile]::Open($Out, 'Create')
try {
    $e = $zip.CreateEntry('package.ini')
    $w = New-Object IO.StreamWriter($e.Open(), [Text.Encoding]::ASCII)
    $w.Write($ini); $w.Dispose()
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Resolve-Path $Dll).Path, 'plugins/VoiceTune_win64.dll')
    $icons = Join-Path $PSScriptRoot '..\assets\icons'
    if (Test-Path $icons) {
        foreach ($png in Get-ChildItem $icons -Filter *.png) {
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $png.FullName, "plugins/VoiceTune_win64/$($png.Name)")
        }
    }
} finally { $zip.Dispose() }
Write-Host "Paket erstellt: $Out"
