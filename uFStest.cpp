// uFStest.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "uFS.h"
#include "uFS_fact_heap.h"
#include "partdefs.h"	// standard C partition definitions
#include "fatdefs.h"	// standard C FAT definitions
#include "AmsDeviceFile.h"
#include "VirtualDisk.h"
#include <SHLOBJ.H>
#include "RingBufferX.h"	// standard C FAT definitions
#include "SampleProcessor.h"	// standard C FAT definitions

using namespace std;

#ifdef NON_MBR_TEST
	#error low level FAT.... are you sure?
//	#define FILESYSTEM "\\FAT"
#else
//	#define FILESYSTEM "\\ATA\\0" // for ATA drives, first partition
#endif

///////////////////////////////////////////////////////////////////////////////
// BlockDeviceInterface_RealDisk

#include <winioctl.h>

class BlockDeviceInterface_RealDisk : public BlockDeviceInterface
{
public:
	BlockDeviceInterface_RealDisk()
	{
		m_hDevice = INVALID_HANDLE_VALUE;
		m_buf = NULL;
		m_nSectors = 0;
	}

	virtual ~BlockDeviceInterface_RealDisk()
	{
		ASSERT(m_hDevice==INVALID_HANDLE_VALUE);
		UnmountHW();
	}

//	Init(unsigned long custom) = 0;
	virtual IO_RESULT MountHW(void* custom=0, unsigned long lMountFlags=0/*, long hSubDevice=-1*/);
	virtual IO_RESULT UnmountHW(/*long hSubDevice=-1*/);
	virtual IO_RESULT ReadSector(unsigned long lba, char* pData);
	virtual IO_RESULT WriteSector(unsigned long lba, const char* pData);

	virtual const char* GetDriverID(/*long hSubDevice=-1*/);
	int GetSectorSize(/*long hSubDevice=-1*/) { return 512; }
	unsigned long GetNrOfSectors()	{ return m_nSectors; }

//protected:
	DWORD m_nSectors;
	HANDLE m_hDevice;
	unsigned char cMediaDescriptor;
	char* m_buf;
};

IO_RESULT BlockDeviceInterface_RealDisk::MountHW(void* custom, unsigned long lMountFlags/*, long hSubDevice*/)
{
	DWORD nBytesReturned = 0;
	DISK_GEOMETRY disk_geometry;
	const char* szDrive = (const char*)custom;
	DWORD mode = GENERIC_READ;

	ASSERT(m_hDevice==INVALID_HANDLE_VALUE);

	// check if we must open for writing
	if ((lMountFlags&IO_MOUNT_WRITABLE)!=0)
		mode |= GENERIC_WRITE;
	// and open the device; seems to work only under winNT/2000
	m_hDevice = CreateFile(szDrive, mode, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_NO_BUFFERING, NULL);
	if (m_hDevice==INVALID_HANDLE_VALUE)
		goto _error;

	if (!DeviceIoControl(m_hDevice, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &nBytesReturned, NULL))
	{
		cout << "Cannot lock drive" << endl;
		goto _error;
	}

	if (!DeviceIoControl(m_hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &disk_geometry, sizeof(disk_geometry), &nBytesReturned, NULL))
	{
		cout << "Cannot get disk geometry" << endl;
		goto _error;
	}
	
	m_buf = (char*)VirtualAlloc(NULL, GetSectorSize(), MEM_COMMIT, PAGE_READWRITE);
	if (m_buf==NULL)
	{
		cout << "Failed to allocate sector buffer" << endl;
		goto _error;
	}

	switch (disk_geometry.MediaType)
	{
	case F3_1Pt44_512: cMediaDescriptor = MD_REMOVABLE_DISK_6; break;
	case RemovableMedia: // i.e. FlashCard
	case FixedMedia:   cMediaDescriptor = MD_FIXED_DISK;       break;

	default: 
		cout << "Unknown media type" << endl;
		goto _error;
	}

	LONGLONG longsect = disk_geometry.Cylinders.QuadPart * disk_geometry.SectorsPerTrack;
	if (longsect>64*1024*2)
	{
		cout << "!!!Large disk!!! will exit just in case" << endl;
		goto _error;
	}
	m_nSectors = (DWORD)longsect;
/*
	IO_RESULT res = ReadSector(0, m_buf);
	if (res!=IO_OK)
		return res;

	cMediaDescriptor = ((BootSector_FAT16*)m_buf)->cMediaDescriptor;
*/
	return IO_OK;

_error:
	UnmountHW();
	cout << GetSystemErrorMessage() << endl;
	return IO_ERROR;
}

