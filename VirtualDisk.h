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

//////////////////////////////////////////////////////////////////////
// VirtualFAT.h: interface for the VirtualFAT class.
// This class allows you to treat a file image as I/O device (disk).
// Main purpose: debugging and testing our FAT drivers.
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_VIRTUALDISK_H__A8C52DEB_7E46_453D_8F21_D577199D25A3__INCLUDED_)
#define AFX_VIRTUALDISK_H__A8C52DEB_7E46_453D_8F21_D577199D25A3__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "pagedfile.h"

class VirtualDisk  
{
public:
	VirtualDisk();
	virtual ~VirtualDisk();

	bool Open(const char* szFilename, bool bReadOnly, unsigned short nBytesPerSector=512);
	bool Create(const char* szFilename, unsigned long nSectors, unsigned short nBytesPerSector=512);
	void Close();
	bool IsOpen();
	bool IsReadOnly() const { return m_bReadOnly; }

	bool WriteSector(unsigned long iSector, const char* pData);
	bool ReadSector(unsigned long iSector, char* pData);

	long GetNrOfSectors() const { return m_nSectors; }

protected:
	bool m_bReadOnly;
	unsigned long m_nSectors;
	unsigned short m_nBytesPerSector;
	char* m_pData;
	FileMapping m_diskfile;
};

#endif // !defined(AFX_VIRTUALDISK_H__A8C52DEB_7E46_453D_8F21_D577199D25A3__INCLUDED_)
