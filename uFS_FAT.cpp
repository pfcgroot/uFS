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
#include "uFS_FAT.h"
#include "partdefs.h"	// standard C partition definitions
#include "fatdefs.h"	// standard C FAT definitions
#include <string.h>

#ifndef ASSERT
#define ASSERT(a)
#endif

class FatManager;
class DeviceIoDriver_FAT;

///////////////////////////////////////////////////////////////////////////////
// FileState_FAT
//
// FileState_FAT structures are used to store the state of open files
// handled by the FAT device driver. A fixed-size (static) table
// is used for a predefined maximum number of open files.
//
// One important note about the current file position ('pos'):
// The file position is a zero-based index, which holds
// the (byte) position of the next read or write operation.
// A value of zero indicates the start of the file.
// An end-of-file situation occurs when a file read operation
// occurs beyond the last byte. The file pointer can be set to
// EOF by setting the value to the file length (in bytes).
// The FatAddress (fa) is always synchronized with the
// current file pointer to improve sector read/write operations.
// A special situation occurs when the file position is set
// to end of file while the file size is exactly a multiple of the
// cluster size (512, 1024, ...). In this case it is not possible
// to synchronize the FatAddress because this cluster does not
// (yet) exist. When this happens we set the FatAddress to
// the last cluster in the FAT chain. This is useful because
// we might want to add new files at the end of the chain during
// a write operation at EOF.

class FileState_FAT
{
public:
	FileState_FAT()
	{
#ifdef _DEBUG
		// put all members in a magic sandwitch  to
		// trap out-of-bound modifications
		magic0 = magic1 = 0xaa55aa55;
#endif
		pos = -1;
		lFileSize = 0;
		lStartCluster = 0;
		pData = NULL;
		lFlags = IO_FILE_UNUSED;	// error flags, or entry unused if -1
	}

	bool IsWritable() const 
		{ return (lFlags&IO_FILE_WRITABLE)!=0; }

#ifdef _DEBUG
	// check state values in debug mode
	void AssertValid()
	{
		ASSERT(magic0==0xaa55aa55); 
		ASSERT(magic1==0xaa55aa55); 
		if (pos==0)
		{
			ASSERT(fa.m_lCluster==lStartCluster);
			ASSERT(fa.m_iSectorOffset==0);
		}
	}
#endif

#ifdef _DEBUG
	unsigned long magic0;
#endif
	char* pData;					// pointer to cache page, or NULL
	unsigned long pos;				// current file position
	unsigned long lFileSize;		// file size in bytes
	unsigned long lStartCluster;	// start of cluster chain (same as value in directory entry, 0 for empty files)
	unsigned long lFlags;			// see IO_FILE_XXX, 0xffffffff for unused entries
	DirEntryAddress dea;			// location of directory entry (not the entry contents)
	FatAddress fa;					// location of current sector (according to 'pos'), NULL_CLUSTER for empty files
#ifdef _DEBUG
	unsigned long magic1;
#endif
}; 

// Pointers to _fsFAT entries are used as file handles.
// (An alternative is to use the table index as a file handle.)
static FileState_FAT _fsFAT[MAX_OPEN_FAT_FILES];


//////////////////////////
// Bit manipulation helper

inline unsigned char GetPower2(unsigned long t)
{
	unsigned char n(0);
	while ((t&0x01)==0)
	{
		n++;
		t >>= 1;
	}
	ASSERT(t==1);
	return n;
}

///////////////////////////////////////////////////////////////////////////////
// Bit manipulation helpers

static void SetStamp(DirEntry* p, const DeviceIoStamp& s, bool bCreationTime)
{
	p->lastAccess.date.Day = s.day+1;
	p->lastAccess.date.Month = s.month+1;
	p->lastAccess.date.Year = s.year<1980 ? 1980 : s.year-1980;
	p->lastAccess.time.Sec = (s.sec>>1); // dos seconds are half the real seconds
	p->lastAccess.time.Min = s.min;
	p->lastAccess.time.Hour = s.hour;
	p->lastAccessDate = p->lastAccess.date;

	if (bCreationTime)
	{
		p->cDeciSecsCreationTime = s.msec/100;
		p->fileCreation = p->lastAccess;
	}
}


IO_RESULT DeviceIoDriver_FAT::LoadFatSector(const FatAddress& fa, char** ppData, bool bWritable, bool bPreLoad)
{
	ASSERT(m_fat.ValidClusterIndex(fa.m_lCluster));
	ASSERT(fa.m_iSectorOffset<GetNrOfSectorsPerCluster()); // cannot be larger then #of sectors per cluster
	// remember: the first two clusters do not exist
	const unsigned long lba = GetFirstDataSector() + ((fa.m_lCluster-FIRST_VALID_CLUSTER)<<m_iSectorToClusterShift) + fa.m_iSectorOffset;
	return LoadSector(lba, ppData, bWritable, bPreLoad);
}

/*
IO_RESULT DeviceIoDriver_FAT::LoadSector(unsigned long lba, char** pData, bool bWritable)
{
	*pData = m_pManager->m_blockDeviceCache.Lock(m_pHal, lba, bWritable);
	return *pData ? IO_OK : IO_ERROR;
}
*/

IO_RESULT DeviceIoDriver_FAT::Update(DirEntryAddress& dea, unsigned long lStartCluster, unsigned long lFileSize)
{
	GenericFatSector sector(this);
	IO_RESULT res = sector.Load(dea, true, true);
	if (res<IO_OK)
		return res;
	DirEntryX* d = sector.GetDirEntryPtr() + dea.m_iTableIndex;
	ASSERT(lFileSize>0?lStartCluster!=NULL_CLUSTER:lStartCluster==NULL_CLUSTER);
//	ASSERT(lStartCluster==d->dirEntry.GetStartCluster()); doesn't hold for extended empty (and resetted) files 
	SetStartCluster(&d->dirEntry, lStartCluster);
	d->dirEntry.lSize = lFileSize;
	d->dirEntry.cAttributes |= FAT_ATTR_ARCHIVE;

	DeviceIoStamp t;
	m_pManager->GetClock()->GetDosStamp(t);
	::SetStamp(&d->dirEntry, t, false);

	return sector.Unload(/*true*/); // TODO: optimize this: only write when size changed? (does that happen?)
}

IO_RESULT DeviceIoDriver_FAT::Update(DirEntryAddress& dea, DirEntryX* dir)
{
	GenericFatSector sector(this);
	IO_RESULT res = sector.Load(dea, true, true);
	if (res<IO_OK)
		return res;
	DirEntryX* d = sector.GetDirEntryPtr() + dea.m_iTableIndex;
	*d = *dir;
	return sector.Unload(/*true*/); // TODO: optimize this: only write when size changed? (does that happen?)
}




///////////////////////////////////////////////////////////////////////////////
// GenericFatSector

GenericFatSector::GenericFatSector(DeviceIoDriver_FAT* pFAT)
{
	m_pFAT = pFAT;
	m_buf = NULL;
	m_sector = -1; // just an illegal value
	m_bWritable = false;
	m_bPreLoad = false;

	if (m_pFAT)
		ConnectToDriver(m_pFAT);
}

GenericFatSector::~GenericFatSector()
{
	if (m_buf!=NULL)
	{
//		ASSERT(m_buf==NULL); // don't rely on destructor cleanup, use UnloadSector()
		Unload(/*m_bWritable*/); // just to make sure
	}
}

IO_RESULT GenericFatSector::ConnectToDriver(DeviceIoDriver_FAT* pFAT)
{
	IO_RESULT res = IO_OK;
	if (m_pFAT) 
		res = Unload(/*m_bWritable save because we have to assume modifications*/);
	m_pFAT = pFAT;
	m_bWritable = false;
	m_bPreLoad = false;
	ASSERT(m_buf==NULL);
	ASSERT(m_sector==-1);
	return res;
}

IO_RESULT GenericFatSector::Load(unsigned long sector, bool bWritable, bool bPreLoad)
{
	IO_RESULT res = IO_OK;
	if (sector!=m_sector)
	{
		// load this sector, but don't forget to release a previous one
		res = Unload(/*m_bWritable*/); // save if it was opened for update
		if (res>=IO_OK)
		{
			res = m_pFAT->LoadSector(sector, (char**)&m_buf, bWritable, bPreLoad);
			m_sector = sector;
			m_bWritable = bWritable;
			m_bPreLoad = bPreLoad;
		}
	}
	else if (!m_bWritable && bWritable) // i.e. currently read only
	{
		ASSERT(m_bPreLoad);
		TRACEUFS1_DET("Warning: switching sector %lu readonly --> writable\n",sector);
		// must unload first because sector will be locked in cached
		res = m_pFAT->UnloadSector((char*)m_buf);
		if (res>=IO_OK)
		{
			m_bWritable = true;
			m_bPreLoad = bPreLoad;
			res = m_pFAT->LoadSector(sector, (char**)&m_buf, m_bWritable, bPreLoad); // force writable flag to be set in cache
		}
	}
	else if (!m_bPreLoad && bPreLoad)
	{
		TRACEUFS1_DET("Warning: switching sector %lu not preloaded --> preloaded\n",sector);
		// must unload first because sector will be locked in cached
		res = m_pFAT->UnloadSector((char*)m_buf);
		if (res>=IO_OK)
		{
			m_bWritable = bWritable;
			m_bPreLoad = bPreLoad;
			res = m_pFAT->LoadSector(sector, (char**)&m_buf, bWritable, bPreLoad); 
		}
	}
	return res;
}

IO_RESULT GenericFatSector::Unload(/*bool bSave*/)
{
	IO_RESULT res = IO_OK;
	if (m_buf!=NULL)
	{
		//ASSERT(m_bWritable==bSave); 
		res = m_pFAT->UnloadSector((char*)m_buf/*, bSave*/);
		m_buf = NULL;
		m_sector = -1; // just an illegal value
		m_bWritable = false;
		m_bPreLoad = false;
	}
	return res;
}

IO_RESULT GenericFatSector::Load(const FatAddress& csa, bool bWritable, bool bPreLoad)
{
	return Load(m_pFAT->GetSectorIndex(csa), bWritable, bPreLoad);
}

