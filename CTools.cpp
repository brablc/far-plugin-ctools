#include <string>
#include <time.h>
#include <stdio.h>

#include <winmem.h>
#include <scanreg.h>
#include <cregexp.h>
#include <FARIntf.h>

#include "collect.h"
#include "CTools.h"
#include "CToolslng.h"

/*** Macros *********************************************************************/

// EXARR: pass an existing array where an open array is expected
#define EXARR(a) (a), ((sizeof(a)/sizeof(a[0])))
#define MARK        "Mark"
#define MARKADD     "MarkAdd"
#define MARKPATTERN "MarkPattern"
#define PROGINIT    "ProgInit"

#undef DEBUG

#ifdef DEBUG
#include <stdio.h>
static void SysLog(char *fmt,...)
{
  va_list argptr;
  va_start(argptr, fmt);
  FILE *LogStream =
    fopen("C:\\FAR\\Dev\\Plugins\\CTools\\CTools.log", "at");
  vfprintf(LogStream, fmt, argptr);
  fflush(LogStream);
  fclose(LogStream);
  va_end(argptr);
}

static void SysMsg(char *fmt,...)
{
  char Msg[256];
  va_list argptr;
  va_start(argptr, fmt);
  vsprintf(Msg, fmt, argptr);
  va_end(argptr);

  const char *MsgItems[]={"Debug Message",Msg,GetMsg(MOK)};
  Info.Message(Info.ModuleNumber,0,NULL,EXARR(MsgItems),1);
}

#else
#define SysLog //
#define SysMsg //
#endif

/*** Structures and variables *************************************************/

static struct TOptions
{
  const char * fileName;
  int  MaxLineLen;
  int  TabSize;

  int  TruncateSpaces;
  int  CheckLongLines;
  int  ExpandTabs;

  int  IgnoreLongLines;

  char OpenComment[256],
       CloseComment[256],
       OpenMark[256],
       CloseMark[256],
       CloseMarkAdd[256],
       LeadingComment[256],
       HeaderComment[256];

  BOOL IsTruncateable();

  int  Customize(char*);
  void SetComments(char*);
  void GetComments();
  void SaveIgnoreLongLines(int newValue);

} Opt = { 0, 80, 4, 0 };

static int regErrorShown = FALSE;

static bool ignoreToday = false;

/*** Functions ****************************************************************/

static inline int IsCharSpace(char c)
{
  return strchr(" \t\r\n", c) != NULL;
}

void NotImplemented()
{
  const char *MsgItems[]={"Oops","Function not implemented yet!",GetMsg(MOK)};
  Info.Message(Info.ModuleNumber,FMSG_WARNING,NULL,EXARR(MsgItems),1);
}

static void BuildPatternMark(char * pattern, bool all, char * currentMark, char * currentPattern)
{
  static char *m = MARK;
  static char *p = MARKPATTERN;
  GetRegKey(HKEY_CURRENT_USER,"", m, currentMark, (char*)GetMsg(MDefaultMark), 250);
  GetRegKey(HKEY_CURRENT_USER,"", p, currentPattern, (char*)GetMsg(MDefaultPattern), 250);

  if (all)
  {
    FSF.sprintf( pattern , "/(%s) ", currentPattern);
  }
  else
  {
    FSF.sprintf( pattern , "/(%s) ", currentMark);
  }
  strcat( pattern, GetMsg(MMarkSearch));
  strcat( pattern, "/");
}

static char *GetCommaWord(char *Src, char *Word)
{
  if (!*Src)
  {
    return NULL;
  }
  int WordPos, SkipBrackets = FALSE;
  for ( WordPos = 0 ; *Src ; Src++, WordPos++ )
  {
    if (*Src == '[' && strchr(Src+1,']'))
      SkipBrackets = TRUE;
    if (*Src == ']')
      SkipBrackets = FALSE;
    if (*Src==',' && !SkipBrackets)
    {
      Word[WordPos] = 0;
      Src++;
      while ( IsCharSpace(*Src) )
      {
        Src++;
      }
      return Src;
    }
    else
    {
      Word[WordPos] = *Src;
    }
  }
  Word[WordPos] = 0;

  return Src;
}

/*** Editor functions *********************************************************/

static int CheckForEsc()
{
  int ExitCode = FALSE;
  for ( ; ; )
  {
    INPUT_RECORD rec;
    static HANDLE hConInp = GetStdHandle(STD_INPUT_HANDLE);
    DWORD ReadCount;
    PeekConsoleInput(hConInp,&rec,1,&ReadCount);
    if (!ReadCount)
      break;
    ReadConsoleInput(hConInp,&rec,1,&ReadCount);
    if (rec.EventType == KEY_EVENT &&
        rec.Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE)
    {
      ExitCode = TRUE;
    }
  }
  return ExitCode;
}

struct TEditorPos
{
  TEditorPos() { Default(); }
  int Row, Col;
  int TopRow,LeftCol;
  void Default(void) { Row = Col = TopRow = LeftCol = -1; }
};

static TEditorPos EditorGetPos(void)
{
  TEditorPos r;
  EditorInfo ei;
  Info.EditorControl(ECTL_GETINFO, &ei);
  r.Row = ei.CurLine;
  r.Col = ei.CurPos;
  r.TopRow = ei.TopScreenLine;
  r.LeftCol = ei.LeftPos;
  return r;
}

static void EditorSetPos(TEditorPos pos)
{
  EditorSetPosition sp;
  sp.CurLine = pos.Row;
  sp.CurPos = pos.Col;
  sp.TopScreenLine = pos.TopRow;
  sp.LeftPos = pos.LeftCol;
  sp.CurTabPos = sp.Overtype = -1;
  Info.EditorControl(ECTL_SETPOSITION,&sp);
}

static void EditorProcessVKey(WORD vkey, int state, int count=1)
{
  INPUT_RECORD tr;
  tr.EventType = KEY_EVENT;
  tr.Event.KeyEvent.bKeyDown = TRUE;
  tr.Event.KeyEvent.wRepeatCount = count;
  tr.Event.KeyEvent.wVirtualKeyCode = vkey;
  tr.Event.KeyEvent.dwControlKeyState = state;
  Info.EditorControl(ECTL_PROCESSINPUT, &tr);
}

static void EditorGetString(EditorGetString *gs, int line = -1)
{
  gs->StringNumber = line;
  Info.EditorControl(ECTL_GETSTRING, gs);
}

static void EditorInsertLine(char * str, int line = -1)
{
    struct EditorInfo ei;
    struct EditorGetString   egs;
    struct EditorSetString   ess;
    struct EditorConvertText ect;
    TEditorPos p = EditorGetPos();

    Info.EditorControl(ECTL_GETINFO,&ei);

    if (line == -1)
    {
        line = ei.CurLine;
    }

    p.Row = line;
    p.Col = 0;
    EditorSetPos(p);

    Info.EditorControl(ECTL_INSERTSTRING,NULL);
    EditorGetString( &egs, line);

    ect.Text=str;
    ect.TextLength=strlen(str);
    Info.EditorControl(ECTL_OEMTOEDITOR,&ect);
    ess.StringNumber=egs.StringNumber;
    ess.StringText=ect.Text;
    ess.StringEOL= const_cast<char *>(egs.StringEOL);
    ess.StringLength=ect.TextLength;
    Info.EditorControl(ECTL_SETSTRING,&ess);
}

/*** Options functions ********************************************************/

static bool AddMask(HKEY, char*, char *key, FarMenuItem *menu,
                    int *n, void *data)
{
  strcpy(menu->Text, key);
  return (data != NULL && *n == -1) ? strcmp(key, (char*)data) == 0 : false;
}

static bool FindMask(HKEY, char *Key, char *key, void *data)
{
  char pattern[256], FullKeyName[512];
  char p[80], *m = pattern;
  FSF.sprintf(FullKeyName,"%s\\%s", Key, key);
  GetRegKey(HKEY_CURRENT_USER,FullKeyName,"Mask", pattern, "", 255);
  while ( ( m = GetCommaWord(m, p) ) != NULL )
  {
    if (Info.CmpName(p, (char*)data, TRUE))
    {
      return 1;
    }
  }
  return 0;
}

