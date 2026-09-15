# mtp_check.ps1 - dump what the Windows shell sees inside the MTP device
$shell = New-Object -ComObject Shell.Application
$ns = $shell.Namespace(17)
$found = $false

foreach ($item in $ns.Items()) {
    if ($item.Name -match 'Eightyzhang|MTP') {
        $found = $true
        Write-Host ("Device: " + $item.Name)
        try {
            $folder = $item.GetFolder()
            $count = 0
            foreach ($child in $folder.Items()) {
                Write-Host ("  item: [" + $child.Name + "] type=" + $child.Type)
                $count++
            }
            Write-Host ("  -> " + $count + " item(s)")
            foreach ($child in $folder.Items()) {
                try {
                    $sub = $child.GetFolder
                    if ($sub) {
                        $subCount = 0
                        foreach ($c in $sub.Items()) {
                            Write-Host ("    sub item: [" + $c.Name + "] type=" + $c.Type)
                            $subCount++
                        }
                        Write-Host ("    -> " + $subCount + " sub item(s)")
                    }
                } catch {
                    Write-Host ("    sub open failed: " + $_.Exception.Message)
                }
            }
        } catch {
            Write-Host ("  open failed: " + $_.Exception.Message)
        }
    }
}

if (-not $found) {
    Write-Host "No Eightyzhang MTP device found"
}
