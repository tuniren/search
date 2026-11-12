param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$QtRoot,
    [switch]$SkipTests,
    [switch]$Deploy,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
if ($Package -and $Configuration -ne 'Release') { throw 'Portable packages must use a Release build.' }
if ($Package -and -not (Get-Command python -ErrorAction SilentlyContinue)) { throw 'Python 3.10+ is required for packaging.' }
$env:VSLANG = '1033'
$projectRoot = Split-Path -Parent $PSScriptRoot

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ desktop tools first.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'MSVC x64 tools were not found.' }
    & (Join-Path $installation 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}

if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) { throw 'CMake 3.25+ must be available on PATH.' }
if (-not (Get-Command ninja.exe -ErrorAction SilentlyContinue)) { throw 'Ninja must be available on PATH.' }

$preset = if ($Configuration -eq 'Debug') { 'windows-debug' } else { 'windows-release' }
if ($QtRoot) {
    if ($Configuration -eq 'Debug') { throw 'Use the vcpkg debug preset or configure a local SDK debug preset explicitly.' }
    $env:QT_ROOT = (Resolve-Path -LiteralPath $QtRoot).Path
    $preset = 'qt-sdk-release'
} elseif (-not $env:VCPKG_ROOT) {
    $localQt = Join-Path $projectRoot '.tools\Qt\6.8.3\msvc2022_64'
    if ((Test-Path -LiteralPath $localQt) -and $Configuration -eq 'Release') {
        $env:QT_ROOT = $localQt
        $preset = 'qt-sdk-release'
    } else { throw 'Set VCPKG_ROOT, or pass -QtRoot pointing to a compatible MSVC Qt SDK.' }
}

Push-Location $projectRoot
try {
    & cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build --preset $preset --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    $buildDirectory = Join-Path $projectRoot "build\$preset"
    if (-not $SkipTests) {
        $qtBin = if ($preset -eq 'qt-sdk-release') { Join-Path $env:QT_ROOT 'bin' } else { Join-Path $buildDirectory 'vcpkg_installed\x64-windows\bin' }
        $savedPath = $env:PATH
        try {
            $env:PATH = "$qtBin;$env:PATH"
            & ctest --preset $preset
            if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
        } finally { $env:PATH = $savedPath }
    }
    if ($Deploy) {
        # Always stage into an empty directory so obsolete DLLs and plugins
        # cannot survive a change to deployment options.
        $deploymentId = [guid]::NewGuid().ToString('N')
        $stagingDirectory = Join-Path $buildDirectory "deploy-$deploymentId"
        $distributionDirectory = Join-Path $projectRoot 'dist'
        & cmake --install $buildDirectory --prefix $stagingDirectory
        if ($LASTEXITCODE -ne 0) { throw 'Deployment failed.' }
        $workspacePrefix = [IO.Path]::GetFullPath($projectRoot).TrimEnd('\') + '\'
        $backupDirectory = Join-Path $buildDirectory "previous-dist-$deploymentId"
        foreach ($targetDirectory in @($stagingDirectory, $distributionDirectory, $backupDirectory)) {
            $absoluteTarget = [IO.Path]::GetFullPath($targetDirectory)
            if (-not $absoluteTarget.StartsWith($workspacePrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Deployment target is outside the workspace: $absoluteTarget"
            }
        }
        if (Test-Path -LiteralPath $distributionDirectory) {
            Move-Item -LiteralPath $distributionDirectory -Destination $backupDirectory
        }
        try {
            Move-Item -LiteralPath $stagingDirectory -Destination $distributionDirectory
        } catch {
            if ((Test-Path -LiteralPath $backupDirectory) -and -not (Test-Path -LiteralPath $distributionDirectory)) {
                Move-Item -LiteralPath $backupDirectory -Destination $distributionDirectory
            }
            throw
        }
    }
    if ($Package) {
        & python (Join-Path $PSScriptRoot 'package.py') --build-dir $buildDirectory
        if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
    }
    Write-Host "Built: $buildDirectory\search.exe"
} finally { Pop-Location }
