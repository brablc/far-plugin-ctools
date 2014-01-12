#include <windows.h>
#include <winmem.h>
#include "collect.h"

#define MAXCOLLECTIONSIZE 0x7FFFFFFF

TEInfo::TEInfo(int aLine, int aCol, char* aMark, char* aState) :
  line(aLine),
  col(aCol)
{
  *state = 0;
  *mark = 0;
  if ( aMark ) strcpy(mark, aMark);
  if ( aState ) strcpy(state, aState);
}

collection::collection(size_t aLimit, size_t aDelta) :
  count(0),
  items(NULL),
  limit(0),
  delta(aDelta)
{
  setLimit(aLimit);
}

collection::~collection()
{
  for ( size_t i =  0; i < count; i++ )
    if ( items[i] )
      winDel(items[i]);
  count = 0;
  setLimit(0);
  if ( items )
    winDel(items);
}

TEInfo *collection::at(size_t index)
{
  if ( index >= count )
    return NULL;
  return items[index];
}

size_t collection::insert(int aLine, int aCol, char* aMark, char *aState)
{
  TEInfo *info = winNew(TEInfo, 1);
  TEInfo info2(aLine, aCol, aMark, aState);
  memcpy(info, &info2, sizeof(TEInfo));
  return insert(info);
}

size_t collection::insert(TEInfo *item)
{
  if ( count == limit )
    setLimit(count+delta);
  items[count++] = item;
  return count;
}

size_t collection::remove(size_t index)
{
  if ( index < limit && count != 0 )
  {
    if ( items[index] )
      winDel(items[index]);
    memcpy(items+index, items+index+1, (--count-index)*sizeof(TEInfo*));
  }
  return count;
}

void collection::setLimit(size_t aLimit)
{
  if ( aLimit < count )
    aLimit =  count;
  if ( aLimit > MAXCOLLECTIONSIZE)
    aLimit = MAXCOLLECTIONSIZE;
  if ( aLimit != limit )
  {
    TEInfo **aItems;
    if ( aLimit == 0 )
      aItems = NULL;
    else
    {
      aItems = winNew(TEInfo*, aLimit);
      if ( !aItems )
        return;
      if ( count != 0 )
        memcpy(aItems, items, count*sizeof(TEInfo*));
    }
    winDel(items);
    items =  aItems;
    limit =  aLimit;
  }
}
