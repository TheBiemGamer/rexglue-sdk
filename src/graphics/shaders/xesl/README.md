# EDRAM shader sources

Xenia's XeSL sources for the D3D12 shaders that read or write EDRAM: resolves, resolve clears and
host depth stores. They come from the Xenia project (https://github.com/xenia-project/xenia) at
commit `04d5c40d0dc23e14e3c6015fd37fa8f3c09a0677`, the revision whose checked-in bytecode matches
`../bytecode/d3d12_5_1/`. The only local change to the originals is the include path of
`xesl.xesli`, which lives in this folder instead of `src/xenia/ui/shaders/`.

Rebuild the headers with `tools/build_d3d12_edram_shaders.ps1` (needs fxc from the Windows SDK and
clang-format). With the default `-TileCount 2048` it reproduces the committed headers exactly.
