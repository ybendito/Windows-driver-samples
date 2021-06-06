@echo off
cd /d "%~dp0"
netcfg -v -u MS_NdisLwf
timeout /t 3
for %%f in (c:\windows\inf\oem*.inf) do call :checkinf %%f
echo Done
timeout /t 3
goto :eof

:checkinf
type %1 | findstr /i netlwf.cat
if not errorlevel 1 goto :removeinf
echo %1 is not MS_NdisLwf inf file
goto :eof

:removeinf
echo %1 is MS_NdisLwf inf file
pnputil /d "%~nx1" /force
timeout /t 2
goto :eof



