[CmdletBinding()]
param(
    [string]$PiHost = "pi@192.168.0.210",
    [string]$ProjectDir = "/home/pi/mv",
    [string]$BuildDir = "build",
    [string]$Generator = "Ninja",
    [string]$Target = "cv",
    [switch]$Run,
    [switch]$InstallAutostart,
    [switch]$SkipPush
)

$ErrorActionPreference = "Stop"

function Invoke-Step {
    param(
        [string]$Title,
        [scriptblock]$Command
    )

    Write-Host ""
    Write-Host "==> $Title"
    & $Command
}

Invoke-Step "Checking local Git state" {
    $branch = (git branch --show-current).Trim()
    if (-not $branch) {
        throw "Could not determine the current Git branch."
    }

    $dirty = git status --porcelain
    if ($dirty) {
        throw "Working tree has uncommitted changes. Commit them before deploying to the Raspberry Pi."
    }

    Write-Host "Branch: $branch"
    Set-Variable -Name CurrentBranch -Value $branch -Scope Script
}

if (-not $SkipPush) {
    Invoke-Step "Pushing local branch to GitHub" {
        git push origin $script:CurrentBranch
    }
}

$remoteCommands = @(
    "set -e",
    "cd '$ProjectDir'",
    "git pull --ff-only",
    "cmake -S . -B '$BuildDir' -G '$Generator' -DCMAKE_BUILD_TYPE=Release",
    "cmake --build '$BuildDir' -j`$(nproc)"
)

if ($InstallAutostart) {
    $remoteCommands += "bash scripts/install-pi-desktop-autostart.sh"
}

if ($Run) {
    $remoteCommands += "./$BuildDir/$Target"
}

$remoteCommand = $remoteCommands -join " && "

Invoke-Step "Deploying on Raspberry Pi $PiHost" {
    ssh $PiHost $remoteCommand
}

Write-Host ""
Write-Host "Deploy complete."
