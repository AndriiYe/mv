# AGENTS.md

## Project overview

This is a C++ OpenCV project developed in VS Code on Windows.

The main goal is to build and test OpenCV-based computer vision code with a clean, reproducible CMake setup.

Typical use cases:
- OpenCV image/video processing
- Camera or video stream capture
- Feature detection / optical flow experiments
- Real-time visualization and debugging
- Later possible migration to embedded/companion-computer vision pipeline

## Development environment

Primary environment:
- OS: Windows
- Editor: VS Code
- Build system: CMake
- Compiler: MSVC / Visual Studio Build Tools
- Generator: Prefer Ninja or Visual Studio 17 2022
- OpenCV installed manually, commonly around:

```text
C:/opencv/build
```

## Build requirements

Required local tools:
- CMake
- MSVC / Visual Studio Build Tools
- Ninja, or the Visual Studio 17 2022 generator
- OpenCV installed at `C:/opencv/build` or another path configured through `OpenCV_DIR`

Current CMake setup expects:

```cmake
set(OpenCV_DIR "C:/opencv/build/x64/vc16/lib")
```

Known-good build command from VS Code CMake Tools:

```powershell
& "C:\Program Files\CMake\bin\cmake.EXE" --build c:/VScode_projects/cv/build --config Debug --target all --
```

The VS Code Build button has been verified to compile and link successfully with this command.

When building from a plain shell, initialize the Visual Studio developer environment first:

```powershell
& cmd.exe /d /s /c '"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files\CMake\bin\cmake.EXE" --build c:/VScode_projects/cv/build --config Debug --target all --'
```

## Raspberry Pi 5 notes

The tracker is intended to remain camera-source agnostic. `LocalTracker` should
not capture frames, convert color, draw, or call `imshow`; it should only process
an already-prepared grayscale frame buffer.

For Raspberry Pi 5 with Camera Module 3, use the modern `libcamera` stack by
setting `capture.mode` to `pi-camera` in `config.json`:

```json
{
  "capture": {
    "mode": "pi-camera",
    "width": 640,
    "height": 480,
    "fps": 30
  }
}
```

See `docs/raspberry_pi5_camera_module3.md` for setup and troubleshooting.

## CRSF serial design notes

Do not use two threads for the same serial port. Testing showed that separate
read and write threads on one CRSF COM port significantly harm transmit timing:
packet rate drops and the send period becomes random.

`CrsfRcSender` should keep one serial I/O loop per port. The loop should read or
drain available bytes first, parse any received CRSF packets, and then write the
next RC packet on the fixed send cadence. This preserves stable output timing
while still allowing RX parsing.

Keep CRSF reads bounded before each write. A busy RX side must not stretch the
TX period; read a small buffer, parse what is available, write the packet, and
continue on the next cadence tick.
