# Captures the whole screen and saves optional crops: shot.ps1 out.png "x,y,w,h" "x,y,w,h" ...
param([string]$Out, [string[]]$Crops = @())
Add-Type -AssemblyName System.Drawing
$bmp = New-Object Drawing.Bitmap 5120, 1440
$gr = [Drawing.Graphics]::FromImage($bmp)
$gr.CopyFromScreen(0, 0, 0, 0, $bmp.Size, [Drawing.CopyPixelOperation]::SourceCopy)
$gr.Dispose()
$bmp.Save($Out)
$i = 0
foreach ($c in $Crops) {
    $v = $c.Split(',') | ForEach-Object { [int]$_ }
    $part = $bmp.Clone([Drawing.Rectangle]::new($v[0], $v[1], $v[2], $v[3]), $bmp.PixelFormat)
    $part.Save(($Out -replace '\.png$', "_$i.png"))
    $i++
}
