param(
    [ValidateSet('build', 'test', 'run', 'probe-main', 'probe-sub', 'detect', 'ai-test', 'ai-scheduler-test', 'capabilities', 'engine', 'engine-status', 'engine-stop', 'camera-reconnect-test', 'package-engine')]
    [string]$Action = 'build',
    [string]$MsysRoot = 'C:\msys64'
)

$ErrorActionPreference = 'Stop'
$taskProjectRoot = Split-Path -Parent $PSScriptRoot
$taskNativePrefix = Join-Path $MsysRoot 'ucrt64'
if (-not (Test-Path -LiteralPath (Join-Path $taskNativePrefix 'bin\g++.exe'))) {
    throw "MSYS2 UCRT64 compiler not found at $taskNativePrefix. See docs/BUILD.md."
}
$env:PATH = (Join-Path $taskNativePrefix 'bin') + ';' + $env:PATH
$env:PKG_CONFIG_PATH = Join-Path $taskNativePrefix 'lib\pkgconfig'
Push-Location -LiteralPath $taskProjectRoot
try {
    switch ($Action) {
        'build' {
            & cmake --preset dev "-DCMAKE_PREFIX_PATH=$($taskNativePrefix.Replace('\','/'))"
            if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
            & cmake --build --preset dev -j 6
            if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
        }
        'test' {
            & ctest --preset dev
            if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
        }
        'run' {
            # This is the interactive app the user asked to open.
            & (Join-Path $taskProjectRoot 'OpenNVR.cmd')
        }
        'capabilities' { & '.\build\dev\opennvr_probe.exe' --capabilities }
        'engine' {
            $taskEngineDirectory = Join-Path $taskProjectRoot '.local\engine'
            New-Item -ItemType Directory -Path $taskEngineDirectory -Force | Out-Null
            Start-Process -FilePath (Join-Path $taskProjectRoot 'build\dev\opennvr_engine.exe') -ArgumentList @('--console','--data','.local/engine') -WorkingDirectory $taskProjectRoot -WindowStyle Hidden -RedirectStandardError (Join-Path $taskEngineDirectory 'engine-stderr.log')
        }
        'engine-status' { & '.\build\dev\opennvr_engine.exe' --request status --data '.local/engine' }
        'engine-stop' { & '.\build\dev\opennvr_engine.exe' --request shutdown --data '.local/engine' }
        'camera-reconnect-test' { & python tools/test_camera_reconnect.py --engine build/dev/opennvr_engine.exe --root .; if ($LASTEXITCODE -ne 0) { throw 'Camera reconnect validation failed.' } }
        'package-engine' { & (Join-Path $PSScriptRoot 'package-engine.ps1') -MsysRoot $MsysRoot }
        'ai-test' {
            $taskAiProcess = Start-Process -FilePath (Join-Path $taskProjectRoot 'build\dev\opennvr.exe') -ArgumentList '--ai-self-test' -WorkingDirectory $taskProjectRoot -WindowStyle Hidden -Wait -PassThru
            Get-Content -LiteralPath '.local\ai-selftest.json' -Raw
            if ($taskAiProcess.ExitCode -ne 0) { throw 'AI self-test failed.' }
        }
        'ai-scheduler-test' {
            $taskAiProcess = Start-Process -FilePath (Join-Path $taskProjectRoot 'build\dev\opennvr.exe') -ArgumentList '--ai-scheduler-test' -WorkingDirectory $taskProjectRoot -WindowStyle Hidden -Wait -PassThru
            Get-Content -LiteralPath '.local\ai-scheduler-validation.json' -Raw
            if ($taskAiProcess.ExitCode -ne 0) { throw 'Shared AI scheduler test failed.' }
        }
        { $_ -in 'probe-main','probe-sub','detect' } {
            $taskProbeArgs = @('--camera-probe', '--seconds', '14', '--report', '.local/probe-result.json')
            if ($Action -eq 'probe-main') { $taskProbeArgs += '--record' }
            elseif ($Action -eq 'probe-sub') { $taskProbeArgs += '--substream' }
            else { $taskProbeArgs = @('--camera-probe','--detect','--seconds','60','--report','.local/probe-result.json') }
            $taskProbeProcess = Start-Process -FilePath (Join-Path $taskProjectRoot 'build\dev\opennvr.exe') -ArgumentList $taskProbeArgs -WorkingDirectory $taskProjectRoot -WindowStyle Hidden -Wait -PassThru
            if (Test-Path -LiteralPath '.local\probe-result.json') { Get-Content -LiteralPath '.local\probe-result.json' -Raw }
            if ($taskProbeProcess.ExitCode -ne 0) { throw 'Camera probe failed.' }
        }
    }
} finally {
    Pop-Location
}