///////////////////////////////////////////////////////////////////////////////
// FatManager

FatManager::FatManager() : 
#ifdef IMPLEMENT_FAT12
	m_fat12(this),
#endif // #ifdef IMPLEMENT_FAT12
#ifdef IMPLEMENT_FAT16
	m_fat16(this),
#endif // #ifdef IMPLEMENT_FAT16
#ifdef IMPLEMENT_FAT32
	m_fat32(this),
#endif // #ifdef IMPLEMENT_FAT32
	m_sector(NULL)
{
	m_iFatStart = 0;
	m_nFatEntries = 0;
	m_nFreeClusters = -1;
	m_pSpec = NULL;
}

//FatManager::~FatManager()
//{
//}

IO_RESULT FatManager::ConnectToDriver(DeviceIoDriver_FAT* pFAT)
{
	m_pFAT = pFAT;

	IO_RESULT res = m_sector.ConnectToDriver(pFAT);
	if (res<IO_OK)
		return res;

	m_iFatStart = pFAT->GetFatStartSector();
	m_nFatEntries = pFAT->GetNrOfFatEntries();
	m_nFreeClusters = -1;

	// select one of the specific implementations
	switch (pFAT->GetNrOfBitsPerFatEntry())
	{
#ifdef IMPLEMENT_FAT12
	case 12: m_pSpec = &m_fat12; break;
#endif // #endif // #ifdef IMPLEMENT_FAT12

#ifdef IMPLEMENT_FAT16
	case 16: m_pSpec = &m_fat16; break;
#endif // #endif // #ifdef IMPLEMENT_FAT16

#ifdef IMPLEMENT_FAT32
	case 32: m_pSpec = &m_fat32; break;
#endif // #endif // #ifdef IMPLEMENT_FAT32

	default: 
		m_pSpec = NULL; 
		res = IO_ERROR; 
		ASSERT(0);
	}
	return res;
}

IO_RESULT FatManager::DisconnectDriver()
{
	m_pSpec = NULL;
	m_iFatStart = 0;
	m_nFatEntries = 0;
	m_nFreeClusters = -1;
	m_pFAT = NULL;
	return Flush();
}

IO_RESULT FatManager::Flush()
{
	return m_sector.Unload();
}

IO_RESULT FatManager::BackupFat()
{
return IO_OK;
	IO_RESULT res = IO_OK;
	if (m_pFAT==NULL)
		return IO_ERROR;
	BlockDeviceInterface* pHal = m_pFAT->GetHal();
	const int nc = m_pFAT->m_nFatCopies;
	ASSERT(nc==1 || nc==2);
	if (pHal==NULL)
		return nc>1 ? IO_ERROR : IO_OK;
	for (int i=1; i<nc; i++)
	{
		int n = m_pFAT->m_nSectorsPerFat;
		unsigned long lFatSector0 = m_iFatStart;
		unsigned long lFatSector1 = lFatSector0 + i*n;
		while (n--)
		{
			res = m_sector.Load(lFatSector0++, false, true);
			if (res<IO_OK)
				break;
			// write to uncached sector through 'low level' interface
			pHal->WriteSector(lFatSector1++, (const char*)m_sector.GetConstCharPtr()/*, m_pFAT->m_hSubDevice*/);
		}
	}
	return res;
}

IO_RESULT FatManager::UnlinkChain(unsigned long lStartCluster)
{
	// Release the given chain of clusters

	if (lStartCluster==NULL_CLUSTER)
		return IO_OK; // nothing to reset

	IO_RESULT res = IO_ERROR;
	const unsigned long lBadFat = m_pSpec->BadFatValue();
	unsigned long iCluster = lStartCluster;
	do
	{	// loop until EOF
		unsigned long nextCluster;
		res = GetEntry(iCluster,nextCluster);
		if (res<IO_OK)
			break;

		if (nextCluster==FAT_FREE_CLUSTER || nextCluster==lBadFat)
		{
			// corrupt FAT: fast exit
			ASSERT(0);
			res = IO_CORRUPT_FAT;
			break;
		}
		else
		{
			res = SetEntry(iCluster, FAT_FREE_CLUSTER, true); // release this cluster
			if (res<IO_OK)
				break;
			if (nextCluster>lBadFat) // (IsEofClusterValue(nextCluster))
				break; // EOF
			else
				iCluster = nextCluster; // continue with next
		}
	} while (true);
	return res;
}

IO_RESULT FatManager::GetEofClusterNr(unsigned long& lStartCluster)
{
	// Find the end of the cluster chain.

	if (!ValidClusterIndex(lStartCluster))
		return IO_OK; // assume null (empty) chain

	IO_RESULT res = IO_ERROR;
	const unsigned long lBadFat = m_pSpec->BadFatValue();
	do
	{
		unsigned long nextCluster;
		res = GetEntry(lStartCluster, nextCluster);
		if (res<IO_OK)
			break;

		if (nextCluster==FAT_FREE_CLUSTER || nextCluster==lBadFat)
		{
			// corrupt FAT: fast exit
			ASSERT(0);
			res = IO_CORRUPT_FAT;
			break;
		}
		else
		{
			if (nextCluster>lBadFat) // (IsEofClusterValue(nextCluster))
			{
				break; // EOF
			}
			else
				lStartCluster = nextCluster; // continue with next
		}
	} while (true);
	return res;
}

IO_RESULT FatManager::AddClusters(unsigned long& lStartCluster, unsigned long nClusters, unsigned long lStartSearchAt/*prefer end of chain*/)
{
	// Add clusters to an existing cluster chain, or create a new chain
	// if lStartCluster==NULL_CLUSTER. In the latter case lStartCluster will be updated
	// and feed back to the caller because the start of the chain should be stored somewhere
	// in a directory table.

	IO_RESULT res = IO_OK;
	unsigned long value;
	unsigned long iPrevCluster; // becomes NULL_CLUSTER for new chains
	unsigned long lRestoreFrom = NULL_CLUSTER;
	bool bNewChain = false;

	if (lStartCluster==NULL_CLUSTER) 
	{
		// start a new chain somewhere, start looking for free cluster at begin of FAT
		lStartSearchAt = FIRST_VALID_CLUSTER;
		iPrevCluster = NULL_CLUSTER;
		bNewChain = true;
	}
	else
	{
		if (lStartSearchAt==NULL_CLUSTER)
			lStartSearchAt = lStartCluster;
		// locate the last cluster of the chain
		res = GetEofClusterNr(lStartSearchAt); // be sure that we append to end of chain
		if (res<IO_OK)
			return res; // corrupt FAT
		lRestoreFrom = lStartSearchAt; // if we fail to add the required nr of clusters, we must release added clusters from this point
		iPrevCluster = lStartSearchAt; // prev. cluster becomes the current end of chain
		if (!ValidClusterIndex(++lStartSearchAt)) // forward starting point to first cluster beyond eof
			lStartSearchAt = FIRST_VALID_CLUSTER; // or start at begin of fat
	}

	TRACEUFS2_DET("adding %lu clusters at %lu\n",nClusters, lStartSearchAt);

	const unsigned long lEOF = m_pSpec->LastClusterValue(); // special EOF value to terminate cluster chain
	unsigned long iCluster = lStartSearchAt;

	while (nClusters>0 && res>=IO_OK)
	{
		// POO (principle of operation):
		// Try to get free clusters that lie beyond the last cluster of our chain (upstream).
		// Wrap back to start of disk if nothing is free upstream.
		// Stop when we're back where we started (i.e. disk full)
		while (true)
		{
			res = GetEntry(iCluster, value);
			if (res<IO_OK)
				break;

			if (value==FAT_FREE_CLUSTER)
			{
				if (lStartCluster==NULL_CLUSTER)
					lStartCluster = iCluster; // acknowledge caller of new start of chain
				if (iPrevCluster!=NULL_CLUSTER) 
					res = SetEntry(iPrevCluster, iCluster, false);
				res = SetEntry(iCluster, lEOF, true); // TODO: put before line above? (security v.s. performance)
				iPrevCluster = iCluster;
				--nClusters;
				break;
			}

			// check for end of fat
			if (!ValidClusterIndex(++iCluster))
				iCluster = FIRST_VALID_CLUSTER;

			// check if we reached initial starting point
			if (iCluster==lStartSearchAt)
			{
				res = IO_DISK_FULL; // out of disk space
				break;
			}
		}
	}
	if (res<IO_OK)
	{
		// undo chain extension
		if(bNewChain)
		{
			if (lStartCluster!=NULL_CLUSTER)
			{
				UnlinkChain(lStartCluster);
				lStartCluster = NULL_CLUSTER;
			}
		}
		else
		{
			if (GetEntry(lRestoreFrom, iCluster)>=IO_OK && !IsEofClusterValue(iCluster))
			{
				UnlinkChain(iCluster);
				SetEntry(lRestoreFrom, lEOF, true);
			}
		}
	}
	return res;
}


#ifndef MEMSET
// arm requires special version for packed structures
#define MEMSET(a,b,c) memset(a,b,c)
#endif


