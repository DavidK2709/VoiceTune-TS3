#
# VoiceTune-TS3 - Voicechanger-Plugin für TeamSpeak 3
# Copyright (C) 2026  David K., Tony S.
#
# This program is free software: you can redistribute it and/or modify
#it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# Kopiert das gebaute Plugin in den TeamSpeak-3-Plugin-Ordner des Benutzers.
# TeamSpeak muss beendet sein (die DLL ist sonst gesperrt).

param(
    [string]$Dll = (Join-Path $PSScriptRoot '..\build\VoiceTune_win64.dll')
)

if (-not (Test-Path $Dll)) { throw "DLL nicht gefunden: $Dll (zuerst bauen, siehe README.md)" }
if (Get-Process ts3client_win64 -ErrorAction SilentlyContinue) { throw 'TeamSpeak laeuft noch - bitte beenden und erneut ausfuehren.' }

$target = Join-Path $env:APPDATA 'TS3Client\plugins'
New-Item -ItemType Directory -Force $target | Out-Null
# Vorgaengerversion "App Audio Mixer" entfernen, sonst waeren zwei Plugins geladen
Remove-Item (Join-Path $target 'AppAudioMixer_win64.dll') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $target 'AppAudioMixer_win64') -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item $Dll $target -Force

# Menue-Icons: Ordner heisst wie die DLL ohne Endung
$icons = Join-Path $PSScriptRoot '..\assets\icons'
if (Test-Path $icons) {
    $iconTarget = Join-Path $target ([IO.Path]::GetFileNameWithoutExtension($Dll))
    New-Item -ItemType Directory -Force $iconTarget | Out-Null
    Copy-Item (Join-Path $icons '*.png') $iconTarget -Force
}
Write-Host "Installiert nach $target"
Write-Host 'TeamSpeak starten -> Einstellungen -> Plugins -> "VoiceTune" aktivieren (einmalig, der Name hat sich geaendert).'
