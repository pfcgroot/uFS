
#ifndef DEBUGUFS_H_INCLUDED
#define DEBUGUFS_H_INCLUDED


#if defined(_DEBUG) && !defined(DEBUG)
	#define DEBUG
#endif

#if defined(DEBUG) && !defined(_DEBUG)
	#define _DEBUG
#endif


#ifdef DEBUG
	#include <assert.h>
	#define ASSERT(a) assert(a)

	// #define TRACE_UFS_CACHE	// define to include cache tracing
	// #define TRACE_UFS_DETAILED	// define to include detailed tracing
#else
	#define ASSERT(a)
#endif


// -DDEBUG -DUFSTRACE	OK
// -DDEBUG 			 	OK
// -DUFSTRACE			ERROR\als -DDEBUG in doel project meedoet en AssertValid() in uFS.h wordt gedeclareerd
// 						ERROR/
#if defined(DEBUG) && defined(UFSTRACE)

#ifdef TRACE0
	#define TRACEUFS0(a) TRACE0(a)
	#define TRACEUFS1(a,b) TRACE1(a,b)
	#define TRACEUFS2(a,b,c) TRACE2(a,b,c)
	#define TRACEUFS3(a,b,c,d) TRACE3(a,b,c,d)
	#define TRACEUFS4(a,b,c,d,e) TRACE4(a,b,c,d,e)
#else
	#define TRACEUFS0(a) printf(a)
	#define TRACEUFS1(a,b) printf(a,b)
	#define TRACEUFS2(a,b,c) printf(a,b,c)
	#define TRACEUFS3(a,b,c,d) printf(a,b,c,d)
	#define TRACEUFS4(a,b,c,d,e) printf(a,b,c,d,e)
#endif

#ifndef VERIFY
	#define VERIFY(a)   { int b=(a); ASSERT(b); }
#endif

#else		// #ifdef DEBUG && UFSTRACE

	#define TRACEUFS0(a)
	#define TRACEUFS1(a,b)
	#define TRACEUFS2(a,b,c)
	#define TRACEUFS3(a,b,c,d)
	#define TRACEUFS4(a,b,c,d,e)

#ifndef VERIFY
	#define VERIFY(a)   (a)
#endif

#endif		// #ifdef DEBUG && UFSTRACE

#ifdef TRACE_UFS_DETAILED
	#define TRACEUFS0_DET TRACEUFS0
	#define TRACEUFS1_DET TRACEUFS1
	#define TRACEUFS2_DET TRACEUFS2
	#define TRACEUFS3_DET TRACEUFS3
	#define TRACEUFS4_DET TRACEUFS4
#else
	#define TRACEUFS0_DET(a)
	#define TRACEUFS1_DET(a,b)
	#define TRACEUFS2_DET(a,b,c)
	#define TRACEUFS3_DET(a,b,c,d)
	#define TRACEUFS4_DET(a,b,c,d,e)
#endif



#endif 		// #ifndef DEBUGUFS_H_INCLUDED

