# Optiforge-VR-Driver
## Installation
1. Download Steam and Steam VR
2. Download this repo and go to the folder `steamvr`
3. Copy the folder `optiforge` to `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers`
4. Go to `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\resources\settings\default.vrsettings` and change `requireHmd` to `false` and `forcedDriver` to `optiforge`
5. Launch Steam VR and enable VR mode on the raspberry pi

## Customization
If you want to use this driver for a different purpose, sent the data in the expected format to port `6969` (I did not come up with that number)