void TOptions::SetComments(char* aLang)
{
  char lang[256] = "", tMask[256] = "";
  char tOpenComment[256] = "",
       tCloseComment[256] = "",
       tOpenMark[256] = "",
       tCloseMark[256] = "",
       tCloseMarkAdd[256] = "",
       tSearchMark[256] = "",
       tLeadingComment[256] = "",
       tHeaderComment[256] = "",
       tsMaxLineLength[256] = "",
       tsTabSize[256] = "";

  int  tTruncateSpaces = 0;
  int  tCheckLongLines = 0;
  int  tExpandTabs = 0;
  int  tMaxLineLength = 80;
  int  tTabSize = 4;

  char FullKeyName[512];

  if (aLang)
  {
    FSF.sprintf(FullKeyName,"Extensions\\%s", strcpy(lang, aLang));
    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "Mask",   tMask,   "", 255);

    tTruncateSpaces = GetRegKey(HKEY_CURRENT_USER,
        FullKeyName, "TruncateSpaces", tTruncateSpaces);
    tCheckLongLines = GetRegKey(HKEY_CURRENT_USER,
        FullKeyName, "CheckLongLines", tCheckLongLines);
    tExpandTabs = GetRegKey(HKEY_CURRENT_USER,
        FullKeyName, "ExpandTabs", tExpandTabs);
    tMaxLineLength = GetRegKey(HKEY_CURRENT_USER,
        FullKeyName, "MaxLineLenght", tMaxLineLength);
    tTabSize = GetRegKey(HKEY_CURRENT_USER,
        FullKeyName, "TabSize", tTabSize);

    sprintf(tsMaxLineLength,"%d", tMaxLineLength);
    sprintf(tsTabSize,"%d", tTabSize);

    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "OpenComment",    tOpenComment,    "", 255);
    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "CloseComment",   tCloseComment,   "", 255);
    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "OpenMark",        tOpenMark,        "", 255);
    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "CloseMark",       tCloseMark,       "", 255);
    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "CloseMarkAdd",    tCloseMarkAdd,    "", 255);
    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "LeadingComment", tLeadingComment, "", 255);
    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "HeaderComment",  tHeaderComment,  "", 255);
  }

  char *M = "CTools-OpenComment";
  char *I = "CTools-CloseComment";
  char *O = "CTools-OpenMark";
  char *C = "CTools-CloseMark";
  char *A = "CTools-CloseMarkAdd";
  char *L = "CTools-LeadingComment";
  char *H = "CTools-HeaderComment";

  int i = 0;

  struct InitDialogItem InitItems[] =
  {
    { DI_DOUBLEBOX, 3, ++i,72, 16,0,       0,              (char*)MCTitle   },

    { DI_TEXT,      5, ++i,29,  0,0,       0,              (char*)MLang     },
    { DI_EDIT,     30,   i,70,  0,0,       0,              lang             },

    { DI_TEXT,      5, ++i,29,  0,0,       0,              (char*)MMask     },
    { DI_EDIT,     30,   i,70,  0,0,       0,              tMask            },

    { DI_CHECKBOX,  5, ++i,70,  0,0,       0,              (char*)MTruncate },
    { DI_CHECKBOX,  5, ++i,60,  0,0,       0,              (char*)MCheckLongLines },
    { DI_EDIT,     68,   i,70,  0,0,       0,              tsMaxLineLength},
    { DI_CHECKBOX,  5, ++i,60,  0,0,       0,              (char*)MExpandTabs },
    { DI_EDIT,     68,   i,70,  0,0,       0,              tsTabSize },

    { DI_TEXT,      5, ++i,29,  0,0,       0,              (char*)MOpenCmt  },
    { DI_EDIT,     30,   i,70,  0,(DWORD)M,DIF_HISTORY,    tOpenComment     },

    { DI_TEXT,      5, ++i,29,  0,0,       0,              (char*)MCloseCmt },
    { DI_EDIT,     30,   i,70,  0,(DWORD)I,DIF_HISTORY,    tCloseComment    },

    { DI_TEXT,      5, ++i,29,  0,0,       0,              (char*)MOpenMark  },
    { DI_EDIT,     30,   i,70,  0,(DWORD)O,DIF_HISTORY,    tOpenMark         },

    { DI_TEXT,      5, ++i,29,  0,0,       0,              (char*)MCloseMark },
    { DI_EDIT,     30,   i,70,  0,(DWORD)C,DIF_HISTORY,    tCloseMark        },

    { DI_TEXT,      5, ++i,29,  0,0,       0,              (char*)MCloseAdd },
    { DI_EDIT,     30,   i,70,  0,(DWORD)A,DIF_HISTORY,    tCloseMarkAdd     },

    { DI_TEXT,      5, ++i,29,  0,0,       0,            (char*)MLeadingCmt },
    { DI_EDIT,     30,   i,70,  0,(DWORD)L,DIF_HISTORY,    tLeadingComment  },

    { DI_TEXT,      5, ++i,29,  0,0,       0,            (char*)MHeaderCmt },
    { DI_EDIT,     30,   i,70,  0,(DWORD)M,DIF_HISTORY,    tHeaderComment},

    { DI_TEXT,      5, ++i,0,255,0,       0,              NULL             },
    { DI_BUTTON,    0, ++i,0,  0,0,       DIF_CENTERGROUP,(char*)MOK       },
    { DI_BUTTON,    0,   i,0,  0,0,       DIF_CENTERGROUP,(char*)MCancel   }
  };

  if (tTruncateSpaces == 1)
  {
    InitItems[5].Selected = TRUE;
  }
  if (tCheckLongLines == 1)
  {
    InitItems[6].Selected = TRUE;
  }
  if (tExpandTabs == 1)
  {
    InitItems[8].Selected = TRUE;
  }

  int dOk      = sizeof(InitItems)/sizeof(InitItems[0]) - 2;

  int ExitCode = ExecDialog(EXARR(InitItems),2,dOk, NULL);
  if (ExitCode == dOk)
  {
    if (!*lang)
    {
      return;
    }

    fileName = 0;

    tTruncateSpaces = (InitItems[5].Selected?1:0);
    tCheckLongLines = (InitItems[6].Selected?1:0);
    tExpandTabs = (InitItems[8].Selected?1:0);

    FSF.sprintf(FullKeyName,"Extensions\\%s", lang);
    SetRegKey(HKEY_CURRENT_USER, FullKeyName,
        "Mask",   tMask  );
    SetRegKey(HKEY_CURRENT_USER,FullKeyName,
        "TruncateSpaces",  tTruncateSpaces);
    SetRegKey(HKEY_CURRENT_USER, FullKeyName,
        "CheckLongLines", tCheckLongLines);
    SetRegKey(HKEY_CURRENT_USER, FullKeyName,
        "ExpandTabs", tExpandTabs);
    SetRegKey(HKEY_CURRENT_USER, FullKeyName,
        "MaxLineLenght",  atoi(tsMaxLineLength));
    SetRegKey(HKEY_CURRENT_USER,FullKeyName,
        "TabSize", atoi(tsTabSize));

    SetRegKey(HKEY_CURRENT_USER, FullKeyName,
        "OpenComment",     tOpenComment );
    SetRegKey(HKEY_CURRENT_USER, FullKeyName,
        "CloseComment",    tCloseComment);
    SetRegKey(HKEY_CURRENT_USER, FullKeyName,
        "OpenMark",         tOpenMark );
    SetRegKey(HKEY_CURRENT_USER, FullKeyName,
        "CloseMark",        tCloseMark);
    SetRegKey(HKEY_CURRENT_USER, FullKeyName,
        "CloseMarkAdd",     tCloseMarkAdd);
    SetRegKey(HKEY_CURRENT_USER, FullKeyName,
        "LeadingComment",  tLeadingComment);
    SetRegKey(HKEY_CURRENT_USER, FullKeyName,
        "HeaderComment", tHeaderComment);
  }
}

void TOptions::SaveIgnoreLongLines(int newValue)
{
  IgnoreLongLines = newValue;
  SetRegKey(HKEY_CURRENT_USER, "", "IgnoreLongLines", newValue);
}

