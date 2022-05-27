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
// VirtualDisk.cpp: implementation of the VirtualDisk class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "VirtualDisk.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

VirtualDisk::VirtualDisk()
{
	m_bReadOnly = true;
	m_nSectors = 0;
	m_nBytesPerSector = 0;
	m_pData = NULL;
}

VirtualDisk::~VirtualDisk()
{
	Close();
}

bool VirtualDisk::Create(const char *szFilename, unsigned long nSectors, unsigned short nBytesPerSector)
{
	LONGLONG llDiskSize = nSectors * nBytesPerSector;
	bool b = m_diskfile.CreateFileMapping(szFilename,TRUE,GENERIC_WRITE|GENERIC_READ, llDiskSize)!=FALSE;
	if (b)
	{
		m_bReadOnly = false;
		m_nSectors = nSectors;
		m_nBytesPerSector = nBytesPerSector;
		m_pData = m_diskfile.GetDataPtr();
	}
	else
	{
		m_bReadOnly = true;
		m_nSectors = 0;
		m_nBytesPerSector = 0;
		m_pData = NULL;
	}

	return b;
}

bool VirtualDisk::Open(const char *szFilename, bool bReadOnly, unsigned short nBytesPerSector)
{
	bool b = m_diskfile.CreateFileMapping(szFilename, FALSE, (bReadOnly?GENERIC_READ:GENERIC_WRITE|GENERIC_READ))!=FALSE;
	if (b)
	{
		m_bReadOnly = bReadOnly;
		LONGLONG llDiskSize = m_diskfile.GetFileSize();
		m_nSectors = (unsigned long)(llDiskSize/nBytesPerSector);
		m_nBytesPerSector = nBytesPerSector;
		m_pData = m_diskfile.GetDataPtr();
	}
	else
	{
		m_nSectors = 0;
		m_nBytesPerSector = 0;
		m_pData = NULL;
	}
	return b;
}

void VirtualDisk::Close()
{
	m_diskfile.CloseFile();
	m_nSectors = 0;
	m_nBytesPerSector = 0;
	m_pData = NULL;
}

bool VirtualDisk::IsOpen()
{
	return m_diskfile.IsOpen();
}

bool VirtualDisk::ReadSector(unsigned long iSector, char *pData)
{
	if (iSector>=m_nSectors)
	{
		ASSERT(FALSE);
		return false;
	}
	ASSERT(m_nBytesPerSector!=0);
	ASSERT(m_pData!=NULL);
	ASSERT(pData!=NULL);
	memcpy(pData, m_pData+iSector*m_nBytesPerSector, m_nBytesPerSector);
	return true;
}

bool VirtualDisk::WriteSector(unsigned long iSector, const char *pData)
{
	if (iSector>=m_nSectors || m_bReadOnly)
	{
		ASSERT(FALSE);
		return false;
	}
	ASSERT(m_nBytesPerSector!=0);
	ASSERT(m_pData!=NULL);
	ASSERT(pData!=NULL);
	memcpy(m_pData+iSector*m_nBytesPerSector, pData, m_nBytesPerSector);
	return true;
}
