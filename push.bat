#!/usr/bin/env pwsh

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Fail([string]$Message) {
    Write-Error $Message
    exit 1
}

try {
    git rev-parse --is-inside-work-tree *> $null
} catch {
    Fail "This script must be run inside a git repository."
}

$commitMessage = Read-Host "Enter commit message"
$commitMessage = $commitMessage.Trim()

if ([string]::IsNullOrWhiteSpace($commitMessage)) {
    Fail "Commit message cannot be empty."
}

$status = git status --porcelain
if (-not $status) {
    Write-Host "No changes to commit."
    exit 0
}

Write-Host "Staging changes..."
git add -A

Write-Host "Creating commit..."
git commit -m "$commitMessage"

Write-Host "Pushing to remote..."
try {
    $upstream = git rev-parse --abbrev-ref --symbolic-full-name "@{u}" 2>$null
    if ($LASTEXITCODE -eq 0 -and $upstream) {
        git push
    } else {
        git push -u origin HEAD
    }
} catch {
    git push -u origin HEAD
}

Write-Host "Done."
