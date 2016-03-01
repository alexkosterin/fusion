/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#define __COMPILING_FUSION_DLL__

#include "include/configure.h"
#include "include/nf.h"
#include "include/nf_internal.h"
#include "include/version.h"
#include "include/mb.h"
#include "include/conn.h"
#include "include/toq.h"
#include "include/tsc.h"
#include "include/idpool.h"
#include "pq.h"
#include "pr.h"
#include "include/mcbpool.h"
#include "include/enumstr.h"
#include "mcb.proto/mcb.pb.h"
#include "include/enumstr.h"

#if	defined(WIN32)
# define NOGDI
# include <windows.h>
#	include <process.h>
# include <Winsock2.h>
# include <Rpc.h>
# pragma comment(lib, "Rpcrt4.lib")
#endif

#include "include/tcpconn.h"
#include "include/lock.h"
#include <string.h>

#include <list>

#define WORKER_GRACE_SHUTDOWN_PERIOD (5000)

namespace nf {
  extern version_t ver;

  bool pq_sort(mcb_t* a, mcb_t* b) {
    return a->prio_ < b->prio_;
  }

  // mcb or raw callback ///////////////////////////////////////////////////////
  typedef void (internal_client_t::*_mcb_callback_t)(mcb_t*);
  typedef result_t (*mcb_callback_t)(mcb_t*);

  // system callbacks //////////////////////////////////////////////////////////
  struct sys_cb_map_t : std::map<mid_t, _mcb_callback_t> {
    _mcb_callback_t get(mid_t m) {
      sys_cb_map_t::iterator I = find(m);

      return (I != end()) ? I->second : 0;
    }
  };

  //////////////////////////////////////////////////////////////////////////////
  struct open_message_t {
    size_t    size_;
    unsigned  oflags_;
    mtype_t   mtype_;
  };

  //////////////////////////////////////////////////////////////////////////////
  struct open_messages_t : std::map<mid_t, open_message_t> {
    _srwlock_t  lock_;

    bool exists(mid_t m) {
      return find(m) != end();
    }

    bool add(mid_t mid, size_t size, unsigned oflags, mtype_t mtype) {
      wlock_t lock(lock_);
      open_message_t om;

      om.oflags_  = oflags;
      om.size_    = size;
      om.mtype_   = mtype;

      return insert(std::make_pair(mid, om)).second;
    }

    ////////////////////////////////////////////////////////////////////////////
    size_t size(mid_t m) {
      rlock_t lock(lock_);

      iterator I = find(m);

      return (I != end()) ? I->second.size_ : -1;
    }

    ////////////////////////////////////////////////////////////////////////////
    unsigned oflags(mid_t m) {
      rlock_t lock(lock_);

      iterator I = find(m);

      return (I != end()) ? I->second.oflags_ : 0;
    }

    ////////////////////////////////////////////////////////////////////////////
    mtype_t mtype(mid_t m) { // @@
      rlock_t lock(lock_);

      iterator I = find(m);

      return (I != end()) ? I->second.mtype_ : 0;
    }

    ////////////////////////////////////////////////////////////////////////////
    bool del(mid_t mid) {
      wlock_t lock(lock_);

      return erase(mid) == 1;
    }
  };

  // subscribe callback descriptor /////////////////////////////////////////////
  struct cb_dcr_t {
    cmi_t       mask_;  // callback methods mask
    callback_t  cb_;    // simplified callback; no access to mcb
  };

  //////////////////////////////////////////////////////////////////////////////
  struct cb_map_t: std::map<mid_t, cb_dcr_t> {
    cb_dcr_t* get(mid_t m) {
      cb_map_t::iterator I = find(m);

      return (I != end()) ? &I->second : 0;
    }

    ////////////////////////////////////////////////////////////////////////////
    bool add(mid_t mid, cmi_t mask, callback_t cb) {
      cb_dcr_t cd;

      cd.mask_  = mask;
      cd.cb_    = cb;

      return insert(std::make_pair(mid, cd)).second;
    }

    ////////////////////////////////////////////////////////////////////////////
    bool del(mid_t mid) {
      return erase(mid) == 1;
    }
  };

  //////////////////////////////////////////////////////////////////////////////
  // there are 32 bits in 'cmi_t' type, hence this ugly limits...
  struct cbm_map_t : std::map<cmi_t, std::pair<callback_method_t, void*>> {
    idpool_t<5, 1, 1, -1> idpool_;

    cbm_map_t() {}

    ////////////////////////////////////////////////////////////////////////////
    std::pair<callback_method_t, void*>* get(cmi_t m) {
      cbm_map_t::iterator I = find(m);

      return (I != end()) ? &I->second : 0;
    }

    ////////////////////////////////////////////////////////////////////////////
    cmi_t add(std::pair<callback_method_t, void*> cb) {
      FUSION_ASSERT(cb.first);

      cmi_t id = idpool_.get();

      FUSION_ASSERT(id != 16);

      cmi_t method = 1 << id;

      FUSION_ASSERT(method && method != CM_MANUAL);

      bool rc = insert(std::make_pair(method, cb)).second;

      FUSION_ASSERT(rc);

      return method;
    }

    ////////////////////////////////////////////////////////////////////////////
    bool del(cmi_t method) {
      FUSION_DEBUG("del: method == %d", method);
      FUSION_ASSERT(method && method != CM_MANUAL);

      FUSION_DEBUG("del: before for loop");

      for (cmi_t id = 1; id < 16; ++id)
        if ((1 << id) == method) {
          FUSION_DEBUG("del: Found callback to deregister");
          size_t nr = erase(method) == 1;

          FUSION_DEBUG("del: asserting that %d == 1", nr);
          FUSION_ASSERT(nr == 1);

          idpool_.put(id);

          return nr == 1;
        }

        return false;
    }
  };

  //////////////////////////////////////////////////////////////////////////////
  struct internal_client_t;

  struct reg_pending_req_t: public pending_req_t { /////////////////////////////
    internal_client_t* ic_;

    reg_pending_req_t(const mcb_t* req, HANDLE evt, internal_client_t* ic): pending_req_t(req, evt), ic_(ic) { FUSION_ASSERT(ic_); }
    reg_pending_req_t(const mcb_t* req, internal_client_t* ic): pending_req_t(req), ic_(ic) { FUSION_ASSERT(ic_); }
    void completion();
  };

  struct unreg_pending_req_t: public pending_req_t { ///////////////////////////
    internal_client_t* ic_;

    unreg_pending_req_t(const mcb_t* req, HANDLE evt, internal_client_t* ic): pending_req_t(req, evt), ic_(ic) { FUSION_ASSERT(ic_); }
    unreg_pending_req_t(const mcb_t* req, internal_client_t* ic): pending_req_t(req), ic_(ic) { FUSION_ASSERT(ic_); }
    void completion();
  };

  struct mopen_pending_req_t: public pending_req_t { ///////////////////////////
    internal_client_t* ic_;
    oflags_t  oflags_;
    mtype_t&  type_;
    mid_t&    m_;
    size_t&   size_;

    mopen_pending_req_t(const mcb_t* req, HANDLE evt, internal_client_t* ic, oflags_t f, mtype_t& t, mid_t& m, size_t&s): pending_req_t(req, evt), ic_(ic), oflags_(f), type_(t), m_(m), size_(s) { FUSION_ASSERT(ic_); }
    mopen_pending_req_t(const mcb_t* req, internal_client_t* ic, oflags_t f, mtype_t& t, mid_t& m, size_t&s): pending_req_t(req), ic_(ic), oflags_(f), type_(t), m_(m), size_(s) { FUSION_ASSERT(ic_); }
    void completion();
  };

  struct mclose_pending_req_t: public pending_req_t { //////////////////////////
    internal_client_t* ic_;
    mid_t mid_;

    mclose_pending_req_t(const mcb_t* req, HANDLE evt, internal_client_t* ic, mid_t mid): pending_req_t(req, evt), ic_(ic), mid_(mid) { FUSION_ASSERT(ic_); }
    mclose_pending_req_t(const mcb_t* req, internal_client_t* ic, mid_t mid): pending_req_t(req), ic_(ic), mid_(mid) { FUSION_ASSERT(ic_); }
    void completion();
  };

  struct subscribe_pending_req_t: public pending_req_t { ///////////////////////
    internal_client_t* ic_;
    mid_t m_;
    int flags_;
    cmi_t cmi_;
    result_t (__stdcall *cb_)(mid_t, size_t, const void*);

    subscribe_pending_req_t(const mcb_t* req, HANDLE evt, internal_client_t* ic, mid_t m, int flags, cmi_t cmi, result_t (__stdcall *cb)(mid_t, size_t, const void*))
    : pending_req_t(req, evt), ic_(ic), m_(m), flags_(flags), cmi_(cmi), cb_(cb) {
      FUSION_ASSERT(ic_);
    }

    subscribe_pending_req_t(const mcb_t* req, internal_client_t* ic, mid_t m, int flags, cmi_t cmi, result_t (__stdcall *cb)(mid_t, size_t, const void*))
    : pending_req_t(req), ic_(ic), m_(m), flags_(flags), cmi_(cmi), cb_(cb) {
      FUSION_ASSERT(ic_);
    }

    void completion();
  };

  struct unsubscribe_pending_req_t: public pending_req_t { /////////////////////
    internal_client_t* ic_;
    mid_t mid_;

    unsubscribe_pending_req_t(const mcb_t* req, HANDLE evt, internal_client_t* ic, mid_t mid): pending_req_t(req, evt), ic_(ic), mid_(mid) { FUSION_ASSERT(ic_); }
    unsubscribe_pending_req_t(const mcb_t* req, internal_client_t* ic, mid_t mid): pending_req_t(req), ic_(ic), mid_(mid) { FUSION_ASSERT(ic_); }
    void completion();
  };

  __declspec(thread) mcb_t* current_mcb__;
  __declspec(thread) bool   reply_sent__;

#if DEBUG_HEAP > 0
  mcbpool_t         lost_and_found__;
#endif

  //////////////////////////////////////////////////////////////////////////////
  struct internal_client_t {
    UUID            uuid_;
    const char*     name_;            // user-friendly (non-unique) name
    cid_t           cid_;             // assigned id
    ::HANDLE        worker_;          // worker thread handle
    ::HANDLE        stop_event_;
    sys_cb_map_t    sys_handlers_;    // incoming system message handlers
    cb_map_t        scb_;             // subscribe callbacks
    conn_t*         conn_;            //
    pq_t<
      mcb_t*,
      pq_sort
    >               pq_;              // req priority queue

