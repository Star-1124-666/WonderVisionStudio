# Wonder Vision Studio

A small standalone Windows app for Thermalright Wonder Vision displays. It does not use OpenRGB.

Features:

- Show still images or animated GIFs.
- Overlay selected CPU/GPU temperatures, loads, and a clock.
- Move the overlay freely with X/Y controls and adjust font, size, weight, italics, text alignment, text color, outline, background color, opacity, corner radius, and padding.
- Replace the automatic overlay with custom multiline text and live `{cpu_temp}`, `{cpu_load}`, `{gpu_temp}`, `{gpu_load}`, and `{clock}` placeholders.
- Organize screen content, overlay styling, and sensor selection into separate tabs.
- Save and reload the selected image, display, sensor, and overlay settings with **Save Config...** and **Load Config...**. The most recently saved or loaded config opens automatically next time.
- Start minimized to the system tray and optionally start the screen when you sign in to Windows, using the saved configuration.
- Optionally launch Libre Hardware Monitor with the app, minimized.
- Read live sensor values from Libre Hardware Monitor's Remote Web Server at `127.0.0.1:8085`.
- Detect the panel firmware frame size during the USB handshake and rotate the image 180 degrees for the upside-down panel mount.
- Preserve image aspect ratio, support fit/fill modes, and cache scaling between GIF frames.

Close any other app using the LCD before starting the screen. Leave the Wonder Vision connected to its internal USB header.

## Build

This project expects the Qt 5.15.0 MSVC 2019 64-bit package and Visual Studio Build Tools 2022 at the same locations used by the previous plugin build. Open PowerShell in this folder and run:

```powershell
.\build.bat
```

The portable app is created at `build_config\release_export4\WonderVisionStudio.exe`. The build also creates `WonderVisionStudio_Export.zip`, a portable folder with the app, required Qt files, and quick-start instructions. Older builds are left untouched.

## Use

1. Start Libre Hardware Monitor and enable **Options → Remote Web Server → Run** if you want sensor overlays.
2. Open `WonderVisionStudio.exe`.
3. Choose a PNG, JPG, BMP, WebP, or GIF.
4. Use the Display, Overlay, and Sensors tabs to choose content and style the text.
5. Click **Start Screen**.

Use **Save Config...** to save the current image and settings to an INI file. Turn on **Start screen when I sign in** in the Display tab to save the current configuration and launch the screen to the system tray after signing in. The app can remove this startup entry when you turn the option off. Close the app window to hide it in the tray; use the tray menu to reopen it or quit.

To start Libre Hardware Monitor independently at sign-in, enable **Start Libre Hardware Monitor with Windows (in tray)** and browse to `LibreHardwareMonitor.exe`. Wonder Vision adds a separate Windows startup entry, saves LHM's tray preferences, and enables its local sensor web server. The app also opens LHM when Wonder Vision itself is opened.

In the Overlay tab, enter your full overlay in **Rewrite the overlay text**. This replaces the automatic CPU/GPU/clock lines. For example, `CPU: {cpu_temp} °C` is replaced with the current CPU temperature. Leave the box blank to return to the automatic lines. Save the config to keep your text.

The sensor overlays can be hidden independently. If LHM is not running, the image or GIF still works and unavailable readings show as `--`.
