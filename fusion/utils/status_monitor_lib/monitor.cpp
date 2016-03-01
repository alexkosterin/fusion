/*
 *  FUSION
 *  Copyright (c) 2014 Alex Kosterin
 */

#include "include/nf_macros.h"
#include "include/mb.h"
#include "include/nf_mcb.h"
#include "include/nfs_monitor.h"
#include "include/lock.h"
#include <map>
#include <windows.h>
#include <process.h>
#include "mcb.proto/mcb.pb.h"

#define SHUTDOWN_GRACE_PERIOD_MSEC (1000)                   // grace period for helper thread shutdown
#define MY_INFINITY                (0x7FFFFFFFFFFFFFFFLL)   // -1LL
#define SI_THROTLING_MSEC          (01 * 1000)              // call system-info() no often then this
#define TIMEDOUT_RETRY_PERIOD_MSEC (30 * 1000)              // period to check if a timed-out client has terminatd
#define SI_CALL_TIMEOUT_MSEC       (30000)                  // max block time on system-info() call
#define RETRANSMIT_SLACK_MSEC(P)   ((11*P)/10 + 2000)       // calculate client retransmit deadline, like adding couple of seconds

namespace nfs {
  //----------------------------------------------------------------------------
  struct monitored_client_t {
    nf::msecs_t   expires_;
    bool          armed_;           // needs attention if true: need for si update, or client missed deadlime

    nf::msecs_t   org_;             // origination time-stamp

    status_code_t status_;
    char*         descr_;

    // const data: name, address, port, ...
    char*         name_;            // may be NULL, if not yet resolved

    // non-const data: queue length, stat, ...

    monitored_client_t()
    : expires_(0), armed_(true), descr_(0), name_(0) {}

    monitored_client_t(const monitored_client_t& mc)
    : expires_(mc.expires_), armed_(mc.armed_), org_(mc.org_), status_(mc.status_), descr_(mc.descr_ ? ::strdup(mc.descr_) : 0), name_(mc.name_ ? ::strdup(mc.name_) : 0) {}

    ~monitored_client_t() {
      ::free(name_);
      ::free(descr_);
    }
  };

  //----------------------------------------------------------------------------
  // the first entry is not a real client, it is system-info throttling timer
  //----------------------------------------------------------------------------
  struct monitored_clients_t: std::map<nf::cid_t, monitored_client_t*> {
    ~monitored_clients_t() {
      for (monitored_clients_t::iterator I = begin(), E = end(); I != E; ++I)
        delete I->second;
    }
  };

  //----------------------------------------------------------------------------
  static _srwlock_t    lock__;
  static nf::client_t* client__               = 0;
  static HANDLE        eupdate__              = INVALID_HANDLE_VALUE;
  static HANDLE        estop__                = INVALID_HANDLE_VALUE;
  static HANDLE        thread__               = INVALID_HANDLE_VALUE; // thread handle
  static nf::cmi_t     cmi__                  = 0;
  static nf::mid_t     mstatus__              = 0;
  static nf::mid_t     mstatus_notify__       = 0;
  static status_monitor_callback_t callback__ = 0;
  static monitored_clients_t mcs__;                                   // monitored clients
  static mcb_sysinfo_reply sysinfo_reply__;
  static void*        cookie__                = 0;

  //----------------------------------------------------------------------------
  // must not block!
  //----------------------------------------------------------------------------
  static nf::result_t __stdcall callback_method__(nf::callback_t /*unused*/, void* /*unused*/, nf::mid_t mid, size_t len, const void *data) {
    const nf::mcb_t* mcb = client__->get_mcb();

    FUSION_ASSERT(mcb);
    FUSION_ASSERT(data);
    FUSION_ASSERT(len >= sizeof status_repr_t);

    const status_repr_t* r = (const status_repr_t*)data;

    {
      wlock_t lock(lock__);
      monitored_clients_t::iterator I = mcs__.find(mcb->src_);
      bool exising                    = I != mcs__.end();
      monitored_client_t* t           = exising ? I->second : new monitored_client_t;
      nf::msecs_t now                 = nf::now_msecs();

      t->org_      = mcb->org_;
      t->status_   = r->status_;
      t->expires_  = now + RETRANSMIT_SLACK_MSEC(r->retrans_);
      t->armed_    = true;

      FUSION_DEBUG("retrans=%d expire=%lld now=%lld", r->retrans_, t->expires_, now);

      if (exising)
        ::free(t->descr_);

      t->descr_    = ((len > sizeof(status_repr_t)) && r->desc_[0]) ? ::strdup(r->desc_) : 0;

      if (!exising)
        mcs__.insert(std::make_pair(mcb->src_, t));
    }

    ::SetEvent(eupdate__);

    return nf::ERR_OK;
  }

