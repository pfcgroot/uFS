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
#include "uFS.h"
#include <string>

///////////////////////////////////////////////////////////////////////////////
// DeviceIoClock

IO_RESULT DeviceIoClock::GetDosStamp(DeviceIoStamp& t)
{
	// default implementation returns a fixed stamp
	t.msec = 0;
	t.sec = 0;
	t.min = 0;
	t.hour = 0;
	t.day = 0;
	t.month = 0;
	t.year = 1980;
	return IO_OK;
}



///////////////////////////////////////////////////////////////////////////////
// DeviceIoDriver

IO_RESULT DeviceIoDriver::LoadSector(unsigned long lba, char** pData, bool bWritable, bool bPreLoad)
{
	ASSERT(bWritable || bPreLoad); // read-only access is nonsence when not loading
	return m_pManager->LoadSector(m_pHal, lba, pData, bWritable, bPreLoad);
}

IO_RESULT DeviceIoDriver::UnloadSector(char* pData/*, bool bFlush*/)
{
	return m_pManager->UnloadSector(pData/*, bFlush*/);
}


///////////////////////////////////////////////////////////////////////////////
// DeviceIoManager

// static 
DeviceIoClock DeviceIoManager::m_defaultClock;

IO_RESULT DeviceIoManager::SetDriverFactory(DeviceIoDriverFactory* pFactory)
{
	m_pFactory = pFactory;
	return IO_OK;
}

#if MAX_ALLOWED_DRIVERS>1

DeviceIoDriver* DeviceIoManager::GetLastDriver()
{
	DeviceIoDriver* p = m_pFirstDriver;
	DeviceIoDriver* t = NULL;
	while (p!=NULL)
	{
		t = p;
		p = p->GetNextDriver();
	}
	return t;
}

DeviceIoDriver* DeviceIoManager::FindPrevDriver(DeviceIoDriver* pDriver)
{
	DeviceIoDriver* p = m_pFirstDriver;
	DeviceIoDriver* t = NULL;
	while (p!=NULL && pDriver!=p)
	{
		t = p;
		p = p->GetNextDriver();
	}
	return t;
}

void DeviceIoManager::AppendDriver(DeviceIoDriver* pAppend)
{
	DeviceIoDriver* p = GetLastDriver();
	if (p)
		p->SetNextDriver(pAppend);
	else
		m_pFirstDriver = pAppend;
	pAppend->SetNextDriver(NULL);
}

#endif // #if MAX_ALLOWED_DRIVERS>1


IO_RESULT DeviceIoManager::CreateDriver(BlockDeviceInterface* pHal, DeviceIoDriver*& pDriver, const char* szDriverID, bool bRootDevice)
{
	if (m_pFactory==NULL)
		return IO_ERROR;

	pDriver = m_pFactory->AllocateDriver(szDriverID==NULL||*szDriverID=='\0' ? pHal->GetDriverID() : szDriverID);
	if (pDriver==NULL)
		return IO_FAILED_TO_LOAD_DRIVER;

	pDriver->SetDeviceIoManager(this);

#if MAX_ALLOWED_DRIVERS>1
	if (bRootDevice)
		AppendDriver(pDriver);
#else
	ASSERT(!bRootDevice || m_pFirstDriver==NULL);
	if (bRootDevice)
		m_pFirstDriver = pDriver;
#endif

//	IO_RESULT res = pDriver->OnMount(pHal); // note that this call may trigger unmount sub file systems (ATA->FAT)
//	if (res==IO_OK)
//		AppendDriver(pDriver);
//	else
//		m_pFactory->ReleaseDriver(pDriver);
//	return res;

	return IO_OK;
}

