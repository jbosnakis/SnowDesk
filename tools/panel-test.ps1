# Drives the settings panel with window messages, so the real mouse stays untouched, and checks that each
# option reaches the settings file and the simulation. Usage: panel-test.ps1 <scratch folder>
param([string]$Dir)
$ini = "$Dir\panel-test.ini"
$log = "$Dir\panel-test.log"
foreach ($f in $ini, $log) { if (Test-Path -LiteralPath $f) { [IO.File]::Delete($f) } }

Add-Type -Namespace Win3 -Name U3 -MemberDefinition @'
[DllImport("user32", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
[DllImport("user32")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
[StructLayout(LayoutKind.Sequential)] public struct R { public int l, t, r, b; }
[DllImport("user32")] public static extern bool GetWindowRect(IntPtr h, out R r);
[DllImport("user32")] public static extern bool IsWindowVisible(IntPtr h);
'@

function Click($hwnd, [int]$x, [int]$y) {
    $l = [IntPtr](($y -shl 16) -bor $x)
    [void][Win3.U3]::PostMessageW($hwnd, 0x0201, [IntPtr]1, $l)
    [void][Win3.U3]::PostMessageW($hwnd, 0x0202, [IntPtr]::Zero, $l)
    Start-Sleep -Milliseconds 300
}
function Key($hwnd, [int]$vk, [bool]$down) { [void][Win3.U3]::PostMessageW($hwnd, $(if ($down) { 0x0100 } else { 0x0101 }), [IntPtr]$vk, [IntPtr]::Zero); Start-Sleep -Milliseconds 120 }
function Value($key) { (Get-Content -LiteralPath $ini | Where-Object { $_ -match "^$key=" }) -replace "^$key=", '' }
function Stats { Get-Content -LiteralPath $log | Where-Object { $_ -match '^t=' } | Select-Object -Last 1 }
function Sky { Get-Content -LiteralPath $log | Where-Object { $_ -match 'weather:' } | Select-Object -Last 1 }
function Check($name, $ok, $detail) { '{0,-4} {1}  {2}' -f $(if ($ok) { 'ΟΚ' } else { 'ΛΑΘΟΣ' }), $name, $detail }

[IO.File]::WriteAllLines($ini, @('[snow]', 'settleSpeed=400'))     # snow has to lie before the test can clear it
$p = Start-Process C:\Projects\SnowDesk\build\dev\SnowDesk.exe -ArgumentList "--log `"$log`" --settings `"$ini`" --panel --fast 6 --hour 23 --quit-after 56" -PassThru
Start-Sleep 12
$h = [Win3.U3]::FindWindowW('SnowDeskPanel', [NullString]::Value)
Check 'πάνελ ανοιχτό' ([Win3.U3]::IsWindowVisible($h)) ''
$r = New-Object 'Win3.U3+R'; [void][Win3.U3]::GetWindowRect($h, [ref]$r)
"ΠΑΡΑΘΥΡΟ $($r.l),$($r.t),$($r.r - $r.l),$($r.b - $r.t)"
& C:\Projects\SnowDesk\tools\shot.ps1 "$Dir\panel.png" "$($r.l),$($r.t),$($r.r - $r.l),$($r.b - $r.t)"

Click $h 171 99             # density slider, middle
Check 'ένταση' ((Value 'density') -eq '160') "density=$(Value 'density')"
Click $h 640 59             # winds
Start-Sleep 2
$sky = Sky
Check 'άνεμοι κλειστοί' ((Value 'wind') -eq '0' -and $sky -match 'wind=-?0 gust=0.00') "$sky"

$before = Stats
Click $h 300 345            # only falling
Start-Sleep 3
$after = Stats
Check 'μόνο χιονόπτωση' ((Value 'settle') -eq '0' -and $after -match ' snow=0 ' -and $before -notmatch ' snow=0 ') "πριν: $($before -replace '.*(snow=\d+).*','$1')  μετά: $($after -replace '.*(snow=\d+).*','$1')"
Start-Sleep 4
$later = Stats
Check 'τίποτα δεν κάθεται' ($later -match ' snow=0 ') ($later -replace '.*(snow=\d+).*','$1')

Click $h 300 345            # back on
Click $h 300 379            # unlimited
Check 'χωρίς όριο' ((Value 'unlimited') -eq '1' -and (Value 'settle') -eq '1') "unlimited=$(Value 'unlimited')"
Click $h 171 521            # settling speed slider, middle
$speed = [int](Value 'settleSpeed')
Check 'ταχύτητα στρωσίματος' ($speed -ge 200 -and $speed -le 230) "settleSpeed=$speed"
Click $h 640 176            # storm button
Start-Sleep 2
$storm = Sky
Check 'χιονοθύελλα' ($storm -match 'snowfall=([2-9])') "$storm"
Click $h 300 379            # back off, so the next line proves the toggle works both ways
Check 'χωρίς όριο κλειστό' ((Value 'unlimited') -eq '0') ''

Click $h 640 423            # low CPU mode
Start-Sleep 3
$low = Stats; $lowSky = Sky
Check 'Low CPU mode' ((Value 'lowCpu') -eq '1' -and $low -match ' snow=0 ' -and $low -match ' active=0 ' -and $lowSky -match 'wind=-?0 gust=0.00 snowfall=1.00') "lowCpu=$(Value 'lowCpu') $($low -replace '.*(fps=\d+).*','$1') $($low -replace '.*(snow=\d+).*','$1') $lowSky"
Click $h 300 379            # disabled while in low CPU mode, so this click must change nothing
Check 'κλειδωμένες επιλογές' ((Value 'unlimited') -eq '0') "unlimited=$(Value 'unlimited')"
Click $h 640 423            # back to normal
Check 'έξοδος από Low CPU' ((Value 'lowCpu') -eq '0') ''

Key $h 0x09 $true; Key $h 0x09 $true        # Tab, Tab: from Snowing on to Intensity
Key $h 0x10 $true; Key $h 0x09 $true; Key $h 0x10 $false    # Shift+Tab: back to Snowing
Key $h 0x20 $true                           # Space turns Snowing off
Start-Sleep -Milliseconds 300
Check 'Shift+Tab με πληκτρολόγιο' ((Value 'enabled') -eq '0') "enabled=$(Value 'enabled')"
Key $h 0x20 $true
Start-Sleep -Milliseconds 300

$link = "$([Environment]::GetFolderPath('Startup'))\SnowDesk.lnk"
$hadLink = Test-Path -LiteralPath $link
if (-not $hadLink) {
    Click $h 640 457            # Start with Windows on
    $target = if (Test-Path -LiteralPath $link) { (New-Object -ComObject WScript.Shell).CreateShortcut($link).TargetPath } else { '' }
    Check 'συντόμευση εκκίνησης' ($target -like '*\SnowDesk.exe') "target=$target"
    Click $h 640 457            # and off again
    Check 'συντόμευση αφαιρέθηκε' (-not (Test-Path -LiteralPath $link)) ''
}

$p.WaitForExit(30000) | Out-Null
Check 'κανονικός τερματισμός' ($p.ExitCode -eq 0) "exit=$($p.ExitCode)"
