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
#include "uFS_ATA.h"
#include "fatdefs.h"

///////////////////////////////////////////////////////////////////////////////
// DeviceIoDriver_ATA

IO_RESULT DeviceIoDriver_ATA::MountSW(BlockDeviceInterface* pHal, void* /*custom*/, long hDevice)
{
	DeviceIoDriver* p;
	IO_RESULT res = IO_OK;
	m_pHal = pHal;
	m_nMounted = 0;
	m_hSubDevice = hDevice; ASSERT(hDevice==-1);

	const MBR* mbr = NULL;

//	res = pHal->ReadSector(0, (char*)&mbr);
	res = LoadSector(0, (char**)&mbr, IO_READ_ONLY, true);
	if (res<IO_OK)
		return res;

	if (mbr->iSignature!=SIGNATURE_MBR) // either MBR or FAT boot bartition
	{
		UnloadSector((char*)mbr/*, false*/);
		return IO_ERROR;
	}
	if (IsValidMBR(mbr))
	{
		TRACEUFS0("DeviceIoDriver_ATA::MountSW: mounting ATA-type using partition table\n");
		for (int i=0; i<sizeof(mbr->partitionTable)/sizeof(mbr->partitionTable[0]); i++)
		{
			const char* szDriverID = NULL;

			switch (mbr->partitionTable[i].cPartitionType)
			{
			case PT_FAT12:
			case PT_FAT16s:			// <=32MB
			case PT_FAT16:			// >32MB && <=2GB
			case PT_FAT16LBA:		// LBA VFAT (BIGDOS/FAT16)
				szDriverID = "FAT";
				break;

			case PT_FAT32:
			case PT_FAT32LBA:
				szDriverID = "FAT32";
				break;

	// TODO parse logical partitions
	//		case PT_EXTENDED:
	//		case PT_EXTENDEDLBA:	// LBA VFAT (DOS Extended)
	//			break;

			default:
				continue;			// skip unsupported partition types
			}

			// limit amount of partitions that can be mounted
			if (m_nMounted>=sizeof(m_pVolumes)/sizeof(m_pVolumes[0]))
			{
				TRACEUFS0("DeviceIoDriver_ATA::MountSW: too many partition entries\n");
				break;
			}

			TRACEUFS2("DeviceIoDriver_ATA::MountSW: mounting partition entry starting %s at %li\n",szDriverID,mbr->partitionTable[i].lbaStart);

			// mount using the appropriate BlockDeviceInterface implementation
			BlockDeviceInterface_FAT* pFatHal = &m_partitions[i];
			pFatHal->m_pHal = pHal;
			pFatHal->m_lStartOfPartition = mbr->partitionTable[i].lbaStart;
			res = m_pManager->CreateDriver(pFatHal, p/*returned*/, szDriverID, false/*i.e. subdevice*/);
			if (res<IO_OK)
				break;

			res = p->MountSW(pFatHal, (void*)&mbr->partitionTable[i], i);
			if (res>=IO_OK)
			{
	//			m_partitions[m_nMounted] = mbr->partitionTable[i];
				m_pVolumes[m_nMounted++] = p;
			}
			else
				break;
		}
	}
	else // since the FAT/ATA signature was OK, this must be some kind of a FAT on its own...
	{
		TRACEUFS0("DeviceIoDriver_ATA::MountSW: mounting FAT-only drive\n");

		const BootSector_FAT16* br16 = (BootSector_FAT16*)mbr;
		const BootSector_FAT32* br32 = (BootSector_FAT32*)mbr;
		if (br16->iSignature!=SIGNATURE_FAT16) // same for FAT32
		{
			res = IO_CORRUPT_BOOT_REC;
		}
		else
		{
			const char* szDriverID = NULL;
			PartitionTableEntry partition;
			partition.cBootIndicator = BI_NONBOOTABLE;
			partition.lbaStart = 0;

			if (strnicmp((const char*)br32->sFileSystemID, "FAT32", 5)==0)
			{
				partition.cPartitionType = PT_FAT32;
				szDriverID = "FAT32";
				partition.nSectors = br32->nSectors32;
				if (br32->lSignature!=SIGNATURE_FAT32)
				{
					res = IO_CORRUPT_BOOT_REC;
				}
			}
			else if (strnicmp((const char*)br16->sFileSystemID, "FAT16", 5)==0)
			{
				partition.cPartitionType = PT_FAT16;
				szDriverID = "FAT";
				partition.nSectors = br16->nSectors;
				if (br16->iSignature!=SIGNATURE_FAT16)
				{
					res = IO_CORRUPT_BOOT_REC;
				}
			}
			else if (strnicmp((const char*)br16->sFileSystemID, "FAT12", 5)==0)
			{
				partition.cPartitionType = PT_FAT12;
				szDriverID = "FAT";
				partition.nSectors = br16->nSectors;
				if (br16->iSignature!=SIGNATURE_FAT16) 
				{
					res = IO_CORRUPT_BOOT_REC;
				}
			}
			else
			{
				res = IO_UNKNOWN_PARTITION_TYPE;
				TRACEUFS1("Unknown partition identifier %s\n",br16->sFileSystemID);
			}

/*			if (br->nSectors!=0)
			{
				// <FAT32
				partition.nSectors = br->nSectors;
			}
			else
			{
				// ==FAT32 or FAT12/16 if nSectors>65535
				partition.nSectors = br->nSectors32;
			}*/
			if (res>=IO_OK && partition.nSectors==0)
			{
				TRACEUFS0("Unknown partition size (#sectors=0)\n");
				res = IO_CORRUPT_BOOT_REC;
			}

			if (res>=IO_OK)
			{
				BlockDeviceInterface_FAT* pFatHal = &m_partitions[0];
				pFatHal->m_pHal = pHal;
				pFatHal->m_lStartOfPartition = 0; // br->nHiddenSectors
				res = m_pManager->CreateDriver(pFatHal, p/*returned*/, szDriverID, false/*i.e. subdevice*/);
				if (res>=IO_OK)
				{
					res = p->MountSW(pFatHal, (void*)&partition, 0);
					if (res>=IO_OK)
					{
						m_pVolumes[0] = p;
						m_nMounted++;
					}
				}
			}
		}
	}

	UnloadSector((char*)mbr/*, false*/);
	if (m_nMounted==0 && res>=0)
		m_nMounted = IO_UNKNOWN_PARTITION_TYPE;
	if (m_nMounted>0 && res!=IO_OK)
	{
		TRACEUFS0("Ignoring last error while mounting partition\n");
		res = IO_OK;
	}
	return res;
}

