[CmdletBinding()]
param(
    # Pasta do jogo extraída do CD, sem patch (a "cópia fiel").
    [Parameter(Mandatory = $true)]
    [string]$Original,
    # ZIP gerado pelo build.ps1.
    [Parameter(Mandatory = $true)]
    [string]$Built,
    [string]$Output = (Join-Path $PSScriptRoot 'dist\Akuma-Patch.zip')
)

# Gera um ZIP só com os arquivos novos ou diferentes entre a cópia fiel e o
# build. Extraído sobre a cópia fiel, deixa a pasta idêntica ao build.

$ErrorActionPreference = 'Stop'
$originalPath = (Resolve-Path -LiteralPath $Original).Path
$builtZip = (Resolve-Path -LiteralPath $Built).Path
$zipPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $zipPath) | Out-Null
$stage = Join-Path (Split-Path -Parent $zipPath) ('.overlay-' + [guid]::NewGuid().ToString('N'))
$built = Join-Path $stage 'built'
$overlay = Join-Path $stage 'overlay'
New-Item -ItemType Directory -Path $built, $overlay | Out-Null

try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($builtZip, $built)
    # O build coloca tudo dentro de uma pasta raiz (Akuma-Portatil); o patch
    # usa caminhos relativos à pasta do jogo.
    $children = @(Get-ChildItem -LiteralPath $built)
    if ($children.Count -eq 1 -and $children[0].PSIsContainer) { $built = $children[0].FullName }

    $changed = 0
    $same = 0
    Get-ChildItem -LiteralPath $built -Recurse -File | ForEach-Object {
        $relative = $_.FullName.Substring($built.Length + 1)
        $source = Join-Path $originalPath $relative
        $identical = (Test-Path -LiteralPath $source -PathType Leaf) -and
            ((Get-FileHash -LiteralPath $source).Hash -eq (Get-FileHash -LiteralPath $_.FullName).Hash)
        if ($identical) { $same++; return }
        $target = Join-Path $overlay $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $target
        $changed++
    }
    Write-Host "$changed arquivos no patch, $same idênticos ao original."

    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath }
    # Entradas com "/" como separador, para abrir em qualquer extrator.
    $archive = [IO.Compression.ZipFile]::Open($zipPath, 'Create')
    try {
        Get-ChildItem -LiteralPath $overlay -Recurse -File | ForEach-Object {
            $name = $_.FullName.Substring($overlay.Length + 1).Replace('\', '/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $name, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    }
    finally { $archive.Dispose() }
    Get-Item -LiteralPath $zipPath
}
finally {
    $resolved = (Resolve-Path -LiteralPath $stage).Path
    if ((Split-Path -Leaf $resolved) -match '^\.overlay-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
