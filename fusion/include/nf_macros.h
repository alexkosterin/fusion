/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_NF_MACROS__H
#define		FUSION_NF_MACROS__H

#include <windows.h>
#include <include/configure.h>

#define _STRINGIFY(S)	#S
#define STRINGIFY(S)	_STRINGIFY(S)

#ifdef OUTPUT_SHOW_TS
#include <windows.h>

# define _FUSION_TS_ do {                     \
  SYSTEMTIME _st;                             \
                                              \
  GetLocalTime(&_st);                         \
                                              \
  fprintf(stderr, "%0.2d:%0.2d:%0.2d:%0.3d ", \
    _st.wHour,                                \
    _st.wMinute,                              \
    _st.wSecond,                              \
    _st.wMilliseconds                         \
  );                                          \
} while (0)
#else
# define _FUSION_TS_ do {} while (0)
#endif

#ifdef OUTPUT_USE_COLOUR
# include <windows.h> 

# define _FUSION_SET_COLOUR_(A)                                       \
  CONSOLE_SCREEN_BUFFER_INFO scbi;                                    \
                                                                      \
  GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &scbi);  \
  SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE), A);

# define _FUSION_RESTORE_COLOUR_ \
  SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE), scbi.wAttributes); 
#else
# define _FUSION_SET_COLOUR_(A)
# define _FUSION_RESTORE_COLOUR_
#endif

#define FATAL_ATTR  (FOREGROUND_RED|FOREGROUND_INTENSITY|(0xF0 & scbi.wAttributes))
#define ERROR_ATTR  (FOREGROUND_RED|(0xF0 & scbi.wAttributes))
#define WARN_ATTR   (FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_INTENSITY|(0xF0 & scbi.wAttributes))
#define INFO_ATTR   (FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN|(0xF0 & scbi.wAttributes))
#define DEBUG_ATTR  (FOREGROUND_RED|FOREGROUND_GREEN|(0xF0 & scbi.wAttributes))

#define _FUSION_SKIP_         do {} while (0)
#define _FUSION_FATAL(M, ...) do { _FUSION_SET_COLOUR_(FATAL_ATTR) _FUSION_TS_; fprintf(stderr, "*** Fatal(" __FILE__ ":%d %s):\t" M "\n", __LINE__, __FUNCTION__, ##__VA_ARGS__); _FUSION_RESTORE_COLOUR_ exit(1); } while (0)
#define _FUSION_ERROR(M, ...) do { _FUSION_SET_COLOUR_(ERROR_ATTR) _FUSION_TS_; fprintf(stderr, "*** Error(" __FILE__ ":%d %s):\t" M "\n", __LINE__, __FUNCTION__, ##__VA_ARGS__); _FUSION_RESTORE_COLOUR_ } while (0)
#define _FUSION_WARN(M, ...)  do { _FUSION_SET_COLOUR_(WARN_ATTR)  _FUSION_TS_; fprintf(stderr, "*** Warn(" __FILE__ ":%d %s):\t"  M "\n", __LINE__, __FUNCTION__, ##__VA_ARGS__); _FUSION_RESTORE_COLOUR_ } while (0)
#define _FUSION_INFO(M, ...)  do { _FUSION_SET_COLOUR_(INFO_ATTR)  _FUSION_TS_; fprintf(stderr, "*** Info(" __FILE__ ":%d %s):\t"  M "\n", __LINE__, __FUNCTION__, ##__VA_ARGS__); _FUSION_RESTORE_COLOUR_ } while (0)
#define _FUSION_DEBUG(M, ...) do { _FUSION_SET_COLOUR_(DEBUG_ATTR) _FUSION_TS_; fprintf(stderr, "*** Debug(" __FILE__ ":%d %s):\t" M "\n", __LINE__, __FUNCTION__, ##__VA_ARGS__); _FUSION_RESTORE_COLOUR_ } while (0)

#ifdef _DEBUG
# define FUSION_ASSERT(X, ...) FUSION_ENSURE(X, __asm { int 3 }, ##__VA_ARGS__)
# define FUSION_VERIFY(X, ...) FUSION_ENSURE(X, __asm { int 3 }, ##__VA_ARGS__)
# define FUSION_FATAL(M, ...) _FUSION_FATAL(M, ##__VA_ARGS__)
# define FUSION_ERROR(M, ...) _FUSION_ERROR(M, ##__VA_ARGS__)
# define FUSION_WARN(M, ...)  _FUSION_WARN(M,  ##__VA_ARGS__)
# define FUSION_INFO(M, ...)  _FUSION_INFO(M,  ##__VA_ARGS__)
# define FUSION_DEBUG(M, ...) _FUSION_DEBUG(M, ##__VA_ARGS__)
#else
# define FUSION_ASSERT(X, ...) _FUSION_SKIP_
# define FUSION_VERIFY(X, ...) FUSION_ENSURE(X, exit(1), ##__VA_ARGS__)
# define FUSION_FATAL(M, ...) _FUSION_FATAL(M, ##__VA_ARGS__)
# define FUSION_ERROR(M, ...) _FUSION_ERROR(M, ##__VA_ARGS__)
# define FUSION_WARN(M, ...)  _FUSION_WARN(M,  ##__VA_ARGS__)
# define FUSION_INFO(M, ...)  _FUSION_INFO(M,  ##__VA_ARGS__)
# define FUSION_DEBUG(M, ...) _FUSION_SKIP_
#endif

#define FUSION_ENSURE(X, H, ...) \
  do {                                                                                              \
    if (!(X)) {                                                                                     \
      _FUSION_SET_COLOUR_(ERROR_ATTR)                                                               \
      _FUSION_TS_;                                                                                  \
                                                                                                    \
      fprintf(stderr, "*** Assert failed(" __FILE__ ":%d %s): !(" #X ")", __LINE__, __FUNCTION__);  \
      fprintf(stderr, "" ##__VA_ARGS__);                                                            \
      fprintf(stderr, "\n");                                                                        \
                                                                                                    \
      _FUSION_RESTORE_COLOUR_                                                                       \
                                                                                                    \
      H;                                                                                            \
    }                                                                                               \
  } while (0)

#define	FUSION_ARRAY_SIZE(A)	(sizeof(A)/sizeof(A[0]))
#define	FUSION_IMPLIES(A, B)	(!(A)||(B))

#endif		//FUSION_NF_MACROS__H
