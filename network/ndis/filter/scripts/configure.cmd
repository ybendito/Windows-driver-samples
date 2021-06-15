:: This is an example of configuration script
net stop ndislwf
reg add HKLM\SYSTEM\CurrentControlSet\Services\NdisLwf\Parameters /f /v TimerPeriod /t REG_DWORD /d 5001
reg add HKLM\SYSTEM\CurrentControlSet\Services\NdisLwf\Parameters /f /v MAC0 /t REG_BINARY /d 40A6B75183E5
reg add HKLM\SYSTEM\CurrentControlSet\Services\NdisLwf\Parameters /f /v MAC1 /t REG_BINARY /d 40A6B75183E4
reg add HKLM\SYSTEM\CurrentControlSet\Services\NdisLwf\Parameters /f /v MAC2 /t REG_BINARY /d 000000000000
reg add HKLM\SYSTEM\CurrentControlSet\Services\NdisLwf\Parameters /f /v MAC3 /t REG_BINARY /d 000000000000
reg add HKLM\SYSTEM\CurrentControlSet\Services\NdisLwf\Parameters /f /v LargePacketSize /t REG_DWORD /d 4096
reg add HKLM\SYSTEM\CurrentControlSet\Services\NdisLwf\Parameters /f /v LargePacketsAction /t REG_DWORD /d 0
net start ndislwf