IO_RESULT FatManager::AddDirCluster(unsigned long& lEofCluster, unsigned long lParentDir)
{
	// this function creates or adds a new directory table cluster 
	unsigned long t = lEofCluster;
	IO_RESULT res = AddClusters(lEofCluster, 1);
	if (res>=IO_OK)
	{
		// now initialize the new entries

		// first check if a new cluster was added to an existing chain,
		// or if we started a new one
		if (t!=NULL_CLUSTER)
		{
			ASSERT(t==lEofCluster);
			res = GetEntry(t, lEofCluster);
		}
		if (res>=IO_OK)	
		{
			// return the new directory entry address to caller,
			// even if we fail below
			// initialize appended sectors
			FatAddress fa(lEofCluster, 0);
			const unsigned short nSectorsPerCluster = m_pFAT->GetNrOfSectorsPerCluster();
			for (; fa.m_iSectorOffset<nSectorsPerCluster; fa.m_iSectorOffset++)
			{
				res = m_sector.Load(fa, true, false); // writable, but do not load sector (contents is initialized below)
				if (res>=IO_OK)
				{
					DirEntryX* p = m_sector.GetDirEntryPtr();
					// finally made it (created a new empty directory entry)
					memset((void*)p, 0, SECTOR_SIZE);
					if (t==NULL_CLUSTER && fa.m_iSectorOffset==0) // if this is a new directory then create '.' and '..' entries
					{
						MEMSET(p->dirEntry.sName, ' ', 8+3); // cross array boundary!!!
						p->dirEntry.sName[0] = '.';
						p->dirEntry.cAttributes = FAT_ATTR_DIRECTORY;
						SetStartCluster(&p->dirEntry, lEofCluster); // point to ourself
						p++;
						MEMSET(p->dirEntry.sName, ' ', 8+3); // cross array boundary!!!
						p->dirEntry.sName[0] = '.';
						p->dirEntry.sName[1] = '.';
						p->dirEntry.cAttributes = FAT_ATTR_DIRECTORY;
						SetStartCluster(&p->dirEntry, lParentDir!=FIXED_ROOT?lParentDir:NULL_CLUSTER); // point to parent (NULL for fat12\16 root)
					}
					res = m_sector.Unload(/*true*/);	// save sector
				}
				if (res<IO_OK)
					break;
			}
//			csa.m_iSectorOffset = 0; not used anymore
		}
	}
	return res;
}

IO_RESULT FatManager::Grow(unsigned long& lStartCluster, unsigned long lCurrentLength, unsigned long nGrowBy, unsigned long lStartSearchAt/*prever end of chain*/)
{
	IO_RESULT res = IO_OK;

	const unsigned long nTotalSize = lCurrentLength + nGrowBy;
	// check for overflow
	if (nTotalSize<nGrowBy) // TODO: limit to 2GB? i.e. nTotalSize&0x80000000
	{
		return IO_DISK_FULL; // file exceeds 4GB
	}

	// check if we must append more clusters to the chain
	const unsigned char nShift = m_pFAT->GetByteToClusterShift();
	const unsigned long nRequiredClusters = nTotalSize>0 ? ((nTotalSize-1)>>nShift) + 1 : 0;
	const unsigned long nExistingClusters = lCurrentLength>0 ? ((lCurrentLength-1)>>nShift)+1 : 0;
	ASSERT(nRequiredClusters>=nExistingClusters); // just cannot shrink when growing
	if (nRequiredClusters>nExistingClusters)
		res = AddClusters(lStartCluster/*will be updated if ==NULL_CLUSTER*/, nRequiredClusters-nExistingClusters, lStartSearchAt);
	return res;
}

IO_RESULT FatManager::SetEntry(unsigned long iCluster, unsigned long value, bool bCount)
{ 
	IO_RESULT res = IO_ERROR;
	if (m_pSpec)
	{
		res = m_pSpec->SetEntry(iCluster, value);
		// keep free cluster count up to date (added by PG 20070106)
		if (bCount && res>=IO_OK && m_nFreeClusters!=-1)
		{
			if (value==FAT_FREE_CLUSTER)
				m_nFreeClusters++;
			else
				m_nFreeClusters--;
		}
	}
	return res; 
}

IO_RESULT FatManager::NumberOfFreeEntries(unsigned long& n)
{
	// use buffered value when available (added by PG 20070106)
	if (m_nFreeClusters!=-1)
	{
		n = m_nFreeClusters;
		return IO_OK;
	}
	// else: traverse FAT and count free clusters (expensive version...)
	n = 0;
	if (m_pSpec==NULL)
		return IO_ILLEGAL_DEVICE;

	IO_RESULT res = IO_ERROR;
	//const unsigned long lBadFat = m_pSpec->BadFatValue();
	unsigned long v;
	for (unsigned long l=FIRST_VALID_CLUSTER; l<FIRST_VALID_CLUSTER+m_nFatEntries; l++)
	{
		res = GetEntry(l,v);
		if (res<IO_OK)
			break;
		if (v==FAT_FREE_CLUSTER)
			n++;
	}
	m_nFreeClusters = n;
	return res;
}

#ifdef FAT_DUMP
void FatManager::Dump()
{
	if (m_pSpec==NULL)
		return;
	TRACEUFS0("BEGIN FAT Dump:\n");
	const unsigned long lBadFat = m_pSpec->BadFatValue();
	unsigned long v;
	const unsigned long n = m_nFatEntries;
	bool bSequence=false;
	for (unsigned long l=FIRST_VALID_CLUSTER; l<n+FIRST_VALID_CLUSTER; l++)
	{
		VERIFY(GetEntry(l,v)>=IO_OK);
		if (v==FAT_FREE_CLUSTER)
			continue;
		else if (v==lBadFat)
			TRACEUFS1("%8i BAD\n",l);
		else if (v>lBadFat)
			TRACEUFS2("%8i EOF = 0x%08x\n",l,v);
		else 
		{
			if (!bSequence)
			{
				if (v==l+1)
				{
					TRACEUFS2("%8i %i ->\n",l,(int)v);
				}
				else
				{
					TRACEUFS2("%8i %i\n",l,(int)v);
				}
			}
		}
//		bSequence = v==l+1;
	}
	TRACEUFS0("END FAT Dump\n");
//	m_sector.Unload();
}
#endif

///////////////////////////////////////////////////////////////////////////////
// FatEntry12

#ifdef IMPLEMENT_FAT12


/*
FatEntry12::FatEntry12(DeviceIoDriver_FAT* pFAT) :
	FatManager(pFAT)
{
	m_FATXX_BAD = FAT12_BAD;
}

FatEntry12::~FatEntry12()
{
}
*/
IO_RESULT FatEntry12::GetEntry(unsigned long lCluster, unsigned long& value)
{
	// Read a 12 bit cluster value from FAT12
	// Hint: think in nibbles (half a byte)

	unsigned char b0,b1;

	if (!ValidClusterIndex(lCluster))
	{
		ASSERT(0);
		return IO_INVALID_CLUSTER;
	}

	unsigned short iCluster = (unsigned short)lCluster;

	const unsigned short iNibble = (iCluster<<1) + iCluster; // 12bit: 3 nibbles per entry
#if SECTOR_SIZE==512
	// 1024 nibbles per sector, i.e split lower 10 bits
	const unsigned entry = (iNibble & 0x03ff)>>1; // two nibbles per entry
	const unsigned sector = iNibble>>10; 
#else
	const int nNibblesPerSector = SECTOR_SIZE<<1; // 1 byte contains 2 nibbles
	const unsigned entry = iNibble % nNibblesPerSector;
	const unsigned sector = iNibble/nNibblesPerSector;
#endif

	IO_RESULT res = m_pBase->LoadFatSector(sector, false, true);
	if (res<IO_OK)
		return res;

	const unsigned char* buf = m_pBase->m_sector.GetConstCharPtr();

	// get two bytes that contain the 12 bits entry, even its
	// crosses a sector boundary.
	b0 = buf[entry];
	if (entry+1>=SECTOR_SIZE)
	{
		// hmmm... part of 12 bits lie in next FAT sector
		res = m_pBase->LoadFatSector(sector + 1, false, true);
		if (res<IO_OK)
			return res;
		buf = m_pBase->m_sector.GetConstCharPtr(); // reset buffer ptr because cache might have selected another page
		b1 = buf[0]; // can only be the first byte
	}
	else
		b1 = buf[entry+1];

	// phew... we now have the two bytes, which contain the 12 byte FAT entry
	if (iNibble&0x01)
	{
		// odd nibble index
		value = (b0>>4) | (b1<<4);
	}
	else
	{
		// even nibble index
		value = b0 | ((b1&0x0f)<<8);
	}
	// do not unlock sector yet because we might want to update more entries...
	return res;
}

IO_RESULT FatEntry12::SetEntry(unsigned long lCluster, unsigned long lValue)
{
	// Write a 12 bit cluster value to FAT12
	// Hint: think in nibbles (half a byte)

	if (!ValidClusterIndex(lCluster) || !ValidFatValue(lValue) )
	{
		ASSERT(0);
		return IO_INVALID_CLUSTER;
	}

	unsigned short iCluster = (unsigned short)lCluster;
	unsigned short iValue = (unsigned short)lValue;

	const unsigned short iNibble = (iCluster<<1) + iCluster; // 12bit: 3 nibbles per entry
#if SECTOR_SIZE==512
	// 1024 nibbles per sector, i.e split lower 10 bits
	const unsigned entry = (iNibble & 0x03ff)>>1; // two nibbles per entry
	const unsigned sector = iNibble>>10; 
#else
	const int nNibblesPerSector = SECTOR_SIZE<<1; // 1 byte contains 2 nibbles
	const unsigned entry = iNibble % nNibblesPerSector;
	const unsigned sector = iNibble/nNibblesPerSector;
#endif
//	const unsigned short iFatStart = m_pFAT->GetFatStartSector();

	IO_RESULT res = m_pBase->LoadFatSector(sector, true, true);
	if (res<IO_OK)
		return res;

	// write first 4 or 8 LSBs of entry (into upper bits)
	unsigned char* p = m_pBase->m_sector.GetCharPtr() + entry;
	if (iNibble&0x01)
		*p = ((*p)&0x0f)|((iValue&0x0f)<<4); // preserve lower nibble, use 4 LSB of new value
	else
		*p = iValue&0xff;

	// go on with next part: point to next byte
	if (entry+1>=SECTOR_SIZE)
	{
		// hmmm... part of 12 bits lie in next FAT sector
		res = m_pBase->LoadFatSector(sector + 1, true, true);
		if (res<IO_OK)
			return res;
		p = m_pBase->m_sector.GetCharPtr();  // can only be the first byte
	}
	else
		p++;// = &m_buf[entry+1];

	// write last 4 or 8 MSBs of entry (into lowest bits)
	if (iNibble&0x01)
		*p = iValue>>4;
	else
		*p = (*p&0xf0)|(iValue>>8); // preserve upper nibble, use 4 MSB of new value

	// do not unlock sector yet because we might want to update more entries...
	return res;
}

