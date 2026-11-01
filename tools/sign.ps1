# Signs the release exe with a code signing certificate from the Windows certificate store, or on its token.
# sign.ps1 -Thumbprint <certificate thumbprint> [-File build\SnowDesk.exe] [-NoTimestamp]
# Always sign with a timestamp for anything handed to others: without one the signature dies with the certificate.
param([Parameter(Mandatory)][string]$Thumbprint, [string]$File = "$PSScriptRoot\..\build\SnowDesk.exe",
      [string]$TimestampUrl = 'http://timestamp.digicert.com', [switch]$NoTimestamp)
$tool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe' | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $tool) { throw 'signtool.exe not found: install the Windows SDK' }
$arguments = @('sign', '/sha1', $Thumbprint, '/fd', 'SHA256', '/d', 'SnowDesk')
if (-not $NoTimestamp) { $arguments += @('/tr', $TimestampUrl, '/td', 'SHA256') }
& $tool.FullName @arguments $File
if ($LASTEXITCODE -ne 0) { throw "signing failed with code $LASTEXITCODE" }
$signature = Get-AuthenticodeSignature $File
'status: {0}  signer: {1}  timestamp: {2}' -f $signature.Status, $signature.SignerCertificate.Subject, $signature.TimeStamperCertificate.Subject
