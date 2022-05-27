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
/* This header file contains class definitions for managing FAT12/16        */
/* and FAT32 compatible block devices. All structures assume a sector       */
/* size of 512 bytes. Different sector sizes are not (yet) supported.       */
/* The classes are implemented on top of the uFS framework (see uFS.h)      */
/*                                                                          */
/* This implementation was designed with the following specifications       */
/* as a starting point:                                                     */
/*                                                                          */
/* - Minimize required memory resources (<2KB)                              */
/* - Don't use dynamic memory allocation (new/delete, malloc/free)          */
/* - No OS dependecy                                                        */
/* - Minimize nr. of I/O operations to minimize devices' power consumption  */
/* - At least support creation of files in root directory                   */
/* - Code should at least be compatible with Visual C++ and ARM C++ tools   */
/* - Integrate support for testing the code using virtual disks under win32 */
/* - Final code should support CompactFlash cards driven by ARM RISC uProc  */
/* - Stay away from 'extreme C++' (exceptions, templates, ...)              */
/*                                                                          */
/* The current implementation has the following known limitations:          */
/* - Most functions are NOT (yet) re-entrant, i.e. NOT (yet) thread-safe.   */
/* - Still have to implement support for long filenames.                    */
/* - Only devices with sectors of 512 bytes are supported.                  */
/* - FAT32 format is not tested/debugged yet. DON'T USE IT AT HOME.         */
/* - Don't open the same file twice at the same!                            */
/* - The FAT backup is not updates at the moment.                           */
/*                                                                          */
/* Most interfaces return an error status (IO_ERROR), which is negative     */
/* when an error has occured. A value >=0 indicates success.                */
/*                                                                          */
/****************************************************************************/

#ifndef __uFS_FAT_h
#define __uFS_FAT_h

#include "uFS.h"
#include "fatdefs.h"

///////////////////////////////////////////////////////////////////////////////
// configuration

#define MAX_OPEN_FAT_FILES 3		// defines how many (FAT) files can be open
									// simultaneously
									// Note that you should have a cache size
									// of at least MAX_OPEN_FAT_FILES+1
									// because the FatManager also requires
									// one cached page.

#if CACHE_SIZE<MAX_OPEN_FAT_FILES+1
#error "Increase cache the size"
#endif

// 'comment out' zero or more (but not all) of the following lines to disable 
// the corresponding partition format.
#define IMPLEMENT_FAT12				// i.e. floppy disk format
#define IMPLEMENT_FAT16				// <2GB partitions
#define IMPLEMENT_FAT32				// >2GB partitions

///////////////////////////////////////////////////////////////////////////////
// common defines

#define FIRST_VALID_CLUSTER 2	// FAT indexes are base-2, i.e. 1st cluster is #2
#define NULL_CLUSTER 0			// special (invalid) cluster value
#define FIXED_ROOT -1			// FAT12/16 have a fixed (non clustered) root directory

#define BIT_SHIFT_TO_N(s) (1<<(s))     // i.e. 2^s


///////////////////////////////////////////////////////////////////////////////
// forward declarations

class DeviceIoDriver_FAT;
class FatManager;

///////////////////////////////////////////////////////////////////////////////
// FatAddress
//
// Pair of values to keep track of sectors within a cluster.
// Compatible with 12, 16 and 32 bit FATs.

class FatAddress
{
public:
	FatAddress()
	{
		m_lCluster = NULL_CLUSTER;
		m_iSectorOffset = 0;
	}

	explicit FatAddress(unsigned long lCluster, unsigned short iSectorOffset)
	{
		m_lCluster = lCluster;
		m_iSectorOffset = iSectorOffset;
	}

	void operator=(const FatAddress& rhs)
	{
		m_lCluster = rhs.m_lCluster;
		m_iSectorOffset = rhs.m_iSectorOffset;
	}

	unsigned long  m_lCluster;		// zero base cluster number
	unsigned short m_iSectorOffset;	// zero base sector number within cluster
};

///////////////////////////////////////////////////////////////////////////////
// DirEntryAddress
//
// Same as FatAddress, but extended with an directory table index.
// Note that directory 'files' most often consist of more than one sector.
// However, m_iTableIndex is always relative to the start of the current sector.

class DirEntryAddress : public FatAddress
{
public:
	DirEntryAddress(unsigned long lCluster=FIXED_ROOT, unsigned short iSectorOffset=0, 
						unsigned short iTableIndex=0xffff) :
		FatAddress(lCluster, iSectorOffset)
	{
		m_iTableIndex = iTableIndex;
	}