  //----------------------------------------------------------------------------
  static bool call_sysinfo__() {
    mcb_sysinfo_request q;

    //CLI_NAME = 256,
    //CLI_UUID = 512,
    //CLI_ADDRESS = 1024,
    //CLI_DEFAULT_PROFILE = 2048,
    //CLI_PROFILES = 4096,
    //CLI_GROUPS = 8192,
    //CLI_START_TIME = 16384,
    //CLI_SYNC_PERIOD = 32768,
    //CLI_CONN_LATENCY = 65536,
    //CLI_QUEUE_LIMIT = 131072,
    //CLI_QUEUE_SIZE = 262144,

    q.set_flags(SYS_CLIENTS|CLI_NAME);
    q.add_cids(nf::CID_ALL|nf::CID_NOSELF);

    size_t req_len  = q.ByteSize();
    char*  req_data = (char*)::alloca(req_len);

    q.SerializeToArray(req_data, req_len);

    size_t       rep_len;
    const void*  rep_data;
    nf::mid_t    rep_mid;
    nf::result_t e;

    if ((e = client__->request(nf::MD_SYS_SYSINFO_REQUEST, nf::CID_SYS, req_len, req_data, rep_mid, rep_len, rep_data, SI_CALL_TIMEOUT_MSEC)) != nf::ERR_OK) {
      FUSION_WARN("request MD_SYS_SYSINFO_REQUEST=%s", nf::result_to_str(e));

      return false;
    }

    FUSION_ASSERT(rep_mid == nf::MD_SYS_SYSINFO_REPLY);

    if (!sysinfo_reply__.ParseFromArray(rep_data, rep_len)) {
      FUSION_WARN("Can not parse sysinfo_reply__, wierd");

      return false;
    }

    return true;
  }

  //----------------------------------------------------------------------------
  static bool update_mcs_based_on_sysinfo__() {
    bool updated = false;
    std::map<nf::cid_t, int> cids;

    for (int i = 0; i < sysinfo_reply__.clients_size(); ++i)
      if (sysinfo_reply__.clients(i).has_cid())
        cids.insert(std::make_pair(sysinfo_reply__.clients(i).cid(), i));

    {
      wlock_t lock(lock__);
      nf::msecs_t now = nf::now_msecs();

      for (monitored_clients_t::iterator I = mcs__.begin(), E = mcs__.end(); I != E; ++I) {
        if (I->first == 0) {
          I->second->expires_ = now + SI_THROTLING_MSEC;
          I->second->org_     = now;
          I->second->armed_   = false;

          continue;
        }

        std::map<nf::cid_t, int>::const_iterator J = cids.find(I->first);

        if (J != cids.cend()) {
          if (I->second->name_ == 0) {
            I->second->name_  = ::strdup(sysinfo_reply__.clients(J->second).name().c_str());
            I->second->armed_ = true;
            updated           = true;
          }
          else if (I->second->armed_)
            updated = true;
        }
        else {
          I->second->status_  = STATUS_TERM;
          I->second->expires_ = MY_INFINITY;
          I->second->org_     = now;
          I->second->armed_   = true;
          updated             = true;

          ::free(I->second->descr_);
          I->second->descr_   = 0;
        }
      }
    }

    return updated;
  }

  //----------------------------------------------------------------------------
  static DWORD get_timeout__() {
    monitored_clients_t::iterator MIN = mcs__.end();
    nf::msecs_t now = nf::now_msecs();

    {
      rlock_t lock(lock__);
      monitored_clients_t::iterator I = mcs__.begin(), E = mcs__.end();

      // si status: armed and expired
      if (I->second->armed_ && I->second->expires_ <= now) {
        FUSION_DEBUG("wait: 0, need and can call si()");

        return 0;
      }

      nf::msecs_t t = MY_INFINITY;

      // clients
      for (++I; I != E; ++I)
        if (I->second->expires_ < t) {
          t   = I->second->expires_;
          MIN = I;
        }
    }

    if (MIN != mcs__.end()) {
      nf::msecs_t t = MIN->second->expires_;

      FUSION_DEBUG("wait(%s)=%d", MIN->second->name_, (t < now) ? 0 : (t - now) < INFINITE ? DWORD(t - now) : INFINITE);

      return (t < now) ? 0 : (t - now) < INFINITE ? DWORD(t - now) : INFINITE;
    }

    FUSION_DEBUG("wait forever");

    return INFINITE;
  }

