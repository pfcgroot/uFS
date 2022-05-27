#ifndef __uFS_fact_heap_h
#define __uFS_fact_heap_h

#include "uFS.h"

///////////////////////////////////////////////////////////////////////////////
// DeviceIoDriverFactory_Heap
//
// A simle driver factory that allocates new driver instances on the heap.

class DeviceIoDriverFactory_Heap : public DeviceIoDriverFactory
{
public:
	virtual DeviceIoDriver* AllocateDriver(const char* szDriverID);
	virtual void ReleaseDriver(DeviceIoDriver* pDriver);
};


#endif // __uFS_fact_heap_h
