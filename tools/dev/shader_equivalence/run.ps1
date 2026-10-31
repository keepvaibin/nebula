# Shader equivalence between two source trees (harness.cpp): builds the harness against each tree's key builder and
# shader generator, runs both over the same seeded random drawing states, and compares the generated sources (and the
# keys, when both trees share the key layout).
#   run.ps1 -Old <tree> -New <tree> [-Work <dir>]
# A tree is a checkout's root (a git worktree of the older commit, for example). Windows, MSVC.
param([Parameter(Mandatory)][string]$Old, [Parameter(Mandatory)][string]$New, [string]$Work = "")
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Resolve-Path (Join-Path $here '..\..\..')
. (Join-Path $repo 'tools\enter_msvc_environment.ps1') | Out-Null
if ($Work -eq "") { $Work = Join-Path ([IO.Path]::GetTempPath()) 'nebula-shader-equivalence' }
New-Item -ItemType Directory -Force $Work | Out-Null

function Build-Harness([string]$Tree, [string]$Name) {
    $obj = Join-Path $Work $Name
    New-Item -ItemType Directory -Force $obj | Out-Null
    $render = Join-Path $Tree 'runtime\src\render'
    # The draw's matrices: draw_matrices.cpp (E-05), matrix_memory.cpp in trees before it.
    $matrices = if (Test-Path "$render\draw_matrices.cpp") { "$render\draw_matrices.cpp" } else { "$render\matrix_memory.cpp" }
    $src = @((Join-Path $here 'harness.cpp'), "$render\shader_keys.cpp", "$render\draw_state.cpp",
             "$render\native_material.cpp", $matrices, "$render\shader_gen.cpp",
             "$render\enhanced_lighting.cpp", "$render\render_config.cpp", "$render\rt_surfaces.cpp",
             (Join-Path $Tree 'runtime\src\atomic_file.cpp'))
    $exe = Join-Path $Work "$Name.exe"
    & cl.exe /nologo /std:c++20 /EHsc /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I (Join-Path $Tree 'runtime\include') `
        /Fo"$obj\\" /Fe"$exe" $src | Select-String -Pattern ' error ' | Select-Object -First 10
    if ($LASTEXITCODE -ne 0) { throw "harness build failed for $Tree" }
    return $exe
}

$oldExe = Build-Harness $Old 'old'
$newExe = Build-Harness $New 'new'
foreach ($mode in @('sources', 'keys')) {
    $a = Join-Path $Work "old_$mode.bin"
    $b = Join-Path $Work "new_$mode.bin"
    & $oldExe $a $(if ($mode -eq 'sources') { 'sources' })
    & $newExe $b $(if ($mode -eq 'sources') { 'sources' })
    $same = (Get-FileHash $a).Hash -eq (Get-FileHash $b).Hash
    "{0}: {1} ({2} and {3} bytes)" -f $mode, $(if ($same) { 'IDENTICAL' } else { 'DIFFERENT' }), (Get-Item $a).Length, (Get-Item $b).Length
}
