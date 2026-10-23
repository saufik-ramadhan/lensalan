param(
    [string]$MsysRoot = 'C:\msys64',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$taskProjectRoot = Split-Path -Parent $PSScriptRoot
$taskPrefix = Join-Path $MsysRoot 'ucrt64'
$taskArtifacts = Join-Path $taskProjectRoot 'artifacts'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $taskArtifacts ('engine-bundle-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$taskTarget = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $taskTarget.StartsWith([IO.Path]::GetFullPath($taskArtifacts) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Engine bundles must be created inside this project artifacts directory.'
}
if (Test-Path -LiteralPath $taskTarget) { throw 'Choose a new output directory; existing bundles are never overwritten.' }
$taskEngine = Join-Path $taskProjectRoot 'build\dev\opennvr_engine.exe'
$taskProbe = Join-Path $taskProjectRoot 'build\dev\opennvr_probe.exe'
$taskScanner = Join-Path $taskPrefix 'libexec\gstreamer-1.0\gst-plugin-scanner.exe'
$taskObjdump = Join-Path $taskPrefix 'bin\objdump.exe'
$taskOrt = Join-Path $taskProjectRoot '.deps\onnxruntime-win-x64-gpu-1.23.2'
foreach ($taskRequired in @($taskEngine,$taskProbe,$taskScanner,$taskObjdump,(Join-Path $taskOrt 'lib\onnxruntime.dll'))) {
    if (-not (Test-Path -LiteralPath $taskRequired)) { throw "Missing build/runtime prerequisite: $taskRequired" }
}
$taskBin = Join-Path $taskTarget 'bin'
$taskPlugins = Join-Path $taskTarget 'lib\gstreamer-1.0'
$taskScannerTarget = Join-Path $taskTarget 'libexec\gstreamer-1.0'
New-Item -ItemType Directory -Path $taskBin,$taskPlugins,$taskScannerTarget -Force | Out-Null
Copy-Item -LiteralPath $taskEngine,$taskProbe -Destination $taskBin
Copy-Item -LiteralPath $taskScanner -Destination $taskScannerTarget
$taskPluginFiles = Get-ChildItem -LiteralPath (Join-Path $taskPrefix 'lib\gstreamer-1.0') -Filter '*.dll' -File
foreach ($taskPlugin in $taskPluginFiles) { Copy-Item -LiteralPath $taskPlugin.FullName -Destination $taskPlugins }

$taskQueue = [Collections.Generic.Queue[string]]::new()
$taskSeen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$taskQueue.Enqueue($taskEngine); $taskQueue.Enqueue($taskProbe); $taskQueue.Enqueue($taskScanner)
foreach ($taskPlugin in $taskPluginFiles) { $taskQueue.Enqueue($taskPlugin.FullName) }
while ($taskQueue.Count -gt 0) {
    $taskBinary = $taskQueue.Dequeue()
    if (-not $taskSeen.Add($taskBinary)) { continue }
    $taskImports = & $taskObjdump -p $taskBinary | Select-String 'DLL Name:\s*(\S+)'
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect binary imports: $taskBinary" }
    foreach ($taskImport in $taskImports) {
        $taskName = $taskImport.Matches[0].Groups[1].Value
        $taskDependency = Join-Path $taskPrefix ('bin\' + $taskName)
        if (Test-Path -LiteralPath $taskDependency) {
            if (-not (Test-Path -LiteralPath (Join-Path $taskBin $taskName))) { Copy-Item -LiteralPath $taskDependency -Destination $taskBin }
            $taskQueue.Enqueue($taskDependency)
        }
    }
}

$taskOrtTarget = Join-Path $taskTarget '.deps\onnxruntime-win-x64-gpu-1.23.2'
New-Item -ItemType Directory -Path (Join-Path $taskOrtTarget 'lib') -Force | Out-Null
foreach ($taskName in @('onnxruntime.dll','onnxruntime_providers_shared.dll','onnxruntime_providers_cuda.dll')) {
    Copy-Item -LiteralPath (Join-Path $taskOrt ('lib\' + $taskName)) -Destination (Join-Path $taskOrtTarget 'lib')
}
Get-ChildItem -LiteralPath $taskOrt -File | Where-Object { $_.Name -match 'LICENSE|NOTICE|VERSION' } | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $taskOrtTarget }
$taskCudaRoot = Join-Path $taskProjectRoot '.deps\cuda'
$taskCudaPrefix = [IO.Path]::GetFullPath($taskCudaRoot).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
foreach ($taskPackage in Get-ChildItem -LiteralPath $taskCudaRoot -Directory) {
    $taskFiles = Get-ChildItem -LiteralPath $taskPackage.FullName -Recurse -File | Where-Object { $_.Extension -eq '.dll' -or $_.Name -match 'LICENSE|NOTICE|METADATA' }
    foreach ($taskFile in $taskFiles) {
        # Windows PowerShell 5.1 uses .NET Framework, which has no Path.GetRelativePath.
        if (-not $taskFile.FullName.StartsWith($taskCudaPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "CUDA runtime file is outside the dependency directory: $($taskFile.FullName)"
        }
        $taskRelative = $taskFile.FullName.Substring($taskCudaPrefix.Length)
        $taskDestination = Join-Path $taskTarget ('.deps\cuda\' + $taskRelative)
        New-Item -ItemType Directory -Path (Split-Path -Parent $taskDestination) -Force | Out-Null
        Copy-Item -LiteralPath $taskFile.FullName -Destination $taskDestination
    }
}
$taskModelTarget = Join-Path $taskTarget '.deps\models'
New-Item -ItemType Directory -Path $taskModelTarget -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskProjectRoot '.deps\models\yolox_tiny.onnx'),(Join-Path $taskProjectRoot '.deps\models\YOLOX-LICENSE.txt') -Destination $taskModelTarget
$taskLicenses = Join-Path $taskPrefix 'share\licenses'
if (Test-Path -LiteralPath $taskLicenses) { Copy-Item -LiteralPath $taskLicenses -Destination (Join-Path $taskTarget 'licenses') -Recurse }
$taskManifest = @{
    product='LensaLAN engine development bundle'; version='0.3.0-dev'; createdUtc=[DateTime]::UtcNow.ToString('o')
    qt='6.11.2'; gstreamer='1.28.6'; onnxruntime='1.23.2'; model='YOLOX-Tiny 416'
    modelSha256=(Get-FileHash -LiteralPath (Join-Path $taskModelTarget 'yolox_tiny.onnx') -Algorithm SHA256).Hash.ToLowerInvariant()
    engineSha256=(Get-FileHash -LiteralPath (Join-Path $taskBin 'opennvr_engine.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    runtimeDllCount=@(Get-ChildItem -LiteralPath $taskBin -Filter '*.dll').Count
    pluginCount=$taskPluginFiles.Count
}
$taskManifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskTarget 'runtime-manifest.json') -Encoding utf8
Write-Output "Engine bundle ready: $taskTarget"
Write-Output "Runtime DLLs: $($taskManifest.runtimeDllCount); GStreamer plugins: $($taskManifest.pluginCount)"
