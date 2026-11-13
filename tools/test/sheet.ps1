param([string]$Dir = "out\shots", [string]$Out = "out\sheet.png", [int]$Cols = 4, [double]$Scale = 0.5, [string]$Filter = "*.png")
# Tile screenshots into one labelled contact sheet.
$root = Split-Path (Split-Path $PSScriptRoot)   # tools/test -> project folder
Set-Location $root
Add-Type -AssemblyName System.Drawing
$files = Get-ChildItem "$Dir\$Filter" | Sort-Object Name
$w = [int](640 * $Scale); $h = [int](480 * $Scale) + 14
$rows = [math]::Ceiling($files.Count / $Cols)
$bmp = New-Object System.Drawing.Bitmap ($w * $Cols), ($h * $rows)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::DimGray)
$font = New-Object System.Drawing.Font "Arial", 9
for ($i = 0; $i -lt $files.Count; $i++) {
  $x = ($i % $Cols) * $w; $y = [math]::Floor($i / $Cols) * $h
  $im = [System.Drawing.Image]::FromFile($files[$i].FullName)
  $g.DrawImage($im, $x, $y + 14, $w, $h - 14); $im.Dispose()
  $g.DrawString($files[$i].BaseName, $font, [System.Drawing.Brushes]::White, $x + 2, $y)
}
$bmp.Save("$root\$Out", [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"sheet: $Out ($($files.Count) shots)"