IO_RESULT DeviceIoManager::ReleaseDriver(DeviceIoDriver* pDriver)
{
	if (pDriver==NULL)
		return IO_ERROR;

	// prevent user from accessing the drive from here
	IO_RESULT res = pDriver->Lock();
	if (res<IO_OK)
		return res;

#if MAX_ALLOWED_DRIVERS>1
	// remove driver from chain
	DeviceIoDriver* pPrevDriver = FindPrevDriver(pDriver);
	if (pPrevDriver)
		pPrevDriver->SetNextDriver(pDriver->GetNextDriver());
	else if (m_pFirstDriver==pDriver)
		m_pFirstDriver = pDriver->GetNextDriver();
	else
		; // not in driver chain
	pDriver->SetNextDriver(NULL);
#else
//	ASSERT(pDriver==m_pFirstDriver);
	m_pFirstDriver = NULL;
#endif

	// and unmount
	res = pDriver->UnmountSW(); // note that this call may unmount sub file systems (ATA->FAT)

	// and finally release any driver related resources
	m_pFactory->ReleaseDriver(pDriver);

	return res;
}

DeviceIoDriver* DeviceIoManager::GetFS(const char* szPath, const char** szPart2)
{
	// search device driver that matches the device ID in first part of path:
	// \ATA\0\mydir\myfile (i.e. \DriverID\partition\filepath

	DeviceIoDriver* p = m_pFirstDriver;
	while (p)
	{
		const char* szID = p->m_pHal->GetDriverID();
		const int len = szID ? strlen(szID) : 0;
		if (szPath[0]=='\\' && szPath[len+1]=='\\' && strnicmp(szPath+1, szID, len)==0)
		{
			if (szPart2)
				*szPart2 = szPath+len+1;
			break;
		}
#if MAX_ALLOWED_DRIVERS>1
		p = p->GetNextDriver();
#else
		p = NULL;
#endif
	}
	return p;
}

// partition/volume info
IO_RESULT DeviceIoManager::GetVolumeID(int i, char* szVolumeID) const
{
	DeviceIoDriver* p = m_pFirstDriver;
	int n = 0;
	szVolumeID[0] = '\0';
	while (p)
	{	int t = p->GetNrOfVolumes();
		if (i<n+t)
		{
			*szVolumeID = '\\';
			strcpy(szVolumeID+1, p->m_pHal->GetDriverID());
			szVolumeID += strlen(szVolumeID);
			i -= n;
			ASSERT(i>=0 && i<10);
			*szVolumeID++ = '\\';
			*szVolumeID++ = '0' + i;
			*szVolumeID = '\0';
			return IO_OK;
		}
		n += t;
#if MAX_ALLOWED_DRIVERS>1
		p = p->GetNextDriver();
#else
		p = NULL;
#endif
	}
	return IO_ERROR;
}

int DeviceIoManager::GetNrOfVolumes() const
{
	DeviceIoDriver* p = m_pFirstDriver;
	int n = 0;
	while (p)
	{
		n += p->GetNrOfVolumes();
#if MAX_ALLOWED_DRIVERS>1
		p = p->GetNextDriver();
#else
		p = NULL;
#endif
	}
	return n;
}


IO_RESULT DeviceIoManager::OpenFile(const char* szPath, DeviceIoFile& f, unsigned long lFlags)
{
	TRACEUFS1("open or create file %s\n",szPath);
	IO_RESULT res = IO_DEVICE_NOT_FOUND;
	DeviceIoDriver* p = GetFS(szPath, &szPath);
//	f.m_pDriver = NULL;
//	f.m_pDriverData = NULL;
	if (p)
	{
		res = p->OpenFile(szPath, f, lFlags);
//		if (res>=IO_OK)
//			f.m_pDriver = p;
//		else
//			f.m_pDriverData = NULL; // just in case
	}
	return res;
}

IO_RESULT DeviceIoManager::FileExist(const char* szFilename)
{	
	// Get the driver and the remaining 2nd part of the filename
	IO_RESULT res = IO_DEVICE_NOT_FOUND;
	DeviceIoDriver* p = GetFS(szFilename, &szFilename);

	// Does the file exist?
	if (p) res = p->FileExist(szFilename);

	// return result
	return res;
}

IO_RESULT DeviceIoManager::CreateDirectory(const char* szPath)
{
	TRACEUFS1("create directory %s\n",szPath);
	IO_RESULT res = IO_DEVICE_NOT_FOUND;
	DeviceIoDriver* p = GetFS(szPath, &szPath);
	if (p)
		res = p->CreateDirectory(szPath);
	return res;
}

