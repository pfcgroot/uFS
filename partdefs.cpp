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

/****************************************************************************/
/*                                                                          */
/* This header file contains structure definitions for accessing master     */
/* boot records on ATA drives.                                              */
/* Make sure that your compiler doesn't optimize the structures and         */
/* use the definitions as-is. That is, set the byte packing to 1.           */
/*                                                                          */
/****************************************************************************/

#include "partdefs.h"

bool IsNull(const CHS_address* p)
{
	return p->H==0 && p->S==0 && p->CH==0 && p->CL==0;
//	return *((long*)p)==0;//Doesn't work with ARM compiler (does compile!)
}

unsigned short Cylinders(const CHS_address* p)
{
	return (((unsigned short)p->CH)<<8) | p->CL;
}



bool IsValid(const PartitionTableEntry* p)
{
	if (p->cBootIndicator!=BI_BOOTABLE && p->cBootIndicator!=BI_NONBOOTABLE)
		return false;
	const unsigned short cylStart = Cylinders(&p->chsStart);
	const unsigned short cylEnd = Cylinders(&p->chsEnd);
	if (cylStart!=0 && cylStart>cylEnd)
		return false;
	if (p->nSectors==0 && (!IsNull(&p->chsStart) || !IsNull(&p->chsEnd)))
		return false;
	return true;
}



bool IsValidMBR(const MBR* p)
{
	if (p->iSignature!=SIGNATURE_MBR)
		return false;
	for (int i=0; i<sizeof(p->partitionTable)/sizeof(p->partitionTable[0]); i++)
		if (!IsValid(&p->partitionTable[i]))
			return false;
	return true;
}
