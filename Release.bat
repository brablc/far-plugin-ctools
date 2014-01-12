@echo off
del  "%TEMP%\CTools.dll"
move "%FARHOME%\Plugins\CTools\CTools.dll" "%TEMP%"
copy CTools.dll "%FARHOME%\Plugins\CTools"
copy CTools.hlf "%FARHOME%\Plugins\CTools"
copy CTools.lng "%FARHOME%\Plugins\CTools"
exit

rem Ignored
del  D:\Andy\WWW\download\ctools.zip
cd   %FARHOME%\Plugins\CTools\
zip -R ctools.zip *.*
if "%1" == "UPLOAD" ftp <%FARHOME%\Dev\Plugins\CTools\Release.ftp
move ctools.zip D:\Andy\WWW\download
