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

#ifndef _MSC_VER
#include "stdafx.h"
#endif

#include "fatdefs.h"
#include <ctype.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned long GetStartCluster(const DirEntry* p) { return p->iStartClusterL | (p->iStartClusterH<<16); }
void SetStartCluster(DirEntry* p, unsigned long s) { p->iStartClusterL=(unsigned short)s; p->iStartClusterH=(unsigned short)(s>>16); }


static const char _szSpecialDosChars[] = "-+=;,&$%_@[]{}~'`!#()\x96"; // 0x96 is a hard hyphen

static int IsValidDosnameChar(int c)
{
	return isalnum(c) || strchr(_szSpecialDosChars,c)!=NULL;
}

static const char* _szReservedDosFilename[] =
{
	"NUL",
	"COM1",
	"COM2",
	"COM3",
	"COM4",
	"LPT1",
	"LPT2",
	"LPT3",
	"PRN",
};

static int IsReservedDosFilename(const char* szName)
{
	int i;
	for (i=0; i<sizeof(_szReservedDosFilename)/sizeof(_szReservedDosFilename[0]); i++)
	{
		if (stricmp(_szReservedDosFilename[i], szName)==0)
			return 1;
	}
	return 0;
}

int CompareDosFilename(const DirEntry* e, const char* szName, int len)
{
	// TODO: can optimize this by comparing directly the strings in e with buf
	//       instead of retrieving the complete name first
	//
	// Ignore len if <=0

	int result = -1;
	char cFirstChar = e->sName[0];
	switch (cFirstChar)
	{
	case FAT_FILE_REMOVED:
		break;

	case FAT_FILE_MAGIC_E5:
		cFirstChar = FAT_FILE_REMOVED;
		// fall through
	default:
		// optimization note: only compare full name if first character matches
		result = toupper(cFirstChar) - toupper(*szName);
		if (result==0)
		{
			char buf[14];
			if (GetDosFilename(e, buf))
				result = len>0 ? strnicmp(buf,szName,len) : stricmp(buf,szName);
			else
				result = -1;
		}
	}
	return result;
}

int SetDosFilename(DirEntry* e, const char* buf/* 8.3 name */)
{
	int k;
	int len=0;
	const char* p = buf;
	if (IsReservedDosFilename(buf))
		return 0;
	for (k=0; k<8; k++)
	{
		const char c = *p;

		if (*p=='.' || *p=='\0')
		{
			if (len==0)
				goto _resetentry; // empty base name is not valid

			e->sName[k] = ' ';
			continue;
		}

		if (!IsValidDosnameChar(c))
			goto _resetentry;

		e->sName[k] = toupper(c);
		len++;
		p++;
	}
	if (*p=='.') 
	{
		p++; // skip dot
		len++;
	}
	for (k=0; k<3; k++)
	{
		const char c = *p;
		if (*p=='\0')
		{
			e->sExt[k] = ' ';
			continue;
		}

		if (!IsValidDosnameChar(c))
			goto _resetentry;

		e->sExt[k] = toupper(c);
		len++;
		p++;
	}
	// all characters should be stored!
	if (*p!='\0')
		goto _resetentry;

	return len;

_resetentry:
	e->sName[0] = FAT_FILE_REMOVED;
	return 0;
}

int GetDosFilename(const DirEntry* dir, char* buf/*at least 8+1+3+1=13 bytes*/)
{
	int k;
	int len=0;
	const char* p;

	for (k=0, p=dir->sName; k<8; k++, p++)
	{
		char c = *p;
		if (c==FAT_FILE_MAGIC_E5 && k==0)
			c = FAT_FILE_REMOVED;
		if (c!=' ')
		{
			if (!IsValidDosnameChar(c))
				return 0;
			buf[len++] = c;
		}
		else
			break;
	}
	if (len>0)
	{
		for (k=0, p=dir->sExt; k<3; k++, p++)
		{
			const char c = *p;
			if (c!=' ')
			{
				if (!IsValidDosnameChar(c))
					return 0;
				if (k==0)
					buf[len++] = '.';
				buf[len++] = c;
			}
			else
				break;
		}
	}
	buf[len] = '\0';
	return len;
}

int GetDosVolumeID(const DirEntry* dir, char* buf/*at least 8+1+3+1=13 bytes*/, int bRemoveTrailingSpaces)
{
	// this function differs from GetDosFilename only because spaces are allowed 
	// inside volume names
	int k;
	int len=0;
	const char* p;

	for (k=0, p=dir->sName; k<8; k++, p++)
	{
		char c = *p;
		if (c==FAT_FILE_MAGIC_E5 && k==0) // not sure, but doesn't harm
			c = FAT_FILE_REMOVED;
		buf[len++] = c;
	}
	if (len>0)
	{
		for (k=0, p=dir->sExt; k<3; k++, p++)
		{
			const char c = *p;
			buf[len++] = c;
		}
	}
	// remove trailing spaces?
	if (bRemoveTrailingSpaces)
		while (len>0 && buf[len-1]==' ')
			len--;
	buf[len] = '\0';
	return len;
}

#ifdef __cplusplus
}
#endif