#endif // #ifdef IMPLEMENT_FAT12

///////////////////////////////////////////////////////////////////////////////
// FatEntry16

#ifdef IMPLEMENT_FAT16

/*
FatEntry16::FatEntry16(DeviceIoDriver_FAT* pFAT) :
	FatManager(pFAT)
{
	m_FATXX_BAD = FAT16_BAD;
}

FatEntry16::~FatEntry16()
{
}
*/

IO_RESULT FatEntry16::GetEntry(unsigned long lCluster, unsigned long& value)
{
	// Read a 16 bit cluster value from FAT16

	if (!ValidClusterIndex(lCluster))
	{
		ASSERT(0);
		return IO_INVALID_CLUSTER;
	}

	unsigned short iCluster = (unsigned short)lCluster;

#if SECTOR_SIZE==512
	// 256 entries per sector
	const unsigned entry = iCluster & 0xff; 
	const unsigned sector = iCluster>>8;
#else
	const unsigned int nEntriesPerSector = SECTOR_SIZE>>1;
	const unsigned entry = iCluster % nEntriesPerSector;
	const unsigned sector = iCluster/nEntriesPerSector;
#endif

	IO_RESULT res = m_pBase->LoadFatSector(sector, false, true);
	if (res<IO_OK)
		return res;

	const unsigned short* buf = m_pBase->m_sector.GetConstShortPtr();
	value = buf[entry];

	// do not unlock sector yet because we might want to update more entries...
	return IO_OK;
}


IO_RESULT FatEntry16::SetEntry(unsigned long lCluster, unsigned long lValue)
{
	// Write a 16 bit cluster value to FAT16

	if (!ValidClusterIndex(lCluster) || !ValidFatValue(lValue) )
	{
		ASSERT(0);
		return IO_INVALID_CLUSTER;
	}

	unsigned short iCluster = (unsigned short)lCluster;

#if SECTOR_SIZE==512
	// 256 entries per sector
	const unsigned entry = iCluster & 0xff; 
	const unsigned sector = iCluster>>8;
#else
	const unsigned int nEntriesPerSector = SECTOR_SIZE>>1;
	const unsigned entry = iCluster % nEntriesPerSector;
	const unsigned sector = iCluster/nEntriesPerSector;
#endif

	IO_RESULT res = m_pBase->LoadFatSector(sector, true, true);
	if (res<IO_OK)
		return res;

	unsigned short* buf = m_pBase->m_sector.GetShortPtr();
	buf[entry] = (unsigned short)lValue;

	// do not unlock sector yet because we might want to update more entries...
	return IO_OK;
}

#endif // #ifdef IMPLEMENT_FAT16

///////////////////////////////////////////////////////////////////////////////
// FatEntry32

#ifdef IMPLEMENT_FAT32

/*
FatEntry32::FatEntry32(DeviceIoDriver_FAT* pFAT) :
	FatManager(pFAT)
{
	m_FATXX_BAD = FAT32_BAD;
}

FatEntry16::~FatEntry32()
{
}
*/

IO_RESULT FatEntry32::GetEntry(unsigned long lCluster, unsigned long& value)
{
	// Read a 32 bit cluster value from FAT32

	if (!ValidClusterIndex(lCluster))
	{
		ASSERT(0);
		return IO_INVALID_CLUSTER;
	}

//	unsigned short iCluster = (unsigned short)lCluster;

#if SECTOR_SIZE==512
	// 128 entries per sector
	const unsigned entry = lCluster & 0x7f; 
	const unsigned long sector = lCluster>>7;
#else
	const unsigned nEntriesPerSector = SECTOR_SIZE>>2;
	const unsigned entry = lCluster % nEntriesPerSector;
	const unsigned long sector = lCluster/nEntriesPerSector;
#endif

	IO_RESULT res = m_pBase->LoadFatSector(sector, false, true);
	if (res<IO_OK)
		return res;

	const unsigned long* buf = m_pBase->m_sector.GetConstLongPtr();
	value = buf[entry] & FAT32_LAST_CLUSTER; // must ignore (and reserve) upper 4 bits

	// do not unlock sector yet because we might want to update more entries...
	return IO_OK;
}


IO_RESULT FatEntry32::SetEntry(unsigned long lCluster, unsigned long lValue)
{
	// Write a 32 bit cluster value to FAT32
	// However, currently only the 28 least significant bits are used, the upper 
	// four are reserved and should be preserved

	if (!ValidClusterIndex(lCluster) || !ValidFatValue(lValue) )
	{
		ASSERT(0);
		return IO_INVALID_CLUSTER;
	}

//	unsigned short iCluster = (unsigned short)lCluster;

#if SECTOR_SIZE==512
	// 128 entries per sector
	const unsigned entry = lCluster & 0x7f; 
	const unsigned long sector = lCluster>>7;
#else
	const unsigned int nEntriesPerSector = SECTOR_SIZE>>2;
	const unsigned entry = lCluster % nEntriesPerSector;
	const unsigned long sector = lCluster/nEntriesPerSector;
#endif

	IO_RESULT res = m_pBase->LoadFatSector(sector, true, true);
	if (res<IO_OK)
		return res;

	unsigned long* buf = m_pBase->m_sector.GetLongPtr() + entry;
	*buf = ((*buf)&~FAT32_LAST_CLUSTER) | lValue; // must reserve upper 4 bits

	// do not unlock sector yet because we might want to update more entries...
	return IO_OK;
}

#endif // #ifdef IMPLEMENT_FAT32

///////////////////////////////////////////////////////////////////////////////
// DeviceIoDriver_FAT

DeviceIoDriver_FAT::DeviceIoDriver_FAT()
{
	m_nBitsPerFatEntry = 0;
	m_cPartitionType = PT_FREE;
	m_nFatCopies = 0;
	m_nReservedSectors = 0;
	m_nRootDirEntries = 0;
	m_nSectors = 0;
	m_nSectorsPerFat = 0;
//		m_nSectorsPerCluster = 0;
	m_iSectorToClusterShift = 0; // use BIT_SHIFT_TO_N(m_iSectorToClusterShift) to get # sectors per cluster
	m_lFirstDataSector = 0;
	m_lRootDirCluster = NULL_CLUSTER;
}

int DeviceIoDriver_FAT::GetNrOfVolumes() const 
{ 
	return m_cPartitionType!=PT_FREE ? 1 : 0; 
}

IO_RESULT DeviceIoDriver_FAT::MountSW(BlockDeviceInterface* pHal, void* custom, long hDevice)
{
#ifdef _DEBUG
	unsigned short nSectorsPerCluster = 0;
#endif
	IO_RESULT res = IO_OK;
	const char* buf = NULL;
	BootSector_FAT16* br16 = NULL;
	BootSector_FAT32* br32 = NULL;
	unsigned short t;
	int nBytesPerSector = 0;
	
	if (custom==NULL)
	{
		ASSERT(0);
		return IO_ERROR;
	}
	if (m_pHal!=NULL)
	{
		ASSERT(0);
		return IO_FAILED_TO_LOAD_DRIVER; // i.e. already mounted to a device
	}
	m_hSubDevice = hDevice;

	// cast generic pointer; should point to partition table entry
	m_cPartitionType = ((PartitionTableEntry*)custom)->cPartitionType;
	switch (m_cPartitionType)
	{
	case PT_FAT12:
		m_nBitsPerFatEntry = 12;
		break;

	case PT_FAT16s:
	case PT_FAT16:
	case PT_FAT16LBA:
		m_nBitsPerFatEntry = 16;
		break;

	case PT_FAT32:
	case PT_FAT32LBA:
		m_nBitsPerFatEntry = 32;
		break;

	default: // unsupported partition layout
		res = IO_UNKNOWN_PARTITION_TYPE;
		goto _exit;
	}

	TRACEUFS1("DeviceIoDriver_FAT::MountSW: mounting partition type FAT%d\n",m_nBitsPerFatEntry);

	// store pointer reference to low level driver
	m_pHal = pHal;

	// load sector in cache; don't forget to unlock it (i.e. jump to exit; don't return)
	res = LoadSector(0/*first sector of partition*/, (char**)&buf, IO_READ_ONLY, true);
	if (res<IO_OK)
		goto _exit;
	ASSERT(buf!=NULL);

	if (m_nBitsPerFatEntry<32)
	{
		br16 = (BootSector_FAT16*)buf;
		if (br16->iSignature!=SIGNATURE_FAT16)
		{
			TRACEUFS0("DeviceIoDriver_FAT::MountSW: illegal boot record signature\n");
			res = IO_CORRUPT_BOOT_REC;
			goto _exit;
		}
		nBytesPerSector = br16->nBytesPerSector;
#ifdef _DEBUG
		nSectorsPerCluster = br16->nSectorsPerCluster;
#endif
		m_nFatCopies         = br16->nFatCopies;
		m_nReservedSectors   = br16->nReservedSectors;
		m_nRootDirEntries    = br16->nRootDirEntries; // zero for FAT32
		m_nSectorsPerFat     = br16->nSectorsPerFat;
		t = br16->nSectorsPerCluster; // remember for bitshift below
		ASSERT(m_nRootDirEntries!=0);
		m_lRootDirCluster    = FIXED_ROOT;
		m_nSectors           = br16->nSectors32!=0 ? br16->nSectors32 : br16->nSectors;
	}
	else
	{
		br32 = (BootSector_FAT32*)buf;
		if (br32->lSignature!=SIGNATURE_FAT32)
		{
			TRACEUFS0("DeviceIoDriver_FAT::MountSW: illegal boot record signature\n");
			res = IO_CORRUPT_BOOT_REC;
			goto _exit;
		}
		nBytesPerSector = br32->nBytesPerSector;
#ifdef _DEBUG
		nSectorsPerCluster = br32->nSectorsPerCluster;
#endif
		m_nFatCopies         = br32->nFatCopies;
		m_nReservedSectors   = br32->nReservedSectors;
		m_nRootDirEntries    = br32->nRootDirEntries; // zero for FAT32
		m_nSectorsPerFat     = br32->nSectorsPerFat32;
		t = br32->nSectorsPerCluster; // remember for bitshift below
		ASSERT(m_nRootDirEntries==0);
		m_lRootDirCluster    = br32->lRootCluster;
		m_nSectors           = br32->nSectors32;
	}
	if (nBytesPerSector!=m_pHal->GetSectorSize() || nBytesPerSector!=SECTOR_SIZE)
	{
		TRACEUFS0("DeviceIoDriver_FAT::MountSW: unsupported sector size\n");
		res = IO_UNSUPPORTED_SECTOR_SIZE;
		goto _exit;
	}
	TRACEUFS1("DeviceIoDriver_FAT::MountSW: #FAT copies          : %d\n",m_nFatCopies);
	TRACEUFS1("DeviceIoDriver_FAT::MountSW: #sectors             : %d\n",m_nSectors);
	TRACEUFS1("DeviceIoDriver_FAT::MountSW: #sectors per cluster : %d\n",nSectorsPerCluster);
	TRACEUFS1("DeviceIoDriver_FAT::MountSW: #RootDirEntries      : %d\n",m_nRootDirEntries);
	TRACEUFS1("DeviceIoDriver_FAT::MountSW: #SectorsPerFat       : %d\n",m_nSectorsPerFat);
	TRACEUFS1("DeviceIoDriver_FAT::MountSW: #RootDirCluster      : %d\n",m_lRootDirCluster);
	
	// unlock buffer because we don't need the MBR anymore
	UnloadSector((char*)buf/*, false*/);
	buf = NULL;
	br16 = NULL;
	br32 = NULL;

	// determine bitshift for cluster<->sector conversion
	m_iSectorToClusterShift = GetPower2(t);

	// calculate start of data space:
	//   BOOT sector (==m_nReservedSectors)
	//   FAT 1
	//   FAT 2
	//   Fixed Root - optional -> m_nRootDirEntries will be zero for FAT32
	//   Data
#if SECTOR_SIZE==512
	m_lFirstDataSector = m_nReservedSectors + m_nFatCopies*m_nSectorsPerFat + (m_nRootDirEntries>>4); // 16 dir entries per sector
#else
	m_lFirstDataSector = m_nReservedSectors + m_nFatCopies*m_nSectorsPerFat + (((unsigned long)m_nRootDirEntries)*sizeof(DirEntry))/SECTOR_SIZE;
#endif

	TRACEUFS1("DeviceIoDriver_FAT::MountSW: m_lFirstDataSector : %d\n",m_lFirstDataSector);

	// all partition information is available now, connect the FAT manager
	res = m_fat.ConnectToDriver(this);

_exit:
#ifdef _DEBUG
	if (res!=IO_OK)
		TRACEUFS1("DeviceIoDriver_FAT::MountSW: error mounting partition (errornr=%d)\n",res);
#endif
	if (buf)
		UnloadSector((char*)buf/*, false*/);
	if (res<IO_OK)
	{
//		ASSERT(0);
		m_cPartitionType = PT_FREE; // kind of error status (probably not formatted)
	}

#ifdef FAT_DUMP
	Dump();
#endif	
	
	return res;
}

