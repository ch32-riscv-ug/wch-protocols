# E063: what Windows sees of the device under test, and a round trip over its
# COM port. Invoked from WSL through powershell.exe; emits one JSON object on
# stdout. Plan and report: README.ja.md
#
# Windows does not expose the USB bus speed as a PnP property, so speed is not
# collected here; step P6 of the plan reads it from the Linux side instead.

param(
  [ValidateSet('enumerate', 'echo')]
  [string]$Action = 'enumerate',
  [string]$Match = 'VID_1209&PID_0002',
  [string]$ComPort = '',
  [string]$Text = 'E063',
  [int]$TimeoutMs = 3000
)

$ErrorActionPreference = 'Stop'
# Device names carry non-ASCII on a localised Windows; pin the output encoding
# so the caller does not have to guess the console code page.
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

function Get-DeviceRecord {
  param($Device)

  # One bulk query: fetching keys one at a time costs seconds per call and made
  # the whole collection take ~1 minute.
  $properties = @{}
  $wanted = @(
    'DEVPKEY_Device_HardwareIds', 'DEVPKEY_Device_CompatibleIds', 'DEVPKEY_Device_Driver',
    'DEVPKEY_Device_Service', 'DEVPKEY_Device_FriendlyName', 'DEVPKEY_Device_BusReportedDeviceDesc',
    'DEVPKEY_Device_LocationInfo', 'DEVPKEY_Device_Parent', 'DEVPKEY_Device_Children',
    'DEVPKEY_Device_DeviceDesc', 'DEVPKEY_Device_Manufacturer', 'DEVPKEY_Device_Address'
  )
  $all = Get-PnpDeviceProperty -InstanceId $Device.InstanceId -ErrorAction SilentlyContinue
  foreach ($entry in $all) {
    if ($wanted -contains $entry.KeyName -and $null -ne $entry.Data) {
      $properties[$entry.KeyName] = $entry.Data
    }
  }

  $com = $null
  $name = $Device.FriendlyName
  if ($name -and $name -match '\((COM\d+)\)') {
    $com = $Matches[1]
  }

  [ordered]@{
    InstanceId   = $Device.InstanceId
    Class        = $Device.Class
    Status       = $Device.Status
    Present      = $Device.Present
    FriendlyName = $name
    ComPort      = $com
    Properties   = $properties
  }
}

if ($Action -eq 'enumerate') {
  $devices = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -like "*$Match*" })
  $records = @($devices | ForEach-Object { Get-DeviceRecord -Device $_ })
  $ports = @($records | Where-Object { $_.ComPort } | ForEach-Object { $_.ComPort })

  $result = [ordered]@{
    Action    = 'enumerate'
    Match     = $Match
    Count     = $records.Count
    ComPorts  = $ports
    Devices   = $records
  }
} else {
  if (-not $ComPort) {
    throw 'echo requires -ComPort'
  }
  $port = New-Object System.IO.Ports.SerialPort $ComPort, 115200, 'None', 8, 'One'
  # The device gates CDC writes on DTR, so the line state has to be asserted.
  $port.DtrEnable = $true
  $port.RtsEnable = $true
  $port.NewLine = "`n"
  $port.ReadTimeout = $TimeoutMs
  $port.WriteTimeout = $TimeoutMs

  $reply = $null
  $errorText = $null
  try {
    $port.Open()
    Start-Sleep -Milliseconds 200
    $port.DiscardInBuffer()
    $port.WriteLine($Text)
    $reply = $port.ReadLine()
  } catch {
    $errorText = $_.Exception.Message
  } finally {
    if ($port.IsOpen) {
      $port.Close()
    }
    $port.Dispose()
  }

  $result = [ordered]@{
    Action  = 'echo'
    ComPort = $ComPort
    Sent    = $Text
    Reply   = $reply
    Error   = $errorText
  }
}

$result | ConvertTo-Json -Depth 6 -Compress