	void operator=(const FatAddress& rhs)
	{
		FatAddress::operator =(rhs);
	}

	unsigned short m_iTableIndex; // zero based directory entry index (per sector)
};

///////////////////////////////////////////////////////////////////////////////
// GenericFatSector
// Simple class that can be used to automatically load, lock, unlock and unload
// data sectors on a FAT partition.

class GenericFatSector
{
private:
	DeviceIoDriver_FAT* m_pFAT;
	unsigned long m_sector; // nr of currently locked sector
	bool m_bWritable;
	bool m_bPreLoad;
protected:
	unsigned char* m_buf; // points to start of (locked!) sector or NULL

public:
	GenericFatSector(DeviceIoDriver_FAT* pFAT/*=NULL*/);
	virtual ~GenericFatSector();

	IO_RESULT ConnectToDriver(DeviceIoDriver_FAT* pFAT);

	IO_RESULT Load(unsigned long sector, bool bWritable, bool bPreLoad);
	IO_RESULT Load(const FatAddress& csa, bool bWritable, bool bPreLoad);
	IO_RESULT Unload(/*bool bSave*/); // always try to explicitly call this one, else we assume modifications and save
	bool IsWritable() const { return m_bWritable; }

	// simple helper functions to access writable data
	unsigned char*  GetCharPtr()		{ ASSERT(m_bWritable); return m_buf; }
	unsigned short* GetShortPtr()		{ ASSERT(m_bWritable); return (unsigned short*)m_buf; }
	unsigned long*  GetLongPtr()		{ ASSERT(m_bWritable); return (unsigned long*)m_buf; }
	DirEntryX*      GetDirEntryPtr()	{ ASSERT(m_bWritable); return (DirEntryX*)m_buf; }

	// simple helper functions to access readonly data
	const unsigned char*  GetConstCharPtr() const		{ return m_buf; }
	const unsigned short* GetConstShortPtr() const		{ return (const unsigned short*)m_buf; }
	const unsigned long*  GetConstLongPtr() const		{ return (const unsigned long*)m_buf; }
	const DirEntryX*      GetConstDirEntryPtr() const	{ return (const DirEntryX*)m_buf; }

private:
	// not implemented
	GenericFatSector() { }
};


///////////////////////////////////////////////////////////////////////////////
// FatEntrySpec
// Stateless base class!!! Only used to define interfaces for FatEntryXX

class FatEntrySpec
{
protected:
	FatManager* m_pBase;

public:
	FatEntrySpec(FatManager* pBase) : m_pBase(pBase)
	{
	}
//	virtual ~FatEntrySpec();

	virtual IO_RESULT GetEntry(unsigned long iCluster, unsigned long& value) = 0;
	virtual IO_RESULT SetEntry(unsigned long iCluster, unsigned long value) = 0;

	virtual bool ValidClusterIndex(unsigned long v) const = 0;
	virtual bool ValidFatValue(unsigned long v) const = 0;
	virtual unsigned long BadFatValue() const = 0;
	virtual unsigned long LastClusterValue() const = 0;
//	virtual unsigned long  GetRootDirCluster() const = 0;
};

///////////////////////////////////////////////////////////////////////////////
// FatEntry12
// Support class to access 12 bit FATs.
// Note: FAT entry management is done by FatManager.

#ifdef IMPLEMENT_FAT12

class FatEntry12 : public FatEntrySpec
{
public:
	FatEntry12(FatManager* pBase) : FatEntrySpec(pBase) { }
//	virtual ~FatEntry12();

	virtual IO_RESULT GetEntry(unsigned long iCluster, unsigned long& value);
	virtual IO_RESULT SetEntry(unsigned long iCluster, unsigned long value);

	// TODO: values below actually depend on size of FAT/partition
	virtual bool ValidClusterIndex(unsigned long v) const { return v>=FIRST_VALID_CLUSTER && v<MAX_CLUST12; }
	virtual bool ValidFatValue(unsigned long v) const { return (v&~FAT12_LAST_CLUSTER)==0; }
	virtual unsigned long BadFatValue() const { return FAT12_BAD; }
	virtual unsigned long LastClusterValue() const  { return FAT12_LAST_CLUSTER; }
//	virtual	unsigned long GetRootDirCluster() const { return FIXED_ROOT; };
};

#endif // #ifdef IMPLEMENT_FAT12

///////////////////////////////////////////////////////////////////////////////
// FatEntry16
// Support class to access 16 bit FATs.
// Note: FAT entry management is done by FatManager.
// Supports [4085..65524] clusters
// Max cluster size is 32K (although NT supports up to 64K)

#ifdef IMPLEMENT_FAT16

