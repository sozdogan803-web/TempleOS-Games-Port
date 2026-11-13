param([string[]]$Games, [int]$Ms = 19000, [int]$Par = 4, [string]$Bin = "out")   # -Bin out\x86 after $env:TOS_ARCH="x86" for the 32-bit build
# Play each game for ~20 s with scripted keys; report crashes / stderr; screenshot at $Ms into <Bin>\soak\
$root = Split-Path (Split-Path $PSScriptRoot)   # tools/test -> project folder
Set-Location $root
if (-not $Games) { $Games = Get-Content "$PSScriptRoot\games.txt" | ForEach-Object { if ($_ -match "/Run\.HC") { Split-Path (Split-Path $_ -Parent) -Leaf } else { [IO.Path]::GetFileNameWithoutExtension($_) } } }
New-Item -ItemType Directory -Force $Bin\soak | Out-Null
Add-Type -AssemblyName System.Drawing
# make-break pairs: Enter, Space, arrows (E0-prefixed), a few letters, digits
$keys = @("1C","9C"), @("39","B9"), @("E0,48","E0,C8"), @("E0,50","E0,D0"), @("E0,4B","E0,CB"), @("E0,4D","E0,CD"),
        @("1E","9E"), @("11","91"), @("1F","9F"), @("20","A0"), @("02","82"), @("03","83")
function KeySeq([int]$seed) {
  $r = New-Object System.Random $seed
  $out = @("1200:1C", "1300:9C", "2500:1C", "2600:9C")   # dismiss intro pop-ups
  for ($t = 3200; $t -lt $Ms - 500; $t += 350 + $r.Next(400)) {
    $k = $keys[$r.Next($keys.Count)]
    foreach ($b in $k[0].Split(",")) { $out += "${t}:$b" }
    $t2 = $t + 80 + $r.Next(250)
    foreach ($b in $k[1].Split(",")) { $out += "${t2}:$b" }
  }
  return ($out -join ",")
}
$sum = @(); $running = @()
$i = 0
foreach ($g in $Games) {
  $env:TOS_KEYS = KeySeq ($i++); $env:TOS_SHOT = "$root\$Bin\soak\$g.bmp"; $env:TOS_SHOT_MS = "$Ms"
  $p = Start-Process -FilePath "$Bin\$g.exe" -PassThru -RedirectStandardError "$Bin\soak\$g.err.txt" -RedirectStandardOutput "$Bin\soak\$g.out.txt"
  $null = $p.Handle
  $running += [pscustomobject]@{ G = $g; P = $p; T = Get-Date }
  Remove-Item Env:TOS_KEYS, Env:TOS_SHOT, Env:TOS_SHOT_MS
  while ($running.Count -ge $Par -or ($running.Count -gt 0 -and $i -ge $Games.Count)) {
    Start-Sleep -Milliseconds 300
    $done = @($running | Where-Object { $_.P.HasExited -or ((Get-Date) - $_.T).TotalSeconds -gt ($Ms / 1000 + 25) })
    foreach ($d in $done) {
      if (-not $d.P.HasExited) { Stop-Process -Id $d.P.Id -Force; $st = "hung" } else { $st = "exit=0x{0:X}" -f $d.P.ExitCode }
      if (Test-Path "$Bin\soak\$($d.G).bmp") {
        $im = [System.Drawing.Image]::FromFile("$root\$Bin\soak\$($d.G).bmp"); $im.Save("$root\$Bin\soak\$($d.G).png", [System.Drawing.Imaging.ImageFormat]::Png); $im.Dispose()
        Remove-Item "$Bin\soak\$($d.G).bmp"; $st += " shot"
      }
      $err = (Get-Content "$Bin\soak\$($d.G).err.txt" -ErrorAction SilentlyContinue | Select-Object -First 2) -join " / "
      $sum += "$($d.G) : $st | $err"
    }
    $running = @($running | Where-Object { $done -notcontains $_ })
    if ($running.Count -eq 0) { break }
  }
}
$sum | Sort-Object | Set-Content $Bin\soak\summary.txt
$sum | Sort-Object
