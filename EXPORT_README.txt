Wonder Vision Studio - portable export

GETTING STARTED
1. Extract this entire ZIP to a folder. Do not run the app from inside the ZIP.
2. Open WonderVisionStudio\WonderVisionStudio.exe.
3. Choose an image or GIF, adjust the Display and Overlay tabs, then click Start Screen.

CUSTOM OVERLAY TEXT
The Overlay tab's Rewrite the overlay text box replaces the automatic stats. It supports these live placeholders:
  {cpu_temp}  {cpu_load}  {gpu_temp}  {gpu_load}  {clock}
Example:
  CPU {cpu_temp} C  {cpu_load}%
  GPU {gpu_temp} C  {gpu_load}%
  {clock}
Save your settings with Save Config.... Configs remember the media file path, so keep that image/GIF in place.

LIVE SENSOR DATA
Run Libre Hardware Monitor, then select Options > Remote Web Server > Run.
In Wonder Vision Studio, open Sensors and click Refresh sensors.

START WITH WINDOWS AND THE SYSTEM TRAY
The Display tab has a Start screen when I sign in option. Wonder Vision Studio starts in the system tray; use its tray icon to reopen it or quit. Leave the extracted folder in place after enabling startup.
To start Libre Hardware Monitor independently at sign-in, enable Start Libre Hardware Monitor with Windows (in tray) and browse to LibreHardwareMonitor.exe. Wonder Vision adds a separate startup entry and saves LHM's tray and sensor server settings. The app also opens LHM when Wonder Vision itself is opened.

TROUBLESHOOTING
If Windows says a Visual C++ runtime is missing, run vc_redist.x64.exe in this folder.
The app needs the Wonder Vision display connected over USB. Close other software that is using it.
