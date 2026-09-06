param(
    [string]$DriverPath = "C:\Users\ncomp\source\repos\BackupExternal\Seraph\build\driver\seraph_drv.sys",
    [string]$LoaderPath = "C:\Users\ncomp\source\repos\BackupExternal\Seraph\build\driver\SeraphLoader.exe"
)

$ErrorActionPreference = "Continue"
$CN = "Seraph Test Sign"

Write-Host "=== [1/3] ensure test cert exists and is trusted ===" -ForegroundColor Cyan

$cert = Get-ChildItem Cert:\LocalMachine\My -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -like "*$CN*" } |
    Select-Object -First 1

if (-not $cert) {
    Write-Host "creating new self-signed code-signing cert..."
    $cert = New-SelfSignedCertificate -Type CodeSigningCert `
        -Subject "CN=$CN" `
        -CertStoreLocation Cert:\LocalMachine\My `
        -KeyUsage DigitalSignature `
        -KeyExportPolicy Exportable `
        -FriendlyName $CN
    if (-not $cert) { Write-Host "[FAIL] cert creation" -ForegroundColor Red; exit 1 }
} else {
    Write-Host "reusing existing cert $($cert.Thumbprint)"
}

# Trust in Root + TrustedPublisher (idempotent; skip if already present)
foreach ($storeName in @("Root", "TrustedPublisher")) {
    $present = Get-ChildItem "Cert:\LocalMachine\$storeName" -ErrorAction SilentlyContinue |
        Where-Object { $_.Thumbprint -eq $cert.Thumbprint }
    if (-not $present) {
        $store = New-Object System.Security.Cryptography.X509Certificates.X509Store($storeName, "LocalMachine")
        try {
            $store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
            $store.Add($cert)
            Write-Host "trusted in $storeName"
        } catch {
            Write-Host "[FAIL] trust in $storeName : $($_.Exception.Message)" -ForegroundColor Red
        } finally {
            $store.Close()
        }
    } else {
        Write-Host "already trusted in $storeName"
    }
}

Write-Host ""
Write-Host "=== [2/3] sign seraph_drv.sys ===" -ForegroundColor Cyan

$signtool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
if (-not (Test-Path $signtool)) {
    Write-Host "[FAIL] signtool not found" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $DriverPath)) {
    Write-Host "[FAIL] driver not found: $DriverPath" -ForegroundColor Red
    exit 1
}

$thumb = $cert.Thumbprint
Write-Host "signing with cert thumbprint $thumb"

# Sign by thumbprint (robust vs /n subject matching). Dry timestamp first,
# fall back to no timestamp if the server is unreachable.
& $signtool sign /sm /s My /sha1 $thumb /fd sha256 /tr http://timestamp.digicert.com /td sha256 $DriverPath
if ($LASTEXITCODE -ne 0) {
    Write-Host "[WARN] timestamped sign failed - retrying without timestamp..." -ForegroundColor Yellow
    & $signtool sign /sm /s My /sha1 $thumb /fd sha256 $DriverPath
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] signtool sign" -ForegroundColor Red
        exit 1
    }
}

Write-Host ""
Write-Host "=== [3/3] load via SeraphLoader ===" -ForegroundColor Cyan
if (-not (Test-Path $LoaderPath)) {
    Write-Host "[FAIL] loader not found: $LoaderPath" -ForegroundColor Red
    exit 1
}
& $LoaderPath $DriverPath
Exit $LASTEXITCODE