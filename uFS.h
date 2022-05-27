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
/* This header file contains class definitions for managing a modular       */
/* file system. You should derive new driver implementations from the       */
/* DeviceIoDriver class. Drivers for ATA disk and FAT12/16/32 partitions    */
/* are provided.                                                            */
/*                                                                          */
/* This implementation was designed with the following specifications       */
/* as a starting point:                                                     */
/*                                                                          */
/* - Minimize required memory resources (<2KB)                              */
/* - Don't use dynamic memory allocation (new/delete, malloc/free)          */
/* - Abstract interface to hardware layer                                   */
/* - No OS dependecy                                                        */
/* - Modular design to support different partition formats                  */
/* - Minimize nr. of I/O operations to minimize devices' power consumption  */
/* - At least support creation of files in root directory                   */
/* - Code should at least be compatible with Visual C++ and ARM C++ tools   */
/* - Integrate support for testing the code using virtual disks under win32 */
/* - Final code should support CompactFlash cards driven by ARM RISC uProc  */
/* - Stay away from 'extreme C++' (exceptions, templates, ...)              */
/*                                                                          */
/* Since a high quality C++ cross compiler was available for both target    */
/* and development system, there was no need to stick to ANSII C.           */
/* Using C++ has made it much easier to implement a very modular 'file      */
/* system'. However, because this system was designed with embedded         */
/* applications in mind, the final implementation cannot be compared        */
/* with robust and complete file systems found in many mature OSs.          */
/* Another reason for not implementing a full blown FS is that our main     */
/* goal was only to interface with CompactFlash cards that contain at least */
/* one FAT partition.                                                       */
/*                                                                          */
/* One important design consideration was the implementation of a caching   */
/* module. The caching system I use solves a few rather important problems, */
/* which relate to our embedded application. The caching module uses        */
/* a fixed memory pool, which is used to hold the information that is       */
/* read from (or written to) the sectors on disk. Nothing new here, you     */
/* probably think, but in this implementation it is possible to lock        */
/* a sector into the cache and directly manipulate its content. No need     */
/* to copy lots of data between the user's code and the cache. The main     */
/* adventage with this setup is that the memory requirements are very       */
/* predictable (i.e. no large chunks of data pushing and popping the stack).*/
/* The number of sectors that can be cached simultaneously can be conf.     */
/* using the CACHE_SIZE macro.                                              */
/* Another important property of the cache is that it reduces the number    */
/* of disk read and write operations. Especialy during FAT modifications.   */
/*                                                                          */
/* The current implementation has the following known limitations:          */
/* - Most functions are NOT (yet) re-entrant, i.e. NOT (yet) thread-safe.   */
/* - Only devices with sectors of 512 bytes are supported.                  */
/* - No long filename support in the FAT drivers yet.                       */
/*                                                                          */
/* Most interfaces return an error status (IO_ERROR), which is negative     */
/* when an error has occured. A value >=0 indicates success.                */
/*                                                                          */
/****************************************************************************/

#ifndef __uFS_h
#define __uFS_h


#ifdef _MSC_VER
// disable warning C4355: 'this' : used in base member initializer list
#pragma warning( disable : 4355)
#endif

#ifndef ASSERT
#define ASSERT(a)
#endif

#define SECTOR_SIZE 512			// cluster size in bytes; other sizes not supported
#define CACHE_SIZE 4			// size (in sectors) of static cache. At least MAX_OPEN_FAT_FILES+1.
								// Also see MAX_OPEN_FAT_FILES in uFS_FAT.h
#define MAX_ALLOWED_DRIVERS 1	// max. nr of 'root' devices (i.e. ATA drivers)
								// note that each a driver can hold zero or more
								// sub drivers (i.e. ATA contains up to FAT drivers)

#define IO_RESULT int			// error return code (see IO_xxx below)
#define IO_HANDLE void*			// abstract file handle

#define IO_READ_ONLY	false
#define IO_WRITABLE		true

