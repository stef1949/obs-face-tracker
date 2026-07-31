# Face Tracker Plugin for OBS Studio

## Introduction

This plugin detects and tracks a person's face, then crops the selected source
to follow them.

This plugin employs [dlib](http://dlib.net/) for face detection and object tracking.
Frames from the selected source are periodically passed to the detector. Once
a face is found, the tracker follows it and crops the frame based on the face's
position and size.

See [CHANGELOG.md](CHANGELOG.md) for the complete set of changes in this fork.

## Usage

The plugin provides three ways to use face tracking.

### Face Tracker Source
Create a dedicated source that tracks a face in another source and zooms in.

1. Click the add button on the source list.
2. Add `Face Tracker`.
3. Set the `Source` property in the `Input` section at the top.

See [Properties](doc/properties.md) for the description of each property.

### Face Tracker Filter
Add face tracking directly to an existing video source.

1. Open filters for a source on OBS Studio.
2. Click the add button on `Effect Filters`.
3. Add `Face Tracker`.

See [Properties](doc/properties.md) for the description of each property.

### Face Tracker PTZ
An experimental version of PTZ control is provided as a video filter.

1. Open filters for a source in OBS Studio.
2. Click the add button on `Audio/Video Filters`.
3. Add `Face Tracker PTZ`.

See [Properties](doc/properties-ptz.md) for the description of each property.

See [Limitations](https://github.com/norihiro/obs-face-tracker/wiki/PTZ-Limitation)
for the current limitations of the PTZ control feature.

## Installation

### Windows requirements

- 64-bit OBS Studio 30, 31, or 32.
- A package whose `obs<major-version>` label matches your installed OBS major
  version. Check it in OBS under **Help > About OBS Studio**.
- A prebuilt package from this fork's
  [Releases page](https://github.com/stef1949/obs-face-tracker/releases).
- An NVIDIA driver that supports CUDA 12 if you want SCRFD GPU acceleration.
  The plugin still starts and SCRFD falls back to CPU if CUDA cannot initialize.

> [!IMPORTANT]
> If the Releases page has no package for your OBS version, a compatible
> prebuilt build has not been published yet. GitHub's automatic **Source code**
> ZIP and the `Windows-Symbols.zip` file are not installable plugin packages.

The standard Windows installer and package ZIP are the CUDA build. They include
ONNX Runtime CUDA 12, the required CUDA 12 and cuDNN 9 runtime DLLs, YuNet,
SCRFD-2.5G, SCRFD-10G, and all three detector model files. You do not need to
install the CUDA Toolkit, cuDNN, ONNX Runtime, or detector models separately.
Because those GPU libraries are prepackaged, the installer is substantially
larger than a CPU-only plugin. YuNet remains the default for new Face Tracker
sources; select SCRFD under **Detector and model** and leave **Use CUDA for
SCRFD** enabled for NVIDIA GPU acceleration. SCRFD-2.5G remains the recommended
real-time preset. SCRFD-10G is more accurate but performs about four times the
model computation and can substantially increase GPU usage, VRAM use, power
draw, and OBS rendering latency. The settings panel displays a warning whenever
10G is selected. SCRFD automatically falls back to its CPU provider if CUDA
cannot initialize.

The packaged SCRFD models are subject to InsightFace's non-commercial research
terms; review `LICENSE-scrfd-model` in the installed plugin data directory
before use.

### Windows installer (recommended)

1. Close OBS Studio.
2. Open the [Releases page](https://github.com/stef1949/obs-face-tracker/releases)
   and expand **Assets** for the release you want.
3. Download
   `obs-face-tracker-<version>-obs<major-version>-Windows-Installer.exe`.
   For example, use the `obs32` installer with OBS Studio 32.
4. Run the downloaded installer.
5. Confirm the OBS Studio application directory. For a standard installation,
   it is `C:\Program Files\obs-studio`. If you use a custom or portable
   installation, select the folder that contains `bin\64bit\obs64.exe`.
   Do not select the `bin` or `bin\64bit` folder itself.
6. Complete the installer. This installs the plugin, CUDA runtime libraries,
   cuDNN, ONNX Runtime, YuNet, SCRFD, and all three detector models into the OBS
   directory; it does not modify a system-wide CUDA installation.
7. Start OBS Studio.
8. Confirm the plugin appears by opening the **Sources** add menu and looking
   for **Face Tracker**, or by opening a video source's **Filters** window and
   looking for **Face Tracker** under **Effect Filters**.
9. Add or open a Face Tracker source/filter. Under **Detector and model**,
   confirm both **YuNet** and **SCRFD** are available. On a supported NVIDIA
   device, **SCRFD** and **Use CUDA for SCRFD** are selected automatically;
   choose another detected GPU here if required. Leave **SCRFD model** on
   **2.5G** for normal real-time use, or select **10G** for higher accuracy after
   reviewing the high-GPU-usage warning.

The installer is a single, self-contained package. It always carries the CPU
and CUDA detector files so it never needs to download a model or NVIDIA runtime
during setup. On a new Face Tracker source, a CUDA 12-capable Turing-or-newer
NVIDIA GPU automatically selects SCRFD with CUDA. Other systems default to
YuNet; SCRFD remains available with its CPU fallback. Existing source settings
are not changed during an upgrade.

### Windows ZIP (manual installation)

1. Close OBS Studio.
2. From the release's **Assets**, download
   `obs-face-tracker-<version>-obs<major-version>-Windows.zip`. Do not download
   `Windows-Symbols.zip`.
3. Open the ZIP. It contains `obs-plugins` and `data` folders.
4. Copy both folders into the OBS Studio application directory, normally
   `C:\Program Files\obs-studio`. Approve the folder merge and administrator
   prompt if Windows asks.
5. Verify these files exist under the same OBS Studio directory:

   ```text
   obs-plugins\64bit\obs-face-tracker.dll
   obs-plugins\64bit\onnxruntime_providers_cuda.dll
   obs-plugins\64bit\cudart64_12.dll
   obs-plugins\64bit\cudnn64_9.dll
   data\obs-plugins\obs-face-tracker\locale\en-US.ini
   data\obs-plugins\obs-face-tracker\yunet_model\face_detection_yunet_2026may.onnx
   data\obs-plugins\obs-face-tracker\scrfd_model\scrfd_2.5g_bnkps.onnx
   data\obs-plugins\obs-face-tracker\scrfd_model\scrfd_10g_bnkps.onnx
   ```

   For a standard installation, the DLL's full path is
   `C:\Program Files\obs-studio\obs-plugins\64bit\obs-face-tracker.dll`. It
   must not be placed beside `obs64.exe`.
6. Start OBS Studio and confirm **Face Tracker** appears in the Sources or
   Effect Filters add menu.

### Upgrading on Windows

1. Close OBS Studio.
2. Download the package for the new version and your OBS major version.
3. Run the new installer, or copy both folders from the new ZIP over the
   existing installation.
4. Start OBS Studio. Existing scenes and Face Tracker settings are retained.

### If Face Tracker does not appear

1. Confirm OBS is 64-bit and the package's `obs<major-version>` label matches
   **Help > About OBS Studio**.
2. Confirm you installed the package ZIP, not a source-code or symbols ZIP.
3. For a manual install, check both file paths shown above. The DLL without its
   `data` folder is not a complete installation.
4. In OBS, select **Help > Log Files > View Current Log** and search for
   `obs-face-tracker` to find the module-loading error.
5. If OBS was open during installation, close and reopen it before checking
   the menus again.
6. If SCRFD reports CPU fallback, update the NVIDIA display driver and inspect
   the OBS log for `SCRFD` and `CUDA`. A separate CUDA Toolkit installation is
   not required.

For macOS, see the
[macOS installation procedure](https://github.com/norihiro/obs-face-tracker/wiki/Install-MacOS).

## Wiki

- [Install procedure for macOS](https://github.com/norihiro/obs-face-tracker/wiki/Install-MacOS)
- [FAQ](https://github.com/norihiro/obs-face-tracker/wiki/FAQ)

## Building

This plugin requires [dlib](http://dlib.net/) to be built.
The `dlib` should be extracted under `obs-face-tracker` so that it will be linked statically.
I modified `dlib` so that `openblasp` won't be linked but `openblas`.

For macOS,
install openblas and configure the path.
```
brew install openblas
export OPENBLAS_HOME=/usr/local/opt/openblas/
```

For Linux and macOS,
expand `obs-face-tracker` outside `obs-studio` and build.
```
d0="$PWD"
git clone https://github.com/obsproject/obs-studio.git
mkdir obs-studio/build && cd obs-studio/build
cmake ..
make
cd "$d0"

git clone https://github.com/norihiro/obs-face-tracker.git
cd obs-face-tracker
git submodule update --init
mkdir build && cd build
cmake .. \
	-DLIBOBS_INCLUDE_DIR=$d0/obs-studio/libobs \
	-DLIBOBS_LIB=$d0/obs-studio/libobs \
	-DOBS_FRONTEND_LIB="$d0/obs-studio/build/UI/obs-frontend-api/libobs-frontend-api.dylib" \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo
make
```

For Windows, see `.github/workflows/main.yml`.

### Publishing a release

1. Update the project version and changelog, then commit the release.
2. Create a version tag on that commit and push it to GitHub.
3. The **Plugin Build** workflow builds and tests every OBS/platform package.
4. After all build jobs pass, the workflow creates or updates the matching
   GitHub Release and uploads the installers, package archives, symbols, and
   `SHA256SUMS.txt`.

### Optional CUDA acceleration

The dlib CNN detector can run on NVIDIA GPUs when the plugin is built with
CUDA and cuDNN. Configure with `-DENABLE_CUDA=ON` and set
`-DDLIB_USE_CUDA_COMPUTE_CAPABILITIES` for the target GPU (for example, `120`
for an RTX 50-series GPU). On Windows, `-DCUDNN_RUNTIME_DIR` can point to a
directory containing the cuDNN runtime DLLs so they are included in the plugin
package. CUDA acceleration applies to the CNN detector; HOG detection and the
correlation tracker remain CPU-based.

### YuNet ONNX detector

YuNet is a lightweight CPU detector through ONNX Runtime. It is prepackaged in
standard Windows releases. For a source build, download the pinned runtime SDK
and configure the plugin:

```powershell
$ort = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\ci\windows\download-onnxruntime.ps1 | Select-Object -Last 1).Trim()
cmake -S . -B build -DENABLE_YUNET=ON -DONNXRUNTIME_ROOT="$ort"
```

Run `ci/download-dlib-models.sh` before packaging so
`face_detection_yunet_2026may.onnx` and its MIT license are included. YuNet
uses one ONNX Runtime inference thread and is the default detector for newly
created sources in YuNet-enabled builds. Existing sources retain their selected
detector.

### SCRFD ONNX detector

SCRFD is the higher-accuracy ONNX detector. Standard Windows releases package
both the recommended real-time 2.5G model and the higher-accuracy 10G model,
use the CUDA build, and include the CUDA 12 provider with automatic CPU
fallback. For a source build on Windows, download the pinned ONNX Runtime and
NVIDIA runtime bundles, then enable both ONNX detectors:

```powershell
$ort = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\ci\windows\download-onnxruntime.ps1 -GpuCuda12 |
  Select-Object -Last 1).Trim()
$cuda = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\ci\windows\download-cuda12-runtime.ps1 |
  Select-Object -Last 1).Trim()
cmake -S . -B build `
  -DENABLE_YUNET=ON `
  -DENABLE_SCRFD=ON `
  -DENABLE_ONNXRUNTIME_CUDA=ON `
  -DONNXRUNTIME_ROOT="$ort" `
  -DCUDNN_RUNTIME_DIR="$cuda\bin" `
  -DCUDA_RUNTIME_DIRS="$cuda\bin" `
  -DCUDA_RUNTIME_NOTICE_DIR="$cuda\licenses"
```

The CUDA 12 configuration shares the CUDA major version used by OBS's NVIDIA
Video Effects module. The runtime downloader pins and packages CUDA 12 and
cuDNN 9; end users do not need the CUDA Toolkit. The ONNX Runtime downloader
also supports `-GpuCuda13` for custom builds on hosts that do not load CUDA 12
GPU modules, but standard release installers use CUDA 12. SCRFD prefers the
selected CUDA device and automatically falls back to its single-threaded CPU
provider if CUDA cannot initialise. Keep
`ENABLE_CUDA=OFF` unless the legacy dlib CNN detector also needs CUDA; this
avoids loading two independent cuDNN clients into OBS. `SCRFD input size`
controls the square input for dynamic-shape models. Fixed-shape models override
it; both included SCRFD models use 640x640. The 10G model has roughly four times
the computation of 2.5G. It should be treated as an optional quality preset,
particularly when OBS is also encoding or applying other GPU-heavy filters.

`CUDNN_RUNTIME_DIR`, `CUDA_RUNTIME_DIRS`, and `CUDA_RUNTIME_NOTICE_DIR` make the
Windows package self-contained. The plugin explicitly preloads cuDNN's split
runtime libraries from its own binary directory before creating the CUDA
session, which is required when OBS loads a plugin outside the process search
path.

The repository and standard Windows packages include the following models:

- `data/scrfd_model/scrfd_2.5g_bnkps.onnx`, SHA-256
  `bc24bb349491481c3ca793cf89306723162c280cb284c5a5e49df3760bf5c2ce`.
- `data/scrfd_model/scrfd_10g_bnkps.onnx`, SHA-256
  `5838f7fe053675b1c7a08b633df49e7af5495cee0493c7dcf6697200b85b5b91`.

InsightFace limits its pretrained models to non-commercial research use, so the
standard model updater does not download or replace SCRFD automatically.
Review `data/LICENSE-scrfd-model` and the current upstream terms before use or
redistribution. A compatible alternative model can be selected in the Face
Tracker properties.

## Preparing model data

The model updater downloads the pinned upstream dlib weights, verifies both the
compressed archives and installed files with SHA-256, and records their source
revision in `data/MODEL-MANIFEST.txt`.

Assuming the current directory is `obs-face-tracker`, run:

```shell
DESTDIR='./' ci/download-dlib-models.sh
```

This installs the HOG face detector, dlib CNN face detector, YuNet ONNX face
detector, and 5-point landmark model under `data`. Their pinned revisions and
checksums are maintained in `ci/dlib-models-manifest.txt`. SCRFD is kept
separate because its pretrained weights have additional use restrictions.

The optional 68-point landmark models can be downloaded with:

```shell
DESTDIR='./' ci/download-dlib-models.sh --nonfree
```

> [!NOTE]
> The training dataset used by the 68-point models excludes commercial use.
> Review the generated `LICENSE-shape_predictor_68_face_landmarks` before using
> either optional model.

### Installing the model files
Once you have prepared the model files under `data` directory,
run `cd build && make install` so that the data file will be installed.

## Known issues
This plugin is heavily under development. So far these issues are under investigation.
- Memory usage is gradually increasing when continuously detecting faces.
- It consumes a lot of CPU resource.
- The frame sometimes vibrates because the face detection results vibrates.

## License
This plugin is licensed under GPLv2.

## Sponsor
- [Jimcom USA](https://www.jimcom.us/?ref=2) - a company of Live Streaming and Content Recording Professionals.
  Development of PTZ camera control is supported by Jimcom.
  Jimcom is now providing a 20% discount for their broadcast-quality network-connected PTZ cameras and free shipping in the USA.
  Visit [Jimcom USA](https://www.jimcom.us/?ref=2) and enter the coupon code **FACETRACK20** when you order.

## Acknowledgments
- [dlib](http://dlib.net/) - [git hub repository](https://github.com/davisking/dlib)
- [obz-ptz](https://github.com/glikely/obs-ptz) - PTZ camera control goes through this plugin.
- [OBS Project](https://obsproject.com/)
