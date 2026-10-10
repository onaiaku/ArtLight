$ErrorActionPreference = 'Stop'

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$installScript = Join-Path $repositoryRoot 'src_assets/windows/drivers/vhf-gamepad/install.ps1'
$workflowPath = Join-Path $repositoryRoot '.github/workflows/ci-windows.yml'
$windowsPackagingCmake = Join-Path $repositoryRoot 'cmake/packaging/windows.cmake'
$expectedSignPathFoundationSignerSubject = 'CN=SignPath Foundation, O=SignPath Foundation, L=Lewes, S=Delaware, C=US'
# The virtual gamepad payload is signed by ArtLight itself, not by SignPath.
$expectedPublisherSignerSubject = 'CN=ArtLight Driver Signing, O=onaiaku'
$expectedPublisherThumbprint = '4EBF1AC9B78D8982DAE701EEAB63A2DE7B5243D6'
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $installScript,
    [ref] $tokens,
    [ref] $parseErrors)
if ($parseErrors.Count -ne 0) {
    throw "install.ps1 has parse errors: $($parseErrors.Message -join '; ')"
}

$installFileText = Get-Content -LiteralPath $installScript -Raw
$expectedSubjectAssignment = "`$expectedPublisherSignerSubject = '$expectedPublisherSignerSubject'"
if ($installFileText.IndexOf($expectedSubjectAssignment, [System.StringComparison]::Ordinal) -lt 0) {
    throw 'install.ps1 does not pin the ArtLight driver-signing publisher subject.'
}
$expectedThumbprintAssignment = "`$expectedPublisherThumbprint = '$expectedPublisherThumbprint'"
if ($installFileText.IndexOf($expectedThumbprintAssignment, [System.StringComparison]::Ordinal) -lt 0) {
    throw 'install.ps1 does not pin the ArtLight driver-signing publisher thumbprint.'
}

$wanted = @(
    'Get-Thumbprint',
    'Test-SubjectMatches',
    'Get-ValidatedSharedPublisherCertificate',
    'Ensure-ProductionPublisherTrusted'
)
$definitions = @($ast.FindAll({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $wanted -contains $node.Name
}, $true))
if ($definitions.Count -ne $wanted.Count) {
    throw "Expected $($wanted.Count) testable trust functions, found $($definitions.Count)."
}
foreach ($definition in $definitions) {
    . ([scriptblock]::Create($definition.Extent.Text))
}

function New-TestSignature {
    param(
        [Parameter(Mandatory = $true)][string] $Thumbprint,
        [string] $Subject = $expectedSignPathFoundationSignerSubject,
        [System.Management.Automation.SignatureStatus] $Status = [System.Management.Automation.SignatureStatus]::Valid
    )
    return [PSCustomObject]@{
        Status = $Status
        SignerCertificate = [PSCustomObject]@{
            Thumbprint = $Thumbprint
            Subject = $Subject
        }
    }
}

$expected = New-TestSignature -Thumbprint '00112233445566778899AABBCCDDEEFF00112233'
$matching = New-TestSignature -Thumbprint '00 11 22 33 44 55 66 77 88 99 aa bb cc dd ee ff 00 11 22 33'
$selected = Get-ValidatedSharedPublisherCertificate `
    -CatalogSignature $expected `
    -ToolSignature $matching `
    -ExpectedSubject $expectedSignPathFoundationSignerSubject
if ((Get-Thumbprint -Certificate $selected) -ne '00112233445566778899AABBCCDDEEFF00112233') {
    throw 'Shared signer selection did not return the exact validated catalog signer.'
}

$mismatchRejected = $false
try {
    Get-ValidatedSharedPublisherCertificate `
        -CatalogSignature $expected `
        -ToolSignature (New-TestSignature -Thumbprint 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF') `
        -ExpectedSubject $expectedSignPathFoundationSignerSubject | Out-Null
} catch {
    $mismatchRejected = $true
}
if (-not $mismatchRejected) {
    throw 'Different catalog/setup signers were accepted.'
}