void TOptions::GetComments()
{
  char key[512], FullKeyName[512];

  struct EditorInfo ei;
  Info.EditorControl(ECTL_GETINFO,&ei);

  if (fileName && !strcmp(ei.FileName,fileName))
  {
    return;
  }

  TruncateSpaces  = 0;
  CheckLongLines  = 0;
  ExpandTabs      = 0;
  MaxLineLen      = 80;
  TabSize         = 4;

  *OpenMark =
  *CloseMark =
  *CloseMarkAdd =
  *OpenComment =
  *CloseComment =
  *LeadingComment = 0;

  if (FindFirstRegKey(HKEY_CURRENT_USER, "Extensions", FindMask,
      static_cast<void *>(const_cast<char*>(ei.FileName)),
      key, 255))
  {
    FSF.sprintf(FullKeyName,"Extensions\\%s", key);

    TruncateSpaces = GetRegKey(HKEY_CURRENT_USER,
        FullKeyName, "TruncateSpaces", TruncateSpaces);
    CheckLongLines = GetRegKey(HKEY_CURRENT_USER,
        FullKeyName, "CheckLongLines", CheckLongLines);
    ExpandTabs = GetRegKey(HKEY_CURRENT_USER,
        FullKeyName, "ExpandTabs", ExpandTabs);
    MaxLineLen      = GetRegKey(HKEY_CURRENT_USER,
        FullKeyName, "MaxLineLenght",  MaxLineLen);
    TabSize = GetRegKey(HKEY_CURRENT_USER,
        FullKeyName, "TabSize",  TabSize);

    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "OpenComment",    OpenComment, "", 255);
    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "CloseComment",   CloseComment, "", 255);
    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "OpenMark",        OpenMark, "", 255);
    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "CloseMark",       CloseMark, "", 255);
    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "CloseMarkAdd",    CloseMarkAdd, "", 255);
    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "LeadingComment", LeadingComment, "", 255);
    GetRegKey(HKEY_CURRENT_USER,FullKeyName,
              "HeaderComment",  HeaderComment, "", 255);
  }

  IgnoreLongLines = GetRegKey(HKEY_CURRENT_USER, "", "IgnoreLongLines", IgnoreLongLines);

}

int TOptions::Customize(char *desc)
{
  FarMenuItem *menu = NULL;
  int Code, n = -1;
  int BreakKeys[5] = { VK_INSERT, VK_DELETE, VK_F4, VK_RETURN, 0 };
  char key[256];
  do
  {
    int Count = MenuFromRegKey(HKEY_CURRENT_USER, "Extensions",
                               AddMask, &menu, &n, desc);

    if (n >= 0 && n < Count) menu[n].Selected = true;

    n = Info.Menu(Info.ModuleNumber, -1, -1, 0, FMENU_WRAPMODE,
                  GetMsg(MCTitle), GetMsg(MCBottom),
                  "Configuration", BreakKeys, &Code, menu, Count);
    if (n >= 0 && n < Count)
    {
      FSF.sprintf(key, "Extensions\\%s", menu[n].Text);
      switch ( Code )
      {
        case 0:
          SetComments(NULL);
          break;
        case 1:
          DeleteRegKey(HKEY_CURRENT_USER, key);
          break;
        case 2:
        case 3:
          SetComments(menu[n].Text);
          break;
        default:
          n = -1;
      }
    }
    if (menu)
    {
      winDel(menu);
    }
    menu = NULL;
  } while ( n != -1 );

  return TRUE;
}

bool IsBlock(void)
{
  struct EditorInfo ei;
  Info.EditorControl(ECTL_GETINFO,&ei);

  // Nothing selected?
  if (ei.BlockType == BTYPE_NONE)
  {
    const char *MsgItems[]={GetMsg(MError),GetMsg(MErrNoBlock),GetMsg(MOK)};
    Info.Message(Info.ModuleNumber,FMSG_WARNING,NULL,EXARR(MsgItems),1);
    return false;
  }

  return true;
}

/*** Special functions ********************************************************/

static int LineTooLong( void );

static void TruncateSpaces()
{
  struct EditorGetString   egs;
  struct EditorSetString   ess;
  struct EditorConvertText ect;
  struct EditorInfo ei;
  static CRegExp regSpaces;
  char * foundStr;

  int longLineCounter=0;

  SMatches m;
  regSpaces.SetExpr("/([ \t]+)$/");

  Info.EditorControl(ECTL_GETINFO, &ei);

  // Scan the file line by line
  for ( int i = 0 ; i < ei.TotalLines ; i++ )
  {
    EditorGetString(&egs,i);  // Get one string from the editor
    if (!egs.StringLength) continue;

    foundStr = winNew(char, egs.StringLength+1);
    if (!foundStr) return;

    memcpy(foundStr, egs.StringText, egs.StringLength);
    foundStr[egs.StringLength] = 0;

    int len = egs.StringLength;

    // Spaces found - truncate them
    if (regSpaces.Parse(foundStr, &m))
    {
      size_t resLen;
      resLen = (size_t)(m.e[1]-m.s[1]);

      ess.StringNumber = egs.StringNumber;
      ess.StringText   = const_cast<char*>(egs.StringText);
      ess.StringEOL    = const_cast<char*>(egs.StringEOL);
      ess.StringLength = egs.StringLength - resLen;
      Info.EditorControl(ECTL_SETSTRING,&ess);

      len = ess.StringLength;
    }

    if (len>Opt.MaxLineLen)
    {
        longLineCounter++;
    }

    winDel(foundStr);
  }

  TEditorPos p = EditorGetPos();
  TEditorPos o = p;

  // Delete empty lines
  // Scan the file line by line
  for ( int i = ei.TotalLines - 1 ; i > 0 ; i-- )
  {
    EditorGetString(&egs,i);  // Get one string from the editor
    if (egs.StringLength) break; // Stop when we have first string

    p.Row = i;
    EditorSetPos(p);

    Info.EditorControl(ECTL_DELETESTRING,NULL);
  }

  EditorSetPos(o);

  if (longLineCounter && !ignoreToday && !Opt.IgnoreLongLines)
  {
    struct InitDialogItem InitItems[] =
    {
    /*00*/ { DI_DOUBLEBOX, 3,1,70, 3, 0,               0, (char*)MLongLineFound },
    /*01*/ { DI_BUTTON,    0,2,0,  0, 1, DIF_CENTERGROUP, (char*)MShow },
    /*02*/ { DI_BUTTON,    0,2,0,  0, 0, DIF_CENTERGROUP, (char*)MIgnoreToday },
    /*03*/ { DI_BUTTON,    0,2,0,  0, 0, DIF_CENTERGROUP, (char*)MIgnoreForEver }
    };

    switch (ExecDialog(EXARR(InitItems),1,1, "LongLinesFound"))
    {
        case 1:
            LineTooLong();
            break;

        case 2:
            ignoreToday = true;
            break;

        case 3:
            ignoreToday = true;
            Opt.SaveIgnoreLongLines(1);
            break;

        default: return;
    }
  }
}

static void ExpandTabs()
{
  struct EditorGetString   egs;
  struct EditorSetString   ess;
  struct EditorConvertText ect;
  struct EditorInfo ei;

  Info.EditorControl(ECTL_GETINFO, &ei);

  // Scan the file line by line
  for ( int i = 0 ; i < ei.TotalLines ; i++ )
  {
    EditorGetString(&egs,i);  // Get one string from the editor
    if (!egs.StringLength) continue;

    std::string buffer;
    int tabcount=0;
    int start=0;
    int copied;

    for (copied=0; copied<egs.StringLength; copied++)
    {
        if (egs.StringText[copied]=='\t')
        {
            for (int k=start;k<copied;k++)
            {
                buffer += egs.StringText[k];
            }

            // always at least one place
            do
            {
                buffer += ' ';
            } while ((int)buffer.length()<Opt.TabSize);

            // align to TAB position
            while (buffer.length()%Opt.TabSize)
            {
                buffer += ' ';
            }

            start = copied+1;
            tabcount++;
        }
    }

    if (tabcount)
    {
      for (int k=start;k<copied;k++)
      {
          buffer += egs.StringText[k];
      }

      ess.StringNumber = egs.StringNumber;
      ess.StringText   = const_cast<char*>(buffer.c_str());
      ess.StringEOL    = const_cast<char*>(egs.StringEOL);
      ess.StringLength = buffer.length();
      Info.EditorControl(ECTL_SETSTRING,&ess);
    }
  }
}

/******************************************************************************/
/*** Menu functions ***********************************************************/
/******************************************************************************/

