# Optiforge-VR-Driver
## Installation (Windows)
1. Download `Steam`, `Steam VR` and `Visual Studio` with `C++ Desktop Development`
2. Download this repo and go to the folder `steamvr`
3. Go inside the folder `optiforge/resources` and set the IP address of the raspberry pi
4. Copy the folder `optiforge` to `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers`
5. Go to `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\resources\settings\default.vrsettings` and change `requireHmd` to `false`, `forcedDriver` to `optiforge` and `activateMultipleDrivers` to `true`
6. Set up the lighthouse devices, see [Lighthouse tracking](#lighthouse-tracking)

## Running (Windows)
1. Turn on the base stations, controllers and the tag
2. Run `Steam VR`

## Installation (Linux)
1. Install `Steam` and `SteamVR` (via the Steam client).
2. Build the driver with CMake from the repo root:
   ```
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_QUIET=OFF --log-level=VERBOSE
   cmake --build build --verbose
   ```
   This fetches the OpenVR SDK automatically and produces `build/bin/linux64/driver_optiforge.so`.
3. Copy `build/bin/linux64/driver_optiforge.so` into `steamvr/optiforge/bin/linux64/`.
4. Go inside the folder `steamvr/optiforge/resources` and set the IP address of the raspberry pi.
5. Copy the folder `steamvr/optiforge` to `~/.steam/steam/steamapps/common/SteamVR/drivers` (or `~/.local/share/Steam/steamapps/common/SteamVR/drivers`, depending on your Steam install).
6. Go to `<SteamVR install>/resources/settings/default.vrsettings` and change `requireHmd` to `false` and `forcedDriver` to `optiforge`.

## Building the Windows driver on Linux
The Windows DLL can be cross-compiled with MinGW-w64:
1. Install MinGW-w64 (`sudo pacman -S mingw-w64-gcc` on Arch, `sudo apt install g++-mingw-w64-x86-64` on Debian/Ubuntu).
2. Build from the repo root:
   ```
   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_QUIET=OFF --log-level=VERBOSE
   cmake --build build-win
   ```
   This produces `build-win/bin/win64/driver_optiforge.dll`, with the MinGW runtime linked in statically.
3. Copy it into `steamvr/optiforge/bin/win64/` and continue with [Installation (Windows)](#installation-windows) from step 2.

## Running (Linux)
1. Run [`vr.sh`](https://github.com/FAV-SmartGlasses/Documentation) on the rpi
2. Run `Steam VR`

## Lighthouse tracking
The headset's position and rotation come from a lighthouse tag (a Vive/Tundra tracker, serial `LHR-...`) mounted on it. The base stations, Index controllers and the tag are handled by SteamVR's own `lighthouse` driver; this driver only reads the tag's pose.

### Hardware
- Index base stations only need power. Since there is no Valve headset to wake them up and put them to sleep, turn off base station power management in `SteamVR → Settings → Controllers/Devices`.
- Without a Vive/Index headset the controllers and the tag need a Watchman USB dongle to connect: one Vive Tracker dongle per device, or one Tundra SW3/SW5 dongle for up to 3/5 devices. A Steam Controller dongle won't work.
- Pair each controller and the tag via `SteamVR → Devices → Pair Controller`.

### SteamVR settings
In `C:\Program Files (x86)\Steam\config\steamvr.vrsettings` (user settings, not overwritten by SteamVR updates), under the `"steamvr"` section:
```json
"requireHmd": false,
"forcedDriver": "optiforge",
"activateMultipleDrivers": true
```
`activateMultipleDrivers` lets the lighthouse driver run next to this one. Without it the controllers and the tag won't show up.

### Driver settings
In `optiforge/resources/settings/default.vrsettings`:

| Key | Meaning |
|---|---|
| `trackerSerial` | Serial of the tag, e.g. `LHR-XXXXXXXX` (shown in `SteamVR → Manage Trackers`). If empty, the first tracker found is used. Set it once the controllers are connected too |
| `trackerOffsetX/Y/Z` | Offset in meters from the tag to the point between your eyes, along the tag's own axes |
| `trackerYaw/Pitch/Roll` | Rotation in degrees from the tag's axes to the headset's (x right, y up, -z forward). Yaw is around Y, pitch around X, roll around Z, applied in that order |

### Calibration
1. Start with all offsets at `0` and tune `trackerYaw/Pitch/Roll` until looking straight ahead gives a level, forward-facing view.
2. Tune `trackerOffsetX/Y/Z` until turning your head doesn't make the world swing around.
3. Run Room Setup (standing only is fine) with a controller.

If the tag loses tracking, the headset keeps its last pose and shows as out of range in SteamVR.

## Customization
The Raspberry Pi IMU connection is currently commented out in `driver.cpp`, as the rotation comes from the lighthouse tag. When it's enabled, send the data in the expected format to the port you've set (`31000` by default) 