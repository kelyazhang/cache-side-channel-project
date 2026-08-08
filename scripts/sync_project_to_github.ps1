param(
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$repoPath = 'D:\aaakeyan\AOP\cache-side-channel-project'
$remoteName = 'origin'
$branchName = 'main'
$logPath = Join-Path $env:TEMP 'cache-side-channel-project-github-sync.log'
$transcriptStarted = $false

function Invoke-GitCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    Write-Host ''
    Write-Host ("git " + ($Arguments -join ' ')) -ForegroundColor Cyan
    & git @Arguments

    if ($LASTEXITCODE -ne 0) {
        throw "Git command failed: git $($Arguments -join ' ')"
    }
}

function Show-ResultMessage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message,

        [Parameter(Mandatory = $true)]
        [ValidateSet('Information', 'Error')]
        [string]$Type
    )

    Add-Type -AssemblyName System.Windows.Forms

    $icon = if ($Type -eq 'Information') {
        [System.Windows.Forms.MessageBoxIcon]::Information
    }
    else {
        [System.Windows.Forms.MessageBoxIcon]::Error
    }

    [void][System.Windows.Forms.MessageBox]::Show(
        $Message,
        'Cache Side-Channel Project - GitHub Sync',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        $icon
    )
}

try {
    if (-not (Test-Path -LiteralPath $repoPath -PathType Container)) {
        throw "Repository folder was not found: $repoPath"
    }

    Set-Location -LiteralPath $repoPath

    if (-not (Test-Path -LiteralPath (Join-Path $repoPath '.git'))) {
        throw "The configured folder is not a Git repository: $repoPath"
    }

    $null = Get-Command git -ErrorAction Stop

    $currentBranch = (& git branch --show-current).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to determine the current Git branch.'
    }

    if ($currentBranch -ne $branchName) {
        throw "The repository is on branch '$currentBranch', not '$branchName'. No files were changed."
    }

    $unmergedFiles = @(& git diff --name-only --diff-filter=U)
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to check for unresolved merge conflicts.'
    }

    if ($unmergedFiles.Count -gt 0) {
        throw "Unresolved merge conflicts exist. Resolve them before syncing.`n$($unmergedFiles -join "`n")"
    }

    $statusBefore = @(& git status --short)
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to read Git status.'
    }

    Write-Host 'Repository:' $repoPath
    Write-Host 'Branch:' $branchName
    Write-Host ''
    Write-Host 'Detected local changes:' -ForegroundColor Yellow

    if ($statusBefore.Count -eq 0) {
        Write-Host '  No local changes.'
    }
    else {
        $statusBefore | ForEach-Object { Write-Host "  $_" }
    }

    if ($DryRun) {
        Write-Host ''
        Write-Host 'Dry run only. No files were staged, committed, pulled, or pushed.' -ForegroundColor Green
        exit 0
    }

    Start-Transcript -LiteralPath $logPath -Append | Out-Null
    $transcriptStarted = $true

    Invoke-GitCommand -Arguments @('add', '-A')

    & git diff --cached --quiet
    $stagedDiffExitCode = $LASTEXITCODE

    if ($stagedDiffExitCode -eq 1) {
        $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
        $commitMessage = "chore: sync project $timestamp"
        Invoke-GitCommand -Arguments @('commit', '-m', $commitMessage)
    }
    elseif ($stagedDiffExitCode -ne 0) {
        throw 'Unable to determine whether staged changes exist.'
    }

    Invoke-GitCommand -Arguments @('pull', '--rebase', $remoteName, $branchName)
    Invoke-GitCommand -Arguments @('push', $remoteName, $branchName)

    $latestCommit = (& git log -1 --pretty=format:'%h %s').Trim()
    if ($LASTEXITCODE -ne 0) {
        $latestCommit = 'Push completed; latest commit could not be displayed.'
    }

    if ($transcriptStarted) {
        Stop-Transcript | Out-Null
        $transcriptStarted = $false
    }

    Show-ResultMessage -Type Information -Message (
        "GitHub synchronization completed successfully.`n`n" +
        "Branch: $branchName`n" +
        "Latest commit: $latestCommit"
    )
}
catch {
    $errorMessage = $_.Exception.Message

    $gitDirectory = Join-Path $repoPath '.git'
    $rebaseInProgress =
        (Test-Path -LiteralPath (Join-Path $gitDirectory 'rebase-merge')) -or
        (Test-Path -LiteralPath (Join-Path $gitDirectory 'rebase-apply'))

    if ($rebaseInProgress) {
        Write-Host ''
        Write-Host 'A rebase conflict occurred. Aborting the rebase to preserve the local commit.' -ForegroundColor Yellow
        & git rebase --abort
    }

    if ($transcriptStarted) {
        Stop-Transcript | Out-Null
        $transcriptStarted = $false
    }

    Write-Host ''
    Write-Host $errorMessage -ForegroundColor Red

    if (-not $DryRun) {
        Show-ResultMessage -Type Error -Message (
            "GitHub synchronization failed.`n`n" +
            "$errorMessage`n`n" +
            "Log file: $logPath"
        )
    }

    exit 1
}
