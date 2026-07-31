param(
    [string]$OutputDir = '',
    [switch]$ValidateMetadataOnly
)

$ErrorActionPreference = 'Stop'

if (-not $OutputDir) {
    $OutputDir = "$PSScriptRoot\..\..\.deps\cuda12-runtime"
}

$packages = @(
    @{
        Name = 'nvidia-cuda-runtime-cu12'
        Version = '12.9.79'
        SHA256 = '8E018AF8FA02363876860388BD10CCB89EB9AB8FB0AA749AAF58430A9F7C4891'
        DllPatterns = @('cudart64_12.dll')
    }
    @{
        Name = 'nvidia-cublas-cu12'
        Version = '12.9.2.10'
        SHA256 = '623F43027D40D44CEADF0043F002BD25CF353E8F13CE90B9A87057019F560661'
        DllPatterns = @('cublas64_12.dll', 'cublasLt64_12.dll', 'nvblas64_12.dll')
    }
    @{
        Name = 'nvidia-cufft-cu12'
        Version = '11.4.1.4'
        SHA256 = '8E5BFAAC795E93F80611F807D42844E8E27E340E0CDE270DCB6C65386D795B80'
        DllPatterns = @('cufft64_11.dll', 'cufftw64_11.dll')
    }
    @{
        Name = 'nvidia-curand-cu12'
        Version = '10.3.10.19'
        SHA256 = 'E8129E6AC40DC123BD948E33D3E11B4AA617D87A583FA2F21B3210E90C743CDE'
        DllPatterns = @('curand64_10.dll')
    }
    @{
        Name = 'nvidia-cudnn-cu12'
        Version = '9.24.0.43'
        SHA256 = 'CBD41A0AB084422C936DC9FB2FC89BE5EA9A85BC421C6F23D0243BDFC945FBEF'
        DllPatterns = @(
            'cudnn64_9.dll'
            'cudnn_adv64_9.dll'
            'cudnn_cnn64_9.dll'
            'cudnn_engines_precompiled64_9.dll'
            'cudnn_engines_runtime_compiled64_9.dll'
            'cudnn_engines_tensor_ir64_9.dll'
            'cudnn_ext64_9.dll'
            'cudnn_graph64_9.dll'
            'cudnn_heuristic64_9.dll'
            'cudnn_ops64_9.dll'
        )
    }
    @{
        Name = 'nvidia-nvjitlink-cu12'
        Version = '12.9.86'
        SHA256 = 'CC6FCEC260CA843C10E34C936921A1C426B351753587FDD638E8CFF7B16BB9DB'
        DllPatterns = @('nvJitLink_120_0.dll')
    }
    @{
        Name = 'nvidia-cuda-nvrtc-cu12'
        Version = '12.9.86'
        SHA256 = '72972EBDCF504D69462D3BCD67E7B81EDD25D0FB85A2C46D3EA3517666636349'
        DllPatterns = @(
            'nvrtc64_120_0.dll'
            'nvrtc64_120_0.alt.dll'
            'nvrtc-builtins64_129.dll'
        )
    }
)

$requiredDllPatterns = @(
    'cudart64_12.dll'
    'cublas64_12.dll'
    'cublasLt64_12.dll'
    'nvblas64_12.dll'
    'cufft64_11.dll'
    'cufftw64_11.dll'
    'curand64_10.dll'
    'cudnn64_9.dll'
    'cudnn_adv64_9.dll'
    'cudnn_cnn64_9.dll'
    'cudnn_engines_precompiled64_9.dll'
    'cudnn_engines_runtime_compiled64_9.dll'
    'cudnn_engines_tensor_ir64_9.dll'
    'cudnn_ext64_9.dll'
    'cudnn_graph64_9.dll'
    'cudnn_heuristic64_9.dll'
    'cudnn_ops64_9.dll'
    'nvJitLink_120_0.dll'
    'nvrtc64_120_0.dll'
    'nvrtc64_120_0.alt.dll'
    'nvrtc-builtins64_129.dll'
)

$outputRoot = [IO.Path]::GetFullPath($OutputDir)
$outputBin = Join-Path $outputRoot 'bin'
$outputLicenses = Join-Path $outputRoot 'licenses'
$workDir = Join-Path $env:TEMP "obs-face-tracker-cuda12-$PID-$([Guid]::NewGuid().ToString('N'))"

$cachedRuntimeComplete =
    Test-Path -LiteralPath (Join-Path $outputLicenses 'CUDA-RUNTIME-MANIFEST.txt') -PathType Leaf
