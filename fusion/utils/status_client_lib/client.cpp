/*
 *  FUSION
 *  Copyright (c) 2014 Alex Kosterin
 */

#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <malloc.h>
#include <stdarg.h>
#include <include/nf_macros.h>
#include <include/nf_macros.h>
#include <include/nf_mcb.h>
# include <include/mb.h>
#include "include/nfs_client.h"
#include "include/lock.h"

#define SHUTDOWN_GRACE_PERIOD_MSEC     (1000)

namespace nfs {
  //----------------------------------------------------------------------------
  static _srwlock_t    lock__;
  static nf::client_t* client__          = 0;
  static HANDLE        enotify__         = INVALID_HANDLE_VALUE; //
  static HANDLE        eset__            = INVALID_HANDLE_VALUE; // set status
  static HANDLE        estop__           = INVALID_HANDLE_VALUE; //
  static HANDLE        thread__          = INVALID_HANDLE_VALUE; // thread handle
  static nf::cmi_t     cmi__             = 0;
  static nf::mid_t     mstatus__         = 0;
  static nf::mid_t     mstatus_notify__  = 0;
  static status_repr_t*status_repr__     = 0;
  static int32_t       retrans_period__  = DEFAULT_RETRANSMIT_PERIOD_MSEC;
  static nf::mid_t     notify_mid__;

  //----------------------------------------------------------------------------
  static size_t set_retransmit_period__(int32_t period) {
    if (period == INFINITE)
      return INFINITE;
    else if (period == 0)
      return DEFAULT_RETRANSMIT_PERIOD_MSEC;
    else if (period < 0)
      period = period > -MIN_RETRANSMIT_PERIOD_MSEC ? -MIN_RETRANSMIT_PERIOD_MSEC : period;
    else
      period = period <  MIN_RETRANSMIT_PERIOD_MSEC ?  MIN_RETRANSMIT_PERIOD_MSEC : period;

    return period;
  }

  //----------------------------------------------------------------------------
  static DWORD get_retranmsmit_period__() {
    return retrans_period__ > 0 ? INFINITE : -retrans_period__;
  }

  //----------------------------------------------------------------------------
  static unsigned __stdcall run__(void*) {
    HANDLE wha[] = { enotify__, eset__, estop__ };
    bool   stop = false;

    while (!stop) {
       nf::result_t e;

      switch (DWORD rc = ::WaitForMultipleObjects(FUSION_ARRAY_SIZE(wha), wha, FALSE, get_retranmsmit_period__())) {
      // retransmit
      case WAIT_TIMEOUT:
        e = nf::ERR_OK;

        {
          rlock_t lock(lock__);

          if (client__ && mstatus__ && status_repr__)
            e = client__->publish(mstatus__, sizeof(status_repr_t) + ::strlen(status_repr__->desc_), status_repr__);
        }

        if (e != nf::ERR_OK)
          FUSION_WARN("publish=%s", nf::result_to_str(e));

        break;

      // notify: got status request
      case WAIT_OBJECT_0:
        e = nf::ERR_OK;

        {
          rlock_t lock(lock__);

          if (client__ && notify_mid__ && status_repr__)
            e = client__->post(mstatus__, notify_mid__, sizeof(status_repr_t) + ::strlen(status_repr__->desc_), status_repr__);

          notify_mid__ = 0;
        }

        if (e != nf::ERR_OK)
          FUSION_WARN("post=%s", nf::result_to_str(e));

        break;

      // event set
      case WAIT_OBJECT_0 + 1:
        /*just reset timer...*/
        break;

      // exiting
      case WAIT_OBJECT_0 + 2:
        e = nf::ERR_OK;

        {
          rlock_t lock(lock__);

          if (client__) {
            status_repr_t t = { STATUS_TERM, INFINITE, 0 };

            e = client__->publish(mstatus__, sizeof t, &t);
          }
        }

        if (e != nf::ERR_OK)
          FUSION_WARN("publish=%s", nf::result_to_str(e));

        stop = true;

        break;

      case WAIT_FAILED:
      case WAIT_ABANDONED_0:
      case WAIT_ABANDONED_0 + 1:
      case WAIT_ABANDONED_0 + 2:
        FUSION_ERROR("rc=%d", rc);

        stop = true;

        break;

      default:
        FUSION_WARN("rc=%d", rc);
      }
    }

    return 0;
  }

