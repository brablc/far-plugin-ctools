DST   = C:\Far\Plugins\CTools
SRC   = C:\Far\Dev\Plugins\CTools

ALL: $(DST)\CTools.dll $(DST)\CTools.hlf $(DST)\CTools.lng

$(DST)\CTools.dll: $(SRC)\CTools.dll
    if exist %TEMP%\CTools.dll del  %TEMP%\CTools.dll
    if exist $(DST)\CTools.dll move $(DST)\CTools.dll %TEMP%
    copy $(SRC)\CTools.dll $(DST)

$(DST)\CTools.hlf: $(SRC)\CTools.hlf
    copy $(SRC)\CTools.hlf $(DST)

$(DST)\CTools.lng: $(SRC)\CTools.lng
    copy $(SRC)\CTools.lng $(DST)
