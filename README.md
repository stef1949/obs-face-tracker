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

> [!IMPORTANT]
> If the Releases page has no package for your OBS version, a compatible
> prebuilt build has not been published yet. GitHub's automatic **Source code**
> ZIP and the `Windows-Symbols.zip` file are not installable plugin packages.

The standard Windows installer and package ZIP include ONNX Runtime, YuNet,
SCRFD-2.5G, and their model files. No separate detector download is required.
YuNet is the default for new Face Tracker sources; SCRFD can be selected under
**Detector and model**. The packaged SCRFD model is subject to InsightFace's
non-commercial research terms; review `LICENSE-scrfd-model` in the installed
plugin data directory before use.

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
6. Complete the installer, then start OBS Studio.
7. Confirm the plugin appears by opening the **Sources** add menu and looking
   for **Face Tracker**, or by opening a video source's **Filters** window and
   looking for **Face Tracker** under **Effect Filters**.

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
   data\obs-plugins\obs-face-tracker\locale\en-US.ini
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

SCRFD-2.5G is a higher-accuracy ONNX detector and is prepackaged with the CPU
provider in standard Windows releases. For an optional CUDA-enabled source
build on Windows with an NVIDIA GPU, use the pinned ONNX Runtime CUDA 12
package and enable both ONNX detectors:

```powershell
$ort = (& powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\ci\windows\download-onnxruntime.ps1 -GpuCuda12 |
  Select-Object -Last 1).Trim()
cmake -S . -B build `
  -DENABLE_YUNET=ON `
  -DENABLE_SCRFD=ON `
  -DENABLE_ONNXRUNTIME_CUDA=ON `
  -DONNXRUNTIME_ROOT="$ort"
```

The CUDA 12 configuration shares the CUDA major version used by OBS's NVIDIA
Video Effects module. It requires CUDA 12 runtime libraries and cuDNN 9. The
downloader also supports `-GpuCuda13` for hosts that do not load CUDA 12 GPU
modules. SCRFD prefers the selected CUDA device and automatically falls back to
its single-threaded CPU provider if CUDA cannot initialise. Keep
`ENABLE_CUDA=OFF` unless the legacy dlib CNN detector also needs CUDA; this
avoids loading two independent cuDNN clients into OBS. `SCRFD input size`
controls the square input for dynamic-shape models. Fixed-shape models override
it; the included SCRFD-2.5G model uses 640x640.

For a self-contained Windows package, set `CUDNN_RUNTIME_DIR` to the cuDNN
`bin` directory and set `CUDA_RUNTIME_DIRS` to a semicolon-separated list of
the CUDA runtime, cuBLAS, and cuFFT `bin` directories. The plugin explicitly
preloads cuDNN's split runtime libraries from its own binary directory before
creating the CUDA session, which is required when OBS loads a plugin outside
the process search path.

The repository and standard Windows packages include
`data/scrfd_model/scrfd_2.5g_bnkps.onnx` with SHA-256
`bc24bb349491481c3ca793cf89306723162c280cb284c5a5e49df3760bf5c2ce`.
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
