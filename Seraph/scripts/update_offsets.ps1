# update_offsets.ps1 - Downloads latest offsets from offsets.imtheo.lol
# Called as a pre-build event to keep offsets current with Roblox updates.

param(
    [string]$OutputPath
)

$Url = "https://offsets.imtheo.lol/version-d584fb6c717a43d9/offsets.hpp"
$MaxRetries = 2

for ($i = 0; $i -le $MaxRetries; $i++) {
    try {
        $response = Invoke-WebRequest -Uri $Url -UseBasicParsing -TimeoutSec 10 -ErrorAction Stop
        if ($response.StatusCode -eq 200 -and $response.Content.Length -gt 500) {
            $content = $response.Content
            if ($content -match 'inline std::string ClientVersion = "([^"]+)"') {
                $version = $Matches[1]
                Write-Host "Offsets updated: $version"

                $fflags = @"

    namespace FFlags {
         inline constexpr uintptr_t GameNetCompressionLodByteBudgetThresholdPct = 0x7991258;
         inline constexpr uintptr_t PhysicsSenderMaxBandwidthBps = 0x798fa88;
         inline constexpr uintptr_t NextGenReplicatorEnabledWrite4 = 0x7da93d0;
    }

"@
                $content = $content -replace "(namespace FakeDataModel \{[^}]+\})", "`$1`n$fflags"

                [System.IO.File]::WriteAllText($OutputPath, $content)
                exit 0
            } else {
                Write-Warning "Downloaded content missing ClientVersion line"
            }
        }
    } catch {
        Write-Warning "Attempt $($i+1)/$($MaxRetries+1) failed: $_"
    }
    if ($i -lt $MaxRetries) { Start-Sleep -Milliseconds 500 }
}

Write-Warning "Could not fetch offsets, using existing offsets.h"
exit 0
