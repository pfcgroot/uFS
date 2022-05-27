#ifndef __pagedfile_h
#define __pagedfile_h

#ifndef _WINDOWS_
#include <windows.h>
#endif

#ifndef ASSERT
#define ASSERT(a)
#endif


///////////////////////////////////////////////////////////////////////////////
// FileMappingCore
// This class encapsulates the win32 mechanism for managing file mappings.
// By using this mechanism you can map the contents of new or existing
// files into your virtual (paged) memory.

class FileMappingAPI
{
public:
	FileMappingAPI() : 
		m_hFile(INVALID_HANDLE_VALUE), 
		m_hFileMapping(NULL), 
		m_dwFileAccess(0)
		{ }

	virtual ~FileMappingAPI() 
	{ 
		CloseFile(); 
	}

	BOOL __fastcall CreateFile(const TCHAR* strFilename, BOOL bAllowCreation=FALSE, DWORD dwDesiredAccess=GENERIC_WRITE|GENERIC_READ)
	{
		m_hFile = ::CreateFile(
			 strFilename,
			 dwDesiredAccess,
			 (dwDesiredAccess==GENERIC_READ?FILE_SHARE_READ:0) /* win32 documentation proposes no sharing, but read-only should be OK */,
			 NULL,
			 (bAllowCreation?OPEN_ALWAYS:OPEN_EXISTING),
			 FILE_ATTRIBUTE_NORMAL,
			 NULL
			);	
		m_dwFileAccess = dwDesiredAccess;
		return IsOpen();
	}

	bool __fastcall IsOpen() const
	{
		return m_hFile!=INVALID_HANDLE_VALUE;
	}

	void __fastcall CloseFile() 
	{ 
		CloseFileMapping(); 
		if (IsOpen()) 
		{ 
			CloseHandle(m_hFile); 
			m_hFile=INVALID_HANDLE_VALUE; 
		} 
	}

	BOOL __fastcall CreateFileMapping(LONGLONG int64MaximumSize=0)
	{
		LARGE_INTEGER t; t.QuadPart=int64MaximumSize;
		ASSERT(m_hFile!=INVALID_HANDLE_VALUE); // first close mapping then the file
		m_hFileMapping = ::CreateFileMapping(
			 m_hFile,
			 NULL, 
			 /*SEC_NOCACHE|*/((m_dwFileAccess&GENERIC_WRITE) ? PAGE_READWRITE: PAGE_READONLY),
			 t.HighPart,
			 t.LowPart,
			 NULL
			);
		return m_hFileMapping!=NULL;
	}

	virtual void __fastcall CloseFileMapping() 
	{ 
		if (m_hFileMapping) 
		{ 
			CloseHandle(m_hFileMapping); 
			m_hFileMapping=NULL; 
		} 
	}

	char* __fastcall MapViewOfFile(
		 LONGLONG int64FileOffset=0,
		 DWORD dwNumberOfBytesToMap=0,
		 DWORD dwDesiredAccess=FILE_MAP_WRITE
		) const
	{
		LARGE_INTEGER t; t.QuadPart=int64FileOffset;
		return (char*)::MapViewOfFile(
				m_hFileMapping,			// file-mapping object to map into address space  
				dwDesiredAccess,		// access mode 
				t.HighPart,				// high-order 32 bits of file offset 
				t.LowPart,				// low-order 32 bits of file offset 
				dwNumberOfBytesToMap 	// number of bytes to map 
				);	
	}
 
	BOOL __fastcall UnmapViewOfFile(void* p) const
	{
		return ::UnmapViewOfFile(p);
	}

	HANDLE __fastcall GetMapHandle() const { return m_hFileMapping; }
	HANDLE __fastcall GetFileHandle() const { return m_hFile; }
	DWORD __fastcall GetFileAccess() const { return m_dwFileAccess; }
	bool __fastcall IsReadOnly() const { return (GetFileAccess()&GENERIC_WRITE)==0; }

	LONGLONG GetFileSize() const 
	{ 
		if (!IsOpen()) return 0;
		ULARGE_INTEGER l;
		l.LowPart = ::GetFileSize(m_hFile, &l.HighPart);
		return l.QuadPart;
	}

private:
	DWORD	m_dwFileAccess; // file opened in read and/or write mode
	HANDLE	m_hFile;
	HANDLE	m_hFileMapping;
};

///////////////////////////////////////////////////////////////////////////////
// FileMapping
// This class extends the FileMappingCore class so that it can be used
// to create a mapping for a compete file at once.
// First call CreateFileMapping() to attach or create the desired file.
// Then use GetDataPtr() to get a pointer to the complete paged data file.

