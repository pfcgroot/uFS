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

#ifndef _partdefs_h
#define _partdefs_h

// set byte packing to 1
#ifdef __arm
#define PACKED __packed
#else
#define PACKED
#endif

#ifdef _MSC_VER
// set byte packing to 1 (for Visual C), reset at eof
#pragma pack( push, enter_partdefs, 1 ) 
#endif


#define SECTOR_SIZE 512		// valid for all know ATA compatible drives

// definitions for iSignature
#define SIGNATURE_MBR 0xAA55

// definitions for cBootIndicator
#define BI_BOOTABLE    0x80
#define BI_NONBOOTABLE 0x00

// definitions for cPartitionType
#define PT_FREE        0x00
#define PT_FAT12       0x01
#define PT_XENIX_ROOT  0x02
#define PT_XENIX_USR   0x03
#define PT_FAT16s      0x04 // <=32MB
#define PT_EXTENDED    0x05
#define PT_FAT16       0x06 // >32MB && <=2GB
#define PT_NTFS        0x07 // or HPFS, or QNX, or Advanced Unix
#define PT_OS2BOOT     0x08 // OS/2 boot (v1.0-v1.3), AIX boot, Commodore DOS, Dell multi drive part.
#define PT_AIX_DATA    0x09 // AIX data
#define PT_OS2BM       0x0A // OS/2 bootmanager
#define PT_FAT32       0x0B
#define PT_FAT32LBA    0x0C
#define PT_FAT16LBA    0x0E // LBA VFAT (BIGDOS/FAT16)
#define PT_EXTENDEDLBA 0x0F // LBA VFAT (DOS Extended)
#define PT_LINUXSWAP   0x82
#define PT_LINUXEXT2   0x83

// Cylinder, Head, Sector address type
typedef PACKED struct CHS_address_struct
{
	unsigned char H;		// Head,     base 0
	unsigned char S  : 6;	// Sector nr base 1
	unsigned char CH : 2;	// upper two bits  of cylinder  (bits 8 & 9)
	unsigned char CL;       // Cylinder, base 0

} CHS_address;

bool IsNull(const CHS_address* p);
unsigned short Cylinders(const CHS_address* p); 


typedef PACKED struct PartitionTableEntry_struct
{
	unsigned char cBootIndicator;	// BI_BOOTABLE, BI_NONBOOTABLE
	CHS_address   chsStart;			// chs-address first sector
	unsigned char cPartitionType;	// PT_XXXX
	CHS_address   chsEnd;			// chs-address last sector
	unsigned long lbaStart;			// relative to current
	unsigned long nSectors;			// size of partition

} PartitionTableEntry;

bool IsValid(const PartitionTableEntry* p);


typedef PACKED struct MBR_struct
{
	char bootcode[SECTOR_SIZE - 4*sizeof(PartitionTableEntry) - sizeof(short)]; // 446
	PartitionTableEntry partitionTable[4];
	unsigned short iSignature; // should be 0xAA55

} MBR;

bool IsValidMBR(const MBR* p);
	

#ifdef _MSC_VER
// reset byte packing
#pragma pack( pop, enter_partdefs )
#endif

#endif //  _partdefs_h
