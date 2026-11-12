# Run on Windows with Windows SDK SignTool and a trusted code-signing certificate.
# Signing happens before packaging so the MSI contains the signed EXE.
param([Parameter(Mandatory=$true)][string]$CertificateThumbprint,
      [string]$TimestampUrl = 'http://timestamp.digicert.com')
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
 & signtool.exe sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 'dist\3DSRP-Companion.exe'
 if ($LASTEXITCODE -ne 0) { throw 'Executable signing failed' }
 Write-Host 'EXE signed. Rebuild the MSI with package.sh, then sign the resulting MSI:'
 Write-Host "signtool.exe sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 dist\3DSRP-Companion-42.0.3-x64.msi"
} finally { Pop-Location }