$unexpectedSubjectRejected = $false
try {
    Get-ValidatedSharedPublisherCertificate `
        -CatalogSignature $expected `
        -ToolSignature (New-TestSignature `
            -Thumbprint $expected.SignerCertificate.Thumbprint `
            -Subject 'CN=Unexpected Publisher, O=Unexpected Publisher, C=US') `
        -ExpectedSubject $expectedSignPathFoundationSignerSubject | Out-Null
} catch {
    $unexpectedSubjectRejected = $true
}
if (-not $unexpectedSubjectRejected) {
    throw 'An unexpected setup signer identity was accepted for publisher trust.'
}

$invalidRejected = $false
try {
    Get-ValidatedSharedPublisherCertificate `
        -CatalogSignature $expected `
        -ToolSignature (New-TestSignature -Thumbprint $expected.SignerCertificate.Thumbprint -Status HashMismatch) `
        -ExpectedSubject $expectedSignPathFoundationSignerSubject | Out-Null
} catch {
    $invalidRejected = $true
}
if (-not $invalidRejected) {
    throw 'A non-valid setup signature was accepted for publisher trust.'
}

# Exercise the store-mutating boundary with an unexpected identity. It must
# reject before constructing or opening LocalMachine\TrustedPublisher.
$unexpectedStoreSignerRejected = $false
try {
    Ensure-ProductionPublisherTrusted `
        -PublisherCertificate ([PSCustomObject]@{
            Thumbprint = $expected.SignerCertificate.Thumbprint
            Subject = 'CN=Unexpected Publisher, O=Unexpected Publisher, C=US'
        }) `
        -ExpectedSubject $expectedSignPathFoundationSignerSubject
} catch {
    $unexpectedStoreSignerRejected = $true
}
if (-not $unexpectedStoreSignerRejected) {
    throw 'The publisher store boundary accepted an unexpected signer identity.'
}

# A certificate renders its subject in a different RDN order depending on how
# it was loaded: the publisher .cer bundled in the package renders as
# 'O=onaiaku, CN=ArtLight Driver Signing', while the same certificate read from
# an Authenticode signature renders as 'CN=ArtLight Driver Signing, O=onaiaku'.
# The ordered comparison shipped in 1.5.3 rejected the certificate the package
# had just been signed with, so the driver never installed. Both renderings
# must be accepted; the name still has to be exactly the expected one.
foreach ($rendering in @(
    'CN=ArtLight Driver Signing, O=onaiaku',
    'O=onaiaku, CN=ArtLight Driver Signing',
    'cn=artlight driver signing,  o=ONAIaku'
)) {
    if (-not (Test-SubjectMatches -Actual $rendering -Expected $expectedPublisherSignerSubject)) {
        throw "A valid publisher subject rendering was rejected: $rendering"
    }
}
foreach ($lookalike in @(
    'CN=ArtLight Driver Signing, O=Somebody Else',
    'CN=ArtLight Driver Signing',
    'CN=ArtLight Driver Signing, O=onaiaku, C=US',
    'CN=ArtLight Driver Signing, O=onaiaku2'
)) {
    if (Test-SubjectMatches -Actual $lookalike -Expected $expectedPublisherSignerSubject) {
        throw "An unexpected publisher subject was accepted: $lookalike"
    }
}

# A certificate carrying the expected name but not the pinned thumbprint must
# be refused before the machine certificate store is touched.
$lookalikeSignerRejected = $false
try {
    Ensure-ProductionPublisherTrusted `
        -PublisherCertificate ([PSCustomObject]@{
            Thumbprint = 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF'
            Subject = $expectedPublisherSignerSubject
        }) `
        -ExpectedSubject $expectedPublisherSignerSubject
} catch {
    $lookalikeSignerRejected = $true
}
if (-not $lookalikeSignerRejected) {
    throw 'A certificate with the expected name but an unpinned thumbprint was accepted for publisher trust.'
}

