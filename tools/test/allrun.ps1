param([string[]]$Games, [int]$Ms = 4000, [int]$Secs = 12, [string]$Bin = "out")   # -Bin out\x86 after $env:TOS_ARCH="x86" for the 32-bit build
# Build and run every game; screenshot each; summary to <Bin>\summary.txt
$root = Split-Path (Split-Path $PSScriptRoot)   # tools/test -> project folder
Set-Location $root
# game name = file name, or the folder name for Apps/<Game>/Run.HC
if (-not $Games) { $Games = Get-Content "$PSScriptRoot\games.txt" | ForEach-Object { if ($_ -match "/Run\.HC") { Split-Path (Split-Path $_ -Parent) -Leaf } else { [IO.Path]::GetFileNameWithoutExtension($_) } } }
New-Item -ItemType Directory -Force $Bin\shots | Out-Null
Add-Type -AssemblyName System.Drawing
$sum = @()
foreach ($g in $Games) {
  $b = cmd /c ".\build.cmd $g" 2>&1 | Select-String "error"
  if ($b) { $sum += "$g : BUILD FAILED $($b | Select-Object -First 2)"; continue }
  $env:TOS_SHOT = "$root\$Bin\shots\$g.bmp"; $env:TOS_SHOT_MS = "$Ms"
  Remove-Item "$Bin\shots\$g.bmp" -ErrorAction SilentlyContinue
  $p = Start-Process -FilePath "$Bin\$g.exe" -PassThru -RedirectStandardError "$Bin\shots\$g.err.txt" -RedirectStandardOutput "$Bin\shots\$g.out.txt"
  $h = $p.Handle
  if (-not $p.WaitForExit($Secs * 1000)) { Stop-Process -Id $p.Id -Force; $st = "hung" } else { $st = "exit=0x{0:X}" -f $p.ExitCode }
  Remove-Item Env:TOS_SHOT, Env:TOS_SHOT_MS
  if (Test-Path "$Bin\shots\$g.bmp") {
    $i = [System.Drawing.Image]::FromFile("$root\$Bin\shots\$g.bmp"); $i.Save("$root\$Bin\shots\$g.png", [System.Drawing.Imaging.ImageFormat]::Png); $i.Dispose()
    Remove-Item "$Bin\shots\$g.bmp"; $st += " shot"
  }
  $err = Get-Content "$Bin\shots\$g.err.txt" -ErrorAction SilentlyContinue
  $un = ($err | Select-String "runtime: (\w+) not implemented" | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique) -join ","
  $ex = ($err | Select-String "exception|Dbg\(|crash|fatal" | Select-Object -First 1)
  $sum += "$g : $st | unimpl: $un | $ex"
}
$sum | Set-Content $Bin\summary.txt
$sum