IO_RESULT DeviceIoManager::DeleteFile(const char* szPath, unsigned long lFlags)
{
	TRACEUFS1("delete file %s\n",szPath);
	IO_RESULT res = IO_DEVICE_NOT_FOUND;
	DeviceIoDriver* p = GetFS(szPath, &szPath);
	if (p)
		res = p->DeleteFile(szPath, lFlags);
	return res;
/*
	IO_RESULT res = IO_DEVICE_NOT_FOUND;
	DeviceIoDriver* p = m_pFirstDriver;
	int iFS = -1;
	while (p)
	{
		const char* szID = p->m_pHal->GetDriverID();
		const int len = szID ? strlen(szID) : 0;
		if (szPath[0]=='\\' && szPath[len+1]=='\\' && strnicmp(szPath+1, szID, len)==0)
		{
			res = p->DeleteFile(szPath+len+1, lFlags);
			break;
		}
#if MAX_ALLOWED_DRIVERS>1
		p = p->GetNextDriver();
#else
		p = NULL;
#endif
	}
	return res;
*/
}

IO_RESULT DeviceIoManager::GetNrOfFreeSectors(const char* szPath, unsigned long& n)
{
	IO_RESULT res = IO_DEVICE_NOT_FOUND;
	DeviceIoDriver* p = GetFS(szPath, &szPath);
	if (p)
		res = p->GetNrOfFreeSectors(szPath, n);
	return res;
}

IO_RESULT DeviceIoManager::GetNrOfSectors(const char* szPath, unsigned long& n)
{
	IO_RESULT res = IO_DEVICE_NOT_FOUND;
	DeviceIoDriver* p = GetFS(szPath, &szPath);
	if (p)
		res = p->GetNrOfSectors(szPath, n);
	return res;
}

IO_RESULT DeviceIoManager::Flush()
{
	DeviceIoDriver* p = m_pFirstDriver;

	IO_RESULT res = m_blockDeviceCache.Flush();
	if(res<IO_OK)
		return res;
	
	while (p)
	{
		p->Flush();
#if MAX_ALLOWED_DRIVERS>1
		p = p->GetNextDriver();
#else
		p = NULL;
#endif
	}
}

///////////////////////////////////////////////////////////////////////////////
// BlockDeviceCache

void BlockDeviceCache::Reset()
{
	m_timeNow = 0;
	for(int i=0;i<CACHE_SIZE;i++)
		m_entries[i].Reset();
}

char* BlockDeviceCache::Lock(BlockDeviceInterface* pDev, unsigned long lba, bool bWritable, bool bPreLoad, unsigned long timeout)
{
	int i;
	char* p = NULL;
	CacheEntry* pBestMatch = NULL;
	for (i=0; i<sizeof(m_entries)/sizeof(m_entries[0]); i++)
	{
		p = m_entries[i].LockDataOnMatch(pDev, lba, bWritable, timeout);
		if (p) return p;
	}
	for (i=0; i<sizeof(m_entries)/sizeof(m_entries[0]); i++)
	{
		CacheEntry* e = &m_entries[i];
		if (e->LockEntry(0))
		{
			if (e->IsFree())
			{

//				TRACEUFS2("cache #%i: time = %i\n",i,e->GetLastAccessTime());

				if (pBestMatch==NULL)
				{
					pBestMatch = e;
				}
				else
				{
					unsigned t = e->GetLastAccessTime();
					unsigned tBest = pBestMatch->GetLastAccessTime();
					if ( (tBest<=m_timeNow && t<m_timeNow && t<tBest) || 
						 (t>m_timeNow && tBest<=m_timeNow) || 
						 (tBest>m_timeNow && t>m_timeNow && t<tBest)
						 ) // note: time will eventually overflow
					{
						pBestMatch->UnlockEntry();
						pBestMatch = e;
					}
					else
						e->UnlockEntry();
				}
			}
			else
				e->UnlockEntry();
		}
	}
	if (pBestMatch)
		p = pBestMatch->LockDataIfFree(pDev, lba, bWritable, bPreLoad, timeout, true);
#ifdef TRACE_UFS_CACHE
//	TRACEUFS2("Cache %i: lba=%li\n",(pBestMatch?((int)(pBestMatch)-(int)m_entries)/sizeof(m_entries[0]):-1),lba);
#ifdef _DEBUG
	TRACEUFS1("Cached  lba=%6ld:",lba);
	for (i=0; i<sizeof(m_entries)/sizeof(m_entries[0]); i++)
		m_entries[i].Trace();
	TRACEUFS0("\n");
#endif
#endif
	return p;
}