  //----------------------------------------------------------------------------
  static nf::result_t __stdcall callback_method__(nf::callback_t, void* cookie, nf::mid_t mid, size_t len, const void *data) {
    const nf::mcb_t* mcb = client__->get_mcb();

    FUSION_ASSERT(client__);
    FUSION_ASSERT(mcb);
    FUSION_ASSERT(mstatus__ != 0);
    FUSION_ASSERT(mstatus_notify__ != 0);
    FUSION_ASSERT(mcb->mid_ == mstatus_notify__);

    {
      wlock_t lock(lock__);

      if (notify_mid__)
        FUSION_WARN("will drop notifiaction for cid=%d", notify_mid__);

      notify_mid__ = mcb->src_;

      ::SetEvent(enotify__);
    }

    return nf::ERR_OK;
  }

  ////////////////////////////////////////////////////////////////////////////////
  nf::result_t init_client(nf::client_t& client, int32_t period, const char* status_name, const char* status_notify_name) {
    {
      wlock_t lock(lock__);

      if (!client__)
        client__ = &client;
      else
        return nf::ERR_INITIALIZED;
    }

    nf::result_t e;
    nf::mtype_t mode;
    size_t      msize;

    FUSION_ASSERT(enotify__ == INVALID_HANDLE_VALUE);
    FUSION_ASSERT(eset__    == INVALID_HANDLE_VALUE);
    FUSION_ASSERT(estop__   == INVALID_HANDLE_VALUE);
    FUSION_ASSERT(cmi__     == 0);
    FUSION_ASSERT(mstatus__ == 0);
    FUSION_ASSERT(mstatus__ == 0);
    FUSION_ASSERT(mstatus_notify__ == 0);
    FUSION_ASSERT(thread__  == INVALID_HANDLE_VALUE);
    FUSION_ASSERT(!status_repr__);

    retrans_period__ = set_retransmit_period__(period);

    FUSION_ENSURE((enotify__  = ::CreateEvent(0, FALSE, FALSE, 0)), { e = nf::ERR_WIN32; goto fail_0; });
    FUSION_ENSURE((eset__     = ::CreateEvent(0, FALSE, FALSE, 0)), { e = nf::ERR_WIN32; goto fail_1; });
    FUSION_ENSURE((estop__    = ::CreateEvent(0, FALSE, FALSE, 0)), { e = nf::ERR_WIN32; goto fail_2; });

    FUSION_ENSURE((nf::ERR_OK == (e = client__->reg_callback_method(callback_method__, 0, cmi__))), goto fail_3);
    msize = -1;
    FUSION_ENSURE((nf::ERR_OK == (e = client__->mopen(status_name, nf::O_WRONLY, mode, mstatus__, msize)) || mstatus__ != 0), goto fail_4);
    msize = 0;
    FUSION_ENSURE((nf::ERR_OK == (e = client__->mopen(status_notify_name, nf::O_RDONLY, mode, mstatus_notify__, msize)) || mstatus_notify__ != 0), goto fail_5);
    FUSION_ENSURE((nf::ERR_OK == (e = client__->subscribe(mstatus_notify__, 0, cmi__, nf::callback_t(1)))), goto fail_6);

    thread__ = (HANDLE)::_beginthreadex(
      NULL,   // security,
      0,			// stack_size,
      run__,	// start_address
      0,			// arg
      0,	    // initflag, 0 -> run immediately
      0				// ptr to thread id
    );

    FUSION_ENSURE(thread__ != 0 && thread__ != INVALID_HANDLE_VALUE, { e = nf::ERR_WIN32; goto fail_7; });

    return nf::ERR_OK;

  fail_7:
    client__->unsubscribe(mstatus__);

  fail_6:
    client__->mclose(mstatus_notify__);
    mstatus_notify__ = 0;

  fail_5:
    client__->mclose(mstatus__);
    mstatus__ = 0;

  fail_4:
    client__->unreg_callback_method(cmi__);
    cmi__ = 0;

  fail_3:
    ::CloseHandle(estop__);
    estop__ = INVALID_HANDLE_VALUE;

  fail_2:
    ::CloseHandle(eset__);
    eset__ = INVALID_HANDLE_VALUE;

  fail_1:
    ::CloseHandle(enotify__);
    enotify__ = INVALID_HANDLE_VALUE;

  fail_0:

    client__ = 0;

    return e;
  }

