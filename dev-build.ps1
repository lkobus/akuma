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

.PARAMETER WinsockLog
  Liga o log do Winsock do DirectPlay (cria winsocklog.enable). Saída: winsocklog.txt.

.PARAMETER NoWinsockLog
  Desliga o log do Winsock (remove winsocklog.enable).

.PARAMETER RelayLocal
  Escreve relays.txt e rede.txt nas duas pastas (jogo e jogo-2) apontando para o relay em
  -RelayAddress (padrão 127.0.0.1:47700), rede "teste", sem senha. Só vale com -Second para a
  segunda pasta. Sem rede.txt o jogo continua usando o Winsock direto.

.PARAMETER NoRelay
  Remove relays.txt e rede.txt (volta ao Winsock direto).

.PARAMETER KitRelay
  host:porta do relay gravado no relays.txt e no rede.txt do kit. Padrão: o IPv4 desta máquina
  na rede local (a interface com gateway) e a porta 47700.

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
    [string]$KitRelay = '',
    [switch]$WinsockLog,
    [switch]$NoWinsockLog,
    [switch]$RelayLocal,
    [switch]$NoRelay,
    [string]$RelayAddress = '127.0.0.1:47700',
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
& $Zig cc -target x86-windows-gnu -O2 -s -shared -I $stage -o $dll (Join-Path $PSScriptRoot 'AkumaHud.c') (Join-Path $PSScriptRoot 'CdAudio.c') (Join-Path $PSScriptRoot 'NetShim.c') -luser32 -lgdi32 -lwinmm -lws2_32
if ($LASTEXITCODE) { throw 'Falha ao compilar o HUD.' }
$launcherExe = Join-Path $stage 'Jogar.exe'
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
& $csc /nologo /target:winexe /platform:x86 /optimize+ "/out:$launcherExe" (Join-Path $PSScriptRoot 'Launcher.cs')
if ($LASTEXITCODE) { throw 'Falha ao compilar o launcher.' }

$flag = Join-Path $gamePath 'netlog.enable'
if (-not $NoInstall) {
    Copy-Item -LiteralPath $exe, $dll, $launcherExe -Destination $gamePath -Force
    if ($NetLog) { New-Item -ItemType File -Force -Path $flag | Out-Null }
    if ($NoNetLog) { Remove-Item -LiteralPath $flag -ErrorAction SilentlyContinue }
    $wsFlag = Join-Path $gamePath 'winsocklog.enable'
    if ($WinsockLog) { New-Item -ItemType File -Force -Path $wsFlag | Out-Null }
    if ($NoWinsockLog) { Remove-Item -LiteralPath $wsFlag -ErrorAction SilentlyContinue }
    $scaleFlag = Join-Path $gamePath 'dialogscale.enable'
    if ($DialogScale) { New-Item -ItemType File -Force -Path $scaleFlag | Out-Null }
    if ($NoDialogScale) { Remove-Item -LiteralPath $scaleFlag -ErrorAction SilentlyContinue }
}