IO_RESULT DeviceIoDriver_FAT::UnmountSW()
{
#ifdef _MSC_VER
#ifdef _DEBUG
//	TRACEUFS0("Unmounting...\n");
//	Dump();
//	TRACEUFS0("Unmounting ready\n");
#endif
#endif
	m_fat.BackupFat();
	IO_RESULT t = m_fat.DisconnectDriver();//JDH
	m_pHal = NULL;//JDH
	return t;//JDH
}

IO_RESULT DeviceIoDriver_FAT::LookupEntry(const char* szDosName, DirEntryAddress* pMatchingEntry, DirEntry* pEntry, DirEntryAddress* pEmptyEntry/*optional*/)
{
	// szDosName        Must be a plain DOS 8.3 nul terminated string, optionally leaded with a directory path.
	//                  Use NULL to find an empty entry (if pEmptyEntry!=NULL).
	// pMatchingEntry   Will be filled with the exact location of the first matching entry.
	// pEntry 			Used to return info on the found (matching or empty) entry
	// pEmptyEntry      Is an optional reference that will be filled with the exact address 
	//                  of the first empty entry (which may be de EOD entry!!!)
	//
	// return	IO_MATCH_ENTRY		when file is found
	//			IO_EMPTY_ENTRY		when file was not found but an empty entry is available (and requested)
	//			IO_FILE_NOT_FOUND	when file was not found and no empty entry was available or requested
	//			<IO_OK				when an error occured

	// NB. This function is not called recursively for subdirectory scans to minimize heap size requirements.
	//     As a result the code might be a bit more complex.

	ASSERT(szDosName!=NULL || pEmptyEntry!=NULL); // must at least look for new empty entry or an existing item
	GenericFatSector sector(this);

	IO_RESULT ret = IO_FILE_NOT_FOUND; // this is returned when no error occured
	IO_RESULT res = IO_ERROR; // this is returned in case of an error
#if SECTOR_SIZE==512
	const unsigned nEntriesPerSector = 16;
#else
	const unsigned nEntriesPerSector = SECTOR_SIZE/sizeof(DirEntry);
#endif
	const unsigned short nSectorsPerCluster = GetNrOfSectorsPerCluster();
	const DirEntryX* d = NULL;
	bool bEmptyEntryFound = false;

//	if (pMatchingEntry==NULL)
//		szDosName = NULL; // i.e., only interested in finding first empty entry

	// Initialize cluster/sector address of this directory.
	// Note that FAT12/16 have a fixed root directory (cluster nr -1==FIXED_ROOT), 
	// which can be found between last FAT and first cluster.
	// If this is the case, we only use the absolute sector in the second field.
	unsigned long lDirStartCluster = GetRootDirCluster();	// first cluster of directory chain or FIXED_ROOT if root of FAT12\16
//	if (lDirStartCluster==NULL_CLUSTER)
//		lDirStartCluster = GetRootDirCluster();
	FatAddress csa(lDirStartCluster, GetRootDirSubSector() );

	// check if we are looking for (nested) directory names
	int len = -1;
	const char* szNextDir = NULL;
	if (szDosName)
	{
		if (*szDosName=='\\') szDosName++; // skip optional directory separator
		szNextDir = strchr(szDosName,'\\');
		len = szNextDir ? (int)szNextDir-(int)szDosName : -1;
		if (szNextDir && *++szNextDir=='\0')  // skip backslash separator
			szNextDir = NULL; // set pointer to NULL if there is no next part after backslash
	}

	// walk directory (tree) until match (bStop==true)
	bool bStop = false;
	do
	{
		bool bRestart = false; // restart search for matching subdirectory if true

		// loop through directory until we find a matching entry, sector by sector
		res = sector.Load(csa, false, true);
		if (res<IO_OK)
			break;
		// get type casted directory table pointer
		d = sector.GetConstDirEntryPtr();
		ASSERT(d!=NULL);

		// loop through all entries in this sector
		unsigned iMatchEntry = -1;
		for (int i=0; i<nEntriesPerSector && !bRestart && !bStop; i++, d++)
		{
			// only check for plain dos 8.3 filenames (incl. directories)
			const unsigned char cAttr = d->dirEntry.cAttributes;
			if (cAttr!=FAT_ATTR_LFN && (cAttr&FAT_ATTR_VOLUMEID)==0)
			{
				switch (d->dirEntry.sName[0])
				{
				case FAT_FILE_EOD:
					bStop = true;
					// fall through: accept first unused entry as empty entry
				case FAT_FILE_REMOVED:
					if (!bEmptyEntryFound && szNextDir==NULL) // only track empty entry if we are in lowest directory level
					{
						if (pEmptyEntry && !bEmptyEntryFound) // store address of this empty entry
						{	
							pEmptyEntry->operator=(csa);
							pEmptyEntry->m_iTableIndex = i;
	//						pEmptyEntry = NULL; // only update on first match
							ret = IO_EMPTY_ENTRY;	// let user know we found an empty entry
														// (useful in case we fail to find the file)
						}
	//					if (szNextDir==NULL) // only track empty entry if we are in lowest directory level
	//					{	
							if (szDosName==NULL)
							{
								// can stop since caller is only interested in empty entry
								bStop = true;
							}
							else if (pEntry) 
							{
								if (SetDosFilename(pEntry,szDosName)<=0) // copy leafname back to user buffer when empty entry was found
									res = IO_ILLEGAL_FILENAME;
							}
	//					}
						bEmptyEntryFound = true;
					}
					break;

				case '.': // "." or ".."
					break;

				default:
					if (szDosName && CompareDosFilename(&d->dirEntry, szDosName, len)==0)
					{
						if (szNextDir) // continue with next part?
						{
							// first check if this is indeed a directory
							if ((d->dirEntry.cAttributes&FAT_ATTR_DIRECTORY)==0)
							{
								res = IO_NOT_A_DIRECTORY;
								bStop = true;
							}
							else
							{
								// matched a directory level, continue with subdir or file,
								// or stop if this was the last part
								ASSERT(d->dirEntry.lSize==0); // should be zero for directories
								szDosName = szNextDir;
								szNextDir = strchr(szDosName,'\\');
								len = szNextDir ? (int)szNextDir-(int)szDosName : -1;
								if (szNextDir && *++szNextDir=='\0')  // skip backslash separator
									szNextDir = NULL; // no next part
								// set cluster address of subdirectory
								csa.m_lCluster = GetStartCluster(&d->dirEntry);
								csa.m_iSectorOffset = 0;
								if (csa.m_lCluster==NULL_CLUSTER) // empty directory?
								{
// PG								res = IO_FILE_NOT_FOUND;
									bStop = true;
								}
								else
								{
									// found next subdirectory
									bRestart = true;
								}
							}
						}
						else
						{
							// final match!!! (file, or directory if not trailed with backslash)
							iMatchEntry=i;
							bStop = true;
							if (pEntry) // copy directory entry back to optional (by ref) argument
								*pEntry = d->dirEntry;
						}

					}
					break;
				} // switch first dosname char
			} // if file or directory entry
		} // next entry
		if (iMatchEntry!=-1)
		{
			TRACEUFS1("lookup directory entry match: %s\n",szDosName);
			if (pMatchingEntry)
			{
				// copy directory entry address (cluster,sector,index) back to caller
				pMatchingEntry->operator=(csa);
				pMatchingEntry->m_iTableIndex = iMatchEntry;
			}
			ret = IO_MATCH_ENTRY;
			bStop = true;
		}
		else if (!bStop && !bRestart)
		{
			// continue with next sector
			if (csa.m_lCluster==FIXED_ROOT)
			{
				// i.e. fixed root of FAT12\16
				if (++csa.m_iSectorOffset>=GetFirstDataSector())
				{
					// this was the last root sector, can't continue
// PG				res = IO_FILE_NOT_FOUND;
					bStop = true;
				}
			}
			else
			{
				// ordinary dir that resides in one or more clusters
				// continue with next sector within cluster, or jump to first sector of next cluster
				if (++csa.m_iSectorOffset>=nSectorsPerCluster)
				{
					csa.m_iSectorOffset = 0;
					unsigned long nextCluster = NULL_CLUSTER;
					res = m_fat.GetEntry(csa.m_lCluster, nextCluster);

					if (res<IO_OK || nextCluster==NULL_CLUSTER)
					{
						// corrupt FAT or premature EOF
						ASSERT(0);
						res = IO_CORRUPT_FAT;
						bStop = true;
					}
					else
					{
						if (!m_fat.ValidClusterIndex(nextCluster)) // should be EOF
						{
							// End of directory, and file or directory not found.
							// Extend the directory table with another cluster if
							// user requested an empty entry, and we still haven't 
							// found one. (Only if we reached the lowest directory
							// level though!)
							if (pEmptyEntry!=NULL && !bEmptyEntryFound && szNextDir==NULL)
							{
								res = m_fat.AddDirCluster(csa.m_lCluster/*will be updated with new cluster nr*/, NULL_CLUSTER);
								if (res>=IO_OK)
								{
									ASSERT(csa.m_iSectorOffset==0);
									pEmptyEntry->operator=(csa);
									pEmptyEntry->m_iTableIndex = 0; // just return the first entry
									pEmptyEntry = NULL;			// not required, but consequent
									ret = IO_EMPTY_ENTRY;		// let user know we have an empty entry
								}
							}
							else
							{
// PG								res = IO_FILE_NOT_FOUND;
							}
							bStop = true; // just created an empty entry
						}
						else
							csa.m_lCluster = nextCluster; // continue with next
					}
				}
			}
		}
		res = sector.Unload(/*false*/);
		if (res<IO_OK)
			break;
	} while (!bStop);
	return res>=IO_OK ? ret : res; // only return res in case of errors
}

