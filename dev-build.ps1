<#
.SYNOPSIS
  Build rápido de desenvolvimento: recompila só AkumaHD.exe e AkumaHud.dll e instala
  sempre no mesmo lugar.

.DESCRIPTION
  Local padrão do jogo de desenvolvimento: dist\jogo\Akuma-Portatil
  (rode o Jogar.exe ou o AkumaHD.exe dali).

  Entradas fixas:
    .work\orig\Akuma.exe                 cópia fiel do executável do CD
    .work\zig\zig-x86_64-windows-0.14.1  compilador do HUD
    C:\Taewool\Slayers\img               arte reaproveitada pelo HUD

  Não mexe em dados do jogo, saves, música nem no ddraw. Para o pacote completo
  e para a release continue usando build.ps1 e make_overlay.ps1.

.PARAMETER NetLog
  Liga o log de rede (cria netlog.enable na pasta do jogo). Saída: netlog.txt.

.PARAMETER NoNetLog
  Desliga o log de rede (remove netlog.enable).

.PARAMETER Kit
  Também gera dist\netlog-kit.zip (AkumaHD.exe, AkumaHud.dll e netlog.enable) para
  copiar em outra máquina.

.EXAMPLE
  .\dev-build.ps1 -NetLog -Kit
#>
[CmdletBinding()]
param(
    [string]$Game = (Join-Path $PSScriptRoot 'dist\jogo\Akuma-Portatil'),
    [string]$Original = (Join-Path $PSScriptRoot '.work\orig\Akuma.exe'),
    [string]$Zig = (Join-Path $PSScriptRoot '.work\zig\zig-x86_64-windows-0.14.1\zig.exe'),
    [string]$SlayersImg = 'C:\Taewool\Slayers\img',
    [switch]$NetLog,
    [switch]$NoNetLog,
    [switch]$Kit,
    [switch]$DialogScale,
    [switch]$NoDialogScale,
    [switch]$NoInstall,
    [switch]$Second,
    [string]$SecondGame = (Join-Path $PSScriptRoot 'dist\jogo-2\Akuma-Portatil')
)

$ErrorActionPreference = 'Stop'
if ($NetLog -and $NoNetLog) { throw 'Use -NetLog ou -NoNetLog, não os dois.' }
foreach ($required in @($Game, $Original, $Zig, $SlayersImg)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Não encontrado: $required" }
}
$gamePath = (Resolve-Path -LiteralPath $Game).Path
if (-not $NoInstall -and (Get-Process -Name AkumaHD -ErrorAction SilentlyContinue)) {
    throw 'Feche o AkumaHD.exe antes de instalar o build.'
}

$stage = Join-Path $PSScriptRoot '.work\dev-stage'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
$exe = Join-Path $stage 'AkumaHD.exe'
$dll = Join-Path $stage 'AkumaHud.dll'
Remove-Item -LiteralPath $exe, $dll -ErrorAction SilentlyContinue

& python (Join-Path $PSScriptRoot 'patch_akuma.py') $Original $exe
if ($LASTEXITCODE) { throw 'Falha ao gerar o AkumaHD.exe.' }
& python (Join-Path $PSScriptRoot 'hud_skin.py') (Join-Path $gamePath 'img\Pan01-4.pcx') (Join-Path $stage 'hud_skin.h') $SlayersImg
if ($LASTEXITCODE) { throw 'Falha ao preparar as texturas do HUD.' }
& python (Join-Path $PSScriptRoot 'quest_starts.py') $gamePath (Join-Path $stage 'quest_starts.h')
if ($LASTEXITCODE) { throw 'Falha ao indexar os inícios de missão.' }
& $Zig cc -target x86-windows-gnu -O2 -s -shared -I $stage -o $dll (Join-Path $PSScriptRoot 'AkumaHud.c') (Join-Path $PSScriptRoot 'CdAudio.c') -luser32 -lgdi32 -lwinmm
if ($LASTEXITCODE) { throw 'Falha ao compilar o HUD.' }

