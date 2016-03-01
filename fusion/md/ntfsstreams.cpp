/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include "include/configure.h"
#include "include/md.h"
#include "md_internal.h"
#include "ntfsstreams.h"

namespace nf {
  void imp_t::init() {
    hdata_  = INVALID_HANDLE_VALUE;
    hmid_   = INVALID_HANDLE_VALUE;
    htype_  = INVALID_HANDLE_VALUE;
    hsize_  = INVALID_HANDLE_VALUE;  
  }

  void imp_t::fini() {
    ::CloseHandle(hdata_);
    ::CloseHandle(hmid_);
    ::CloseHandle(htype_);
    ::CloseHandle(hsize_);
  }
      
  //////////////////////////////////////////////////////////////////////////////
  result_t imp_t::read(const char* fname, bool existing, ::HANDLE hdata, unsigned* ptype, mid_t* pm, size_t* psize, size_t* plen, void** pdata) {
    ASSERT(hdata != INVALID_HANDLE_VALUE);

    ::BY_HANDLE_FILE_INFORMATION fi;
    BOOL rc = ::GetFileInformationByHandle(hdata, &fi);
    ::HANDLE hmid, htype, hsize;

    ASSERT(rc);

    if (!rc)
      return ERR_UNKNOWN;
    
    ASSERT(!fi.nFileSizeHigh && fi.nFileSizeLow < 0xFFFF);

    if (fi.nFileSizeHigh || fi.nFileSizeLow > 0xFFFF)
      return ERR_INVALID_FORMAT;
    
    if (existing) {
      char path[MAX_PATH+1] = {0};
      char data[1024] = {0};
      DWORD have, done, val;

      //------------------------------------
      ::_snprintf(path, sizeof path, "%s:mid", fname);

      hmid = ::CreateFile(path, FILE_ALL_ACCESS, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

      if (hmid == INVALID_HANDLE_VALUE)
        return ERR_INVALID_FORMAT;
  
      have = sizeof data;
      rc = ::ReadFile(hmid, data, have, &done, 0);

      if (!rc)
        return ERR_IO;

      ASSERT(done);

      int mid;

      _snscanf(data, done, "%d", &mid);

      if (pm)
        *pm = (mid_t)mid;

      //------------------------------------
      ::_snprintf(path, sizeof path, "%s:type", fname);

      htype = ::CreateFile(path, FILE_ALL_ACCESS, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

      if (htype == INVALID_HANDLE_VALUE)
        return ERR_INVALID_FORMAT;

      have = sizeof data;
      rc = ::ReadFile(htype, data, have, &done, 0);

      if (!rc)
        return ERR_IO;

      ASSERT(done);

      _snscanf(data, done, "%d", &val);

      if (ptype && val != *ptype)
        return ERR_MESSAGE_TYPE;

      //------------------------------------
      ::_snprintf(path, sizeof path, "%s:size", fname);

      hsize = ::CreateFile(path, FILE_ALL_ACCESS, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

      if (hsize == INVALID_HANDLE_VALUE)
        return ERR_INVALID_FORMAT;

      have = sizeof data;
      rc = ::ReadFile(hsize, data, have, &done, 0);

      if (!rc)
        return ERR_IO;

      ASSERT(done);

      _snscanf(data, done, "%d", &val);

      if (psize && val != *psize)
        return ERR_MESSAGE_SIZE;

      //------------------------------------
      if (ptype && (*ptype & MT_TYPE_MASK) == MT_KEEP_LAST) {
        ASSERT(plen);
        ASSERT(pdata);

        have = fi.nFileSizeLow;
        *pdata = malloc(have);
        rc = ::ReadFile(hdata, *pdata, have, &done, 0);

        if (!rc || done != have) {
          free(*pdata);

          return ERR_IO;
        }

        if (psize) {
          ASSERT(*psize == -1 || *psize == fi.nFileSizeLow);

          if (*psize != -1 && *psize != fi.nFileSizeLow) {
            free(*pdata);

            return ERR_INVALID_FORMAT;
          }
        }

        *plen = done;
      }
    }
    else {
      char path[MAX_PATH+1] = {0};
      char data[1024] = {0};

      //------------------------------------
      ::_snprintf(path, sizeof path, "%s:mid", fname);

      hmid = ::CreateFile(path, FILE_ALL_ACCESS, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

      if (hmid == INVALID_HANDLE_VALUE) {
        DEBUG("CreateFile(%s)=%d", path, ::GetLastError());

        return ERR_INVALID_FORMAT;
      }

      //------------------------------------
      ::_snprintf(path, sizeof path, "%s:type", fname);

      htype = ::CreateFile(path, FILE_ALL_ACCESS, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

      if (hmid == INVALID_HANDLE_VALUE) {
        DEBUG("CreateFile(%s)=%d", path, ::GetLastError());

        ::CloseHandle(hmid);

        return ERR_INVALID_FORMAT;
      }

      //------------------------------------
      ::_snprintf(path, sizeof path, "%s:size", fname);

      hsize = ::CreateFile(path, FILE_ALL_ACCESS, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

      if (hmid == INVALID_HANDLE_VALUE) {
        DEBUG("CreateFile(%s)=%d", path, ::GetLastError());

        ::CloseHandle(hmid);
        ::CloseHandle(htype);

        return ERR_INVALID_FORMAT;
      }
    }

    hdata_ = hdata;
    hmid_  = hmid;
    htype_ = htype;
    hsize_ = hsize;

    return ERR_OK;
  }

  result_t imp_t::write(md_t* md) {
    ASSERT(md);
    ASSERT(hdata_ != INVALID_HANDLE_VALUE);
    ASSERT(hmid_  != INVALID_HANDLE_VALUE);
    ASSERT(htype_ != INVALID_HANDLE_VALUE);
    ASSERT(hsize_ != INVALID_HANDLE_VALUE);

    char data[32]; // @@
    DWORD done;
    BOOL rc;

    //------------------------------------
    _snprintf(data, sizeof data, "%d", md->mid_);
    ::SetFilePointer(hmid_, 0, 0, FILE_BEGIN);
    rc = ::WriteFile(hmid_, data, ::strlen(data), &done, 0);

    if (!rc || done != ::strlen(data))
      return ERR_IO;

    ASSERT(done);

    //------------------------------------
    _snprintf(data, sizeof data, "%d", md->type_);
    ::SetFilePointer(htype_, 0, 0, FILE_BEGIN);
    rc = ::WriteFile(htype_, data, ::strlen(data), &done, 0);

    if (!rc || done != ::strlen(data))
      return ERR_IO;

    ASSERT(done);

    //------------------------------------
    _snprintf(data, sizeof data, "%d", md->size_);
    ::SetFilePointer(hsize_, 0, 0, FILE_BEGIN);
    rc = ::WriteFile(hsize_, data, ::strlen(data), &done, 0);

    if (!rc || done != ::strlen(data))
      return ERR_IO;

    ASSERT(done);

    //------------------------------------
    if ((md->type_ & MT_TYPE_MASK) == MT_KEEP_LAST) {
      ::SetFilePointer(hdata_, 0, 0, FILE_BEGIN);
      rc = ::WriteFile(hdata_, md->data_, md->len_, &done, 0);

      if (!rc || done != md->len_)
        return ERR_IO;

      ASSERT(done);

      if (md->size_ == -1) {
        rc = ::SetEndOfFile(hdata_);

        if (!rc)
          return ERR_IO;
      }
    }
    
    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t imp_t::remove(md_t* md) {
    FILE_DISPOSITION_INFO fdi;

    fdi.DeleteFile = TRUE; // marking for deletion

    BOOL rc = ::SetFileInformationByHandle(hdata_, FileDispositionInfo, &fdi, sizeof(FILE_DISPOSITION_INFO))
      && ::SetFileInformationByHandle(hmid_, FileDispositionInfo, &fdi, sizeof(FILE_DISPOSITION_INFO))
      && ::SetFileInformationByHandle(htype_, FileDispositionInfo, &fdi, sizeof(FILE_DISPOSITION_INFO))
      && ::SetFileInformationByHandle(hsize_, FileDispositionInfo, &fdi, sizeof(FILE_DISPOSITION_INFO));

    return rc ? ERR_OK : ERR_IO;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t imp_t::close(md_t* md) {
    result_t e = ::CloseHandle(hdata_) && ::CloseHandle(hmid_) && ::CloseHandle(htype_) && ::CloseHandle(hsize_) ? ERR_OK : ERR_UNEXPECTED;

    hdata_  = INVALID_HANDLE_VALUE;
    hmid_   = INVALID_HANDLE_VALUE;
    htype_  = INVALID_HANDLE_VALUE;
    hsize_  = INVALID_HANDLE_VALUE;

    return e;
  }
}