static int SelectCurrent()
{
  static char *m = MARK;
  static char *a = MARKADD;
  static char *p = MARKPATTERN;

  char mark[256], add[256], pattern[256];

  GetRegKey(HKEY_CURRENT_USER,"", m, mark,    (char*)GetMsg(MDefaultMark), 250);
  GetRegKey(HKEY_CURRENT_USER,"", p, pattern, (char*)GetMsg(MDefaultPattern), 250);
  GetRegKey(HKEY_CURRENT_USER,"", a, add,     "", 60);

  int i=0;

  struct InitDialogItem InitItems[] =
  {
    { DI_DOUBLEBOX, 3, ++i,51, 7,0,        0,               (char*)MTitle }, // 0
    { DI_TEXT,      5, ++i,30, 0,0,        0,         (char*)MSetMark  }, // 1
    { DI_EDIT,     32,   i,49, 0,(DWORD)m, DIF_HISTORY|DIF_USELASTHISTORY, mark }, // 2
    { DI_TEXT,      5, ++i,30, 0,0,        0,          (char*)MSetMarkAdd  },
    { DI_EDIT,     32,   i,49, 0,(DWORD)a, DIF_HISTORY, add },
    { DI_TEXT,      5, ++i,30, 0,0,        0,          (char*)MSearchMarkReg},
    { DI_EDIT,     32,   i,49, 0,(DWORD)p, DIF_HISTORY|DIF_USELASTHISTORY, pattern },
    { DI_BUTTON,    0, (i+=2), 0, 0,0,        DIF_CENTERGROUP, (char*)MOK    }, // 3
    { DI_BUTTON,    0,      i, 0, 0,0,        DIF_CENTERGROUP, (char*)MCancel}  // 4
  };

  int dOk = sizeof(InitItems)/sizeof(InitItems[0]) - 2;
  int ret = ExecDialog(EXARR(InitItems),2,dOk,"SelectCurrent");

  if (ret == dOk)
  {
    mark[sizeof(mark)-1] = 0;
    SetRegKey(HKEY_CURRENT_USER,"", m, mark);
    add[sizeof(add)-1] = 0;
    SetRegKey(HKEY_CURRENT_USER,"", a, add);
    pattern[sizeof(pattern)-1] = 0;
    SetRegKey(HKEY_CURRENT_USER,"", p, pattern);
  }

  return ret;
}

static int AddHistoryLine()
{
  static char *m = MARK;
  static char *a = MARKADD;
  static char *g = PROGINIT;
  static char *t = "Reason";
  static char *p = MARKPATTERN;

  char mark[256],proginit[256],reason[256],add[256],pattern[256];

  GetRegKey(HKEY_CURRENT_USER,"", m, mark,    (char*)GetMsg(MDefaultMark), sizeof(mark));
  GetRegKey(HKEY_CURRENT_USER,"", p, pattern, (char*)GetMsg(MDefaultPattern), sizeof(pattern));
  GetRegKey(HKEY_CURRENT_USER,"", a, add,      "", sizeof(add));
  GetRegKey(HKEY_CURRENT_USER,"", g, proginit,(char*)GetMsg(MDefaultProgInit), sizeof(proginit));
  GetRegKey(HKEY_CURRENT_USER,"", t, reason,   "", sizeof(reason ));

  int i=0;

  struct InitDialogItem InitItems[] =
  {
    { DI_DOUBLEBOX, 3,++i,51,10,0,        0,          (char*)MTitle },
    { DI_TEXT,      5,++i,30, 0,0,        0,          (char*)MSetProgInit},
    { DI_EDIT,     32,  i,49, 0,(DWORD)g, DIF_HISTORY|DIF_USELASTHISTORY, proginit },
    { DI_TEXT,      5,++i,30, 0,0,        0,          (char*)MSetMark },
    { DI_EDIT,     32,  i,49, 0,(DWORD)m, DIF_HISTORY|DIF_USELASTHISTORY, mark      },
    { DI_TEXT,      5,++i,30, 0,0,        0,          (char*)MSetMarkAdd  },
    { DI_EDIT,     32,  i,49, 0,(DWORD)a, DIF_HISTORY, add },
    { DI_TEXT,      5,++i,30, 0,0,        0,          (char*)MSetReason  },
    { DI_EDIT,      5,++i,49, 0,(DWORD)t, DIF_HISTORY|DIF_USELASTHISTORY, reason        },
    { DI_TEXT,      5,++i,30, 0,0,        0,          (char*)MSearchMarkReg},
    { DI_EDIT,     32,  i,49, 0,(DWORD)p, DIF_HISTORY|DIF_USELASTHISTORY, pattern },
    { DI_BUTTON,    0,(i+=2), 0, 0,0,        DIF_CENTERGROUP, (char*)MOK    },
    { DI_BUTTON,    0,     i, 0, 0,0,        DIF_CENTERGROUP, (char*)MCancel}
  };

  int dOk = sizeof(InitItems)/sizeof(InitItems[0]) - 2;
  int ret = ExecDialog(EXARR(InitItems),3,dOk,"AddHistoryLine");

  if (ret == dOk)
  {
    mark[sizeof(mark)-1] = 0;
    SetRegKey(HKEY_CURRENT_USER,"", m, mark);
    proginit[sizeof(proginit)-1] = 0;
    SetRegKey(HKEY_CURRENT_USER,"", g, proginit);
    reason[sizeof(reason)-1] = 0;
    SetRegKey(HKEY_CURRENT_USER,"", t, reason);
    add[sizeof(add)-1] = 0;
    SetRegKey(HKEY_CURRENT_USER,"", a, add);
    pattern[sizeof(pattern)-1] = 0;
    SetRegKey(HKEY_CURRENT_USER,"", p, pattern);

    // Insert into editor
    static char dateStr[13];
    static char historyLine[256];
    struct tm *time_now;
    time_t secs_now;
    tzset();
    time(&secs_now);
    time_now = localtime(&secs_now);
    strftime(dateStr, 13, GetMsg(MFmtDate), time_now);

    strupr(dateStr);

    int offset = atoi(GetMsg(MFmtMarkOffset));

    FSF.sprintf( historyLine, GetMsg(MFmtHistory),
      Opt.HeaderComment,
      dateStr,
      proginit,
      mark+offset,
      reason);

    EditorInsertLine( historyLine );
  }

  return ret;
}

void PrintMark( char * changeMark, int Msg)
{
    static char *m = MARK;
    static char *a = MARKADD;

    char mark[256], add[60];
    char addInfo[80] = "";

    GetRegKey(HKEY_CURRENT_USER,"", m, mark, (char*)GetMsg(MDefaultMark), 250);
    GetRegKey(HKEY_CURRENT_USER,"", a, add, "", 60);

    if (strlen(add)>0)
    {
        FSF.sprintf( addInfo, Opt.CloseMarkAdd, add);
    }
    else
    {
        strcpy( addInfo, Opt.CloseMark);
    }

    FSF.sprintf( changeMark, "%s%s%s %s%s%s",
              Opt.OpenComment,
              Opt.OpenMark,
              mark,
              GetMsg(Msg),
              addInfo,
              Opt.CloseComment);
}

static int DeleteBlock()
{
  char   changeMark[256];
  struct EditorSelect      sel;

  // Delete block
  EditorProcessVKey('D', LEFT_CTRL_PRESSED);
  TEditorPos p = EditorGetPos();
  PrintMark( changeMark, MMarkDelete);
  EditorInsertLine( changeMark, p.Row);

  sel.BlockStartLine = p.Row;
  sel.BlockStartPos  = 0;
  sel.BlockType      = BTYPE_STREAM;
  sel.BlockHeight    = 1;
  sel.BlockWidth     = -1;
  Info.EditorControl(ECTL_SELECT,  &sel);

  return 0;
}

static int MarkBlock(bool debugMark)
{
  char   changeMark[256];
  struct EditorInfo ei;
  struct EditorGetString   egs;
  struct EditorSelect      sel;

  // Save start/end lines of block
  Info.EditorControl(ECTL_GETINFO,&ei);

  int startLine, endLine;
  startLine = ei.CurLine;
  // Nothing selected?
  if (ei.BlockType==BTYPE_STREAM)
  {
    startLine = endLine = ei.BlockStartLine;

    do
    {
      egs.StringNumber = endLine;

      // If can't get line
      if (!Info.EditorControl(ECTL_GETSTRING,&egs))
        break;

      if (egs.SelEnd>0)
      {
        endLine += 2;
        break;
      }

      ++endLine;
    } while (egs.SelEnd<0);

  }
  else  // Find block last line
  {
    egs.StringNumber = startLine;
    Info.EditorControl(ECTL_GETSTRING,&egs);
    endLine = startLine+1;
  }

  if (!debugMark)
  {
    PrintMark(changeMark, MMarkBegin);
  }
  else
  {
    FSF.sprintf( changeMark, GetMsg(MDebugBegin),Opt.OpenComment, Opt.CloseComment);
  }
  EditorInsertLine(changeMark, startLine);

  if (!debugMark)
  {
    PrintMark(changeMark, MMarkEnd);
  }
  else
  {
    FSF.sprintf( changeMark, GetMsg(MDebugEnd),Opt.OpenComment, Opt.CloseComment);
  }

  EditorInsertLine(changeMark, endLine);

  sel.BlockStartLine = startLine;
  sel.BlockStartPos  = 0;
  sel.BlockType      = BTYPE_STREAM;
  sel.BlockHeight    = endLine - startLine + 2;
  sel.BlockWidth     = 0;
  Info.EditorControl(ECTL_SELECT,  &sel);

  return 0;
}


