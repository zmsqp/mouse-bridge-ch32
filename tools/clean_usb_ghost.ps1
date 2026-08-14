$phantoms = Get-PnpDevice | Where-Object {
    $_.Problem -eq 'CM_PROB_PHANTOM' -and $_.InstanceId -match 'VID_1A86&PID_FE00'
}
Write-Host "Found $($phantoms.Count) FE00 phantom devices"
foreach ($dev in $phantoms) {
    Write-Host "Remove: $($dev.InstanceId)"
    pnputil /remove-device $dev.InstanceId
}

$recv = Get-PnpDevice | Where-Object {
    ($_.Problem -eq 'CM_PROB_PHANTOM' -or $_.Status -eq 'Unknown') -and $_.InstanceId -match 'VID_1A2C&PID_7F07'
}
Write-Host "Found $($recv.Count) 1A2C:7F07 devices"
foreach ($dev in $recv) {
    Write-Host "Remove: $($dev.InstanceId)"
    pnputil /remove-device $dev.InstanceId
}

Write-Host "Done. Unplug CH32 USB, wait 5s, replug."
