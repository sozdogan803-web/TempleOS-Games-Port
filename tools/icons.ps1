# icons.ps1 <game.HC ...>  -- makes out\icons\<Game>.ico for each game (build.cmd links it in).
# Sprite icons: TempleOS draws them itself (IconGen.exe, made by mkicons.py + the translator).
# Games with no sprite at all get a picture of their screen after 4 seconds.
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Games)
$ErrorActionPreference = "Stop"
$H = Split-Path -Parent $PSScriptRoot
$Src = if ($env:TOS_SRC) { $env:TOS_SRC } else { "$H\tos_src" }
$Icons = "$H\out\icons"
New-Item -ItemType Directory -Force $Icons, "$H\gen" | Out-Null

Push-Location $Src
python -I "$H\tools\mkicons.py" gen tos2 m_tinkeros_TinkerOS/src "$H\gen\IconGen.HC" @Games
python -I "$H\tools\hc_emit.py" tos2 m_tinkeros_TinkerOS/src "$H\gen" "$H\gen\IconGen.HC" | Select-String "failed"
Pop-Location
Remove-Item "$H\obj\tos_lib.obj" -ErrorAction SilentlyContinue   # the library was regenerated too
cmd /c "`"$H\build.cmd`" IconGen" | Select-String "error"
Get-ChildItem "$Icons\*.raw" -ErrorAction SilentlyContinue | Remove-Item
$env:TOS_ICON_DIR = $Icons
$p = Start-Process "$H\out\IconGen.exe" -PassThru -WindowStyle Minimized; $null = $p.Handle
if (-not $p.WaitForExit(60000)) { Stop-Process -Id $p.Id -Force; throw "IconGen hung" }
Remove-Item Env:TOS_ICON_DIR
python -I "$H\tools\mkicons.py" ico $Icons

# no sprite: a square from the middle of the game's screen
Add-Type -AssemblyName System.Drawing
foreach ($g in $Games) {
  $name = python -I -c "import sys; sys.path.insert(0, r'$H\tools'); import hc_build; print(hc_build.game_name(r'$g'))"
  if (Test-Path "$Icons\$name.ico") { continue }
  cmd /c "`"$H\build.cmd`" $name" | Select-String "error"
  $env:TOS_SHOT = "$Icons\$name.bmp"; $env:TOS_SHOT_MS = "4000"
  $p = Start-Process "$H\out\$name.exe" -PassThru -WindowStyle Minimized; $null = $p.Handle
  if (-not $p.WaitForExit(20000)) { Stop-Process -Id $p.Id -Force }
  Remove-Item Env:TOS_SHOT, Env:TOS_SHOT_MS
  if (-not (Test-Path "$Icons\$name.bmp")) { "no picture for $name"; continue }
  $shot = [System.Drawing.Image]::FromFile("$Icons\$name.bmp")
  $pngs = @()
  foreach ($s in 16, 24, 32, 48, 64, 256) {
    $b = New-Object System.Drawing.Bitmap $s, $s
    $gr = [System.Drawing.Graphics]::FromImage($b)
    $gr.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $gr.DrawImage($shot, (New-Object System.Drawing.Rectangle 0, 0, $s, $s), (New-Object System.Drawing.Rectangle 80, 0, 480, 480), [System.Drawing.GraphicsUnit]::Pixel)
    $f = "$Icons\$name.$s.png"; $b.Save($f, [System.Drawing.Imaging.ImageFormat]::Png); $pngs += $f
    $gr.Dispose(); $b.Dispose()
  }
  $shot.Dispose()
  python -I "$H\tools\mkicons.py" pack "$Icons\$name.ico" @pngs
  Remove-Item ($pngs + "$Icons\$name.bmp")
  "$name.ico (from its screen)"
}