// Positive IO return values just return information, negative values 
// indicate an error has occured
#define IO_OK						0
#define IO_EOF						1 
#define IO_MATCH_ENTRY				2
#define IO_EMPTY_ENTRY				3
#define IO_FILE_OR_DIR_EXISTS		4
#define IO_ALREADY_CLOSED			5
#define IO_NOMATCH_ENTRY			6
#define IO_ERROR					-1			// recoverable errors start here
#define IO_DISK_FULL				-2
#define IO_FILE_NOT_FOUND			-3
#define IO_CORRUPT_BOOT_REC			-4
#define IO_CANNOT_OPEN				-5
#define IO_CANNOT_WRITE_FILE		-6 
#define IO_INVALID_FILE_POS			-7
#define IO_FILE_OPEN				-8
#define IO_OUT_OF_FILE_HANDLES		-9
#define IO_DEVICE_NOT_FOUND			-10
#define IO_ILLEGAL_DEVICE			-11
#define IO_ILLEGAL_FILENAME			-12
#define IO_NOT_A_DIRECTORY			-13
#define IO_DIRECTORY_NOT_EMPTY		-14
#define IO_WRONG_ATTRIBUTES			-15
#define IO_CORRUPT_FAT				-100		// serious errors start here (i.e. non recoverable)
#define IO_ILLEGAL_LBA				-101
#define IO_FAILED_TO_LOAD_DRIVER	-102
#define IO_UNSUPPORTED_SECTOR_SIZE	-103
#define IO_INVALID_HANDLE			-104
#define IO_INVALID_CLUSTER			-105
#define IO_UNKNOWN_PARTITION_TYPE	-106
#define IO_CANNOT_WRITE_SECTOR		-107
#define IO_CANNOT_READ_SECTOR		-108

// Device driver types
//#define IO_DRIVER_TYPE_FILE_SYTEM	1

// File open/creation flags
#define IO_FILE_WRITABLE	0x00010000
#define IO_FILE_RESET		0x00020000 // delete file contents while opening file
#define IO_FILE_CREATE		0x00040000 // create file when it doesn't exist while opening
#define IO_FILE_STATE_MASK	0x0000ffff
#define IO_FILE_UNUSED		0xffffffff

// Device mounting flags
#define IO_MOUNT_WRITABLE	0x00000001

#ifndef ASSERT_ME
	#ifdef _DEBUG
		#define ASSERT_ME AssertValid()
	#else
		#define ASSERT_ME 
	#endif
#endif

#ifndef NULL
#define NULL 0
#endif

class BlockDeviceInterface;
class DeviceIoDriver;
class DeviceIoManager;
class DeviceIoDriverFactory;
class BlockDeviceCache;
class DeviceIoFile;

///////////////////////////////////////////////////////////////////////////////
// Seek() mode values

enum seekMode
{
	seekBegin = 0,
	seekEnd = -1,
	seekCurrent = -2
};

///////////////////////////////////////////////////////////////////////////////
// DeviceIoClock
//
// This class is used to let the user implement his own file system clock.
// An instance of a DeviceIoClock derived class can be hooked
// to the DeviceIoManager. The hooked object will be used for time
// stamping file access. The default implementation will
// set the date and time to 1 jan 1980; 00:00:00, so you will
// have to derive (and hook) your own class to let it tick.

struct DeviceIoStamp
{
	unsigned short msec;	// 0..999  Milliseconds ( as first element to optimize byte arrangement)
	unsigned short year;	// YYYY    Year (4 digits)
	unsigned char month;	// 0..11   Month
	unsigned char day;		// 0..30   Day
	unsigned char hour;		// 0..23   Hour (24-hour)
	unsigned char min;		// 0..59   Minute
	unsigned char sec;		// 0..59   Seconds
};

class DeviceIoClock
{
public:
	virtual IO_RESULT GetDosStamp(DeviceIoStamp& t);
};

///////////////////////////////////////////////////////////////////////////////
// BlockDeviceInterface
//
// This is a low level driver specification, which interfaces the hardware to 
// the actual driver. Derive your own classes for new hardware protocols.
// Note that this is an abstract base class that cannot be instantiated.
// Derivations (implementations) of this interface are typically used 
// by 'DeviceIoDriver' drivers.
// Note that a 'DeviceIoDriver' driver can connect to different
// BlockDeviceInterface implementations. For example: A 'DeviceIoDriver'
// that implements support for FAT compatible partitions can connect to
// different kind of interfaces to drive the hardware (memory mapped floppy
// disk, hard disk, zip drive on parallel port, CompactFlash card, ...).
// Another useful application, is to replace the hardware interface with
// an interface that mimics a device using only software. Such a virtual disk
// is ideal for 'early bird' testing, without accessing the actual hardware.

class BlockDeviceInterface
{
public:
	virtual IO_RESULT MountHW(void* custom=0, unsigned long lMountFlags=0/*, long hSubDevice=-1*/) = 0;
	virtual IO_RESULT UnmountHW(/*long hSubDevice=-1*/) = 0;
	virtual IO_RESULT ReadSector(unsigned long lba, char* pData) = 0;
	virtual IO_RESULT WriteSector(unsigned long lba, const char* pData) = 0;

	virtual const char* GetDriverID(/*long hSubDevice=-1*/) = 0;
	virtual int GetSectorSize(/*long hSubDevice=-1*/) = 0;
};

