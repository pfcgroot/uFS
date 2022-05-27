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
/* This header file contains structure definitions for accessing FAT12/16   */
/* and FAT32 compatible data structures. All structures assume a sector     */
/* size of 512 bytes.                                                       */
/* Make sure that your compiler doesn't optimize the structures and         */
/* use the definitions as-is. That is, set the byte packing to 1.           */
/*                                                                          */
/****************************************************************************/

#ifndef _fatdefs_h
#define _fatdefs_h

// set byte packing to 1
#ifdef __arm
#define PACKED __packed
#else
#define PACKED
#endif

#ifdef _MSC_VER
// set byte packing to 1 (for Visual C), reset at eof
#pragma pack( push, enter_fatdefs, 1 ) 
#endif


#ifdef __cplusplus
extern "C" {
#endif

// definitions for cMediaDescriptor
#define MD_FIXED_DISK           0xF8
#define MD_REMOVABLE_DISK_6     0xF0	// 2.88 MB                 3.5",  2-sided, 36-sectors per track
										// 1.44 MB                 3.5",  2-sided, 18-sectors per track
#define MD_REMOVABLE_DISK_5     0xF9	// 720 KB                  3.5",  2-sided,  9-sectors per track
										// 1.2 MB                  5.25", 2-sided, 15-sectors per track
#define MD_REMOVABLE_DISK_4     0xFD	// 360 KB                  5.25", 2-sided,  9-sectors per track
#define MD_REMOVABLE_DISK_3     0xFF	// 320 KB                  5.25", 2-sided,  8-sectors per track
#define MD_REMOVABLE_DISK_2     0xFC	// 180 KB                  5.25", 1-sided,  9-sectors per track
#define MD_REMOVABLE_DISK_1     0xFE	// 160 KB                  5.25", 1-sided,  8-sectors per track

// definitions for iSignature
#define SIGNATURE_FAT16 0xAA55 // Not unique: applies to FAT, NTFS and HPFS

// definitions for lSignature
#define SIGNATURE_FAT32 0xAA550000 // Not unique: applies to FAT, NTFS and HPFS

// definitions for cExtSignature
#define SIGNATURE_EBR 0x29

// definitions for lEbrSignature32
#define SIGNATURE_EBR32  0x41615252

// definitions for lFsInfoSignature
#define SIGNATURE_FSINFO 0x61417272


// First sector of diskette or FAT12/16 partition, aka BIOS Parameter Block (BPB)
typedef PACKED struct BootSector_FAT16_struct
{ 
	unsigned char  jumpinstr[3];		// 00 - IX86 jump instruction, must be 0xEB, 0x3C, 0x90
	unsigned char  sIdentification[8];	// 03 - OEM ID: i.e. "MSDOS5.0", ..., NOT zero-terminated
	unsigned short nBytesPerSector;		// 0b - should be SECTOR_SIZE=512
	unsigned char  nSectorsPerCluster;	// 0d - should be power of 2 AND cluster size <= 32K (e.g. 1,2,4,16,32,64,128)
	unsigned short nReservedSectors;	// 0e - nr of sectors with bootcode, normally 1
	unsigned char  nFatCopies;			// 10 - normally 2
	unsigned short nRootDirEntries;		// 11 - multiple of size(sector)=512 / size(dir_entry)=32 = 16
	unsigned short nSectors;			// 13 - nr of sectors in this partition (0 if >=64k OR FAT32)
	unsigned char  cMediaDescriptor;	// 15 - 0xF8=fixed disk, 0xF0..0xFF diskettes
	unsigned short nSectorsPerFat;		// 16 - 0 for FAT32; 512 bytes per sector, always 2 byte entries (values = 12 or 16 bit, use 16 !!), first and second entry = copy of byte medium_descr + filling; 16bits:0xF8 0xFF 0xFF 0xFF, 12bits: 0xF0 0xFF 0xFF
	unsigned short nSectorsPerTrack;	// 18 - 
	unsigned short nHeads;				// 1a - 
	unsigned long  nHiddenSectors;		// 1c - same as 'lbaStart' in partition entry for this partition
	unsigned long  nSectors32;			// 20 - nr of sectors in this partition (0 if <64k)
	unsigned short iDriveNumber;		// 24 - Drive number (corr. Int13H)
	unsigned char  cExtSignature;		// 26 - Extended Boot Record signature (29h => following 3 fields are valid)
	unsigned long  lSerialNr;			// 27 - Volume serial number
	unsigned char  sLabel[11];			// 2b - Volume label, filled with spaces, NOT zero-terminated
	unsigned char  sFileSystemID[8];	// 36 - File system id (FAT12, FAT16, FAT), filled with spaces, NOT zero-terminated
	unsigned char  xcode[448];			// 3e - Loader executable code
	unsigned short iSignature;			// 1fe - Magic number (Must be 0xAA55) 
} BootSector_FAT16; // also FAT12

// The 11 bytes starting at xcode (0x3e) are immediately overlaid by information 
// copied from another part of memory. That information is the 
// Diskette Parameter Table. This data is pointed to by INT 1E. This data is:
// 
// 3e = Step rate and head unload time. 
// 3f = Head load time and DMA mode flag. 
// 40 = Delay for motor turn off. 
// 41 = Bytes per sector. 
// 42 = Sectors per track. 
// 43 = Intersector gap length. 
// 44 = Data length. 
// 45 = Intersector gap length during format. 
// 46 = Format byte value. 
// 47 = Head settling time. 
// 48 = Delay until motor at normal speed. 
// The 11 bytes starting at 0000:7c49 are also overlaid by the following data:
// 
// 49 - 4c = diskette sector address (as LBA) of the data area. 
// 4d - 4e = cylinder number to read from. 
// 4f - 4f = sector number to read from. 
// 50 - 53 = diskette sector address (as LBA) of the root directory. 



// first sector of FAT32 partition
typedef PACKED struct BootSector_FAT32_struct
{ 
	unsigned char  jumpinstr[3];		// 00 - IX86 jump instruction, must be 0xEB, 0x3C, 0x90
	unsigned char  sIdentification[8];	// 03 - OEM ID: i.e. "MSWIN4.1", ..., NOT zero-terminated
	unsigned short nBytesPerSector;		// 0b - should be SECTOR_SIZE=512
	unsigned char  nSectorsPerCluster;	// 0d - should be power of 2 AND cluster size <= 32K (e.g. 1,2,4,16,32,64,128)
	unsigned short nReservedSectors;	// 0e - nr of sectors with bootcode, normally 32
	unsigned char  nFatCopies;			// 10 - normally 2
	unsigned short nRootDirEntries;		// 11 - should be zero for FAT32
	unsigned short nSectors;			// 13 - should be zero for FAT32
	unsigned char  cMediaDescriptor;	// 15 - 0xF8=fixed disk, 0xF0..0xFF diskettes
	unsigned short nSectorsPerFat;		// 16 - 0 for FAT32
	unsigned short nSectorsPerTrack;	// 18 - 
	unsigned short nHeads;				// 1a - 
	unsigned long  nHiddenSectors;		// 1c - same as 'lbaStart' in partition entry for this partition
	unsigned long  nSectors32;			// 20 - nr of sectors in this partition (0 if <64k)
	unsigned long  nSectorsPerFat32;	// 24 - Sectors per FAT (FAT32)
	unsigned short cActiveFat;			// 28 - if bit 7 is 1 then bits 0..3 indicate valid #FAT, else all FATs are OK
	unsigned short iFatVersion;			// 2a - should be zero (? is MSB of cActiveFat major version nr ?)
	unsigned long  lRootCluster;		// 2c - Cluster nr of start of root dir (normally 2)
	unsigned short iFSInfoSector;		// 30 - FS Info Sector number inside reserved sectors (normally 1) 
	unsigned short iBootSectorBackup;	// 32 - Boot sector backup (normally 6)	
	unsigned char  reserved1[12];		// 34 - Reserved	
	unsigned char  cDriveNumber;		// 40 - Physical drive number (80h)
	unsigned char  reserved2[1];		// 41 - Reserved	
	unsigned char  cExtSignature;		// 42 - Extended Boot Record signature (29h => following 3 fields are valid)
	unsigned long  lSerialNr;			// 43 - Volume serial number
	unsigned char  sLabel[11];			// 47 - Volume label, filled with spaces, NOT zero-terminated
	unsigned char  sFileSystemID[8];	// 52 - File system id (FAT12, FAT16, FAT), filled with spaces, NOT zero-terminated
	unsigned char  xcode[418];			// 5a - Boot loader code (first part)
	unsigned long  lSignature;			// 1fc - Magic number (Must be 0xAA550000) 	
} BootSector_FAT32;


// Sector 2 
typedef PACKED struct BootSector_FAT32_2_struct
{ 
	unsigned long  lEbrSignature32;		// Ext Boot Record Sign (0x41615252)
	unsigned char  reserved1[480];		// Reserved
	unsigned long  lFsInfoSignature;	// FS Info Signature    (0x61417272)
	unsigned long  nFreeClusters;		// Number of free clusters
	unsigned long  iNextFreeCluster;	// Next free cluster
	unsigned char  reserved2[12];		// Reserved
	unsigned long  lSignature;			// Magic number (Must be 0xAA550000) 	
} BootSector_FAT32_2;


// Sector 3 
typedef PACKED struct BootSector_FAT32_3_struct
{ 
	unsigned char  reserved[508];		// Reserved
	unsigned long  lSignature;			// Magic number (Must be 0xAA550000) 	
} BootSector_FAT32_3;


///////////////////////////////////////////////////////////////////////////////
// Directory table entries

// Time-field in DirEntry [2 byte] 
typedef PACKED struct DosTime_struct
{
	unsigned short Sec    :5; // 0..30   Seconds (div 2)
	unsigned short Min    :6; // 0..59   Minute
	unsigned short Hour   :5; // 0..23   Hour (24-hour)
} DosTime;

// Date-field in DirEntry [2 byte] 
typedef PACKED struct DosDate_struct
{
	unsigned short Day    :5; // 1..31   Day
	unsigned short Month  :4; // 1..12   Month
	unsigned short Year   :7; // 0..128  Year (from 1980 - meaning year=1980+value)
} DosDate;

// Time/date-field in DirEntry [4 byte] 
typedef PACKED struct DosStamp_struct
{
	DosTime time;
	DosDate date;
} DosStamp;

// standard DOS Attributes in a DirectoryEntry [1 byte]
/*
typedef PACKED struct 
{
	unsigned char ReadOnly  :1;
	unsigned char Hidden    :1;
	unsigned char System    :1;

	unsigned char VolumeID  :1;
	unsigned char Directory :1;
	unsigned char Archive   :1;
	unsigned char reserved  :2;
} DosAttributes;
*/
#define FAT_ATTR_READONLY  0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUMEID  0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F // long filename entry

typedef unsigned char DosAttributes;


// Directory entry [32 byte]
typedef PACKED struct DirEntry_struct
{
	char			sName[8];			// First part of 8.3 filename (trailed with spaces)
	char			sExt[3];			// Second part of 8.3 filename (trailed with spaces)
	DosAttributes	cAttributes;		// Bitfield with FAT_ATTR_??? bits. Use DirLfnEntry if this is FAT_ATTR_LFN
	unsigned char	cReservedNT;		// Normally zero
	unsigned char	cDeciSecsCreationTime;	// creation time in 1/10 of a second
	DosStamp		fileCreation;
	DosDate			lastAccessDate;		// Date of last file access (see also lastAccess)
	unsigned short	iStartClusterH;		// High part of start cluster nr (0 for FAT12/16)
	DosStamp		lastAccess;			// Last access date & time
	short			iStartClusterL;		// Low part of start cluster nr
	unsigned long	lSize;				// Total size of cluster chain in bytes, 0 for directory entries

} DirEntry;

unsigned long GetStartCluster(const DirEntry* p);
void SetStartCluster(DirEntry* p, unsigned long s);


//#define DIR_ENTRY_BITSHIFT 4 // instead of mul/div by 32 just shift


#define FAT_LFN_UNUSED  0xFFFF

typedef PACKED struct DirLfnEntry_struct
{
	unsigned char   iSequenceNr : 5; // >0; LFN can consist of max. (2^5-1)*(5+6+2) = 403 char's ???
	unsigned char   bLastEntry  : 1; // 1 if this is last LFN entry of sequence
	unsigned char   iReserved   : 2; // should be 0

	unsigned short  ucPart1[5];  // unused unicode should be FAT_LFN_UNUSED
	DosAttributes	cAttributes; // should be FAT_ATTR_LFN
	unsigned char   cCheckSum;   // CS of short alias that follows this LFN sequence
	unsigned short  ucPart2[6];  // unused unicode should be 0xFFFF
	unsigned short  ucReserved;  // should be 0x0000
	unsigned short  ucPart3[2];  // unused unicode should be 0xFFFF
} DirLfnEntry;

typedef union DirEntryX_union
{
	DirEntry    dirEntry;
	DirLfnEntry lfnEntry;
} DirEntryX;

//typedef DirEntry DirSector[16];

// special values for first character of sName:
#define FAT_FILE_REMOVED  '\xE5' // 
#define FAT_FILE_MAGIC_E5 '\x05'  // should be interpreted as 0xE5
#define FAT_FILE_EOD      '\0'  // indicates end of directory (free entry)

// FAT table: special cluster index values
#define FAT_FREE_CLUSTER   0

#define FAT12_BAD          0x0ff7
#define FAT12_MASK         0x0ff8
#define FAT12_LAST_CLUSTER 0x0fff
#define MAX_CLUST12	       0x0ff4		// Maximum number of clusters in FAT12 system (theoretical)

#define FAT16_BAD          0xfff7
#define FAT16_MASK         0xfff8
#define FAT16_LAST_CLUSTER 0xffff
#define MAX_CLUST16        0xfff4		// Maximum number of clusters in FAT16 system (theoretical)

#define FAT32_BAD          0x0ffffff7
#define FAT32_MASK         0x0ffffff8
#define FAT32_LAST_CLUSTER 0x0fffffff
#define MAX_CLUST32        0x0ffffff4   // Maximum number of clusters in FAT32 system (theoretical)


int SetDosFilename(DirEntry* e, const char* buf/* 8.3 name */); /* returns length of string */
int GetDosFilename(const DirEntry* e, char* buf/*at least 8+1+3+1=13 bytes*/);
int GetDosVolumeID(const DirEntry* e, char* buf/*at least 8+3+1=13 bytes*/, int bRemoveTrailingSpaces);
int CompareDosFilename(const DirEntry* e, const char* buf, int len); // 0 if equal

#ifdef _MSC_VER
// reset byte packing (for Visual C)
#pragma pack( pop, enter_fatdefs )
#endif

#ifdef __cplusplus
}
#endif


#endif //  _fatdefs_h
