// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
=======================================================================

FILE:         std_path.c

=======================================================================
=======================================================================
*/

/* To get memrchr on GNU/Linux */
#define _GNU_SOURCE
#include "AEEBufBound.h"
#include "AEEstd.h"
#include <string.h>
/*===========================================================================

===========================================================================*/
int
std_makepath(const char *cpszDir, const char *cpszFile, char *pszOut,
             int nOutLen)
{
	BufBound bb;

	BufBound_Init(&bb, pszOut, nOutLen);

	BufBound_Puts(&bb, cpszDir);

	if(('\0' != cpszDir[0]) && /* non-empty dir */
	   ('/'
	    != cpszDir[strlen(cpszDir) - 1])) { /* no slash at end of dir */
		BufBound_Putc(&bb, '/');
	}
	if('/' == cpszFile[0]) {
		cpszFile++;
	}

	BufBound_Puts(&bb, cpszFile);

	BufBound_ForceNullTerm(&bb);

	return BufBound_Wrote(&bb) - 1;
}

/*===========================================================================

===========================================================================*/
char *
std_splitpath(const char *cpszPath, const char *cpszDir)
{
	const char *cpsz = cpszPath;

	while(!('\0' == cpszDir[0]
	        || ('/' == cpszDir[0] && '\0' == cpszDir[1]))) {

		if(*cpszDir != *cpsz) {
			return 0;
		}

		++cpsz;
		++cpszDir;
	}

	/* Found the filename part of the path.
	   It should begin with a '/' unless there is no filename */
	if('/' == *cpsz) {
		cpsz++;
	} else if('\0' != *cpsz) {
		cpsz = 0;
	}

	return (char *)cpsz;
}

char *
std_cleanpath(char *pszPath)
{
	char *pszStart = pszPath;
	char *pc;
	char *pcEnd = pszStart + strlen(pszStart);

	/* preserve leading slash */
	if('/' == pszStart[0]) {
		pszStart++;
	}

	pc = pszStart;

	while((char *)0 != (pc = strstr(pc, "/."))) {
		char *pcDelFrom;

		if('/' == pc[2] || '\0' == pc[2]) {
			/*  delete "/." */
			pcDelFrom = pc;
			pc += 2;
		} else if('.' == pc[2] && ('/' == pc[3] || '\0' == pc[3])) {
			/*  delete  "/element/.." */
			pcDelFrom = memrchr(pszStart, '/', pc - pszStart);
			if(!pcDelFrom)
				pcDelFrom = pszStart;
			pc += 3;
		} else {
			pc += 2;
			continue;
		}

		memmove(pcDelFrom, pc, pcEnd - pcDelFrom);

		pc = pcDelFrom;
	}

	/* eliminate leading "../" */
	while(pszStart == strstr(pszStart, "../")) {
		memmove(pszStart, pszStart + 2, pcEnd - pszStart);
	}

	/* eliminate leading "./" */
	while(pszStart == strstr(pszStart, "./")) {
		memmove(pszStart, pszStart + 1, pcEnd - pszStart);
	}

	if(!strncmp(pszStart, "..", 2) || !strncmp(pszStart, ".", 1)) {
		pszStart[0] = '\0';
	}

	/* whack double '/' */
	while((char *)0 != (pc = strstr(pszPath, "//"))) {
		memmove(pc, pc + 1, pcEnd - pc);
	}

	return pszPath;
}

char *
std_basename(const char *cpszFile)
{
	const char *cpsz;

	if((char *)0 != (cpsz = strrchr(cpszFile, '/'))) {
		cpszFile = cpsz + 1;
	}

	return (char *)cpszFile;
}
