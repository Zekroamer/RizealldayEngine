# End-to-end check of the game export, without a GPU or any clicks:
#   new project + Survivors demo -> export (editor exe) -> copy to a clean folder -> run N frames (Release runtime).
# Needs both configurations built, or pass the editor of an unpacked release zip.
#   powershell -ExecutionPolicy Bypass -File Tools\SmokeTestExport.ps1 [-EditorExe <path>] [-Warp] [-KeepFiles]
#   Performance run: add -Sample benchmark -Frames 900 (300 enemies + 500 bullets); prints the frame timings.
#   Effect graphs: -Sample effects (a material graph and a particle graph; the run fails when one does not compile).
param([string]$EditorExe, [switch]$Warp, [switch]$KeepFiles, [int]$Frames = 120, [string]$Sample = 'survivors')
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if (-not $EditorExe) { $EditorExe = "$repo\bin\Development\RizealldaySandbox.exe" }
if (-not (Test-Path $EditorExe)) { throw "Editor not found: $EditorExe" }

function Invoke-Timed([string]$exe, [string[]]$arguments, [string]$workDir, [int]$seconds) {
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $workDir -PassThru
    $null = $p.Handle  # keeps ExitCode available after WaitForExit
    if (-not $p.WaitForExit($seconds * 1000)) {
        Stop-Process -Id $p.Id -Force
        throw "Timed out after $seconds s: $exe $arguments"
    }
    return $p.ExitCode
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("RizeSmoke_" + [guid]::NewGuid().ToString('N').Substring(0, 8))
$project = "$work\SmokeGame"
$exportDir = "$work\Exported"
$clean = "$work\Clean Folder"
New-Item -ItemType Directory -Force $work | Out-Null
$warpArg = @(); if ($Warp) { $warpArg = @('--warp') }
try {
    # 1. Project + export: the arguments are quoted because the temp path may contain spaces
    $editorDir = Split-Path -Parent (Resolve-Path $EditorExe).Path
    $code = Invoke-Timed (Resolve-Path $EditorExe).Path (@('--new-project', "`"$project`"", '--sample', $Sample, '--export', "`"$exportDir`"") + $warpArg) $editorDir 600
    if ($code -ne 0) { throw "Export failed (exit code $code)." }

    # 2. A clean folder elsewhere: nothing of the project or engine is next to the game
    Copy-Item -Recurse $exportDir $clean
    $gameExe = Get-ChildItem "$clean\*.exe" | Select-Object -First 1
    if (-not $gameExe) { throw 'The export contains no exe.' }
    if (Test-Path "$clean\imgui.ini") { throw 'imgui.ini in the export: it came from the editor.' }

    # 3. Run the game
    $code = Invoke-Timed $gameExe.FullName (@('--smoke-frames', "$Frames") + $warpArg) $clean 180
    $result = if (Test-Path "$clean\smoke_result.txt") { (Get-Content "$clean\smoke_result.txt" -Raw).Trim() } else { '(no smoke_result.txt)' }
    Write-Host "Game run: exit code $code, $result"
    if ($code -ne 0) { throw "The exported game failed: $result" }
    if (-not $result.StartsWith('OK')) { throw "The exported game reported: $result" }
    if (Test-Path "$clean\imgui.ini") { throw 'The game created imgui.ini: it is running the editor build.' }

    $size = [math]::Round(((Get-ChildItem -Recurse -File $exportDir | Measure-Object Length -Sum).Sum) / 1MB, 1)
    Write-Host "Smoke test passed. Export size: $size MB"
} finally {
    if ($KeepFiles) { Write-Host "Files kept in: $work" } else { Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue }
}