  //----------------------------------------------------------------------------
  static unsigned __stdcall run__(void*) {
    {
      rlock_t lock(lock__);

      if (client__)
        client__->publish(mstatus_notify__, 0, 0);
    }

    HANDLE wha[] = { eupdate__, estop__ };
    bool   stop  = false;

    while (!stop)
      switch(DWORD rc = ::WaitForMultipleObjects(FUSION_ARRAY_SIZE(wha), wha, FALSE, get_timeout__())) {
      // time-out: either SI call gets enabled, or some client(s) missed deadlines
      case WAIT_TIMEOUT:
        {
          bool can_call_sysinfo = false;
          bool need_update_mcs  = false;

          {
            wlock_t lock(lock__);
            nf::msecs_t now = nf::now_msecs();

            for (monitored_clients_t::iterator I = mcs__.begin(), E = mcs__.end(); I != E; ++I)
              if (I->first == 0) {
                can_call_sysinfo = I->second->expires_ <= now;
                need_update_mcs  = I->second->armed_;
              }
              else {
                if (I->second->expires_ <= now) {
                  I->second->armed_     = I->second->status_ != STATUS_UNKNOWN;
                  I->second->status_    = STATUS_UNKNOWN;
                  I->second->expires_   = now + TIMEDOUT_RETRY_PERIOD_MSEC;

                  ::free(I->second->descr_);
                  I->second->descr_     = 0;
                }

                need_update_mcs |= I->second->name_ == 0 || I->second->status_ == STATUS_UNKNOWN;
              }
          }

          if (need_update_mcs) {
            if (can_call_sysinfo)
              call_sysinfo__();

            if (update_mcs_based_on_sysinfo__())
              goto update;
          }
        }

        break;

      // got new status(s), already inserted into mcs__
      case WAIT_OBJECT_0:
        {
update:     monitored_clients_t mcs;

          {
            wlock_t lock(lock__);
            nf::msecs_t now = nf::now_msecs();
            monitored_clients_t::iterator I = mcs__.begin();
            monitored_clients_t::iterator E = mcs__.end();

            ++I;  // si is first, skip it

            while (I != E) {
              FUSION_DEBUG("%s", I->second->name_);

              if (I->second->armed_) {
                I->second->armed_ = false;

                if (I->second->status_ == STATUS_TERM) {
                  if (callback__)
                    mcs.insert(std::make_pair(I->first, I->second));
                  else
                    delete I->second;

                  I->second = 0;
                  I = mcs__.erase(I); // erase() returns iterator to next element

                  continue;
                }
                else if (I->second->name_) {
                  if (callback__)
                    mcs.insert(std::make_pair(I->first, new monitored_client_t(*I->second)));
                }
                else
                  mcs__[0]->armed_ = true;
              }

              ++I;
            }
          }

          for (monitored_clients_t::iterator I = mcs.begin(), E = mcs.end(); I != E; ++I) {
            FUSION_ASSERT(callback__);
            FUSION_ASSERT(I->second->name_);

            try {
              callback__(I->second->status_, I->second->org_, I->first, I->second->name_, I->second->descr_, cookie__);
            }
            catch (...) {
              FUSION_ERROR("User callback excepted");
            }
          }
        }

        break;

      // exiting
      case WAIT_OBJECT_0 + 1:
        stop = true;

        break;

      case WAIT_ABANDONED_0:
      case WAIT_ABANDONED_0 + 1:
      case WAIT_FAILED:
        FUSION_WARN("WaitForMultipleObjects=%d err=%d", rc, ::GetLastError());

        stop = true;

        break;
      }

    return 0;
  }