static int CommentBlock( bool pbDo)
{
  static char *m = MARK;
  char currentMark[256], newLine[256];
  struct EditorInfo ei;
  struct EditorGetString   egs;
  struct EditorSetString   ess;
  struct EditorConvertText ect;
  TEditorPos p = EditorGetPos();

  // Save start/end lines of block
  Info.EditorControl(ECTL_GETINFO,&ei);
  GetRegKey(HKEY_CURRENT_USER,"", m, currentMark, "", 6);

  int startLine, endLine;

  egs.StringNumber = ei.BlockStartLine;

  for (;;)
  {
    char * foundStr;
    int  liStrlen;

    // If can't get line
    if (!Info.EditorControl(ECTL_GETSTRING,&egs)) break;
    if (egs.SelStart<=0 && egs.SelEnd ==0) break;

    if (pbDo)
    {
      liStrlen = egs.StringLength+strlen(Opt.LeadingComment);
      foundStr = winNew(char, liStrlen+1);
      if (!foundStr) return 0;
      strcpy( foundStr, Opt.LeadingComment);
      strncat(foundStr, egs.StringText, egs.StringLength);
    }
    else
    {
      if (strncmp(egs.StringText,Opt.LeadingComment,
                  strlen(Opt.LeadingComment))!=0)
      {
        const char *MsgItems[]={GetMsg(MError),GetMsg(MErrNoComment),GetMsg(MOK)};
        Info.Message(Info.ModuleNumber,FMSG_WARNING,NULL,EXARR(MsgItems),1);

        TEditorPos p = EditorGetPos();
        p.Row = egs.StringNumber;
        p.Col = 0;
        EditorSetPos(p);

        return 0;
      }

      liStrlen = egs.StringLength-strlen(Opt.LeadingComment);
      foundStr = winNew(char, liStrlen+1);
      if (!foundStr) return 0;
      strncpy(foundStr, egs.StringText+strlen(Opt.LeadingComment), liStrlen);
    }

    const char *MsgItems[]={GetMsg(MTitle),GetMsg(MWorking)};
    Info.Message(Info.ModuleNumber,FMSG_WARNING,NULL,EXARR(MsgItems),0);

    foundStr[liStrlen] = 0;
    ess.StringNumber = egs.StringNumber;
    ess.StringText   = foundStr;
    ess.StringEOL    = const_cast<char*>(egs.StringEOL);
    ess.StringLength = liStrlen;
    Info.EditorControl(ECTL_SETSTRING,&ess);

    winDel( foundStr);
    egs.StringNumber++;
  }

  return 0;
}

static int LineTooLong( void )
{
  char   desc[256], data[512], top[256],
         bottom[256], pattern[512], sir[256];
  struct EditorGetString gs;
  struct EditorConvertText ect;
  struct EditorInfo ei;
  int    retVal = 0;

  ignoreToday = false;
  Opt.SaveIgnoreLongLines(0);

  Info.EditorControl(ECTL_GETINFO, &ei);
  TEditorPos p, oldPos = EditorGetPos();

  collection strList(10, 5);
  regErrorShown = FALSE;
  int lines = ei.TotalLines;
  int step  = lines/100;
  HANDLE hScreen;
  if (step > 10)
  {
    hScreen = Info.SaveScreen(0,0,-1,-1);
  }

  // Scan the file line by line
  for ( p.Row = 0 ; p.Row < lines ; p.Row++ )
  {
    // Sometimes check for Esc, write progress info
    if (( step > 10 ) && ( p.Row % step == 0 ))
    {
      if (CheckForEsc())
      {
        break;
      }
      char InfoLine[80], FoundLine[80];

      if (strList.getCount())
      {
        FSF.sprintf(FoundLine,GetMsg(MFound),strList.getCount());
      }
      else
      {
        *FoundLine = 0;
      }
      FSF.sprintf(InfoLine,GetMsg(MComplete),100*p.Row/lines);
      const char *MsgItems[]={GetMsg(MTitle),InfoLine, FoundLine};
      Info.Message(Info.ModuleNumber,0,NULL,EXARR(MsgItems),0);
    }

    EditorSetPos(p);       // Set current pos here
    EditorGetString(&gs);  // Get one string from the editor

    // If there is a string
    if (gs.StringLength >Opt.MaxLineLen)
    {
      strList.insert(p.Row, gs.StringLength);
    }
  }

  // Restore screen
  if (step > 10)
  {
    Info.RestoreScreen(hScreen);
  }
  EditorSetPos(oldPos);

  // Display results
  size_t count = strList.getCount();

  strcpy(top,GetMsg(MFoundLongLines));

  // Analyze the result and create menu with results
  if (count)
  {
    struct FarMenuItem *menuItems = winNew(FarMenuItem, count);
    if (menuItems) // Memory allocated
    {
      int  itemsCount = 0, beginCount = 0;
      char nestStr[256];
      int  errId, errCandidate;
      char stopper[10], starter[10];

      for ( size_t i = 0 ; i < count ; i++ )
      {
        TEInfo *te = strList.at(i);
        char foundStr[256];

        p.Row = te->line;
        EditorSetPos(p);       // Set current pos here
        EditorGetString(&gs);  // Get one string from the editor

        int  exLen = gs.StringLength - Opt.MaxLineLen;
        strncpy( foundStr, gs.StringText+Opt.MaxLineLen, exLen);
        foundStr[exLen] = 0;

        FSF.sprintf( menuItems[itemsCount].Text, "%6d³%.66s",
                  te->line+1, foundStr);

        menuItems[itemsCount].Selected  = FALSE;
        menuItems[itemsCount].Checked   = FALSE;
        menuItems[itemsCount].Separator = FALSE;

        itemsCount++;
      }

      // Set bottom message
      FSF.sprintf(bottom,GetMsg(MTotal),itemsCount);

      int res = Info.Menu(Info.ModuleNumber,-1,-1,0,FMENU_SHOWAMPERSAND,
                top,bottom,"List",NULL,NULL,menuItems,itemsCount);

      if (res != -1)
      {
        p.Default();
        p.Row = atoi(menuItems[res].Text)-1;
        p.Col = Opt.MaxLineLen;
        EditorSetPos(p);
        retVal = 1;
      }
      else
      {
        EditorSetPos(oldPos);
      }
      winDel(menuItems);
    }
  }
  else
  {
    const char *MsgItems[]={top,GetMsg(MNoErrorsFound),GetMsg(MOK)};
    Info.Message(Info.ModuleNumber,0,"List",EXARR(MsgItems),1);
  }

  return retVal;
}

static int FindPair( )
{
  struct EditorGetString gs;
  struct EditorConvertText ect;
  struct EditorInfo ei;
  char   foundMark[16], foundState[16];

  EditorGetString(&gs);  // Get one string from the editor

  // If there is a string
  if (gs.StringLength == 0)
  {
    const char *MsgItems[]={GetMsg(MError),GetMsg(MErrNoMark),GetMsg(MOK)};
    Info.Message(Info.ModuleNumber,FMSG_WARNING,NULL,EXARR(MsgItems),1);
    return 0;
  }

  // Make copy of the string
  char * foundStr = winNew(char, gs.StringLength+1);
  if (!foundStr)
  {
    return 0;
  }

  memcpy(foundStr, gs.StringText, gs.StringLength);
  foundStr[gs.StringLength] = 0;

  // Convert to OEM
  ect.Text       = foundStr;
  ect.TextLength = gs.StringLength;
  Info.EditorControl(ECTL_EDITORTOOEM, &ect);

  char currentMark[256];
  char currentPattern[256];
  char pattern[256];
  BuildPatternMark(pattern, false, currentMark, currentPattern);

  static CRegExp  reg;
  static SMatches m;
  if (!reg.SetExpr(pattern))
  {
    winDel( foundStr);
    return 0;
  }

  if (!reg.Parse(foundStr, &m))
  {
    const char *MsgItems[]={GetMsg(MError),GetMsg(MErrNoMark),GetMsg(MOK)};
    Info.Message(Info.ModuleNumber,FMSG_WARNING,NULL,EXARR(MsgItems),1);
    winDel( foundStr);
    return 0;
  }

  // Get number and sir mark
  size_t resLen;
  resLen = (size_t)(m.e[1]-m.s[1]);
  if (resLen)
  {
    strncpy(foundMark, foundStr+m.s[1], resLen);
    foundMark[resLen] = 0;
  }
  resLen = (size_t)(m.e[2]-m.s[2]);
  if (resLen)
  {
    strncpy(foundState, foundStr+m.s[2], resLen);
    foundState[resLen] = 0;
  }

  if (strcmp(foundState,GetMsg(MMarkDelete))==0)
  {
      const char *MsgItems[]={GetMsg(MError),
                         GetMsg(MErrNoPairForDelete),
                         GetMsg(MOK)};
      Info.Message(Info.ModuleNumber,FMSG_WARNING,NULL,EXARR(MsgItems),1);
      winDel( foundStr);
      return 0;
  }

  winDel( foundStr);

  int direction;
  FSF.sprintf( pattern , "/%s ", currentMark);
  if (strcmp(foundState,GetMsg(MMarkBegin))==0)
  {
    direction=1;
    strcat( pattern, GetMsg(MMarkEnd));
  }
  else
  {
    direction=-1;
    strcat( pattern, GetMsg(MMarkBegin));
  }
  strcat( pattern, "/");

  if (!reg.SetExpr(pattern))
  {
    return 0;
  }

  Info.EditorControl(ECTL_GETINFO, &ei);
  TEditorPos p = EditorGetPos();

  BOOL found = FALSE;

  while (p.Row>0 && p.Row<ei.TotalLines && !found)
  {
    p.Row += direction;
    EditorGetString(&gs, p.Row);  // Get one string from the editor
    foundStr = winNew(char, gs.StringLength+1);
    if (!foundStr)
    {
      return 0;
    }
    memcpy(foundStr, gs.StringText, gs.StringLength);
    foundStr[gs.StringLength] = 0;

    if (reg.Parse(foundStr, &m))
    {
        found = TRUE;
    }

    winDel( foundStr);
  }

  if (found)
  {
    p.Col = 0;
    EditorSetPos(p);
  }
  else
  {
    const char *MsgItems[]={GetMsg(MError),GetMsg(MErrNoPair),GetMsg(MOK)};
    Info.Message(Info.ModuleNumber,FMSG_WARNING,NULL,EXARR(MsgItems),1);
  }

  return 0;
}

