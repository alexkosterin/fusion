/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef   FUSION_CONFIGURE_H
#define   FUSION_CONFIGURE_H

#ifdef __DLL__
# define FUSION_EXPORT __declspec(dllexport)
#else
# define FUSION_EXPORT __declspec(dllimport)
#endif

#pragma warning (disable: 4341)     // signed value is out of range for enum constant
#pragma warning (disable: 4810)     // value of pragma pack(show) == n
#pragma warning (disable: 4103)     // alignment changed after including header, may be due to missing #pragma pack(pop)
#pragma warning (disable: 4996)     // use secure string funcs
#pragma warning (disable: 4200)     // zero-sized array in struct/union
#pragma warning (disable: 4800)     // forcing value to bool 'true' or 'false'
#pragma warning (disable: 6255)     // alloca not exceptions safe
#pragma warning (disable: 6282)     // assignmets in if (...)

//#pragma pack(push, mb, 1)
//#pragma pack(show)

#define FUSION_CALLING_CONVENTION           __stdcall
#define FUSION_IN
#define FUSION_OUT
#define FUSION_INOUT

// Note: must be more savvy - syntax is diffrent for MSVC and GCC
#if defined(_MSC_VER)
# define FUSION_DEPRECATRED                 __declspec(deprecated)
#elif defined(_MSC_VER)
# define FUSION_DEPRECATRED                 __attribute__ ((deprecated))
#else
# define FUSION_DEPRECATRED
#endif // DEBUG

#define MAX_MESSAGE_NAME_LENGTH	            (MAX_PATH)

#define MBM_PENDING_NET_MESSAGES_THRESHOLD  (64)
#define MBM_DISPLAY_MESSAGES_PERIOD         (60000)
#define MB_PENDING_NET_MESSAGES_THRESHOLD   (16)
#define MBM_SUBSCRBE_IS_ALWAYS_PUBLISH      1
#define MB_UNREGISTER_CLOSES_CONNECTION     1
#define MB_SHUTDOWN_TIMEOUT_MSECS           (1000)

#define USE_VECTORED_IO                     1

#ifdef _DEBUG
#define MB_DEFAULT_TIMEOUT_MSECS            (10*60*1000)
#else
#define MB_DEFAULT_TIMEOUT_MSECS            (20*1000)
#endif

#define MBM_DEFAULT_FLUSH_PERSISTENT_DATA   (false)

#	define INVALID_NAME_CHARS                 ":<>|"

#define PROFILE_MESSAGE_SEPARATOR_CHAR      ':'

#define PROFILE_PATH_SEPARATOR_CHAR         ':'
#define PROFILE_COMMENT_CHARS               ";#"

#define MBM_ENFORCE_INIT_PERSISTENT_MESSAGE (0)

#ifdef _DEBUG
# define  PREALLOCATED_MCBS                 (1*256*1024)
# define  CLOCK_SYNC_MIN_PERIOD             (1000)
# define  MBM_DEFAULT_OUTPUT_QUEUE_LIMIT    (1000)
# define  MBM_PQ_SORTING                    (1)

# define DEBUG_HEAP                         (1)

# if DEBUG_HEAP > 0
#  define _CRTDBG_MAP_ALLOC
#  include <stdlib.h>
#  include <crtdbg.h>
# endif
#else
#define DEBUG_HEAP                          (0)

# define  PREALLOCATED_MCBS                 (1000000)
# define  CLOCK_SYNC_MIN_PERIOD             (10000)
# define  MBM_DEFAULT_OUTPUT_QUEUE_LIMIT    (10000)
# define  MBM_PQ_SORTING                    (0)
#endif

// profiles //
#define MBM_PERMISSIVE_PROFILE              (0)
#define MBM_DEFAULT_PROFILE                 "unknown"
#define LOCK_FILENAME                       ".lock"
#define PROFILE_DIRNAME                     ".profiles"

#define SHARING_RETRY_MAX                   (16)
#define MD_KEEP_NAME                        (1)
#define MD_KEEP_PATH                        (1)

// mbpool
#define DISPOSE_MBPOOL                      (0)
#define CHECK_UNIQ_MBPOOL_PUT               (0)
#define MCBPOOL_KEEP_COUNT                  (1)

#define MB_TCP_NO_DELAY                     (1)
#define MBM_TCP_NO_DELAY                    (1)

#define MBM_LISTEN_BACKLOG                  (32)
#define MBM_LISTEN_TIMER_MSECS              (300000)  // once in 5 mins

// stress test partial writes
//#define TEST_PARTIAL_WRITES               (100) // if defined, then 1/TEST_PARTIAL_WRITES is a ratio of partial writes
                                                  // that is '2' means 50% (1/2) of all writes will partial
#if	defined(WIN32)
# define NOGDI
# define _WINSOCKAPI_
# include <windows.h>
#endif	//WIN32

#define OUTPUT_USE_COLOUR
#define OUTPUT_SHOW_TS

#endif		//FUSION_CONFIGURE_H