  ////////////////////////////////////////////////////////////////////////////////
  nf::result_t init_monitor(nf::client_t& client, status_monitor_callback_t callback, void* cookie, const char* status_name, const char* status_notify_name) {
    {
      wlock_t lock(lock__);

      if (!client__)
        client__ = &client;
      else
        return nf::ERR_INITIALIZED;
    }

    FUSION_ASSERT(mcs__.empty());
    FUSION_ASSERT(eupdate__ == INVALID_HANDLE_VALUE);
    FUSION_ASSERT(estop__   == INVALID_HANDLE_VALUE);
    FUSION_ASSERT(thread__  == INVALID_HANDLE_VALUE);

    nf::result_t e;
    nf::mtype_t mode;
    size_t      msize;

    monitored_client_t* si = new monitored_client_t;

    si->descr_    = 0;
    si->name_     = 0;
    si->expires_  = 0;
    si->armed_    = false;
    mcs__.insert(std::make_pair(0, si));

    FUSION_ENSURE(eupdate__ = ::CreateEvent(0, FALSE, FALSE, 0),                                          { e = nf::ERR_WIN32; goto fail_0; });
    FUSION_ENSURE(estop__   = ::CreateEvent(0, FALSE, FALSE, 0),                                          { e = nf::ERR_WIN32; goto fail_1; });
    FUSION_ENSURE(nf::ERR_OK == (e = client__->reg_callback_method(callback_method__, 0, cmi__)),         goto fail_2);

    msize = -1;
    FUSION_ENSURE(nf::ERR_OK == (e = client__->mopen(status_name, nf::O_RDONLY, mode, mstatus__, msize)), goto fail_3);
    FUSION_ENSURE(nf::ERR_OK == (e = client__->subscribe(mstatus__, 0, cmi__, nf::callback_t(1))),        goto fail_4);

    msize = 0;
    FUSION_ENSURE(nf::ERR_OK == (e = client__->mopen(status_notify_name, nf::O_WRONLY, mode, mstatus_notify__, msize)), goto fail_5);

    thread__ = (HANDLE)::_beginthreadex(
        NULL,		// security,
        0,			// stack_size,
        run__,	// start_address
        0,			// arg
        0,	    // initflag,
        0				// ptr to thread id
    );

    FUSION_ENSURE(thread__ != 0 && thread__ != INVALID_HANDLE_VALUE, { e = nf::ERR_WIN32; goto fail_6; });

    callback__ = callback;
    cookie__   = cookie;

    return nf::ERR_OK;

  fail_6:
    thread__  = INVALID_HANDLE_VALUE;
    client__->mclose(mstatus_notify__);
    mstatus_notify__ = 0;

  fail_5:
    client__->unsubscribe(mstatus__);
    mstatus__ = 0;

  fail_4:
    client__->mclose(mstatus__);
    mstatus__ = 0;

  fail_3:
    client__->unreg_callback_method(cmi__);
    cmi__ = 0;

  fail_2:
    ::CloseHandle(estop__);
    estop__ = INVALID_HANDLE_VALUE;

  fail_1:
    ::CloseHandle(eupdate__);
    eupdate__ = INVALID_HANDLE_VALUE;

  fail_0:
    client__  = 0;
    mcs__.clear();

    return e;
  }

  ////////////////////////////////////////////////////////////////////////////////
  nf::result_t fini_monitor() {
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

    ::CloseHandle(thread__);

    ::CloseHandle(eupdate__);
    ::CloseHandle(estop__);

    client__->unsubscribe(mstatus__);
    client__->mclose(mstatus__);
    client__->unreg_callback_method(cmi__);

    callback__        = 0;
    mstatus__         = 0;
    mstatus_notify__  = 0;
    cmi__             = 0;

    eupdate__         = INVALID_HANDLE_VALUE;
    estop__           = INVALID_HANDLE_VALUE;
    client__          = 0;
    thread__          = INVALID_HANDLE_VALUE;

    return nf::ERR_OK;
  }

  ////////////////////////////////////////////////////////////////////////////////
  nf::result_t enum_monitor_clients(status_monitor_callback_t callback, bool show_incomlete) {
    FUSION_ENSURE(callback, return nf::ERR_PARAMETER);

    monitored_clients_t mcs;

    {
      rlock_t lock(lock__);

      if (!client__)
        return nf::ERR_INITIALIZED;

      for (monitored_clients_t::iterator I = mcs__.begin(), E = mcs__.end(); I != E; ++I)
        if (I->first != 0 && I->second->status_ != STATUS_TERM && (show_incomlete || I->second->name_))
          mcs.insert(std::make_pair(I->first, new monitored_client_t(*I->second)));
    }

    nf::result_t e;

    for (monitored_clients_t::iterator I = mcs.begin(), E = mcs.end(); I != E; ++I)
      if ((e = callback(I->second->status_, I->second->org_, I->first, I->second->name_, I->second->descr_, cookie__)) != nf::ERR_OK)
        return e;

    return nf::ERR_OK;
  }
}