IO_RESULT BlockDeviceCache::Unlock(char* pData/*, bool bFlush*/)
{
	IO_RESULT res;
	for (int i=0; i<sizeof(m_entries)/sizeof(m_entries[0]); i++)
	{
		res = m_entries[i].Unlock(pData/*, bFlush*/, ++m_timeNow);
		if (res!=IO_NOMATCH_ENTRY)
			return res; // either OK or some error
	}
	ASSERT(0); // you're unlocking something that ain't cached failed!
	return IO_NOMATCH_ENTRY;
}

IO_RESULT BlockDeviceCache::Flush()
{
	IO_RESULT res;
	for (int i=0; i<sizeof(m_entries)/sizeof(m_entries[0]); i++)
	{
		res = m_entries[i].Flush();
		if (res<IO_OK)
			return res; // either OK or some error
	}
	return IO_OK;
}

void BlockDeviceCache::CacheEntry::Reset()
{
#ifdef _DEBUG
	m_lMagicBefore = m_lMagicAfter = CACHE_MAGIC_VALUE;
#endif
	m_lba = 0;
	m_pDev = NULL;
	m_lockData = 0;
	m_lockEntry = 0;
	m_tLastAccessTime = 0;
	m_bWritable = false;
}

#ifdef _DEBUG

void BlockDeviceCache::CacheEntry::AssertValid()
{
	ASSERT(m_lMagicBefore==CACHE_MAGIC_VALUE);
	ASSERT(m_lMagicAfter==CACHE_MAGIC_VALUE);
}

void BlockDeviceCache::CacheEntry::Trace() const
{
	TRACEUFS2("  %6d %c",m_lba,(m_lockData?'L':'U'));
}
#endif

char* BlockDeviceCache::CacheEntry::LockDataOnMatch( BlockDeviceInterface* pDev, unsigned long lba, bool bWritable, unsigned long timeout)
{
	char* p = NULL;
	LockEntry(timeout);
	ASSERT_ME;
	if (m_pDev==pDev && m_lba==lba)
	{
#ifdef TRACE_UFS_CACHE
		TRACEUFS1("Cache hit (lba==%li)\n",lba);
#endif
		m_bWritable = m_bWritable || bWritable; // never clear the writable state
		p = LockData(timeout);
	}
	ASSERT_ME;
	UnlockEntry();
	return p;
}

char* BlockDeviceCache::CacheEntry::LockDataIfFree( BlockDeviceInterface* pDev, unsigned long lba, bool bWritable, bool bPreLoad, unsigned long timeout, bool bEntryIsLocked)
{
	IO_RESULT res = IO_ERROR;
	char* p = NULL;
	if (!bEntryIsLocked)
		LockEntry(timeout);
	ASSERT_ME;
	if (IsFree())
	{
		p = LockData(timeout);
		if (bPreLoad) // don't read sectors that are overwritten (i.e. extending a file)
		{
#ifdef TRACE_UFS_CACHE
			TRACEUFS1("Reading lba=%li\n",lba);
#endif
			res = pDev->ReadSector(lba, p);
		}
		else
		{
#ifdef TRACE_UFS_CACHE
			TRACEUFS1("Locking lba=%li (no preload)\n",lba);
#endif
			res = IO_OK;
		}
		if (res>=IO_OK)
		{
			m_pDev=pDev;
			m_lba=lba;
			m_bWritable=bWritable;
			ASSERT(m_pData==p);
		}
		else
		{
			UnlockData();
			p = NULL;
			TRACEUFS1("ERROR: couldn't read lba=%li from disk\n",lba);
		}
	}
	ASSERT_ME;
	UnlockEntry();
	return p;
}

