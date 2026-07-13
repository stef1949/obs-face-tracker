param(
    [string]$OutputDir = '',
    [switch]$GpuCuda12,
    [switch]$GpuCuda13
)

$ErrorActionPreference = 'Stop'
$version = '1.26.0'
if ($GpuCuda12 -and $GpuCuda13) {
    throw 'Choose only one GPU runtime: -GpuCuda12 or -GpuCuda13.'
}
if ($GpuCuda12) {
    $expectedHash = '1133B1BCB0FB6F82B1C5B470B7CC15F9080A58B27DBC7B579A1FD63125EC2A15'
    $archiveName = "onnxruntime-win-x64-gpu-$version.zip"
    $packageUrl = "https://github.com/microsoft/onnxruntime/releases/download/v$version/$archiveName"
    if (-not $OutputDir) {
        $OutputDir = "$PSScriptRoot\..\..\.deps\onnxruntime-gpu-cuda12"
    }
} elseif ($GpuCuda13) {
    $expectedHash = '4FA096030EE766B2E590D71FB6676BBD00595C92AB87ACF497FE075E98834D8B'
    $archiveName = "onnxruntime-win-x64-gpu_cuda13-$version.zip"
    $packageUrl = "https://github.com/microsoft/onnxruntime/releases/download/v$version/$archiveName"
    if (-not $OutputDir) {
        $OutputDir = "$PSScriptRoot\..\..\.deps\onnxruntime-gpu-cuda13"
    }
} else {
    $expectedHash = '50CC3772668F04B8373AD65A36793F94699BC4E818F6E691FC68F1578C38CE42'
    $archiveName = "Microsoft.ML.OnnxRuntime.$version.zip"
    $packageUrl = "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime/$version"
    if (-not $OutputDir) {
        $OutputDir = "$PSScriptRoot\..\..\.deps\onnxruntime"
    }
}
$package = Join-Path $env:TEMP $archiveName

Invoke-WebRequest -UseBasicParsing -Uri $packageUrl -OutFile $package
$actualHash = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash
if ($actualHash -ne $expectedHash) {
    throw "ONNX Runtime package checksum mismatch. Expected $expectedHash, got $actualHash"
}

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
Expand-Archive -LiteralPath $package -DestinationPath $OutputDir -Force
if ($GpuCuda12 -or $GpuCuda13) {
    $runtimeRoot = Join-Path $OutputDir "onnxruntime-win-x64-gpu-$version"
    if (-not (Test-Path -LiteralPath (Join-Path $runtimeRoot 'lib\onnxruntime_providers_cuda.dll'))) {
        throw "The CUDA provider was not found in $runtimeRoot"
    }
} else {
    $runtimeRoot = $OutputDir
}
Write-Output (Resolve-Path -LiteralPath $runtimeRoot).Path
