/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdarg.h>
#include <include/nf_macros.h>
#include <include/nf_macros.h>
#include <include/nf_mcb.h>
# include <include/mb.h>
#include "include/nfel_client.h"
#include "include/lock.h"

namespace nfel {
  //----------------------------------------------------------------------------
  static _srwlock_t    lock__;
  static nf::client_t* client__ = 0;
  static nf::mid_t     mlog__   = 0;

  //----------------------------------------------------------------------------
  nf::result_t init_client(nf::client_t& client, const char* tag_name) {
    {
      wlock_t lock(lock__);

      if (!client__)
        client__ = &client;
      else
        return nf::ERR_INITIALIZED;

      FUSION_ASSERT(mlog__ == 0);

      nf::result_t  e;
      nf::mtype_t   mode;
      size_t        msize = -1;

      FUSION_ENSURE(
        (nf::ERR_OK == (e = client__->mopen(tag_name, nf::O_WRONLY, mode, mlog__, msize)) || mlog__ != 0),
        { client__ = 0; return e; }
      );
    }

    return nf::ERR_OPEN;
  }

  //----------------------------------------------------------------------------
  nf::result_t fini_client() {
    {
      wlock_t lock(lock__);

      if (!client__)
        return nf::ERR_INITIALIZED;

      client__->mclose(mlog__);
      mlog__    = 0;
      client__  = 0;
    }

    return nf::ERR_OK;
  }

# define SV_FORMAT             "Log sev=%s name=\"%s\" msg=\"%s\""

  //----------------------------------------------------------------------------
  nf::result_t log(severity_t severity, const char* desc, ...) {
    {
      rlock_t lock(lock__);
      va_list ap;
      int     len;

      if (!client__)
        return nf::ERR_INITIALIZED;

      FUSION_ENSURE(desc, desc = "");

      try {
        va_start(ap, desc);
        len = ::_vsnprintf(0, 0, desc, ap) + 1;
        va_end(ap);
      }
      catch (...) {
        FUSION_ERROR("sprintf excepted, probably format mismatches arguments");

        va_end(ap);

        return nf::ERR_PARAMETER;
      }

      if (len > EVENTLOG_MAX_STRING)
        len = EVENTLOG_MAX_STRING;

      char* buff = (char*)::alloca(len);

      FUSION_ASSERT(buff);

      try {
        va_start(ap, desc);
        FUSION_VERIFY(::_vsnprintf(buff, len, desc, ap) <= len);
        va_end(ap);
      }
      catch (...) {
        FUSION_ERROR("sprintf excepted, probably format mismatches arguments");

        va_end(ap);

        return nf::ERR_PARAMETER;
      }

      try {
        len = ::_snprintf(0, 0, SV_FORMAT, severity_to_str(severity), client__->name(), buff) + 1;
      }
      catch (...) {
        FUSION_ERROR("sprintf excepted, probably format mismatches arguments");

        return nf::ERR_PARAMETER;
      }

      if (len > EVENTLOG_MAX_STRING)
        len = EVENTLOG_MAX_STRING;

      char* buff2 = (char*)::alloca(len);

      FUSION_ASSERT(buff2);

      try {
        FUSION_VERIFY(::_snprintf(buff2, len, SV_FORMAT, severity_to_str(severity), client__->name(), buff) <= len);
      }
      catch (...) {
        FUSION_ERROR("sprintf excepted, probably format mismatches arguments");

        return nf::ERR_PARAMETER;
      }

      nf::result_t e;

      FUSION_ENSURE((e = client__->publish(mlog__, len, buff2)) == nf::ERR_OK, return e);
    }

    return nf::ERR_OK;
  }
}

