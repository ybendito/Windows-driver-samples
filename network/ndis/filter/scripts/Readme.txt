The driver is filter driver that sits above selected network adapters and can drop
or redirected incoming UDP packets based on their size and destination UDP port

Typical procedure:
Installation
Configuration
Configuration
Uninstallation

- Installation
   Configure the machine for test mode: "bcdedit /set testsigning on" + reboot (once)
   Copy all the files to a local directory
   From admin command prompt in this directory run: install-filter.cmd
   Allow installation when the OS asks

- Configuration
  Populate MAC address(es) of selected receiving adapter(s) in configure.cmd
  By default the driver does not do anything as the MAC addresses of the network adapters
    to bind to them are not specified. The driver supports up to 4 MAC addresses.
    In practice we will use one or two. The MAC0,MAC1,MAC2,MAC3 are binary values of 12 hex digits,
	i.e. 01:02:03:04:05:06 = 010203040506 
  The driver inspects packets with total length > 'LargePacketSize' parameter.
  If the 'LargePacketSize' = 0, the inspection is disabled.
  Set 'LargePacketSize' to the size of UDP payload, then the payload packets will be larger.
  If 'DropLargePackets' parameter set to 1 - the driver will drop any UDP packet larger than 'LargePacketSize'.
  If 'DropLargePackets' parameter set to 0 - the driver will redirect any UDP packet larger than 'LargePacketSize'
    to CPU from 2 to 9 depending on 3 lower bits of the destination UDP port.
  For multiple UDP stream (up to 8) use 8 sequential numbers of the UDP ports on receiver side.

  It is recommended to leave parameter 'TimerPeriod' at 5001. Timer is used only for statistics.

Uninstallation:
  uninstall-filter.cmd

Just stop until next reboot:
  net stop ndislwf
Restart after stop:
  net start ndislwf


Any questions to Yuri Benditovich 0547-884160 any time.