static int ListMarks( int showAll, int onlyErrors)
{
  char   desc[256];
  char   data[512];
  char   top[256];
  char   bottom[256];
  char   pattern[512];
  char   currentMark[256];
  char   currentPattern[256];

  struct EditorGetString gs;
  struct EditorConvertText ect;
  struct EditorInfo ei;
  char   foundMark[16], foundState[16];

  int retVal = 0;

  Info.EditorControl(ECTL_GETINFO, &ei);
  TEditorPos p, oldPos = EditorGetPos();
  BuildPatternMark(pattern,showAll,currentMark, currentPattern);

  // Set header message
  if (showAll)
  {
    FSF.sprintf(top,GetMsg(MSelect),GetMsg(MAll));
  }
  else
  {
    FSF.sprintf(top,GetMsg(MSelect),currentMark);
  }

  if (!showAll && strlen(currentMark)==0)
  {
    const char *MsgItems[]={top, GetMsg(MErrCurrentMarkSet), GetMsg(MOK)};
    Info.Message(Info.ModuleNumber,FMSG_WARNING,"List",EXARR(MsgItems),1);
    return retVal;
  }

  collection strList(10, 5);
  regErrorShown = FALSE;
  int lines = ei.TotalLines;
  int step  = lines/100;
  HANDLE hScreen;
  if (step > 10)
  {
    hScreen = Info.SaveScreen(0,0,-1,-1);
  }

  // Scan the file line by line
  for ( p.Row = 0 ; p.Row < lines ; p.Row++ )
  {
    // Sometimes check for Esc, write progress info
    if (( step > 10 ) && ( p.Row % step == 0 ))
    {
      if (CheckForEsc())
      {
        break;
      }
      char InfoLine[80], FoundLine[80];

      if (strList.getCount())
      {
        FSF.sprintf(FoundLine,GetMsg(MFound),strList.getCount());
      }
      else
      {
        *FoundLine = 0;
      }
      FSF.sprintf(InfoLine,GetMsg(MComplete),100*p.Row/lines);
      const char *MsgItems[]={GetMsg(MTitle),InfoLine, FoundLine};
      Info.Message(Info.ModuleNumber,0,NULL,EXARR(MsgItems),0);
    }

    EditorSetPos(p);       // Set current pos here
    EditorGetString(&gs);  // Get one string from the editor

    // If there is a string
    if (gs.StringLength)
    {
      // Make copy of the string
      char * foundStr = winNew(char, gs.StringLength+1);
      if (foundStr)
      {
        memcpy(foundStr, gs.StringText, gs.StringLength);
        foundStr[gs.StringLength] = 0;

        // Convert to OEM
        ect.Text       = foundStr;
        ect.TextLength = gs.StringLength;
        Info.EditorControl(ECTL_EDITORTOOEM, &ect);

        static CRegExp reg;
        SMatches m;
        if (reg.SetExpr(pattern))
        {
          if (reg.Parse(foundStr, &m))
          {
            size_t resLen;
            resLen = (size_t)(m.e[1]-m.s[1]);
            if (resLen)
            {
              strncpy(foundMark, foundStr+m.s[1], resLen);
              foundMark[resLen] = 0;
            }
            resLen = (size_t)(m.e[2]-m.s[2]);
            if (resLen)
            {
              strncpy(foundState, foundStr+m.s[2], resLen);
              foundState[resLen] = 0;
            }

            // Insert new record into collection
            strList.insert(p.Row, 0, foundMark, foundState);
          }
        }
        else if (!regErrorShown)
        {
          const char *MsgItems[]={GetMsg(MError),GetMsg(MRegError),
                            pattern,GetMsg(MOK)};
          Info.Message(Info.ModuleNumber,FMSG_WARNING,NULL,EXARR(MsgItems),1);
          regErrorShown = TRUE;
        }
        winDel(foundStr);
      }
    }
  }

  // Restore screen
  if (step > 10)
  {
    Info.RestoreScreen(hScreen);
  }
  EditorSetPos(oldPos);

  // Display results
  size_t count = strList.getCount();

  // Analyze the result and create menu with results
  if (count)
  {
    struct FarMenuItem *menuItems = winNew(FarMenuItem, count);
    if (menuItems) // Memory allocated
    {
      int  itemsCount = 0, beginCount = 0;
      char nestStr[256];
      int  errId, errCandidate;
      char stopper[10], starter[10];

      for ( size_t i = 0 ; i < count ; i++ )
      {
        TEInfo *tte, *te = strList.at(i);

        if ( strcmp(te->state, GetMsg(MMarkBegin)) == 0)
        {
          beginCount++;
        }

        errId = MOK;

        if (strcmp(te->state, GetMsg(MMarkBegin))  == 0)
        {
            strcpy( stopper, GetMsg(MMarkBegin));
            *starter = 0;
            errCandidate = MErrAlreadyOpened;
        }
        else if ( strstr(te->state, GetMsg(MMarkDelete)) == te->state)
        {
            strcpy( stopper, GetMsg(MMarkBegin));
            *starter = 0;
            errCandidate = MErrDeleteInside;
        }
        else if (strcmp(te->state, GetMsg(MMarkEnd)) == 0)
        {
            strcpy( stopper, GetMsg(MMarkEnd));
            strcpy( starter, GetMsg(MMarkBegin));
            errCandidate = MErrClosedTwice;
        }
        else
        {
            InfoMsg(true, GetMsg(MTitle), GetMsg(MOK), "Unknown mark: [%s]", te->state);
        }

        // Look backward for wrong pairs
        for (int j = i-1 ; j >= 0 && i>0  ; j-- )
        {
          tte = strList.at(j);

          if (!strcmp(tte->mark,te->mark))
          {
              if (strcmp(tte->state, stopper) == 0)
              {
                  errId = errCandidate;
              }
              else if (*starter != 0 && strcmp( tte->state , starter) != 0)
              {
                  errId = MErrNotOpened;
              }
              break;
          }
        }

        if (beginCount>0)
        {
          memset(&nestStr,' ',sizeof(nestStr));

          if (strcmp(te->state, GetMsg(MMarkDelete)) == 0)
          {
              nestStr[beginCount] = 0;
          }
          else
          {
              nestStr[beginCount-1] = 0;
          }
        }
        else
        {
          *nestStr = 0;
        }

        if (strcmp(te->state, GetMsg(MMarkEnd)) == 0)
        {
          beginCount--;

          // Look back for wrong nesting B1 B2 E1 E2
          if (i>0) // There is something before
          {
              tte = strList.at(i-1);
              if (strcmp(tte->state,GetMsg(MMarkBegin)) == 0 &&
                  strcmp(tte->mark, te->mark))
              {
                errId = MErrNesting;
              }
          }
        }

        if (strcmp(te->state, GetMsg(MMarkBegin)) == 0)
        {
          if (i>0) // There is something before
          {
              tte = strList.at(i-1);
              if (strcmp(tte->state,GetMsg(MMarkEnd)) == 0 &&
                  !strcmp(tte->mark,te->mark) && tte->line == te->line-1)
              {
                errId = MErrContinuous;
              }
          }
        }

        if (onlyErrors && errId == MOK)
        {
            continue;
        }

        char foundStr[256];
        FSF.sprintf( foundStr, "%s%s", nestStr, te->state);
        FSF.sprintf( menuItems[itemsCount].Text, "%6d³%-10s³%-25s³%s",
                  te->line+1, te->mark, foundStr, GetMsg(errId));

        menuItems[itemsCount].Selected  = FALSE;
        menuItems[itemsCount].Checked   = FALSE;
        menuItems[itemsCount].Separator = FALSE;

        itemsCount++;
      }

      // Set bottom message
      FSF.sprintf(bottom,GetMsg(MTotal),itemsCount);

      int res = Info.Menu(Info.ModuleNumber,-1,-1,0,FMENU_SHOWAMPERSAND,
                top,bottom,"List",NULL,NULL,menuItems,itemsCount);

      if (res != -1)
      {
        p.Default();
        p.Row = atoi(menuItems[res].Text)-1;
        p.Col = 0;
        EditorSetPos(p);
        retVal = 1;
      }
      winDel(menuItems);
    }
  }
  else
  {
    const char *MsgItems[]={top, GetMsg(MNoMarkFound), GetMsg(MOK)};
    Info.Message(Info.ModuleNumber,FMSG_WARNING,"List",EXARR(MsgItems),1);
  }

  return retVal;
}

