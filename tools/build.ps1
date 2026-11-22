param(
    [Parameter(Mandatory)][string]$WxRoot,
    [string]$Compiler = "g++",
    [string]$ResourceCompiler = "windres"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$wx = (Resolve-Path -LiteralPath $WxRoot).Path
$libs = Join-Path $wx "lib/gcc_dll"
$setup = Join-Path $libs "mswu"
$includes = Join-Path $wx "include"
$build = Join-Path $root "build"
$output = Join-Path $build "windows"

if (!(Test-Path (Join-Path $setup "wx/setup.h"))) {
    throw "Se necesita wxWidgets 3.3.1 para MinGW, compilado como DLL Unicode monolítica con OpenGL (gcc_dll/mswu)."
}

New-Item -ItemType Directory -Force $output | Out-Null
$defines = @("-D__WXMSW__", "-DWXUSINGDLL", "-DUNICODE", "-D_UNICODE")
Push-Location -LiteralPath $root
try {
    & $ResourceCompiler "--preprocessor=gcc -E -xc -DRC_INVOKED" @defines "-I$setup" "-I$includes" `
        -i resource.rc -o build/resource.o
    if ($LASTEXITCODE -ne 0) { throw "Falló la compilación del recurso de Windows." }

    & $Compiler -std=c++17 -O2 -mthreads @defines "-I$setup" "-I$includes" `
        SISTEMA_DIVINCRIApp.cpp SISTEMA_DIVINCRIMain.cpp build/resource.o `
        "-L$libs" -mwindows -lwxmsw33u_gl -lwxmsw33u -lglu32 -lopengl32 -lgdi32 `
        -static-libgcc -static-libstdc++ -o build/windows/DIVINCRI-3D.exe
    if ($LASTEXITCODE -ne 0) { throw "Falló la compilación de DIVINCRI 3D." }
}
finally {
    Pop-Location
}

Copy-Item -Path (Join-Path $libs "wxmsw331u*_gcc_custom.dll") -Destination $output -Force
$compilerBin = Split-Path (Get-Command $Compiler -CommandType Application).Source
foreach ($name in @("libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll")) {
    $runtime = Join-Path $compilerBin $name
    if (!(Test-Path -LiteralPath $runtime)) {
        throw "No se encontró la DLL de ejecución $name del compilador MinGW."
    }
    Copy-Item -LiteralPath $runtime -Destination $output -Force
}
Copy-Item -LiteralPath (Join-Path $root "assets") -Destination $output -Recurse -Force
if (Test-Path (Join-Path $wx "docs/licence.txt")) {
    foreach ($file in @("licence.txt", "lgpl.txt", "gpl.txt")) {
        Copy-Item -LiteralPath (Join-Path $wx "docs/$file") -Destination (Join-Path $output "wxWidgets-$file")
    }
}

Write-Output (Join-Path $output "DIVINCRI-3D.exe")
