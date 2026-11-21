[CmdletBinding()]
param(
    [string]$Image = (Join-Path $PSScriptRoot 'AKUMA.bin'),
    [Parameter(Mandatory = $true)]
    [string]$CncDdraw,
    [string]$Zig = "zig",
    [string]$Dialogues = (Join-Path $PSScriptRoot 'dialogues.json'),
    [string]$SlayersImg = 'C:\Taewool\Slayers\img',
    [string]$Output = (Join-Path $PSScriptRoot 'dist\Akuma-Portatil.zip')
)

$ErrorActionPreference = 'Stop'
$imagePath = (Resolve-Path -LiteralPath $Image).Path
$dialoguePath = (Resolve-Path -LiteralPath $Dialogues).Path
$slayersImgPath = (Resolve-Path -LiteralPath $SlayersImg).Path
$wrapperPath = (Resolve-Path -LiteralPath $CncDdraw).Path
$wrapperDll = Join-Path $wrapperPath 'ddraw.dll'
$wrapperLicense = Join-Path $wrapperPath 'LICENSE.txt'
if (-not (Test-Path -LiteralPath $wrapperLicense -PathType Leaf)) {
    $wrapperLicense = Join-Path $wrapperPath 'LICENSE'
}
$wrapperHash = '85E0F7D530DFDA134793A57CB3E76B0287DCC96892EE57162DD68F47283B03A9'
if ((Get-FileHash -LiteralPath $wrapperDll).Hash -ne $wrapperHash) {
    throw 'Use a DLL x86 do cnc-ddraw v7.1.0.0.'
}
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw 'Compilador do .NET Framework 4 indisponível neste Windows.'
}
$hudCompiler = (Get-Command -Name $Zig -CommandType Application -ErrorAction Stop).Source
$zipPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
$parent = Split-Path -Parent $zipPath
New-Item -ItemType Directory -Force -Path $parent | Out-Null
$stage = Join-Path $parent ('.build-' + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $stage 'Akuma-Portatil'
$temporaryZip = Join-Path $stage 'package.zip'
New-Item -ItemType Directory -Path $stage | Out-Null

try {
    & python (Join-Path $PSScriptRoot 'extract_cd.py') $imagePath $payload
    if ($LASTEXITCODE) { throw 'Falha ao extrair os dados do CD.' }

    & python (Join-Path $PSScriptRoot 'patch_akuma.py') (Join-Path $payload 'AKUMA.EXE') (Join-Path $payload 'AkumaHD.exe') --player (Join-Path $payload 'MCIPLAY.EXE') --wrapper $wrapperDll (Join-Path $payload 'ddraw.dll')
    if ($LASTEXITCODE) { throw 'Falha ao preparar o executável.' }

    & python (Join-Path $PSScriptRoot 'localize.py') (Join-Path $payload 'Img')
    if ($LASTEXITCODE) { throw 'Falha ao preparar os idiomas.' }
    & python (Join-Path $PSScriptRoot 'menu_labels.py') (Join-Path $payload 'Img')
    if ($LASTEXITCODE) { throw 'Falha ao preparar os rótulos do menu.' }
    & python (Join-Path $PSScriptRoot 'localize_dialogues.py') $payload $dialoguePath
    if ($LASTEXITCODE) { throw 'Falha ao preparar os diálogos.' }

    Remove-Item -LiteralPath (Join-Path $payload 'AKUMA.EXE'), (Join-Path $payload 'AKUMAUN.EXE')
    Copy-Item -LiteralPath $wrapperLicense -Destination (Join-Path $payload 'LICENSE-cnc-ddraw.txt')
    Copy-Item -LiteralPath (Join-Path $wrapperPath 'Shaders') -Destination $payload -Recurse
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ddraw.ini') -Destination $payload
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'LEIA-ME.txt') -Destination $payload

    $hud = Join-Path $stage 'AkumaHud.dll'
    & python (Join-Path $PSScriptRoot 'hud_skin.py') (Join-Path $payload 'Img\Pan01-4.pcx') (Join-Path $stage 'hud_skin.h') $slayersImgPath
    if ($LASTEXITCODE) { throw 'Falha ao preparar as texturas do HUD.' }
    & python (Join-Path $PSScriptRoot 'quest_starts.py') $payload (Join-Path $stage 'quest_starts.h')
    if ($LASTEXITCODE) { throw 'Falha ao indexar os inícios de missão.' }
    & $hudCompiler cc -target x86-windows-gnu -O2 -s -shared -I $stage -o $hud (Join-Path $PSScriptRoot 'AkumaHud.c') -luser32 -lgdi32
    if ($LASTEXITCODE) { throw 'Falha ao compilar o HUD x86.' }
    Copy-Item -LiteralPath $hud -Destination $payload

    $launcher = Join-Path $payload 'Jogar.exe'
    & $compiler /nologo /target:winexe /platform:x86 /optimize+ "/out:$launcher" (Join-Path $PSScriptRoot 'Launcher.cs')
    if ($LASTEXITCODE) { throw 'Falha ao compilar o launcher.' }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($payload, $temporaryZip, [IO.Compression.CompressionLevel]::Optimal, $true)
    Move-Item -LiteralPath $temporaryZip -Destination $zipPath -Force
    Get-Item -LiteralPath $zipPath
}
finally {
    $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
    $resolvedParent = (Resolve-Path -LiteralPath $parent).Path
    if ([string]::Equals((Split-Path -Parent $resolvedStage), $resolvedParent, [StringComparison]::OrdinalIgnoreCase) -and
         (Split-Path -Leaf $resolvedStage) -match '^\.build-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}






