# Builds the D3D12 shaders that address EDRAM (resolve, clear, host depth store) from the Xenia
# sources in src/graphics/shaders/xesl, for a 2048-tile (stock) or 4096-tile EDRAM (resolves and
# clears only, as *_4096_cs.h). Uses the same
# fxc flags as Xenia's xenia-build buildshaders at the imported revision (see xesl/README.md), then
# clang-format, so a 2048-tile build reproduces the committed headers exactly.
param([ValidateSet(2048, 4096)][int]$TileCount = 2048, [string]$Only = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$src = Join-Path $root "src/graphics/shaders/xesl"
$dst = Join-Path $root "src/graphics/shaders/bytecode/d3d12_5_1"
$fxc = $env:FXC_PATH
if (-not $fxc) {
  $fxc = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\fxc.exe" |
         Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
}
if (-not $fxc) { throw "fxc.exe not found; install the Windows SDK or set FXC_PATH" }
$clangFormat = $env:CLANG_FORMAT_PATH
if (-not $clangFormat) {
  $clangFormat = (Get-Command clang-format -ErrorAction SilentlyContinue).Source
}
if (-not $clangFormat) {
  $clangFormat = Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\clang-format.exe" |
                 Select-Object -First 1 -ExpandProperty FullName
}
if (-not $clangFormat) { throw "clang-format not found; set CLANG_FORMAT_PATH" }
$baseBits = if ($TileCount -eq 4096) { 12 } else { 11 }
$suffix = if ($TileCount -eq 4096) { "_4096" } else { "" }
$names = Get-ChildItem $src -Filter "*.cs.xesl" | ForEach-Object { $_.Name -replace '\.cs\.xesl$', '' }
# Host depth stores address the destination relative to the render target, not EDRAM, so they
# don't depend on the EDRAM size and have no 4096-tile variant.
if ($TileCount -eq 4096) { $names = @($names | Where-Object { $_ -like "resolve_*" }) }
if ($Only) { $names = @($names | Where-Object { $_ -eq $Only }) }
Push-Location $src
try {
  foreach ($n in $names) {
    $out = Join-Path $dst "$($n)$($suffix)_cs.h"
    $fxcArgs = @("/D", "SHADING_LANGUAGE_HLSL_XE=1", "/D", "XE_EDRAM_TILE_COUNT=$TileCount",
                 "/D", "XE_EDRAM_BASE_TILES_BITS=$baseBits", "/Fh", $out, "/T", "cs_5_1",
                 "/Vn", "$($n)$($suffix)_cs", "/nologo", "$n.cs.xesl")
    & $fxc @fxcArgs | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "fxc failed for $n" }
    & $clangFormat -i "--style=file:$(Join-Path $root '.clang-format')" $out
    if ($LASTEXITCODE -ne 0) { throw "clang-format failed for $n" }
    # Committed headers use LF line endings.
    $text = [System.IO.File]::ReadAllText($out) -replace "`r`n", "`n"
    [System.IO.File]::WriteAllText($out, $text)
  }
} finally {
  Pop-Location
}
"built $($names.Count) shaders for $TileCount tiles"