    _srwlock_t      lock_;            // general lock (?)
    _srwlock_t      scb_lock_;        // subscribe lock
    _srwlock_t      mq_lock_;         // manual (callback method) queue lock

    toq_t<
      mcb_t
    >               toq_;             // req timeout queue
    pending_reqs_t  prs_;             // pending requests

    seq_t*          next_seq_;        // must be 32-bit aligned

    mcbpool_t       mcbpool_;

    std::list<mcb_t*> mq_;            // manual queue
    ::HANDLE        mq_event_;        // manual queue wait event
    cbm_map_t       cbms_;            // registered callback methods
    open_messages_t omsgs_;           //

    sockaddr_in     server_addr_;
    SOCKET          udp_socket_;
    ::WSAEVENT	    udp_event_;

    // ** non worker thread ** /////////////////////////////////////////////////
    internal_client_t(const char* name) :
      cid_(CID_NONE),
      name_(::_strdup(name ? name : "")),
      conn_(0),
      worker_(INVALID_HANDLE_VALUE),
      mcbpool_(1024, 1024),
      next_seq_((seq_t*)_aligned_malloc(sizeof seq_t, 4))
    {
      RPC_STATUS rs = ::UuidCreate(&uuid_);

      FUSION_ASSERT(rs == RPC_S_OK);
      FUSION_ASSERT(next_seq_);

      FUSION_VERIFY((stop_event_ = ::CreateEvent(0, FALSE, FALSE, 0)) != INVALID_HANDLE_VALUE, "CreateEvent()=%d", ::GetLastError());
      FUSION_VERIFY((mq_event_   = ::CreateEvent(0, FALSE, FALSE, 0)) != INVALID_HANDLE_VALUE, "CreateEvent()=%d", ::GetLastError());
      FUSION_VERIFY((udp_event_ = ::WSACreateEvent()) != WSA_INVALID_EVENT, "WSACreateEvent()=%d", ::WSAGetLastError());

      *next_seq_ = 0;

      {
#define INSTALL_SYS_HANDLER(M, C) sys_handlers_.insert(std::pair<mid_t, _mcb_callback_t>(M, C));
        // system
        INSTALL_SYS_HANDLER(MD_SYS_STATUS,                      &nf::internal_client_t::omh_unexpected);
        INSTALL_SYS_HANDLER(MD_SYS_ECHO_REQUEST,                &nf::internal_client_t::omh_echo_request);
        INSTALL_SYS_HANDLER(MD_SYS_ECHO_REPLY,                  &nf::internal_client_t::omh_unexpected);

        // time synchronization
        INSTALL_SYS_HANDLER(MD_SYS_TIMESYNC_REQUEST,            &nf::internal_client_t::omh_timesync_request);
        INSTALL_SYS_HANDLER(MD_SYS_TIMESYNC_REPLY,              &nf::internal_client_t::omh_unexpected);

        // registration/termination
        INSTALL_SYS_HANDLER(MD_SYS_REGISTER_REQUEST,            &nf::internal_client_t::omh_unexpected);
        INSTALL_SYS_HANDLER(MD_SYS_REGISTER_REPLY,              &nf::internal_client_t::omh_unexpected);
        INSTALL_SYS_HANDLER(MD_SYS_STOP_REQUEST,                &nf::internal_client_t::omh_stop_request_default);
        INSTALL_SYS_HANDLER(MD_SYS_TERMINATE_REQUEST,           &nf::internal_client_t::omh_terminate_request_default);

        // clients
        INSTALL_SYS_HANDLER(MD_SYS_QUERY_CLIENTS_REQUEST,       &nf::internal_client_t::omh_unexpected);
        INSTALL_SYS_HANDLER(MD_SYS_QUERY_CLIENT_BY_ID_REQUEST,  &nf::internal_client_t::omh_unexpected);
        INSTALL_SYS_HANDLER(MD_SYS_QUERY_CLIENTS_REPLY,         &nf::internal_client_t::omh_unexpected);
        INSTALL_SYS_HANDLER(MD_SYS_QUERY_CLIENT_REPLY,          &nf::internal_client_t::omh_unexpected);

        // user
//      INSTALL_SYS_HANDLER(MD_SYS_QUERY_USER_REQUEST,          &nf::internal_client_t::omh_unexpected);
//      INSTALL_SYS_HANDLER(MD_SYS_QUERY_USER_REPLY,            &nf::internal_client_t::omh_unexpected));

        // groups
        INSTALL_SYS_HANDLER(MD_SYS_QUERY_GROUP_REQUEST,         &nf::internal_client_t::omh_unexpected);
        INSTALL_SYS_HANDLER(MD_SYS_QUERY_GROUP_REPLY,           &nf::internal_client_t::omh_unexpected);

        // message
        INSTALL_SYS_HANDLER(MD_SYS_MOPEN_REQUEST,               &nf::internal_client_t::omh_unexpected);
        INSTALL_SYS_HANDLER(MD_SYS_MCLOSE_REQUEST,              &nf::internal_client_t::omh_unexpected);
        INSTALL_SYS_HANDLER(MD_SYS_MUNLINK_REQUEST,             &nf::internal_client_t::omh_unexpected);
        INSTALL_SYS_HANDLER(MD_SYS_MLINK_REQUEST,               &nf::internal_client_t::omh_unexpected);
//      INSTALL_SYS_HANDLER(MD_SYS_MMOVE_REQUEST,               &nf::internal_client_t::omh_unexpected);
//      INSTALL_SYS_HANDLER(MD_SYS_MLIST_REQUEST,               &nf::internal_client_t::omh_unexpected);

        // message attribues
//      INSTALL_SYS_HANDLER(MD_SYS_MSTAT_BY_ID_REQUEST,         &nf::internal_client_t::omh_unexpected);
//      INSTALL_SYS_HANDLER(MD_SYS_MSTAT_BY_NAME_REQUEST,       &nf::internal_client_t::omh_unexpected);
//      INSTALL_SYS_HANDLER(MD_SYS_MATTR_READ_REQUEST,          &nf::internal_client_t::omh_unexpected);
//      INSTALL_SYS_HANDLER(MD_SYS_MATTR_WRITE_REQUEST,         &nf::internal_client_t::omh_unexpected);
//      INSTALL_SYS_HANDLER(MD_SYS_MATTR_BULK_READ_REQUEST,     &nf::internal_client_t::omh_unexpected);
//      INSTALL_SYS_HANDLER(MD_SYS_MATTR_BULK_WRITE_REQUEST,    &nf::internal_client_t::omh_unexpected);
//      INSTALL_SYS_HANDLER(MD_SYS_MLIST_REPLY,                 &nf::internal_client_t::omh_unexpected);
//      INSTALL_SYS_HANDLER(MD_SYS_MSTAT_REPLY,                 &nf::internal_client_t::omh_unexpected);
//      INSTALL_SYS_HANDLER(MD_SYS_MATTR_READ_REPLY,            &nf::internal_client_t::omh_unexpected);
//      INSTALL_SYS_HANDLER(MD_SYS_MATTR_BULK_READ_REPLY,       &nf::internal_client_t::omh_unexpected);

        // subscribe
        INSTALL_SYS_HANDLER(MD_SYS_SUBSCRIBE_REQUEST,           &nf::internal_client_t::omh_unexpected);
        INSTALL_SYS_HANDLER(MD_SYS_UNSUBSCRIBE_REQUEST,         &nf::internal_client_t::omh_unexpected);

        // sys info
        INSTALL_SYS_HANDLER(MD_SYS_SYSINFO_REQUEST,             &nf::internal_client_t::omh_unexpected);
#undef INSTALL_SYS_HANDLER
      }
    }

    ~internal_client_t() {
      unreg();

      ::SetEvent(stop_event_);

      FUSION_ASSERT(worker_ && worker_ != INVALID_HANDLE_VALUE);

      switch(::WaitForSingleObject(worker_, MB_SHUTDOWN_TIMEOUT_MSECS)) {
      case WAIT_OBJECT_0:
        /* ended gracefully */
        break;

      case WAIT_TIMEOUT:
        FUSION_WARN("Worker thread wait failed after %d ms, terminating it.", MB_SHUTDOWN_TIMEOUT_MSECS);

        ::TerminateThread(worker_, 0);
        ::CloseHandle(worker_);
        worker_ = INVALID_HANDLE_VALUE;

        break;
      }

      ::CloseHandle(worker_);
      ::CloseHandle(stop_event_);
      ::CloseHandle(udp_event_);

      // free resources
      free((void*)name_);
      ::_aligned_free(next_seq_);
      delete conn_;
    }

    ////////////////////////////////////////////////////////////////////////////

    // ** non worker thread ** /////////////////////////////////////////////////
    void _cleanup() {
      ::SetEvent(stop_event_);
      ::WaitForSingleObject(worker_, WORKER_GRACE_SHUTDOWN_PERIOD);
      conn_->close();
      //::CloseHandle(udp_event_);      FUSION_INFO("close udp_event_=%d",  udp_event_);
      ::closesocket(udp_socket_);       FUSION_INFO("close udp_socket_=%d", udp_socket_);
      ::ResetEvent(stop_event_);
    }

    ////////////////////////////////////////////////////////////////////////////
    bool write(mcb_t* mcb) {
      FUSION_ASSERT(mcb);

      if (!conn_->write_mcb(mcb)) {
        FUSION_DEBUG("write_mcb failed");

        _cleanup();

        return false;
      }

      return true;
    }

    //////////////////////////////////////////////////////////////////////////////
    bool blocking_write(mcb_t* mcb) {
      FUSION_ASSERT(mcb);

      ::HANDLE write_event = ::CreateEvent(0, FALSE, FALSE, 0);

      FUSION_ASSERT(write_event);

      mcb->wevent_ = write_event;

      if (!conn_->write_mcb(mcb)) {
        FUSION_DEBUG("write_mcb failed");

        _cleanup();

        return false;
      }

      bool rc;
      ::HANDLE wha[] = { write_event, worker_ };

retry:switch (::WaitForMultipleObjectsEx(FUSION_ARRAY_SIZE(wha), wha, FALSE, INFINITE, TRUE)) {
      case WAIT_OBJECT_0:       rc = true;  break;
      default:                  rc = false; break;
      case WAIT_IO_COMPLETION:  goto retry;
      }

      ::CloseHandle(write_event);

      return rc;
    }

