# Asks a running SnowDesk to exit cleanly, so its tray icon goes away too.
Add-Type -Namespace Win -Name U -MemberDefinition @'
[DllImport("user32", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
[DllImport("user32")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
'@
$h = [Win.U]::FindWindowW('SnowDeskHost', [NullString]::Value)
if ($h -ne [IntPtr]::Zero) { [void][Win.U]::PostMessageW($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
Wait-Process -Name SnowDesk -Timeout 5 -ErrorAction SilentlyContinue