IO_RESULT DeviceIoDriver_ATA::UnmountSW()
{
	IO_RESULT res = IO_OK;
	for (int i=0; i<m_nMounted; i++)
	{
		DeviceIoDriver* p = m_pVolumes[i];
		if (p)
		{
			m_pVolumes[i] = NULL; // unmounting releases this driver resource

			IO_RESULT t = m_pManager->ReleaseDriver(p);
			if (t<IO_OK)
				res = t; // remember this error, but continue
		}
	}
	return res;
}

int DeviceIoDriver_ATA::GetNrOfVolumes() const 
{
	return m_nMounted; // since volumes are identified as index into m_pVolumes, there must be one volume per sub driver
/*
	int n = 0;
	for (int i=0; i<m_nMounted; i++)
	{
		DeviceIoDriver* p = m_pVolumes[i];
		if (p)
			n += p->GetNrOfVolumes(); // should add one per partition
	}
	return n; */
}


IO_RESULT DeviceIoDriver_ATA::CreateDirectory(const char* szFilePath)
{
	if (szFilePath[0]=='\\' && szFilePath[2]=='\\')
	{
		int iPartition = szFilePath[1] - '0';
		if (iPartition>=0 && iPartition<m_nMounted)
		{
			return m_pVolumes[iPartition]->CreateDirectory(szFilePath+2);
		}
	}
	return IO_ERROR; 
}

IO_RESULT DeviceIoDriver_ATA::DeleteFile(const char* szFilePath, unsigned long lFlags)
{
	if (szFilePath[0]=='\\' && szFilePath[2]=='\\')
	{
		int iPartition = szFilePath[1] - '0';
		if (iPartition>=0 && iPartition<m_nMounted)
		{
			return m_pVolumes[iPartition]->DeleteFile(szFilePath+2, lFlags);
		}
	}
	return IO_ERROR; 
}

IO_RESULT DeviceIoDriver_ATA::OpenFile(const char* szFilePath, DeviceIoFile& ioFile, unsigned long lFlags) 
{
	if (szFilePath[0]=='\\' && szFilePath[2]=='\\')
	{
		int iPartition = szFilePath[1] - '0';
		if (iPartition>=0 && iPartition<m_nMounted)
		{
			return m_pVolumes[iPartition]->OpenFile(szFilePath+2, ioFile, lFlags);
		}
	}
	return IO_ERROR; 
}