  ////////////////////////////////////////////////////////////////////////////////
  nf::result_t fini_client() {
    {
      wlock_t lock(lock__);

      if (!client__)
        return nf::ERR_INITIALIZED;
    }

    ::SetEvent(estop__);

    DWORD rc;

    if ((rc = ::WaitForSingleObject(thread__, SHUTDOWN_GRACE_PERIOD_MSEC)) != WAIT_OBJECT_0) {
      FUSION_WARN("WaitForSingleObject: rc=%d err=%d", rc, ::GetLastError());

      ::_endthread();
    }

    client__->unsubscribe(mstatus__);
    client__->mclose(mstatus_notify__);
    client__->mclose(mstatus__);
    client__->unreg_callback_method(cmi__);

    ::CloseHandle(thread__);
    ::CloseHandle(estop__);
    ::CloseHandle(eset__);
    ::CloseHandle(enotify__);

    ::free(status_repr__);

    thread__          = INVALID_HANDLE_VALUE;
    mstatus_notify__  = 0;
    mstatus__         = 0;
    cmi__             = 0;
    estop__           = INVALID_HANDLE_VALUE;
    eset__            = INVALID_HANDLE_VALUE;
    enotify__         = INVALID_HANDLE_VALUE;
    status_repr__     = 0;
    client__          = 0;

    return nf::ERR_OK;
  }

  ////////////////////////////////////////////////////////////////////////////////
  nf::result_t set_status(status_code_t code, const char* desc, ...) {
    FUSION_ASSERT(code > STATUS_UNKNOWN);

    {
      wlock_t lock(lock__);

      if (!client__)
        return nf::ERR_INITIALIZED;

      FUSION_ENSURE(desc, desc = "");

      if (retrans_period__ < 0 && status_repr__ && status_repr__->status_ == code && ::strncmp(status_repr__->desc_, desc, STATUS_MAX_STRING) == 0)
        return nf::ERR_OK;

      if (!status_repr__)
        status_repr__ = (status_repr_t*)::malloc(sizeof(status_repr_t) + STATUS_MAX_STRING + 1);

      FUSION_ASSERT(status_repr__);

      va_list ap;

      va_start(ap, desc);
      int len = ::_vsnprintf(0, 0, desc, ap) + 1;
      va_end(ap);

      if (len > STATUS_MAX_STRING)
        len = STATUS_MAX_STRING + 1;

      char* buff = (char*)::alloca(len);

      FUSION_ASSERT(buff);

      va_start(ap, desc);
      FUSION_VERIFY(::_vsnprintf(buff, len, desc, ap) <= len);
      va_end(ap);

      status_repr__->status_  = code;
      status_repr__->retrans_ = retrans_period__ < 0 ? -retrans_period__ : retrans_period__;
      status_repr__->desc_[0] = 0;
      ::strncpy(status_repr__->desc_, buff, len);

      nf::result_t e;

      FUSION_ENSURE((e = client__->publish(mstatus__, sizeof(status_repr_t) + len, status_repr__)) == nf::ERR_OK, return e);
    }

    ::SetEvent(eset__);

    return nf::ERR_OK;
  }

  ////////////////////////////////////////////////////////////////////////////////
  nf::result_t set_status_retransmit_period(size_t period) {
    {
      wlock_t lock(lock__);

      if (!client__)
        return nf::ERR_INITIALIZED;

      retrans_period__ = set_retransmit_period__(period);
    }

    return nf::ERR_OK;
  }

  size_t get_status_retranslate_period() {
    wlock_t lock(lock__);

    return retrans_period__;
  }
}
