# 从 app/image 的 C 数组生成 LVGL 图片 bin (4字节头 + RGB565 数据)
# 输出到 resources/ 目录, 可通过 USB MTP 拷贝到 LittleFS, 或由设备首次启动自供给

$icons = @('icon_wifi','icon_no_wifi','icon_qingtian','icon_yintian','icon_duoyun','icon_zhongyu',
           'icon_leizhenyu','icon_zhongxue','icon_yueliang','icon_wu','icon_na')

$outDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'resources'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

foreach ($n in $icons) {
    $src = Join-Path $PSScriptRoot "..\app\image\$n.c"
    $text = Get-Content $src -Raw
    if ($text -match '\.width = (\d+)') { $w = [int]$Matches[1] } else { throw "width not found: $n" }
    if ($text -match '\.height = (\d+)') { $h = [int]$Matches[1] } else { throw "height not found: $n" }
    if ($text -match '\[\d*\]\s*=\s*\{(?s)(.*?)\};') { $body = $Matches[1] } else { throw "array not found: $n" }

    # 剔除数组内的注释, 避免把注释里的十六进制样例误当数据

$body = [regex]::Replace($body, '/\*.*?\*/', '', 'Singleline')

    $bytes = [System.Collections.Generic.List[byte]]::new()
    foreach ($m in [regex]::Matches($body, '0[Xx]([0-9A-Fa-f]{2})')) {
        $bytes.Add([Convert]::ToByte($m.Groups[1].Value, 16))
    }

    # LVGL v8.3 小端图片头: cf(5) | always_zero(3) | reserved(2) | w(11) | h(11)
    # w 从 bit10 开始, h 从 bit21 开始 (w<<11/h<<22 会读成 2 倍尺寸)
    $hdr = 4 -bor ($w -shl 10) -bor ($h -shl 21)
    $fs = [System.IO.File]::Open((Join-Path $outDir "$n.bin"), 'Create')
    $bw = New-Object System.IO.BinaryWriter($fs)
    $bw.Write([uint32]$hdr)
    $bw.Write($bytes.ToArray())
    $bw.Close()
    "$n.bin : ${w}x${h}, $($bytes.Count) bytes"
}