IO_RESULT BlockDeviceInterface_RealDisk::UnmountHW(/*long hSubDevice*/)
{
	if (m_buf)
	{
		VERIFY(VirtualFree(m_buf, 0, MEM_RELEASE));
		m_buf = NULL;
	}
	if (m_hDevice!=INVALID_HANDLE_VALUE)
	{
		DWORD returnLength=0;
		VERIFY(DeviceIoControl(m_hDevice, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &returnLength, NULL ));
		VERIFY(DeviceIoControl(m_hDevice, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &returnLength, NULL ));
		VERIFY(DeviceIoControl(m_hDevice, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &returnLength, NULL ));
		VERIFY(CloseHandle(m_hDevice));
		m_hDevice = INVALID_HANDLE_VALUE;
	}
	return IO_OK;
}

IO_RESULT BlockDeviceInterface_RealDisk::ReadSector(unsigned long lba, char* pData)
{
#ifdef TRACE_UFS_DETAILED
	cout << "realdisk: reading sector lba = " << lba << endl;
#endif
	DWORD n = 0;
	if (0xffffffff==SetFilePointer(m_hDevice, lba*GetSectorSize(), 0, FILE_BEGIN))
		return IO_INVALID_CLUSTER;
 	if (!ReadFile(m_hDevice, m_buf, GetSectorSize(), &n, NULL))
		return IO_ERROR;
	memcpy(pData, m_buf, GetSectorSize()); // read from disk using SECTOR aligned buffer!
	return IO_OK;
}

IO_RESULT BlockDeviceInterface_RealDisk::WriteSector(unsigned long lba, const char* pData)
{
#ifdef TRACE_UFS_DETAILED
	cout << "realdisk: writing sector lba = " << lba << endl;
#endif
	DWORD n = 0;
	memcpy(m_buf, pData, GetSectorSize()); // write to disk using SECTOR aligned buffer!
	if (0xffffffff==SetFilePointer(m_hDevice, lba*GetSectorSize(), 0, FILE_BEGIN))
		return IO_INVALID_CLUSTER;
	return WriteFile(m_hDevice, m_buf, GetSectorSize(), &n, NULL) ? IO_OK : IO_ERROR;
}

const char* BlockDeviceInterface_RealDisk::GetDriverID(/*long hSubDevice*/)
{
#ifdef NON_MBR_TEST
	switch (cMediaDescriptor)
	{
	case MD_FIXED_DISK:	      // i.e. a disk that has a MBR in first sector
		return "FAT";
//		return "ATA";

	case MD_REMOVABLE_DISK_1: // i.e. a disk that has a FAT12/FAT16 partition layout
	case MD_REMOVABLE_DISK_2: 
	case MD_REMOVABLE_DISK_3: 
	case MD_REMOVABLE_DISK_4: 
	case MD_REMOVABLE_DISK_5: 
	case MD_REMOVABLE_DISK_6: 
		return "FAT";
	}
	return NULL;
#else
	return "ATA"; // also support a single FAT partition without MBR
#endif
}


///////////////////////////////////////////////////////////////////////////////
// BlockDeviceInterface_VirtualDisk