///////////////////////////////////////////////////////////////////////////////
// DeviceIoDriver
//
// This class is the starting point for implementing sector based I/O drivers.
// Implementations of 'BlockDeviceInterface' are used to access the actual
// hardware. Drivers are mounted (and unmounted) using a DeviceIoManager.
// A pointer reference to the manager is stored in m_pManager, so it is
// possible to use the caching mechanism provided by the manager.
// Mounted DeviceIoDrivers become part of a linked list of drivers (see m_pNext).
// A linked list is prefered instead of a fixed size or dynamic table
// because memory requirements should be minimal.

class DeviceIoDriver
{
protected:
	// must derive from this class
	DeviceIoDriver()
	{
#if MAX_ALLOWED_DRIVERS>1
		m_pNext = NULL;
#endif
		m_pManager = NULL;
		m_pHal = NULL;
		m_hSubDevice = -1;
	}

public:
	virtual ~DeviceIoDriver()
	{
	}

	// mount/unmount drivers (SW==software)
	virtual IO_RESULT MountSW(BlockDeviceInterface* pHal, void* custom=NULL, long hDevice=-1) = 0;
	virtual IO_RESULT UnmountSW() = 0;
//	virtual IO_RESULT Standby() { return IO_OK; }
//	virtual IO_RESULT Reset() { return IO_OK; }
	virtual IO_RESULT Lock() = 0;
	virtual IO_RESULT Unlock() = 0;

	virtual IO_RESULT LoadSector(unsigned long lba, char** ppData, bool bWritable, bool bPreLoad);
	virtual IO_RESULT UnloadSector(char* pData/*, bool bFlush=true*/);

//	virtual IO_RESULT GetType() const	// returns IO_DRIVER_TYPE_XXX or IO_ERROR
//		{ return IO_ERROR; }

	virtual int GetNrOfVolumes() const = 0;

	BlockDeviceInterface* GetHal()		// returns low level driver
		{ ASSERT(m_pHal!=NULL); return m_pHal; }

	// DeviceIoFile support (treat pDriverData as a file handle!)
public:
	virtual IO_RESULT OpenFile(const char* szFilePath, DeviceIoFile& ioFile, unsigned long lFlags) = 0;
	virtual IO_RESULT FileExist(const char* szFilename) = 0; 
	virtual IO_RESULT CreateDirectory(const char* szFilePath) = 0;
	virtual IO_RESULT DeleteFile(const char* szFilename, unsigned long lFlags=0) = 0; // also for deleting directories
	virtual IO_RESULT GetNrOfFreeSectors(const char* szPath, unsigned long& n) = 0;
	virtual IO_RESULT GetNrOfSectors(const char* szPath, unsigned long& n) = 0;
	virtual IO_RESULT Flush() = 0;//Flush driver

protected:
	friend class DeviceIoFile;
	friend class DeviceIoManager;
	virtual IO_RESULT CloseFile(IO_HANDLE pDriverData) = 0;
	virtual IO_RESULT ReadFile(IO_HANDLE pDriverData, char* pBuf, unsigned int& n) = 0;
	virtual IO_RESULT WriteFile(IO_HANDLE pDriverData, const char* pBuf, unsigned int& n) = 0;
	virtual IO_RESULT Seek(IO_HANDLE pDriverData, seekMode mode, long pos) = 0;
	virtual IO_RESULT Tell(IO_HANDLE pDriverData, unsigned long& pos) = 0;
	virtual IO_RESULT Flush(IO_HANDLE pDriverData) = 0;//Flush file
	virtual IO_RESULT GetFileSize(IO_HANDLE pDriverData, unsigned long& s) = 0;
	

	void SetDeviceIoManager(DeviceIoManager* pManager) 
		{ m_pManager = pManager; }

#if MAX_ALLOWED_DRIVERS>1
	// Linked list implementation. Should only be used by DeviceIoManager.
	void SetNextDriver(DeviceIoDriver* pNext) 
		{ m_pNext=pNext; }
	DeviceIoDriver* GetNextDriver() 
		{ return m_pNext; }

	DeviceIoDriver* m_pNext;		// Forward only linked list of IO drivers
#endif


	// data members
	BlockDeviceInterface* m_pHal;	// interface reference to hardware driver
	DeviceIoManager* m_pManager;	// Parent reference
	long m_hSubDevice;				// device identifier if this is a subdevice, or -1
};

///////////////////////////////////////////////////////////////////////////////
// DeviceIoDriverFactory
//
// This interface defines the layout for classes, which are used to 
// load/create/lookup IO drivers.

