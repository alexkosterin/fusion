/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_RESOLVE__H
#define		FUSION_RESOLVE__H

#ifdef	WIN32
# define NOGDI
# define _WINSOCKAPI_
# include <windows.h>
#endif

ULONG resolve_address(const char* name);

#endif  //FUSION_RESOLVE__H
