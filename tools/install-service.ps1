param(
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'OpenNVR'),
    [string]$DataRoot = (Join-Path $env:ProgramData 'OpenNVR'),
    [string]$SourceCredential = '',
    [string]$OperatorSid = '',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$taskProjectRoot = Split-Path -Parent $PSScriptRoot
$taskPackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$taskInstallRoot = [IO.Path]::GetFullPath($InstallRoot)
$taskDataRoot = [IO.Path]::GetFullPath($DataRoot)
if (-not $SourceCredential) { $SourceCredential = Join-Path $taskProjectRoot '.local\camera.json' }
if (-not $OperatorSid) { $OperatorSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value }
$null = [Security.Principal.SecurityIdentifier]::new($OperatorSid)
if (-not (Test-Path -LiteralPath (Join-Path $taskPackageRoot 'runtime-manifest.json')) -or
    -not (Test-Path -LiteralPath (Join-Path $taskPackageRoot 'bin\opennvr_engine.exe'))) { throw 'Package is not a prepared OpenNVR engine bundle.' }
if (-not (Test-Path -LiteralPath $SourceCredential)) { throw 'Configure a local camera credential before service installation.' }
foreach ($taskDirectory in @($taskInstallRoot,$taskDataRoot)) {
    if ([IO.Path]::GetPathRoot($taskDirectory) -eq $taskDirectory -or (Split-Path -Leaf $taskDirectory) -ne 'OpenNVR') {
        throw 'InstallRoot and DataRoot must be dedicated OpenNVR directories.'
    }
    if ((Test-Path -LiteralPath $taskDirectory) -and ((Get-Item -LiteralPath $taskDirectory).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Service directories must not be junctions or symlinks.'
    }
}
if ($DryRun) {
    Write-Output "Validated service install plan: $taskPackageRoot -> $taskInstallRoot"
    Write-Output "Data: $taskDataRoot; service identity: LocalService; operator: $OperatorSid"
    Write-Output 'No files, ACLs, credentials or Windows services were changed.'
    return
}
$taskPrincipal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $taskPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Windows requires Administrator rights to install a service. Open an elevated PowerShell and rerun this reviewed installer.'
}
$taskExe = Join-Path $taskInstallRoot 'bin\opennvr_engine.exe'
$taskExisting = Get-CimInstance Win32_Service -Filter "Name='OpenNVR'"
if ($taskExisting -and -not $taskExisting.PathName.StartsWith('"' + $taskExe + '"', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'An unrelated OpenNVR service already exists. Refusing to modify it.'
}
if (-not $taskExisting -and ((Test-Path -LiteralPath $taskInstallRoot) -or (Test-Path -LiteralPath $taskDataRoot))) {
    throw 'For a first install, dedicated target directories must not already exist. Review any previous or unrelated data before retrying; this installer never deletes it.'
}
if ($taskExisting) {
    foreach ($taskDirectory in @($taskInstallRoot,$taskDataRoot,(Join-Path $taskDataRoot 'config'))) {
        $taskExistingAcl = Get-Acl -LiteralPath $taskDirectory
        $taskOwnerSid = $taskExistingAcl.GetOwner([Security.Principal.SecurityIdentifier]).Value
        if (-not $taskExistingAcl.AreAccessRulesProtected -or $taskOwnerSid -notin @('S-1-5-18','S-1-5-32-544')) {
            throw 'Existing service directories are not protected/administrator-owned. Refusing an elevated overwrite.'
        }
        $taskLinks = Get-ChildItem -LiteralPath $taskDirectory -Force -Recurse | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1
        if ($taskLinks) { throw 'Reparse points are not allowed in service deployment/data directories.' }
    }
}
if ($taskExisting) { Stop-Service -Name OpenNVR; (Get-Service OpenNVR).WaitForStatus('Stopped',[TimeSpan]::FromSeconds(45)) }

function Set-OpenNvrDirectoryAcl([string]$Path, [bool]$Data, [bool]$Config = $false) {
    $taskAcl = [Security.AccessControl.DirectorySecurity]::new()
    $taskAcl.SetAccessRuleProtection($true,$false)
    $taskAcl.SetOwner([Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'))
    $taskRules = @(@('S-1-5-18','FullControl'),@('S-1-5-32-544','FullControl'))
    if ($Config) { $taskRules += ,@('S-1-5-19','ReadAndExecute'); $taskRules += ,@($OperatorSid,'ReadAndExecute') }
    elseif ($Data) { $taskRules += ,@('S-1-5-19','Modify'); $taskRules += ,@($OperatorSid,'ReadAndExecute') }
    else { $taskRules += ,@('S-1-5-32-545','ReadAndExecute'); $taskRules += ,@('S-1-5-19','ReadAndExecute') }
    foreach ($taskRule in $taskRules) {
        $taskIdentity = [Security.Principal.SecurityIdentifier]::new($taskRule[0])
        $taskAccess = [Security.AccessControl.FileSystemRights]$taskRule[1]
        $taskAcl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($taskIdentity,$taskAccess,
            [Security.AccessControl.InheritanceFlags]'ContainerInherit,ObjectInherit',[Security.AccessControl.PropagationFlags]::None,
            [Security.AccessControl.AccessControlType]::Allow))
    }
    if (Test-Path -LiteralPath $Path) { Set-Acl -LiteralPath $Path -AclObject $taskAcl }
    elseif ($PSVersionTable.PSEdition -eq 'Desktop') {
        # .NET Framework exposes this overload directly; the extension type is absent.
        [IO.DirectoryInfo]::new($Path).Create($taskAcl)
    }
    else { [IO.FileSystemAclExtensions]::Create([IO.DirectoryInfo]::new($Path),$taskAcl) }
}
Set-OpenNvrDirectoryAcl $taskInstallRoot $false
Set-OpenNvrDirectoryAcl $taskDataRoot $true
Set-OpenNvrDirectoryAcl (Join-Path $taskDataRoot 'config') $true $true
Start-Transcript -Path (Join-Path $taskDataRoot 'install.log') -Append | Out-Null
foreach ($taskItem in Get-ChildItem -LiteralPath $taskPackageRoot -Force) { Copy-Item -LiteralPath $taskItem.FullName -Destination $taskInstallRoot -Recurse -Force }
Set-Content -LiteralPath (Join-Path $taskDataRoot '.acl-provisioned') -Value 'Created by the OpenNVR service installer; native provisioning also verifies the DACL.' -Encoding utf8
& $taskExe --provision-service --root $taskInstallRoot --data $taskDataRoot --source-credential $SourceCredential --operator-sid $OperatorSid
if ($LASTEXITCODE -ne 0) { throw 'Credential provisioning failed; the service has not been started.' }
$taskBinaryPath = '"{0}" --service --root "{1}" --data "{2}"' -f $taskExe,$taskInstallRoot,$taskDataRoot
if ($taskExisting) { & sc.exe config OpenNVR binPath= $taskBinaryPath start= delayed-auto obj= 'NT AUTHORITY\LocalService' }
else { & sc.exe create OpenNVR binPath= $taskBinaryPath start= delayed-auto obj= 'NT AUTHORITY\LocalService' DisplayName= 'OpenNVR recording engine' }
if ($LASTEXITCODE -ne 0) { throw 'Windows service registration failed.' }
& sc.exe failure OpenNVR reset= 86400 actions= restart/5000/restart/15000/restart/60000
if ($LASTEXITCODE -ne 0) { throw 'Service recovery policy configuration failed.' }
& sc.exe failureflag OpenNVR 1
if ($LASTEXITCODE -ne 0) { throw 'Service non-crash failure recovery configuration failed.' }
Start-Service -Name OpenNVR
(Get-Service OpenNVR).WaitForStatus('Running',[TimeSpan]::FromSeconds(45))
Write-Output 'OpenNVR service installed and started. Check --request status to verify camera/AI readiness.'
Stop-Transcript | Out-Null
