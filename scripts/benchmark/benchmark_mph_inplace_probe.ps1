param(
    [int]$N = 16,
    [int]$LimitSeconds = 120,
    [string]$Executable = "",
    [string]$Tag = "single"
)

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$auditRoot = Join-Path $repoRoot "benchmarks\audit_logs"
New-Item -ItemType Directory -Force -Path (Join-Path $auditRoot "stdout"), (Join-Path $auditRoot "stderr") | Out-Null

if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $repoRoot "build\moto_probe_mph_inplace.exe"
} elseif (-not [System.IO.Path]::IsPathRooted($Executable)) {
    $Executable = [System.IO.Path]::GetFullPath((Join-Path ([string](Get-Location)) $Executable))
}
if (-not (Test-Path -LiteralPath $Executable)) {
    throw "Executable not found: $Executable"
}

$stdout = Join-Path $auditRoot "stdout\mph_inplace_${Tag}_n$N.stdout.txt"
$stderr = Join-Path $auditRoot "stderr\mph_inplace_${Tag}_n$N.stderr.txt"
$watch = [System.Diagnostics.Stopwatch]::StartNew()
$process = Start-Process -FilePath $Executable `
    -ArgumentList @("$N", "$LimitSeconds") -PassThru -NoNewWindow `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$null = $process.Handle
[int64]$maxWorkingSet = 0
[int64]$maxPrivate = 0
while (-not $process.HasExited) {
    $process.Refresh()
    if ($process.WorkingSet64 -gt $maxWorkingSet) {
        $maxWorkingSet = $process.WorkingSet64
    }
    if ($process.PrivateMemorySize64 -gt $maxPrivate) {
        $maxPrivate = $process.PrivateMemorySize64
    }
    Start-Sleep -Milliseconds 25
}
$process.WaitForExit()
$process.Refresh()
$watch.Stop()

$outputText = [string](Get-Content $stdout -Raw)
$status = [regex]::Match($outputText, '(?m)^status = (.+)$').Groups[1].Value.Trim()
$paths = [regex]::Match($outputText, '(?m)^paths = (.+)$').Groups[1].Value.Trim()
$internalElapsed = [regex]::Match($outputText, '(?m)^elapsed = ([0-9.]+)').Groups[1].Value
$visits = [regex]::Match($outputText, '(?m)^state visits = ([0-9]+)').Groups[1].Value
$threads = [regex]::Match($outputText, '(?m)^threads = ([0-9]+)').Groups[1].Value

[pscustomobject]@{
    tag = $Tag
    n = $N
    exit_code = [int]$process.ExitCode
    status = $status
    paths = $paths
    internal_elapsed_seconds = $internalElapsed
    wall_elapsed_seconds = [math]::Round($watch.Elapsed.TotalSeconds, 6)
    state_visits = $visits
    threads = $threads
    peak_working_set_bytes = $maxWorkingSet
    peak_private_bytes = $maxPrivate
    retained_stdout = $stdout
    retained_stderr = $stderr
}

if (Test-Path $stderr) {
    $errorText = [string](Get-Content $stderr -Raw)
    if (-not [string]::IsNullOrWhiteSpace($errorText)) {
        Write-Error $errorText
    }
}

# Temporary outputs are intentionally retained for audit and follow-up analysis.
