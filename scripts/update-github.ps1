[CmdletBinding()]
param(
    [string]$Remote = "origin",
    [string]$Message = "",
    [switch]$NoPrompt
)

$ErrorActionPreference = "Stop"

function Invoke-Step {
    param(
        [string]$Title,
        [scriptblock]$Command
    )

    Write-Host ""
    Write-Host "==> $Title" -ForegroundColor Cyan
    & $Command
}

function Read-YesNo {
    param(
        [string]$Prompt,
        [bool]$DefaultYes = $true
    )

    if ($NoPrompt) {
        return $true
    }

    $suffix = if ($DefaultYes) { "[Y/n]" } else { "[y/N]" }
    $answer = Read-Host "$Prompt $suffix"
    if ([string]::IsNullOrWhiteSpace($answer)) {
        return $DefaultYes
    }

    return $answer.Trim().ToLowerInvariant().StartsWith("y")
}

$scriptPath = $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path (Split-Path -Parent $scriptPath) "..")
Set-Location $repoRoot

Invoke-Step "Checking repository" {
    git rev-parse --is-inside-work-tree | Out-Null

    $branch = (git branch --show-current).Trim()
    if (-not $branch) {
        throw "Could not determine the current Git branch. Are you in detached HEAD?"
    }

    $remoteUrl = (git remote get-url $Remote).Trim()
    Write-Host "Repository: $repoRoot"
    Write-Host "Branch:     $branch"
    Write-Host "Remote:     $Remote ($remoteUrl)"

    Set-Variable -Name CurrentBranch -Value $branch -Scope Script
}

Invoke-Step "Current changes" {
    $changes = git status --short
    if (-not $changes) {
        Write-Host "No changes to commit."
        Set-Variable -Name HasChanges -Value $false -Scope Script
        return
    }

    $changes | ForEach-Object { Write-Host $_ }
    Set-Variable -Name HasChanges -Value $true -Scope Script
}

if (-not $script:HasChanges) {
    exit 0
}

Invoke-Step "Staging project changes" {
    git add -A
}

Invoke-Step "Files staged for commit" {
    $staged = git diff --cached --name-status
    if (-not $staged) {
        Write-Host "Nothing staged after git add."
        exit 0
    }

    $staged | ForEach-Object { Write-Host $_ }
}

if (-not $Message) {
    if ($NoPrompt) {
        $Message = "PC update $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
    } else {
        $Message = Read-Host "Commit message (leave empty for timestamp message)"
        if ([string]::IsNullOrWhiteSpace($Message)) {
            $Message = "PC update $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
        }
    }
}

if (-not (Read-YesNo "Commit and push these files to GitHub?")) {
    Write-Host "Cancelled. Staged changes were left staged."
    exit 1
}

Invoke-Step "Committing" {
    git commit -m $Message
}

Invoke-Step "Pushing to GitHub" {
    git push $Remote $script:CurrentBranch
}

Write-Host ""
Write-Host "Done. Raspberry Pi can now pull with the web Update button." -ForegroundColor Green
