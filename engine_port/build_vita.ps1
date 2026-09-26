# Build and package the native Far Cry PS Vita port.
#
# The default invocation creates the canonical player-facing release at
# engine_port/vita_kit/FarCry.vpk.  Vita3K deployment is opt-in because an
# emulator installation is not required to produce a valid hardware package.
#
# Usage:
#   pwsh -File engine_port/build_vita.ps1
#   pwsh -File engine_port/build_vita.ps1 -DeployVita3K
#   pwsh -File engine_port/build_vita.ps1 -Telemetry -AutoTestTraining

[CmdletBinding()]
param(
	[ValidateSet(50, 66, 75, 100)]
	[int]$RenderScale = 50,

	[switch]$Telemetry,
	[switch]$AutoTestTraining,
	[switch]$Vita3KLab,
	[switch]$DeployVita3K
)

$ErrorActionPreference = "Stop"

$IsDiagnostic = $Telemetry -or $AutoTestTraining -or $Vita3KLab
$ProfileName = if ($IsDiagnostic) { "diagnostic" } else { "release" }
$BuildDir = Join-Path $PSScriptRoot "build_$ProfileName"
$KitDir = Join-Path $PSScriptRoot "vita_kit"
$SfoPath = Join-Path $KitDir "sce_sys\param.sfo"
$IconPath = Join-Path $KitDir "sce_sys\icon0.png"
$DeployDir = Join-Path $env:APPDATA "Vita3K\Vita3K\ux0\app\FCRY00002"

if (-not $env:VITASDK) {
	throw "VITASDK environment variable is not set."
}
$PackTool = Join-Path $env:VITASDK "bin\vita-pack-vpk.exe"

foreach ($RequiredPath in @($SfoPath, $IconPath, $PackTool)) {
	if (-not (Test-Path -LiteralPath $RequiredPath)) {
		throw "Required packaging input is missing: $RequiredPath"
	}
}

$TelemetryValue = if ($Telemetry) { "ON" } else { "OFF" }
$AutoTestValue = if ($AutoTestTraining) { "ON" } else { "OFF" }
$Vita3KLabValue = if ($Vita3KLab) { "ON" } else { "OFF" }

$CMakeArguments = @(
	"-S", $PSScriptRoot,
	"-B", $BuildDir,
	"-G", "Ninja",
	"-DCMAKE_BUILD_TYPE=Release",
	"-DFARCRY_VITA_RENDER_SCALE=$RenderScale",
	"-DFARCRY_VITA_PERF_TELEMETRY=$TelemetryValue",
	"-DFARCRY_VITA_AUTOTEST_TRAINING=$AutoTestValue",
	"-DFARCRY_VITA3K_LAB=$Vita3KLabValue"
)
& cmake @CMakeArguments
if ($LASTEXITCODE -ne 0) {
	throw "CMake configuration failed."
}

# Four jobs stays inside the host memory envelope for the large CryEngine files.
& cmake --build $BuildDir --parallel 4
if ($LASTEXITCODE -ne 0) {
	throw "Vita build failed."
}

$SelfPath = Join-Path $BuildDir "farcry_vita.self"
if (-not (Test-Path -LiteralPath $SelfPath)) {
	throw "Build reported success but $SelfPath is missing."
}

if ($Vita3KLab) {
	$EbootPath = Join-Path $BuildDir "eboot_vita3k.bin"
	$VpkPath = Join-Path $BuildDir "FarCry_vita3k.vpk"
}
elseif ($IsDiagnostic) {
	$EbootPath = Join-Path $BuildDir "eboot_diagnostic.bin"
	$VpkPath = Join-Path $BuildDir "FarCry_diagnostic.vpk"
}
else {
	$EbootPath = Join-Path $KitDir "eboot.bin"
	$VpkPath = Join-Path $KitDir "FarCry.vpk"
}

Copy-Item -LiteralPath $SelfPath -Destination $EbootPath -Force
& $PackTool -s $SfoPath -b $EbootPath -a "$IconPath=sce_sys/icon0.png" $VpkPath
if ($LASTEXITCODE -ne 0) {
	throw "VPK packaging failed."
}

if ($DeployVita3K) {
	if (-not (Test-Path -LiteralPath $DeployDir)) {
		throw "Deploy target $DeployDir does not exist. Install FCRY00002 in Vita3K first."
	}
	Copy-Item -LiteralPath $EbootPath -Destination (Join-Path $DeployDir "eboot.bin") -Force
	Write-Host "Deployed $EbootPath -> $DeployDir\eboot.bin"
}

$VpkHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $VpkPath).Hash
Write-Host "Built $VpkPath"
Write-Host "SHA-256 $VpkHash"