if ($Kit) {
    $kitPath = Join-Path $PSScriptRoot 'dist\netlog-kit.zip'
    $kitStage = Join-Path $stage 'kit'
    Remove-Item -LiteralPath $kitStage -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $kitStage | Out-Null
    Copy-Item -LiteralPath $exe, $dll, $launcherExe -Destination $kitStage
    New-Item -ItemType File -Path (Join-Path $kitStage 'netlog.enable') | Out-Null
    New-Item -ItemType File -Path (Join-Path $kitStage 'winsocklog.enable') | Out-Null
    if (-not $KitRelay) {
        $lan = Get-NetIPConfiguration -ErrorAction SilentlyContinue | Where-Object { $_.IPv4DefaultGateway -and $_.IPv4Address } |
            Select-Object -First 1 -ExpandProperty IPv4Address
        $KitRelay = ($(if ($lan) { $lan.IPAddress } else { '127.0.0.1' })) + ':47700'
    }
    Set-Content -LiteralPath (Join-Path $kitStage 'relays.txt') -Encoding ASCII -Value @('# nome regiao host:porta', "casa lan $KitRelay")
    Set-Content -LiteralPath (Join-Path $kitStage 'rede.txt') -Encoding ASCII -Value @('relay=casa', 'rede=teste', 'senha=', 'apelido=pc2')
    Set-Content -LiteralPath (Join-Path $kitStage 'LEIA-ME.txt') -Encoding UTF8 -Value @(
        'KIT DE TESTE DO RELAY',
        '',
        '1. Feche o jogo. Copie TODOS estes arquivos para a pasta do jogo (onde esta o Jogar.exe),',
        '   substituindo AkumaHD.exe, AkumaHud.dll e Jogar.exe.',
        "2. O relay deve estar rodando no outro PC ($KitRelay).",
        '3. Abra o Jogar.exe, aba Online: o relay "casa" ja vem na lista. Marque "Jogar online",',
        '   escolha "casa", rede "teste", e clique em Testar relay. Depois Jogar.',
        '   (Se abrir o AkumaHD.exe direto, o rede.txt desta pasta faz o mesmo.)',
        '4. No jogo: Multi Play, TCP/IP, deixe o IP vazio e procure. Um PC cria, o outro procura.',
        '5. Para colher logs, jogue um pouco e feche. Mande winsocklog.txt, netlog.txt e relay-status.txt.',
        '',
        'netlog.enable e winsocklog.enable ligam os logs. Apague os dois para desligar.')
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
        $_.Name -notmatch '^(LEGENSAV|MULTISAV|netlog|winsocklog|relay-status|rede\.txt)' -or $_.Name -in 'netlog.enable', 'winsocklog.enable'
    } | ForEach-Object {
        if ($_.Name -eq 'dialogscale.enable' -and $NoDialogScale) { return }
        $target = Join-Path $SecondGame $_.Name
        # Mantém Akuma.ini/ddraw.ini já ajustados da segunda cópia.
        if (($_.Name -in 'Akuma.ini', 'ddraw.ini') -and (Test-Path -LiteralPath $target)) { return }
        Copy-Item -LiteralPath $_.FullName -Destination $target -Force
    }
    if ($NoNetLog) { Remove-Item -LiteralPath (Join-Path $SecondGame 'netlog.enable') -ErrorAction SilentlyContinue }
    if ($NoWinsockLog) { Remove-Item -LiteralPath (Join-Path $SecondGame 'winsocklog.enable') -ErrorAction SilentlyContinue }
    if ($NoDialogScale) { Remove-Item -LiteralPath (Join-Path $SecondGame 'dialogscale.enable') -ErrorAction SilentlyContinue }
    Write-Host "Jogo 2: $SecondGame"
}
if (-not $NoInstall) {
    # Relay: relays.txt (cadastro) e rede.txt (qual relay, rede e apelido). Sem credenciais.
    $targets = @($gamePath); if ($Second) { $targets += $SecondGame }
    foreach ($t in $targets) {
        $isSecond = $t -ne $gamePath
        if ($NoRelay) {
            Remove-Item -LiteralPath (Join-Path $t 'relays.txt'), (Join-Path $t 'rede.txt') -ErrorAction SilentlyContinue
        } elseif ($RelayLocal) {
            Set-Content -LiteralPath (Join-Path $t 'relays.txt') -Encoding ASCII -Value @('# nome regiao host:porta', "local dev $RelayAddress")
            Set-Content -LiteralPath (Join-Path $t 'rede.txt') -Encoding ASCII -Value @('relay=local', 'rede=teste', 'senha=', ('apelido=' + $(if ($isSecond) { 'jogador2' } else { 'jogador1' })))
        }
    }
    if ($RelayLocal) { Write-Host "Relay:  rede 'teste' em $RelayAddress (relays.txt e rede.txt gravados)" }
    if ($NoRelay) { Write-Host 'Relay:  desligado (relays.txt e rede.txt removidos)' }
}
if ($NoInstall) {
    Write-Host "Compilado em $stage (nada foi instalado no jogo)."
    Get-Item -LiteralPath $exe, $dll | Select-Object Name, Length, LastWriteTime
    return
}
Write-Host "Jogo:   $gamePath"
Write-Host ("Netlog: " + $(if (Test-Path -LiteralPath $flag) { 'LIGADO' } else { 'desligado' }) + "  ($flag)")
Get-Item -LiteralPath (Join-Path $gamePath 'AkumaHD.exe'), (Join-Path $gamePath 'AkumaHud.dll') | Select-Object Name, Length, LastWriteTime
