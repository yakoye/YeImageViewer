# 仅用于测试排队，不关闭、挂起或修改其他项目；无冲突时不增加固定等待。
function Wait-YeViewerDesktopTestSlot {
    param([int]$MaximumWaitSeconds=900,[int]$QuietSeconds=4)
    $deadline=[DateTime]::UtcNow.AddSeconds($MaximumWaitSeconds)
    $waited=$false
    $quietSince=$null
    do {
        $foreign=@(Get-CimInstance Win32_Process | Where-Object {
            ($_.Name -match '^Shazao.*\.exe$' -and $_.CommandLine -match '(?i)--automation|Temp\\shazao') -or
            ($_.Name -match '^python.*\.exe$' -and $_.CommandLine -match '(?i)(pressure|e2e|visual).*(shazao|Shazao)')
        })
        if($foreign.Count) {
            if(-not $waited) {
                Write-Host '等待其他项目桌面测试结束，本项目尚未启动窗口输入。'
                $foreign | ForEach-Object {Write-Host ("让行：PID=$($_.ProcessId)，$($_.Name)，$($_.CommandLine)")}
            }
            $waited=$true;$quietSince=$null
        } else {
            if(-not $waited){return}
            if($null -eq $quietSince){$quietSince=[DateTime]::UtcNow}
            if(([DateTime]::UtcNow-$quietSince).TotalSeconds -ge $QuietSeconds) {
                Write-Host "其他桌面测试已结束，连续${QuietSeconds}秒空闲，恢复本项目窗口测试。"
                return
            }
        }
        Start-Sleep -Milliseconds 1000
    } while([DateTime]::UtcNow -lt $deadline)
    throw '等待其他项目桌面测试超时；尚未运行本项窗口输入，不是产品断言失败。'
}
