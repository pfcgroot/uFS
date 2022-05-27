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

#ifndef __uFS_ATA_h
#define __uFS_ATA_h

#include "uFS.h"
#include "partdefs.h"

///////////////////////////////////////////////////////////////////////////////
// BlockDeviceInterface_FAT
//
// This class is a simple wrapper for converting relative (partition) lba numbers
// to absolute (ATA) lba numbers.

class BlockDeviceInterface_FAT : public BlockDeviceInterface
{
public:
	BlockDeviceInterface_FAT()
	{
		m_pHal = NULL;
		m_lStartOfPartition = 0;
	}

	virtual IO_RESULT MountHW(void* custom=0, unsigned long lMountFlags=0/*, long hSubDevice=-1*/)
	{
		return m_pHal->MountHW(custom, lMountFlags/*, hSubDevice*/);
	}
	virtual IO_RESULT UnmountHW(/*long hSubDevice=-1*/)
	{
		return m_pHal->UnmountHW(/*hSubDevice*/);
	}
	virtual IO_RESULT ReadSector(unsigned long lba, char* pData)
	{
		return m_pHal->ReadSector(m_lStartOfPartition+lba, pData);
	}
	virtual IO_RESULT WriteSector(unsigned long lba, const char* pData)
	{
		return m_pHal->WriteSector(m_lStartOfPartition+lba, pData);
	}
	virtual const char* GetDriverID(/*long hSubDevice=-1*/)
	{
		return m_pHal->GetDriverID();
	}
	virtual int GetSectorSize(/*long hSubDevice=-1*/)
	{
		return m_pHal->GetSectorSize();
	}

	BlockDeviceInterface* m_pHal;
	unsigned long m_lStartOfPartition; // absolute lba nr of first sector in partition
};

///////////////////////////////////////////////////////////////////////////////
// DeviceIoDriver_ATA
//
// This driver is used to interface block devices that are formatted conform
// the ATA specs. Such devices normally contain 1 or more partitions
// (FAT12, FAT16, FAT32, NTFS, Linux ext2, ...) and a master boot record
// in the first sector, which contains a partition table at the end.
// This class contains a fixed size table (of size MAX_ATA_VOLUMES), which
// contains pointer references to 'partition' drivers. The members
// in this class are normally not used directly, but indirectly
// via the sub-drivers that are connected to the partitions.

#define MAX_ATA_VOLUMES 4


class DeviceIoDriver_ATA : public DeviceIoDriver//, public BlockDeviceInterface
{
public:
	DeviceIoDriver_ATA()
	{
		m_nMounted = 0;
		for (int i=0; i<sizeof(m_pVolumes)/sizeof(m_pVolumes[0]); i++)
			m_pVolumes[i] = NULL;
	}

	// DeviceIoDriver implementation
	virtual IO_RESULT MountSW(BlockDeviceInterface* pHal, void* custom=NULL, long hDevice=-1);
	virtual IO_RESULT UnmountSW();
	virtual IO_RESULT FileExist(const char* szFilename);
	virtual IO_RESULT Lock() { return IO_OK; }
	virtual IO_RESULT Unlock() { return IO_OK; }

	virtual int GetNrOfVolumes() const;

	// BlockDeviceInterface implementation for dispatching sub partitions calls
//	virtual IO_RESULT MountHW(void* custom=0, unsigned long lMountFlags=0, long hSubDevice=-1);
//	virtual IO_RESULT UnmountHW(long hSubDevice=-1);
//	virtual IO_RESULT ReadSector(unsigned long lba, char* pData, long hSubDevice=-1);
//	virtual IO_RESULT WriteSector(unsigned long lba, const char* pData, long hSubDevice=-1);
//	virtual const char* GetDriverID(long hSubDevice=-1);
//	virtual int GetSectorSize(long hSubDevice=-1);

	// DeviceIoFile NOT supported (cannot put files outside partitions)
	virtual IO_RESULT CreateDirectory(const char* szFilePath);
	virtual IO_RESULT OpenFile(const char* szFilePath, DeviceIoFile& ioFile, unsigned long lFlags);
	virtual IO_RESULT DeleteFile(const char* szFilename, unsigned long lFlags=0); // also for deleting directories
	virtual IO_RESULT GetNrOfFreeSectors(const char* szPath, unsigned long& n);
	virtual IO_RESULT GetNrOfSectors(const char* szPath, unsigned long& n);
	virtual IO_RESULT Flush();
	virtual IO_RESULT CloseFile(IO_HANDLE /*pDriverData*/) { return IO_ERROR; }
	virtual IO_RESULT ReadFile(IO_HANDLE /*pDriverData*/, char* /*pBuf*/, unsigned int& /*n*/) { return IO_ERROR; }
	virtual IO_RESULT WriteFile(IO_HANDLE /*pDriverData*/, const char* /*pBuf*/, unsigned int& /*n*/) { return IO_ERROR; }
	virtual IO_RESULT Seek(IO_HANDLE /*pDriverData*/, seekMode /*mode*/, long /*pos*/) { return IO_ERROR; }
	virtual IO_RESULT Tell(IO_HANDLE /*pDriverData*/, unsigned long& /*pos*/) { return IO_ERROR; }
	virtual IO_RESULT Flush(IO_HANDLE /*pDriverData*/) { return IO_ERROR; }
	virtual IO_RESULT GetFileSize(IO_HANDLE /*pDriverData*/, unsigned long& /*s*/) { return IO_ERROR; }

protected:
	DeviceIoDriver* m_pVolumes[MAX_ATA_VOLUMES]; // partition references
//	PartitionTableEntry m_partitions[MAX_ATA_VOLUMES]; // partition info
	BlockDeviceInterface_FAT m_partitions[MAX_ATA_VOLUMES];
	int m_nMounted; // nr of mounted partitions (may be heterogeneous, i.e. FAT12, FAT32, Linux, ....)
};


#endif
