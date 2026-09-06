# Optiforge-VR-Driver
## Installation (Windows)
1. Download `Steam`, `Steam VR` and `Visual Studio` with `C++ Desktop Development`
2. Download this repo and go to the folder `steamvr`
3. Go inside the folder `optiforge/resources` and set the IP address of the raspberry pi
4. Copy the folder `optiforge` to `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers`
5. Go to `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\resources\settings\default.vrsettings` and change `requireHmd` to `false` and `forcedDriver` to `optiforge`

## Running (Windows)
1. Run [`vr.sh`](https://github.com/FAV-SmartGlasses/Documentation) on the rpi
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

## Running (Linux)
1. Run [`vr.sh`](https://github.com/FAV-SmartGlasses/Documentation) on the rpi
2. Run `Steam VR`

## Customization
If you want to use this driver for a different purpose, send the data in the expected format to the port you've set (`31000` by default) 