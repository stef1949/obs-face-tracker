call "build\ci\ci_includes.generated.cmd"

if not exist "release\obs-plugins\64bit\%PluginName%.dll" (
	echo ERROR: Plugin DLL is not in release\obs-plugins\64bit. 1>&2
	exit /b 1
)

if not exist "release\data\obs-plugins\%PluginName%\locale\en-US.ini" (
	echo ERROR: Plugin data is not in release\data\obs-plugins\%PluginName%. 1>&2
	exit /b 1
)

if not exist "release\obs-plugins\64bit\onnxruntime.dll" (
	echo ERROR: ONNX Runtime is not in release\obs-plugins\64bit. 1>&2
	exit /b 1
)

if not exist "release\data\obs-plugins\%PluginName%\yunet_model\face_detection_yunet_2026may.onnx" (
	echo ERROR: The YuNet model is missing from the release package. 1>&2
	exit /b 1
)

if not exist "release\data\obs-plugins\%PluginName%\scrfd_model\scrfd_2.5g_bnkps.onnx" (
	echo ERROR: The SCRFD model is missing from the release package. 1>&2
	exit /b 1
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
