# Optiforge HMD for Monado (Linux)

Monado patch that adds the Optiforge headset (Waveshare 1080x1920 panel) to Monado's lighthouse builder.
Its head pose comes from a Vive tracker mounted on the headset, tracked by libsurvive. No real HMD needed.

## Build and install

Needs `libsurvive-git` installed first (see the libpcap note below).

```
cd monado
makepkg -si
```

This builds `monado-git` pinned to commit `e936b6a53` with `0001-optiforge-hmd.patch` applied, and replaces the
installed `monado-git`. Don't reinstall monado-git with `yay` afterwards, or it is replaced by the unpatched one.

libsurvive note: `libsurvive-git` from the AUR fails to build against current libpcap. In its PKGBUILD `prepare()`, add
`sed -i 's/find_library(PCAP_LIBRARY pcap)/set(PCAP_LIBRARY "")/' src/CMakeLists.txt` and build it with `makepkg -si`.

## One-time libsurvive calibration

With the tracker visible to the base station(s) and lying still:

```
survive-cli --force-calibrate
```

This saves the base station positions to `~/.config/libsurvive/config.json`. Redo it if a base station moves.

## Running (Envision)

Leave "Use development profiles" off. In Preferences, set the environment variables:

| Variable | Value | Meaning |
|---|---|---|
| `LH_DRIVER` | `survive` | Use libsurvive for lighthouse tracking |
| `OPTIFORGE_HMD` | `1` | Create the Optiforge HMD |
| `OPTIFORGE_TRACKER_SERIAL` | `LHR-8BD7C17A` | Tracker on the headset (empty = first tracker found) |
| `OPTIFORGE_TRACKER_OFFSET_X/Y/Z` | metres, default 0 | Head (centre between the eyes) position in the tracker's axes |
| `OPTIFORGE_TRACKER_YAW/PITCH/ROLL` | degrees, default 0 | Head rotation relative to the tracker (Y, X, Z, same as the SteamVR driver) |
| `OPTIFORGE_PANEL_ROTATION` | `none` (default), `left`, `right` | `none`: eyes are the left/right halves of the portrait panel. `left`/`right`: eyes are the top/bottom halves, image rotated 90° |
| `OPTIFORGE_SWAP_EYES` | `1` | Swap which panel half is the left eye |
| `OPTIFORGE_FOV` | degrees, default 90 | Horizontal FOV per eye; vertical follows the aspect ratio |
| `OPTIFORGE_REFRESH_HZ` | default 60 | Panel refresh rate |

Remove `QWERTY_ENABLE`: the QWERTY builder is picked before the lighthouse builder when it is set.

The tracker must be on and connected when Monado starts. The tracker offset and rotation can also be tuned live in the
Monado debug GUI ("Optiforge HMD").

Monado log lines to look for: `Using builder lighthouse`, `Using tracker 'LHR-8BD7C17A'`, and `head: Optiforge HMD`.

## Not done yet

- Lens distortion: none, the image is shown undistorted.
- FOV is a placeholder until measured on the headset.
