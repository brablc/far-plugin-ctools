#ifndef __CTOOLS_H
#define __CTOOLS_H

extern "C"
{
    void   WINAPI _export SetStartupInfo(const struct PluginStartupInfo *Info);
    HANDLE WINAPI _export OpenPlugin(int OpenFrom,int Item);
    void   WINAPI _export GetPluginInfo(struct PluginInfo *Info);
    int    WINAPI _export Configure(int ItemNumber);
    void   WINAPI _export ExitFAR(void);
    int    WINAPI _export ProcessEditorInput(const INPUT_RECORD *Rec);
};

#endif
