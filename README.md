# Optiforge-VR-Driver
## Installation
1. Download Steam, Steam VR and Visual Studio with C++ Desktop Development
2. Download this repo and go to the folder `steamvr`
3. Go inside the folder `optiforge/resources` and set the IP address of the raspberry pi
4. Copy the folder `optiforge` to `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers`
5. Go to `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\resources\settings\default.vrsettings` and change `requireHmd` to `false` and `forcedDriver` to `optiforge`
6. Launch Steam VR and enable VR mode on the raspberry pi

## Customization
If you want to use this driver for a different purpose, send the data in the expected format to the port you've set (`31000` by default) 