IO_RESULT DeviceIoDriver_FAT::FileExist(const char* szFilename)
{	DirEntryAddress MatchingEntry; DirEntry Entry;	// Needed for LookupEntry, but discarded when done.
	return LookupEntry(szFilename, &MatchingEntry, &Entry, NULL);
}

/*
IO_RESULT DeviceIoDriver_FAT::UnlinkChain(unsigned long lStartCluster)
{
//	IO_RESULT res = IO_ERROR;
//	FatManager FatManager(this);
//	res = FatManager.UnlinkChain(lStartCluster);
	return m_fat.UnlinkChain(lStartCluster);
}
*/


IO_RESULT DeviceIoDriver_FAT::CreateDirectory(const char* szFilePath/*full path without trailing slash*/)
{
	IO_RESULT res = IO_ERROR;
	DirEntryAddress dea;
	DirEntryX de;

//	TRACEUFS1("create directory %s\n",szFilePath);

	res = LookupEntry(szFilePath, NULL/*not interested in opening an existing*/, &de.dirEntry, &dea);
	switch (res)
	{
	case IO_MATCH_ENTRY: // file found
		res = IO_FILE_OR_DIR_EXISTS;
		break;

	case IO_EMPTY_ENTRY: // file not found, but an empty entry exists
		{
			DeviceIoStamp t;
			m_pManager->GetClock()->GetDosStamp(t);

//			const char* szNextDir;
//			while (szNextDir = strchr(szFilePath,'\\'), szNextDir!=NULL)
//				szFilePath = szNextDir+1;

			unsigned long lNewCluster = NULL_CLUSTER;
			res = m_fat.AddDirCluster(lNewCluster/*will be updated with new cluster nr*/, dea.m_lCluster);
			if (res>=IO_OK)
			{
				::SetStamp(&de.dirEntry, t, true);
				de.dirEntry.cAttributes = FAT_ATTR_DIRECTORY; // FAT_ATTR_ARCHIVE normally not set
				SetStartCluster(&de.dirEntry, lNewCluster);
				de.dirEntry.lSize = 0; // always, even if table size > 0
				de.dirEntry.cReservedNT = 0;
//				if (SetDosFilename(&de.dirEntry, szFilePath)==0)
//					res = IO_ILLEGAL_FILENAME;
//				else
					res = Update(dea, &de);
			}

		}
		break;
	}
	return res;
}

IO_RESULT DeviceIoDriver_FAT::DeleteFile(const char* szFilePath, unsigned long lFlags)
{
	IO_RESULT res = IO_ERROR;
	DirEntryAddress dea;
	DirEntryX de;
	DirEntryX* pDirEntry = NULL;
	GenericFatSector sector(this);

//	TRACEUFS1("delete file %s\n",szFilePath);

	// first find the directory
	res = LookupEntry(szFilePath, &dea, &de.dirEntry, NULL);
	if (res!=IO_MATCH_ENTRY)
		return res;

	// can only delete files and directories when correct attributes are specified (read only, hidden, directory, ...)
	if (de.dirEntry.cAttributes&~lFlags)
		return IO_WRONG_ATTRIBUTES;

	unsigned long lStartCluster = GetStartCluster(&de.dirEntry);

	// careful when this is a directory: must be empty!
	if (de.dirEntry.cAttributes&FAT_ATTR_DIRECTORY)
	{
		/////////////////////////////////////
		// check if directory entry is empty
		bool bEmpty = true;
#if SECTOR_SIZE==512
		const unsigned nEntriesPerSector = 16;
#else
		const unsigned nEntriesPerSector = SECTOR_SIZE/sizeof(DirEntry);
#endif
		const unsigned short nSectorsPerCluster = GetNrOfSectorsPerCluster();

		if (lStartCluster!=NULL_CLUSTER)
		{
			FatAddress csa(lStartCluster, 0);

			// walk directory (tree) until match (bStop==true)
			bool bStop = false;
			do
			{
				// loop through directory until we find a matching entry, sector by sector
				res = sector.Load(csa, false, true);
				if (res<IO_OK)
					break;
				// get type casted directory table pointer
				pDirEntry = (DirEntryX*)sector.GetConstDirEntryPtr();
				ASSERT(pDirEntry!=NULL);

				// loop through all entries in this sector
				for (int i=0; i<nEntriesPerSector && !bStop; i++, pDirEntry++)
				{
					// only check for plain dos 8.3 filenames (incl. directories)
					const unsigned char cAttr = pDirEntry->dirEntry.cAttributes;
					if (cAttr!=FAT_ATTR_LFN && (cAttr&FAT_ATTR_VOLUMEID)==0)
					{
						switch (pDirEntry->dirEntry.sName[0])
						{
						case FAT_FILE_EOD:
							bStop = true;
							break;
						case '.': // "." or ".."
						case FAT_FILE_REMOVED:
							break;
						default:
							bEmpty = false;
							bStop = true;
							break;
						}
					}
				}
				if (!bStop)
				{
					// continue with next sector within cluster, or jump to first sector of next cluster
					if (++csa.m_iSectorOffset>=nSectorsPerCluster)
					{
						csa.m_iSectorOffset = 0;
						unsigned long nextCluster = NULL_CLUSTER;
						res = m_fat.GetEntry(csa.m_lCluster, nextCluster);

						if (res<IO_OK || nextCluster==NULL_CLUSTER)
						{
							// corrupt FAT or premature EOF
							ASSERT(0);
							res = IO_CORRUPT_FAT;
							bStop = true;
						}
						else
						{
							if (!m_fat.ValidClusterIndex(nextCluster)) // should be EOF
								bStop = true;
							else
								csa.m_lCluster = nextCluster; // continue with next
						}
					}
				}
				res = sector.Unload(/*false*/);
				if (res<IO_OK)
					bStop = true;
			} while (!bStop);
		}
		if (res<IO_OK)
			return res;
		if (!bEmpty)
			return IO_DIRECTORY_NOT_EMPTY;
	}

	/////////////////////////////////////////////
	// remove file/directory from directory table
	res = sector.Load(dea, true, true);
	if (res<IO_OK)
		return res;
	// get type casted directory table pointer
	pDirEntry = sector.GetDirEntryPtr() + dea.m_iTableIndex;
	SetStartCluster(&pDirEntry->dirEntry, NULL_CLUSTER);
	pDirEntry->dirEntry.sName[0] = FAT_FILE_REMOVED;
	pDirEntry->dirEntry.lSize = 0;
	// TODO: what exactly should be reset when deleting a file?
	res = sector.Unload(/*true*/);
	if (res<IO_OK)
		return res;

	/////////////////////////////////////////////
	// release clusters
	if (lStartCluster!=NULL_CLUSTER)
		res = m_fat.UnlinkChain(lStartCluster);

	return res;
}