class FileMapping : public FileMappingAPI
{
public:
	FileMapping() : 
		m_pData(NULL)
		{ }

	virtual ~FileMapping() 
		{ UnmapDataPtr(); }

	// The CreateFileMapping() lets you attach (or create) a mapping (i.e. paging) file
	// to this mapping object. Use GetDataPtr() to retrieve a pointer to the complete
	// file.
	// Arguments:
	// - strFilename		is the name of the paging file. If the file exists, you can set
	//							dwMaximumSize to 0 to map the complete file. This is useful if
	//							the file isn't allowed to grow.
	// - bAllowCreation  specifies wether the file may be created or must exist.
	// - dwMaximumSize	is the size of the address space which will be reserved for this
	//							paging file
	BOOL __fastcall CreateFileMapping(const TCHAR* strFilename, BOOL bAllowCreation=FALSE, DWORD dwDesiredAccess=GENERIC_WRITE|GENERIC_READ, LONGLONG dwMaximumSize=0UL)
	{
		return CreateFile(strFilename, bAllowCreation, dwDesiredAccess)
			&& FileMappingAPI::CreateFileMapping(dwMaximumSize);
	}

	void __fastcall CloseFile() { UnmapDataPtr(); FileMappingAPI::CloseFile(); }

	// The following function lets you access the whole file at once,
	// without the need of performing multiple and delicate unmapping calls.
	char* __fastcall GetDataPtr(/*DWORD dwDesiredAccess=FILE_MAP_WRITE*/)
	{ 
		// map whole file 
		if (NULL==m_pData)
			m_pData = MapViewOfFile(0L,0L,(GetFileAccess()&GENERIC_WRITE ? FILE_MAP_WRITE : FILE_MAP_READ));
		return m_pData; 
	}

	const char* __fastcall GetDataPtr() const
	{ 
		// map whole file 
		if (NULL==m_pData)
			const_cast<FileMapping*>(this)->m_pData = MapViewOfFile(0L,0L,(GetFileAccess()&GENERIC_WRITE ? FILE_MAP_WRITE : FILE_MAP_READ));
		return m_pData; 
	}

	// This function unmaps the one and only mapping supported by this
	// implementation. It is automatically called at destruction time.
	void __fastcall UnmapDataPtr()
	{
		if (m_pData)
		{
			UnmapViewOfFile(m_pData);
			m_pData = NULL;
		}
	}

protected:
	virtual void __fastcall CloseFileMapping() 
	{ 
		UnmapDataPtr();
		FileMappingAPI::CloseFileMapping();
	}

private:
	char*	m_pData;		// points to whole file contents a.s.a. the user
							// calls GetDataPtr()
};


///////////////////////////////////////////////////////////////////////////////
// class PagedFile<MemoryFormatter>
// To be able to access a formatted file you can use an MemoryFormatter class that can
// be attached to a memory object, i.e. a paged file.
// The PagedFile class is a simple class to map existing files into virtual
// memory. The class is used to extend an existing MemoryFormatter class, which
// only has to implement an interface for GetEOF(), AttachDataPtr() and DetachDataPtr()
// calls.

template <class MemoryFormatter>
class PagedFile : public MemoryFormatter
{
public:
	PagedFile() : m_bAttached(false) { }
	virtual ~PagedFile() { CloseFile(); }

	bool __fastcall IsOpen() const
		{ return m_bAttached; }

	bool __fastcall IsReadOnly() const 
		{ return m_pagedFile.IsReadOnly(); }

	bool __fastcall CreateNewFile(const char* szFilename, LONGLONG nBytes)
	{
		// assert if already attached to memory object
		ASSERT(m_bAttached==false);

		// Calculate file size needed to hold nChannels*nSamplesPerChannel samples
		// and round up to systems page size (4096==0x1000)
		if (nBytes&0xfff)
			nBytes = (nBytes+0x1000)&~0xfff;
		m_bAttached = m_pagedFile.CreateFileMapping(szFilename, TRUE, GENERIC_WRITE|GENERIC_READ, nBytes)!=FALSE;
		char* pData = m_bAttached ? m_pagedFile.GetDataPtr() : NULL;
		if (pData)
		{
			m_bAttached = AttachDataPtr(pData, true);
			if (!m_bAttached)
				m_pagedFile.CloseFile();
		}
		return m_bAttached;
	}

