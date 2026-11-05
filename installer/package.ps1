param(
    [Parameter(Mandatory=$true)][string]$MakeNsis,
    [string]$OutputDirectory = "$PSScriptRoot\..\build\installers",
    [string]$QtRoot = 'C:\Qt\6.12.0\mingw_64',
    [string]$CompilerRoot = 'C:\Qt\Tools\mingw1310_64',
    [string]$CMake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe',
    [string]$Ninja = 'C:\Qt\Tools\Ninja\ninja.exe',
    [switch]$AllowDirty
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot
$version = (Get-Content -LiteralPath "$project\APP_VERSION.txt" -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'APP_VERSION.txt must contain a numeric major.minor.patch version' }
$sourceState = @(git -C $project status --porcelain --untracked-files=normal)
if ($LASTEXITCODE) { throw 'Cannot read Git source state' }
$sourceDirty = $sourceState.Count -gt 0
if ($sourceDirty -and -not $AllowDirty) { throw 'Commit source changes before packaging, or use -AllowDirty for an explicitly marked local build.' }
$build = Join-Path $project 'build\release-package'
$env:PATH = "$QtRoot\bin;$CompilerRoot\bin;$env:PATH"
& $CMake -S $project -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF "-DCMAKE_CXX_COMPILER=$CompilerRoot/bin/g++.exe" "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE) { throw 'Release configuration failed' }
& $CMake --build $build --parallel 4
if ($LASTEXITCODE) { throw 'Release build failed' }
# Each package gets a fresh staging directory, so old files cannot leak into it.
$stage = Join-Path $project ('build\package-' + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $stage 'payload'
New-Item -ItemType Directory -Force $payload,$OutputDirectory | Out-Null
Copy-Item -LiteralPath "$build\QtApp.exe" -Destination "$payload\BasisCode.exe"
Copy-Item "$project\LICENSE" "$payload\LICENSE"
Copy-Item "$project\NOTICE" "$payload\NOTICE"
$sourceCommit = git -C $project rev-parse HEAD
@{ version = $version; sourceCommit = $sourceCommit; sourceDirty = $sourceDirty; repository = 'https://github.com/ocean-Doctom/basis-code' } | ConvertTo-Json | Set-Content "$payload\release.json" -Encoding utf8
Get-ChildItem $build -Filter '*.dll' -File | Copy-Item -Destination $payload
foreach ($folder in @('platforms','styles','imageformats','iconengines','generic','networkinformation','tls')) {
    if (Test-Path "$build\$folder") { Copy-Item -LiteralPath "$build\$folder" -Destination $payload -Recurse }
}
New-Item -ItemType Directory "$payload\licenses" | Out-Null
Copy-Item "$PSScriptRoot\licenses\*" "$payload\licenses" -Recurse
Copy-Item "$project\themes\GitHub-LICENSE" "$payload\licenses\GitHub-theme.txt"
Copy-Item "$project\icons\SOURCES.md" "$payload\licenses\icons.md"
Copy-Item "$project\themes\SOURCES.md" "$payload\licenses\themes.md"
if (Test-Path "$QtRoot\LICENSES") { Copy-Item "$QtRoot\LICENSES" "$payload\licenses\Qt" -Recurse }
$lines = @(Get-ChildItem $payload -Recurse -File | ForEach-Object {
    'Delete "$INSTDIR\' + $_.FullName.Substring($payload.Length + 1) + '"'
})
$lines += @(Get-ChildItem $payload -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
    'RMDir "$INSTDIR\' + $_.FullName.Substring($payload.Length + 1) + '"'
})
$lines | Set-Content -LiteralPath "$stage\uninstall-files.nsh" -Encoding utf8
& $MakeNsis "/DAPP_VERSION=$version" "/DPAYLOAD=$payload" "/DOUTPUT=$OutputDirectory" "$PSScriptRoot\setup.nsi"
if ($LASTEXITCODE) { throw 'Installer compilation failed' }
$installer = Join-Path $OutputDirectory "BasisCode-Setup-$version-x64.exe"
(Get-FileHash $installer -Algorithm SHA256).Hash + '  ' + (Split-Path $installer -Leaf) | Set-Content "$installer.sha256" -Encoding ascii
Write-Output "Installer: $installer"