class BlockDeviceInterface_VirtualDisk : public BlockDeviceInterface
{
public:
//	Init(unsigned long custom) = 0;
	virtual IO_RESULT MountHW(void* custom=0, unsigned long lMountFlags=0/*, long hSubDevice=-1*/);
	virtual IO_RESULT UnmountHW(/*long hSubDevice=-1*/);
	virtual IO_RESULT ReadSector(unsigned long lba, char* pData);
	virtual IO_RESULT WriteSector(unsigned long lba, const char* pData);

	virtual const char* GetDriverID(/*long hSubDevice=-1*/);
	int GetSectorSize(/*long hSubDevice=-1*/) { return 512; }
	unsigned long GetNrOfSectors()	{ return m_vd.GetNrOfSectors()-lStartAt; }

//protected:
	VirtualDisk m_vd;
	unsigned char cMediaDescriptor;
	unsigned long lStartAt;
};

IO_RESULT BlockDeviceInterface_VirtualDisk::MountHW(void* custom, unsigned long lMountFlags/*, long hSubDevice*/)
{
	lStartAt = 0;
	BOOL b = m_vd.Open((const char*)custom, ((lMountFlags&IO_MOUNT_WRITABLE)==0), 512);
	if (!b)
		return IO_ERROR;

	ASSERT(sizeof(BootSector_FAT16)==GetSectorSize());
	char buf[512];
	do
	{
		IO_RESULT res = ReadSector(0, (char*)buf);
		if (res!=IO_OK)
			return res;

		const BootSector_FAT16* f = (BootSector_FAT16*)buf;
		if (f->iSignature==SIGNATURE_FAT16)
		{
			cMediaDescriptor = f->cMediaDescriptor;
			break;
		}
		lStartAt++;
	} while (true);
	return IO_OK;
}

IO_RESULT BlockDeviceInterface_VirtualDisk::UnmountHW(/*long hSubDevice*/)
{
	m_vd.Close();
	return IO_OK;
}

IO_RESULT BlockDeviceInterface_VirtualDisk::ReadSector(unsigned long lba, char* pData)
{
	return m_vd.ReadSector(lStartAt+lba, pData) ? IO_OK : IO_ERROR;
}

IO_RESULT BlockDeviceInterface_VirtualDisk::WriteSector(unsigned long lba, const char* pData)
{
	return m_vd.WriteSector(lStartAt+lba, pData) ? IO_OK : IO_ERROR;
}

const char* BlockDeviceInterface_VirtualDisk::GetDriverID(/*long hSubDevice*/)
{
#ifdef NON_MBR_TEST
	switch (cMediaDescriptor)
	{
	case MD_FIXED_DISK:	      // i.e. a disk that has a MBR in first sector
//		return "ATA"; // can only access partitions, not the MBR

	case MD_REMOVABLE_DISK_1: // i.e. a disk that has a FAT12/FAT16 partition layout
	case MD_REMOVABLE_DISK_2: 
	case MD_REMOVABLE_DISK_3: 
	case MD_REMOVABLE_DISK_4: 
	case MD_REMOVABLE_DISK_5: 
	case MD_REMOVABLE_DISK_6: 
		return "FAT";
	}
	return NULL;
#else
	return "ATA"; 
#endif
}



//////////////////////
// vu-ams file format
//
// header
// file id			("vuams" <EOS> <EOF>)
// file version		(<128)
// compression      (<128)
// registration ID  (<string>)
// operator ID      (<string>)
// #channels		(<128)
// sampling period  (usec, <4294.967295 sec)
//
// tag table ???
// tag nr
// sizeof tag or 0
//
//    tag    #   description
//    0x00 - 4 - 24 bit (or less) synchr sample
//    0x01 - 4 - 24 bit (or less) asynchr. sample (i.e. R-peak)
//               time stamp + type
//    0xff - 4 - EOF
//
// followed by one or more channel headers:
// channel ID       ("ICG" <EOS>)
// sampling period  (usec, <4294.967295 sec)
// bits per sample
// signed/unsigned
// phys. range		(min, max)
// type             (X, dX/dt, ((d/dt)^2)X, ...)


