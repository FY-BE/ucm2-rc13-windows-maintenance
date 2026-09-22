@echo off
setlocal

net session >nul 2>&1
if not "%errorlevel%"=="0" (
  echo Requesting administrator permission to install Hikrobot MVS camera drivers...
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
  exit /b 0
)

set "ROOT=%~dp0"
set "LOG=%ROOT%MVS-driver-install.log"
echo [%date% %time%] Hikrobot MVS driver installation started > "%LOG%"

if exist "%ROOT%MVS\Drivers\GigE\install.bat" (
  call "%ROOT%MVS\Drivers\GigE\install.bat" >> "%LOG%" 2>&1
  if errorlevel 1 echo GigE driver installer returned %errorlevel% >> "%LOG%"
) else (
  echo GigE driver package is missing >> "%LOG%"
)

if exist "%ROOT%MVS\Drivers\Usb3.0\install.bat" (
  call "%ROOT%MVS\Drivers\Usb3.0\install.bat" >> "%LOG%" 2>&1
  if errorlevel 1 echo USB3 driver installer returned %errorlevel% >> "%LOG%"
) else (
  echo USB3 driver package is missing >> "%LOG%"
)

echo [%date% %time%] Finished. Review "%LOG%". >> "%LOG%"
echo Hikrobot MVS driver installation finished. Log: "%LOG%"
exit /b 0
