<#
  Optional code signing for the Voxual binaries.

  Nothing is signed unless signing is configured, so an ordinary build is unaffected. Configuration
  is read from the environment, then from signing.local.json beside this script's parent folder
  (gitignored). One mode is used, in this order:

    Azure Trusted Signing   VOXUAL_SIGN_AZURE=1, VOXUAL_SIGN_AZURE_DLIB, VOXUAL_SIGN_AZURE_METADATA
    Certificate store       VOXUAL_SIGN_THUMBPRINT   or   VOXUAL_SIGN_SUBJECT
    PFX file                VOXUAL_SIGN_PFX, VOXUAL_SIGN_PFX_PASSWORD

  VOXUAL_SIGN_TIMESTAMP_URL overrides the RFC 3161 timestamp server
  (default http://timestamp.digicert.com).

  The installer carries Voxual.exe as a resource, so the app must be signed before the installer is
  linked: otherwise the copy that ends up in Program Files is unsigned, and Windows treats the
  installed app the same way it treats the download. build.bat signs the app, rebuilds the
  installer around it and signs that, then calls this script with -VerifyEmbedded to prove the
  installer really carries the signed app.

  Exit codes: 0 signed (or verification passed), 3 signing not configured, 1 failure.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)][string[]]$Files,
    [switch]$DryRun,
    [string]$VerifyEmbedded
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$EXIT_OK = 0
$EXIT_FAILED = 1
$EXIT_NOT_CONFIGURED = 3

function Write-Step($Text) { Write-Host ''; Write-Host "== $Text" }

function Get-SignConfig {
    $cfg = @{}
    $local = Join-Path (Split-Path -Parent $PSScriptRoot) 'signing.local.json'
    if (Test-Path -LiteralPath $local) {
        $json = Get-Content -Raw -LiteralPath $local | ConvertFrom-Json
        foreach ($prop in $json.PSObject.Properties) { $cfg[$prop.Name] = [string]$prop.Value }
    }
    $names = @(
        'VOXUAL_SIGN_AZURE', 'VOXUAL_SIGN_AZURE_DLIB', 'VOXUAL_SIGN_AZURE_METADATA',
        'VOXUAL_SIGN_THUMBPRINT', 'VOXUAL_SIGN_SUBJECT',
        'VOXUAL_SIGN_PFX', 'VOXUAL_SIGN_PFX_PASSWORD', 'VOXUAL_SIGN_TIMESTAMP_URL'
    )
    foreach ($name in $names) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value) { $cfg[$name] = $value }
    }
    return $cfg
}