    ////////////////////////////////////////////////////////////////////////////
    char udp_buff_[0x10000]; // 64K, Fusion max mcb + payload size

    ////////////////////////////////////////////////////////////////////////////
    bool udp_read(mcb_t*& mcb) {
      sockaddr_in from;
      int         from_len = sizeof from;

#if USE_VECTORED_IO > 0
      mcb = mcbpool_.incoming_get(sizeof mcb_xfer_t);

      FUSION_ASSERT(mcb);

      WSABUF  bufs[2] = {{ sizeof mcb_xfer_t, (CHAR*)mcb }, { sizeof udp_buff_, (CHAR*)udp_buff_ }};
      DWORD   bytes = 0, flags = 0;

      if (::WSARecvFrom(udp_socket_, (LPWSABUF)&bufs, FUSION_ARRAY_SIZE(bufs), &bytes, &flags, (sockaddr*)&from, &from_len, 0, 0) == SOCKET_ERROR) {
        FUSION_ERROR("::WSARecvFrom=%d", ::WSAGetLastError());

        return false;
      }

      FUSION_ASSERT(bytes >= sizeof mcb_xfer_t);
      FUSION_ASSERT(bytes < 0x10000 - sizeof mcb_xfer_t);

      mcb->len_ = (unsigned short)(bytes - sizeof mcb_xfer_t);

      if (mcb->len_ > sizeof mcb->u) {
        mcb->u.pdata_ = ::malloc(mcb->len_);

        FUSION_ASSERT(mcb->u.pdata_);

        ::memcpy(mcb->u.pdata_, udp_buff_, mcb->len_);
      }
      else
        ::memcpy(&mcb->u.data_, udp_buff_, mcb->len_);

      return true;
#else
      int len = ::recvfrom(udp_socket_, udp_buff_, sizeof udp_buff_, 0, (SOCKADDR*)&from, &from_len))

      if (len == SOCKET_ERROR) {
        FUSION_ERROR("recvfrom()=%d", ::WSAGetLastError());

        return false;
      }

      FUSION_ASSERT(sizeof mcb_xfer_t <= len && len <= 0x10000);

      // extract mcb...
      mcb = mcbpool_.incoming_get(len);

      FUSION_ASSERT(mcb);

      *(mcb_xfer_t*)mcb = *(mcb_xfer_t*)udp_buff_;
      ::memcpy(mcb->data(), &udp_buff_[sizeof mcb_xfer_t], len - sizeof mcb_xfer_t);

      return true;
#endif
    }

    ////////////////////////////////////////////////////////////////////////////
    bool udp_write(mcb_t* mcb) {
      FUSION_ASSERT(mcb);

#if USE_VECTORED_IO > 0
      WSABUF  bufs[2] = {{ sizeof mcb_xfer_t, (CHAR*)mcb }, { mcb->len_, (CHAR*)mcb->data() }};
      DWORD   bytes = 0;

retry:
      if (::WSASendTo(udp_socket_, (LPWSABUF)&bufs, FUSION_ARRAY_SIZE(bufs), &bytes, 0, (sockaddr*)&server_addr_, sizeof server_addr_, 0, 0) == SOCKET_ERROR) {
        if (::WSAGetLastError() == WSAEWOULDBLOCK) {
          ::Sleep(0);
          goto retry;
        }

        FUSION_ERROR("::WSASendTo=%d", ::WSAGetLastError());

        return false;
      }

      return bytes == sizeof mcb_xfer_t + mcb->len_;
#else
      int len = sizeof mcb_xfer_t + mcb->len_;

      // serialize mcb
      *(mcb_xfer_t*)udp_buff_ = *(mcb_xfer_t*)mcb;
      ::memcpy(&udp_buff_[sizeof mcb_xfer_t], mcb->data(), mcb->len_);

      if (len != ::sendto(udp_socket_, udp_buff_, len, 0, (SOCKADDR*)&server_addr_, sizeof server_addr_)) {
        FUSION_ERROR("sendto()=%d", ::WSAGetLastError());

        return false;
      }

      return true;
#endif // WIN32
    }

    ////////////////////////////////////////////////////////////////////////////
    void omh_unexpected(mcb_t* mcb) {
      FUSION_WARN("unexpected %s: mid=%s seq=%d src=%d dst=%d len=%d", mcb->request_ ? "req" : CID_IS_CLIENT(mcb->dst_) ? "post" : "pub",  enumstr((_md_sys_t)mcb->mid_), mcb->seq_, mcb->src_, mcb->dst_, mcb->len_);
    }

    ////////////////////////////////////////////////////////////////////////////
    void omh_echo_request(mcb_t* mcb) {
      FUSION_DEBUG("echo request: %s", enumstr((_md_sys_t)mcb->mid_));

      mcb->dst_     = MD_SYS_ECHO_REPLY;
      mcb->dst_     = mcb->src_;
      mcb->src_     = cid_;
      mcb->org_     = now_msecs();
      mcb->expired_ = false;
      mcb->req_seq_ = mcb->seq_;
      mcb->seq_     = InterlockedIncrement(next_seq_);
      mcb->timeout_ = 0;
      mcb->request_ = 0;

      blocking_write(mcb);
    }

    ////////////////////////////////////////////////////////////////////////////
    void omh_timesync_request(mcb_t* mcb) {
      FUSION_DEBUG("timesync request: %s", enumstr((_md_sys_t)mcb->mid_));

      mcb->mid_     = MD_SYS_TIMESYNC_REPLY;
      mcb->dst_     = mcb->src_;
      mcb->src_     = cid_;
      mcb->u.msecs_ = mcb->org_;
      mcb->org_     = now_msecs();
      mcb->expired_ = false;
      mcb->req_seq_ = mcb->seq_;
      mcb->seq_     = InterlockedIncrement(next_seq_);
      mcb->timeout_ = 0;
      mcb->request_ = 0;

      FUSION_DEBUG("mbm=%lld cli=%lld", mcb->u.msecs_, mcb->org_);

      mcb->len_     = sizeof(mcb->u.msecs_);
      blocking_write(mcb);
    }

    ////////////////////////////////////////////////////////////////////////////
    void omh_stop_request_default(mcb_t* mcb) {
      FUSION_WARN("stop request: %s src=%d", enumstr((_md_sys_t)mcb->mid_), mcb->src_);

      result_t e    = ERR_IGNORE;

      mcb->mid_     = MD_SYS_STATUS;
      mcb->dst_     = mcb->src_;
      mcb->src_     = cid_;
      mcb->org_     = now_msecs();
      mcb->expired_ = false;
      mcb->req_seq_ = mcb->seq_;
      mcb->seq_     = InterlockedIncrement(next_seq_);
      mcb->timeout_ = 0;
      mcb->request_ = 0;

      mcb->reset_data();
      mcb->data(&e, sizeof e);

      blocking_write(mcb);
    }

