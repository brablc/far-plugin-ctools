#ifndef __COLLECT_H
#define __COLLECT_H

#include <string.h>

struct TEInfo
{
  int  line, col;
  char mark[10];
  char state[10];
  TEInfo(int, int, char*, char*);
};

class collection
{
  public:
    collection(size_t, size_t);
    ~collection();
    TEInfo *at(size_t);
    size_t insert(int, int, char* mark=0, char* state=0);
    size_t insert(TEInfo*);
    size_t remove(size_t);
    virtual void setLimit(size_t);
    size_t getCount() { return count; }
  private:
    TEInfo **items;
    size_t count, limit, delta;
};

#endif
