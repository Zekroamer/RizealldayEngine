# Builds the engine and packs it for the team: dist\RizealldayEngine-<version>.zip
#   powershell -ExecutionPolicy Bypass -File Tools\MakeRelease.ps1
# Bump kEngineVersion in RizealldayEngine\base\EngineVersion.h before every release.
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot

# Reject build if there are uncommitted changes in git
$gitStatus = (& git -C $repo status --porcelain)
if ($gitStatus) {
    throw "Release build rejected: working tree has uncommitted changes:`n$gitStatus"
}

# Require SCRIPTING_GUIDE.md and CHANGELOG.md
if (-not (Test-Path "$repo\SCRIPTING_GUIDE.md")) {
    throw "Release build rejected: SCRIPTING_GUIDE.md not found in working tree."
}
if (-not (Test-Path "$repo\CHANGELOG.md")) {
    throw "Release build rejected: CHANGELOG.md not found in repository."
}

$commit = (& git -C $repo rev-parse HEAD).Trim()

$versionHeader = Get-Content -Raw "$repo\RizealldayEngine\base\EngineVersion.h"
$version = [regex]::Match($versionHeader, 'kEngineVersion\s*=\s*"([^"]+)"').Groups[1].Value
$scriptApi = [regex]::Match((Get-Content -Raw "$repo\RizealldayEngine\base\ScriptAPI.h"), 'kScriptApiVersion\s*=\s*(\d+)').Groups[1].Value
if (-not $version -or -not $scriptApi) { throw 'Could not read kEngineVersion / kScriptApiVersion.' }

# Always built here, so the commit in engine.json is the code in the zip
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
foreach ($config in 'Development', 'Release') {
    & $msbuild "$repo\Rizeallday_Engine_v2_GE3.slnx" /p:Configuration=$config /p:Platform=x64 /m /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "$config build failed." }
}

$name = "RizealldayEngine-$version"
$dist = "$repo\dist"
$out = "$dist\$name"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null

# Editor\ = the Development build (editor UI) with its runtime files
$bin = "$repo\bin\Development"
New-Item -ItemType Directory -Force "$out\Editor" | Out-Null
Copy-Item "$bin\RizealldaySandbox.exe", "$bin\*.dll" "$out\Editor"
Copy-Item -Recurse "$bin\Resources", "$bin\shaders" "$out\Editor"

# Runtime\ = the Release build (no editor): every exported game starts from these files
$release = "$repo\bin\Release"
New-Item -ItemType Directory -Force "$out\Runtime" | Out-Null
Copy-Item "$release\RizealldaySandbox.exe", "$release\*.dll" "$out\Runtime"

# SDK\include = the headers game scripts compile against (header-only: scripts link nothing from the engine)
New-Item -ItemType Directory -Force "$out\SDK\include\base", "$out\SDK\include\math" | Out-Null
Copy-Item "$repo\RizealldayEngine\base\ScriptAPI.h", "$repo\RizealldayEngine\base\ScriptTimers.h", "$repo\RizealldayEngine\base\EngineVersion.h" "$out\SDK\include\base"
Get-ChildItem "$repo\RizealldayEngine\math\*.h" | Where-Object { $_.Name -ne 'MathUtil.h' } | Copy-Item -Destination "$out\SDK\include\math"

# Templates\: the scripts project of every new game project, and the sample scripts
Copy-Item -Recurse "$repo\Templates" "$out\Templates"
New-Item -ItemType Directory -Force "$out\Templates\ScriptsProject\Scripts", "$out\Templates\Samples\Survivors" | Out-Null
Copy-Item "$repo\GameScripts\Scripts\dllmain.cpp" "$out\Templates\ScriptsProject\Scripts"
Copy-Item "$repo\GameScripts\Scripts\Survivors*" "$out\Templates\Samples\Survivors"
New-Item -ItemType Directory -Force "$out\Templates\Samples\Bench" | Out-Null
Copy-Item "$repo\GameScripts\Scripts\Bench*" "$out\Templates\Samples\Bench"
New-Item -ItemType Directory -Force "$out\Templates\Samples\ThirdPerson" | Out-Null
Copy-Item "$repo\GameScripts\Scripts\ThirdPerson*" "$out\Templates\Samples\ThirdPerson"

Copy-Item "$repo\Tools\README_TEAM.md", "$repo\Tools\GUIDE_JA.md", "$repo\CHANGELOG.md", "$repo\SCRIPTING_GUIDE.md" "$out"
$built = (Get-Date).ToString('yyyy-MM-dd')
Set-Content -Encoding ASCII "$out\engine.json" "{ `"version`": `"$version`", `"scriptApi`": $scriptApi, `"built`": `"$built`", `"commit`": `"$commit`" }"

# tar.exe (Windows 10/11) writes "/" separators, which the editor's updater (also tar.exe) reads back
$zip = "$dist\$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
& tar.exe -a -c -f $zip -C $dist $name
if ($LASTEXITCODE -ne 0) { throw 'Creating the zip failed.' }
Write-Host "Release ready: $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