function Resolve-SignTool {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path -LiteralPath $kits) {
        $found = Get-ChildItem -LiteralPath $kits -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
                 Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

# Returns the configured mode, or $null when nothing is configured.
function Get-SignMode($cfg) {
    if ($cfg['VOXUAL_SIGN_AZURE'] -eq '1' -or $cfg['VOXUAL_SIGN_AZURE'] -eq 'true') {
        if (-not $cfg['VOXUAL_SIGN_AZURE_DLIB'] -or -not $cfg['VOXUAL_SIGN_AZURE_METADATA']) {
            throw 'VOXUAL_SIGN_AZURE needs VOXUAL_SIGN_AZURE_DLIB and VOXUAL_SIGN_AZURE_METADATA'
        }
        return 'azure'
    }
    if ($cfg['VOXUAL_SIGN_THUMBPRINT'] -or $cfg['VOXUAL_SIGN_SUBJECT']) { return 'store' }
    if ($cfg['VOXUAL_SIGN_PFX']) { return 'pfx' }
    return $null
}

function Get-SignArguments($cfg, $mode) {
    $list = @('sign', '/fd', 'SHA256', '/v')
    $stamp = $cfg['VOXUAL_SIGN_TIMESTAMP_URL']
    if (-not $stamp) { $stamp = 'http://timestamp.digicert.com' }
    $list += @('/tr', $stamp, '/td', 'SHA256')
    if ($mode -eq 'azure') {
        $list += @('/dlib', $cfg['VOXUAL_SIGN_AZURE_DLIB'], '/dmdf', $cfg['VOXUAL_SIGN_AZURE_METADATA'])
    } elseif ($mode -eq 'store') {
        if ($cfg['VOXUAL_SIGN_THUMBPRINT']) { $list += @('/sha1', $cfg['VOXUAL_SIGN_THUMBPRINT']) }
        else { $list += @('/n', $cfg['VOXUAL_SIGN_SUBJECT']) }
    } else {
        $list += @('/f', $cfg['VOXUAL_SIGN_PFX'])
        if ($cfg['VOXUAL_SIGN_PFX_PASSWORD']) { $list += @('/p', $cfg['VOXUAL_SIGN_PFX_PASSWORD']) }
    }
    return $list
}

function Assert-Signed($Path) {
    $name = [System.IO.Path]::GetFileName($Path)
    $sig = Get-AuthenticodeSignature -LiteralPath $Path
    if ($sig.Status -eq 'NotSigned' -or $sig.Status -eq 'HashMismatch') {
        throw "$name is still not properly signed ($($sig.Status))"
    }
    if ($sig.Status -ne 'Valid') {
        Write-Warning "$name signature status is $($sig.Status); check the certificate chain and timestamp"
    }
    $who = 'unknown signer'
    if ($sig.SignerCertificate) { $who = $sig.SignerCertificate.Subject }
    Write-Host "   $name -> $($sig.Status), $who"
}

# Locates the app inside the installer by a slice of its code and compares the whole image.
function Test-EmbeddedPayload($InstallerPath, $AppPath) {
    $app = [System.IO.File]::ReadAllBytes($AppPath)
    $setup = [System.IO.File]::ReadAllBytes($InstallerPath)
    $needleAt = 0x1000
    $needleLen = 48
    if ($app.Length -lt ($needleAt + $needleLen)) { throw "$AppPath is too small to probe" }
    $first = $app[$needleAt]
    $found = -1
    $from = 0
    while ($from -le ($setup.Length - $needleLen)) {
        $hit = [Array]::IndexOf($setup, $first, $from)
        if ($hit -lt 0 -or $hit -gt ($setup.Length - $needleLen)) { break }
        $same = $true
        for ($j = 1; $j -lt $needleLen; $j++) {
            if ($setup[$hit + $j] -ne $app[$needleAt + $j]) { $same = $false; break }
        }
        if ($same) { $found = $hit; break }
        $from = $hit + 1
    }
    if ($found -lt 0) { throw 'the installer does not carry that app' }
    $start = $found - $needleAt
    if ($start -lt 0 -or ($start + $app.Length) -gt $setup.Length) { throw 'the carried payload is truncated' }
    $region = New-Object byte[] $app.Length
    [Array]::Copy($setup, $start, $region, 0, $app.Length)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $wanted = [BitConverter]::ToString($sha.ComputeHash($app)).Replace('-', '')
    $have = [BitConverter]::ToString($sha.ComputeHash($region)).Replace('-', '')
    return @{ Start = $start; Wanted = $wanted; Have = $have; Match = ($wanted -eq $have) }
}

if ($VerifyEmbedded) {
    Write-Step 'Checking that the installer carries the app that was just signed'
    $result = Test-EmbeddedPayload $Files[0] $VerifyEmbedded
    Write-Host ('   payload at 0x{0:X}' -f $result.Start)
    Write-Host "   embedded $($result.Have)"
    Write-Host "   app      $($result.Wanted)"
    if (-not $result.Match) {
        Write-Host '   MISMATCH: the installer would ship a different app than the one that was signed'
        exit $EXIT_FAILED
    }
    Write-Host '   match'
    exit $EXIT_OK
}

$config = Get-SignConfig
$mode = Get-SignMode $config

if (-not $mode) {
    Write-Host 'Code signing is not configured - leaving the binaries unsigned.'
    Write-Host "See 'Code signing (optional)' in README.md."
    exit $EXIT_NOT_CONFIGURED
}

$signtool = Resolve-SignTool
if (-not $signtool) {
    Write-Host 'signtool.exe was not found. Install the Windows SDK signing tools.'
    exit $EXIT_FAILED
}

$signArgs = Get-SignArguments $config $mode
Write-Step "Signing $($Files.Count) file(s), mode '$mode'"

foreach ($file in $Files) {
    if (-not (Test-Path -LiteralPath $file)) { Write-Host "missing: $file"; exit $EXIT_FAILED }
    if ($DryRun) {
        Write-Host "   would run: `"$signtool`" $($signArgs -join ' ') `"$file`""
        continue
    }
    & $signtool @signArgs $file
    if ($LASTEXITCODE -ne 0) { Write-Host "signing failed for $file (signtool exit $LASTEXITCODE)"; exit $EXIT_FAILED }
}

if ($DryRun) { Write-Host ''; Write-Host 'Dry run only, nothing was signed.'; exit $EXIT_OK }

Write-Step 'Verifying the signatures'
foreach ($file in $Files) { Assert-Signed $file }
Write-Host ''
Write-Host 'Signed.'
exit $EXIT_OK