IO_RESULT DeviceIoDriver_ATA::GetNrOfFreeSectors(const char* szFilePath, unsigned long& n)
{
	if (szFilePath[0]=='\\' /*&& szFilePath[2]=='\\'*/)
	{
		int iPartition = szFilePath[1] - '0';
		if (iPartition>=0 && iPartition<m_nMounted)
		{
			return m_pVolumes[iPartition]->GetNrOfFreeSectors(/*szFilePath+2*/NULL, n);
		}
	}
	return IO_ERROR; 
}

IO_RESULT DeviceIoDriver_ATA::GetNrOfSectors(const char* szFilePath, unsigned long& n)
{
	if (szFilePath[0]=='\\' /*&& szFilePath[2]=='\\'*/)
	{
		int iPartition = szFilePath[1] - '0';
		if (iPartition>=0 && iPartition<m_nMounted)
		{
			return m_pVolumes[iPartition]->GetNrOfSectors(/*szFilePath+2*/NULL, n);
		}
	}
	return IO_ERROR; 
}

IO_RESULT DeviceIoDriver_ATA::Flush()
{
	IO_RESULT res = IO_OK;
	for (int i=0; i<m_nMounted; i++)
	{
		DeviceIoDriver* p = m_pVolumes[i];
		if (p)
			res= p->Flush(); // should add one per partition
	}
	return res;
}

IO_RESULT DeviceIoDriver_ATA::FileExist(const char* szFilePath/*szFilename*/)
{	
	if (szFilePath[0]=='\\' && szFilePath[2]=='\\')
	{
		int iPartition = szFilePath[1] - '0';
		if (iPartition>=0 && iPartition<m_nMounted)
		{
			return m_pVolumes[iPartition]->FileExist(szFilePath+2);
		}
	}
	return IO_ERROR; 
}

/*
IO_RESULT DeviceIoDriver_ATA::MountHW(void* custom, unsigned long lMountFlags, long hSubDevice)
{
	ASSERT(hSubDevice==-1);
	return m_pHal->MountHW(custom, lMountFlags, hSubDevice);
}

IO_RESULT DeviceIoDriver_ATA::UnmountHW(long hSubDevice)
{
	return m_pHal->UnmountHW(hSubDevice);
}

IO_RESULT DeviceIoDriver_ATA::ReadSector(unsigned long lba, char* pData, long hSubDevice)
{
	// non-cached read operation
	if (hSubDevice>=0 && hSubDevice<m_nMounted)
	{
		const PartitionTableEntry& part = m_partitions[hSubDevice];
		if (lba>=part.nSectors)
			return IO_ILLEGAL_LBA;
		lba += part.lbaStart;
	}
	else if (hSubDevice==-1)
	{
		TRACEUFS0("Warning: reading raw sectors from ATA drive\n");
	}
	else
		return IO_ILLEGAL_DEVICE;
	return m_pHal->ReadSector(lba, pData, -1);
}

IO_RESULT DeviceIoDriver_ATA::WriteSector(unsigned long lba, const char* pData, long hSubDevice)
{
	// non-cached write operation
	if (hSubDevice>=0 && hSubDevice<m_nMounted)
	{
		const PartitionTableEntry& part = m_partitions[hSubDevice];
		if (lba>=part.nSectors)
			return IO_ILLEGAL_LBA;
		lba += part.lbaStart;
	}
	else if (hSubDevice==-1)
	{
		TRACEUFS0("Warning: reading raw sectors from ATA drive\n");
	}
	else
		return IO_ILLEGAL_DEVICE;
	return m_pHal->WriteSector(lba, pData, -1);
}

const char* DeviceIoDriver_ATA::GetDriverID(long hSubDevice)
{
	if (hSubDevice>=0 && hSubDevice<m_nMounted)
		return m_pVolumes[hSubDevice]->GetHal()->GetDriverID();
	else if (hSubDevice==-1)
		return m_pHal->GetDriverID(); // ATA?
	ASSERT(0);
	return NULL;
}

int DeviceIoDriver_ATA::GetSectorSize(long / *hSubDevice* /)
{
	// note that m_pHal of subdevices points to this, so code below is complex and recursive
	// the sector size of subdevices is by definition the same as *this anyway
	return m_pHal->GetSectorSize();
//	if (hSubDevice>=0 && hSubDevice<m_nMounted)
//		return m_pVolumes[hSubDevice]->GetHal()->GetSectorSize();
//	else if (hSubDevice==-1)
//		return m_pHal->GetSectorSize();
//	ASSERT(FALSE);
//	return 0;
}
*/