static int MainMenu(int & piDefault)
{
  int countMenu = 13;
  struct FarMenuItem *mainMenu = winNew(FarMenuItem, countMenu);

  if (!mainMenu) return -1;

  for ( int i = 0 ; i < countMenu ; i++ )
  {
    mainMenu[i].Selected = mainMenu[i].Checked = mainMenu[i].Separator = FALSE;
  }

  int c = 0;

  strcpy( mainMenu[c++].Text, GetMsg(MCommentBlock));
  strcpy( mainMenu[c++].Text, GetMsg(MUncommentBlock));
  strcpy( mainMenu[c++].Text, GetMsg(MMarkDebugBlock));
  strcpy( mainMenu[c++].Text, GetMsg(MFindLongLines));

          mainMenu[c  ].Separator = TRUE;
  strcpy( mainMenu[c++].Text, "");
  strcpy( mainMenu[c++].Text, GetMsg(MSelectCurr));
  strcpy( mainMenu[c++].Text, GetMsg(MAddHistory));
  strcpy( mainMenu[c++].Text, GetMsg(MDeleteBlk));
  strcpy( mainMenu[c++].Text, GetMsg(MMarkBlock));
  strcpy( mainMenu[c++].Text, GetMsg(MFindPair));
  strcpy( mainMenu[c++].Text, GetMsg(MListCurr));
  strcpy( mainMenu[c++].Text, GetMsg(MListErrors));
  strcpy( mainMenu[c++].Text, GetMsg(MListAll));

  mainMenu[piDefault].Selected = TRUE;

  piDefault = Info.Menu(Info.ModuleNumber,-1,-1,0,FMENU_WRAPMODE,
                        GetMsg(MMainMenu),NULL,"Contents",
                        NULL,NULL,mainMenu,countMenu);

  winDel(mainMenu);
  return piDefault;
}

void EditorMenu()
{
  bool lbAgain;
  int  liDefault = 0;

  do
  {
    lbAgain = false;

    Opt.GetComments(); // Get the actual parameters

    switch ( MainMenu(liDefault))
    {
      case 0:
        if (IsBlock())
        {
          CommentBlock(true);
        }
        break;
      case 1:
        if (IsBlock())
        {
          CommentBlock(false);
        }
        break;
      case 2:
        MarkBlock(true);
        break;
      case 3:
        LineTooLong();
        break;
      case 5:
        SelectCurrent();
        lbAgain = true;
        break;
      case 6:
        AddHistoryLine();
        break;
      case 7:
        if (IsBlock())
        {
          DeleteBlock();
        }
        break;
      case 8:
        MarkBlock(false);
        break;
      case 9:
        FindPair();
        break;
      case 10:
        if (! ListMarks(0,0))
        {
          lbAgain = true;
        }
        break;
      case 11:
        if (! ListMarks(1,1))
        {
          lbAgain = true;
        }
        break;
      case 12:
        if (! ListMarks(1,0))
        {
          lbAgain = true;
        }
        break;
      default:
        break;
    }

  } while (lbAgain);
}

int GetUser(char * user)
{
    static char * u = "CDUser";

    struct InitDialogItem InitItems[] =
    {
    /*00*/ { DI_DOUBLEBOX, 3,1,34,  5,        0,               0, (char*)MCDTitle },
    /*03*/ { DI_TEXT,      5,2,14,  0,        0,               0, (char*)MCDUserName },
    /*04*/ { DI_EDIT,     15,2,27,  0,(DWORD) u, DIF_HISTORY|
                                                 DIF_USELASTHISTORY, user         },
    /*05*/ { DI_TEXT,      5,3, 0,255,        0,                  NULL            },
    /*06*/ { DI_BUTTON,    0,4, 0,  0,        0, DIF_CENTERGROUP, (char*)MOK      },
    /*07*/ //!!{ DI_BUTTON,    0,4, 0,  0,        0, DIF_CENTERGROUP, (char*)MChange  },
    /*07*/ { DI_BUTTON,    0,4, 0,  0,        0, DIF_CENTERGROUP, (char*)MCancel  }
    };

    int size = sizeof(InitItems)/sizeof(InitItems[0]);
    int def  = size - 3;
    char * helpTopic = NULL;

    struct FarDialogItem * Item = winNew(struct FarDialogItem, size);
    InitDialogItems(InitItems, Item, size);
    Item[2].Focus = Item[def].DefaultButton = 1;
    int Result = Info.Dialog(Info.ModuleNumber,-1,-1,Item[0].X2+4,Item[0].Y2+2,helpTopic,Item,size);
    if ( Result == def || Result == def + 1 )
    {
        SaveDialogItems(Item, InitItems, size);
        FSF.LUpperBuf(user, strlen(user));
    }
    winDel(Item);

    return (Result - def);
}

BOOL EditLevel( char* desc, char* dir, char* fmtuser, char* fmtglobal)
{
    static char * d = "CTools\\Desc";
    static char * r = "CTools\\Dir";
    static char * u = "CTools\\FmtUser";
    static char * g = "CTools\\FmtGlobal";

    struct InitDialogItem InitItems[] =
    {
        { DI_DOUBLEBOX, 3,1,50,  8,        0,               0, (char*)MCDLevelEdit },
        { DI_TEXT,      5,2,20,  0,        0,               0, (char*)MCDDesc },
        { DI_EDIT,     21,2,47,  0,(DWORD) d, DIF_HISTORY,  desc         },
        { DI_TEXT,      5,3,20,  0,        0,               0, (char*)MCDDir },
        { DI_EDIT,     21,3,47,  0,(DWORD) r, DIF_HISTORY,  dir},
        { DI_TEXT,      5,4,20,  0,        0,               0, (char*)MCDFmtUser },
        { DI_EDIT,     21,4,47,  0,(DWORD) u, DIF_HISTORY,  fmtuser         },
        { DI_TEXT,      5,5,20,  0,        0,               0, (char*)MCDFmtGlobal },
        { DI_EDIT,     21,5,47,  0,(DWORD) g, DIF_HISTORY,  fmtglobal},

        { DI_TEXT,      5,6, 0,255,        0,                  NULL            },
        { DI_BUTTON,    0,7, 0,  0,        0, DIF_CENTERGROUP, (char*)MOK      },
        { DI_BUTTON,    0,7, 0,  0,        0, DIF_CENTERGROUP, (char*)MCancel  }
    };

    int dOk       = sizeof(InitItems)/sizeof(InitItems[0]) - 2;
    int dExitCode = ExecDialog(EXARR(InitItems),2,dOk, NULL);

    return (dExitCode == dOk);
}