$trustFunction = @($definitions | Where-Object Name -eq 'Ensure-ProductionPublisherTrusted')
if ($trustFunction.Count -ne 1) {
    throw 'Ensure-ProductionPublisherTrusted was not found exactly once.'
}
$trustText = $trustFunction[0].Extent.Text
foreach ($requiredTrustOperation in @(
    "X509Store]::new('TrustedPublisher', 'LocalMachine')",
    '$store.Add($PublisherCertificate)',
    '[System.Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly',
    'Failed to establish publisher trust'
)) {
    if ($trustText.IndexOf($requiredTrustOperation, [System.StringComparison]::Ordinal) -lt 0) {
        throw "Publisher trust function lacks required operation: $requiredTrustOperation"
    }
}
$subjectGuardOffset = $trustText.IndexOf('Test-SubjectMatches', [System.StringComparison]::Ordinal)
$thumbprintGuardOffset = $trustText.IndexOf('$thumbprint -ne $expectedPublisherThumbprint', [System.StringComparison]::Ordinal)
$storeConstructionOffset = $trustText.IndexOf("X509Store]::new('TrustedPublisher', 'LocalMachine')", [System.StringComparison]::Ordinal)
if ($subjectGuardOffset -lt 0 -or $storeConstructionOffset -lt 0 -or $subjectGuardOffset -ge $storeConstructionOffset) {
    throw 'Unexpected publisher identity is not rejected before the machine certificate store is accessed.'
}
if ($thumbprintGuardOffset -lt 0 -or $thumbprintGuardOffset -ge $storeConstructionOffset) {
    throw 'The pinned publisher thumbprint is not checked before the machine certificate store is accessed.'
}

# The publisher certificate is self-signed, so it is its own root. Windows
# cannot build a chain for a signature whose root it does not trust: the
# catalog and setup tool report UnknownError rather than Valid, and the
# production path then refused them. Both stores must be established, and only
# after the pinned thumbprint has authorised it.
$rootStoreOffset = $trustText.IndexOf("X509Store]::new('Root', 'LocalMachine')", [System.StringComparison]::Ordinal)
if ($rootStoreOffset -lt 0) {
    throw 'The production publisher certificate is never trusted as a root; a self-signed signer cannot report a valid signature without it.'
}
if ($rootStoreOffset -le $thumbprintGuardOffset) {
    throw 'Root trust is established before the pinned thumbprint is checked.'
}
if ($trustText.IndexOf('$rootStore.Add($PublisherCertificate)', [System.StringComparison]::Ordinal) -lt 0) {
    throw 'The root store block does not add the validated publisher certificate.'
}
if ($trustText.IndexOf('Root and LocalMachine\TrustedPublisher', [System.StringComparison]::Ordinal) -lt 0) {
    throw "The publisher trust report does not name both stores it established."
}

# Signature objects are snapshots. The production branch reads the catalog and
# setup tool signatures, then establishes trust, and the snapshots above keep
# reporting the pre-trust status. They must be re-read before validation, or
# the guard rejects a package that is in fact now trusted.
$packageFunction = @($ast.FindAll({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Assert-DriverPackage'
}, $true))
if ($packageFunction.Count -ne 1) {
    throw 'Assert-DriverPackage was not found exactly once.'
}
$packageText = $packageFunction[0].Extent.Text
$trustCallOffset = $packageText.IndexOf('Ensure-ProductionPublisherTrusted', [System.StringComparison]::Ordinal)
$validationOffset = $packageText.IndexOf('Get-ValidatedSharedPublisherCertificate', [System.StringComparison]::Ordinal)
if ($trustCallOffset -lt 0 -or $validationOffset -lt 0 -or $trustCallOffset -ge $validationOffset) {
    throw 'The production branch does not establish publisher trust before validating the signatures.'
}
$catalogRereadOffset = $packageText.IndexOf('$catalogSignature = Get-AuthenticodeSignature -LiteralPath $catalogPath', $trustCallOffset, [System.StringComparison]::Ordinal)
$toolRereadOffset = $packageText.IndexOf('$toolSignature = Get-AuthenticodeSignature -LiteralPath $toolPath', $trustCallOffset, [System.StringComparison]::Ordinal)
if ($catalogRereadOffset -lt $trustCallOffset -or $catalogRereadOffset -ge $validationOffset -or
    $toolRereadOffset -lt $trustCallOffset -or $toolRereadOffset -ge $validationOffset) {
    throw 'The production branch validates signature snapshots taken before publisher trust was established.'
}

$installFunction = @($ast.FindAll({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Install-DriverPackage'
}, $true))
if ($installFunction.Count -ne 1) {
    throw 'Install-DriverPackage was not found exactly once.'
}
$installText = $installFunction[0].Extent.Text
$trustOffset = $installText.IndexOf('Ensure-ProductionPublisherTrusted', [System.StringComparison]::Ordinal)
$pnpOffset = $installText.IndexOf("Invoke-PnpUtil -Arguments @('/add-driver'", [System.StringComparison]::Ordinal)
if ($trustOffset -lt 0 -or $pnpOffset -lt 0 -or $trustOffset -ge $pnpOffset) {
    throw 'Production publisher trust is not established before PnPUtil driver staging.'
}