IO_RESULT BlockDeviceCache::CacheEntry::Unlock(char* pData/*, bool bFlush*/, unsigned timeNow)
{
	IO_RESULT res = IO_NOMATCH_ENTRY;
	LockEntry();
	ASSERT_ME;
	//ASSERT(pData==m_pData);
	if (pData==m_pData)
	{
		if (m_bWritable)
		{
			//ASSERT(m_bWritable);
#ifdef TRACE_UFS_CACHE
			TRACEUFS1("Writing lba=%li\n",m_lba);
#endif
			res = m_pDev->WriteSector(m_lba, m_pData);
		}
		else
		{
			//ASSERT(!m_bWritable);
#ifdef TRACE_UFS_CACHE
			TRACEUFS1("Destroy lba=%li\n",m_lba);
#endif
			res = IO_OK;
		}
		if (res>=IO_OK)
		{
			UnlockData();
			m_tLastAccessTime = timeNow;
		}
		else
			TRACEUFS1("ERROR: couldn't write lba=%li to disk\n",m_lba);
	}
	ASSERT_ME;
	UnlockEntry();
	return res;
}

IO_RESULT BlockDeviceCache::CacheEntry::Flush()
{
	IO_RESULT res = IO_OK;
//	LockEntry();
	ASSERT_ME;
	//ASSERT(pData==m_pData);
//	if (pData==m_pData)
//	{
		if (m_bWritable && m_tLastAccessTime!=0)
		{
			//ASSERT(m_bWritable);
#ifdef TRACE_UFS_CACHE
			TRACEUFS1("Writing lba=%li\n",m_lba);
#endif
			res = m_pDev->WriteSector(m_lba, m_pData);
		}
/*		else
		{
			//ASSERT(!m_bWritable);
#ifdef TRACE_UFS_CACHE
			TRACEUFS1("Destroy lba=%li\n",m_lba);
#endif
			res = IO_OK;
		}
		if (res>=IO_OK)
		{
			UnlockData();
			m_tLastAccessTime = timeNow;
		}
		else
			TRACEUFS1("ERROR: couldn't write lba=%li to disk\n",m_lba);
	}
	ASSERT_ME;
	UnlockEntry();
*/	return res;
}

bool BlockDeviceCache::CacheEntry::LockEntry(unsigned long timeout)
{
#ifdef UFS_MULTI_THREADED
	// TODO: must implement timed semaphores for multithreaded apps.
	// For the time being we just fail immediatly if entry is locked!
	timeout = 0;

	while (m_lockEntry) 
		if (timeout==0) 
			return false;
#else
	ASSERT(m_lockEntry==0);
#endif
	ASSERT_ME;
	m_lockEntry=1;
	return true;
}

char* BlockDeviceCache::CacheEntry::LockData(unsigned long timeout)
{
#ifdef UFS_MULTI_THREADED
	// TODO: must implement timed semaphores for multithreaded apps.
	// For the time being we just fail immediatly if entry is locked!
	timeout = 0;

	while (m_lockData) 
		if (timeout==0) 
			return NULL;
#else
	ASSERT(m_lockData==0);
#endif
	m_lockData=1;
	return m_pData;
}


///////////////////////////////////////////////////////////////////////////////
// DeviceIoFile

IO_RESULT DeviceIoFile::Connect(DeviceIoDriver* pDriver, void* pDriverData)
{
	if (m_pDriver==NULL)
	{
		m_pDriver = pDriver;
		m_pDriverData = pDriverData;
		m_lLastResult = IO_OK;
	}
	else 
		m_lLastResult = IO_FILE_OPEN;
	return m_lLastResult;
}

IO_RESULT DeviceIoFile::Close()
{
	// record any errors, but continue in any case (must close the file)
	if (m_pDriver)
	{
		IO_RESULT res = IO_ERROR;
		res = Flush();
		if (res<IO_OK && m_lLastResult>=IO_OK)
			m_lLastResult = res;
		res = m_pDriver->CloseFile(m_pDriverData);
		if (res<IO_OK && m_lLastResult>=IO_OK)
			m_lLastResult = res;
	}
	else 
		if (m_lLastResult==IO_OK)
			m_lLastResult = IO_ALREADY_CLOSED;
	m_pDriver = NULL;
	m_pDriverData = NULL;
	return m_lLastResult;
}

