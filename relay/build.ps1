<#
.SYNOPSIS
  Compila o akuma-relay para Windows e Linux com o Zig do projeto. Saída em relay\bin.
#>
$ErrorActionPreference = 'Stop'
$zig = Join-Path $PSScriptRoot '..\.work\zig\zig-x86_64-windows-0.14.1\zig.exe'
$src = Join-Path $PSScriptRoot 'akuma-relay.c'
$bin = Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force -Path $bin | Out-Null
$targets = @(
    @{ t = 'x86_64-windows-gnu'; o = 'akuma-relay-win64.exe'; l = '-lws2_32' },
    @{ t = 'x86-windows-gnu';    o = 'akuma-relay-win32.exe'; l = '-lws2_32' },
    @{ t = 'x86_64-linux-musl';  o = 'akuma-relay-linux-x86_64'; l = $null },
    @{ t = 'aarch64-linux-musl'; o = 'akuma-relay-linux-aarch64'; l = $null }
)
foreach ($x in $targets) {
    $args = @('cc', '-target', $x.t, '-O2', '-Wall', '-s', '-o', (Join-Path $bin $x.o), $src)
    if ($x.l) { $args += $x.l }
    & $zig @args
    if ($LASTEXITCODE) { throw "Falha em $($x.t)" }
    Write-Host ("{0,-28} {1,8} bytes" -f $x.o, (Get-Item (Join-Path $bin $x.o)).Length)
}