if ($cachedRuntimeComplete) {
    foreach ($pattern in $requiredDllPatterns) {
        if (-not (Get-ChildItem -LiteralPath $outputBin -Filter $pattern -File -ErrorAction SilentlyContinue)) {
            $cachedRuntimeComplete = $false
            break
        }
    }
}
if ($cachedRuntimeComplete) {
    foreach ($package in $packages) {
        $licensePath = Join-Path $outputLicenses "LICENSE-$($package.Name).txt"
        if (-not (Test-Path -LiteralPath $licensePath -PathType Leaf)) {
            $cachedRuntimeComplete = $false
            break
        }
    }
}
if ($cachedRuntimeComplete -and -not $ValidateMetadataOnly) {
    Write-Output $outputRoot
    return
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
New-Item -ItemType Directory -Path $outputBin, $outputLicenses, $workDir -Force | Out-Null

$manifest = @(
    'obs-face-tracker CUDA 12 runtime manifest'
    '=========================================='
    ''
    'These NVIDIA runtime packages are distributed under their included license terms.'
    ''
)

try {
    foreach ($package in $packages) {
        $name = $package.Name
        $version = $package.Version
        $expectedHash = $package.SHA256
        $metadataUrl = "https://pypi.org/pypi/$name/$version/json"
        $metadata = Invoke-RestMethod -UseBasicParsing -Uri $metadataUrl
        $asset = $metadata.urls |
            Where-Object { $_.filename -like '*-win_amd64.whl' } |
            Select-Object -First 1
        if (-not $asset) {
            throw "No Windows x64 wheel is available for $name $version"
        }
        if ($asset.digests.sha256.ToUpperInvariant() -ne $expectedHash) {
            throw "$name metadata checksum mismatch. Expected $expectedHash, got $($asset.digests.sha256)"
        }

        if ($ValidateMetadataOnly) {
            $manifest += "$expectedHash  $($asset.filename)"
            continue
        }

        $licenseDestination = Join-Path $outputLicenses "LICENSE-$name.txt"
        $packageComplete = Test-Path -LiteralPath $licenseDestination -PathType Leaf
        if ($packageComplete) {
            foreach ($pattern in $package.DllPatterns) {
                if (-not (Get-ChildItem -LiteralPath $outputBin -Filter $pattern -File -ErrorAction SilentlyContinue)) {
                    $packageComplete = $false
                    break
                }
            }
        }
        if ($packageComplete) {
            $manifest += "$expectedHash  $($asset.filename)"
            continue
        }

        $wheelPath = Join-Path $workDir $asset.filename
        Invoke-WebRequest -UseBasicParsing -Uri $asset.url -OutFile $wheelPath
        $actualHash = (Get-FileHash -LiteralPath $wheelPath -Algorithm SHA256).Hash
        if ($actualHash -ne $expectedHash) {
            throw "$name checksum mismatch. Expected $expectedHash, got $actualHash"
        }

        $extractDir = Join-Path $workDir "$name-$version"
        [IO.Compression.ZipFile]::ExtractToDirectory($wheelPath, $extractDir)

        $nativeDlls = Get-ChildItem -LiteralPath $extractDir -Recurse -Filter '*.dll' |
            Where-Object { $_.FullName -match '[\\/]nvidia[\\/].*[\\/]bin[\\/]' }
        if (-not $nativeDlls) {
            throw "$name did not contain NVIDIA runtime DLLs"
        }
        foreach ($dll in $nativeDlls) {
            $destination = Join-Path $outputBin $dll.Name
            if (Test-Path -LiteralPath $destination) {
                $existingHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
                $incomingHash = (Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash
                if ($existingHash -ne $incomingHash) {
                    throw "Conflicting CUDA runtime DLLs named $($dll.Name)"
                }
            } else {
                Copy-Item -LiteralPath $dll.FullName -Destination $destination
            }
        }

        $license = Get-ChildItem -LiteralPath $extractDir -Recurse -File |
            Where-Object {
                $_.Name -eq 'License.txt' -and
                $_.FullName -match '\.dist-info[\\/](licenses[\\/])?License\.txt$'
            } |
            Select-Object -First 1
        if (-not $license) {
            throw "$name did not contain its License.txt"
        }
        Copy-Item -LiteralPath $license.FullName -Destination $licenseDestination -Force

        $manifest += "$expectedHash  $($asset.filename)"
    }

    if ($ValidateMetadataOnly) {
        Write-Output "Validated metadata for $($packages.Count) pinned NVIDIA CUDA runtime wheels."
        return
    }

    foreach ($pattern in $requiredDllPatterns) {
        if (-not (Get-ChildItem -LiteralPath $outputBin -Filter $pattern -File)) {
            throw "The CUDA runtime bundle is missing $pattern"
        }
    }

    $manifest += ''
    $manifest += 'Bundled DLL SHA-256 values:'
    $manifest += ''
    foreach ($dll in Get-ChildItem -LiteralPath $outputBin -Filter '*.dll' | Sort-Object Name) {
        $hash = (Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $manifest += "$hash  bin/$($dll.Name)"
    }
    Set-Content -LiteralPath (Join-Path $outputLicenses 'CUDA-RUNTIME-MANIFEST.txt') `
        -Value $manifest -Encoding UTF8
} finally {
    if (Test-Path -LiteralPath $workDir) {
        Remove-Item -LiteralPath $workDir -Recurse -Force
    }
}

Write-Output $outputRoot
