# Validation

Verified locally with Qt 6.12 and libmpv 2.5 on Linux.

- All three CTest suites pass: themes, playback backend, and native window interactions.
- Desktop-locale regression reproduces `mpv_create()` failure under `en_US.UTF-8` before the fix and verifies C numeric parsing after initialization. Failed opens preserve initialization errors without allocating a renderer.
- Native Wayland tests render a generated H.264 MP4 video and inspect its framebuffer for actual color data. They also verify seeking, embedded dialogs, compact window bounds, and live palette reloads.
- AddressSanitizer and UndefinedBehaviorSanitizer pass all three suites under Mesa software OpenGL, including leak detection. The backend exercises repeated load/seek/queue/destruction and string properties.
- An earlier unsuppressed native GPU sanitizer run passed functional checks but reported 3,608 bytes in 31 allocations at exit. Astra's audit attributed most to CUDA/NVIDIA and D-Bus; 590 bytes had unknown-module stacks. These remain unattributed. This is not a blanket claim of leak-free behavior on every driver. The original report is retained in `build/evidence/native-gpu-leaks.txt`.
- Astra reviewed callback lifetimes, string-property argument ownership, renderer cleanup, lazy initialization and teardown ordering. No outstanding high or critical ownership findings were reported. If the original OpenGL context genuinely cannot be made current, cleanup deliberately fails rather than freeing libmpv resources under an invalid context.
- All 24 available external palette fixtures imported successfully. Tests exercise source retargeting, source updates, saved JSON changes, invalid-update retention, Qt alpha-first hexadecimal colors, and fractional `Qt.rgba(...)` expressions.
- Desktop files pass `desktop-file-validate`. Installation was tested in an isolated prefix, including deduplication and preservation of unrelated launcher entries.
- Reference renders were inspected at 1080×720 and 680×440, plus the native compositor's tiled size. The file dialog keeps its action footer visible at the minimum size.

Measured after lazy renderer initialization was added: approximately 96 MiB RSS and 56 MiB proportional memory for an idle native Wayland window. Memory during playback depends on resolution, codecs and drivers; this is a startup sample, not a maximum bound.

The UI tests generate local media. Network-stream providers, audible speaker output, and every hardware codec/driver combination were not individually verified.

## Repeat the checks

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j4
QT_QPA_PLATFORM=wayland ctest --test-dir build --output-on-failure

cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMPVIDEO_SANITIZE=ON
cmake --build build-asan -j4
QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 __GLX_VENDOR_LIBRARY_NAME=mesa \
  MPVIDEO_TEST_SOFTWARE=1 ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 xvfb-run -a \
  ctest --test-dir build-asan --output-on-failure
```

For screenshots, set `MPVIDEO_TEST_SCREENSHOTS` to an output directory before running the window suite. MpVideo also accepts `--screenshot PATH --quit-after MILLISECONDS` for a bounded UI capture. Headless render tests require Xvfb, Mesa software OpenGL and FFmpeg.
