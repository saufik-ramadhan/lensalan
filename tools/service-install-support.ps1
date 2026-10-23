# Shared installer operations; importing this file does not change the system.
function Set-OpenNvrServiceRegistration([string]$BinaryPath, $Existing) {
    # Pass PathName as a CIM string. Legacy PowerShell native argument marshalling
    # removes the executable/path quotes when binPath is passed through sc.exe.
    $taskArguments = @{
        PathName = $BinaryPath
        StartMode = 'Automatic'
        StartName = 'NT AUTHORITY\LocalService'
    }
    if ($Existing) {
        $taskResult = Invoke-CimMethod -InputObject $Existing -MethodName Change -Arguments $taskArguments
    }
    else {
        $taskArguments.Name = 'OpenNVR'
        $taskArguments.DisplayName = 'OpenNVR recording engine'
        $taskArguments.ServiceType = [byte]16
        $taskArguments.ErrorControl = [byte]1
        $taskArguments.DesktopInteract = $false
        $taskResult = Invoke-CimMethod -ClassName Win32_Service -MethodName Create -Arguments $taskArguments
    }
    if ($null -eq $taskResult -or $null -eq $taskResult.ReturnValue -or $taskResult.ReturnValue -ne 0) {
        throw "Windows service registration failed (Win32_Service return code: $($taskResult.ReturnValue))."
    }
}

function Assert-OpenNvrResumeState([string]$PackageRoot, [string]$InstallRoot, [string]$DataRoot, [string]$OperatorSid) {
    # Directory ownership, protected ACLs and reparse points must be checked by
    # the caller before using these files. Resume never copies/provisions again.
    foreach ($taskFile in @(
        (Join-Path $InstallRoot 'bin\opennvr_engine.exe'),
        (Join-Path $InstallRoot 'runtime-manifest.json'),
        (Join-Path $DataRoot '.acl-provisioned'),
        (Join-Path $DataRoot 'config\engine.json'),
        (Join-Path $DataRoot 'config\camera.json'))) {
        if (-not (Test-Path -LiteralPath $taskFile -PathType Leaf)) {
            throw "Cannot resume: required provisioned file is missing: $taskFile"
        }
    }
    $taskSourceHash = (Get-FileHash -LiteralPath (Join-Path $PackageRoot 'bin\opennvr_engine.exe') -Algorithm SHA256).Hash
    $taskInstalledHash = (Get-FileHash -LiteralPath (Join-Path $InstallRoot 'bin\opennvr_engine.exe') -Algorithm SHA256).Hash
    if ($taskSourceHash -ne $taskInstalledHash) { throw 'Cannot resume: installed engine differs from the supplied bundle.' }
    $taskSettings = Get-Content -LiteralPath (Join-Path $DataRoot 'config\engine.json') -Raw | ConvertFrom-Json
    if ($taskSettings.operatorSid -ne $OperatorSid) { throw 'Cannot resume: OperatorSid differs from the provisioned configuration.' }
}
