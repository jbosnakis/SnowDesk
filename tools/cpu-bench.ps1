# Compares the CPU of SnowDesk builds at steady state, taking turns so background load hits them alike.
# Uses CPU cycles: TotalProcessorTime is charged per 15.6 ms clock tick, which misreads a program waking 120 times
# a second. Only one copy can run at a time.
# cpu-bench.ps1 -Exes a.exe,b.exe -Dir <scratch folder> [-Hour 22] [-Warm 15] [-Seconds 20] [-Runs 3] [-Extra "..."]
param([string[]]$Exes = @('C:\Projects\SnowDesk\build\dev\SnowDesk.exe'), [string]$Dir, [double]$Hour = 22,
      [int]$Warm = 15, [int]$Seconds = 20, [int]$Runs = 3, [string]$Extra = '', [string[]]$IniLines = @())
Add-Type -Namespace Bench -Name Cycles -MemberDefinition @'
[DllImport("kernel32")] public static extern bool QueryProcessCycleTime(IntPtr process, out ulong cycles);
[DllImport("kernel32")] public static extern bool QueryThreadCycleTime(IntPtr thread, out ulong cycles);
[DllImport("kernel32")] public static extern IntPtr GetCurrentThread();
public static double PerSecond() {
    ulong a, b;
    QueryThreadCycleTime(GetCurrentThread(), out a);
    var w = System.Diagnostics.Stopwatch.StartNew();
    while (w.ElapsedMilliseconds < 300) { }
    QueryThreadCycleTime(GetCurrentThread(), out b);
    return (b - a) / w.Elapsed.TotalSeconds;
}
'@
$cyclesPerSecond = [Bench.Cycles]::PerSecond()
$ini = "$Dir\bench.ini"
[IO.File]::WriteAllLines($ini, @('[snow]') + $IniLines)
$results = @{}
foreach ($exe in $Exes) { $results[$exe] = @() }
for ($i = 0; $i -lt $Runs; $i++) {
    foreach ($exe in $Exes) {
        $p = Start-Process $exe -ArgumentList "--settings `"$ini`" --hour $Hour --quit-after $($Warm + $Seconds + 3) $Extra" -PassThru
        Start-Sleep $Warm
        [uint64]$c0 = 0; [uint64]$c1 = 0
        [void][Bench.Cycles]::QueryProcessCycleTime($p.Handle, [ref]$c0)
        $clock = [Diagnostics.Stopwatch]::StartNew()
        Start-Sleep $Seconds
        [void][Bench.Cycles]::QueryProcessCycleTime($p.Handle, [ref]$c1)
        $results[$exe] += ($c1 - $c0) / $cyclesPerSecond / $clock.Elapsed.TotalSeconds * 100
        $p.WaitForExit(15000) | Out-Null
        if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
        Start-Sleep -Milliseconds 700
    }
}
foreach ($exe in $Exes) {
    $sorted = @($results[$exe] | Sort-Object)
    '{0,-14} hour={1} runs: {2}  median={3:F1}% of a core' -f [IO.Path]::GetFileNameWithoutExtension($exe), $Hour,
        (($results[$exe] | ForEach-Object { '{0:F1}' -f $_ }) -join ' '), $sorted[[int][math]::Floor($sorted.Count / 2)]
}
