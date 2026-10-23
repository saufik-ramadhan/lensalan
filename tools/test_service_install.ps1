# Run under Windows PowerShell 5.1 and PowerShell 7; never registers a service.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'service-install-support.ps1')
$taskSchema = Get-CimClass -ClassName Win32_Service
$script:taskCall = $null
$script:taskReturnValue = 0
function Invoke-CimMethod($InputObject, [string]$ClassName, [string]$MethodName, [hashtable]$Arguments) {
    $script:taskCall = @{ InputObject=$InputObject; ClassName=$ClassName; MethodName=$MethodName; Arguments=$Arguments }
    [pscustomobject]@{ ReturnValue=$script:taskReturnValue }
}
function Assert-InstallTest([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-InstallFailure([scriptblock]$Action, [string]$Message) {
    $taskFailed = $false
    try { & $Action } catch { $taskFailed = $true }
    Assert-InstallTest $taskFailed $Message
}
$taskCommandLine = '"C:\Program Files\OpenNVR\bin\opennvr_engine.exe" --service --root "C:\Program Files\OpenNVR" --data "C:\ProgramData\OpenNVR"'
Set-OpenNvrServiceRegistration -BinaryPath $taskCommandLine -Existing $null
Assert-InstallTest ($script:taskCall.MethodName -eq 'Create' -and $script:taskCall.ClassName -eq 'Win32_Service') 'Create was not dispatched to Win32_Service'
Assert-InstallTest ($script:taskCall.Arguments.PathName -ceq $taskCommandLine) 'Quoted service path was changed'
Assert-InstallTest ($script:taskCall.Arguments.StartName -eq 'NT AUTHORITY\LocalService') 'Wrong service account'
Assert-InstallTest ($script:taskCall.Arguments.StartMode -eq 'Automatic') 'Wrong startup mode'
Assert-InstallTest ($script:taskCall.Arguments.ServiceType -eq 16 -and -not $script:taskCall.Arguments.DesktopInteract) 'Wrong service type'
foreach ($taskEntry in $script:taskCall.Arguments.GetEnumerator()) {
    $taskParameter = $taskSchema.CimClassMethods['Create'].Parameters[$taskEntry.Key]
    Assert-InstallTest ($null -ne $taskParameter) "Unsupported CIM parameter: $($taskEntry.Key)"
    switch ($taskParameter.CimType.ToString()) {
        String { Assert-InstallTest ($taskEntry.Value -is [string]) 'Expected CIM string' }
        UInt8 { Assert-InstallTest ($taskEntry.Value -is [byte]) 'Expected CIM uint8' }
        Boolean { Assert-InstallTest ($taskEntry.Value -is [bool]) 'Expected CIM boolean' }
        default { throw 'Unexpected CIM parameter type' }
    }
}
$taskExisting = [pscustomobject]@{ Name='OpenNVR' }
Set-OpenNvrServiceRegistration -BinaryPath $taskCommandLine -Existing $taskExisting
Assert-InstallTest ($script:taskCall.MethodName -eq 'Change' -and $script:taskCall.InputObject -eq $taskExisting) 'Existing service was not updated'
Assert-InstallTest ($script:taskCall.Arguments.PathName -ceq $taskCommandLine) 'Update lost quoted path'
Assert-InstallTest (-not $script:taskCall.Arguments.ContainsKey('Name')) 'Change received a Create-only parameter'
$script:taskReturnValue = 2
Assert-InstallFailure { Set-OpenNvrServiceRegistration -BinaryPath $taskCommandLine -Existing $null } 'CIM access denied did not stop installation'
$script:taskReturnValue = 0

$taskFixture = Join-Path (Split-Path -Parent $PSScriptRoot) ('build\test-output\installer-' + [Guid]::NewGuid().ToString('N'))
$taskPackage = Join-Path $taskFixture 'package'
$taskInstall = Join-Path $taskFixture 'runtime\OpenNVR'
$taskData = Join-Path $taskFixture 'data\OpenNVR'
$null = New-Item -ItemType Directory -Path (Join-Path $taskPackage 'bin'), (Join-Path $taskInstall 'bin'), (Join-Path $taskData 'config') -Force
Set-Content -LiteralPath (Join-Path $taskPackage 'bin\opennvr_engine.exe') -Value 'test fixture; never executed' -Encoding ascii
Copy-Item -LiteralPath (Join-Path $taskPackage 'bin\opennvr_engine.exe') -Destination (Join-Path $taskInstall 'bin')
Set-Content -LiteralPath (Join-Path $taskInstall 'runtime-manifest.json') -Value '{}' -Encoding ascii
Set-Content -LiteralPath (Join-Path $taskData '.acl-provisioned') -Value 'test fixture' -Encoding ascii
Set-Content -LiteralPath (Join-Path $taskData 'config\engine.json') -Value '{"operatorSid":"test-operator","retentionDays":37}' -Encoding ascii
Set-Content -LiteralPath (Join-Path $taskData 'config\camera.json') -Value 'opaque test fixture; never decrypted' -Encoding ascii
$taskBefore = @(Get-ChildItem -LiteralPath $taskFixture -Recurse -File | Get-FileHash | ForEach-Object { $_.Hash }) -join ','
Assert-OpenNvrResumeState $taskPackage $taskInstall $taskData 'test-operator'
$taskAfter = @(Get-ChildItem -LiteralPath $taskFixture -Recurse -File | Get-FileHash | ForEach-Object { $_.Hash }) -join ','
Assert-InstallTest ($taskBefore -eq $taskAfter) 'Resume validation modified installed files'
Assert-InstallFailure { Assert-OpenNvrResumeState $taskPackage $taskInstall $taskData 'wrong-operator' } 'Resume accepted another operator'
Add-Content -LiteralPath (Join-Path $taskPackage 'bin\opennvr_engine.exe') -Value 'different build'
Assert-InstallFailure { Assert-OpenNvrResumeState $taskPackage $taskInstall $taskData 'test-operator' } 'Resume accepted a different engine'
Copy-Item -LiteralPath (Join-Path $taskInstall 'bin\opennvr_engine.exe') -Destination (Join-Path $taskPackage 'bin') -Force
Rename-Item -LiteralPath (Join-Path $taskData 'config\camera.json') -NewName 'credential.fixture'
Assert-InstallFailure { Assert-OpenNvrResumeState $taskPackage $taskInstall $taskData 'test-operator' } 'Resume accepted missing provisioned files'
Write-Output "PASS PowerShell $($PSVersionTable.PSVersion): quoted Create/Change arguments, CIM schema/error handling, resume preservation and invalid-state rejection. No Windows services changed."
