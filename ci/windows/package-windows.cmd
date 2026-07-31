call "build\ci\ci_includes.generated.cmd"

if not exist "release\obs-plugins\64bit\%PluginName%.dll" (
	echo ERROR: Plugin DLL is not in release\obs-plugins\64bit. 1>&2
	exit /b 1
)

if not exist "release\data\obs-plugins\%PluginName%\locale\en-US.ini" (
	echo ERROR: Plugin data is not in release\data\obs-plugins\%PluginName%. 1>&2
	exit /b 1
)

for %%F in (
	"release\obs-plugins\64bit\onnxruntime.dll"
	"release\obs-plugins\64bit\onnxruntime_providers_shared.dll"
	"release\obs-plugins\64bit\onnxruntime_providers_cuda.dll"
	"release\obs-plugins\64bit\cudart64_12.dll"
	"release\obs-plugins\64bit\cublas64_12.dll"
	"release\obs-plugins\64bit\cublasLt64_12.dll"
	"release\obs-plugins\64bit\nvblas64_12.dll"
	"release\obs-plugins\64bit\cufft64_11.dll"
	"release\obs-plugins\64bit\cufftw64_11.dll"
	"release\obs-plugins\64bit\curand64_10.dll"
	"release\obs-plugins\64bit\cudnn64_9.dll"
	"release\obs-plugins\64bit\cudnn_adv64_9.dll"
	"release\obs-plugins\64bit\cudnn_cnn64_9.dll"
	"release\obs-plugins\64bit\cudnn_engines_precompiled64_9.dll"
	"release\obs-plugins\64bit\cudnn_engines_runtime_compiled64_9.dll"
	"release\obs-plugins\64bit\cudnn_engines_tensor_ir64_9.dll"
	"release\obs-plugins\64bit\cudnn_ext64_9.dll"
	"release\obs-plugins\64bit\cudnn_graph64_9.dll"
	"release\obs-plugins\64bit\cudnn_heuristic64_9.dll"
	"release\obs-plugins\64bit\cudnn_ops64_9.dll"
	"release\obs-plugins\64bit\nvJitLink_120_0.dll"
	"release\obs-plugins\64bit\nvrtc64_120_0.dll"
	"release\obs-plugins\64bit\nvrtc64_120_0.alt.dll"
	"release\obs-plugins\64bit\nvrtc-builtins64_129.dll"
	"release\data\obs-plugins\%PluginName%\yunet_model\face_detection_yunet_2026may.onnx"
	"release\data\obs-plugins\%PluginName%\scrfd_model\scrfd_2.5g_bnkps.onnx"
	"release\data\obs-plugins\%PluginName%\cuda-runtime-licenses\CUDA-RUNTIME-MANIFEST.txt"
	"release\data\obs-plugins\%PluginName%\cuda-runtime-licenses\LICENSE-nvidia-cublas-cu12.txt"
	"release\data\obs-plugins\%PluginName%\cuda-runtime-licenses\LICENSE-nvidia-cuda-nvrtc-cu12.txt"
	"release\data\obs-plugins\%PluginName%\cuda-runtime-licenses\LICENSE-nvidia-cuda-runtime-cu12.txt"
	"release\data\obs-plugins\%PluginName%\cuda-runtime-licenses\LICENSE-nvidia-cudnn-cu12.txt"
	"release\data\obs-plugins\%PluginName%\cuda-runtime-licenses\LICENSE-nvidia-cufft-cu12.txt"
	"release\data\obs-plugins\%PluginName%\cuda-runtime-licenses\LICENSE-nvidia-curand-cu12.txt"
	"release\data\obs-plugins\%PluginName%\cuda-runtime-licenses\LICENSE-nvidia-nvjitlink-cu12.txt"
) do (
	if not exist "%%~F" (
		echo ERROR: Required CUDA package file is missing: %%~F 1>&2
		exit /b 1
	)
)

mkdir package
cd package

git describe --tags --always > package-version.txt
set /p PackageVersion=<package-version.txt
del package-version.txt

copy ..\LICENSE          ..\release\data\obs-plugins\%PluginName%\LICENCE-%PluginName%.txt

REM Package ZIP archive
7z a "%PluginName%-%PackageVersion%-obs%1-Windows-Symbols.zip" "..\release\obs-plugins\64bit\*.pdb"
7z a "%PluginName%-%PackageVersion%-obs%1-Windows.zip" "..\release\*" -xr!*.pdb

REM Build installer
iscc ..\build\installer-Windows.generated.iss /O. /F"%PluginName%-%PackageVersion%-obs%1-Windows-Installer"

certutil.exe -hashfile "%PluginName%-%PackageVersion%-obs%1-Windows.zip" SHA256
certutil.exe -hashfile "%PluginName%-%PackageVersion%-obs%1-Windows-Installer.exe" SHA256
