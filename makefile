# Intended for use with free compiler from Borland
# http://www.borland.com/bcppbuilder/freecompiler/

MYLIB     = ..\..\Lib
MYINCLUDE = ..\..\Include

# ---------------------------------------------------------------------------
BCB         = $(MAKEDIR)\..
DCC32       = dcc32
LINKER      = ilink32
BCC32       = bcc32
LIBPATH     = $(BCB)\lib;$(MYLIB)
PATHCPP     = .;$(MYLIB)
USERDEFINES = _DEBUG
SYSDEFINES  = NO_STRICT;_NO_VCL;_RTLDLL
INCLUDEPATH = $(BCB)\include;$(MYINCLUDE)
WARNINGS    = -w-par
CFLAG1      = -w-rvl -w-pia -w-aus -d -x- -c -O2 -w3 -RT- -N- -a2
LFLAGS      = -Tpd -Gn

# ---------------------------------------------------------------------------
CTOOLS_PRJ = CTools.dll
CTOOLS_OBJ = CTools.obj $(MYLIB)\scanreg.obj $(MYLIB)\farintf.obj $(MYLIB)\cregexp.obj collect.obj

.autodepend

ALL: $(CTOOLS_PRJ) CTools.lng

$(CTOOLS_PRJ): $(CTOOLS_OBJ)
    $(BCB)\BIN\$(LINKER) @&&!
    $(LFLAGS) -L$(LIBPATH) +
    c0d32.obj $(CTOOLS_OBJ), +
    $(CTOOLS_PRJ),, +
    import32.lib cw32.lib htmlhelp.lib, +
    $(DEFFILE),
!

CTools.lng: CToolsLng.h
    perl makelng.pl CTools

# ---------------------------------------------------------------------------
.PATH.CPP = $(PATHCPP)
.PATH.C   = $(PATHCPP)

.cpp.obj:
    $(BCB)\BIN\$(BCC32) $(CFLAG1) $(WARNINGS) -I$(INCLUDEPATH) -D$(USERDEFINES);$(SYSDEFINES) -n$(@D) {$< }

.c.obj:
    $(BCB)\BIN\$(BCC32) $(CFLAG1) $(WARNINGS) -I$(INCLUDEPATH) -D$(USERDEFINES);$(SYSDEFINES) -n$(@D) {$< }
# ---------------------------------------------------------------------------
