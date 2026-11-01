# Attacks a dev build the way a hostile local program could, and reports what survives. The real mouse and the
# user's settings stay untouched. attack-test.ps1 -Exe <dev build> -Dir <scratch folder> [-Plant <benign test dll>]
param([string]$Exe = 'C:\Projects\SnowDesk\build\dev\SnowDesk.exe', [string]$Dir, [string]$Plant = '')

Add-Type -Namespace Atk -Name W -MemberDefinition @'
[DllImport("user32", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
[DllImport("user32")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
[DllImport("user32")] public static extern IntPtr SendMessageTimeoutW(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr result);
[DllImport("kernel32", CharSet = CharSet.Unicode)] public static extern IntPtr CreateMutexW(IntPtr a, bool own, string name);
[DllImport("kernel32")] public static extern bool CloseHandle(IntPtr h);
[DllImport("kernel32")] public static extern uint SetErrorMode(uint mode);
public static int Flood(IntPtr[] targets, int count, int seed) {
    var r = new Random(seed);
    uint[] skip = { 0x0002, 0x0010, 0x0012, 0x0016, 0x0011, 0x0082 };   // DESTROY CLOSE QUIT ENDSESSION QUERYENDSESSION NCDESTROY
    int sent = 0;
    for (int i = 0; i < count; i++) {
        uint m = (uint)r.Next(1, 0x0420);
        if (System.Array.IndexOf(skip, m) >= 0) continue;
        if (m == 0x0111 || m == 0x0401) continue;                       // the menu commands are covered on their own
        long w = r.Next(), l = (long)r.Next() << r.Next(0, 16);
        if (PostMessageW(targets[r.Next(targets.Length)], m, (IntPtr)w, (IntPtr)l)) sent++;
    }
    return sent;
}
'@

$script:bad = 0
function Report($name, $ok, $detail = '') {
    if (-not $ok) { $script:bad++ }
    '{0,-5} {1}  {2}' -f $(if ($ok) { 'ΟΚ' } else { 'ΕΥΡΗΜΑ' }), $name, $detail
}

function Stop-It($p) {
    if ($p.HasExited) { return }
    $h = [Atk.W]::FindWindowW('SnowDeskHost', [NullString]::Value)
    if ($h -ne [IntPtr]::Zero) { [void][Atk.W]::PostMessageW($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
    [void]$p.WaitForExit(6000)
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; [void]$p.WaitForExit(2000); 'HUNG' }
}

# Starts it with these arguments, gives it a few seconds, and tells whether it lived and how it ended.
function Survives($name, $arguments, $seconds = 3) {
    $p = Start-Process $Exe -ArgumentList $arguments -PassThru
    Start-Sleep $seconds
    $alive = -not $p.HasExited
    $hung = Stop-It $p
    $code = if ($p.HasExited) { $p.ExitCode } else { 'running' }
    $clean = $code -is [int] -and $code -ge 0 -and -not $hung
    Report $name $clean "alive=$alive exit=$code $hung"
}

'--- hostile settings files'
$ini = "$Dir\atk.ini"
$cases = [ordered]@{
    'τεράστιες τιμές' = "[snow]`r`ndensity=99999999999`r`nopacity=-5`r`nflakeSize=abc`r`nfallSpeed=`r`nwindStrength=2147483648`r`nsettleSpeed=0x7fffffff"
    'τεράστια γραμμή' = "[snow]`r`ndensity=" + ('9' * 5000000)
}
foreach ($k in $cases.Keys) {
    [IO.File]::WriteAllText($ini, $cases[$k])
    Survives $k "--settings `"$ini`""
}
$junk = New-Object byte[] 1000000
(New-Object Random 7).NextBytes($junk)
[IO.File]::WriteAllBytes($ini, $junk)
Survives 'ένα MB τυχαίων bytes' "--settings `"$ini`""
Survives 'ρυθμίσεις = φάκελος' "--settings `"$Dir`""
Survives 'ρυθμίσεις σε φάκελο που δεν υπάρχει' "--settings `"$Dir\no\such\place.ini`""

'--- strange arguments'
foreach ($a in '--fast 0', '--fast -5', '--fast nan', '--fast 1e30', '--hour -1000', '--hour 1e30', '--quit-after -1 --log', '--log C:\Windows\System32\snow.log',
    '--scene zzz', '--settings', ('--settings ' + ('A' * 30000)), '--dump-mask C:\Windows\System32\mask.pgm') {
    $label = if ($a.Length -gt 44) { $a.Substring(0, 44) + '...' } else { $a }
    Survives $label "$a --settings `"$ini`""
}

'--- window messages'
[IO.File]::WriteAllLines($ini, @('[snow]'))
$p = Start-Process $Exe -ArgumentList "--settings `"$ini`" --panel" -PassThru
Start-Sleep 3
$targets = @('SnowDeskHost', 'SnowDeskPanel', 'SnowDeskOverlay') | ForEach-Object { [Atk.W]::FindWindowW($_, [NullString]::Value) } | Where-Object { $_ -ne [IntPtr]::Zero }
$sent = 0
1..4 | ForEach-Object { $sent += [Atk.W]::Flood([IntPtr[]]$targets, 40000, $_); Start-Sleep -Milliseconds 400 }
Start-Sleep 2
Report "πλημμύρα $sent τυχαίων μηνυμάτων" (-not $p.HasExited) "exit=$(if ($p.HasExited) { $p.ExitCode } else { 'running' })"
if (-not $p.HasExited) {
    $panel = [Atk.W]::FindWindowW('SnowDeskPanel', [NullString]::Value)
    # Sent, not posted: Windows refuses to post a message that carries a pointer.
    $ignored = [IntPtr]::Zero
    [void][Atk.W]::SendMessageTimeoutW($panel, 0x02E0, [IntPtr]0x00600060, [IntPtr]0x41414141, 2, 1500, [ref]$ignored)
    Start-Sleep 2
    Report 'WM_DPICHANGED με ψεύτικο δείκτη' (-not $p.HasExited) "exit=$(if ($p.HasExited) { '0x{0:X}' -f $p.ExitCode } else { 'running' })"
}
if (-not $p.HasExited) {
    $hostWin = [Atk.W]::FindWindowW('SnowDeskHost', [NullString]::Value)
    1..2000 | ForEach-Object { [void][Atk.W]::PostMessageW($hostWin, 0x007E, [IntPtr]::Zero, [IntPtr]::Zero) }
    Start-Sleep 4
    Report 'πλημμύρα WM_DISPLAYCHANGE' (-not $p.HasExited) "exit=$(if ($p.HasExited) { '0x{0:X}' -f $p.ExitCode } else { 'running' })"
}
[void](Stop-It $p)

'--- single instance mutex'
$m = [Atk.W]::CreateMutexW([IntPtr]::Zero, $true, 'Local\SnowDesk.SingleInstance')
$p = Start-Process $Exe -ArgumentList "--settings `"$ini`"" -PassThru
Start-Sleep 2
"ΔΕΚΤΟ κατάληψη mutex  όποιος τρέχει ήδη ως ίδιος χρήστης μπορεί και να κλείσει την εφαρμογή, δεν κερδίζει κάτι"
[void](Stop-It $p)
[void][Atk.W]::CloseHandle($m)

if ($Plant -and (Test-Path -LiteralPath $Plant)) {
    '--- planted DLL next to the exe'
    $lab = "$Dir\plantlab"
    [void](New-Item -ItemType Directory -Force $lab)
    $marker = "$env:TEMP\snowdesk-planted-marker.txt"
    if (Test-Path -LiteralPath $marker) { [IO.File]::Delete($marker) }
    Copy-Item $Exe "$lab\SnowDesk.exe" -Force
    # The stand-in has none of the real exports, so a planted copy either runs its DllMain or stops the start
    # with "entry point not found". Both prove the file in the exe's folder was the one used.
    foreach ($name in 'D3DCOMPILER_47.dll', 'dcomp.dll', 'dwmapi.dll', 'd2d1.dll', 'DWrite.dll', 'WTSAPI32.dll', 'version.dll') {
        Get-ChildItem -LiteralPath $lab -Filter *.dll | ForEach-Object { [IO.File]::Delete($_.FullName) }
        Copy-Item $Plant "$lab\$name" -Force
        if (Test-Path -LiteralPath $marker) { [IO.File]::Delete($marker) }
        # Started directly, so a failed start inherits the silent error mode instead of showing a dialog.
        [void][Atk.W]::SetErrorMode(0x8003)
        $start = New-Object Diagnostics.ProcessStartInfo "$lab\SnowDesk.exe", "--settings `"$ini`""
        $start.UseShellExecute = $false
        $p = [Diagnostics.Process]::Start($start)
        Start-Sleep 2
        $alive = -not $p.HasExited
        [void](Stop-It $p)
        $code = '0x{0:X8}' -f ($p.ExitCode -band 0xFFFFFFFF)
        $used = (Test-Path -LiteralPath $marker) -or $code -eq '0xC0000139' -or $code -eq '0xC000007B'
        Report "ξένο $name δίπλα στο exe" (-not $used) "ξεκίνησε=$alive exit=$code"
    }
}
"findings: $script:bad"