IO_RESULT DeviceIoDriver_FAT::OpenFile(const char* szFilePath, DeviceIoFile& ioFile, unsigned long lFlags)
{
	// TODO: in a multithreaded env. we should lock common resources (i.e. _fsFAT)

//	TRACEUFS1("open or create file %s\n",szFilePath);

	IO_RESULT res = IO_ERROR;
	DirEntryX de;
	FileState_FAT* pFS = NULL;
//	DirManager dm(this);
	DirEntryAddress deaEmpty;

	res = ioFile.Close();
	if (res<IO_OK)
		return res;

	// file must be writable if we must reset or create it
	if ((lFlags&IO_FILE_WRITABLE)==0 && ((lFlags&IO_FILE_RESET) || (lFlags&IO_FILE_CREATE)) )
		return IO_CANNOT_WRITE_FILE;

	// find an unused file handle
	for (int i=0; i<sizeof(_fsFAT)/sizeof(_fsFAT[0]); i++)
	{
		if (_fsFAT[i].lFlags==IO_FILE_UNUSED)
		{
			pFS = &_fsFAT[i];
			// clear IO_FILE_UNUSED status at the end if we can continue without errors
			break;
		}
	}
	if (pFS==NULL)
		return IO_OUT_OF_FILE_HANDLES;

//	if (*szFilePath=='\\') szFilePath++;
	res = LookupEntry(szFilePath, &pFS->dea, &de.dirEntry, &deaEmpty);
	switch (res)
	{
	case IO_MATCH_ENTRY: // file found
		break;
	case IO_EMPTY_ENTRY: // file not found, but an empty entry exists
		// check if we have to create a new empty file
		if (lFlags&IO_FILE_CREATE)
		{
			DeviceIoStamp t;
			m_pManager->GetClock()->GetDosStamp(t);

			::SetStamp(&de.dirEntry, t, true);
			de.dirEntry.cAttributes = (unsigned char)(FAT_ATTR_ARCHIVE | (lFlags&IO_FILE_STATE_MASK));
			SetStartCluster(&de.dirEntry, NULL_CLUSTER);
			de.dirEntry.lSize = 0;
			de.dirEntry.cReservedNT = 0;
//			if (SetDosFilename(&de.dirEntry, szFilePath)==0) LookupEntry filles this item
//				return IO_ILLEGAL_FILENAME;
			res = Update(deaEmpty, &de);
			if (res<IO_OK)
				return res;

			pFS->dea = deaEmpty; // copy new directory entry to file state structure

			lFlags &= ~IO_FILE_RESET; // clear rest flag because we're sure the file is empty
		}
		break;
	default:
		return res;
	}

	// check if we are allowed to modify (IO_FILE_RESET implies IO_FILE_WRITABLE)
	if ((lFlags&(IO_FILE_WRITABLE)) && (de.dirEntry.cAttributes&(FAT_ATTR_READONLY|FAT_ATTR_VOLUMEID|FAT_ATTR_DIRECTORY)))
		return IO_CANNOT_OPEN;

	if (lFlags&(IO_FILE_RESET))
	{
		res = m_fat.UnlinkChain(GetStartCluster(&de.dirEntry));
		if (res<IO_OK)
			return res;
		SetStartCluster(&de.dirEntry, NULL_CLUSTER/*EOF*/);
		de.dirEntry.lSize = 0;
	}
	pFS->lFileSize = de.dirEntry.lSize;
	pFS->fa.m_lCluster = pFS->lStartCluster = GetStartCluster(&de.dirEntry);
	pFS->fa.m_iSectorOffset = 0;
	pFS->pos = 0;
	ASSERT(pFS->pData==NULL);
	pFS->pData = NULL;
	pFS->lFlags = lFlags;

	return ioFile.Connect(this,(IO_HANDLE)pFS);
//	*pDriverData = (IO_HANDLE)pFS; // its now save to return a file handle to the caller
}

IO_RESULT DeviceIoDriver_FAT::CloseFile(IO_HANDLE pDriverData)
{
	TRACEUFS0("close file\n");

	if (pDriverData==NULL)
		return IO_INVALID_HANDLE;

#ifdef _DEBUG
	((FileState_FAT*)pDriverData)->AssertValid();
#endif

	IO_RESULT res = Flush(pDriverData);
	((FileState_FAT*)pDriverData)->lFlags = IO_FILE_UNUSED; // and release the file handle

#ifdef _DEBUG
	((FileState_FAT*)pDriverData)->AssertValid();
#endif
	return res;
}

IO_RESULT DeviceIoDriver_FAT::Flush(IO_HANDLE pDriverData)
{
	TRACEUFS0("flush file\n");

	// Write (open) buffer to cache and update directory entry.
	if (pDriverData==NULL)
		return IO_ERROR;
	IO_RESULT res = IO_ERROR;
	FileState_FAT* pFS = (FileState_FAT*)pDriverData;

#ifdef _DEBUG
	pFS->AssertValid();
#endif

	// there is data in the buffer; save it
	if (pFS->pData)
	{
		res = UnloadFatSector(pFS->pData/*, pFS->IsWritable()*/);
		ASSERT(res>=IO_OK);
		pFS->pData = NULL;
	}

	// Update directory entry.
	// If you skip this, you will loose clusters and the OS will report a short file
	res = Update(pFS->dea, pFS->lStartCluster, pFS->lFileSize);

#ifdef _DEBUG
	pFS->AssertValid();
#endif

	return res;
}

IO_RESULT DeviceIoDriver_FAT::GetFileSize(IO_HANDLE pDriverData, unsigned long& s)
{
	TRACEUFS0("get file size\n");

	// Write (open) buffer to cache and update directory entry.
	if (pDriverData==NULL)
		return IO_ERROR;
	s = ((FileState_FAT*)pDriverData)->lFileSize;
	return IO_OK;
}

IO_RESULT DeviceIoDriver_FAT::GetNrOfFreeSectors(const char* /*szPath*/, unsigned long& n)
{
	unsigned long t;
	const IO_RESULT res = m_fat.NumberOfFreeEntries(t);
	if (res>=IO_OK) n = t<<m_iSectorToClusterShift;
	return res;
}

IO_RESULT DeviceIoDriver_FAT::Flush()
{
	return m_fat.Flush();
}

IO_RESULT DeviceIoDriver_FAT::ReadFile(IO_HANDLE pDriverData, char* pBuf, unsigned int& n)
{
	TRACEUFS1_DET("read from file: %i bytes\n",n);

	if (pDriverData==NULL)
		return IO_INVALID_HANDLE;

	IO_RESULT res = IO_OK;
	FileState_FAT* pFS = (FileState_FAT*)pDriverData;

#ifdef _DEBUG
	pFS->AssertValid();
#endif

	unsigned int nBytesRead = 0;
	bool bEOF = false;
	if (pFS->pos+n>pFS->lFileSize)
	{
		bEOF = true;
		n = pFS->lFileSize - pFS->pos;
	}
	// determine how many bytes we can read from current sector
	unsigned int posWithinSector = pFS->pos & (SECTOR_SIZE-1);
//	unsigned int posWithinSector = pFS->pos % SECTOR_SIZE;
	unsigned int maxReadWithinSector = SECTOR_SIZE-posWithinSector;
	while (n>0)
	{
		const unsigned int nBytesToRead = n>=maxReadWithinSector ? maxReadWithinSector : n;
		if (pFS->pData==NULL)
		{
			res = LoadFatSector(pFS->fa, &pFS->pData, pFS->IsWritable(), true);
			if (res<IO_OK)
			{
				pFS->pData = NULL;
				goto _exit;
			}
		}
		memcpy(pBuf,pFS->pData+posWithinSector, nBytesToRead);
		// forward file position
		res = Seek(pFS, seekCurrent, nBytesToRead); // will release sector
		if (res<IO_OK)
		{
			goto _exit;
		}
		pBuf+=nBytesToRead;
		n-=nBytesToRead;
		nBytesRead+=nBytesToRead;
		maxReadWithinSector = SECTOR_SIZE; // can read complete sectors from now on
		posWithinSector = 0;
	}
_exit:
#ifdef _DEBUG
	pFS->AssertValid();
#endif
	n = nBytesRead; // inform user about how many bytes are actually read
	return res>=IO_OK && bEOF ? IO_EOF : res; // TODO: put EOF in separate bit
}

IO_RESULT DeviceIoDriver_FAT::WriteFile(IO_HANDLE pDriverData, const char* pBuf, unsigned int& n)
{
	TRACEUFS1_DET("write to file: %i bytes\n",n);

	unsigned int posWithinSector;
	unsigned int maxWriteWithinSector;
	bool bPreLoad = true;

	if (pDriverData==NULL)
		return IO_INVALID_HANDLE;

	IO_RESULT res = IO_OK;
	FileState_FAT* pFS = (FileState_FAT*)pDriverData;

#ifdef _DEBUG
	pFS->AssertValid();
#endif

	if (!pFS->IsWritable())
	{
		n = 0;
		return IO_CANNOT_WRITE_FILE; // read only file
	}

	if (pFS->pos>pFS->lFileSize)
	{
		n = 0;
		return IO_INVALID_FILE_POS;
	}
	unsigned int nBytesWritten = 0;
	const unsigned long oldFileSize = pFS->lFileSize;
	// check if we must increase file size
//	unsigned long lNewSize = pFS->pos+n;
	bool bGrow = false;
	unsigned long newSize = pFS->pos+n;
	//check addition for overflow
	if (newSize<pFS->pos)
	{
		ASSERT(0);
		newSize = 0xFFFFFFFF - pFS->pos; // limit to 4G
	}
	if (newSize>pFS->lFileSize) 
	{
		res = m_fat.Grow(pFS->lStartCluster, pFS->lFileSize, newSize-pFS->lFileSize, pFS->fa.m_lCluster/*last cluster hint*/);
		if (res<IO_OK)
			goto _exit;
		ASSERT(m_fat.ValidFatValue(pFS->lStartCluster));
		bGrow = true;
		// check if this is an empty file that is being expanded
		if (pFS->lFileSize==0)
		{
			ASSERT(pFS->fa.m_lCluster==NULL_CLUSTER);
			ASSERT(pFS->pos==0);
			pFS->fa.m_lCluster = pFS->lStartCluster;
			ASSERT(pFS->fa.m_iSectorOffset==0);
			bPreLoad = false;
		}
		// special case: when pos was at EOF and on a sector boundary
		else if (pFS->lFileSize==pFS->pos && (pFS->lFileSize&(SECTOR_SIZE-1))==0) 
		{
			// In this case 'pos' pointed beyond the last valid sector,
			// but since the cluster chain has grown it is now save to move on when
			// m_iSectorOffset was set to #sectors_per_cluster in Seek
			ASSERT(m_fat.ValidClusterIndex(pFS->fa.m_lCluster));
			if (pFS->fa.m_iSectorOffset>=GetNrOfSectorsPerCluster())
			{
				pFS->fa.m_iSectorOffset = 0;
				res = m_fat.GetEntry(pFS->fa.m_lCluster, pFS->fa.m_lCluster);
				ASSERT(m_fat.ValidClusterIndex(pFS->fa.m_lCluster));
				if (res<IO_OK)
				{
					ASSERT(0);
					goto _exit;
				}
			}
			bPreLoad = false;
		}
//		ASSERT(pFS->fa.m_lCluster==pFS->lStartCluster);
		pFS->lFileSize = newSize;
	}

	// determine how many bytes we can read from current sector
	posWithinSector = pFS->pos & (SECTOR_SIZE-1);
//	posWithinSector = pFS->pos % SECTOR_SIZE;
	maxWriteWithinSector = SECTOR_SIZE-posWithinSector;
	while (n>0)
	{
		const unsigned int nBytesToWrite = n>=maxWriteWithinSector ? maxWriteWithinSector : n;
		if (pFS->pData==NULL)
		{
			res = LoadFatSector(pFS->fa, &pFS->pData, true, (bPreLoad && nBytesToWrite!=SECTOR_SIZE) ); // preserve content of partially written sectors
			if (res<IO_OK)
			{
				pFS->pData = NULL;
				goto _exit;
			}
		}
		memcpy(pFS->pData+posWithinSector, pBuf, nBytesToWrite);
		// forward file position
		res = Seek(pFS, seekCurrent, nBytesToWrite); // will release sector
		if (res<IO_OK)
		{
			goto _exit;
		}
		pBuf+=nBytesToWrite;
		n-=nBytesToWrite;
		nBytesWritten+=nBytesToWrite;
		maxWriteWithinSector = SECTOR_SIZE; // can read complete sectors from now on
		posWithinSector = 0;
		if (pFS->pos>=oldFileSize)
			bPreLoad = false; // no need to load next sector when writing beyond EOF
	}
_exit:
	n = nBytesWritten; // inform user about how many bytes are actually written
#ifdef _DEBUG
	pFS->AssertValid();
#endif
	return res;
}

