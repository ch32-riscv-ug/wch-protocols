# SPDX-License-Identifier: MIT
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"
$pattern = "VID_1209&PID_0001"

function Read-PnpProperty {
    param([string]$InstanceId, [string]$KeyName)
    try {
        return (Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName $KeyName).Data
    }
    catch {
        return $null
    }
}

$devices = Get-PnpDevice |
    Where-Object { $_.InstanceId -like "*$pattern*" } |
    ForEach-Object {
        [ordered]@{
            FriendlyName = $_.FriendlyName
            Class = $_.Class
            Status = $_.Status
            Present = $_.Present
            InstanceId = $_.InstanceId
            HardwareIds = Read-PnpProperty $_.InstanceId "DEVPKEY_Device_HardwareIds"
            CompatibleIds = Read-PnpProperty $_.InstanceId "DEVPKEY_Device_CompatibleIds"
            Parent = Read-PnpProperty $_.InstanceId "DEVPKEY_Device_Parent"
            DriverDescription = Read-PnpProperty $_.InstanceId "DEVPKEY_Device_DriverDesc"
            DriverVersion = Read-PnpProperty $_.InstanceId "DEVPKEY_Device_DriverVersion"
            BusReportedDescription = Read-PnpProperty $_.InstanceId "DEVPKEY_Device_BusReportedDeviceDesc"
            Location = Read-PnpProperty $_.InstanceId "DEVPKEY_Device_LocationInfo"
        }
    }

$snapshot = [ordered]@{
    Timestamp = (Get-Date).ToUniversalTime().ToString("o")
    Windows = Get-ComputerInfo | Select-Object WindowsProductName, WindowsVersion, OsBuildNumber, OsArchitecture
    Filter = $pattern
    Devices = @($devices)
}

$directory = Split-Path -Parent $OutputPath
if ($directory) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
$snapshot | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 $OutputPath
Write-Host "Wrote $OutputPath"

