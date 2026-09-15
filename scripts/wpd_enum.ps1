# wpd_enum.ps1 - enumerate MTP device objects through the WPD API
$dir = Join-Path $env:TEMP 'wpd_pkgs'
$dlls = @(
 "$dir\vanara.core\lib\net48\Vanara.Core.dll",
 "$dir\vanara.pinvoke.ole\lib\net48\Vanara.PInvoke.Ole.dll",
 "$dir\vanara.pinvoke.setupapi\lib\net48\Vanara.PInvoke.SetupAPI.dll",
 "$dir\vanara.pinvoke.portabledeviceapi\lib\net48\Vanara.PInvoke.PortableDeviceApi.dll",
 "$dir\microsoft.win32.registry\lib\net461\Microsoft.Win32.Registry.dll",
 "$dir\system.memory\lib\net462\System.Memory.dll",
 "$dir\system.runtime.compilerservices.unsafe\lib\net462\System.Runtime.CompilerServices.Unsafe.dll",
 "$dir\system.security.accesscontrol\lib\net461\System.Security.AccessControl.dll",
 "$dir\system.componentmodel.annotations\lib\net461\System.ComponentModel.Annotations.dll"
)
foreach ($d in $dlls) {
    if (Test-Path $d) { [System.Reflection.Assembly]::LoadFrom($d) | Out-Null }
}

function Enum-Objects($content, [string]$parent, [string]$indent, [int]$depth) {
    try {
        $enum = $content.EnumObjects(0, $parent, $null)
        $buf = New-Object string[] 256
        $fetched = [uint32]0
        $hr = $enum.Next(256, $buf, [ref]$fetched)
        for ($i = 0; $i -lt [int]$fetched; $i++) {
            Write-Host ($indent + "obj: [" + $buf[$i] + "]")
        }
        Write-Host ($indent + "-> " + $fetched + " object(s) under " + $parent)
        if ($depth -lt 4) {
            foreach ($id in $buf) {
                if ($id -and $id -ne $parent) {
                    Enum-Objects $content $id ($indent + "    ") ($depth + 1)
                }
            }
        }
    } catch {
        Write-Host ($indent + "enum failed: " + $_.Exception.Message)
    }
}

$mgr = New-Object 'Vanara.PInvoke.PortableDeviceApi+PortableDeviceManager'
$imgr = [Vanara.PInvoke.PortableDeviceApi+IPortableDeviceManager]$mgr
$count = [uint32]0
$hr = $imgr.GetDevices($null, [ref]$count)
Write-Host ("device count=" + $count)
if ($count -eq 0) { exit }
$devices = New-Object string[] $count
$hr = $imgr.GetDevices($devices, [ref]$count)
foreach ($devId in $devices) {
    if ([string]::IsNullOrEmpty($devId)) { continue }
    Write-Host ("device id: " + $devId)
    $pd = New-Object 'Vanara.PInvoke.PortableDeviceApi+PortableDevice'
    $ipd = [Vanara.PInvoke.PortableDeviceApi+IPortableDevice]$pd
    try {
        $ipd.Open($devId, $null)
        $content = $ipd.Content()
        Enum-Objects $content "DEVICE" "  " 0
        $ipd.Close()
    } catch {
        Write-Host ("  open failed: " + $_.Exception.Message)
    }
}