class DeviceIoDriverFactory
{
public:
	virtual DeviceIoDriver* AllocateDriver(const char* szDriverID) = 0;
	virtual void ReleaseDriver(DeviceIoDriver* pDriver) = 0;
};

///////////////////////////////////////////////////////////////////////////////
// BlockDeviceCache
// Sector cache!
// Kind of smartdrive, but without 'lazy write'. That is, data is written
// immediately when a sector is unlocked.
// TODO: use real (timed) semaphores, instead of plain integers (only req. in a multithreading design)
// TODO: implement separate reader-writer locks (only req. in a multithreading design)

#define CACHE_MAGIC_VALUE 0x5aa5a55a

class BlockDeviceCache
{
public:
	BlockDeviceCache()
	{
		Reset();
	}

	void Reset();
	char* Lock(BlockDeviceInterface* pDev, unsigned long lba, bool bWritable, bool bPreLoad, unsigned long timeout=-1);
	IO_RESULT Unlock(char* pData/*, bool bFlush*/);
	IO_RESULT Flush();

protected:

	class CacheEntry
	{
	public:
		CacheEntry()
		{
//			Reset();
		}

		void Reset();
		char* LockDataOnMatch( BlockDeviceInterface* pDev, unsigned long lba, bool bWritable, unsigned long timeout=-1);
		char* LockDataIfFree( BlockDeviceInterface* pDev, unsigned long lba, bool bWritable, bool bPreLoad, unsigned long timeout=-1, bool bEntryIsLocked=false);
		IO_RESULT Unlock(char* pData/*, bool bFlush*/, unsigned timeNow);
		IO_RESULT Flush();

#ifdef _DEBUG
		void AssertValid(); // check internal structures
		void Trace() const;
#endif

		// locking mechanism to protect members of this struct (not the data) for internal use 
		bool LockEntry(unsigned long timeout=-1);
		void UnlockEntry()
		{
			ASSERT_ME;
			m_lockEntry=0;
		}

		bool IsFree() 
		{
//			ASSERT(m_lockEntry>0);
			return m_lockData==0;
		}

		unsigned GetLastAccessTime() const 
		{ 
			return m_tLastAccessTime; 
		}

	protected:
		char* LockData(unsigned long timeout=-1);
		void UnlockData()
		{
			m_lockData=0;
		}


		// wrap data between magic numbers to trap beyond-buffer writes
#ifdef _DEBUG
		long m_lMagicBefore;
#endif
		char m_pData[SECTOR_SIZE]; // this buffer will hold the actual data
#ifdef _DEBUG
		long m_lMagicAfter;
#endif
		unsigned long m_lba;
		BlockDeviceInterface* m_pDev;
		unsigned m_tLastAccessTime; // just an incrementing integer for LRU algorithm (Least Recently Used)
		unsigned m_lockData;	// no distinction yet between reader/writer locks
		unsigned m_lockEntry;	// no distinction yet between reader/writer locks
		bool m_bWritable;		// true if this sector was locked as writable
	};
	unsigned m_timeNow; // just an incrementing integer used as 'time stamp' for LRU
	CacheEntry m_entries[CACHE_SIZE];
};

///////////////////////////////////////////////////////////////////////////////
// DeviceIoFile

class DeviceIoFile
{
public:
	DeviceIoFile()
	{
		m_pDriver = NULL;
		m_pDriverData = NULL;
		m_lLastResult = IO_OK;
	}

	virtual ~DeviceIoFile()
	{
		Close();
	}

	bool IsOpen() const
	{
		return m_pDriver!=NULL;
	}

	IO_RESULT Connect(DeviceIoDriver* pDriver, void* pDriverData); // open a file by connecting to driver
	IO_RESULT Close();

	IO_RESULT Read(char* pBuf, unsigned int& n)
	{ 
		if (m_lLastResult>=IO_OK) 
			m_lLastResult = m_pDriver ? m_pDriver->ReadFile(m_pDriverData, pBuf, n) : IO_ERROR; 
		return m_lLastResult; 
	}

	IO_RESULT Write(const char* pBuf, unsigned int& n)
	{	if (m_lLastResult>=IO_OK) 
			m_lLastResult = m_pDriver ? m_pDriver->WriteFile(m_pDriverData, pBuf, n) : IO_ERROR; 
		return m_lLastResult; 
	}

	IO_RESULT Seek(seekMode mode, long pos)
	{ 
		if (m_lLastResult>=IO_OK) 
			m_lLastResult = m_pDriver ? m_pDriver->Seek(m_pDriverData, mode, pos) : IO_ERROR; 
		return m_lLastResult; 
	}