	bool __fastcall OpenExistingFile(const char* strFilename, DWORD dwDesiredAccess=GENERIC_WRITE|GENERIC_READ)
	{
		ASSERT(m_bAttached==false);
		if (!m_pagedFile.CreateFileMapping(strFilename, FALSE, dwDesiredAccess, 0UL))
			return false;

		char* pData = m_pagedFile.GetDataPtr();
		if (pData)
			m_bAttached = MemoryFormatter::AttachDataPtr(pData, false);
		else
			m_bAttached = false;
		if (!m_bAttached)
			m_pagedFile.CloseFile();
		return m_bAttached;
	}

	void __fastcall CloseFile()
	{
		if (m_bAttached)
		{
			MemoryFormatter::DetachDataPtr();
			m_bAttached = false;
		}
		m_pagedFile.CloseFile();
	}

	LONGLONG __fastcall GetCapacity() const { ULARGE_INTEGER li; li.LowPart = GetFileSize(m_pagedFile.GetFileHandle(),&li.HighPart); return li.QuadPart; }
	LONGLONG __fastcall GetBytesInUse() const { return ((char*)GetEOF()) - ((char*)GetBOF()); }
	LONGLONG __fastcall GetBytesFree() const { return GetCapacity() - GetBytesInUse(); }

	bool __fastcall Grow(LONGLONG nBytesToGrow)
	{
		bool b = true;
		LONGLONG nBytesFree = GetBytesFree();
		if (nBytesToGrow>nBytesFree)
		{
			LONGLONG n = GetCapacity()+nBytesToGrow;
			if (n&0xfff)
				n = (n+0x1000)&~0xfff;
			DetachDataPtr();
			m_pagedFile.UnmapDataPtr();
			b = m_pagedFile.FileMappingAPI::CreateFileMapping(n)==TRUE;
			if (b)
			{
				char* pData = m_pagedFile.GetDataPtr();
				if (pData)
				{
					m_bAttached = AttachDataPtr(pData, false);
					if (!m_bAttached)
					{
						m_pagedFile.CloseFile();
						b = false;
					}
				}
				else
					b = false;
			}
		}
		return b;
	}

protected:
	FileMapping m_pagedFile;
	bool m_bAttached;
};

///////////////////////////////////////////////////////////////////////////////
// class IndexedFileMemoryFormatter<Header, Block>
// This MemoryFormatter class is used for accessing memory that is formatted as 
// indexed sequential blocks of data. The class must implements two
// functions to be able to use it as MemoryFormatter for a paged memory file:
// AttachDataPtr() and DetachDataPtr(). The file format is build around
// 3 structures: 1) a table for storing block offsets (internal use only), 
// 2) a user defined header (H) and 3) a user defined block structure.
// The offset table is recontructed in memory to be able to store pointers
// as soon as a pointer is attached.

template <class H, class B>
class IndexedFileMemoryFormatter
{
public:
	IndexedFileMemoryFormatter() : m_lFileVersion(0), m_pData(NULL) {}

// public interface to internal data structures:
	H* __fastcall GetHeaderPtr() { return (H*)(m_pData!=NULL ? m_pData+GetPreHeaderSize() : NULL); }
	const H* __fastcall GetHeaderPtr() const { return (const H*) (m_pData!=NULL ? m_pData+GetPreHeaderSize() : NULL); }

	B* __fastcall GetBlockPtr(unsigned long iBlock) { return (B*)(m_indexTable.GetBlockPtr(iBlock)); }
	const B* __fastcall GetBlockPtr(unsigned long iBlock) const { return (B*)(m_indexTable.GetBlockPtr(iBlock)); }

	long __fastcall GetNrOfBlocks() const { return m_indexTable.GetNrOfBlocks(); }
	unsigned long __fastcall GetFileVersion() const { return m_lFileVersion; }

	void* __fastcall GetEOF() const { ASSERT(FALSE); return NULL; }
	void* __fastcall GetBOF() const { ASSERT(FALSE); return m_pData; }

	bool HasData() const { return m_pData!=NULL; }

protected:
	char* m_pData;

// MemoryFormatter interface:
	bool __fastcall AttachDataPtr(char* pData, bool bNew)
	{
		ASSERT(bNew==false);
		m_pData = pData;
		unsigned long* lData = (unsigned long*)pData;
		m_lFileVersion = lData[0];
		unsigned long nBlocks = lData[1];
		m_indexTable.InitializeBlockPointers(nBlocks, lData+2, pData);
		return true;
	}