    ////////////////////////////////////////////////////////////////////////////
    void omh_terminate_request_default(mcb_t* mcb) {
      FUSION_WARN("terminate request: %s src=%d", enumstr((_md_sys_t)mcb->mid_), mcb->src_);

      result_t e = ERR_OK;
      int reason = mcb->u.int_;

      mcb->mid_     = MD_SYS_STATUS;
      mcb->dst_     = mcb->src_;
      mcb->src_     = cid_;
      mcb->org_     = now_msecs();
      mcb->expired_ = false;
      mcb->req_seq_ = mcb->seq_;
      mcb->seq_     = InterlockedIncrement(next_seq_);
      mcb->timeout_ = 0;
      mcb->request_ = 0;

      mcb->reset_data();
      mcb->data(&e, sizeof e);

      blocking_write(mcb);

      ::TerminateProcess(::GetCurrentProcess(), reason);
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t reg(const char* conn, size_t profiles_nr, const char** profile) {
      if (!conn)
        return ERR_PARAMETER;

      {
        wlock_t lock(lock_);

        if (worker_ == INVALID_HANDLE_VALUE)
          worker_ = (HANDLE)::_beginthreadex(
             NULL,							// security,
             0,									// stack_size,
             run__,							// start_address
             this,							// arg
             CREATE_SUSPENDED,	// initflag,
             0									// ptr to thread id
          );
      }

      {
        wlock_t lock(lock_);

        if (!conn_)
          conn_ = create_conn(conn, &mcbpool_);
      }

      if (!conn_ || worker_ == INVALID_HANDLE_VALUE) {
        if (conn_) {
          conn_->close();
          //::CloseHandle(udp_event_);      FUSION_INFO("close udp_event_=%d",  udp_event_);
          ::closesocket(udp_socket_);       FUSION_INFO("close udp_socket_=%d", udp_socket_);
        }

        if (worker_ != INVALID_HANDLE_VALUE) {
          ::SetEvent(stop_event_);

          switch (::WaitForSingleObject(worker_, MB_SHUTDOWN_TIMEOUT_MSECS)) {
          case WAIT_OBJECT_0:
            /* ended gracefully*/
            break;

          case WAIT_TIMEOUT:
            ::TerminateThread(worker_, 0);
            break;

          default:
            worker_ = INVALID_HANDLE_VALUE;

            return ERR_PARAMETER;
          }

          ::CloseHandle(worker_);
          worker_ = INVALID_HANDLE_VALUE;
        }

        return ERR_PARAMETER;
      }

      if (cid_ != CID_NONE)
        return ERR_REGISTERED;

      if (!conn_->open())
        return ERR_CONNECTION;

      {
        sockaddr_in a;
        int len = sizeof a;

        if (::getpeername(((tcp_conn_t*)conn_)->socket_, (SOCKADDR*)&server_addr_, &len) == SOCKET_ERROR)
          return ERR_UNEXPECTED; //@@

        FUSION_ASSERT(len == sizeof a);

        if (::getsockname(((tcp_conn_t*)conn_)->socket_, (SOCKADDR*)&a, &len) == SOCKET_ERROR)
          return ERR_UNEXPECTED; //@@

        FUSION_ASSERT(len == sizeof a);

        FUSION_ENSURE((udp_socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) != INVALID_SOCKET, return ERR_UNEXPECTED,  "::socket()->%d", ::WSAGetLastError());
#if 1
        {
          BOOL v = 1;
          int rc = ::setsockopt(udp_socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&v, sizeof v);

          if (rc == SOCKET_ERROR)
            FUSION_WARN("setsockopt(SOL_SOCKET)=%d", ::WSAGetLastError());
        }
#endif
        FUSION_ENSURE(::bind(udp_socket_, (SOCKADDR*)&a, len) != SOCKET_ERROR,                      return ERR_UNEXPECTED,  "::bind(%d, (SOCKADDR*)&a, %d)->%d", udp_socket_, len, ::WSAGetLastError());
        FUSION_ENSURE(::WSAEventSelect(udp_socket_, udp_event_, FD_READ) != SOCKET_ERROR,           return ERR_UNEXPECTED,  "WSAEventSelect(%d, %d, FD_READ)->%d", udp_socket_, udp_event_, ::WSAGetLastError());
      }

      scb_.clear();

      ::ResumeThread(worker_);

      mcb_t* req = mcbpool_.outgoing_get(MD_SYS_REGISTER_REQUEST, CID_NONE, CID_SYS, InterlockedIncrement(next_seq_), true);
      mcb_reg d;

      d.set_ver_maj(ver.mini.maj);
      d.set_ver_min(ver.mini.min);
      d.set_name(name_);
      d.set_uuid((const char*)&uuid_, sizeof uuid_);

      for (size_t i = 0; i < profiles_nr; ++i)
        d.add_profile(profile[i]);

      req->len_ = d.ByteSize();

      if (req->len_ > sizeof req->u)
        req->u.pdata_ = ::malloc(req->len_);

      if (!d.SerializeToArray(req->data(), req->len_))
        return ERR_PARAMETER;

      reg_pending_req_t pr(req, this);

      prs_.put(pr);

      if (!blocking_write(req))
        return ERR_CONNECTION;

      if (pr.wait(MB_DEFAULT_TIMEOUT_MSECS) || !prs_.get(req->seq_)) {
        FUSION_ASSERT(pr.rep_, "have reply");
        FUSION_ASSERT(pr.rep_->mid_ == MD_SYS_REGISTER_REPLY || pr.rep_->mid_ == MD_SYS_STATUS, "expected mid or ACK");
        FUSION_ASSERT(pr.rep_->src_ == CID_SYS, "reply from mbm");

        result_t e = pr.rep_->u.error_;

        RELEASE(mcbpool_, req);
        RELEASE(mcbpool_, pr.rep_);

        return e;
      }

      RELEASE(mcbpool_, req);

      return ERR_TIMEOUT;
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t unreg() {
      if (cid_ == CID_NONE)
        return ERR_REGISTERED;

      if (!conn_->open())
        return ERR_CONNECTION;

      mcb_t* req = mcbpool_.outgoing_get(MD_SYS_UNREGISTER_REQUEST, cid_, CID_SYS, InterlockedIncrement(next_seq_), true);

      req->len_     = 0;
      req->u.pdata_ = 0;

      unreg_pending_req_t pr(req, this);

      prs_.put(pr);

      if (!blocking_write(req))
        return ERR_CONNECTION;

      if (pr.wait(MB_DEFAULT_TIMEOUT_MSECS) || !prs_.get(req->seq_)) {
        FUSION_ASSERT(pr.rep_, "have reply");
        FUSION_ASSERT(pr.rep_->mid_ == MD_SYS_UNREGISTER_REPLY || pr.rep_->mid_ == MD_SYS_STATUS, "expected mid or ACK");
        FUSION_ASSERT(pr.rep_->src_ == CID_SYS, "reply from mbm");
        FUSION_ASSERT(pr.rep_->dst_ != CID_NONE, "invalid reply cid");

        result_t e = pr.rep_->u.error_;

        RELEASE(mcbpool_, req);
        RELEASE(mcbpool_, pr.rep_);

        return e;
      }

      RELEASE(mcbpool_, req);

      return ERR_TIMEOUT;
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    void terminate() {
      _cleanup();
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    // when call fails with ERR_OPEN then:
    //   MD_SYS_NONE will mean _message_not_found_
    //  !MD_SYS_NONE will mean _you_aleary_have_it, check returned mid
    result_t _mopen(const char* name, oflags_t oflags, mtype_t& type, mid_t& mid, size_t& size, size_t len, const void* data, bool create) {
      FUSION_ASSERT((len && data) || (!len && !data));

      if (cid_ == CID_NONE)
        return ERR_REGISTERED;

      if (!conn_->open())
        return ERR_CONNECTION;

      if (!name)
        return ERR_PARAMETER;

      if (oflags & ~O_VALIDATE_MASK)
        return ERR_PARAMETER;

      if (create)
        if (type & ~MT_VALIDATE_MASK)
          return ERR_PARAMETER;

      mcb_mopen_req d;

      d.set_name(name);

      if (create) {
        // verify create mtype bits
        switch (type & MT_TYPE_MASK) {
        case MT_EVENT:
          if (type & MT_PERSISTENT && len == 0) {
            FUSION_WARN("not initializing persistent message");

#if MBM_ENFORCE_INIT_PERSISTENT_MESSAGE
            return ERR_PARAMETER;
#endif
          }
        }

        oflags |= O_CREATE;
        d.set_type(type);
        d.set_size(size);

        if (len || data)
          d.set_data((const char*)data, len);
      }
      else {
        // verify open mtype bits
        switch (type & MT_TYPE_MASK) {
        case MT_EVENT:
          if (type & MT_PERSISTENT && len) {
            FUSION_WARN("initialize persistent message allowed only on create");

#if MBM_ENFORCE_INIT_PERSISTENT_MESSAGE
            return ERR_PARAMETER;
#endif
          }
        }

        oflags &= ~O_CREATE;
      }

      d.set_flags(oflags);

      mcb_t* req = mcbpool_.outgoing_get(MD_SYS_MOPEN_REQUEST, cid_, CID_SYS, InterlockedIncrement(next_seq_), true);

      req->len_ = d.ByteSize();

      if (req->len_ > sizeof req->u)
        req->u.pdata_ = ::malloc(req->len_);

      if (!d.SerializeToArray(req->data(), req->len_))
        return ERR_PARAMETER;

      mopen_pending_req_t pr(req, this, oflags, type, mid, size);

      prs_.put(pr);

      if (!blocking_write(req))
        return ERR_CONNECTION;

      if (pr.wait(MB_DEFAULT_TIMEOUT_MSECS) || !prs_.get(req->seq_)) {
        FUSION_ASSERT(pr.rep_, "have reply");
        FUSION_ASSERT(pr.rep_->mid_ == MD_SYS_MOPEN_REPLY || pr.rep_->mid_ == MD_SYS_STATUS, "expected mid or ACK");
        FUSION_ASSERT(pr.rep_->src_ == CID_SYS, "reply from mbm");
        FUSION_ASSERT(pr.rep_->dst_ == cid_, "invalid reply cid");

        result_t e;

        if (pr.rep_->mid_ == MD_SYS_MOPEN_REPLY)
          e  = ERR_OK;
        else if (pr.rep_->mid_ == MD_SYS_STATUS)
          e = pr.rep_->u.error_;
        else
          e = ERR_UNEXPECTED;

        RELEASE(mcbpool_, req);
        RELEASE(mcbpool_, pr.rep_);

        return e;
      }

      RELEASE(mcbpool_, req);

      return ERR_TIMEOUT;
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t mcreate(const char* name, oflags_t oflags, mtype_t mode, mid_t& mid, size_t size, size_t len, const void* data) {
      return _mopen(name, oflags, mode, mid, size, len, data, true);
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t mopen(const char* name, oflags_t oflags, mtype_t& mode, mid_t& mid, size_t& size) {
      return _mopen(name, oflags, mode, mid, size, 0, 0, false);
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t mclose(mid_t mid) {
      if (cid_ == CID_NONE)
        return ERR_REGISTERED;

      if (!conn_->open())
        return ERR_CONNECTION;

      mcb_t* req = mcbpool_.outgoing_get(MD_SYS_MCLOSE_REQUEST, cid_, CID_SYS, InterlockedIncrement(next_seq_), true);

      req->len_   = sizeof mid;
      req->u.mid_ = mid;

      mclose_pending_req_t pr(req, this, mid);

      prs_.put(pr);

      if (!blocking_write(req))
        return ERR_CONNECTION;

      if (pr.wait(MB_DEFAULT_TIMEOUT_MSECS) || !prs_.get(req->seq_)) {
        FUSION_ASSERT(pr.rep_, "have reply");
        FUSION_ASSERT(pr.rep_->mid_ == MD_SYS_STATUS, "expected mid or ACK");
        FUSION_ASSERT(pr.rep_->src_ == CID_SYS, "reply from mbm");
        FUSION_ASSERT(pr.rep_->dst_ == cid_, "invalid reply cid");

        result_t e = pr.rep_->u.error_;

        RELEASE(mcbpool_, req);
        RELEASE(mcbpool_, pr.rep_);

        return e;
      }

      RELEASE(mcbpool_, req);

      return ERR_TIMEOUT;
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t munlink(const char* name) {
      if (cid_ == CID_NONE)
        return ERR_REGISTERED;

      if (!conn_->open())
        return ERR_CONNECTION;

      if (!name)
        return ERR_PARAMETER;

      mcb_t* req = mcbpool_.outgoing_get(MD_SYS_MUNLINK_REQUEST, cid_, CID_SYS, InterlockedIncrement(next_seq_), true);

      mcb_munlink d;

      d.set_name(name);

      req->len_ = d.ByteSize();

      if (req->len_ > sizeof req->u)
        req->u.pdata_ = ::malloc(req->len_);

      if (!d.SerializeToArray(req->data(), req->len_))
        return ERR_PARAMETER;

      pending_req_t pr(req);

      prs_.put(pr);

      if (!blocking_write(req))
        return ERR_CONNECTION;

      if (pr.wait(MB_DEFAULT_TIMEOUT_MSECS) || !prs_.get(req->seq_)) {
        FUSION_ASSERT(pr.rep_, "have reply");
        FUSION_ASSERT(pr.rep_->mid_ == MD_SYS_STATUS, "expected mid or ACK");
        FUSION_ASSERT(pr.rep_->src_ == CID_SYS, "reply from mbm");
        FUSION_ASSERT(pr.rep_->dst_ == cid_, "invalid reply cid");

        result_t e = pr.rep_->u.error_;

        RELEASE(mcbpool_, req);
        RELEASE(mcbpool_, pr.rep_);

        return e;
      }

      RELEASE(mcbpool_, req);

      return ERR_TIMEOUT;
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t munlink(mid_t m) {
      if (cid_ == CID_NONE)
        return ERR_REGISTERED;

      if (!conn_->open())
        return ERR_CONNECTION;

      mcb_t* req = mcbpool_.outgoing_get(MD_SYS_MUNLINK2_REQUEST, cid_, CID_SYS, InterlockedIncrement(next_seq_), true);

      req->len_   = sizeof m;
      req->u.mid_ = m;

      pending_req_t pr(req);

      prs_.put(pr);

      if (!blocking_write(req))
        return ERR_CONNECTION;

      if (pr.wait(MB_DEFAULT_TIMEOUT_MSECS) || !prs_.get(req->seq_)) {
        FUSION_ASSERT(pr.rep_, "have reply");
        FUSION_ASSERT(pr.rep_->mid_ == MD_SYS_STATUS, "expected mid or ACK");
        FUSION_ASSERT(pr.rep_->src_ == CID_SYS, "reply from mbm");
        FUSION_ASSERT(pr.rep_->dst_ == cid_, "invalid reply cid");

        result_t e = pr.rep_->u.error_;

        RELEASE(mcbpool_, req);
        RELEASE(mcbpool_, pr.rep_);

        return e;
      }

      RELEASE(mcbpool_, req);

      return ERR_TIMEOUT;
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t mlink(const char* from, const char* to) {
      if (cid_ == CID_NONE)
        return ERR_REGISTERED;

      if (!conn_->open())
        return ERR_CONNECTION;

      if (!from || !to)
        return ERR_PARAMETER;

      mcb_t* req = mcbpool_.outgoing_get(MD_SYS_MLINK_REQUEST, cid_, CID_SYS, InterlockedIncrement(next_seq_), true);

      FUSION_ASSERT(from && to);

      mcb_mlink d;

      d.set_link(from);
      d.set_orig(to);
      d.set_soft(false);

      req->len_ = d.ByteSize();

      if (req->len_ > sizeof req->u)
        req->u.pdata_ = ::malloc(req->len_);

      if (!d.SerializeToArray(req->data(), req->len_))
        return ERR_PARAMETER;

      pending_req_t pr(req);

      prs_.put(pr);

      if (!blocking_write(req))
        return ERR_CONNECTION;

      if (pr.wait(MB_DEFAULT_TIMEOUT_MSECS) || !prs_.get(req->seq_)) {
        FUSION_ASSERT(pr.rep_, "have reply");
        FUSION_ASSERT(pr.rep_->mid_ == MD_SYS_STATUS, "expected mid or ACK");
        FUSION_ASSERT(pr.rep_->src_ == CID_SYS, "reply from mbm");
        FUSION_ASSERT(pr.rep_->dst_ == cid_, "invalid reply cid");

        result_t e = pr.rep_->u.error_;

        RELEASE(mcbpool_, req);
        RELEASE(mcbpool_, pr.rep_);

        return e;
      }

      RELEASE(mcbpool_, req);

      return ERR_TIMEOUT;
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t mlist(const char* profile, const char* mask, size_t& names_nr, char**& names) {
      if (cid_ == CID_NONE)
        return ERR_REGISTERED;

      if (!conn_->open())
        return ERR_CONNECTION;

      mcb_mlist_req d;

      if (profile)
        d.set_profile(profile);

      if (mask)
        d.set_mask(mask);

      mcb_t* req = mcbpool_.outgoing_get(MD_SYS_MLIST_REQUEST, cid_, CID_SYS, InterlockedIncrement(next_seq_), true);

      req->len_ = d.ByteSize();

      if (req->len_ > sizeof req->u)
        req->u.pdata_ = ::malloc(req->len_);

      if (!d.SerializeToArray(req->data(), req->len_))
        return ERR_PARAMETER;

      pending_req_t pr(req);

      prs_.put(pr);

      if (!blocking_write(req))
        return ERR_CONNECTION;

      if (pr.wait(MB_DEFAULT_TIMEOUT_MSECS) || !prs_.get(req->seq_)) {
        FUSION_ASSERT(pr.rep_, "have reply");
        FUSION_ASSERT(pr.rep_->mid_ == MD_SYS_MLIST_REPLY || pr.rep_->mid_ == MD_SYS_STATUS, "expected mid or STATUS");
        FUSION_ASSERT(pr.rep_->src_ == CID_SYS, "reply from mbm");
        FUSION_ASSERT(pr.rep_->dst_ == cid_, "invalid reply cid");

        result_t e;

        if (pr.rep_->mid_ == MD_SYS_MLIST_REPLY) {
          mcb_mlist_rep d1;

          d1.ParseFromArray(pr.rep_->data(), pr.rep_->len_);

          names_nr  = d1.names().size();

          {
            size_t sz = sizeof(const char*) * names_nr;

            for (size_t i = 0; i < names_nr; ++i)
              sz += d1.names(i).length() + 1;

            names = (char**)malloc(sz);

            FUSION_ASSERT(names);
          }

          size_t ofs  = sizeof(const char*) * names_nr;
          char*  buff = (char*)names;

          // fill buffer, initialize pointers
          for (size_t i = 0; i < names_nr; ++i) {
            ::strcpy(names[i] = buff + ofs, d1.names(i).c_str());
            ofs += d1.names(i).length() + 1;
          }

          e = ERR_OK;
        }
        else if (pr.rep_->mid_ == MD_SYS_STATUS)
          e = pr.rep_->u.error_;
        else
          e = ERR_UNEXPECTED;

        RELEASE(mcbpool_, req);
        RELEASE(mcbpool_, pr.rep_);

        return e;
      }

      RELEASE(mcbpool_, req);

      return ERR_TIMEOUT;
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t subscribe(mid_t mid, int flags, cmi_t cmi_mask, result_t (__stdcall *cb)(mid_t mid, size_t len, const void *data)) {
      if (cid_ == CID_NONE)
        return ERR_REGISTERED;

      if (!conn_->open())
        return ERR_CONNECTION;

      switch (mid) {  //////////////////////////////////////
      case  MD_SYS_STOP_REQUEST:
      case  MD_SYS_TERMINATE_REQUEST:
      case  MD_SYS_NOTIFY_OPEN:
      case  MD_SYS_NOTIFY_SUBSCRIBE:
      case  MD_SYS_NOTIFY_CONFIGURE:
        if (scb_.get(mid))
          return ERR_SUBSCRIBED;

        scb_.add(mid, cmi_mask, cb);

        return ERR_OK;
      } ////////////////////////////////////////////////////

      unsigned oflags = omsgs_.oflags(mid);

      if (oflags & O_WRONLY)
        return ERR_WRITEONLY;

      //{
      //  rlock_t lock(scb_lock_);

      //  if (scb_.get(mid))
      //    return ERR_SUBSCRIBED;
      //}

      mcb_subscribe d;

      d.set_mid(mid);
      d.set_flags(flags);

      mcb_t* req = mcbpool_.outgoing_get(MD_SYS_SUBSCRIBE_REQUEST, cid_, CID_SYS, InterlockedIncrement(next_seq_), true);

      req->len_ = d.ByteSize();

      if (req->len_ > sizeof req->u)
        req->u.pdata_ = ::malloc(req->len_);

      if (!d.SerializeToArray(req->data(), req->len_))
        return ERR_PARAMETER;

      subscribe_pending_req_t pr(req, this, mid, flags, cmi_mask, cb);

      prs_.put(pr);

      if (!blocking_write(req))
        return ERR_CONNECTION;

      if (pr.wait(MB_DEFAULT_TIMEOUT_MSECS) || !prs_.get(req->seq_)) {
        FUSION_ASSERT(pr.rep_, "have reply");
        FUSION_ASSERT(pr.rep_->mid_ == MD_SYS_STATUS, "expected mid or ACK");
        FUSION_ASSERT(pr.rep_->src_ == CID_SYS, "reply from mbm");
        FUSION_ASSERT(pr.rep_->dst_ == cid_, "invalid reply cid");

        result_t e = pr.rep_->u.error_;

        RELEASE(mcbpool_, req);
        RELEASE(mcbpool_, pr.rep_);

        return e;
      }

      RELEASE(mcbpool_, req);

      return ERR_TIMEOUT;
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t unsubscribe(mid_t mid) {
      if (cid_ == CID_NONE)
        return ERR_REGISTERED;

      if (!conn_->open())
        return ERR_CONNECTION;

      switch (mid) {  //////////////////////////////////////
      case  MD_SYS_STOP_REQUEST:
      case  MD_SYS_TERMINATE_REQUEST:
      case  MD_SYS_NOTIFY_OPEN:
      case  MD_SYS_NOTIFY_SUBSCRIBE:
      case  MD_SYS_NOTIFY_CONFIGURE:
        if (!scb_.del(mid))
          return ERR_SUBSCRIBED;

        return ERR_OK;
      } ////////////////////////////////////////////////////

      //{
      //  rlock_t lock(scb_lock_);

      //  if (!scb_.get(mid))
      //    return ERR_SUBSCRIBED;
      //}

      mcb_t* req = mcbpool_.outgoing_get(MD_SYS_UNSUBSCRIBE_REQUEST, cid_, CID_SYS, InterlockedIncrement(next_seq_), true);

      req->len_   = sizeof mid;
      req->u.mid_ = mid;

      unsubscribe_pending_req_t pr(req, this, mid);

      prs_.put(pr);

      if (!blocking_write(req)) {
        RELEASE(mcbpool_, req);

        return ERR_CONNECTION;
      }

      if (pr.wait(MB_DEFAULT_TIMEOUT_MSECS) || !prs_.get(req->seq_)) {
        FUSION_ASSERT(pr.rep_, "have reply");
        FUSION_ASSERT(pr.rep_->mid_ == MD_SYS_STATUS, "expected mid or ACK");
        FUSION_ASSERT(pr.rep_->src_ == CID_SYS, "reply from mbm");
        FUSION_ASSERT(pr.rep_->dst_ == cid_, "invalid reply cid");

        result_t e = pr.rep_->u.error_;

        RELEASE(mcbpool_, req);
        RELEASE(mcbpool_, pr.rep_);

        return e;
      }

      RELEASE(mcbpool_, req);

      return ERR_TIMEOUT;
    }

    ////////////////////////////////////////////////////////////////////////////
    // ** worker thread ** /////////////////////////////////////////////////////
    unsigned run() {
      HANDLE	hv[] = { stop_event_, toq_.wait_handle(), conn_->wait_handle(), udp_event_, worker_ };
      mcb_t* mcb;
      bool stop = false;

      ::SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

      while (!stop || !pq_.empty()) {
        while (1) {
          DWORD index;

          if (pq_.queue_.size() < MB_PENDING_NET_MESSAGES_THRESHOLD)
            index = ::WaitForMultipleObjectsEx(FUSION_ARRAY_SIZE(hv), hv, FALSE, pq_.empty() ? INFINITE : 0, TRUE);
          else
            index = WAIT_TIMEOUT;

          switch (index) {
          // no activity: !pq_.empty() && timeout 0
          case WAIT_TIMEOUT:
            FUSION_ASSERT(!pq_.empty());

            goto do_dispatch;

          // stop event
          case WAIT_OBJECT_0 + 0:
            conn_->shutdown();
            stop = true;

            if (pq_.empty())
              goto do_exit;

            break;

          // timeout
          case WAIT_OBJECT_0 + 1:
            toq_.get(mcb);
            mcb->expired_ = true;
            pq_.put(mcb);

            break;

          // connection: read/write/disconnect
          case WAIT_OBJECT_0 + 2:
            {
              WSANETWORKEVENTS nevts;

              //FUSION_INFO("WSAEnumNetworkEvents=>%x %x", ((tcp_conn_t*)conn_)->socket_, conn_->wait_handle());

              if (::WSAEnumNetworkEvents(((tcp_conn_t*)conn_)->socket_, conn_->wait_handle(), &nevts)) {
                FUSION_ERROR("WSAEnumNetworkEvents=%d", ::WSAGetLastError());

                stop = true;

                if (pq_.empty())
                  goto do_exit;

                break;
              }

              if (nevts.lNetworkEvents & FD_CLOSE) {
                FUSION_ERROR("FD_CLOSE=%d", nevts.iErrorCode[FD_CLOSE_BIT]);

                if (pq_.empty())
                  goto do_exit;

                break;
              }

              if (nevts.lNetworkEvents & FD_READ) {
                FUSION_DEBUG("FD_READ=%d", nevts.iErrorCode[FD_READ_BIT]);

                bool    first = true;
                bool    rc;
//              mcb_t*  mcb;

                do {
                  rc = conn_->read_mcb(first, &mcb);
                  first = false;

                  if (mcb) {
                    FUSION_DEBUG("mid=%s src=%d dst=%d seq=%d prio=%d org=%lld req_seq=%d request=%d", enumstr((_md_sys_t)mcb->mid_), mcb->src_, mcb->dst_, mcb->seq_, mcb->prio_, mcb->org_, mcb->req_seq_, mcb->request_);

                    pq_.put(mcb);
                  }
                } while (rc);
              }

              if (nevts.lNetworkEvents & FD_WRITE) {
                FUSION_DEBUG("FD_WRITE=%d", nevts.iErrorCode[FD_WRITE_BIT]);

                conn_->resume_write();
              }
            }

            break;

          case WAIT_OBJECT_0 + 3: {
              WSANETWORKEVENTS nevts;

              if (::WSAEnumNetworkEvents(udp_socket_, udp_event_, &nevts)) {
                FUSION_DEBUG("WSAEnumNetworkEvents=%d", ::WSAGetLastError());

                stop = true;

                if (pq_.empty())
                  goto do_exit;

                break;
              }

              mcb = 0;

              if ((nevts.lNetworkEvents & FD_READ) && udp_read(mcb) && mcb) {
                FUSION_DEBUG("mid=%s src=%d dst=%d seq=%d prio=%d org=%lld req_seq=%d request=%d", enumstr((_md_sys_t)mcb->mid_), mcb->src_, mcb->dst_, mcb->seq_, mcb->prio_, mcb->org_, mcb->req_seq_, mcb->request_);

                pq_.put(mcb);
              }
              else {
                FUSION_DEBUG("udp receive failed");

                stop = true;

                if (pq_.empty())
                  goto do_exit;
              }
            }

            break;

          case WAIT_IO_COMPLETION:
            continue;

          case WAIT_OBJECT_0    + 4:
          case WAIT_ABANDONED_0 + 0:
          case WAIT_ABANDONED_0 + 1:
          case WAIT_ABANDONED_0 + 2:
          case WAIT_ABANDONED_0 + 3:
          case WAIT_FAILED:
          default:
            goto do_exit;
          }
        }

do_dispatch:
        if (pq_.get(mcb))
          dispatch(mcb);
      }

do_exit:
      cid_ = CID_NONE;
      conn_->close();

      return 0;
    }

    // dispatch incoming event or timeout //////////////////////////////////////
    void dispatch(mcb_t* mcb) {
      FUSION_ASSERT(mcb);
      FUSION_DEBUG("mid=%s src=%d dst=%d seq=%d prio=%d org=%lld req_seq=%d request=%d", enumstr((_md_sys_t)mcb->mid_), mcb->src_, mcb->dst_, mcb->seq_, mcb->prio_, mcb->org_, mcb->req_seq_, mcb->request_);

      if (mcb->expired_) {
        // expired pending request?
        if (pending_req_t* pr = prs_.get(mcb->seq_)) {
          FUSION_ASSERT(pr->req_ == mcb);
          FUSION_ASSERT(!pr->rep_);

          REFERENCE(mcb);
          ::SetEvent(pr->event_); // release pending request
        }
        else
          FUSION_WARN("Timed-out with nothing pending: mid=%s src=%d dst=%d seq=%d prio=%d org=%lld req_seq=%d request_=%d", enumstr((_md_sys_t)mcb->mid_), mcb->src_, mcb->dst_, mcb->seq_, mcb->prio_, mcb->org_, mcb->req_seq_, mcb->request_);
      }
      // incoming mcb
      else {
        // reply to pending request?
        if (pending_req_t* pr = prs_.get(mcb)) {
          FUSION_ASSERT(pr->req_ != mcb);

          REFERENCE(mcb);
          pr->rep_ = mcb;
          pr->completion();
          ::SetEvent(pr->event_); // release pending request
        }
        else if (mcb->req_seq_)
          FUSION_WARN("No pending request for: mid=%s src=%d dst=%d seq=%d prio=%d org=%lld req_seq=%d request=%d", enumstr((_md_sys_t)mcb->mid_), mcb->src_, mcb->dst_, mcb->seq_, mcb->prio_, mcb->org_, mcb->req_seq_, mcb->request_);
        // subscribe notification?
        else {
          cb_dcr_t* cd  = 0;
          current_mcb__ = mcb;

          {
            wlock_t lock(scb_lock_);
            cd = scb_.get(mcb->mid_);
          }

          if (cd) {
            if (!cd->cb_) {
              FUSION_DEBUG("null callback");

              omh_unexpected(mcb);
            }
            else {
              if (cd->mask_ & CM_MANUAL) {
                // enqueue for manual delivery
                wlock_t lock(mq_lock_);

                REFERENCE(mcb);
                mq_.push_back(mcb);
                ::SetEvent(mq_event_);
              }

              current_mcb__ = mcb;

              // for each registered delivery method that matches
              for (cbm_map_t::const_iterator I = cbms_.cbegin(); I != cbms_.cend(); ++I) {
                if (cd->mask_ & I->first)
                  try {
                    if (I->second.first(cd->cb_, I->second.second, mcb->mid_, mcb->len_, mcb->data()) != ERR_OK)
                      break;
                  }
                  catch (...)
                  {
                  /** pass exceptions ??? **/
                  }
              }

              current_mcb__ = 0;
            }
          }
          // system message
          else if (_mcb_callback_t cb = sys_handlers_.get(mcb->mid_))
            (this->*cb)(mcb);
          else {
            FUSION_DEBUG("not subscribed");

            omh_unexpected(mcb);
          }
        }
      }

      RELEASE(mcbpool_, mcb);
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t reply(mid_t m, size_t len, const void* data) {
      if (!current_mcb__)
        return ERR_CONTEXT; //@@ only from call back

      if (!current_mcb__->request_)
        return ERR_CONTEXT; //@@ not a requet

      if (reply_sent__)
        return ERR_CONTEXT;

      reply_sent__ = true;

      mcb_t* rep = mcbpool_.outgoing_get(m, cid_, current_mcb__->src_, InterlockedIncrement(next_seq_), false, current_mcb__->seq_);

      rep->data(data, len);

      return write(rep) ? ERR_OK : ERR_CONNECTION;
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t manual_dispatch(size_t timeout_msecs, bool all) {
      if (cid_ == CID_NONE)
        return ERR_REGISTERED;

      result_t  e = ERR_OK;
      mcb_t*    mcb = 0;

      {
        wlock_t lock(mq_lock_);

        if (!mq_.empty()) {
          mcb = mq_.front();
          mq_.pop_front();
        }
      }

      if (!mcb)
        if (timeout_msecs) {
          ::HANDLE wha[] = { mq_event_, worker_ };
          FILETIME t;
          ULARGE_INTEGER now, end;

          ::GetSystemTimeAsFileTime(&t);
          now.LowPart   = t.dwLowDateTime;
          now.HighPart  = t.dwHighDateTime;

          end = now;
          end.QuadPart += timeout_msecs * 10000LL;

retry:    if (end.QuadPart > now.QuadPart) {
            size_t to = (size_t)((end.QuadPart - now.QuadPart) / 10000LL);

            switch (::WaitForMultipleObjectsEx(FUSION_ARRAY_SIZE(wha), wha, FALSE, to, TRUE)) {
            case WAIT_OBJECT_0:
              break;

            case WAIT_TIMEOUT:
              return ERR_OK;

            case WAIT_IO_COMPLETION:
              ::GetSystemTimeAsFileTime(&t);
              now.LowPart   = t.dwLowDateTime;
              now.HighPart  = t.dwHighDateTime;
              goto retry;

            case WAIT_OBJECT_0 + 1:
              return ERR_REGISTERED;

            default:
              return ERR_UNEXPECTED;
            }
          }
        }

      do {
        if (!mcb) {
          wlock_t lock(mq_lock_);

          if (mq_.empty())
            return ERR_OK;

          mcb = mq_.front();
          mq_.pop_front();
        }

        FUSION_ASSERT(mcb);

        cb_dcr_t cd = { 0, 0 };

        {
          wlock_t lock(scb_lock_);

          if (cb_dcr_t* _cd = scb_.get(mcb->mid_))
            cd = *_cd;
        }

        if (cd.cb_) {
          current_mcb__ = mcb;
          reply_sent__  = false;

          try {
            e = cd.cb_(mcb->mid_, mcb->len_, mcb->data());
          }
          catch (...) {
            current_mcb__ = 0;
            e = ERR_UNEXPECTED;

            throw;
          }

          current_mcb__ = 0;
        }
        else {
          FUSION_DEBUG("null handler");

          e = ERR_UNEXPECTED;
        }

        // need reply?
        if (!reply_sent__ && e != ERR_OK && mcb->request_) {
          mcb_t* rep = mcbpool_.outgoing_get(MD_SYS_STATUS, cid_, mcb->src_, InterlockedIncrement(next_seq_), false, mcb->seq_);

          rep->data(&e, sizeof e);

          write(rep);
        }

        RELEASE(mcbpool_, mcb);

        mcb = 0;
      } while (e == ERR_OK && all);

      return e;
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t _post(mid_t m, cid_t c, size_t len, const void* data) {
      if (cid_ == CID_NONE)
        return ERR_REGISTERED;

      if (!conn_->open())
        return ERR_CONNECTION;

      mtype_t mtype = MT_EVENT;

      if (m > MD_SYS_LAST_) {
        if (!omsgs_.exists(m))
          return ERR_OPEN;

        size_t size = omsgs_.size(m);

        if (size != -1 && size != len)
          return ERR_MESSAGE_SIZE;

        unsigned oflags = omsgs_.oflags(m);

        if (oflags & O_RDONLY)
          return ERR_READONLY;

        mtype = omsgs_.mtype(m);
      }

      mcb_t* req = mcbpool_.outgoing_get(m, cid_, c, InterlockedIncrement(next_seq_));

      req->len_  = len;

      if (len > sizeof req->u) {
        req->u.pdata_ = ::malloc(len);

        FUSION_ASSERT(req->u.pdata_);

        ::memcpy(req->u.pdata_, data, len);
      }
      else
        ::memcpy(&req->u, data, len);

      bool rc;

      switch (mtype & MT_TYPE_MASK) {
      case MT_EVENT: rc = write(req); break;
      case MT_DATA:  rc = udp_write(req); break;
      default: rc = false; break;
      }

      RELEASE(mcbpool_, req);

      return rc ? ERR_OK : ERR_CONNECTION;
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    result_t _send(mid_t m, cid_t dst, size_t len, const void* data, mid_t& rep_mid, size_t& rep_len, const void*& rep_data, unsigned timeout) {
      // verify
      if (!CID_IS_CLIENT(dst))
        return ERR_INVALID_DESTINATION;

//    FUSION_ENSURE(FUSION_IMPLIES(((CID_CLIENT & dst) & ~CID_NOSELF) == cid_, return ERR_INVALID_DESTINATION), "self send");

      // system messages
      //switch (m) {
      //case MD_SYS_ECHO_REQUEST:
      //case MD_SYS_ECHO_REPLY:
      //}

      // user messages
      if (m > MD_SYS_LAST_) {
        if (!omsgs_.exists(m))
          return ERR_OPEN;

        size_t size = omsgs_.size(m);

        if (size != -1 && size != len)
          return ERR_MESSAGE_SIZE;

        unsigned oflags = omsgs_.oflags(m);

        if (oflags & O_RDONLY)
          return ERR_READONLY;

        mtype_t mtype = omsgs_.mtype(m);

        if ((mtype & MT_TYPE_MASK) != MT_EVENT)
          return ERR_MESSAGE_TYPE;
      }

      seq_t sq    = InterlockedIncrement(next_seq_);
      mcb_t* req  = mcbpool_.outgoing_get(m, cid_, dst, sq, true, 0, timeout);

      req->len_ = len;

      if (req->len_ > sizeof req->u) {
        req->u.pdata_ = ::malloc(req->len_);

        FUSION_ASSERT(req->u.pdata_);

        ::memcpy(req->u.pdata_, data, len);
      }
      else
        ::memcpy(&req->u, data, len);

      // timeout
      if (req->timeout_) {
        REFERENCE(req);
        toq_.put(req->timeout_, req);
      }

      // pending
      if (req->request_) {
        pending_req_t pr(req);
        ::HANDLE wha[] = { pr.event_, worker_ };

        prs_.put(pr);

        // send
        if (!blocking_write(req))
          return ERR_CONNECTION;

retry:  switch (::WaitForMultipleObjectsEx(FUSION_ARRAY_SIZE(wha), wha, FALSE, INFINITE, TRUE)) {
        case WAIT_OBJECT_0:
          if (req->expired_) {
            FUSION_ASSERT(req->timeout_);

            RELEASE(mcbpool_, req);

            return ERR_TIMEOUT;
          }

          if (req->timeout_) {
            RELEASE(mcbpool_, req);
            toq_.del(*req);
          }

          FUSION_ASSERT(pr.rep_);
          FUSION_ASSERT(req == pr.req_);
          FUSION_ASSERT(req->request_, "it was a request");
          FUSION_ASSERT(req->seq_ == pr.rep_->req_seq_, "... and got reply for the right request");

          rep_mid = pr.rep_->mid_;

          if (pr.rep_->mid_ != MD_SYS_STATUS) {
            if (rep_len = pr.rep_->len_) { // @@@@@@@@@@@@@
              rep_data = ::malloc(rep_len);

              FUSION_ASSERT(rep_data);

              ::memcpy((void*)rep_data, pr.rep_->data(), rep_len);
            }
            else
              rep_data = 0;

            RELEASE(mcbpool_, pr.rep_);

            return ERR_OK;
          }
          else {
            result_t e = pr.rep_->u.error_;

            RELEASE(mcbpool_, pr.rep_);

            return e;
          }

          RELEASE(mcbpool_, pr.rep_);

          return ERR_UNEXPECTED;

        case WAIT_OBJECT_0 + 1:
        case WAIT_TIMEOUT:
          return ERR_UNEXPECTED;

        case WAIT_IO_COMPLETION:
          goto retry;

        case WAIT_ABANDONED_0:
        case WAIT_ABANDONED_0 + 1:
        case WAIT_FAILED:
        default:
          return ERR_WIN32;
        }
      }
      // send
      else if (!blocking_write(req))
        return ERR_CONNECTION;

      return ERR_UNEXPECTED;
    }

    // ** non worker thread ** /////////////////////////////////////////////////
    static unsigned __stdcall run__(void* arg) {
      ::SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
      ::SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

      return (static_cast<internal_client_t*>(arg))->run();
    }

    void set_on_connect_handler(void (FUSION_CALLING_CONVENTION *callback)()) { /*TODO*/ }
    void set_on_disconnect_handler(void (FUSION_CALLING_CONVENTION *callback)()) { /*TODO*/ }
  };

  //////////////////////////////////////////////////////////////////////////////
  void reg_pending_req_t::completion() {
    FUSION_ASSERT(req_);
    FUSION_ASSERT(rep_);

    if (rep_->u.error_ == ERR_OK)
      ic_->cid_ = rep_->dst_;
  }

  void unreg_pending_req_t::completion() {
    FUSION_ASSERT(req_);
    FUSION_ASSERT(rep_);

    if (rep_->u.error_ == ERR_OK) {
      ic_->cid_ = CID_NONE;
#if MB_UNREGISTER_CLOSES_CONNECTION > 0
//    ic_->conn_->close();
//    ::CloseHandle(ic_->udp_event_);        //FUSION_INFO("close udp_event_=%d",  ic_->udp_event_);
      ::closesocket(ic_->udp_socket_);       //FUSION_INFO("close udp_socket_=%d", ic_->udp_socket_);
#endif
    }
    else
      FUSION_WARN("%s", result_to_str(rep_->u.error_));
  }

  void mopen_pending_req_t::completion() {
    FUSION_ASSERT(req_);
    FUSION_ASSERT(rep_);

    if (rep_->mid_ == MD_SYS_MOPEN_REPLY) {
      mcb_mopen_rep d;

      d.ParseFromArray(rep_->data(), rep_->len_);

      m_ = d.mid();

      if (oflags_ & O_CREATE) {
        FUSION_ASSERT(!d.has_size());
        FUSION_ASSERT(!d.has_type());
      }
      else {
        FUSION_ASSERT(d.has_size());
        FUSION_ASSERT(d.has_type());

        type_ = d.type();
        size_ = d.size();
      }

      if (d.has_already_opened()) {
        FUSION_ASSERT(d.already_opened());

        rep_->fini();		// dispose old payload

        rep_->mid_			= MD_SYS_STATUS;
        rep_->len_			= sizeof rep_->u.error_;
        rep_->u.error_	= ERR_OPEN;
      }
      else
        ic_->omsgs_.add(m_, size_, oflags_, type_);
    }
    else
      m_ = MD_SYS_NONE;
  }

  void mclose_pending_req_t::completion() {
    FUSION_ASSERT(req_);
    FUSION_ASSERT(rep_);

    if (rep_->mid_ == MD_SYS_STATUS && rep_->u.error_ == ERR_OK) {
      ic_->omsgs_.del(mid_);

      wlock_t lock(ic_->scb_lock_);

      ic_->scb_.del(mid_);
    }
  }

  void subscribe_pending_req_t::completion() {
    FUSION_ASSERT(req_);
    FUSION_ASSERT(rep_);

    if (rep_->u.error_ == ERR_OK) {
      wlock_t lock(ic_->scb_lock_);

      ic_->scb_.add(m_, cmi_, cb_);
    }
  }

  void unsubscribe_pending_req_t::completion() {
    FUSION_ASSERT(req_);
    FUSION_ASSERT(rep_);

    if (rep_->u.error_ == ERR_OK) {
      wlock_t lock(ic_->scb_lock_);

      ic_->scb_.del(mid_);
    }
  }

#if DEBUG_HEAP > 0
  void release(mcb_t* mcb) {
    RELEASE(lost_and_found__, mcb);
  }
#endif

  //////////////////////////////////////////////////////////////////////////////
  // interface

  //////////////////////////////////////////////////////////////////////////////
  client_t::client_t(const char* name) : pimp_(*new internal_client_t(name)) {
    FUSION_DEBUG("this=%p\n", this); //@@@
  }

  //////////////////////////////////////////////////////////////////////////////
  client_t::~client_t() {
    FUSION_DEBUG("this=%p\n", this); //@@@

    delete &pimp_;
  }

  //////////////////////////////////////////////////////////////////////////////
  cid_t client_t::id() const {
    rlock_t lock(pimp_.lock_);

    return pimp_.cid_;
  }

  //////////////////////////////////////////////////////////////////////////////
  const char* client_t::name() const {
    FUSION_DEBUG("this=%p\n", this); //@@@

    return pimp_.name_;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::reg_callback_method(callback_method_t cb, void* cookie, cmi_t& id) {
    wlock_t lock(pimp_.lock_);

    id = pimp_.cbms_.add(std::make_pair(cb, cookie));

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::unreg_callback_method(cmi_t id) {
    wlock_t lock(pimp_.lock_);

    return pimp_.cbms_.del(id) ? ERR_OK : ERR_PARAMETER;
  }

  //////////////////////////////////////////////////////////////////////////////
  const mcb_t* client_t::get_mcb() {
    return current_mcb__;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::reg(const char* con, const char* profile) {
    FUSION_DEBUG("this=%p con=%s profile=%s\n", this, con, profile);

    // resolve uid, gid, roles...
    return pimp_.reg(con, 1, &profile);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::reg(const char* con, size_t profiles_nr, const char* profile, ...) {
    FUSION_DEBUG("this=%p con=%s profiles_nr=%d profile=%s ...\n", this, con, profiles_nr, profile);

    // resolve uid, gid, roles...
    return pimp_.reg(con, profiles_nr, &profile);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::reg(const char* con, size_t profiles_nr, const char** profilevec) {
    FUSION_DEBUG("this=%p con=%s profiles_nr=%d ...\n", this, con, profiles_nr);

    // resolve uid, gid, roles...
    return pimp_.reg(con, profiles_nr, profilevec);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::unreg() {
    FUSION_DEBUG("this=%p\n", this); //@@@

    return pimp_.unreg();
  }

  //////////////////////////////////////////////////////////////////////////////
  bool client_t::registered() {
    return pimp_.cid_ != CID_NONE;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::dispatch(bool all) {
    FUSION_DEBUG("this=%p all=%d\n", this, all); //@@@

    return pimp_.manual_dispatch(0, all);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::dispatch(size_t timeout_msecs, bool all) {
    FUSION_DEBUG("this=%p timeout_msecs=%d all=%d\n", this, timeout_msecs, all); //@@@

    return pimp_.manual_dispatch(timeout_msecs, all);
  }

  // clients ///////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::cquery(size_t& size, size_t& nr, const char**& names) {
    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::cquery( size_t& len, const char*& names, const char* sep) {
    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  cid_t client_t::id(const char* name) {
    return ERR_IMPLEMENTED;
  }

  // messages //////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::mcreate(const char* msg, oflags_t flags, mtype_t mode, mid_t& mid, size_t size) {
    FUSION_DEBUG("this=%p msg=%s flags=%d mode=%d size=%d\n", this, msg, flags, mode, size); //@@@

    return pimp_.mcreate(msg, flags, mode, mid, size, 0, 0);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::mcreate(const char* msg, oflags_t flags, mtype_t mode, mid_t& mid, size_t size, size_t len, const void* data) {
    FUSION_DEBUG("this=%p msg=%s flags=%d mode=%d size=%d\n", this, msg, flags, mode, size); //@@@

    return pimp_.mcreate(msg, flags, mode, mid, size, len, data);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::mopen(const char* msg, oflags_t flags, mtype_t& mode, mid_t& mid, size_t& size) {
    FUSION_DEBUG("this=%p msg=%s flags=%d\n", this, msg, flags); //@@@

    return pimp_.mopen(msg, flags, mode, mid, size);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::mclose(mid_t mid) {
    FUSION_DEBUG("this=%p mid=%d\n", this, mid); //@@@

    return pimp_.mclose(mid);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::munlink(const char* msg) {
    FUSION_DEBUG("this=%p name=%s\n", this, msg); //@@@

    return pimp_.munlink(msg);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::munlink(mid_t m) {
    return pimp_.munlink(m);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::mlink(const char* from, const char* to) {
    return pimp_.mlink(from, to);
  }

  // messages: query
  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::mlist(const char* profile, const char* mask, size_t& names_nr, char**& names) {
    return pimp_.mlist(profile, mask, names_nr, names);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::mstat(const char* msg, int& data) {
    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::mstat(mid_t m, int& data) {
    return ERR_IMPLEMENTED;
  }

  // messages: attributes
  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::mattr_read(mid_t m, const char* key, size_t& len, char* value) {
    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::mattr_write(mid_t m, const char* key, char* value) {
    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::mattrs_read(mid_t m, size_t& size, size_t& nr, char**& pairs) {
    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::mattrs_write(mid_t m, size_t& nr, char** pairs) {
    return ERR_IMPLEMENTED;
  }

  // subscription //////////////////////////////////////////////////////////////
  result_t client_t::subscribe(mid_t mid, int flags, cmi_t cmi_mask, callback_t cb) {
    return pimp_.subscribe(mid, flags, cmi_mask, cb);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::unsubscribe(mid_t mid) {
    return pimp_.unsubscribe(mid);
  }

  // post/send /////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::publish(mid_t m, size_t len, const void* data) {
    FUSION_DEBUG("this=%p mid=%d len=%d\n", this, m, len); //@@@

    return pimp_._post(m, CID_PUB, len, data);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::post(mid_t m, cid_t c, size_t len, const void* data) {
    if (c == nf::CID_NONE)
      return ERR_SUBSCRIBERS;

    return pimp_._post(m, c, len, data);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::send(mid_t m, cid_t dst, size_t len, const void* data, unsigned timeout) {
    if (dst == nf::CID_NONE)
      return ERR_SUBSCRIBERS;

    size_t rep_len = 0;
    const void* rep_data = 0;
    mid_t rep_m;

    return pimp_._send(m, dst, len, data, rep_m, rep_len, rep_data, timeout);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::request(mid_t m, cid_t dst, size_t len, const void* data, mid_t& rep_m, size_t& rep_len, const void*& rep_data, unsigned timeout) {
    if (dst == nf::CID_NONE)
      return ERR_SUBSCRIBERS;

    return pimp_._send(m, dst, len, data, rep_m, rep_len, rep_data, timeout);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::reply(mid_t m, size_t len, const void* data) {
    return pimp_.reply(m, len, data);
  }

  //////////////////////////////////////////////////////////////////////////////
  client_t* FUSION_CALLING_CONVENTION _create_client(const char* name) {
    return new client_t(name);
  }

  void FUSION_CALLING_CONVENTION _delete_client(client_t* client) { ////////////
    delete client;
  }

  // sysinfo ///////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION client_t::sysinfo(const mcb_sysinfo_request& req, mcb_sysinfo_reply& rep, unsigned timeout) {
    size_t      req_len   = req.ByteSize();
    void*       req_data  = alloca(req_len);
    size_t      rep_len;
    const void* rep_data;

    FUSION_ASSERT(req_data);

    if (!req.SerializeToArray(req_data, req_len))
      return ERR_PARAMETER;

    mid_t rep_mid;

    result_t rc = request(MD_SYS_SYSINFO_REQUEST, CID_SYS, req_len, &req, rep_mid, rep_len, rep_data, timeout);

    if (rc != ERR_OK)
      return rc;

    FUSION_ASSERT(rep_mid == MD_SYS_SYSINFO_REPLY);

    if (!rep.ParseFromArray(rep_data, rep_len))
      return ERR_PARAMETER;

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  const version_t& FUSION_CALLING_CONVENTION version() {
    return nf::ver;
  }

  #define MS_TO_HNS 1000

  result_t FUSION_CALLING_CONVENTION client_t::get_timestamp(FUSION_OUT nf::msecs_t* msg_timestamp) {
    if (const mcb_t* mcb = get_mcb()) {
      if (msg_timestamp)
        *msg_timestamp = mcb->org_ * MS_TO_HNS;

      return ERR_OK;
    }

    return ERR_UNEXPECTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  const char* FUSION_CALLING_CONVENTION result_to_str(result_t rc) {
    return enumstr(rc);
  }

  void FUSION_CALLING_CONVENTION client_t::set_on_connect_handler(void (FUSION_CALLING_CONVENTION *callback)()) {
    pimp_.set_on_connect_handler(callback);
  }

  void FUSION_CALLING_CONVENTION client_t::set_on_disconnect_handler(void (FUSION_CALLING_CONVENTION *callback)()) {
    pimp_.set_on_disconnect_handler(callback);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t client_t::read_int    (const char*, int&)                  { return ERR_IMPLEMENTED; }
  result_t client_t::read_double (const char*, double&)               { return ERR_IMPLEMENTED; }
  result_t client_t::read_bool   (const char*, bool&)                 { return ERR_IMPLEMENTED; }
  result_t client_t::read_string (const char*, size_t&, char*)        { return ERR_IMPLEMENTED; }
  result_t client_t::read_blob   (const char*, size_t&, void*)        { return ERR_IMPLEMENTED; }

  result_t client_t::write_int   (const char*, int)                   { return ERR_IMPLEMENTED; }
  result_t client_t::write_double(const char*, double)                { return ERR_IMPLEMENTED; }
  result_t client_t::write_bool  (const char*, bool)                  { return ERR_IMPLEMENTED; }
  result_t client_t::write_string(const char*, size_t, const char*)   { return ERR_IMPLEMENTED; }
  result_t client_t::write_blob  (const char*, size_t, const void*)   { return ERR_IMPLEMENTED; }

  result_t client_t::send(mcb_t* mcb)                                 { return ERR_IMPLEMENTED; }
  result_t client_t::mmove(const char* msg)                           { return ERR_IMPLEMENTED; }

  //////////////////////////////////////////////////////////////////////////////
  namespace {
    struct __module_init__ {
      __module_init__() {
#if DEBUG_HEAP > 0
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF|_CRTDBG_LEAK_CHECK_DF|_CRTDBG_CHECK_CRT_DF);
        _CrtSetReportMode(_CRT_WARN,    _CRTDBG_MODE_FILE);
        _CrtSetReportMode(_CRT_ERROR,   _CRTDBG_MODE_FILE);
        _CrtSetReportMode(_CRT_ASSERT,  _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN,    _CRTDBG_FILE_STDERR);
        _CrtSetReportFile(_CRT_ERROR,   _CRTDBG_FILE_STDERR);
        _CrtSetReportFile(_CRT_ASSERT,  _CRTDBG_FILE_STDERR);
#endif
      };

      ~__module_init__() {
#if DEBUG_HEAP > 0
        google::protobuf::ShutdownProtobufLibrary();
#endif
      };
    } __module_init__;
  }
}