/*
template< typename Iterator, typename T>
Iterator encode(Iterator p, T i)
{
	for (;(-(T)1>0 ? i : i+64u)>0x7fU; i>>=7)
		*p++ = i&0x7f|0x80;
	*p++ = i&0x7f;
	return p;
}

template< typename Iterator, typename T>
Iterator decode(Iterator p, T& result)
{
	T u = 0;
	int s = 0;
	unsigned char c;
	do
	{
		c = *p++;
		u|= (c&0x7f)<<s;
		s +=7;
	} while (c&0x80);
	result = -(T)1>0 ? u : u - (c>>6&1<<s);
	return p;
}


class OFile : public DeviceIoFile
{
public:
	OFile()
	{
		m_p = m_buf;
	}

	~OFile()
	{
		Close();
#ifdef _DEBUG
		unsigned long l;
		m_p = decode(m_p, l); TRACEUFS1("%x\n",l);
		m_p = decode(m_p, l); TRACEUFS1("%x\n",l);
		m_p = decode(m_p, l); TRACEUFS1("%x\n",l);
		m_p = decode(m_p, l); TRACEUFS1("%x\n",l);
		m_p = decode(m_p, l); TRACEUFS1("%x\n",l);
		long sl;
		m_p = decode(m_p, sl); TRACEUFS1("%li\n",sl);
		m_p = decode(m_p, sl); TRACEUFS1("%li\n",sl);
		m_p = decode(m_p, sl); TRACEUFS1("%li\n",sl);
		m_p = decode(m_p, sl); TRACEUFS1("%li\n",sl);
		m_p = decode(m_p, sl); TRACEUFS1("%li\n",sl);
#endif
	}

	IO_RESULT Close()
	{
		Flush();
		return DeviceIoFile::Close();
	}

	IO_RESULT Flush()
	{
		WriteBuffer();
		return DeviceIoFile::Flush();
	}

	OFile& operator<<(unsigned long l)
	{
		if (CheckSpace(5)>=IO_OK)
			m_p = encode(m_p,l);
		return *this;
	}

protected:
	IO_RESULT WriteBuffer()
	{
		IO_RESULT res = IO_OK;
		unsigned int n = (int)(m_p - m_buf);
		if (n)
		{
			res = Write(m_buf,n);
			m_p = m_buf;
		}
		return res;
	}

	IO_RESULT CheckSpace(int nBytesFree)
	{
		IO_RESULT res = IO_OK;
		unsigned int n = (int)(m_p - m_buf);
		if (n+nBytesFree>sizeof(m_buf))
		{
			res = Write(m_buf,n);
			m_p = m_buf;
		}
		return res;
	}

	char m_buf[128];
	char* m_p;
};
*/

class PcStamp : public DeviceIoClock
{
public:
	virtual IO_RESULT GetDosStamp(DeviceIoStamp& t)
	{
		SYSTEMTIME st;
		GetLocalTime(&st);

		t.msec = st.wMilliseconds;	// 0..999  Milliseconds
		t.year = st.wYear;			// YYYY    Year (4 digits)
		t.month = st.wMonth-1;		// 0..11   Month
		t.day = st.wDay - 1;		// 0..30   Day
		t.hour = (unsigned char)st.wHour;			// 0..23   Hour (24-hour)
		t.min = (unsigned char)st.wMinute;			// 0..59   Minute
		t.sec = (unsigned char)st.wSecond;			// 0..30   Seconds
		return IO_OK;
	}
} _pcClock;

inline unsigned long Chop16(long v)
{ return v>0 ? (v<0xFFFF?v:0xFFFF) : 0; }
inline unsigned long Chop16(double v)
{ return Chop16((long)v); }
inline unsigned long Chop12(long v)
{ return v>0 ? (v<0x0FFF?v:0x0FFF) : 0; }
inline unsigned long Chop12(double v)
{ return Chop12((long)v); }

