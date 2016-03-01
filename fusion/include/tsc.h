/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_TSC_H
#define		FUSION_TSC_H

#include "include/nf.h"
#include "include/configure.h"

#if	defined(WIN32)
  struct _FILETIME;
#endif

namespace nf {
  msecs_t now_msecs();

#if	defined(WIN32)
  msecs_t filetime_to_msecs(const _FILETIME&);
  _FILETIME msecs_to_filetime(msecs_t);
#endif

  time_t msecs_to_unix(msecs_t msecs);
  msecs_t unix_to_msecs(time_t time);
}

#endif		//FUSION_TSC_H
