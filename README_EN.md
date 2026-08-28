<p align="center">
  <img src="ico.png" alt="YeImageViewer" width="100">
</p>

<h1 align="center">YeImageViewer</h1>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue" alt="License"></a>
  <img src="https://img.shields.io/badge/OS-Windows%2010%2F11%2064--bit-00adef" alt="Platform">
</p>

<p align="center"><a href="README.md">中文</a> | English</p>

**YeImageViewer** is a minimal, fast native Windows image viewer. It supports common still images, animations, RAW files, iOS Live Photos, and Android Motion Photos, together with EXIF display, printing, simple editing, and file associations.

Current version: **v1.36.27** · [Changelog](CHANGELOG.md)

This project is based on [JarkViewer](https://github.com/jark006/JarkViewer) and is licensed under GNU GPL v3. Thanks to upstream author JARK006 and all contributors.

![Preview](preview.png)

## Controls

1. Switch images: use the bottom toolbar, or press `Left` / `Right`
2. Zoom: use `Ctrl + mouse wheel`, or press `Up` / `Down`
3. Rotate: use the bottom-right toolbar, or press `Q` / `E`
4. Pan: use the unmodified wheel vertically and `Shift + mouse wheel` horizontally, drag with the mouse, or press `W` / `A` / `S` / `D`
5. Image information: click the mouse wheel, or press `Tab` / `I`
6. Fullscreen: double-click, or press `F` / `F11`
7. Copy image: `Ctrl + C`
8. Print image: use the context menu, or press `Ctrl + P`
9. Settings: use the bottom-right toolbar, or press `F1`
10. Browse frames: use the top controls, or press `J` / `K` / `L`
11. Split an animation into frames: `Ctrl + S`

Every keyboard shortcut and all three wheel actions can be reassigned on the Settings “Shortcuts” tab. Changes are stored in the existing 4096-byte settings file and survive restarts.

Opening an image now starts in a borderless immersive preview covering the current monitor work area. A per-pixel-alpha black layer at roughly 60% opacity keeps the desktop visible while the image itself stays opaque; Windows frosted glass is not used. After returning to the framed window, the area outside the image switches to opaque `#7F7F7F` middle gray and remains stable when resized. Small images use logical 100% at the current DPI. Landscape images are capped at 90% of the work-area width and 82.5% of its height; portrait images shrink only when their width exceeds 90%, and may be panned vertically when taller than the viewport.
Clicking the background outside the image or pressing `Esc` returns to a framed window sized for the image currently visible at the moment presentation ends, capped at 90% of the monitor work area. If another image is selected while immersive, that image determines the restored frame. Once framed, browsing previous or next images keeps the frame fixed while each image retains its own preview zoom. Rotation is remembered per image path.
“Remember Last Monitor” is enabled by default; if that monitor is disconnected, the window falls back to the primary display.

## Format support

- Still: `apng avif avifs blp bmp dib exr gif hdr heic heif ico icon jfif jp2 jpe jpeg jpg jxl jxr livp pbm pfm pgm pic png pnm ppm psd pxm qoi ras sr svg tga tif tiff webp wp2`
- Animated: `gif webp png apng jxl avif`
- Live: LivePhoto, MicroVideo, and MotionPhoto in `livp`, `jpg`, `heic`, or `heif` files (audio is not played yet)
- RAW: `3fr ari arw bay cap cr2 cr3 crw dcr dcs dng drf eip erf fff gpr iiq k25 kdc mdc mef mos mrw nef nrw orf pef ptx r3d raf raw rw2 rwl rwz sr2 srf srw x3f`

## Build and local install

The build requires Windows x64, Visual Studio 2026 Build Tools, MSVC v145, and the project's static libraries.

```powershell
.\buildRelease.ps1
.\installLocal.ps1
.\packageRelease.ps1 -SkipBuild
```

Build outputs are written to `x64/Release`. The install script installs to `%LOCALAPPDATA%\Programs\YeImageViewer`, registers the per-user thumbnail provider, creates Start-menu and desktop shortcuts, reports the install location, and launches the viewer. It does not change default image-file applications automatically. The packaging script creates both a full LZMA2 archive and a one-click setup executable with every supported format; each download must remain below 25 MiB.

This repository started as a shallow clone of JarkViewer. To prepare the upstream source again, keep the shallow-clone recommendation:

```sh
git clone git@github.com:jark006/JarkViewer.git --depth=50
```

Upstream static libraries are available from [JarkViewer static_lib](https://github.com/jark006/JarkViewer/releases/tag/static_lib). Additional upstream implementation notes are available on [DeepWiki](https://deepwiki.com/jark006/JarkViewer) and [Zread](https://zread.ai/jark006/JarkViewer).

## Compatibility

- Supports 64-bit Windows 10 and Windows 11.
- Does not support 32-bit Windows or Windows 7 and earlier.

## License

This project is open source under GPL-3.0. See [LICENSE](LICENSE).