int main(int argc, char* argv[])
{
	char szFile[256];
	char szVolumeID[16] = "\\ATA\\0";
	bool bDump = false;
	int i;
	AmsDeviceFile fAMS;
	AMSII_Time tme;
	AMSII_ChannelSettings channels;
	SampleStruct samples;
//	SampleStructX samplesX;
	SampleProcessor sp;


	if (argc==2) bDump = true;
#ifdef _DEBUG
	const char* lpszPathName = "\\\\.\\PHYSICALDRIVE1"; // "g:\\temp\\images\\boot.img";
#else
	if (argc<2)
	{
		cout << "missing arguments" << endl;
		return -1;
	}
	const char* lpszPathName = argv[1];
#endif

	IO_RESULT res;
	DeviceIoDriverFactory_Heap h;
	DeviceIoManager m;
	m.Init();
	m.SetDriverFactory(&h);
	m.ConnectClock(&_pcClock);
#if 0
	// virtual disk (i.a. a file)
	BlockDeviceInterface_VirtualDisk vdisk;
	res = vdisk.MountHW((void*)lpszPathName, IO_MOUNT_WRITABLE);
#else
	// real disk
	BlockDeviceInterface_RealDisk vdisk;
	res = vdisk.MountHW((void*)lpszPathName, IO_MOUNT_WRITABLE);
#endif
	if (res<IO_OK) 
		return TRUE;

	SHChangeNotify(SHCNE_MEDIAREMOVED, SHCNF_PATH, lpszPathName, NULL); // ??

	DeviceIoDriver* pDriver=NULL;
	res = m.CreateDriver(&vdisk, pDriver);
	if (res<IO_OK) 
	{
		cout << "failed to create a driver for specified hardware interface" << endl;
		return -1;
	}

#ifdef NON_MBR_TEST
	// use this code when you would like to test a single FAT driver on a non-ATA partitioned drive
	PartitionTableEntry partition;
	partition.cBootIndicator = BI_NONBOOTABLE;
	partition.cPartitionType = PT_FAT16;
	partition.lbaStart = 0;
	partition.nSectors = vdisk.GetNrOfSectors();
	res = pDriver->MountSW(&vdisk, &partition);
#else
	res = pDriver->MountSW(&vdisk);
#endif
	if (res<IO_OK) 
	{
		cout << "failed to mount driver" << endl;
		return -1;
	}

	const char cbuf[] = "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
						"0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
						"0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
						"0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
						"0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
						"01234567890\n";
	char obuf[sizeof(cbuf)];
	char ibuf[sizeof(cbuf)];
	unsigned int n = sizeof(obuf)-1;
	unsigned long pos;
//		IO_HANDLE hFile = NULL;
	DeviceIoFile f;

	if (bDump)
		goto _exit;

	unsigned long nMaxFreeSectors = 0;
	for (i=0;i<m.GetNrOfVolumes();i++)
	{
		char szTemp[16];
		m.GetVolumeID(0,szTemp);
		cout << "volume " << i << ": " << szTemp;

		res = m.GetNrOfFreeSectors(szTemp,pos);
		if (res<IO_OK)
		{
			cout << "Couldn't determine amount of free space" << endl;
			goto _exit;
		}
		cout << ", number of free sectors " << pos << endl;
		if (pos>nMaxFreeSectors)
		{
			nMaxFreeSectors = pos;
			strcpy(szVolumeID,szTemp);
		}
	}
	if (nMaxFreeSectors==0)
	{
		cout << "No free space detected" << endl;
		goto _exit;
	}
	cout << "using volume " << szVolumeID << endl;


	sprintf(szFile,"%s\\test.5fs",szVolumeID);
	res = m.OpenFile(szFile, fAMS, IO_FILE_CREATE|IO_FILE_WRITABLE|IO_FILE_RESET );
	if (res<IO_OK) 
	{
		cout << "failed to open " << szFile << endl;
		goto _exit;
	}

	tme.Init();
	channels.SetToDefault();
	channels.info.A25.dwDivider = 0;
	channels.info.DZ.dwDivider  = 1;
//	channels.info.SCL.dwDivider = channels.info.SCL_filtered.dwDivider = 0;
	channels.info.ECG.dwDivider = 0;
	channels.info.Xt1.dwDivider = 0;
	channels.info.PCG.dwDivider = 0;
	channels.info.Ire.dwDivider = 0;
	channels.info.BATraw.dwDivider = channels.info.BAT_avg.dwDivider = 0;
	channels.info.Z0raw.dwDivider  = channels.info.Z0_avg.dwDivider = 0;
	channels.info.YMT.dwDivider = channels.info.YMT_avg_ac.dwDivider = channels.info.YMT_avg_dc.dwDivider = 0;
	channels.info.XMT.dwDivider = channels.info.XMT_avg_ac.dwDivider = channels.info.XMT_avg_dc.dwDivider = 0;
	channels.info.LDR_avg.dwDivider = 0;



	res = fAMS.Initialize("dummy",1,tme,1,channels);
	if (res<IO_OK) 
	{
		cout << "failed to initialize " << szFile << endl;
		goto _exit;
	}
	sp.Init(channels.info.SCL_filtered.dwDivider,
			channels.info.Z0_avg.dwDivider, 
			channels.info.BAT_avg.dwDivider, 
			channels.info.BATraw.dwDivider, 
			channels.info.XMT_avg_ac.dwDivider, 
			channels.info.YMT_avg_ac.dwDivider,
			channels.info.LDR_avg.dwDivider);
	fAMS.SetBlockStartTime(tme);
	for (i=1; i<100000; i++)
	{
#define PHASE 5
#define Hz1 (2*3.1415926535897932384626433832795*0.001*(i-PHASE))
#define NOISE(a) ((unsigned)(a*(rand()/(double)RAND_MAX)))

		samples.SCL_index = CLOCK_INVALID;
		samples.A25 = SAMPLE_INVALID;
		samples.Xt1 = SAMPLE_INVALID;
		samples.Ire = SAMPLE_INVALID;
		samples.LDRraw = SAMPLE_INVALID;
		samples.XMT = SAMPLE_INVALID;
		samples.YMT = SAMPLE_INVALID;
		samples.SCLraw = SAMPLE_INVALID;
		samples.Z0raw  = SAMPLE_INVALID;
		samples.BATraw = SAMPLE_INVALID;
		samples.DZ  = SAMPLE_INVALID;
		samples.ECG = SAMPLE_INVALID;
		samples.PCG = SAMPLE_INVALID;

		samples.msC = i+1;
		if (channels.info.SCLraw.dwDivider)
		{
			samples.SCL_index = (i % 100);
			samples.SCLraw = Chop16(0x7FFF+0x4000*sin(10*Hz1)+0x2000*sin(50*Hz1)+NOISE(0x1000)); // 16 bit
		}
		if (channels.info.DZ.dwDivider)
			samples.DZ  = Chop16(0x7FFF+0x7FFF*sin(1*Hz1));	// 16 bit
		if (channels.info.ECG.dwDivider)
			samples.ECG = Chop16(0x7FFF+0x7FFF*sin(2*Hz1));	// 16 bit
		if (channels.info.PCG.dwDivider)
			samples.PCG = Chop12(0x07FF+0x07FF*sin(3*Hz1));	// 12 bit
		if (channels.info.Z0raw.dwDivider && i%channels.info.Z0raw.dwDivider==0) 
			samples.Z0raw = Chop12(0x07FF+0x01FF*sin(Hz1));	// 12 bit
		if (channels.info.BATraw.dwDivider && i%channels.info.BATraw.dwDivider==0) 
			samples.BATraw = Chop12(0x07FF+0xFF*sin(.01*Hz1));	// 12 bit
		if (channels.info.YMT.dwDivider && i%channels.info.YMT.dwDivider==0)  
			samples.YMT = Chop12(0x07FF+0xFF*sin(.5*Hz1));	// 12 bit

		res = fAMS.AppendSamples(sp.Process(samples), channels);

		if (res<IO_OK) 
		{
			cout << "failed to initialize " << szFile << endl;
			goto _exit;
		}
	}
	fAMS.Stop(tme); // wrong time!
	fAMS.Close();

//goto _exit;

//	res = m.CreateDirectory(szVolumeID"\\TEMP");
//	if (res<IO_OK) 
//	{
//		cout << "failed create directory \\TEMP" << endl;
//		goto _exit;
//	}

//m.GetNrOfFreeSectors(szVolumeID,pos);
//cout << "Number of free sectors: " << pos << endl;


//goto _exit;
	f.Close();

	for (int filenum=0; filenum<1; filenum++)
	for (int filetype=0; filetype<2; filetype++)
	{
		sprintf(szFile,"%s\\empty.dir",szVolumeID);
		res = m.CreateDirectory(szFile);
		if (res<IO_OK) 
		{
			cout << "failed to create directory " << szFile << endl;
			goto _exit;
		}
//		res = m.DeleteFile(FILESYSTEM"\\12345678.txt",FAT_ATTR_DIRECTORY);
//		if (res<IO_OK) 
//		{
//			cout << "failed to delete directory " << szFile << endl;
//			goto _exit;
//		}

		sprintf(szFile,"%s\\out%02d_%d.txt",szVolumeID,filenum,filetype);
		for (int loop=0; loop<3; loop++)
		{
			res = m.OpenFile(szFile, f, (loop==0 ? IO_FILE_CREATE|IO_FILE_WRITABLE|IO_FILE_RESET : 0 ) );
			if (res<IO_OK) 
			{
				cout << "failed to open " << szFile << endl;
				goto _exit;
			}

			for (i=0; i<513; i++)
			{
		//		res = f.Tell(pos);
				memcpy(ibuf,cbuf,sizeof(cbuf));
				memcpy(obuf,cbuf,sizeof(cbuf));
				sprintf(obuf,"%04d",i);
				obuf[4] = ' ';
				switch (filetype)
				{
				case 0: n = 256; break;
				case 1:	n = sizeof(cbuf)-1; break;
				case 2: n = i%(5+sizeof(cbuf))+1; break;
				default: ASSERT(FALSE);
				}
				obuf[n-1] = '\n';
				if (loop==0)
					res = f.Write(obuf, n);
				else
					res = f.Read(ibuf, n);
				if (res<IO_OK) 
				{
					cout << "failed to write to " << szFile << endl;
					goto _exit;
				}
				if (loop==1 && memcmp(ibuf,obuf,n-1)!=0)
				{
					f.Tell(pos);
					cout << "mismatch at " << pos << endl;
					goto _exit;
				}
			}
			VERIFY(f.Seek(seekBegin,0)>=IO_OK);
			VERIFY(f.Seek(seekEnd,0)>=IO_OK);
			if (loop==0)
			{
				res = f.Write("the end\n", n=8);
				if (res<IO_OK) 
				{
					cout << "failed to write to " << szFile << endl;
					goto _exit;
				}
			}
			f.Close();
		}
	}
	//pDriver->GetNrOfFreeSectors(pos);
	m.GetNrOfFreeSectors(szVolumeID,pos);
	cout << "Number of free sectors: " << pos << endl;

	sprintf(szFile,"%s\\full.txt",szVolumeID);
	res = m.OpenFile(szFile, f, IO_FILE_WRITABLE|IO_FILE_RESET|IO_FILE_CREATE); 
	for(i=0;i<1025;i++) // forever
	{		
		unsigned int n = sizeof(cbuf)-1;
		res = f.Write(cbuf, n);
		if (res<IO_OK) 
		{
			cout << "failed to write to file full.txt "  << endl;
			break;
		}
	}
_fast:
	sprintf(szFile,"%s\\ADC12.txt",szVolumeID);
	res = m.OpenFile(szFile, f, IO_FILE_WRITABLE|IO_FILE_RESET|IO_FILE_CREATE); 
	for (i=0;i<1000000;i++)
	{		
			unsigned int n;
			n = sprintf(obuf,"%hu\r\n", (unsigned short)i);
			f.Write(obuf, n);
	}
	
	//	VERIFY(f.Seek(seekMode::seekBegin,0)>=IO_OK);

	m.GetNrOfFreeSectors(szVolumeID,pos);
	cout << "Number of free sectors: " << pos << endl;
//goto _exit;
	f.Close();
/*
goto _exit;
	f.Close();

//	res = m.DeleteFile(FILESYSTEM"\\TEMP\\OUTFILE.PLY", FAT_ATTR_ARCHIVE);
//	res = m.DeleteFile(FILESYSTEM"\\TEMP", FAT_ATTR_ARCHIVE|FAT_ATTR_DIRECTORY);
	res = m.OpenFile(FILESYSTEM"\\FILE1.TXT", f, IO_FILE_CREATE|IO_FILE_WRITABLE);
	if (res<IO_OK) 
	{
		cout << "failed to open " FILESYSTEM "\\OUTFILE.TXT" << endl;
		goto _exit;
	}
	for (i=0; i<100; i++)
	{
		res = f.Write(obuf, n);
		if (res<IO_OK) 
		{
			cout << "failed to write to " FILESYSTEM "\\OUTFILE.TXT" << endl;
			goto _exit;
		}
	}
	f.Close();

	res = m.OpenFile(FILESYSTEM"\\OUTFILE2.TXT", f, IO_FILE_CREATE|IO_FILE_WRITABLE);
	if (res<IO_OK) 
	{
		cout << "failed to open " FILESYSTEM "\\OUTFILE2.TXT" << endl;
		goto _exit;
	}
	for (i=0; i<100; i++)
	{
		res = f.Tell(pos);
		res = f.Write(obuf, n);
		if (res<IO_OK) 
		{
			cout << "failed to write to " FILESYSTEM"\\OUTFILE2.TXT" << endl;
			goto _exit;
		}
		res = f.Seek(seekBegin, pos+10);
		unsigned int m=7;
		res = f.Write("!HOERA!", m);
		if (res<IO_OK) 
		{
			cout << "failed to write at spec. position to " FILESYSTEM "\\OUTFILE2.TXT" << endl;
			goto _exit;
		}
		res = f.Seek(seekEnd, 0);
	}
	f.Close();
*/
/*
	OFile amsFile;
	res = m.OpenFile(FILESYSTEM"\\0\\OUTFILE2.AMS", IO_FILE_CREATE|IO_FILE_WRITABLE, amsFile);
	if (res<IO_OK) 
	{
		cout << "failed to open \\ATA\\0\\TEMP\\OUTFILE2.AMS" << endl;
		goto _exit;
	}
	amsFile << 0x00000000U << 0x0000007fU << 0x00007fffU << 0x007fffffU << 0x7fffffffU;
	amsFile << 0 << -1 << 2000 << -80000 << (long)0xffffffff;
	amsFile.Close();
*/

_exit:
	if (fAMS.IsOpen()) fAMS.Close();
	f.Close();

	pDriver->UnmountSW();
	vdisk.UnmountHW();
	m.ReleaseDriver(pDriver);

	SHChangeNotify(SHCNE_MEDIAINSERTED, SHCNF_PATH, lpszPathName, NULL);
	SHChangeNotify(SHCNE_DISKEVENTS, 0, 0, NULL);

	return 0;
}