	void __fastcall DetachDataPtr()
	{
		m_indexTable.Empty();
		m_pData = NULL;
	}

// implementation:
	unsigned long __fastcall GetPreHeaderSize() const 
		{ return 2UL*sizeof(unsigned long) + GetTableSize(); }
	unsigned long __fastcall GetTableSize() const // align the table to 8 byte boundary, this speeds things up on pentiums
		{ unsigned long n = GetNrOfBlocks(); if (n&0x0001) n++; return n*sizeof(unsigned long); }
	unsigned long m_lFileVersion;

	// index table
	class IndexFileTable
	{
	public:
		IndexFileTable() : m_nBlocks(0UL), m_pBlocks(NULL) 
			{}
		~IndexFileTable() 
			{ delete [] m_pBlocks; }

		void __fastcall Empty() 
			{
				delete [] m_pBlocks; m_pBlocks = NULL;
				m_nBlocks = 0;
			}

		// user interface
		void* __fastcall GetBlockPtr(unsigned long iBlock) const 
			{ ASSERT(iBlock<m_nBlocks); return m_pBlocks[iBlock]; }
		unsigned long __fastcall GetNrOfBlocks() const 
			{ return m_nBlocks; }

		void __fastcall InitializeBlockPointers(unsigned long nBlocks, unsigned long* pOffsets, char* pAnchor)
		{
			delete [] m_pBlocks; m_pBlocks = NULL;
			if (nBlocks)
			{
				m_pBlocks = new void*[nBlocks];
				ASSERT(m_pBlocks);
				for (unsigned long i=0; i<nBlocks; i++)
					m_pBlocks[i] = (void*)(pAnchor + pOffsets[i]);
			}
			m_nBlocks = nBlocks;
		}

	private:
		unsigned long m_nBlocks;
		void** m_pBlocks;
	};

	IndexFileTable m_indexTable;
};

///////////////////////////////////////////////////////////////////////////////
//

template <class H, class B>
class IndexedFile : public PagedFile< IndexedFileMemoryFormatter<H,B> >
{
public:
	IndexedFile() {}
};



///////////////////////////////////////////////////////////////////////////////
// class IndexedFileMemoryFormatter<Header, Block>
// This MemoryFormatter class is used for accessing memory that is formatted as 
// indexed sequential ficed size records of data. The class must implements 4
// functions to be able to use it as MemoryFormatter for a paged memory file:
// GetBOF(), GetEOF, AttachDataPtr() and DetachDataPtr(). The file format is build around
// 2 structures: 1) a user defined header (H) and 2) a user defined record structure.
// The header class that is being used for building this structure must also implement
// an interface: GetNrOfRecords().

template <class H, class R>
class ISAMFormatter
{
public:
	ISAMFormatter() : m_pHeader(NULL), m_pRecords(NULL) {}

// public interface to internal data structures:
	H* __fastcall GetHeaderPtr() { return m_pHeader; }
	const H* __fastcall GetHeaderPtr() const { return m_pHeader; }

	R* __fastcall GetRecordPtr(long iRec) 
	{ 
		ASSERT(iRec<GetNrOfRecords()); 
		return (R*)( ((char*)m_pRecords) + iRec*GetRecordSize() ); 
	}
	const R* __fastcall GetRecordPtr(long iRec) const 
	{ 
		ASSERT(iRec<GetNrOfRecords()); 
		return (const R*)( ((const char*)m_pRecords) + iRec*GetRecordSize() ); 
	}

	long __fastcall GetNrOfRecords() const { return m_pHeader ? m_pHeader->GetNrOfRecords(): 0; }

protected:
	H* m_pHeader;
	R* m_pRecords;

	virtual int GetHeaderSize() const { return sizeof(H); }
	virtual int GetRecordSize() const { return sizeof(R); }

// MemoryFormatter interface:
	void* __fastcall GetEOF() const { ASSERT(FALSE); return NULL; }
	void* __fastcall GetBOF() const { return m_pHeader; }

	bool __fastcall AttachDataPtr(char* pData, bool bNew)
	{
		m_pHeader = (H*)pData;
		if (!bNew)
		{
			m_pRecords = (R*)(((char*)m_pHeader)+GetHeaderSize());
		}
		return true;
	}

	void __fastcall DetachDataPtr()
	{
		m_pHeader = NULL;
		m_pRecords = NULL;
	}
};

template <class H, class R>
class ISAMFile : public PagedFile< ISAMFormatter<H,R> > // i.e. derived from ISAMFormatter
{
public:
	ISAMFile() {}
};


#endif
