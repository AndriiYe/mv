# OpenCV CRSF Tracker

This project runs an OpenCV tracking loop and can optionally open one CRSF serial
port for RC input/output. The CRSF serial logic is asynchronous, so serial reads
and writes do not block the main computer-vision loop.

When CRSF is active, the application listens on TCP `127.0.0.1:4005`. Each
generated CRSF packet is mirrored as raw bytes to the connected TCP client. The
listener is nonblocking and does not stop serial output if no client is present.
Receivers such as `UCRSFParserComponent` should connect as TCP clients. The TCP
stream has no additional envelope or length prefix: it contains consecutive raw
26-byte `RC_CHANNELS_PACKED` frames.

## Build

Windows MSVC build from a normal PowerShell needs the Visual Studio developer
environment:

```powershell
& cmd.exe /d /s /c '"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files\CMake\bin\cmake.EXE" -S c:/VScode_projects/cv -B c:/VScode_projects/cv/build -G Ninja -DCMAKE_BUILD_TYPE=Debug && "C:\Program Files\CMake\bin\cmake.EXE" --build c:/VScode_projects/cv/build --config Debug --target all --'
```

## Deploy To Raspberry Pi

The expected Raspberry Pi checkout is:

```bash
git clone git@github.com:AndriiYe/mv.git ~/mv
```

From the Windows PC, deploy and build on the Pi:

```powershell
.\scripts\deploy-pi.ps1
```

Deploy, build, and run:

```powershell
.\scripts\deploy-pi.ps1 -Run
```

The script pushes the current clean local branch to GitHub, SSHs to
`pi@192.168.0.210`, runs `git pull --ff-only` in `/home/pi/mv`, configures with
Ninja, and builds on the Pi. The same commands are available in VS Code as:

- `Deploy: Raspberry Pi build`
- `Deploy: Raspberry Pi build and run`

## Configure CRSF

CRSF is configured through `config.json`. Windows serial ports look like this:

```json
"crsf": {
  "device": "COM47",
  "baudrate": 420000
}
```

Raspberry Pi 5 serial ports use Linux device paths:

```json
"crsf": {
  "device": "/dev/ttyAMA0",
  "baudrate": 420000
}
```

Leave `device` empty to disable CRSF:

```json
"crsf": {
  "device": "",
  "baudrate": 420000
}
```

## Run From JSON Config

`cv.exe` automatically loads the first `config.json` it finds in the current
working directory, next to the executable, or one directory above the executable.
The JSON file can keep all launch options in one place:

```json
{
  "capture": {
    "mode": "camera",
    "source": "C:/VScode_projects/cv/src/w22.mp4",
    "camera_index": 0,
    "width": 640,
    "height": 480,
    "fps": 30,
    "screen_left": 100,
    "screen_top": 100,
    "screen_width": 800,
    "screen_height": 600
  },
  "crsf": {
    "device": "COM47",
    "baudrate": 420000
  }
}
```

Set `capture.mode` to one of:

- `camera`
- `video`
- `pi-camera`
- `screen`
- `screen-virtual`
- `screen-region`

When `capture.mode` is `screen-region`, the capture position can be adjusted
while the app is running with `W`/`A`/`S`/`D` or the arrow keys. The region moves
10 pixels per key press and keeps the configured width and height.

With that file present, run the app without the long command line:

```powershell
.\build\cv.exe
```

Runtime command-line options are intentionally not read. Edit `config.json` to
change capture source, CRSF port, baud rate, size, or FPS. If no `config.json`
is found, the app exits with an error.

## Public CRSF API

`CrsfRcSender` keeps the public channel API intentionally small:

```cpp
CrsfRcSender crsf("COM3");
crsf.start();

crsf.setChennel(1, 1000);          // transmit channel 1 at 1000 us
uint16_t ch1 = crsf.getChannel(1); // latest received channel 1, or 0 before RX

crsf.stop();
```

Channel numbers are 1-based: `1` through `16`.

The method name is currently `setChennel` to match the requested API spelling.

## Async Design

```mermaid
flowchart LR
    Main["main.cpp / OpenCV loop"] -->|"setChennel(ch, us)"| TxBank["TX channel bank\natomic fields"]
    TxBank -->|"snapshot every 20 ms"| IoThread["CRSF I/O thread"]
    IoThread -->|"RC_CHANNELS_PACKED"| Serial["One serial port\n420000 baud default"]
    IoThread -->|"same raw CRSF packet"| Tcp["TCP listener\n127.0.0.1:4005"]

    Serial -->|"raw CRSF bytes"| IoThread
    IoThread -->|"CRC valid packet"| RxBank["RX channel bank\natomic fields"]
    RxBank -->|"getChannel(ch)"| Main

    Stop["stop()"] -->|"running=false"| IoThread
    IoThread -->|"joined"| Closed["close serial"]
```

Important behavior:

- `setChennel()` only writes one atomic channel value. It does not touch the
  serial port.
- `getChannel()` only reads one atomic channel value. It does not wait for a
  serial packet.
- One I/O thread reads available bytes first, validates CRSF CRC, stores the
  latest received channel bank, then sends the current TX channel bank every
  `20 ms`.
- `stop()` clears one shared `running_` atomic flag, joins the I/O thread, and
  only then closes the serial handle.

## Current Demo Behavior

When `crsf.device` is not empty, `main.cpp` starts the CRSF runtime and toggles
channel 1 every two seconds:

- `1000 us`
- `2000 us`

All other transmitted channels stay neutral at `1500 us`.