class FatEntry16 : public FatEntrySpec
{
public:
	FatEntry16(FatManager* pBase) : FatEntrySpec(pBase) { }
//	virtual ~FatEntry16();

	virtual IO_RESULT GetEntry(unsigned long iCluster, unsigned long& value);
	virtual IO_RESULT SetEntry(unsigned long iCluster, unsigned long value);

	// TODO: values below actually depend on size of FAT/partition
	virtual bool ValidClusterIndex(unsigned long v) const { return v>=FIRST_VALID_CLUSTER && v<MAX_CLUST16; }
	virtual bool ValidFatValue(unsigned long v) const { return (v&~FAT16_LAST_CLUSTER)==0; }
	virtual unsigned long BadFatValue() const { return FAT16_BAD; }
	virtual unsigned long LastClusterValue() const  { return FAT16_LAST_CLUSTER; }
//	virtual	unsigned long GetRootDirCluster() const { return FIXED_ROOT; };
};

#endif // #ifdef IMPLEMENT_FAT16

///////////////////////////////////////////////////////////////////////////////
// FatEntry32
// Support class to access 32 bit FATs.
// Note: FAT entry management is done by FatManager.
// Supports [65525..2^28] clusters

#ifdef IMPLEMENT_FAT32

class FatEntry32 : public FatEntrySpec
{
public:
	FatEntry32(FatManager* pBase) : FatEntrySpec(pBase) { }
//	virtual ~FatEntry32();

	virtual IO_RESULT GetEntry(unsigned long iCluster, unsigned long& value);
	virtual IO_RESULT SetEntry(unsigned long iCluster, unsigned long value);

	// TODO: values below actually depend on size of FAT/partition
	virtual bool ValidClusterIndex(unsigned long v) const { return v>=FIRST_VALID_CLUSTER && v<MAX_CLUST32; }
	virtual bool ValidFatValue(unsigned long v) const { return (v&~FAT32_LAST_CLUSTER)==0; }
	virtual unsigned long BadFatValue() const { return FAT32_BAD; }
	virtual unsigned long LastClusterValue() const  { return FAT32_LAST_CLUSTER; }
//	virtual	unsigned long GetRootDirCluster() const { return NULL_CLUSTER; };
};

#endif // #ifdef IMPLEMENT_FAT32

///////////////////////////////////////////////////////////////////////////////
// FatManager
// Wrapper class for accessing specific FATs.
// Note: FAT entry management is done by DeviceIoDriver_FAT.

class FatManager
{
#ifdef IMPLEMENT_FAT12
	friend class FatEntry12;
	FatEntry12 m_fat12;		// fat12 specific code
#endif // #ifdef IMPLEMENT_FAT12

#ifdef IMPLEMENT_FAT16
	friend class FatEntry16;
	FatEntry16 m_fat16;		// fat16 specific code
#endif // #ifdef IMPLEMENT_FAT16

#ifdef IMPLEMENT_FAT32
	friend class FatEntry32;
	FatEntry32 m_fat32;		// fat32 specific code
#endif // #ifdef IMPLEMENT_FAT32

protected:
	unsigned long m_nFatEntries;	// nr of entries in FAT
	unsigned short m_iFatStart;		// start sector of FAT
	unsigned long m_nFreeClusters;	// added by PG 20070106
									// total number of free clusters, initial value -1, 
									// becomes valid once NumberOfFreeEntries() is called
									// and will be updated when clusters are allocated or released.
	GenericFatSector m_sector;
	DeviceIoDriver_FAT* m_pFAT;

	FatEntrySpec* m_pSpec;	// pointer to one of the following specialisations:

	IO_RESULT LoadFatSector(unsigned long sector, bool bWritable, bool bPreLoad)
	{
		return m_sector.Load(m_iFatStart + sector, bWritable, bPreLoad);
	}

public:
	FatManager();
//	~FatManager();

	IO_RESULT ConnectToDriver(DeviceIoDriver_FAT* pFAT);
	IO_RESULT DisconnectDriver();
	IO_RESULT Flush();

	// implementation choice:
	// - put non-specific methods directly in this class
	// - dispatch specific methods to the selected specialisations

