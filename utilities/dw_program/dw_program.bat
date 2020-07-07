@echo OFF
IF "%~1"=="" GOTO Usage

echo Erasing flash ...
Esptool.py -p %1 -b 460800 erase_flash

echo Programming flash ...
Esptool.py -p %1 -b 460800 --after no_reset write_flash --flash_mode dio --flash_freq 40m 0x1000 bootloader.bin 0xE000 partition-table.bin 0x10000 ota_data_initial.bin 0x200000 dw_ModelA.bin

echo Encrypting flash contents ....

rem Delay for 10 seconds while Bootloader encrypts flash contents
ping -n 11 127.0.0.1 > nul

echo All Done.
GOTO:EOF

:Usage
echo Usage: dw_program COMn
echo where COMn = programming adapter port
echo

:EOF
