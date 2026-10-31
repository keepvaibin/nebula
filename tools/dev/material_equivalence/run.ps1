# Material shader equivalence (harness.cpp): renders the same seeded random pixel keys, constants, textures, lights
# and interpolants on WARP with an older tree's generated pixel shaders and with this tree's material shader, then
# compares every target's bytes (compare.py).
#   run.ps1 -Old <tree> [-Work <dir>]
# -Old is the root of a checkout that still has ShaderGenerator::generate_ps (a git worktree of 10ba1d80, for example).
param([Parameter(Mandatory)][string]$Old, [string]$Work = "")
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Resolve-Path (Join-Path $here '..\..\..')
. (Join-Path $repo 'tools\enter_msvc_environment.ps1') | Out-Null
if ($Work -eq "") { $Work = Join-Path ([IO.Path]::GetTempPath()) 'nebula-material-equivalence' }
New-Item -ItemType Directory -Force $Work | Out-Null

function Build-Harness([string]$Tree, [string]$Name, [string[]]$Defines) {
    $obj = Join-Path $Work $Name
    New-Item -ItemType Directory -Force $obj | Out-Null
    $render = Join-Path $Tree 'runtime\src\render'
    $src = @((Join-Path $here 'harness.cpp'), "$render\shader_keys.cpp", "$render\draw_state.cpp",
             "$render\native_material.cpp", "$render\matrix_memory.cpp", "$render\shader_gen.cpp",
             "$render\enhanced_lighting.cpp", "$render\render_config.cpp", "$render\rt_surfaces.cpp",
             (Join-Path $Tree 'runtime\src\atomic_file.cpp'))
    $exe = Join-Path $Work "$Name.exe"
    & cl.exe /nologo /std:c++20 /EHsc /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN $Defines /I (Join-Path $Tree 'runtime\include') `
        /Fo"$obj\\" /Fe"$exe" $src | Select-String -Pattern ' error ' | Select-Object -First 10
    if ($LASTEXITCODE -ne 0) { throw "harness build failed for $Tree" }
    return $exe
}

$oldExe = Build-Harness $Old 'old' @('/DOLD_GENERATOR')
$newExe = Build-Harness $repo 'new' @()
$a = Join-Path $Work 'old.bin'
$b = Join-Path $Work 'new.bin'
& $oldExe $a
& $newExe $b (Join-Path $repo 'runtime\src\render\material_ps.hlsl')
& python -I (Join-Path $here 'compare.py') $a $b
