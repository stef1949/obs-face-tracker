param(
    [string]$OutputDir = '',
    [switch]$GpuCuda12,
    [switch]$GpuCuda13
)

$ErrorActionPreference = 'Stop'
$version = '1.28.0'
if ($GpuCuda12 -and $GpuCuda13) {
    throw 'Choose only one GPU runtime: -GpuCuda12 or -GpuCuda13.'
}
if ($GpuCuda12) {
    $expectedHash = '6B7BF16D6D30180DB7F386FB179AA4E4F1313F0924531A2879B7B090B56518C1'
    $archiveName = "onnxruntime-win-x64-gpu_cuda12-$version.zip"
    $packageUrl = "https://github.com/microsoft/onnxruntime/releases/download/v$version/$archiveName"
    if (-not $OutputDir) {
        $OutputDir = "$PSScriptRoot\..\..\.deps\onnxruntime-gpu-cuda12"
    }
} elseif ($GpuCuda13) {
    $expectedHash = '137F0822A4923B1D84D3E09496E0792EBBB221EB3A61A0657F71A12AB68AB1E2'
    $archiveName = "onnxruntime-win-x64-gpu_cuda13-$version.zip"
    $packageUrl = "https://github.com/microsoft/onnxruntime/releases/download/v$version/$archiveName"
    if (-not $OutputDir) {
        $OutputDir = "$PSScriptRoot\..\..\.deps\onnxruntime-gpu-cuda13"
    }
} else {
    $expectedHash = '769D1D3EA8AB6CD69F737C9DD4D4462AA4AD0CCFA106EAF506EFC40D7BEAD5DB'
    $archiveName = "Microsoft.ML.OnnxRuntime.$version.zip"
    $packageUrl = "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime/$version"
    if (-not $OutputDir) {
        $OutputDir = "$PSScriptRoot\..\..\.deps\onnxruntime"
    }
}

if ($GpuCuda12 -or $GpuCuda13) {
    $runtimeRoot = Join-Path $OutputDir ([System.IO.Path]::GetFileNameWithoutExtension($archiveName))
    $requiredFiles = @(
        'include\onnxruntime_cxx_api.h'
        'lib\onnxruntime.lib'
        'lib\onnxruntime.dll'
        'lib\onnxruntime_providers_shared.dll'
        'lib\onnxruntime_providers_cuda.dll'
    )
} else {
    $runtimeRoot = $OutputDir
    $requiredFiles = @(
        'Microsoft.ML.OnnxRuntime.nuspec'
        'build\native\include\onnxruntime_cxx_api.h'
        'runtimes\win-x64\native\onnxruntime.lib'
        'runtimes\win-x64\native\onnxruntime.dll'
    )
}

$cachedRuntimeComplete = $true
foreach ($requiredFile in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtimeRoot $requiredFile) -PathType Leaf)) {
        $cachedRuntimeComplete = $false
        break
    }
}
if ($cachedRuntimeComplete -and -not ($GpuCuda12 -or $GpuCuda13)) {
    $nuspecPath = Join-Path $runtimeRoot 'Microsoft.ML.OnnxRuntime.nuspec'
    $versionPattern = '<version>' + [regex]::Escape($version) + '</version>'
    $cachedRuntimeComplete = Select-String -LiteralPath $nuspecPath -Pattern $versionPattern -Quiet
}
if ($cachedRuntimeComplete) {
    Write-Output (Resolve-Path -LiteralPath $runtimeRoot).Path
    return
}

$package = Join-Path $env:TEMP $archiveName

Invoke-WebRequest -UseBasicParsing -Uri $packageUrl -OutFile $package
$actualHash = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash
if ($actualHash -ne $expectedHash) {
    throw "ONNX Runtime package checksum mismatch. Expected $expectedHash, got $actualHash"
}

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
Expand-Archive -LiteralPath $package -DestinationPath $OutputDir -Force
foreach ($requiredFile in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtimeRoot $requiredFile) -PathType Leaf)) {
        throw "The ONNX Runtime package is missing $requiredFile"
    }
}
Write-Output (Resolve-Path -LiteralPath $runtimeRoot).Path
