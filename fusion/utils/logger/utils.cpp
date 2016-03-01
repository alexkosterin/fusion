/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <malloc.h>
#include <stdarg.h>

#include "include/nf.h"	    // FUSION
#include "include/mb.h"	    // FUSION MESSAGE BUS
#include "include/nf_macros.h"
#include "include/nf_mcb.h"	// FUSION MCB
#include "include/tsc.h"

#define COMMAND "CMD /D /C START \"On Volume Finish\" %s "
//#define COMMAND "CMD /D /K %s "
//#define COMMAND  "%s "

int _system(const char* command, ...) {
    DWORD len = 0;
    //CHAR  comspec[MAX_PATH + 1];
    //DWORD len = ::GetEnvironmentVariable("COMSPEC", comspec, MAX_PATH);

    //if ((len == 0) || (len > MAX_PATH))
    //    return -1;

    va_list v;

    va_start(v, command);

    while (const char* param = va_arg(v, const char*))
      len += ::strlen(param) + 1;

    va_end(v);

    char* cmdline = (char*)::alloca(len + ::strlen(command) + ::strlen(COMMAND));

    if (!cmdline)
        return -1;

//  ::sprintf(cmdline, COMMAND, comspec, command);
    ::sprintf(cmdline, COMMAND, command);

    va_start(v, command);

    while (const char* param = va_arg(v, const char*)) {
      ::strcat(cmdline, " ");
      ::strcat(cmdline, param);
    }

    va_end(v);

    STARTUPINFO si = { 0 };

    si.cb           = sizeof si;
    si.dwFlags      = STARTF_USESHOWWINDOW;
    si.wShowWindow  = SW_HIDE;

    PROCESS_INFORMATION pi;

    ::memset(&pi, 0, sizeof pi);

    BOOL rc = ::CreateProcess(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);

    if (rc) {
      ::CloseHandle(pi.hThread);
      ::CloseHandle(pi.hProcess);
    }

    return rc ? 0 : -1;
}

////////////////////////////////////////////////////////////////////////////////
char* add_ext(char* name, const char* ext) {
  FUSION_ASSERT(name);
  FUSION_ASSERT(ext);

  char res[MAX_PATH + 1] = {0};

  if (*ext == '.')
    ext++;

  ::_snprintf(res, MAX_PATH, "%s.%s", name, ext);

  return ::strdup(res);
}

////////////////////////////////////////////////////////////////////////////////
char* subst_ext(char* name, const char* ext) {
  FUSION_ASSERT(name);
  FUSION_ASSERT(ext);

  char drv[MAX_PATH + 1] = {0};
  char dir[MAX_PATH + 1] = {0};
  char nam[MAX_PATH + 1] = {0};
  char res[MAX_PATH + 1] = {0};

  ::_splitpath(name, drv, dir, nam, 0);

  if (*ext == '.')
    ext++;

  ::_snprintf(res, MAX_PATH, "%s%s%s.%s", drv, dir, nam, ext);

  return ::strdup(res);
}

////////////////////////////////////////////////////////////////////////////////
char* subst_dir(char* name, const char* dir) {
  FUSION_ASSERT(name);
  FUSION_ASSERT(dir);

  char nam[MAX_PATH + 1] = {0};
  char ext[MAX_PATH + 1] = {0};
  char res[MAX_PATH + 1] = {0};

  ::_splitpath(name, 0, 0, nam, ext);
  ::_snprintf(res, MAX_PATH, "%s/%s%s", dir, nam, ext);

  return ::strdup(res);
}

////////////////////////////////////////////////////////////////////////////////
void sanitize_name(char* name) {
  while (char* p = ::strpbrk(name, ":/\\"))
    *p = '_';
}

