# Shows which threads of a running SnowDesk use the CPU, and which module started each one.
# cpu-threads.ps1 -Exe <path> -Dir <scratch folder> [-Hour 22] [-Warm 12] [-Seconds 15]
param([string]$Exe = 'C:\Projects\SnowDesk\build\dev\SnowDesk.exe', [string]$Dir, [double]$Hour = 22, [int]$Warm = 12, [int]$Seconds = 15)
Add-Type -Namespace Bench2 -Name T -MemberDefinition @'
[DllImport("kernel32")] public static extern IntPtr OpenThread(uint access, bool inherit, int id);
[DllImport("kernel32")] public static extern bool CloseHandle(IntPtr h);
[DllImport("kernel32")] public static extern bool QueryThreadCycleTime(IntPtr thread, out ulong cycles);
[DllImport("kernel32")] public static extern IntPtr GetCurrentThread();
[DllImport("ntdll")] public static extern int NtQueryInformationThread(IntPtr h, int cls, out IntPtr value, int size, IntPtr ret);
public static double PerSecond() {
    ulong a, b;
    QueryThreadCycleTime(GetCurrentThread(), out a);
    var w = System.Diagnostics.Stopwatch.StartNew();
    while (w.ElapsedMilliseconds < 300) { }
    QueryThreadCycleTime(GetCurrentThread(), out b);
    return (b - a) / w.Elapsed.TotalSeconds;
}
'@
$rate = [Bench2.T]::PerSecond()
$ini = "$Dir\bench.ini"
[IO.File]::WriteAllLines($ini, @('[snow]'))
$p = Start-Process $Exe -ArgumentList "--settings `"$ini`" --hour $Hour --quit-after $($Warm + $Seconds + 3)" -PassThru
Start-Sleep $Warm
$p.Refresh()
$handles = @{}
$before = @{}
$start = @{}
foreach ($t in $p.Threads) {
    $h = [Bench2.T]::OpenThread(0x0040 -bor 0x0200, $false, $t.Id)     # QUERY_INFORMATION | QUERY_LIMITED
    if ($h -eq [IntPtr]::Zero) { continue }
    $handles[$t.Id] = $h
    [uint64]$c = 0; [void][Bench2.T]::QueryThreadCycleTime($h, [ref]$c); $before[$t.Id] = $c
    $addr = [IntPtr]::Zero
    [void][Bench2.T]::NtQueryInformationThread($h, 9, [ref]$addr, [IntPtr]::Size, [IntPtr]::Zero)
    $start[$t.Id] = $addr.ToInt64()
}
$clock = [Diagnostics.Stopwatch]::StartNew()
Start-Sleep $Seconds
$mods = foreach ($m in $p.Modules) { [pscustomobject]@{ Name = $m.ModuleName; Base = $m.BaseAddress.ToInt64(); End = $m.BaseAddress.ToInt64() + $m.ModuleMemorySize } }
$rows = foreach ($id in $handles.Keys) {
    [uint64]$c = 0; [void][Bench2.T]::QueryThreadCycleTime($handles[$id], [ref]$c)
    $a = $start[$id]
    $mod = ($mods | Where-Object { $a -ge $_.Base -and $a -lt $_.End } | Select-Object -First 1).Name
    [pscustomobject]@{ Thread = $id; Pct = [math]::Round(($c - $before[$id]) / $rate / $clock.Elapsed.TotalSeconds * 100, 2); StartedIn = $mod; Offset = '0x{0:X}' -f ($a - ($mods | Where-Object Name -eq $mod | Select-Object -First 1).Base) }
}
$rows | Sort-Object Pct -Descending | Select-Object -First 8 | Format-Table -AutoSize
'total {0:F1}% of a core' -f ($rows | Measure-Object Pct -Sum).Sum
foreach ($h in $handles.Values) { [void][Bench2.T]::CloseHandle($h) }
$p.WaitForExit(15000) | Out-Null