	IO_RESULT Tell(unsigned long& pos)
	{ 
		if (m_lLastResult>=IO_OK) 
			m_lLastResult = m_pDriver ? m_pDriver->Tell(m_pDriverData, pos) : IO_ERROR; 
		return m_lLastResult; 
	}

	IO_RESULT Flush()
	{ 
		if (m_lLastResult>=IO_OK) 
			m_lLastResult = m_pDriver ? m_pDriver->Flush(m_pDriverData) : IO_ERROR; 
		return m_lLastResult; 
	}

	IO_RESULT GetFileSize(unsigned long& s)
	{ 
		if (m_lLastResult>=IO_OK) 
			m_lLastResult = m_pDriver ? m_pDriver->GetFileSize(m_pDriverData, s) : IO_ERROR; 
		return m_lLastResult; 
	}

	IO_RESULT GetErrorStatus() const
	{
		return m_lLastResult;
	}

	IO_RESULT ClearErrorStatus()
	{
		IO_RESULT t = m_lLastResult;
		m_lLastResult = IO_OK;
		return t;
	}

protected:
//	friend class DeviceIoManager;
	DeviceIoDriver* m_pDriver;		// object that is responsible for carrying out the job
	IO_HANDLE m_pDriverData;		// handle to driver specific data
	IO_RESULT m_lLastResult;
};

///////////////////////////////////////////////////////////////////////////////
// DeviceIoManager

class DeviceIoManager
{
private:
//	DeviceIoManager(DeviceIoDriverFactory* pFactory=NULL)
//	{
//		Init(pFactory);
//	}

public:
	// Implement your own driver factory (DeviceIoDriverFactory) to
	// control how drivers are loaded or created.
	IO_RESULT SetDriverFactory(DeviceIoDriverFactory* pFactory);

	// Connect you own file system clock if you prefer real time stamps instead of fixed ones
	IO_RESULT ConnectClock(DeviceIoClock* pClock) { m_pClock = pClock ? pClock : &m_defaultClock; return IO_OK; }
	DeviceIoClock* GetClock() { ASSERT(m_pClock!=NULL); return m_pClock; }

	void Init()
	{
		//m_pFactory = pFactory;
		m_pFirstDriver = NULL;
		m_pClock = &m_defaultClock;
		m_blockDeviceCache.Reset();
	}

//	IO_RESULT Reset();

	// Load a driver for each hardware device in you system
	IO_RESULT CreateDriver(BlockDeviceInterface* pHal, DeviceIoDriver*& pDriver, const char* szDriverID=NULL, bool bRootDevice=true);
	IO_RESULT ReleaseDriver(DeviceIoDriver* pDriver);
//	IO_RESULT OpenDriver();

	// partition/volume info
	IO_RESULT GetVolumeID(int i, char* szVolumeID) const;
	int GetNrOfVolumes() const;

	// this is your starting point for creating files
	IO_RESULT OpenFile(const char* szPath, DeviceIoFile& f, unsigned long lFlags);
	IO_RESULT FileExist(const char* szFilename);
	IO_RESULT CreateDirectory(const char* szFilePath);
	IO_RESULT DeleteFile(const char* szFilename, unsigned long lFlags=0);
	IO_RESULT GetNrOfFreeSectors(const char* szPath, unsigned long& n);
	IO_RESULT GetNrOfSectors(const char* szPath, unsigned long& n);
	IO_RESULT Flush();

	// cached load/unload sector routines
	IO_RESULT LoadSector(BlockDeviceInterface* pHal, unsigned long lba, char** pData, bool bWritable, bool bPreLoad)
	{
		*pData = m_blockDeviceCache.Lock(pHal, lba, bWritable, bPreLoad);
		return *pData ? IO_OK : IO_ERROR;
	}
	IO_RESULT UnloadSector(char* pData/*, bool bFlush*/)
	{
		return m_blockDeviceCache.Unlock(pData/*, bFlush*/);
	}

protected:
	// we support a cache!
	BlockDeviceCache m_blockDeviceCache;

	static DeviceIoClock m_defaultClock;
	DeviceIoClock* m_pClock;
	DeviceIoDriverFactory* m_pFactory;
	DeviceIoDriver* m_pFirstDriver;

	DeviceIoDriver* GetFS(const char* szPath, const char** szPart2);

#if MAX_ALLOWED_DRIVERS>1
	DeviceIoDriver* GetLastDriver();
	DeviceIoDriver* FindPrevDriver(DeviceIoDriver* pDriver);
	void AppendDriver(DeviceIoDriver* pAppend);
#endif
};

#endif // #ifndef __uFS_h