$expectedGate = @'
      - name: Test VHF publisher trust policy
        if: inputs.build_tests
        shell: pwsh
        run: .\packaging\windows\virtual_gamepad_driver\tests\test_publisher_trust.ps1
'@

function Test-WorkflowContainsPublisherTrustGate {
    param(
        [Parameter(Mandatory = $true)][string] $WorkflowText,
        [Parameter(Mandatory = $true)][string] $ExpectedGate
    )

    $normalizedWorkflowText = $WorkflowText.Replace("`r`n", "`n").Replace("`r", "`n")
    $normalizedExpectedGate = $ExpectedGate.Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd()
    return $normalizedWorkflowText.IndexOf(
        $normalizedExpectedGate,
        [System.StringComparison]::Ordinal) -ge 0
}

$lfGateFixture = $expectedGate.Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd()
$crlfGateFixture = $lfGateFixture.Replace("`n", "`r`n")
foreach ($validGateFixture in @($lfGateFixture, $crlfGateFixture)) {
    if (-not (Test-WorkflowContainsPublisherTrustGate `
            -WorkflowText $validGateFixture `
            -ExpectedGate $expectedGate)) {
        throw 'The publisher trust workflow gate rejected a valid newline style.'
    }
}
$missingGateFixture = $lfGateFixture.Replace(
    '.\packaging\windows\virtual_gamepad_driver\tests\test_publisher_trust.ps1',
    '.\packaging\windows\virtual_gamepad_driver\tests\missing.ps1')
if (Test-WorkflowContainsPublisherTrustGate `
        -WorkflowText $missingGateFixture `
        -ExpectedGate $expectedGate) {
    throw 'The publisher trust workflow gate accepted a missing policy test.'
}

$workflowText = Get-Content -LiteralPath $workflowPath -Raw
if (-not (Test-WorkflowContainsPublisherTrustGate `
        -WorkflowText $workflowText `
        -ExpectedGate $expectedGate)) {
    throw 'The ordinary/release Windows build does not run this publisher trust policy test.'
}

function Get-WorkflowLiteralRunBlock {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string[]] $Lines,
        [Parameter(Mandatory = $true)][string] $StepName
    )

    $stepPattern = '^(?<indent>\s*)- name: ' + [regex]::Escape($StepName) + '\s*$'
    for ($stepIndex = 0; $stepIndex -lt $Lines.Count; ++$stepIndex) {
        $stepMatch = [regex]::Match($Lines[$stepIndex], $stepPattern)
        if (-not $stepMatch.Success) {
            continue
        }
        $stepIndent = $stepMatch.Groups['indent'].Value.Length
        for ($runIndex = $stepIndex + 1; $runIndex -lt $Lines.Count; ++$runIndex) {
            $line = $Lines[$runIndex]
            if ($line -match '^\s*$') {
                continue
            }
            $lineIndent = ([regex]::Match($line, '^\s*')).Value.Length
            if ($lineIndent -le $stepIndent) {
                break
            }
            if ($line -notmatch '^\s*run:\s*\|\s*$') {
                continue
            }

            $scriptLines = New-Object System.Collections.Generic.List[string]
            $scriptIndent = $null
            for ($scriptIndex = $runIndex + 1; $scriptIndex -lt $Lines.Count; ++$scriptIndex) {
                $scriptLine = $Lines[$scriptIndex]
                if ($scriptLine -notmatch '^\s*$') {
                    $currentIndent = ([regex]::Match($scriptLine, '^\s*')).Value.Length
                    if ($currentIndent -le $stepIndent) {
                        break
                    }
                    if ($null -eq $scriptIndent) {
                        $scriptIndent = $currentIndent
                    }
                }
                if ($null -eq $scriptIndent) {
                    $scriptLines.Add('')
                } else {
                    $scriptLines.Add($scriptLine.Substring([Math]::Min($scriptIndent, $scriptLine.Length)))
                }
            }
            return $scriptLines -join "`n"
        }
    }
    throw "Workflow literal run block was not found: $StepName"
}

