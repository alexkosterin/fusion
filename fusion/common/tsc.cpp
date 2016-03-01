/*
 *  FUSION
 *  Copyright (c) 2012-2014 Alex Kosterin
 */

#include "include/tsc.h"
#include "include/configure.h"
#include "include/nf_macros.h"
#include <stdlib.h>
#include <stdio.h>

// difference in milliseconds between Microsoft and UNIX time bases (Jan 1, 1601 and Jan 1, 1970 (all UTC))
#define UNIX_TO_MSFILETIME_DIFFERENCE_MSECS 11644473600000LL

// msecs_t type is:
//  64-bit value representing the number of milliseconds since Jan 1, 1970 (UTC)
//  convert to/from UNIX:        change scale msecs <--> secs
//  convert to/from MS FILETIME: change scale msecs <--> 10000 nanosecs and change base using UNIX_TO_MSFILETIME_DIFFERENCE_MSECS
// Note:
//   Maximum time range 32bit unsigned value in milliseconds can hold is roughly 40 days

namespace nf {
#if defined(WIN32)

  msecs_t filetime_to_msecs(const _FILETIME& ft) { /////////////////////////////
    ULARGE_INTEGER li;

    li.LowPart	= ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;

    return (msecs_t)((li.QuadPart / 10000LL) - UNIX_TO_MSFILETIME_DIFFERENCE_MSECS);
  }

  msecs_t now_msecs() { ////////////////////////////////////////////////////////
    FILETIME now;

    ::GetSystemTimeAsFileTime(&now);

    return filetime_to_msecs(now);
  }

  _FILETIME msecs_to_filetime(msecs_t msecs) { /////////////////////////////////
    ULARGE_INTEGER li;

    li.QuadPart = (msecs + UNIX_TO_MSFILETIME_DIFFERENCE_MSECS) * 10000LL;

    _FILETIME ft = { li.LowPart, li.HighPart };

    return ft;
  }
#endif  //WIN32

  time_t msecs_to_unix(msecs_t msecs) { ////////////////////////////////////////
#if 0
    ::SYSTEMTIME  st;
    ::FILETIME    ft;
    ::ULARGE_INTEGER* puli;

    st.wYear          = 1970;
    st.wMonth         = 1;
    st.wDay           = 1;
    st.wHour          = 0;
    st.wMinute        = 0;
    st.wSecond        = 0;
    st.wMilliseconds  = 0;

    BOOL rc = ::SystemTimeToFileTime(&st, &ft);

    FUSION_ASSERT(rc);

    puli = (::ULARGE_INTEGER*)&ft;
    msecs_t _UNIX_TO_MSFILETIME_DIFFERENCE_MSECS = puli->QuadPart / 10000LL;

    *(msecs_t*)&ft = _UNIX_TO_MSFILETIME_DIFFERENCE_MSECS * 10000LL;
    rc = ::FileTimeToSystemTime(&ft, &st);

    FUSION_ASSERT(rc);

    FUSION_ASSERT(st.wYear         == 1970);
    FUSION_ASSERT(st.wMonth        == 1);
    FUSION_ASSERT(st.wDay          == 1);
    FUSION_ASSERT(st.wHour         == 0);
    FUSION_ASSERT(st.wMinute       == 0);
    FUSION_ASSERT(st.wSecond       == 0);
    FUSION_ASSERT(st.wMilliseconds == 0);
#endif

    FUSION_ENSURE(msecs > 0, msecs = 0);

    return msecs / 1000;
  }

  msecs_t unix_to_msecs(time_t t) { ////////////////////////////////////////////
    FUSION_ENSURE(t > 0, t = 0);

    return t * 1000;
  }
}
