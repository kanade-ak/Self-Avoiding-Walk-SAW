param(
    [int]$N = 15,
    [string]$Executable = ""
)

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$auditRoot = Join-Path $repoRoot "benchmarks\audit_logs"
New-Item -ItemType Directory -Force -Path (Join-Path $auditRoot "stdout"), (Join-Path $auditRoot "stderr") | Out-Null

if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $repoRoot "build\moto_probe_mph_reference_port.exe"
} elseif (-not [System.IO.Path]::IsPathRooted($Executable)) {
    $Executable = [System.IO.Path]::GetFullPath((Join-Path ([string](Get-Location)) $Executable))
}
if (-not (Test-Path -LiteralPath $Executable)) {
    throw "Executable not found: $Executable"
}

$stdout = Join-Path $auditRoot "stdout\mph_reference_n$N.stdout.txt"
$stderr = Join-Path $auditRoot "stderr\mph_reference_n$N.stderr.txt"
$watch = [System.Diagnostics.Stopwatch]::StartNew()
$process = Start-Process -FilePath $Executable -ArgumentList @("-q", "$N") `
    -PassThru -NoNewWindow -RedirectStandardOutput $stdout `
    -RedirectStandardError $stderr
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

[pscustomobject]@{
    n = $N
    exit_code = [int]$process.ExitCode
    paths = (Get-Content $stdout -Raw).Trim()
    elapsed_seconds = [math]::Round($watch.Elapsed.TotalSeconds, 6)
    peak_working_set_bytes = $maxWorkingSet
    peak_private_bytes = $maxPrivate
}

if (Test-Path $stderr) {
    $errorText = [string](Get-Content $stderr -Raw)
    if (-not [string]::IsNullOrWhiteSpace($errorText)) {
        Write-Error $errorText
    }
}

# Intentionally retain the temporary stdout/stderr files for auditability.