	IO_RESULT GetEofClusterNr(unsigned long& lStartCluster); // get nr of last cluster in chain
	IO_RESULT UnlinkChain(unsigned long lStartCluster); // releases a chain of clusters
	IO_RESULT AddClusters(unsigned long& lStartCluster, unsigned long nClusters, unsigned long lStartSearchAt=NULL_CLUSTER); // allocates a cluster chain
	IO_RESULT AddDirCluster(unsigned long& lEofCluster, unsigned long lParentDir); // creates or adds a cluster to a directory table
	IO_RESULT Grow(unsigned long& lStartCluster/*updated if NULL_CLUSTER*/, unsigned long nCurrentLength, unsigned long lGrowBy, unsigned long lStartSearchAt); // lengthen a chain according to new size
	IO_RESULT BackupFat(); // most FAT partitions contain a backup of the FAT. Call this fn to sync. them.
	IO_RESULT NumberOfFreeEntries(unsigned long& n);

	IO_RESULT GetEntry(unsigned long iCluster, unsigned long& value)
		{ return m_pSpec ? m_pSpec->GetEntry(iCluster, value) : IO_ERROR; }

	IO_RESULT SetEntry(unsigned long iCluster, unsigned long value, bool bCount);

	bool ValidClusterIndex(unsigned long v) const // takes into account the actual FAT size
		{ return v>=FIRST_VALID_CLUSTER && v<FIRST_VALID_CLUSTER+m_nFatEntries; }

	bool ValidFatValue(unsigned long v) const  // takes into account the actual FAT size
		{ return ValidClusterIndex(v) || v==FAT_FREE_CLUSTER || (m_pSpec ? v==m_pSpec->LastClusterValue() : false); }

	unsigned long LastClusterValue() const  
		{ return m_pSpec ? m_pSpec->LastClusterValue() : NULL_CLUSTER; }

	bool IsEofClusterValue(unsigned long v) const // returns special value for last cluster (FAT specific)
		{ return m_pSpec!=NULL ? v>=m_pSpec->BadFatValue() : false; }

//	unsigned long  GetRootDirCluster() const
//		{ return m_pSpec!=NULL ? m_pSpec->GetRootDirCluster() : NULL_CLUSTER; }

#ifdef FAT_DUMP
	void Dump(); // write FAT to debug console
#endif
};

///////////////////////////////////////////////////////////////////////////////
// Class DeviceIoDriver_FAT
//
// FAT partition layout:
//
// ---------------------
// BootSector_FAT12/16
// ---------------------
// File allocation table
// ---------------------
// File allocation table copies (optional; see m_nFatCopies)
// ---------------------
// Root directory (resides inside clusters in data space if type is FAT32)
// ---------------------
// Data (cluster numbering starts at 2==FIRST_VALID_CLUSTER)
//

class DeviceIoDriver_FAT : public DeviceIoDriver
{
friend class FatManager;

public:
	DeviceIoDriver_FAT();

	// DeviceIoDriver interface implementation 
	virtual IO_RESULT MountSW(BlockDeviceInterface* pHal, void* custom=NULL, long hDevice=-1);
	virtual IO_RESULT UnmountSW();
	virtual IO_RESULT Lock() { return IO_OK; }
	virtual IO_RESULT Unlock() { return IO_OK; }
//	virtual IO_RESULT GetType() const { return IO_DRIVER_TYPE_FILE_SYTEM; }

	virtual int GetNrOfVolumes() const;

	// DeviceIoFile interface implementation (treat pDriverData as a file handle!)
	virtual IO_RESULT CreateDirectory(const char* szFilePath);
	virtual IO_RESULT OpenFile(const char* szFilePath, DeviceIoFile& ioFile, unsigned long lFlags);
	virtual IO_RESULT FileExist(const char* szFilename);
	virtual IO_RESULT DeleteFile(const char* szFilename, unsigned long lFlags=0); // also for deleting directories
	virtual IO_RESULT CloseFile(IO_HANDLE pDriverData);
	virtual IO_RESULT ReadFile(IO_HANDLE pDriverData, char* pBuf, unsigned int& n);
	virtual IO_RESULT WriteFile(IO_HANDLE pDriverData, const char* pBuf, unsigned int& n);
	virtual IO_RESULT Seek(IO_HANDLE pDriverData, seekMode mode, long pos);
	virtual IO_RESULT Tell(IO_HANDLE pDriverData, unsigned long& pos);
	virtual IO_RESULT Flush(IO_HANDLE pDriverData);
	virtual IO_RESULT GetFileSize(IO_HANDLE pDriverData, unsigned long& s);
	virtual IO_RESULT GetNrOfFreeSectors(const char* szPath/*ignored*/, unsigned long& n);
	virtual IO_RESULT GetNrOfSectors(const char* szPath, unsigned long& n) {n = m_nSectors; return IO_OK;}
	virtual IO_RESULT Flush();
	
//	virtual IO_RESULT LoadSector(unsigned long lba, char** ppData, bool bWritable); // map relative lba to absolute lba

