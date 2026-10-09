$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
function Run-Git {
    & git @args
    if ($LASTEXITCODE -ne 0) { throw "Git command failed (exit $LASTEXITCODE)." }
}
try {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw 'Install Git for Windows first: https://git-scm.com/download/win'
    }
    if (-not (Test-Path '.git')) {
        throw 'Open a Git clone of EE_Proj. For an extracted ZIP run GIT_SETUP.bat once.'
    }
    Run-Git status --short
    $message = Read-Host 'Commit message (Enter = Update EE_Proj)'
    if ([string]::IsNullOrWhiteSpace($message)) { $message = 'Update EE_Proj' }
    Run-Git add --all
    & git diff --cached --quiet
    if ($LASTEXITCODE -eq 1) {
        $commitFile = [System.IO.Path]::GetTempFileName()
        try {
            [System.IO.File]::WriteAllText($commitFile, $message, [System.Text.UTF8Encoding]::new($false))
            Run-Git commit -F $commitFile
        } finally { Remove-Item $commitFile -ErrorAction SilentlyContinue }
    } elseif ($LASTEXITCODE -ne 0) {
        throw 'Could not inspect staged changes.'
    }
    # Never force-push or auto-rewrite local commits. A rejected push preserves work.
    Run-Git push --set-upstream origin HEAD
    Write-Host 'COMMIT / PUSH COMPLETE'
} catch {
    Write-Host $_ -ForegroundColor Red
    Write-Host 'Your files and local commits are preserved. Resolve the Git error before retrying.'
    exit 1
}