$flag = Join-Path $gamePath 'netlog.enable'
if (-not $NoInstall) {
    Copy-Item -LiteralPath $exe, $dll -Destination $gamePath -Force
    if ($NetLog) { New-Item -ItemType File -Force -Path $flag | Out-Null }
    if ($NoNetLog) { Remove-Item -LiteralPath $flag -ErrorAction SilentlyContinue }
    $scaleFlag = Join-Path $gamePath 'dialogscale.enable'
    if ($DialogScale) { New-Item -ItemType File -Force -Path $scaleFlag | Out-Null }
    if ($NoDialogScale) { Remove-Item -LiteralPath $scaleFlag -ErrorAction SilentlyContinue }
}

if ($Kit) {
    $kitPath = Join-Path $PSScriptRoot 'dist\netlog-kit.zip'
    $kitStage = Join-Path $stage 'kit'
    Remove-Item -LiteralPath $kitStage -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $kitStage | Out-Null
    Copy-Item -LiteralPath $exe, $dll -Destination $kitStage
    New-Item -ItemType File -Path (Join-Path $kitStage 'netlog.enable') | Out-Null
    Set-Content -LiteralPath (Join-Path $kitStage 'LEIA-ME.txt') -Encoding UTF8 -Value @(
        'Copie estes arquivos para a pasta do jogo, substituindo AkumaHD.exe e AkumaHud.dll.',
        'netlog.enable liga o log de rede; o jogo grava netlog.txt na mesma pasta.',
        'Apague netlog.enable para desligar.')
    Remove-Item -LiteralPath $kitPath -ErrorAction SilentlyContinue
    Compress-Archive -Path (Join-Path $kitStage '*') -DestinationPath $kitPath
    Write-Host "Kit:    $kitPath"
}

if ($Second -and -not $NoInstall) {
    # Segunda cópia para rodar dois jogos na mesma máquina: dados pesados por junction
    # (somente leitura para o jogo); executáveis, ini, saves e log são próprios.
    New-Item -ItemType Directory -Force -Path $SecondGame | Out-Null
    foreach ($shared in 'img', 'map', 'WAV', 'Shaders', 'manual') {
        $link = Join-Path $SecondGame $shared
        if (-not (Test-Path -LiteralPath $link)) {
            New-Item -ItemType Junction -Path $link -Target (Join-Path $gamePath $shared) | Out-Null
        }
    }
    Get-ChildItem -LiteralPath $gamePath -File | Where-Object {
        $_.Name -notmatch '^(LEGENSAV|MULTISAV|netlog)' -or $_.Name -eq 'netlog.enable'
    } | ForEach-Object {
        if ($_.Name -eq 'dialogscale.enable' -and $NoDialogScale) { return }
        $target = Join-Path $SecondGame $_.Name
        # Mantém Akuma.ini/ddraw.ini já ajustados da segunda cópia.
        if (($_.Name -in 'Akuma.ini', 'ddraw.ini') -and (Test-Path -LiteralPath $target)) { return }
        Copy-Item -LiteralPath $_.FullName -Destination $target -Force
    }
    if ($NoNetLog) { Remove-Item -LiteralPath (Join-Path $SecondGame 'netlog.enable') -ErrorAction SilentlyContinue }
    if ($NoDialogScale) { Remove-Item -LiteralPath (Join-Path $SecondGame 'dialogscale.enable') -ErrorAction SilentlyContinue }
    Write-Host "Jogo 2: $SecondGame"
}
if ($NoInstall) {
    Write-Host "Compilado em $stage (nada foi instalado no jogo)."
    Get-Item -LiteralPath $exe, $dll | Select-Object Name, Length, LastWriteTime
    return
}
Write-Host "Jogo:   $gamePath"
Write-Host ("Netlog: " + $(if (Test-Path -LiteralPath $flag) { 'LIGADO' } else { 'desligado' }) + "  ($flag)")
Get-Item -LiteralPath (Join-Path $gamePath 'AkumaHD.exe'), (Join-Path $gamePath 'AkumaHud.dll') | Select-Object Name, Length, LastWriteTime
