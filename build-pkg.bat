REM Before executing this command, you need to install WiX

set WIX_HOME=C:\Program Files\Windows Installer XML v3.5\bin
set PATH=%WIX_HOME%

candle.exe -ext "%WIX_HOME%\WixUIExtension.dll" -ext "%WIX_HOME%\WixDifxAppExtension.dll" SteVPN_setup.wxs
light.exe -ext "%WIX_HOME%\WixUIExtension.dll" -ext "%WIX_HOME%\WixDifxAppExtension.dll" SteVPN_setup.wixobj difxapp_x86.wixlib  -o SteVPN_setup.msi