////////////////////////////////////////////////////////////////////////////////
char* expand_name(const char* tmplt, int N, const char* G) {
  FUSION_ASSERT(tmplt);

  time_t      t;
  struct tm*  timeinfo;
  char buff [MAX_PATH + 1] = {0};
  char buff2[MAX_PATH + 1] = {0};

  ::strncpy(buff, tmplt, MAX_PATH);

  // screen %N & %G
  for (char* p = ::strstr(buff, "%N"); p; p = ::strstr(p + 2, "%N"))
    p[0] = '@';

  for (char* p = ::strstr(buff, "%G"); p; p = ::strstr(p + 2, "%G"))
    p[0] = '@';

  time(&t);
  timeinfo = localtime(&t);

  ::strftime(buff2, sizeof buff2, buff, timeinfo);

  // expand %NNN (@NNN->%.3d)
  while (char* p = ::strstr(buff2, "@NNN")) {
    char tmp[MAX_PATH + 1] = {0};

    p[0] = '%';
    p[1] = '.';
    p[2] = '3';
    p[3] = 'd';

    ::_snprintf(tmp, MAX_PATH, buff2, N);
    ::strcpy(buff2, tmp);
  }

  // expand %N (@N->%d)
  while (char* p = ::strstr(buff2, "@N")) {
    char tmp[MAX_PATH + 1] = {0};

    p[0] = '%';
    p[1] = 'd';

    ::_snprintf(tmp, MAX_PATH, buff2, N);
    ::strcpy(buff2, tmp);
  }

  while (char* p = ::strstr(buff2, "@G")) {
    char tmp[MAX_PATH + 1] = {0};

    p[0] = '%';
    p[1] = 's';

    ::_snprintf(tmp, MAX_PATH, buff2, G);
    ::strcpy(buff2, tmp);
  }

  FUSION_ASSERT(::strlen(buff2));

  return ::strdup(buff2);
}

////////////////////////////////////////////////////////////////////////////////
bool str2guid(const char* str, UUID& guid) {
  int rc = ::sscanf(str, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x",
    &guid.Data1, &guid.Data2, &guid.Data3,
    &guid.Data4[0], &guid.Data4[1], &guid.Data4[2], &guid.Data4[3],
    &guid.Data4[4], &guid.Data4[5], &guid.Data4[6], &guid.Data4[7]
  );

  return rc == 11;
}

////////////////////////////////////////////////////////////////////////////////
int64_t parse_time(const char* arg) {
  int64_t v;
  int pos = 0;
  int rc = ::sscanf(arg, "%lld%n", &v, &pos);

  FUSION_ASSERT(rc == 1);

  if (arg[pos] == 0)
    return v;
  else if (!::stricmp(arg + pos, "s"))
    return v * 1000;
  else if (!::stricmp(arg + pos, "m"))
    return v * 1000 * 60;
  else if (!::stricmp(arg + pos, "h"))
    return v * 1000 * 60 * 60;
  else if (!::stricmp(arg + pos, "d"))
    return v * 1000 * 60 * 60 * 24;

  FUSION_ERROR("Invalid time format: %s", arg);

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
int64_t parse_size(const char* arg) {
  int64_t v;
  int pos = 0;
  int rc = ::sscanf(arg, "%lld%n", &v, &pos);

  FUSION_ASSERT(rc == 1);

  if (arg[pos] == 0)
    return v;
  else if (!::stricmp(arg + pos, "k"))
    return v * 1000;
  else if (!::strcmp(arg + pos, "KB"))
    return v * 1024;
  else if (!::stricmp(arg + pos, "m"))
    return v * 1000 * 1000;
  else if (!::strcmp(arg + pos, "MB"))
    return v * 1024 * 1024;
  else if (!::stricmp(arg + pos, "g"))
    return v * 1000 * 1000 * 1000;
  else if (!::strcmp(arg + pos, "GB"))
    return v * 1024 * 1024 * 1024;
  else if (!::stricmp(arg + pos, "t"))
    return v * 1000 * 1000 * 1000 * 1000;
  else if (!::strcmp(arg + pos, "TB"))
    return v * 1024 * 1024 * 1024 * 1024;

  FUSION_ERROR("Invalid size format: %s", arg);

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
size_t popcount(size_t len, const char* buff) {
  FUSION_ASSERT(buff);

//  if (len % 4)
//    return 0;
//
//  if (len == 0)
//    return 0;
//
//  __asm {
//    xor     ecx, ecx                        ;; loop counter = 0
//    xor     edx, edx                        ;; t = 0
//    mov     ebx, dword ptr[buff]            ;; ebx = buff
//
//loop_:
//    popcnt  bl, byte ptr[ebx]             ;; eax = bit count
//    add     edx, bx                        ;; t += eax
//    add     ecx, 4                          ;; ++counter
//    cmp     ecx, dword ptr[len]             ;; counter < sizeod bm_ / sizeof bm_[0]; ARRAY_SIZE == (pos_ - bm_) / 4
//
//    jb      loop_
//
//    mov     eax, edx
//  }

  return true;
}
