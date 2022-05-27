/****************************************************************************/
/*                                                                          */
/*            (C) Copyright 2001 Vrije Universiteit Amsterdam TD\FPP        */
/*                           All Rights Reserved.                           */
/*                                                                          */
/*                              Paul FC Groot                               */
/*                       Vrije Universiteit Amsterdam                       */
/*          Technische Dienst Faculteit Psychologie en Pedagogiek           */
/*                Van der Boechorststraat 1, 1081 BT AMSTERDAM              */
/*               pfc.groot@psy.vu.nl  or //www.psy.vu.nl/~paul              */
/*                                                                          */
/****************************************************************************/

#include "stdafx.h"
#include "uFS_fact_heap.h"
#include "uFS_ATA.h"
#include "uFS_FAT.h"
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// DeviceIoDriverFactory_Heap

DeviceIoDriver* DeviceIoDriverFactory_Heap::AllocateDriver(const char* szDriverID)
{
	DeviceIoDriver* p = NULL;
	if (szDriverID==NULL)
		TRACEUFS0("Warning: missing driver ID\n");
	else if (strcmp("ATA",szDriverID)==0)
		p = new DeviceIoDriver_ATA;
	else if (strnicmp("FAT",szDriverID,3)==0)
		p = new DeviceIoDriver_FAT;
	return p;
}

void DeviceIoDriverFactory_Heap::ReleaseDriver(DeviceIoDriver* pDriver)
{
	delete pDriver;
}
