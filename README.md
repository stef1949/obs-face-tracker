# Face Tracker Plugin for OBS Studio

## Introduction

This plugin provide a feature to track face of a person by detecting and tracking a face.

This plugin employs [dlib](http://dlib.net/) on face detection and object tracking.
The frame of the source is periodically taken to face detection algorithm.
Once a face is found, the face is tracked.
Based on the location and the size of the face under tracking, the frame will be cropped.

See [CHANGELOG.md](CHANGELOG.md) for the complete set of changes in this fork.

## Usage

For several use cases, total 3 methods are provided.

### Face Tracker Source
The face tracker is implemented as a source. You can easily have another source that tracks and zooms into a face.
1. Click the add button on the source list.
2. Add `Face Tracker`.
3. Set the `Source` property in the `Input` section at the top.

See [Properties](doc/properties.md) for the description of each property.

### Face Tracker Filter
The face tracker is implemented as an effect filter so that any video source can have the face tracker.
1. Open filters for a source on OBS Studio.
2. Click the add button on `Effect Filters`.
3. Add `Face Tracker`.

See [Properties](doc/properties.md) for the description of each property.

### Face Tracker PTZ
Experimental version of PTZ control is provided as an video filter.
1. Open filters for a source on OBS Studio,
2. Click the add button on `Audio/Video Filters`.
3. Add `Face Tracker PTZ`.

See [Properties](doc/properties-ptz.md) for the description of each property.

See [Limitations](https://github.com/norihiro/obs-face-tracker/wiki/PTZ-Limitation)
for current limitations of PTZ control feature.

## Installing on Windows

Use the Windows installer when possible. If you install a ZIP manually, extract
it into the OBS Studio application directory, normally
`C:\Program Files\obs-studio`. The resulting plugin DLL must be at
`C:\Program Files\obs-studio\obs-plugins\64bit\obs-face-tracker.dll` (or the
equivalent path for your OBS installation), not beside `obs64.exe`.

Current release builds are tested against OBS Studio 30, 31, and 32.

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

### Optional CUDA acceleration

The dlib CNN detector can run on NVIDIA GPUs when the plugin is built with
CUDA and cuDNN. Configure with `-DENABLE_CUDA=ON` and set
`-DDLIB_USE_CUDA_COMPUTE_CAPABILITIES` for the target GPU (for example, `120`
for an RTX 50-series GPU). On Windows, `-DCUDNN_RUNTIME_DIR` can point to a
directory containing the cuDNN runtime DLLs so they are included in the plugin
package. CUDA acceleration applies to the CNN detector; HOG detection and the
correlation tracker remain CPU-based.

### YuNet ONNX detector

YuNet is available as an optional lightweight CPU detector through ONNX
Runtime. On Windows, download the pinned runtime SDK and configure the plugin:

```powershell
$ort = & .\ci\windows\download-onnxruntime.ps1
cmake -S . -B build -DENABLE_YUNET=ON -DONNXRUNTIME_ROOT="$ort"
```

Run `ci/download-dlib-models.sh` before packaging so
`face_detection_yunet_2026may.onnx` and its MIT license are included. YuNet
uses one ONNX Runtime inference thread and is the default detector for newly
created sources in YuNet-enabled builds. Existing sources retain their selected
detector.

### SCRFD ONNX detector with CUDA

SCRFD-2.5G is available as a higher-accuracy ONNX detector. On Windows with an
NVIDIA GPU, use the pinned ONNX Runtime CUDA 12 package and enable both ONNX
detectors:

```powershell
$ort = & .\ci\windows\download-onnxruntime.ps1 -GpuCuda12
cmake -S . -B build `
  -DENABLE_YUNET=ON `
  -DENABLE_SCRFD=ON `
  -DENABLE_ONNXRUNTIME_CUDA=ON `
  -DONNXRUNTIME_ROOT="$ort"
```

The OBS build uses the CUDA 12 ONNX Runtime package so it shares the CUDA major
version used by OBS's NVIDIA Video Effects module. It requires CUDA 12 runtime
libraries and cuDNN 9. The downloader also supports `-GpuCuda13` for hosts that
do not load CUDA 12 GPU modules. SCRFD prefers
the selected CUDA device and automatically falls back to its single-threaded
CPU provider if CUDA cannot initialise. Keep `ENABLE_CUDA=OFF` unless the
legacy dlib CNN detector also needs CUDA; this avoids loading two independent
cuDNN clients into OBS. `SCRFD input size` controls the square input for
dynamic-shape models. Fixed-shape models override it; the local SCRFD-2.5G
model documented below uses 640x640.

For a self-contained Windows package, set `CUDNN_RUNTIME_DIR` to the cuDNN
`bin` directory and set `CUDA_RUNTIME_DIRS` to a semicolon-separated list of
the CUDA runtime, cuBLAS, and cuFFT `bin` directories. The plugin explicitly
preloads cuDNN's split runtime libraries from its own binary directory before
creating the CUDA session, which is required when OBS loads a plugin outside
the process search path.

Place a compatible `scrfd_2.5g_bnkps.onnx` file in `data/scrfd_model`. The
expected local development model has SHA-256
`bc24bb349491481c3ca793cf89306723162c280cb284c5a5e49df3760bf5c2ce`.
InsightFace limits its pretrained models to non-commercial research use, so the
standard model updater does not download SCRFD automatically. Review
`data/LICENSE-scrfd-model` and the current upstream terms before use or
redistribution.

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