IO_RESULT DeviceIoDriver_FAT::Tell(IO_HANDLE pDriverData, unsigned long& pos)
{
	if (pDriverData==NULL)
		return IO_INVALID_HANDLE;

	FileState_FAT* pFS = (FileState_FAT*)pDriverData;
#ifdef _DEBUG
	pFS->AssertValid();
#endif
	pos = pFS->pos;
	ASSERT(pos<=pFS->lFileSize);
	TRACEUFS1_DET("tell %lu\n",pos);
	return IO_OK;
}

IO_RESULT DeviceIoDriver_FAT::Seek(IO_HANDLE pDriverData, seekMode mode, long offset)
{
	// Seek a file postion (byte offset) in existing cluster chain!
	// Note the seek to EOF means that the file position becomes equal to file size in bytes.

	// This function should be optimal for forward seeks (sequential access) and rewinds.

	if (pDriverData==NULL)
		return IO_INVALID_HANDLE;

	IO_RESULT res = IO_ERROR;
	FileState_FAT* pFS = (FileState_FAT*)pDriverData;

#ifdef _DEBUG
	pFS->AssertValid();
#endif

	unsigned long seekPos;
	switch (mode)
	{
	case seekBegin: 
		seekPos = (unsigned long)offset;
		break;

	case seekCurrent:
		seekPos = pFS->pos + (unsigned long)offset;
		break;

	case seekEnd: // NB pos should be zero or negative
		seekPos = (offset>0 || ((unsigned long)-offset)>pFS->lFileSize) ? -1 : (pFS->lFileSize - (unsigned long)offset);
		break;
	}

	TRACEUFS1_DET("seek %lu\n",seekPos);

	if (seekPos>pFS->lFileSize) 
		return IO_INVALID_FILE_POS;

	if (seekPos==pFS->pos && pFS->pData!=NULL)
		return IO_OK; // current pos OK && data sector locked

	// compare 'distance' in sector units
	const unsigned char nShift = GetByteToSectorShift();
	const unsigned long currentLogicalSector = pFS->pos>>nShift;
	const unsigned long seekLogicalSector = seekPos>>nShift;
//	const unsigned long currentLogicalSector = pFS->pos/SECTOR_SIZE;
//	const unsigned long seekLogicalSector = seekPos/SECTOR_SIZE;

	// do we need to explore new unknown space; where no one has gone before...?
	if (seekLogicalSector!=currentLogicalSector)
	{
		unsigned long currentLogicalCluster = currentLogicalSector>>m_iSectorToClusterShift;
		const unsigned long seekLogicalCluster = seekLogicalSector>>m_iSectorToClusterShift;

		// release the current sector
		if (pFS->pData!=NULL)
		{
			const bool bSave = pFS->IsWritable();
			res = UnloadFatSector(pFS->pData/*, bSave*/);
			ASSERT(res>=IO_OK);
			pFS->pData = NULL;
		}

		if (seekLogicalSector<currentLogicalSector)
		{
			// hmmm... rewind and walk upstream
			currentLogicalCluster = 0;
			pFS->fa.m_lCluster = pFS->lStartCluster; // zero for empty files
			pFS->fa.m_iSectorOffset = 0;
		}
		// walk the FAT upstream from here
		while (currentLogicalCluster!=seekLogicalCluster)
		{
			// realize that we have logical cluster and sectors and REAL ones
			ASSERT(pFS->fa.m_lCluster!=0); // file cannot be empty at this point
			unsigned long next = 0;
			res = m_fat.GetEntry(pFS->fa.m_lCluster, next);
			if (!m_fat.ValidClusterIndex(next))
			{
				if (m_fat.IsEofClusterValue(next) && currentLogicalCluster+1==seekLogicalCluster)
				{
					// This (premature) EOF situation occurs when you seek
					// to end of file and the file size is exactly a multiple of the
					// cluster size (512, 1024, ...). This is because the file
					// pointer (pos) 'points' to the next byte to read or write.
					// Solution: let pFS->fa.m_lCluster hold the index of the last
					// cluster (containing eof) because this cluster is used as hint 
					// during chain extension (ie. FileWrite).
					// File read operation are no problem in this situation since
					// we are at EOF.
					ASSERT((seekPos&((SECTOR_SIZE<<m_iSectorToClusterShift)-1))==0 && seekPos==pFS->lFileSize);
					pFS->fa.m_iSectorOffset = GetNrOfSectorsPerCluster(); // one beyond valid number == #of sectors per cluster
					goto _done;
				}
				ASSERT(0);
				res = IO_CORRUPT_FAT; // corrupt FAT or beyond EOF seek
				goto _exit;
			}
			pFS->fa.m_lCluster = next;
			currentLogicalCluster++;
		}
		// Also fill the sector offset in pFS->fa address to make it complete
		pFS->fa.m_iSectorOffset = (unsigned short)(seekLogicalSector & (BIT_SHIFT_TO_N(m_iSectorToClusterShift)-1));
	}
_done:
	// Cluster nr and sector offset are known and available in pFS->fa
	// Note the special case: cluster nr==0 is only valid when seekPos==0 (seek begin empty file)
	// and another: m_iSectorOffset will be #sectors per cluster when a file is exactly a multiple clusters in size

	pFS->pos = seekPos;

#ifdef _DEBUG
	pFS->AssertValid();
#endif
	return IO_OK;

_exit:
#ifdef _DEBUG
	pFS->AssertValid();
#endif
	ASSERT(0);
	// TODO: set pFS to appropriate error state
	return res;
}

#ifdef FAT_DUMP
#ifdef _DEBUG
void DeviceIoDriver_FAT::Dump()
{
	IO_RESULT res = IO_OK;

	// test cache
//	LoadSectorR(0, &buf);
//	UnloadSectorR(buf);

	// dump FAT
	m_fat.Dump();

	TRACEUFS0("BEGIN ROOT Dump:\n");
	// read root directory
	bool bEOD = false;
	char dosname[13];
	const int nEntriesPerSector = SECTOR_SIZE/sizeof(DirEntry);
	for (int i=0; i<m_nRootDirEntries && !bEOD; i+=nEntriesPerSector)
	{
		const DirEntry* dirBase = NULL;
		const DirEntry* dir = NULL;
		res = LoadSector(m_nReservedSectors + m_nFatCopies*m_nSectorsPerFat + (i/nEntriesPerSector), (char**)&dirBase, IO_READ_ONLY, true);
		if (res<IO_OK)
			return;
		dir=dirBase;
		for (int j=0; j<nEntriesPerSector && !bEOD; j++, dir++)
		{
			int len = 0;
			int k=0;

			TRACEUFS1("Dir entry %i: ",j);

			if (dir->cAttributes==FAT_ATTR_LFN)
			{
				TRACEUFS0("<LFN>\n");
			}
			else if (dir->cAttributes&FAT_ATTR_VOLUMEID)
			{
				GetDosVolumeID(dir,dosname,false);
				TRACEUFS1("VOLUME: %14s\n",dosname);
			}
			else
			{
				switch (dir->sName[0])
				{
				case FAT_FILE_EOD:
					bEOD = true;
					TRACEUFS0("<EOD>\n");
					break;
				case FAT_FILE_REMOVED:
					TRACEUFS0("<DEL>\n");
					break;
				case '.': // "." or ".."
//				case FAT_FILE_MAGIC_E5:
				default:
					len = GetDosFilename(dir,dosname);
					ASSERT(len>0);
					TRACEUFS4("%s %14s at cluster %li (#=%li)\n",((dir->cAttributes&FAT_ATTR_DIRECTORY)!=0?"DIR ":"FILE"),dosname,(long)GetStartCluster(dir),(long)dir->lSize);
				}
			}
		}
		UnloadSector((char*)dirBase/*, false*/);
	}
	TRACEUFS0("END ROOT Dump\n");

	// read FAT
//	m_fat.Dump();
}
#endif
#endif