$workflowLines = Get-Content -LiteralPath $workflowPath
$postSignScript = Get-WorkflowLiteralRunBlock -Lines $workflowLines -StepName 'Verify SignPath signatures'
$postSignTokens = $null
$postSignParseErrors = $null
[System.Management.Automation.Language.Parser]::ParseInput(
    $postSignScript,
    [ref] $postSignTokens,
    [ref] $postSignParseErrors) | Out-Null
if ($postSignParseErrors.Count -ne 0) {
    throw "Verify SignPath signatures has parse errors: $($postSignParseErrors.Message -join '; ')"
}
foreach ($requiredPostSignCheck in @(
    "`$expectedSignPathFoundationSigner = '$expectedSignPathFoundationSignerSubject'",
    "`$catalogSignature.SignerCertificate.Subject -ne `$expectedSignPathFoundationSigner",
    "`$vhfCatalogSignature.SignerCertificate.Thumbprint -cne `$vhfToolSignature.SignerCertificate.Thumbprint",
    "`$vhfCatalogSignature.SignerCertificate.Subject -ne `$expectedVhfPublisherSigner",
    "`$vhfToolSignature.SignerCertificate.Subject -ne `$expectedVhfPublisherSigner"
)) {
    if ($postSignScript.IndexOf($requiredPostSignCheck, [System.StringComparison]::Ordinal) -lt 0) {
        throw "Post-sign VHF verification lacks required signer check: $requiredPostSignCheck"
    }
}

# The VHF payload is consumer-signed, so its verification must live on the
# always-running path. The SignPath-gated step cannot be the only place that
# checks these signatures, because SignPath is not enabled.
$vhfSourceVerifyScript = Get-WorkflowLiteralRunBlock `
    -Lines $workflowLines `
    -StepName 'Verify unsigned MSI contains the pinned VHF package'
foreach ($requiredSourceCheck in @(
    "'publisher/ArtLightDriverSigning.cer',",
    "`$expectedVhfPublisherSubject = '$expectedPublisherSignerSubject'",
    "`$expectedVhfPublisherThumbprint = '$expectedPublisherThumbprint'",
    "verify '/v' '/pa' '/c' `$vhfCatalogPath `$payload",
    'catalog-bound VHF DLL must stay unsigned'
)) {
    if ($vhfSourceVerifyScript.IndexOf($requiredSourceCheck, [System.StringComparison]::Ordinal) -lt 0) {
        throw "The always-running VHF verification lacks a required check: $requiredSourceCheck"
    }
}

# The workflow checked above is upstream Vibepollo's copy, staged under the
# tree. GitHub runs workflows only from the repository root, so that copy never
# executes - which is how a driverless installer shipped while its tests passed.
# Guard the gate that actually runs as well.
$shippingWorkflowPath = Join-Path $repositoryRoot '../.github/workflows/artlight-server-windows.yml'
if (-not (Test-Path -LiteralPath $shippingWorkflowPath -PathType Leaf)) {
    throw "The shipping Windows workflow was not found: $shippingWorkflowPath"
}
$shippingWorkflowText = Get-Content -LiteralPath $shippingWorkflowPath -Raw
foreach ($requiredShippingCheck in @(
    'Assert the pinned virtual gamepad driver is inside the MSI',
    'publisher/ArtLightDriverSigning.cer',
    '4EBF1AC9B78D8982DAE701EEAB63A2DE7B5243D6',
    'osslsigncode verify',
    'ArtLightDriverSigning.cer'
)) {
    if ($shippingWorkflowText.IndexOf($requiredShippingCheck, [System.StringComparison]::Ordinal) -lt 0) {
        throw "The shipping workflow no longer verifies the driver payload: $requiredShippingCheck"
    }
}
if ($shippingWorkflowText.IndexOf('Re-sign the verified package with the ArtLight publisher certificate', [System.StringComparison]::Ordinal) -lt 0) {
    throw 'The shipping workflow no longer re-signs the verified producer package.'
}

$packagingText = Get-Content -LiteralPath $windowsPackagingCmake -Raw
if ($packagingText.IndexOf('test_publisher_trust.ps1', [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
    throw 'The publisher trust policy test was added to the installed VHF payload.'
}
if (Test-Path -LiteralPath (Join-Path $repositoryRoot 'src_assets/windows/drivers/vhf-gamepad/tests/test_publisher_trust.ps1')) {
    throw 'The publisher trust policy test remains under the volatile installed payload root.'
}

Write-Host 'VHF publisher trust policy checks passed.'
