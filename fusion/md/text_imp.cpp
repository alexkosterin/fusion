/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include "include/configure.h"
#include "include/nf_macros.h"
#include "include/md.h"
#include "md_internal.h"
#include "md/text_imp.h"
#include "include/tsc.h"

#include <malloc.h>

#define GARBAGE_SHOW_MAX 8

namespace nf {
  imp_t::imp_t(::HANDLE hfile) : hfile_(hfile) {
    FUSION_ASSERT(hfile_ != INVALID_HANDLE_VALUE);
  }

  imp_t::~imp_t() {
    if (hfile_ != INVALID_HANDLE_VALUE)
      ::CloseHandle(hfile_);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t imp_t::read(mtype_t& type, mid_t& mid, size_t& size, msecs_t& atime, msecs_t& ctime, msecs_t& mtime, bool& has_value, size_t* plen, void** pdata) {
    ::BY_HANDLE_FILE_INFORMATION fi;
    BOOL rc = ::GetFileInformationByHandle(hfile_, &fi);

    FUSION_ASSERT(rc);

    if (!rc)
      return ERR_IO;

    FUSION_ASSERT(!fi.nFileSizeHigh && fi.nFileSizeLow < 0xFFFF);

    if (fi.nFileSizeHigh || fi.nFileSizeLow > 0xFFFF)
      return ERR_MESSAGE_FORMAT;

    cookie_ = (int64_t)fi.nFileIndexLow + (int64_t)((int64_t)fi.nFileIndexHigh << 32);

    atime  = filetime_to_msecs(fi.ftLastAccessTime);
    ctime  = filetime_to_msecs(fi.ftLastWriteTime);
    mtime  = filetime_to_msecs(fi.ftCreationTime);

    char*   text  = (char*)malloc(fi.nFileSizeLow + 1);
    size_t  total = fi.nFileSizeLow;

    FUSION_ASSERT(text);

    ::DWORD bytes = fi.nFileSizeLow;

    ::memset(text, 0, bytes+1);
    rc = ::ReadFile(hfile_, text, bytes, &bytes, 0);

    if (!rc)
      return ERR_IO;

    //FUSION_DEBUG("text[%d]=%.*s", bytes, bytes, text);

    size_t  done = 0, nr;
    int     m;
    int     sz, typ, ofs;

    nr = _snscanf(text + done, total - done, "mid = %d %n",  &m, &ofs);

    if (nr != 1) {
      FUSION_DEBUG("ERR_MESSAGE_FORMAT: mid: %.*s%s", (((total - done) > GARBAGE_SHOW_MAX) ? GARBAGE_SHOW_MAX : (total - done)), text + done, (((total - done) > GARBAGE_SHOW_MAX) ? "..." : ""));

      free(text);

      return ERR_MESSAGE_FORMAT;
    }

    done += ofs;

    nr = _snscanf(text + done, total - done, "size = %d %n", &sz, &ofs);

    if (nr != 1) {
      FUSION_DEBUG("ERR_MESSAGE_FORMAT: size: %.*s", total - done, text + done);

      free(text);

      return ERR_MESSAGE_FORMAT;
    }

    done += ofs;

    nr = _snscanf(text + done, total - done, "type = %d %n", &typ, &ofs);

    if (nr != 1) {
      FUSION_DEBUG("ERR_MESSAGE_FORMAT: type: %.*s", total - done, text + done);

      free(text);

      return ERR_MESSAGE_FORMAT;
    }

    FUSION_DEBUG("mid=%d type=%d size=%d", m, typ, sz);

    done += ofs;

    if (typ & MT_PERSISTENT) {
      FUSION_ASSERT(fi.nFileSizeLow >= done);

      nr = _snscanf(text + done, total - done, "data =%n", &ofs);

      if (nr == -1)
        has_value = false;
      else {
        has_value = true;
        done += ofs;

        if (pdata) {
          free(*pdata);

          if (*plen = (fi.nFileSizeLow - done)) {
            *pdata = ::malloc(*plen);

            FUSION_ASSERT(*pdata);

            if (!*pdata) {
              free(text);

              return ERR_MEMORY;
            }

            ::memcpy(*pdata, text + done, *plen);
          }
          else
            *pdata = 0;
        }

        if (sz != -1 && sz != fi.nFileSizeLow - done) {
          free(text);

          return ERR_MESSAGE_SIZE;
        }
      }
    }

    size  = sz;
    type  = typ;
    mid   = (mid_t)m;

    free(text);

    return ERR_OK;
  }

  result_t imp_t::init(md_t* md, msecs_t& atime, msecs_t& ctime, msecs_t& mtime) {
    FUSION_ASSERT(md);
    FUSION_ASSERT(hfile_ != INVALID_HANDLE_VALUE);

    ::BY_HANDLE_FILE_INFORMATION fi;
    BOOL rc = ::GetFileInformationByHandle(hfile_, &fi);

    FUSION_ASSERT(rc);

    if (!rc)
      return ERR_IO;

    FUSION_ASSERT(!fi.nFileSizeHigh && fi.nFileSizeLow < 0xFFFF);

    if (fi.nFileSizeHigh || fi.nFileSizeLow > 0xFFFF)
      return ERR_MESSAGE_FORMAT;

    cookie_ = (int64_t)fi.nFileIndexLow + (int64_t)((int64_t)fi.nFileIndexHigh << 32);

    atime  = filetime_to_msecs(fi.ftLastAccessTime);
    ctime  = filetime_to_msecs(fi.ftLastWriteTime);
    mtime  = filetime_to_msecs(fi.ftCreationTime);

    return ERR_OK;
  }

  result_t imp_t::write(md_t* md) {
    FUSION_ASSERT(md);
    FUSION_ASSERT(hfile_ != INVALID_HANDLE_VALUE);
//  FUSION_ASSERT(md->size_ == -1 || md->len_ == md->size_);

    char text[32] = {0};
    DWORD bytes;
    BOOL rc;

    //------------------------------------
    _snprintf(text, sizeof(text) - 1, "mid=%d\n", md->mid_);
    ::SetFilePointer(hfile_, 0, 0, FILE_BEGIN);
    rc = ::WriteFile(hfile_, text, ::strlen(text), &bytes, 0);

    if (!rc || bytes != ::strlen(text))
      return ERR_IO;

    //------------------------------------
    _snprintf(text, sizeof text, "size=%d\n", md->size_);
    rc = ::WriteFile(hfile_, text, ::strlen(text), &bytes, 0);

    if (!rc || bytes != ::strlen(text))
      return ERR_IO;

    //------------------------------------
    _snprintf(text, sizeof text, "type=%d\n", md->mtype_);
    rc = ::WriteFile(hfile_, text, ::strlen(text), &bytes, 0);

    if (!rc || bytes != ::strlen(text))
      return ERR_IO;

    //------------------------------------
    if (md->mtype_ & MT_PERSISTENT && md->last_sender_ != CID_NONE) {
      _snprintf(text, sizeof text, "data=");
      rc = ::WriteFile(hfile_, text, ::strlen(text), &bytes, 0);

      if (!rc || bytes != ::strlen(text))
        return ERR_IO;

      rc = ::WriteFile(hfile_, md->last_data_, md->last_len_, &bytes, 0);

      if (!rc || bytes != md->last_len_)
        return ERR_IO;

      FUSION_ASSERT(bytes == md->last_len_);
    }

    rc = ::SetEndOfFile(hfile_);

    FUSION_ASSERT(rc);

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t imp_t::remove(md_t* md) {
    FILE_DISPOSITION_INFO fdi;

    fdi.DeleteFile = TRUE; // marking for deletion

    BOOL rc = ::SetFileInformationByHandle(hfile_, FileDispositionInfo, &fdi, sizeof(FILE_DISPOSITION_INFO));

    if (!rc)
      FUSION_WARN("imp_t::remove(): ::SetFileInformationByHandle()=%d", ::GetLastError());

    return rc ? ERR_OK : ERR_IO;
  }
}