void PanelMenu()
{
    FarMenuItem *menu = NULL;
    int Code, n = -1;
    int BreakKeys[6] = { VK_INSERT, VK_DELETE, VK_F3, VK_F4, VK_RETURN, 0 };
    char key[256];
    char last[80];
    char deffmtuser[80];
    char deffmtglobal[80];

    GetRegKey(HKEY_CURRENT_USER,"Levels", "Format user",   deffmtuser,   "cd %s:[PND_ENV.%s.%s]",     80);
    GetRegKey(HKEY_CURRENT_USER,"Levels", "Format global", deffmtglobal, "cd %s:[PND_ENV.%s.GLOBAL]", 80);

    do
    {
        GetRegKey(HKEY_CURRENT_USER,"Levels", "Last", last, "", 80);

        int Count = MenuFromRegKey(HKEY_CURRENT_USER, "Levels",
                                   AddMask, &menu, &n, last);

        if (n >= 0 && n < Count) menu[n].Selected = true;

        n = Info.Menu(Info.ModuleNumber, -1, -1, 0, FMENU_WRAPMODE,
                      GetMsg(MCDTitle), GetMsg(MCDBottom),
                      "ChangeDir", BreakKeys, &Code, menu, Count);

        if (Code == -1 && n>-1 && Count>0) Code =  4; // Highlight is like enter

        if (n >= 0 && n < Count)
        {
            char desc[255];
            strcpy( desc, menu[n].Text);

            FSF.sprintf(key, "Levels\\%s", desc);
            SetRegKey(HKEY_CURRENT_USER,"Levels", "Last", desc);

            char fmtuser[80];
            char fmtglobal[80];
            char dir[80];
            char cd[80]   = "";
            char user[80] = "";

            if (Code!=0)
            {
                GetRegKey(HKEY_CURRENT_USER,key, "Dir",    dir, "", 80);
                GetRegKey(HKEY_CURRENT_USER,key, "Format user",   fmtuser,  deffmtuser ,  80);
                GetRegKey(HKEY_CURRENT_USER,key, "Format global", fmtglobal,deffmtglobal, 80);
            }

            switch ( Code )
            {
                case 0: // Insert
                    *desc =0;
                    *dir  =0;
                    strcpy(fmtuser,deffmtuser);
                    strcpy(fmtglobal,deffmtglobal);

                    if (EditLevel(desc,dir,fmtuser,fmtglobal))
                    {
                        FSF.sprintf(key, "Levels\\%s", desc);

                        SetRegKey(HKEY_CURRENT_USER,key, "Dir",    dir);
                        SetRegKey(HKEY_CURRENT_USER,key, "Format user", fmtuser);
                        SetRegKey(HKEY_CURRENT_USER,key, "Format global", fmtglobal);
                    }
                    break;
                case 1: // Delete
                    DeleteRegKey(HKEY_CURRENT_USER, key);
                    break;

                case 2: // View global
                    FSF.sprintf( cd, fmtglobal, dir);
                    break;
                case 3: // Edit
                    if (EditLevel(desc,dir,fmtuser,fmtglobal))
                    {
                        DeleteRegKey(HKEY_CURRENT_USER, key);

                        FSF.sprintf(key, "Levels\\%s", desc);

                        SetRegKey(HKEY_CURRENT_USER,key, "Dir",    dir);
                        SetRegKey(HKEY_CURRENT_USER,key, "Format user", fmtuser);
                        SetRegKey(HKEY_CURRENT_USER,key, "Format global", fmtglobal);
                    }
                    break;
                case 4: // Go to users
                    if (strcmp(fmtuser,"-")!=0)
                    {
                        if (GetUser(user) == 0)
                        {
                            FSF.sprintf( cd, fmtuser, user, user, dir);
                        }
                    }
                    else
                    {
                        FSF.sprintf( cd, fmtglobal, dir);
                    }
                    break;

                default:
                    n = -1;
            }

            if (strlen(cd)>0)
            {
                if (strcmp(cd,"-")==0)
                {
                    const char *MsgItems[]={GetMsg(MCDTitle),GetMsg(MCDNotAllowed),GetMsg(MOK)};
                    Info.Message(Info.ModuleNumber,FMSG_WARNING,NULL,EXARR(MsgItems),1);
                }
                else
                {
                    Info.Control(INVALID_HANDLE_VALUE,FCTL_SETCMDLINE,cd);
                    INPUT_RECORD ir;
                    ir.EventType = KEY_EVENT;
                    ir.Event.KeyEvent.bKeyDown = TRUE;
                    ir.Event.KeyEvent.wRepeatCount = 1;
                    ir.Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
                    ir.Event.KeyEvent.dwControlKeyState = 0;
                    DWORD written;
                    WriteConsoleInput(GetStdHandle(STD_INPUT_HANDLE),&ir,1,&written);

                    n = -1;
                }
            }
        }

        if (menu)
        {
            winDel(menu);
        }

        menu = NULL;
    } while ( n != -1 );
}

/*****************************************************************************/

HANDLE WINAPI _export OpenPlugin(int piOpenFrom, int)
{
    switch ( piOpenFrom)
    {
        case OPEN_PLUGINSMENU:
            PanelMenu();
            break;

        case OPEN_EDITOR:
            EditorMenu();
            break;
    }

    return INVALID_HANDLE_VALUE;
}

int WINAPI _export Configure(int ItemNumber)
{
  if (ItemNumber)
  {
    return FALSE;
  }
  return Opt.Customize(NULL);
}

void WINAPI _export ExitFAR()
{
//  MessageBox( NULL, "Stop", "Change Track", MB_OK);
}

int WINAPI _export ProcessEditorEvent(int Event, void *Param)
{
  switch ( Event )
  {
    case EE_READ:
      Opt.GetComments(); // Get the actual parameters
      if (Opt.ExpandTabs)
      {
          EditorSetParameter param1 = {ESPT_TABSIZE, Opt.TabSize};
          Info.EditorControl(ECTL_SETPARAM,&param1);
          EditorSetParameter param2 = {ESPT_EXPANDTABS, true};
          Info.EditorControl(ECTL_SETPARAM,&param2);
      }
      else
      {
          EditorSetParameter param1 = {ESPT_EXPANDTABS, false};
          Info.EditorControl(ECTL_SETPARAM,&param1);
      }
      return 0;

    case EE_SAVE:
      Opt.GetComments(); // Get the actual parameters
      if (Opt.TruncateSpaces) TruncateSpaces();
      return 0;

    default:
      return 0;
  }
}

int WINAPI _export ProcessEditorInput(const INPUT_RECORD *Rec)
{
  static BOOL Reenter = FALSE;

  if (Reenter ||
      !(Rec->EventType == KEY_EVENT &&
        Rec->Event.KeyEvent.bKeyDown &&
        (Rec->Event.KeyEvent.wVirtualKeyCode == VK_END )))
  {
    return FALSE;
  }

  Opt.GetComments();

  if (!Opt.TruncateSpaces) return FALSE;

  struct EditorGetString   egs;
  struct EditorSetString   ess;
  TEditorPos p = EditorGetPos();

  egs.StringNumber=p.Row;
  Info.EditorControl(ECTL_GETSTRING,&egs);

  int  i;

  for (i=egs.StringLength;i>0;i--)
  {
    char c = egs.StringText[i-1];
    if (c != ' ' && c != '\t')
    {
        break;
    }
  }

  if (i==egs.StringLength)
  {
    return FALSE;
  }

  ess.StringNumber = egs.StringNumber;
  ess.StringText   = const_cast<char*>(egs.StringText);
  ess.StringEOL    = const_cast<char*>(egs.StringEOL);
  ess.StringLength = i;

  Info.EditorControl(ECTL_SETSTRING,&ess);
  p.Col = i;
  EditorSetPos(p);

  Reenter=TRUE;
  Info.EditorControl(ECTL_PROCESSINPUT,(void*)Rec);
  Reenter=FALSE;

  return TRUE;
}

void WINAPI _export SetStartupInfo(const struct PluginStartupInfo *Info)
{
  ::Info     = *Info;
  ::FSF      = *Info->FSF;
  ::Info.FSF = &::FSF;

  FSF.sprintf(PluginRootKey,"%s\\CTools",Info->RootKey);
}

void WINAPI _export GetPluginInfo(struct PluginInfo *Info)
{
  static char *PluginMenuStrings[1];
  Info->StructSize = sizeof(*Info);
  Info->Flags = PF_EDITOR;
  Info->DiskMenuStringsNumber = 0;
  Info->PluginConfigStringsNumber = 0;
  PluginMenuStrings[0] = const_cast<char*>(GetMsg(MMenuString));
  Info->PluginMenuStrings = PluginMenuStrings;
  Info->PluginMenuStringsNumber = 1;
  static char *PluginCfgStrings[1];
  PluginCfgStrings[0]=const_cast<char*>(GetMsg(MConfigMenu));
  Info->PluginConfigStrings=PluginCfgStrings;
  Info->PluginConfigStringsNumber=
      sizeof(PluginCfgStrings)/sizeof(PluginCfgStrings[0]);
}