	// implementation
	IO_RESULT LoadFatSector(const FatAddress& fa, char** ppData, bool bWritable, bool bPreLoad);
	IO_RESULT UnloadFatSector(char* pData/*, bool bFlush=true*/)
		{ return UnloadSector(pData/*, bFlush*/); }

	IO_RESULT LookupEntry(const char* szDosName, DirEntryAddress* pMatchingEntry, DirEntry* pEntry=NULL, DirEntryAddress* pEmptyEntry=NULL);
	IO_RESULT Update(DirEntryAddress& dea, unsigned long lStartCluster, unsigned long lFileSize);
	IO_RESULT Update(DirEntryAddress& dea, DirEntryX* dir);

	// simple (but handy) helpers:
	unsigned long  GetSectorIndex(const FatAddress& csa) const { ASSERT(csa.m_lCluster!=NULL_CLUSTER); return csa.m_lCluster!=FIXED_ROOT ? (m_lFirstDataSector + ((csa.m_lCluster-FIRST_VALID_CLUSTER)<<m_iSectorToClusterShift) + csa.m_iSectorOffset) : csa.m_iSectorOffset; }
	unsigned short GetFatStartSector() const { return m_nReservedSectors; }
	unsigned long  GetFirstDataSector() const { return m_lFirstDataSector; }
	unsigned char  GetNrOfBitsPerFatEntry() const { return m_nBitsPerFatEntry; }
	unsigned long  GetNrOfFatEntries() const { return (m_nSectors-m_lFirstDataSector)>>m_iSectorToClusterShift; } // first cluster is 2==FIRST_VALID_CLUSTER
	unsigned short GetNrOfSectorsPerCluster() const { return BIT_SHIFT_TO_N(m_iSectorToClusterShift); }
	unsigned long  GetRootDirCluster() const { return m_lRootDirCluster; /* FIXED_ROOT for <FAT32 */ }
											//{ ASSERT(GetNrOfBitsPerFatEntry()!=32); return FIXED_ROOT; }
	unsigned short GetRootDirSubSector() const // i.e. sector offset within cluster
		{ ASSERT(m_nSectorsPerFat<=0xFFFF); return m_lRootDirCluster==FIXED_ROOT ? /*i.e. FAT1X*/ m_nReservedSectors + (m_nFatCopies==2?((unsigned short)m_nSectorsPerFat<<1):m_nFatCopies*(unsigned short)m_nSectorsPerFat) : 0; }

	unsigned char GetByteToClusterShift() const
	{
#if SECTOR_SIZE==512
		ASSERT(m_iSectorToClusterShift<=8);
		return 9 + m_iSectorToClusterShift;
#else
		return GetPower2(SECTOR_SIZE) + m_iSectorToClusterShift;
#endif
	}

	unsigned char GetByteToSectorShift() const
	{
#if SECTOR_SIZE==512
		return 9;
#else
		return GetPower2(SECTOR_SIZE);
#endif
	}

#ifdef FAT_DUMP
#ifdef _DEBUG
	void Dump();
#endif
#endif

	FatManager m_fat;						// intelliFAT (manages FAT sectors all by itself)

protected:
	unsigned long  m_lFirstDataSector;		// sector nr of first data sector after FAT tables and fixed directory
	unsigned long  m_nSectors;				// nr of sectors in this partition (0 if <64k)
	unsigned short m_nReservedSectors;		// nr of sectors with bootcode, normally 1
	unsigned short m_nRootDirEntries;		// multiple of size(sector)=512 / size(dir_entry)=32 = 16
//	unsigned char  m_nSectorsPerCluster;	// should be power of 2 AND cluster size <= 32K (e.g. 1,2,4,16,32,64,128)
	unsigned char  m_iSectorToClusterShift;	// nr of bits to sfhift to go from cluster to fat (see m_nSectorsPerCluster)
	unsigned char  m_nBitsPerFatEntry;		// nr of bits per FAT entry value (12 or 16)
	unsigned char  m_nFatCopies;			// normally 2
	unsigned char  m_cPartitionType;		// PT_XXXX; copied from partition table
	unsigned long  m_nSectorsPerFat;		// 512 bytes per sector, always 2 byte entries (values = 12 or 16 bit, use 16 !!), first and second entry = copy of byte medium_descr + filling; 16bits:0xF8 0xFF 0xFF 0xFF, 12bits: 0xF0 0xFF 0xFF
	unsigned long  m_lRootDirCluster;		// cluster number for root dir for FAT32, or FICED_ROOT for FAT12/16
};


#endif // __uFS_FAT_h

