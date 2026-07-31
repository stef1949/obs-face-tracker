# Changelog

This file records the changes made in the `stef1949/obs-face-tracker` fork.
The initial fork baseline is upstream commit
[`5a2b349`](https://github.com/norihiro/obs-face-tracker/commit/5a2b34900877898783e3a6d2f0950ff12e99b8c8)
from 1 November 2025, after the upstream 0.9.1 release.

## Unreleased - fork changes since upstream `5a2b349`

### Highlights

- Added two ONNX Runtime detector backends: the lightweight YuNet CPU detector
  and the higher-accuracy SCRFD-2.5G detector with NVIDIA CUDA acceleration.
- Reduced CPU work and GPU readback by scheduling detection and correlation
  tracking independently, sharing one captured frame between consumers, and
  skipping frame conversion when no worker needs an update.
- Added stable subject selection and stationary-subject hysteresis to prevent
  target switching and back-and-forth camera jitter.
- Updated the plugin for OBS Studio 30, 31, and 32, including Qt 6 defaults,
  color-space forwarding, safer display handling, and current packaging paths.
- Hardened worker-thread, texture, dock, PTZ, installer, and model-download
  lifecycles, and added automated tests and sanitizer coverage.
- Reorganized the settings dialog into a setup-oriented order while preserving
  every existing setting key and saved scene value.

### Face detection and AI models

- Added a YuNet detector implemented directly on ONNX Runtime.
  - Uses the pinned OpenCV Zoo `face_detection_yunet_2026may.onnx` model.
  - Runs with sequential execution, one intra-op thread, one inter-op thread,
    and full ONNX graph optimization to avoid oversubscribing OBS's CPU.
  - Adds configurable confidence threshold, non-maximum-suppression threshold,
    and maximum inference size.
  - Resizes and pads detector input, decodes multi-stride outputs, rejects
    invalid tensors and non-finite values, caps candidate counts, and performs
    IoU-based non-maximum suppression.
  - Retries model/session creation after a delay instead of continuously
    reloading a missing or invalid model.
- Added an SCRFD detector implemented directly on ONNX Runtime.
  - Supports fixed- and dynamic-shape SCRFD models with 3-level or 5-level
    output layouts, with and without landmark outputs.
  - Adds configurable confidence threshold, overlap threshold, input size,
    CUDA enablement, and CUDA device selection.
  - Uses aspect-preserving bilinear resize, normalized NCHW input, padding,
    validated output shapes, bounded candidate processing, and IoU NMS.
  - Uses ONNX Runtime's CUDA execution provider with heuristic cuDNN algorithm
    selection and the requested GPU device.
  - Falls back to a single-threaded CPU session if the CUDA provider cannot be
    initialized, and logs the active ONNX Runtime version and provider.
  - Retries failed model/session creation after a delay to prevent log spam and
    repeated expensive initialization.
- Added explicit loading of cuDNN 9's split runtime libraries from the plugin
  binary directory on Windows. This fixes CUDA session startup when OBS loads
  the plugin from a directory that is not on the process DLL search path.
- Added CUDA device enumeration and selection for the legacy dlib CNN detector.
- Made dlib HOG and CNN model loading null-safe and retryable, with clearer
  errors and delayed retries after invalid or unavailable model files.
- Added and pinned the runtime model data used by normal builds:
  - dlib HOG face detector;
  - dlib CNN face detector;
  - dlib 5-point landmark predictor;
  - YuNet 2026 May ONNX model;
  - SCRFD-2.5G ONNX model.
- Added model and third-party license files plus a manifest containing source
  revisions and SHA-256 checksums.
- Documented that the included InsightFace SCRFD weights are subject to
  non-commercial research terms, independently of the GPL-licensed plugin
  source code.

### Subject selection and tracking behavior

- Added four multi-face target-selection policies:
  - keep the current subject;
  - select the largest face;
  - select the face closest to frame center;
  - use the first detected face.
- The default sticky policy chooses the face nearest the previous target and
  falls back to the largest face when no prior target exists.
- Added a configurable lost-subject hold time so brief occlusions do not
  immediately switch tracking to another person.
- Added separate detector intervals for active tracking and searching, allowing
  fast reacquisition without continuously running the detector at high rate.
- Added a configurable correlation-tracker update interval.
- Resetting tracking now clears selected-target state and all detection and
  tracking deadlines immediately.
- Invalid or zero-area rectangles are rejected safely during duplicate removal
  and tracker attenuation.
- Detector changes now clear stale results, reset scheduling deadlines, and
  retire incompatible tracker state instead of continuing with mixed backend
  data.

### Stationary-subject stability

- Replaced the original threshold-only controller deadband with a symmetric
  soft deadband that includes hysteresis.
- Once an axis settles inside the deadband, it remains still until movement
  clears the nonlinear release band. This prevents detector noise near the
  boundary from repeatedly reversing camera direction.
- Clears accumulated integral motion while settled.
- Clears residual low-pass motion without moving the existing crop, preventing
  the final filtered value from causing a visible jump.
- Tracks settled state independently for X, Y, and zoom, and resets that state
  whenever tracking is reset.
- Added practical default deadbands and nonlinear bands to both crop-based and
  PTZ tracking.

### CPU, GPU, and memory efficiency

- Replaced frame-count scheduling with monotonic nanosecond deadlines for
  detector and tracker work.
- The detector uses the short searching interval only while no face is tracked
  and the longer correction interval during active tracking.
- Harvests completed worker results before deciding whether a new frame is
  needed.
- Skips GPU-to-CPU frame readback when neither the detector nor a correlation
  tracker is ready for new input.
- Captures a source frame once and shares the immutable image between all
  active detector/tracker consumers.
- Caches the converted dlib RGB image in the shared texture object so multiple
  workers do not repeat the same pixel conversion.
- Downscales packed RGB frames during the copy step, reducing memory bandwidth
  and temporary full-resolution allocations.
- Limits the idle correlation-tracker pool to two instances instead of allowing
  unused workers to accumulate indefinitely.
- The PTZ video filter now checks whether a frame is due before allocating and
  converting a texture.
- YuNet and SCRFD CPU sessions are explicitly limited to one inference thread.

### Thread safety and lifecycle hardening

- Replaced cross-thread `volatile` flags with C++ atomics in detector, tracker,
  and VISCA worker classes.
- Added locking around shared manager state, PTZ frame caches, and lazy RGB
  image construction.
- Detector and tracker destructors now stop and join their worker threads before
  releasing owned state.
- Handles thread-creation failures explicitly instead of marking failed workers
  as running.
- Uses local shared ownership while a worker processes a texture, preventing a
  producer from invalidating the frame mid-inference.
- Made detector texture interfaces const-correct for shared immutable frames.
- Hardened VISCA configuration updates against use-after-release, empty
  addresses, failed thread creation, and data races in pan, tilt, zoom, preset,
  and reconnect state.
- Fixed dock lifecycle handling by preventing duplicate initialization,
  removing frontend callbacks on shutdown, deleting failed dock registrations,
  and releasing the Tools-menu action.
- Added failure handling for OBS display creation and refreshes dock display
  color-space state.

### OBS Studio compatibility and rendering

- Added tested build coverage for OBS Studio 30, 31, and 32 on Linux, macOS,
  and Windows.
- Changed the default OBS Qt dependency from Qt 5 to Qt 6.
- Added source color-space forwarding so the Face Tracker filter/source follows
  its target's negotiated color space on current OBS releases.
- Hardened texture staging, mapped-surface handling, video-frame conversion,
  dimensions, and invalid-frame checks for newer OBS graphics behavior.
- Updated PTZ frame scaling so output dimensions never reach zero and format
  conversions are recreated only when their input/output descriptions change.
- Added plugin author, display name, and description metadata.
- Improved source enumeration by excluding scenes, groups, the current source,
  and blank names where appropriate.
- Replaced free-text monitor source/filter fields with populated selectors; the
  available filter list updates when the selected source changes.

### Settings and usability

- Reordered the Face Tracker source properties so `Input` appears first instead
  of being appended at the bottom.
- Split the previous long face-detection block into logical sections:
  `Detector and model`, `Subject tracking`, `Detection area`,
  `Timing and performance`, and `Landmarks`.
- Placed the default SCRFD/CUDA controls directly after detector and GPU
  selection, followed by YuNet and legacy dlib model options.
- The complete setup flow is now: input, detector/model, subject selection,
  detection area, timing/performance, landmarks, framing, tracking response,
  output, automation, presets, and debugging.
- Moved presets below operational settings and kept diagnostics last.
- Applied the same detector/tracking organization to Face Tracker PTZ.
- Added localized labels for detector choices, SCRFD, YuNet, GPU selection,
  target policies, timing controls, monitor selectors, and the new groups in
  US and British English.
- Existing property names, defaults stored in existing scenes, and preset data
  remain compatible.

### Build, packaging, and installation

- Added CMake options for dlib CUDA, YuNet, SCRFD, ONNX Runtime CUDA, Address /
  UndefinedBehavior sanitizers, and ThreadSanitizer.
- Added ONNX Runtime SDK discovery, compile definitions, linking, runtime DLL
  installation, and third-party notice installation.
- Added Windows packaging for ONNX Runtime, CUDA runtime, cuBLAS, cuFFT, and
  cuDNN DLLs, including validation that required runtime files exist.
- Added a checksum-pinned PowerShell downloader for ONNX Runtime 1.26.0 CPU,
  CUDA 12, and CUDA 13 packages.
- Added CUDA 13 handling for dlib's obsolete `sm_50` configuration probe while
  retaining the actual target-architecture build as the compatibility check.
- Updated model preparation to use pinned revisions and SHA-256 validation for
  both downloaded archives and extracted files.
- Added retrying downloads, automatic temporary-directory cleanup, and an
  explicit `--nonfree` option for the excluded 68-point landmark models.
- Updated CI and local packaging to use the shared model downloader.
- Corrected the dlib HOG default model directory.
- Windows ZIP packages now preserve the standard OBS directory layout and
  exclude PDB files; symbols are distributed separately.
- The Windows installer now:
  - excludes PDB files;
  - checks both 64-bit and 32-bit OBS registry locations;
  - normalizes accidental selection of `bin\64bit` back to the OBS root;
  - rejects directories that do not contain `bin\64bit\obs64.exe`.
- Added optional Authenticode signing for the Windows plugin DLL and installer
  through CI secrets.
- Added SHA-256 output for Windows archives and installers.

### Tests and continuous integration

- Added unit coverage for target-selection policies, rectangle area and IoU,
  invalid rectangles, GPU-device fallback, interval scheduling, and deadband
  hysteresis.
- Added a YuNet model smoke test that loads the pinned model, runs inference,
  and validates all expected float output tensors.
- Added an SCRFD model smoke test that validates model input/output layouts and
  can run through either the CPU or CUDA execution provider.
- Enabled CTest in Linux, macOS, and Windows CI jobs.
- Added a Linux AddressSanitizer and UndefinedBehaviorSanitizer job.
- Added optional ThreadSanitizer build support for local and future CI use.
- Added grouped weekly Dependabot updates for GitHub Actions.

### Documentation

- Expanded the README with Windows installation paths, OBS 30-32 support,
  dlib CUDA guidance, YuNet and SCRFD build instructions, ONNX Runtime CUDA
  compatibility, self-contained runtime packaging, model preparation, hashes,
  and licensing notes.
- Expanded Face Tracker and PTZ property references with detector selection,
  CUDA settings, target policies, scheduling controls, CPU tradeoffs, and jitter
  suppression behavior.

### Validation performed

- Built the plugin successfully in separate Windows CPU and CUDA configurations.
- Passed all three CTest targets in both configurations.
- Validated shell, PowerShell, GitHub Actions, and whitespace checks during
  development.
- Installed the CUDA build into OBS Studio 32.1.0 and verified live face
  tracking, settings layout, cuDNN runtime preload, SCRFD model loading, and the
  ONNX Runtime 1.26 CUDA provider on GPU 0.
 