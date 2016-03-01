/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include "include/configure.h"
#include "include/nf.h"
#include "include/nf_internal.h"
#include "include/version.h"
#include "include/conn.h"
#include "include/toq.h"
#include "include/tsc.h"
#include "include/toq.h"
#include "include/idpool.h"
#include "include/md.h"
#include "mb/pq.h"
#include "mb/pr.h"
#include "include/mcbpool.h"
#include "include/enumstr.h"
#include "include/resolve.h"

#include "clients.h"
#include "subs.h"

#include "mcb.proto/mcb.pb.h"

#if	defined(WIN32)
# define NOGDI
# include <Winsock2.h>
# include <windows.h>
#	include <process.h>
# include <wincon.h>
#	pragma comment(lib, "ws2_32.lib")
#endif

#include "include/lock.h"
#include "include/tcpconn.h"

#include "md/md_internal.h"
#include "md/pcb.h"

#include <string.h>
#include <vector>

#define USE_STOP_EVENT

#ifdef  TEST_PARTIAL_WRITES
# ifdef  USE_STOP_EVENT
#  ifdef  MBM_LISTEN_TIMER_MSECS
#   define STOP_EVENT_IDX         3
#   define LISTEN_TIMER_EVENT_IDX 4
#   define FAKE_RESUME_EVENT_IDX  5
#   define FIXED_EVENTS_NR        6
#  else
#   define STOP_EVENT_IDX         3
#   define FAKE_RESUME_EVENT_IDX  4
#   define FIXED_EVENTS_NR        5
#  endif
# else
#  ifdef  MBM_LISTEN_TIMER_MSECS
#   define LISTEN_TIMER_EVENT_IDX 3
#   define FAKE_RESUME_EVENT_IDX  4
#   define FIXED_EVENTS_NR        5
#  else
#   define FAKE_RESUME_EVENT_IDX  3
#   define FIXED_EVENTS_NR        4
#  endif
# endif
#else
# ifdef  USE_STOP_EVENT
#  ifdef  MBM_LISTEN_TIMER_MSECS
#   define STOP_EVENT_IDX         3
#   define LISTEN_TIMER_EVENT_IDX 4
#   define FIXED_EVENTS_NR        5
#  else
#   define STOP_EVENT_IDX         3
#   define FIXED_EVENTS_NR        4
#  endif
# else
#  ifdef  MBM_LISTEN_TIMER_MSECS
#   define LISTEN_TIMER_EVENT_IDX 3
#   define FIXED_EVENTS_NR        4
#  else
#   define FIXED_EVENTS_NR        3
#  endif
# endif
#endif

#define MCB_MAX_PAYLOAD ((1 << sizeof(short)) - sizeof(mcb_xfer_t) - sizeof(double /*mcb_t::u*/))

const char* opt_confdir                 = "./";
size_t  opt_prealloc_mcbs               = PREALLOCATED_MCBS;
size_t  opt_max_alloc_mcbs              = 16 * PREALLOCATED_MCBS;
size_t  opt_listen_backlog              = MBM_LISTEN_BACKLOG;
size_t  opt_listen_timer                = MBM_LISTEN_TIMER_MSECS;
bool    opt_wha_shuffle                 = true;
bool    opt_check                       = true;
int     opt_dump_tags                   = 0; /* 0 - none, 1 - all, 2 - only linked */
bool    opt_dump_tags_value             = false;
bool    opt_dump_tags_mid               = false;
bool    opt_dump_tags_size              = false;
bool    opt_dump_tags_type              = false;
bool    opt_write_thru_persistent_data  = MBM_DEFAULT_FLUSH_PERSISTENT_DATA;
int     opt_verbose                     = 0;

namespace nf {
  extern version_t ver;

  //////////////////////////////////////////////////////////////////////////////
  typedef result_t (*sys_cb_t)(mcb_t*, client_t*);

  sys_cb_t handle_sys_msgs(bool, mid_t);

  struct cid_pool_t : idpool_t<10, 4, 2, -1> {
    cid_t get()         {
      cid_t cid = (cid_t)idpool_t::get();

      FUSION_ASSERT(
           cid != 0
        && cid != CID_SYS
        && MIN <= cid
        && cid < MAX
        && cid != BAD
      );

      return cid;
    }

    void put(cid_t id)  {
      idpool_t::put((cid_t)id);
    }
  };

  ULONG       host          = INADDR_ANY;
  USHORT      port          = 0;
  ::SOCKET    listen_socket = INVALID_SOCKET;
  ::SOCKET    udp_socket    = INVALID_SOCKET;
  ::WSAEVENT	listen_event;
  ::WSAEVENT	udp_event;
#ifdef  USE_STOP_EVENT
  ::WSAEVENT	stop_event;
#endif
#ifdef  MBM_LISTEN_TIMER_MSECS
  ::HANDLE	  listen_timer;
#endif
#ifdef  TEST_PARTIAL_WRITES
  ::HANDLE	  fake_write_resume_event;
#endif

  mcbpool_t   mcbpool;

  toq_t<
    mcb_t
  >           toq;                // timeout queue

  bool pq_sort(
    std::pair<mcb_t*, cid_t> a,
    std::pair<mcb_t*, cid_t> b
  ) {
    return a.first->prio_ < b.first->prio_;
  }

  pq_t<
    std::pair<
      mcb_t*,                     // request
      cid_t>,                     // source client
    pq_sort
  >           pq;                 // priority queue

  subs_t      subs;               // subscriptions
  clients_t*  clients;            // clients, except this (SYS)
  cid_pool_t  cid_pool;

  HANDLE wha[MAXIMUM_WAIT_OBJECTS];// array of wait-handles

#if (MBM_DISPLAY_MESSAGES_PERIOD > 0)
  size_t nrr  = 0;                // number of received
  size_t nrs  = 0;                // number of sent
  size_t nrd  = 0;                // number of dropped (udp)
#endif

  seq_t get_next_seq() {
    static seq_t seq__ = 0;

    // check for 0, it has special meaning
    if (++seq__)
      return seq__;

    return ++seq__;
  }

  volatile bool stop = false;
  volatile bool need_cleanup = true;

  //////////////////////////////////////////////////////////////////////////////
  BOOL WINAPI on_break(DWORD CtrlType) {
#ifdef  USE_STOP_EVENT
    ::SetEvent(stop_event);
#else
    stop = true;
#endif

    switch (CtrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      // give slack terminating main thread
      // Windows will terminate process in 5 secs anyways
      do {
        ::Sleep(0);
      } while (need_cleanup);

      break;
    }

    return TRUE;
  }

  //////////////////////////////////////////////////////////////////////////////

  static char udp_buff__[0x10000]; // 64K, Fusion max mcb + payload size

  bool udp_read(client_t*& c, mcb_t*& mcb) {
    sockaddr_in from;
    int         from_len = sizeof from;

#if USE_VECTORED_IO > 0
    mcb = mcbpool.get();

    FUSION_ASSERT(mcb);

    WSABUF  bufs[2] = {{ sizeof mcb_xfer_t, (CHAR*)mcb }, { sizeof udp_buff__, (CHAR*)udp_buff__ }};
    DWORD   bytes = 0, flags = 0;

    if (::WSARecvFrom(udp_socket, (LPWSABUF)&bufs, FUSION_ARRAY_SIZE(bufs), &bytes, &flags, (sockaddr*)&from, &from_len, 0, 0) == SOCKET_ERROR) {
      FUSION_ERROR("::WSARecvFrom=%d", ::WSAGetLastError());

      RELEASE(mcbpool, mcb);
      c = 0;

      return false;
    }

    FUSION_ASSERT(bytes >= sizeof mcb_xfer_t);
    FUSION_ASSERT(bytes < 0x10000 - sizeof mcb_xfer_t);

    if (!(c = (client_t*)clients->find_by_ap(from.sin_addr, from.sin_port))) {
      FUSION_WARN("No peer upd found. mcbs=%d/%d", mcb_t::nr__, mcbpool.size());

      RELEASE(mcbpool, mcb);
      c = 0;

      return false;
    }

    mcb->incoming_init(bytes);

    FUSION_ASSERT(mcb->len_ == (unsigned short)(bytes - sizeof mcb_xfer_t));

    if (mcb->len_ > sizeof mcb->u) {
      mcb->u.pdata_ = ::malloc(mcb->len_);

      FUSION_ASSERT(mcb->u.pdata_);

      ::memcpy(mcb->u.pdata_, udp_buff__, mcb->len_);
    }
    else
      ::memcpy(&mcb->u.data_, udp_buff__, mcb->len_);

    return true;
#else
    int len = ::recvfrom(udp_socket, udp_buff__, sizeof udp_buff__, 0, (SOCKADDR*)&from, &from_len);

    if (len == SOCKET_ERROR) {
      FUSION_ERROR("recvfrom()=%d", ::WSAGetLastError());

      return false;
    }

    FUSION_ASSERT(sizeof mcb_xfer_t <= len && len <= 0x10000);

    if (!(c = (client_t*)clients->find_by_ap(from.sin_addr, from.sin_port))) { //@@
      FUSION_ERROR("TODO");

      return false;
    }

    // extract mcb...
    mcb = mcbpool.incoming_get(len);

    FUSION_ASSERT(mcb);

    *(mcb_xfer_t*)mcb = *(mcb_xfer_t*)udp_buff__;
    ::memcpy(mcb->data(), &udp_buff__[sizeof mcb_xfer_t], len - sizeof mcb_xfer_t);

    FUSION_ASSERT(c->cid_ == mcb->src_);

    return true;
#endif
  }

  bool udp_write(client_t* c, mcb_t* mcb) {
    FUSION_ASSERT(c);
    FUSION_ASSERT(mcb);

    int         len = sizeof mcb_xfer_t + mcb->len_;
    sockaddr_in to;

    to.sin_family           = AF_INET;
    to.sin_addr.S_un.S_addr = c->addr_;
    to.sin_port             = c->port_;

#if USE_VECTORED_IO > 0
    WSABUF  bufs[2] = {{ sizeof mcb_xfer_t, (CHAR*)mcb }, { mcb->len_, (CHAR*)mcb->data() }};
    DWORD   bytes;

    if (::WSASendTo(udp_socket, (LPWSABUF)&bufs, FUSION_ARRAY_SIZE(bufs), &bytes, 0, (sockaddr*)&to, sizeof to, 0, 0) == SOCKET_ERROR) {
      if (::WSAGetLastError() == WSAEWOULDBLOCK) {
        // dropping send

#if (MBM_DISPLAY_MESSAGES_PERIOD > 0)
        ++nrd;
#endif

        return true;
      }

      FUSION_ERROR("::WSASendTo=%d", ::WSAGetLastError());

      return false;
    }

    return bytes == sizeof mcb_xfer_t + mcb->len_;
#else
    // serialize mcb
    *(mcb_xfer_t*)udp_buff__= *(mcb_xfer_t*)mcb;
    ::memcpy(&udp_buff__[sizeof mcb_xfer_t], mcb->data(), mcb->len_);

    if (len != ::sendto(udp_socket, udp_buff__, len, 0, (SOCKADDR*)&to, sizeof to)) {
      FUSION_ERROR("sendto()=%d", ::WSAGetLastError());

      return false;
    }

    return true;
#endif
}

  //////////////////////////////////////////////////////////////////////////////
  static void cleanup(client_t* c) {
    FUSION_ENSURE(c, return);

    // notify clients who have pending reqs: no more hope
    for (auto I = c->pcrs_.cbegin(), E = c->pcrs_.cend(); I != E; ++I)
      if (client_t* o = clients->get(I->first)) {
        mcb_t* mcb = mcbpool.outgoing_get(MD_SYS_STATUS, CID_SYS, I->first, get_next_seq(), false, I->second);
        result_t e = ERR_INVALID_DESTINATION;

        mcb->data(&e, sizeof e);

        o->conn_->write_mcb(mcb);

        RELEASE(mcbpool, mcb);
      }

    cid_t cid = c->cid_;

    clients->del(cid);
    subs.del(cid);
    cid_pool.put(cid);
  }

  //////////////////////////////////////////////////////////////////////////////
  static bool write(mcb_t* mcb, client_t* c, size_t output_limit = 0) {
    FUSION_ENSURE(mcb, return false);
    FUSION_ENSURE(c,   return false);
    FUSION_ENSURE(!output_limit || c->conn_->wqueue_.size() < output_limit, goto on_cleanup, "kill client=%d: output queue limit exceeded=%d/%d", c->cid_, c->conn_->wqueue_.size(), output_limit);
    FUSION_ENSURE(c->conn_->write_mcb(mcb), goto on_cleanup);

    return true;

on_cleanup:
    cleanup(c);

    return false;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_unexpected(mcb_t* req, client_t*) {
    FUSION_WARN("src=%d dst=%d mid=%s", req->src_, req->dst_, enumstr((_md_sys_t)req->mid_));

    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_echo_request(mcb_t* req, client_t* c) {
    // use request as reply...
    req->mid_     = MD_SYS_ECHO_REPLY;
    req->org_     = now_msecs();
    req->expired_ = false;
//  req->rc_      = 1;
    req->dst_     = req->src_;
    req->src_     = CID_SYS;
    req->prio_    = PRIORITY_LO;
    req->request_ = false;
    req->timeout_ = 0;
    req->req_seq_ = req->seq_;
    req->seq_     = get_next_seq();
//  req->len_     ...
//  req->u        ...

    write(req, c);

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_timesync_reply(mcb_t* rsp, client_t* c) {
    FUSION_ENSURE(rsp,                                return ERR_UNEXPECTED);
    FUSION_ENSURE(c,                                  return ERR_UNEXPECTED);
    FUSION_ENSURE(c->registered_,                     return ERR_UNEXPECTED);
    FUSION_ENSURE(c->cid_ == rsp->src_,               return ERR_UNEXPECTED);
    FUSION_ENSURE(rsp->mid_ == MD_SYS_TIMESYNC_REPLY, return ERR_UNEXPECTED);
    FUSION_ENSURE(c->conn_,                           return ERR_UNEXPECTED);

    msecs_t now       = now_msecs();
    msecs_t req_xmit  = rsp->u.msecs_;  // mbm clock at sys_timesync_request
    msecs_t rsp_xmit  = rsp->org_;      // client clock processing request

    c->conn_->clock_sync_.latency_      = (uint32_t)(now - req_xmit);
    c->conn_->clock_sync_.clock_delta_  = ((rsp_xmit - req_xmit) + (rsp_xmit - now) + 1) / 2;

    FUSION_DEBUG("cid=%d req_xmit=%lld rsp_xmit=%lld rsp_rcpt=%lld latency=%ld delta=%lld",
      c->cid_,
      req_xmit,
      rsp_xmit,
      now,
      c->conn_->clock_sync_.latency_,
      c->conn_->clock_sync_.clock_delta_
    );

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  static bool resync_clock(mcb_t* mcb) {
    FUSION_ENSURE(mcb,                  goto stop_synch );
    FUSION_ENSURE(mcb->src_ == CID_SYS, goto stop_synch );
    FUSION_ENSURE(mcb->request_,        goto stop_synch );
//  FUSION_INFO(">>>>>>>>> mid=%d rc=%d src=%d dst=%d seq=%d len=%d", mcb->mid_, mcb->rc_, mcb->src_, mcb->dst_, mcb->seq_, mcb->len_);
    FUSION_VERIFY(mcb->rc_ > 0, " mid=%s rc=%d src=%d dst=%d seq=%d len=%d", enumstr((_md_sys_t)mcb->mid_), mcb->rc_, mcb->src_, mcb->dst_, mcb->seq_, mcb->len_);

    if (client_t* c = clients->get(mcb->dst_)) {
      if (mcb->expired_) {
//      RELEASE(mcbpool, mcb);
        mcb->org_     = now_msecs();
        mcb->seq_     = get_next_seq();
        mcb->expired_ = false;
      }

      mcb->u.msecs_ = mcb->org_;
      mcb->len_     = sizeof(mcb->u.msecs_);

//    FUSION_INFO(">>>>>>>>> mid=%d rc=%d src=%d dst=%d seq=%d len=%d", mcb->mid_, mcb->rc_, mcb->src_, mcb->dst_, mcb->seq_, mcb->len_);

      if (write(mcb, c)) {
        REFERENCE(mcb);
        FUSION_VERIFY(mcb->rc_ > 0, " mid=%s rc=%d src=%d dst=%d seq=%d len=%d", enumstr((_md_sys_t)mcb->mid_), mcb->rc_, mcb->src_, mcb->dst_, mcb->seq_, mcb->len_);
        toq.put(c->conn_->clock_sync_.period_, mcb);

        return true;
      }
    }

stop_synch:
    RELEASE(mcbpool, mcb);

    return false;
  }

  static void start_sync_clock(cid_t cid) {
    mcb_t* mcb = mcbpool.outgoing_get(MD_SYS_TIMESYNC_REQUEST, CID_SYS, cid, get_next_seq(), true);

    FUSION_ENSURE(mcb, return);

    resync_clock(mcb);
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_reg_request(mcb_t* req, client_t* c) {
    FUSION_ENSURE(req,                                  return ERR_UNEXPECTED);
    FUSION_ENSURE(c,                                    return ERR_UNEXPECTED);
    FUSION_ENSURE(!c->registered_,                      return ERR_UNEXPECTED);
    FUSION_ENSURE(c->cid_ == req->src_,                 return ERR_UNEXPECTED);
    FUSION_ENSURE(req->mid_ == MD_SYS_REGISTER_REQUEST, return ERR_UNEXPECTED);

    result_t e;
    mcb_reg d;

    FUSION_ENSURE(d.ParseFromArray(req->data(), req->len_), return ERR_UNEXPECTED);

    if (d.ver_maj() == ver.mini.maj && d.ver_min() == ver.mini.min) {
      const char** profiles = (const char**)::alloca(d.profile_size() * sizeof(const char*));

      for (int i = 0; i < d.profile_size(); ++i)
        profiles[i] = d.mutable_profile(i)->c_str();

      e = md_reg_client(c->cid_, d.name().c_str(), d.profile_size(), profiles);
    }
    else
      e = ERR_VERSION;

    if (d.has_uuid()) {
      // watch for dangling connections
      if (client_t* same = clients->find_by_uuid((UUID*)d.uuid().c_str())) {
        FUSION_WARN("Killing dangling client=%d", same->cid_);

        cleanup(same);
      }
    }

    if (e == ERR_OK) {
      FUSION_INFO("cid=%d name=%s", c->cid_, d.name().c_str());

      c->profile_opts_ = md_get_default_profile(c->cid_);

      FUSION_ENSURE(c->profile_opts_, return ERR_UNEXPECTED);

      c->conn_->clock_sync_.period_ = c->profile_opts_->clock_sync_period_;

      if (d.has_uuid())
        ::memcpy(&c->uuid_, (UUID*)d.uuid().c_str(), sizeof c->uuid_);

      mcb_t* rep = mcbpool.outgoing_get(MD_SYS_REGISTER_REPLY, CID_SYS, c->cid_, get_next_seq(), false, req->seq_);

      rep->len_     = sizeof rep->u.error_;
      rep->u.error_ = ERR_OK;
      c->registered_= true;

      if (write(rep, c))
        if (c->conn_->clock_sync_.period_)
          start_sync_clock(c->cid_);

      RELEASE(mcbpool, rep);

      subs.put(MD_SYS_STOP_REQUEST, true, c->cid_);
      subs.put(MD_SYS_TERMINATE_REQUEST, true, c->cid_);

      return ERR_OK;
    }

    return e;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_unreg_request(mcb_t* req, client_t* c) {
    FUSION_ENSURE(req,                                    return ERR_UNEXPECTED);
    FUSION_ENSURE(c,                                      return ERR_UNEXPECTED);
    FUSION_ENSURE(c->cid_ == req->src_,                   return ERR_UNEXPECTED);
    FUSION_ENSURE(req->mid_ == MD_SYS_UNREGISTER_REQUEST, return ERR_UNEXPECTED);
    FUSION_ENSURE(c->registered_,                         return ERR_REGISTERED);
    FUSION_INFO("cid=%d", c->cid_);

    c->registered_ = false;

    result_t e = md_unreg_client(c->cid_);

    FUSION_ASSERT(e == ERR_OK);

    mcb_t* rep = mcbpool.outgoing_get(MD_SYS_UNREGISTER_REPLY, CID_SYS, c->cid_, get_next_seq(), false, req->seq_);

    rep->len_     = sizeof rep->u.error_;
    rep->u.error_ = ERR_OK;

    write(rep, c);

    RELEASE(mcbpool, rep);

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  static bool notify_open_pred(void*, unsigned oflags) {
    return oflags & O_NOTIFY_OPEN;
  }

  static void notify_open(void*, const md_t* md, const ccb_t* ccb) {
    FUSION_ENSURE(md,  return);
    FUSION_ENSURE(ccb, return);

    mcb_t* notify = mcbpool.outgoing_get(MD_SYS_NOTIFY_OPEN, CID_SYS, ccb->cid_, get_next_seq());
    client_t* c   = clients->get(ccb->cid_);

    FUSION_ENSURE(notify, return);
    FUSION_ENSURE(c,      goto exit);

    notify->prio_         = PRIORITY_LO;
    notify->timeout_      = 0;
    notify->request_      = 0;
    notify->req_seq_      = 0;

    notify->u.notify.mid_ = md->mid_;
    notify->u.notify.rnr_ = md->xread_  ? -1 : md->reads_;
    notify->u.notify.wnr_ = md->xwrite_ ? -1 : md->writes_;
    notify->len_          = sizeof notify->u.notify;

    write(notify, c);

exit:
    RELEASE(mcbpool, notify);
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_mopen_request(mcb_t* req, client_t* c) {
    FUSION_ENSURE(req,                                return ERR_UNEXPECTED);
    FUSION_ENSURE(c,                                  return ERR_UNEXPECTED);
    FUSION_ENSURE(c->registered_,                     return ERR_UNEXPECTED);
    FUSION_ENSURE(c->cid_ == req->src_,               return ERR_UNEXPECTED);
    FUSION_ENSURE(req->mid_ == MD_SYS_MOPEN_REQUEST,  return ERR_UNEXPECTED);

    result_t      e;
    mcb_mopen_req d;
    mtype_t       type;
    mid_t         m = 0;
    size_t        size;

    FUSION_ENSURE(d.ParseFromArray(req->data(), req->len_), return ERR_UNEXPECTED);

    if (d.flags() & O_CREATE) {
      FUSION_ENSURE(d.has_type(), return ERR_PARAMETER);
      FUSION_ENSURE(d.has_size(), return ERR_PARAMETER);
      FUSION_INFO("cid=%d name='%s' flags=%x type=%x size=%d", c->cid_, d.name().c_str(), d.flags(), d.type(), d.size());

      bool created;

      e = md_create(c->cid_, d.name().c_str(), d.flags(), type = d.type(), m, size = d.size(), created);

//      FUSION_ENSURE(e == ERR_OK && created && d.has_data(), return e);

      const md_t* md = md_get_internal_descriptor(m);

//      FUSION_ENSURE(md, return ERR_UNEXPECTED);

      if (md && md->mtype_ & MT_PERSISTENT) {
        md->last_sender_ = c->cid_;
        md->data(d.data().size(), (void*)d.data().c_str());
        md->ctime_ = req->org_;
      }
    }
    else {
      FUSION_ENSURE(!d.has_type(), return ERR_PARAMETER);
      FUSION_ENSURE(!d.has_size(), return ERR_PARAMETER);
      FUSION_INFO("cid=%d name='%s' flags=%x", c->cid_, d.name().c_str(), d.flags());

      e = md_open(c->cid_, d.name().c_str(), d.flags(), type, m, size);

      FUSION_DEBUG("type=%x size=%d", type, size);
    }

    if (e == ERR_OK || e == ERR_OPEN) {
      mcb_mopen_rep d1;

      if (d.flags() & O_CREATE)
        d1.set_mid(m);
      else {
        d1.set_mid(m);
        d1.set_size(size);
        d1.set_type(type);
      }

      if (e == ERR_OPEN)
        d1.set_already_opened(true);

      mcb_t* rep = mcbpool.outgoing_get(MD_SYS_MOPEN_REPLY, CID_SYS, c->cid_, get_next_seq(), false, req->seq_);

      FUSION_ENSURE(rep, return ERR_UNEXPECTED);

      rep->mid_ = MD_SYS_MOPEN_REPLY;
      rep->len_ = d1.ByteSize();

      if (rep->len_ > sizeof rep->u)
        rep->u.pdata_ = ::malloc(rep->len_);

      if (!d1.SerializeToArray(rep->data(), rep->len_))
        e = ERR_UNEXPECTED;

      if (write(rep, c) && e == ERR_OK)
        md_foreach_message(0, m, notify_open_pred, notify_open);

      RELEASE(mcbpool, rep);

      return ERR_OK;
    }

    return e;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_mclose_request(mcb_t* req, client_t* c) {
    FUSION_ENSURE(req,                                return ERR_UNEXPECTED);
    FUSION_ENSURE(c,                                  return ERR_UNEXPECTED);
    FUSION_ENSURE(c->registered_,                     return ERR_UNEXPECTED);
    FUSION_ENSURE(c->cid_ == req->src_,               return ERR_UNEXPECTED);
    FUSION_ENSURE(req->mid_ == MD_SYS_MCLOSE_REQUEST, return ERR_UNEXPECTED);
    FUSION_INFO("cid=%d mid=%d", c->cid_, req->u.mid_);

    mid_t mid   = req->u.mid_;
    result_t e  = md_close(c->cid_, mid);

    if (e == ERR_OK) {
      mcb_t* rep  = mcbpool.outgoing_get(MD_SYS_STATUS, CID_SYS, c->cid_, get_next_seq(), false, req->seq_);

      rep->len_     = sizeof e;
      rep->u.error_ = e;

      write(rep, c);

      RELEASE(mcbpool, rep);

      subs.del(mid, c->cid_);

      md_foreach_message(0, mid, notify_open_pred, notify_open);

      return ERR_OK;
    }

    return e;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_mlink_request(mcb_t* req, client_t* c) {
    FUSION_ENSURE(req,                                return ERR_UNEXPECTED);
    FUSION_ENSURE(c,                                  return ERR_UNEXPECTED);
    FUSION_ENSURE(c->registered_,                     return ERR_UNEXPECTED);
    FUSION_ENSURE(c->cid_ == req->src_,               return ERR_UNEXPECTED);
    FUSION_ENSURE(req->mid_ == MD_SYS_MLINK_REQUEST,  return ERR_UNEXPECTED);
    FUSION_INFO("cid=%d mid=%d", c->cid_, req->u.mid_);

    mcb_mlink d;

    FUSION_ENSURE(d.ParseFromArray(req->data(), req->len_), return ERR_UNEXPECTED);

    result_t e = md_ln(c->cid_, d.link().c_str(), d.orig().c_str(), d.soft()) ;

    if (e == ERR_OK) {
      mcb_t* rep  = mcbpool.outgoing_get(MD_SYS_STATUS, CID_SYS, c->cid_, get_next_seq(), false, req->seq_);

      rep->len_     = sizeof e;
      rep->u.error_ = e;

      write(rep, c);

      RELEASE(mcbpool, rep);

      return ERR_OK;
    }

    return e;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_munlink_request(mcb_t* req, client_t* c) {
    FUSION_ENSURE(req,                                                                          return ERR_UNEXPECTED);
    FUSION_ENSURE(c,                                                                            return ERR_UNEXPECTED);
    FUSION_ENSURE(c->registered_,                                                               return ERR_UNEXPECTED);
    FUSION_ENSURE(c->cid_ == req->src_,                                                         return ERR_UNEXPECTED);
    FUSION_ENSURE(req->mid_ == MD_SYS_MUNLINK_REQUEST || req->mid_ == MD_SYS_MUNLINK2_REQUEST,  return ERR_UNEXPECTED);
    FUSION_INFO("cid=%d mid=%d", c->cid_, req->u.mid_);

    result_t e = ERR_UNEXPECTED;

    switch (req->mid_) {
    case MD_SYS_MUNLINK_REQUEST:
      {
        mcb_munlink d;

        FUSION_ENSURE(d.ParseFromArray(req->data(), req->len_), return ERR_PARAMETER);

        e = md_unlink(c->cid_, d.name().c_str());
      }
      break;

    case MD_SYS_MUNLINK2_REQUEST:
      e = md_unlink(c->cid_, req->u.mid_);
      break;
    }

    if (e == ERR_OK) {
      mcb_t* rep  = mcbpool.outgoing_get(MD_SYS_STATUS, CID_SYS, c->cid_, get_next_seq(), false, req->seq_);

      rep->len_     = sizeof e;
      rep->u.error_ = e;

      write(rep, c);

      RELEASE(mcbpool, rep);

      return ERR_OK;
    }

    return e;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_mlist_request(mcb_t* req, client_t* c) {
    FUSION_ENSURE(req,                                return ERR_UNEXPECTED);
    FUSION_ENSURE(c,                                  return ERR_UNEXPECTED);
    FUSION_ENSURE(c->registered_,                     return ERR_UNEXPECTED);
    FUSION_ENSURE(c->cid_ == req->src_,               return ERR_UNEXPECTED);
    FUSION_ENSURE(req->mid_ == MD_SYS_MLIST_REQUEST,  return ERR_UNEXPECTED);
    FUSION_INFO("cid=%d", c->cid_);

    mcb_mlist_req d;

    FUSION_ENSURE(d.ParseFromArray(req->data(), req->len_), return ERR_UNEXPECTED);

    char* buff = 0;
    size_t nr;
    const char* prof = d.has_profile() ? d.profile().c_str() : 0;
    const char* mask = d.has_mask() ? d.mask().c_str() : 0;

    result_t e = md_mlist(c->cid_, prof, mask, nr, buff) ;

    if (e == ERR_OK) {
      mcb_mlist_rep d1;
      char** pbuff = (char**)buff;

      for (size_t i = 0; i < nr; ++i)
        d1.add_names(pbuff[i]);

      mcb_t* rep = mcbpool.outgoing_get(MD_SYS_MLIST_REPLY, CID_SYS, c->cid_, get_next_seq(), MD_SYS_NONE, req->seq_);

      rep->mid_ = MD_SYS_MLIST_REPLY;
      rep->len_ = d1.ByteSize();

      if (rep->len_ > sizeof rep->u)
        rep->u.pdata_ = ::malloc(rep->len_);

      if (!d1.SerializeToArray(rep->data(), rep->len_))
        e = ERR_UNEXPECTED;
      else if (rep->len_ > MCB_MAX_PAYLOAD)
        e = ERR_TRUNCATED;
      else
        write(rep, c);

      ::free(buff);

      RELEASE(mcbpool, rep);
    }

    return e;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_mmove_request(mcb_t* req, client_t* c) {
    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_subscribe_request(mcb_t* req, client_t* c) {
    FUSION_ENSURE(req,                                    return ERR_UNEXPECTED);
    FUSION_ENSURE(c,                                      return ERR_UNEXPECTED);
    FUSION_ENSURE(req->mid_ == MD_SYS_SUBSCRIBE_REQUEST,  return ERR_UNEXPECTED);

    mcb_subscribe d;

    FUSION_ENSURE(d.ParseFromArray(req->data(), req->len_), return ERR_UNEXPECTED);

    mid_t mid = d.mid();
    int flags = d.flags();

    FUSION_INFO("cid=%d mid=%d flags=%d", c->cid_, mid, flags);

    result_t e = ERR_OK;

    if (mid <= MD_SYS_LAST_)
      e = ERR_PARAMETER;
    else {
      // HACK: using md_check_size to see if message was opened
      switch (e = md_check_size(c->cid_, mid, 0)) {
      case ERR_OK:
      case ERR_MESSAGE_SIZE:
        e = subs.get(mid, c->cid_) ? ERR_SUBSCRIBED : ERR_OK;
      }
    }

    if (e == ERR_OK) {
      mcb_t* rep = mcbpool.outgoing_get(MD_SYS_STATUS, CID_SYS, c->cid_, get_next_seq(), false, req->seq_);

      rep->len_     = sizeof e;
      rep->u.error_ = e;

      if (write(rep, c)) {
#if MBM_SUBSCRBE_IS_ALWAYS_PUBLISH > 0
        subs.put(mid, true, c->cid_);
#else
        subs.put(mid, flags & SF_PUBLISH, c->cid_);
#endif

        const md_t* md = md_get_internal_descriptor(mid);

        FUSION_ENSURE(md, return ERR_UNEXPECTED);

        if (md->mtype_ & MT_PERSISTENT && md->last_sender_ != CID_NONE) {
          mcb_t* last = mcbpool.outgoing_get(mid, md->last_sender_, c->cid_, get_next_seq(), false);

          FUSION_ENSURE(last, return ERR_UNEXPECTED);

          last->org_ = md->ctime_;
          last->len_ = md->last_len_;

          if (last->len_) {
            if (last->len_ > sizeof(last->u)) {
              FUSION_VERIFY(last->u.pdata_ = ::malloc(last->len_));

              ::memcpy(last->u.pdata_, md->last_data_, last->len_);
            }
            else
              ::memcpy(&last->u, md->last_data_, last->len_);
          }

          if (write(last, c))
            md->atime_ = now_msecs();

          RELEASE(mcbpool, last);
        }
      }

      RELEASE(mcbpool, rep);

      return ERR_OK;
    }

    return e;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_unsubscribe_request(mcb_t* req, client_t* c) {
    FUSION_ENSURE(req,                                      return ERR_UNEXPECTED);
    FUSION_ENSURE(c,                                        return ERR_UNEXPECTED);
    FUSION_ENSURE(req->mid_ == MD_SYS_UNSUBSCRIBE_REQUEST,  return ERR_UNEXPECTED);
    FUSION_INFO("cid=%d mid=%d", c->cid_, req->u.mid_);

    result_t e = subs.del(req->u.mid_, c->cid_) ? ERR_OK : ERR_SUBSCRIBED;

    if (e == ERR_OK) {
      mcb_t* rep = mcbpool.outgoing_get(MD_SYS_STATUS, CID_SYS, c->cid_, get_next_seq(), false, req->seq_);

      rep->len_     = sizeof e;
      rep->u.error_ = e;

      write(rep, c);

      RELEASE(mcbpool, rep);

      return ERR_OK;
    }

    return e;
  }

  //////////////////////////////////////////////////////////////////////////////
  static bool cb_sysinfo_pred_any(void* ctx, unsigned oflags) {
    return true;
  }

  //////////////////////////////////////////////////////////////////////////////
  static void cb_sysinfo_client(void* ctx, const ccb_t* ccb) {
    FUSION_ENSURE(ctx, return);
    FUSION_ENSURE(ccb, return);

    mcb_sysinfo_reply*    p     = (mcb_sysinfo_reply*)ctx;
    int                   flags = p->flags();
    mcb_sysinfo_client*   sc    = p->add_clients();
    auto                  I     = clients->find(ccb->cid_);

    FUSION_ENSURE(I != clients->end(), return);

    client_t*             c     = I->second;

    FUSION_ENSURE(c->conn_, return);

    sc->set_cid(ccb->cid_);

    if ((flags & CLI_NAME) && ccb->name_)
      sc->set_name(ccb->name_);

    if ((flags & CLI_ADDRESS))
      sc->set_address(c->address_);

    if (flags & CLI_UUID)
      sc->set_uuid(&c->uuid_, sizeof c->uuid_);

    if ((flags & CLI_DEFAULT_PROFILE) && ccb->dp_->id_)
      sc->set_default_profile(ccb->dp_->id_);

    if (flags & CLI_PROFILES)
      for (auto I = ccb->profiles_.cbegin(), E = ccb->profiles_.cend(); I != E; ++I)
        if (I->first)
          sc->add_profiles(I->first);

    if (flags & CLI_SYNC_PERIOD)
      sc->set_clock_sync_period(c->conn_->clock_sync_.period_);

    if (flags & CLI_CONN_LATENCY)
      sc->set_connection_latency(c->conn_->clock_sync_.latency_);

    if (flags & CLI_QUEUE_LIMIT && c->profile_opts_)
      sc->set_output_queue_limit(c->profile_opts_->output_queue_limit_);

    if (flags & CLI_QUEUE_SIZE)
      sc->set_output_queue_size(c->conn_->wqueue_.size());
  }

  //////////////////////////////////////////////////////////////////////////////
  static void cb_sysinfo_mc(void* ctx, const md_t* md, const ccb_t* ccb) {
    FUSION_ENSURE(ctx,  return);
    FUSION_ENSURE(md,   return);
    FUSION_ENSURE(ccb,  return);

    mcb_sysinfo_reply*    p     = (mcb_sysinfo_reply*)ctx;
    int                   flags = p->flags();
    mcb_sysinfo_message*  sm    = p->add_messages();

    sm->set_cid(ccb->cid_);
    sm->set_mid(md->mid_);

    if (flags & (MSG_NAME|MSG_PATH|MSG_OFLAG|MSG_OPEN_NR)) {
      auto I = ccb->omsgs_.find(md->mid_);

      if (I != ccb->omsgs_.end()) {
#if (MD_KEEP_NAME > 0)
        if (flags & MSG_NAME)     sm->set_name(I->second.name_);
#endif
#if (MD_KEEP_PATH > 0)
        if (flags & MSG_PATH)     sm->set_path(I->second.path_);
#endif
        if (flags & MSG_OFLAG)    sm->set_oflags(I->second.oflags_);
        if (flags & MSG_OPEN_NR)  sm->set_open_nr(1);
      }
    }

    if (flags & (MSG_SFLAG|MSG_SUBS_NR))
      if (sub_dcr_t* sd = subs.get(md->mid_, ccb->cid_)) {
        if (flags & MSG_SFLAG)    sm->set_sflags(sd->publish_ ? SF_PUBLISH : SF_PRIVATE);
        if (flags & MSG_SUBS_NR)  sm->set_subs_nr(1);
      }
      else
        if (flags & MSG_SUBS_NR)  sm->set_subs_nr(0);
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t sys_info_request(mcb_t* req, client_t* c) {
    FUSION_ENSURE(req,  return ERR_UNEXPECTED);
    FUSION_ENSURE(req,  return ERR_UNEXPECTED);
    FUSION_ENSURE(c,    return ERR_UNEXPECTED);
    FUSION_ENSURE(req->mid_ == MD_SYS_SYSINFO_REQUEST, return ERR_UNEXPECTED);
    FUSION_INFO("cid=%d", c->cid_);

    result_t            e = md_access(c->cid_, MD_SYS_SYSINFO_REQUEST, MD_ACCESS_OPERATION_READ);
    mcb_sysinfo_request q;
    mcb_sysinfo_reply   p;

    if (e != ERR_OK)
      return e;

    FUSION_ENSURE(q.ParseFromArray(req->data(), req->len_), return ERR_PARAMETER);

    int flags = q.flags();

    FUSION_INFO("flags=%x", flags);

    p.set_flags(flags);

    if (flags & COM_START_TIME) {  /*TODO*/; }

    if (flags & COM_AVAIL_MCBS)
      p.mutable_common()->set_avail_mcbs(mcbpool.size());

    if (flags & COM_ALLOCATED_MCBS)
      p.mutable_common()->set_allocated_mcbs(mcb_t::nr__);

    if (flags & COM_PREALLOC_MCBS)
      p.mutable_common()->set_prealloc_mcbs(opt_prealloc_mcbs);

    if (flags & COM_MAX_ALLOC_MCBS)
      p.mutable_common()->set_max_alloc_mcbs(opt_max_alloc_mcbs);

    if (flags & SYS_CLIENTS) { /////////////////////////////////////////////////
      if (q.cids_size() == 0) {
        for (auto I = clients->cbegin(), E = clients->cend(); I != E; ++I)
          md_foreach_message(&p, I->first, cb_sysinfo_pred_any, cb_sysinfo_client);
      }
      else if (q.cids_size() == 1) {
        if (q.cids(0) == CID_ALL) {
          /* all c-infos */
          for (auto I = clients->cbegin(), E = clients->cend(); I != E; ++I)
            md_foreach_message(&p, I->first, cb_sysinfo_pred_any, cb_sysinfo_client);
        }
        else if (q.cids(0) == (CID_NOSELF|CID_ALL)) {
          for (auto I = clients->cbegin(), E = clients->cend(); I != E; ++I)
            if (c->cid_ != I->first)
              md_foreach_message(&p, I->first, cb_sysinfo_pred_any, cb_sysinfo_client);
        }
        else
          goto cl;
      }
      else {
cl:     for (int i = 0; i < q.cids_size(); ++i)
          md_foreach_message(&p, q.cids(i), cb_sysinfo_pred_any, cb_sysinfo_client);
      }
    }

    if (flags & SYS_CLIENT_MESSAGES) { /////////////////////////////////////////
      bool mall   = q.mids_size() == 0 || (q.mids_size() == 1 && q.mids(0) == MD_SYS_ANY);
      bool call   = q.cids_size() == 0 || (q.cids_size() == 1 && (q.cids(0) == CID_ALL) || q.cids(0) == (CID_NOSELF|CID_ALL));

      if (mall && call) {
        if (q.cids(0) == (CID_NOSELF|CID_ALL)) {
          for (auto I = clients->cbegin(), E = clients->cend(); I != E; ++I)
            if (c->cid_ != I->first)
              md_foreach_message(&p, I->first, MD_SYS_ANY, cb_sysinfo_pred_any, cb_sysinfo_mc);
        }
        else
          md_foreach_message(&p, MD_SYS_ANY, cb_sysinfo_pred_any, cb_sysinfo_mc);
      }
      else if (!mall && call) {
        if (q.cids(0) == (CID_NOSELF|CID_ALL)) {
          for (auto I = clients->cbegin(), E = clients->cend(); I != E; ++I)
            if (c->cid_ != I->first)
              for (int i = 0; i < q.mids_size(); ++i)
                md_foreach_message(&p, I->first, q.mids(i), cb_sysinfo_pred_any, cb_sysinfo_mc);
        }
        else
          for (auto I = clients->cbegin(), E = clients->cend(); I != E; ++I)
            for (int i = 0; i < q.mids_size(); ++i)
              md_foreach_message(&p, I->first, q.mids(i), cb_sysinfo_pred_any, cb_sysinfo_mc);
      }
      else if (mall && !call) {
        for (int i = 0; i < q.cids_size(); ++i)
          md_foreach_message(&p, q.cids(i), MD_SYS_ANY, cb_sysinfo_pred_any, cb_sysinfo_mc);
      }
      else if (!mall && !call) {
        for (int i = 0; i < q.cids_size(); ++i)
          for (int j = 0; j < q.mids_size(); ++j)
            md_foreach_message(&p, q.cids(i), q.mids(j), cb_sysinfo_pred_any, cb_sysinfo_mc);
      }
    }

    if (flags & SYS_MESSAGES) { ////////////////////////////////////////////////
      /* TODO  */
    }

    mcb_t* rep = mcbpool.outgoing_get(MD_SYS_SYSINFO_REPLY, CID_SYS, c->cid_, get_next_seq(), false, req->seq_);

    rep->len_ = p.ByteSize();

    if (rep->len_ > sizeof rep->u)
      rep->u.pdata_ = ::malloc(rep->len_);

    if (!p.SerializeToArray(rep->data(), rep->len_))
      e = ERR_UNEXPECTED;
    else if (rep->len_ > MCB_MAX_PAYLOAD)
      e = ERR_TRUNCATED;
    else
        write(rep, c);

    RELEASE(mcbpool, rep);

    return e;
  }

  //////////////////////////////////////////////////////////////////////////////
  static bool start_listen() {
    sockaddr_in service;

    service.sin_family      = AF_INET;
    service.sin_addr.s_addr = host;
    service.sin_port        = htons(port);

    FUSION_ENSURE((listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) != INVALID_SOCKET, return false, "socket=%d", ::WSAGetLastError());
    FUSION_ENSURE(::bind(listen_socket, (SOCKADDR*)&service, sizeof (service)) != SOCKET_ERROR, goto exit0, "bind=%d", ::WSAGetLastError());
    FUSION_ENSURE(::listen(listen_socket, opt_listen_backlog) != SOCKET_ERROR, goto exit0, "listen=%d", ::WSAGetLastError());
    FUSION_ENSURE((listen_event = ::WSACreateEvent()) != WSA_INVALID_EVENT, goto exit0, "WSACreateEvent()=%d", ::WSAGetLastError());
    FUSION_ENSURE(::WSAEventSelect(listen_socket, listen_event, FD_READ|FD_ACCEPT) != SOCKET_ERROR, goto exit1, "WSAEventSelect()=%d", ::WSAGetLastError());

    FUSION_ENSURE((udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) != INVALID_SOCKET, goto exit1, "socket=%d", ::WSAGetLastError());
    FUSION_ENSURE(::bind(udp_socket, (SOCKADDR*)&service, sizeof (service)) != SOCKET_ERROR, goto exit2, "bind=%d", ::WSAGetLastError());
    FUSION_ENSURE((udp_event = ::WSACreateEvent()) != WSA_INVALID_EVENT, goto exit2, "WSACreateEvent()=%d", ::WSAGetLastError());
    FUSION_ENSURE(::WSAEventSelect(udp_socket, udp_event, FD_READ) != SOCKET_ERROR, goto exit3, "WSAEventSelect()=%d", ::WSAGetLastError());

#ifdef  USE_STOP_EVENT
    FUSION_ENSURE((stop_event = ::WSACreateEvent()) != WSA_INVALID_EVENT, goto exit4, "WSACreateEvent()=%d", ::WSAGetLastError());
#endif

#ifdef  TEST_PARTIAL_WRITES
    FUSION_ENSURE((fake_write_resume_event = ::CreateEvent(0, FALSE, FALSE, 0)) != WSA_INVALID_EVENT, goto exit5, "WSACreateEvent()=%d", ::WSAGetLastError());
#endif

    return true;

#ifdef  TEST_PARTIAL_WRITES
exit5:
    ::CloseHandle(stop_event);
    stop_event = INVALID_HANDLE_VALUE;
#endif

#ifdef  USE_STOP_EVENT
exit4:
    // un-???::WSAEventSelect(udp_socket, udp_event, FD_READ)
#endif

exit3:
    ::CloseHandle(udp_event);
    udp_event = INVALID_HANDLE_VALUE;

exit2:
    if (::closesocket(udp_socket) == SOCKET_ERROR)
      FUSION_ERROR("closesocket=%d", ::WSAGetLastError());

exit1:
    ::CloseHandle(listen_event);
    listen_event = INVALID_HANDLE_VALUE;

exit0:
    if (::closesocket(listen_socket) == SOCKET_ERROR)
      FUSION_ERROR("closesocket=%d", ::WSAGetLastError());

    return false;
  }

  //////////////////////////////////////////////////////////////////////////////
  static void send_error_reply(client_t* c, mcb_t* req, result_t e) {
    mcb_t* rep = mcbpool.outgoing_get(MD_SYS_STATUS, CID_SYS, req->src_, get_next_seq(), false, req->seq_);

    rep->data(&e, sizeof e);

    c->conn_->write_mcb(rep);

    RELEASE(mcbpool, rep);
  }

  // dispatch in-coming event or timeout ///////////////////////////////////////
  static void dispatch(mcb_t* req, cid_t cid, std::vector<client_t*>& dist_list) {
    FUSION_ENSURE(req, return);
    FUSION_DEBUG("cid=%d mid=%s src=%d dst=%d seq=%d", cid, enumstr((_md_sys_t)req->mid_), req->src_, req->dst_, req->seq_);

    FUSION_VERIFY( // @ ASSERT
      FUSION_IMPLIES(
        req->request_ || req->req_seq_,
        !((CID_IS_ALL(req->dst_) || CID_IS_PUB(req->dst_) || CID_IS_ALL_NOSELF(req->dst_) || CID_IS_PUB_NOSELF(req->dst_)))
      ),
      " if it is a request(%d) or a reply(%d) then it is not pub (all=%d pub=%d all-noself=%d pub-noself=%d)",
      req->request_,
      req->req_seq_ != 0,
      CID_IS_ALL(req->dst_),
      CID_IS_PUB(req->dst_),
      CID_IS_ALL_NOSELF(req->dst_),
      CID_IS_PUB_NOSELF(req->dst_)
    );

#if (MBM_DISPLAY_MESSAGES_PERIOD > 0)
    static nf::msecs_t ms0  = nf::now_msecs();

    nf::msecs_t ms1         = nf::now_msecs();

    ++nrr;

    if (ms1 - ms0 >= MBM_DISPLAY_MESSAGES_PERIOD) {
      FUSION_INFO("period=%d msecs: received=%d [%g], sent=%d [%g], dropped=%d", size_t(ms1 - ms0), nrr, 1000.0 * double(nrr)/double(ms1 - ms0), nrs, 1000.0 * double(nrs)/double(ms1 - ms0), nrd);

      ms0 = ms1;
      nrr = nrs = nrd = 0;
    }
#endif

    // timeout /////////////////////////////////////////////////////////////////
    if (req->expired_) {
      if (req->mid_ == MD_SYS_TIMESYNC_REQUEST)
        resync_clock(req);
      else
        FUSION_WARN("Expired msg=%s [%d]; do not know what to do with it...", enumstr((_md_sys_t)req->mid_), req->mid_);

      goto exit;
    }

    client_t* c = clients->get(cid);

    if (!c || !c->conn_)
      goto exit;

    FUSION_ENSURE(c && c->conn_, goto exit);
    FUSION_VERIFY(req->src_ == c->cid_, "spoofed?");

    // server reqs /////////////////////////////////////////////////////////////
    if (CID_IS_SYS(req->dst_)) {
      result_t e = handle_sys_msgs(c->registered_, req->mid_)(req, c);

      if (e != ERR_OK && req->request_)
        send_error_reply(c, req, e);
    }
    // user reqs ///////////////////////////////////////////////////////////////
    else {
      const md_t* md = md_get_internal_descriptor(req->mid_);

      FUSION_ENSURE(md, goto exit, "mid=%s", enumstr((_md_sys_t)req->mid_));
      FUSION_ENSURE(FUSION_IMPLIES(req->request_ || req->req_seq_, (md->mtype_ & MT_TYPE_MASK) == MT_EVENT), goto exit);

      result_t e;

      if (md->size_ != -1 && md->size_ != req->len_)
        e = ERR_MESSAGE_SIZE;
      else
        e = md_access(cid, req->mid_, MD_ACCESS_OPERATION_WRITE);

      if (e != ERR_OK) {
        if (req->request_)
          send_error_reply(c, req, e);
      }
      /*reply?*/
      else if (req->req_seq_) {
        FUSION_ASSERT(CID_IS_CLIENT(req->dst_));

        auto k = std::make_pair(req->dst_, req->req_seq_);
        auto P = c->pcrs_.find(k);

        if (P == c->pcrs_.end()) {
          FUSION_WARN("cid=%0.4x - no pending-client-reply cookie found: dst=%0.4x sq=%d", req->src_, req->dst_, req->req_seq_);

          if (req->request_)
            send_error_reply(c, req, ERR_INVALID_DESTINATION);
        }
        else {
          c->pcrs_.erase(k);

          auto C = clients->find(req->dst_);

          if (C != clients->cend())
            switch (md->mtype_ & MT_TYPE_MASK) {
            case MT_DATA:
              udp_write(C->second, req);

              break;

            case MT_EVENT:
              FUSION_ENSURE(C->second->profile_opts_, continue);

              write(req, C->second, C->second->profile_opts_->output_queue_limit_);

              break;
            }
          else
            FUSION_WARN("dst=%0.4x is gone", req->dst_);
        }
      }
      else {
        FUSION_ASSERT(md);

        if (md && (md->mtype_ & MT_PERSISTENT) &&                           // is persistent kind
            CID_IS_ALL(req->dst_) && CID_IS_PUB(req->dst_) &&               // is pub/sub
            CID_IS_ALL_NOSELF(req->dst_) && CID_IS_PUB_NOSELF(req->dst_) && // is pub/sub
            !req->request_ && !req->req_seq_)                               // is pub/sub: not request/reply
        {
          md->last_sender_ = cid;
          md->data(req->len_, req->data());
          md->ctime_ = req->org_;

          if (opt_write_thru_persistent_data)
            if (!md_flush_internal_descriptor(md))
              FUSION_WARN("md_flush_internal_descriptor(mid=%d) failed", md->mid_);
        }

        if (CID_IS_ALL(req->dst_)) {
          if (req->request_)
            send_error_reply(c, req, ERR_INVALID_DESTINATION);
          else
            for (clients_t::const_iterator I = clients->cbegin(); I != clients->cend(); ++I) {
              if (md_access(I->first, req->mid_, MD_ACCESS_OPERATION_READ) == ERR_OK)
                dist_list.push_back(I->second);
            }
        }
        else if (CID_IS_ALL_NOSELF(req->dst_)) {
          if (req->request_)
            send_error_reply(c, req, ERR_INVALID_DESTINATION);
          else
            for (clients_t::const_iterator I = clients->cbegin(); I != clients->cend(); ++I) {
              if (req->src_ != I->first && md_access(I->first, req->mid_, MD_ACCESS_OPERATION_READ) == ERR_OK)
                dist_list.push_back(I->second);
            }
        }
        else if (CID_IS_PUB(req->dst_)) {
          if (req->request_)
            send_error_reply(c, req, ERR_INVALID_DESTINATION);
          else {
            std::pair<subs_t::iterator, subs_t::iterator> p = subs.equal_range(req->mid_);

            for (subs_t::iterator I = p.first; I != p.second; ++I) {
              FUSION_ENSURE(I->first == req->mid_, continue);

              if (I->second.publish_) {
                clients_t::const_iterator J = clients->find(I->second.cid_);

                if (J != clients->cend() && md_access(J->first, req->mid_, MD_ACCESS_OPERATION_READ) == ERR_OK)
                  dist_list.push_back(J->second);
              }
            }
          }
        }
        else if (CID_IS_PUB_NOSELF(req->dst_)) {
          if (req->request_)
            send_error_reply(c, req, ERR_INVALID_DESTINATION);
          else {
            std::pair<subs_t::iterator, subs_t::iterator> p = subs.equal_range(req->mid_);

            for (subs_t::iterator I = p.first; I != p.second; ++I) {
              FUSION_ENSURE(I->first == req->mid_, continue);

              if (I->second.publish_ && I->second.cid_ != c->cid_) {
                clients_t::const_iterator J = clients->find(I->second.cid_);

                if (J != clients->cend() && md_access(J->first, req->mid_, MD_ACCESS_OPERATION_READ) == ERR_OK)
                  dist_list.push_back(J->second);
              }
            }
          }
        }
        //else if (CID_IS_GRP(req->dst_)) { /* TODO */; }
        //else if (CID_IS_GRP_NOSELF(req->dst_)) { /* TODO */; }
        else if (CID_IS_CLIENT(req->dst_)) {
client:
          clients_t::const_iterator I = clients->find(req->dst_);
          client_t* dst = 0;

          if (I == clients->cend()) {
            if (req->request_)
              send_error_reply(c, req, ERR_INVALID_DESTINATION);
          }
          else {
            dst = I->second;

            if (req->request_) {
              if (subs.get(req->mid_, req->dst_)) {
                dst->pcrs_.insert(std::make_pair(req->src_, req->seq_));
                dist_list.push_back(dst);
              }
              else
                send_error_reply(c, req, ERR_INVALID_DESTINATION);
            }
            else if (subs.get(req->mid_, req->dst_))
              if (dst && md_access(dst->cid_, req->mid_, MD_ACCESS_OPERATION_READ) == ERR_OK)
                dist_list.push_back(dst);
          }
        }
        else if (CID_IS_CLIENT_NOSELF(req->dst_)) {
          if (req->src_ != (req->dst_ & CID_CLIENT))
            goto client;
          else if (req->request_)
            send_error_reply(c, req, ERR_INVALID_DESTINATION);
        }

        if (md && !dist_list.empty())
          md->atime_ = req->org_;

        for (std::vector<client_t*>::const_iterator I = dist_list.cbegin(), E = dist_list.cend(); I != E; ++I) {
          FUSION_ENSURE(*I, continue);

#if (MBM_DISPLAY_MESSAGES_PERIOD > 0)
          ++nrs;
#endif

          switch (md->mtype_ & MT_TYPE_MASK) {
          case MT_DATA:
            udp_write(*I, req);

            break;

          case MT_EVENT:
            if (req->mid_ == MD_SYS_TERMINATE_REQUEST) {
              (*I)->conn_->_write(req);
              cleanup(*I);
            }
            else if ((*I)->profile_opts_)
              write(req, *I, (*I)->profile_opts_->output_queue_limit_);

            break;
          }
        }
      }
    }

exit:
    RELEASE(mcbpool, req);
  }

  //////////////////////////////////////////////////////////////////////////////
  static void populate_wha() {
#if defined(_DEBUG)
    for (size_t i = FIXED_EVENTS_NR; i < FUSION_ARRAY_SIZE(wha); ++i)
      wha[i] = 0;
#endif

    if (opt_wha_shuffle)
      clients->shuffle();

    for (size_t i = 0; i < clients->size(); ++i)
      wha[i + FIXED_EVENTS_NR] = clients->array[i]->conn_->wait_handle();

#if defined(_DEBUG)
    {
      char buff[1024] = {0}, *cp = &buff[0];
      int n = 0;

      for (size_t i = 0; i < clients->size(); ++i)
        n += _snprintf(cp + n, sizeof(buff) - n - 1, "0x%p ", clients->array[i]->conn_->wait_handle());

      FUSION_DEBUG("wha { %s}", buff);
    }
#endif
  }

  //////////////////////////////////////////////////////////////////////////////
  static void run() {
    FUSION_VERIFY(::SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS));
    FUSION_VERIFY(::SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL));

# ifdef  MBM_LISTEN_TIMER_MSECS
    {
      LARGE_INTEGER DueTime;

      if (!opt_listen_timer)
        DueTime.QuadPart  = -LLONG_MIN;
      else
        DueTime.QuadPart  = -600000000;

      listen_timer = ::CreateWaitableTimer(0, FALSE, 0);

      FUSION_ENSURE(listen_timer, return);
      FUSION_ENSURE(::SetWaitableTimer(listen_timer,  &DueTime,  opt_listen_timer, 0, 0, TRUE), { ::CloseHandle(listen_timer); return; });
    }
# endif

    if (!start_listen())
      return;

    mcb_t* mcb;
    bool force_accept = false;

    wha[0] = listen_event;
    wha[1] = toq.wait_handle();
    wha[2] = udp_event;
#ifdef  USE_STOP_EVENT
    wha[STOP_EVENT_IDX] = stop_event;
# endif
#ifdef  MBM_LISTEN_TIMER_MSECS
    wha[LISTEN_TIMER_EVENT_IDX] = listen_timer;
# endif
#ifdef  TEST_PARTIAL_WRITES
    wha[FAKE_RESUME_EVENT_IDX] = fake_write_resume_event;
#endif

    std::vector<client_t*> dist_list_placeholder(MAXIMUM_WAIT_OBJECTS - FIXED_EVENTS_NR);

    FUSION_ASSERT(WSA_WAIT_EVENT_0       == WAIT_OBJECT_0);
    FUSION_ASSERT(WSA_WAIT_TIMEOUT       == WAIT_TIMEOUT);
    FUSION_ASSERT(WSA_WAIT_IO_COMPLETION == WAIT_IO_COMPLETION);
    FUSION_ASSERT(WSA_WAIT_FAILED        == WAIT_FAILED);

    ::fprintf(stdout, "Ready to accept requests. Press ^C to exit...\n");

    while (!stop || !pq.empty()) {
      DWORD index;

      if (pq.queue_.size() < MBM_PENDING_NET_MESSAGES_THRESHOLD)  // @@
        index = ::WSAWaitForMultipleEvents(FIXED_EVENTS_NR + clients->size(), wha, FALSE, pq.empty() ? INFINITE : 0, TRUE);
      else
        index = WAIT_TIMEOUT;

      switch (index) {
      case WAIT_TIMEOUT:
        FUSION_ENSURE(!pq.empty(), break);

        goto do_dispatch;

      case WAIT_OBJECT_0:
        {
          WSANETWORKEVENTS nevts;
          bool             accepted = false;

          if (::WSAEnumNetworkEvents(listen_socket, listen_event, &nevts)) {
            FUSION_DEBUG("WSAEnumNetworkEvents=%d", ::WSAGetLastError());

            break;
          }

          force_accept = false;

try_accept:
          for (size_t i = 0; i < opt_listen_backlog; ++i) {
            //struct sockaddr addr;
            struct sockaddr_in addr_ip4;
            int addrlen = sizeof addr_ip4;
            ::SOCKET s = ::accept(listen_socket, (sockaddr*)&addr_ip4, &addrlen);

            if (s == INVALID_SOCKET)
              continue;

            ::WSAResetEvent(listen_event);

#if (MBM_TCP_NO_DELAY > 0)
            BOOL v = 1;
            int rc = ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&v, sizeof v);

            if (rc == SOCKET_ERROR)
              FUSION_WARN("setsockopt(TCPNODELAY)=%d", ::WSAGetLastError());
#endif

            // check limits
            if (clients->size() >= MAXIMUM_WAIT_OBJECTS - FIXED_EVENTS_NR) {
              FUSION_ERROR("clients limit");

              // send error????
              ::closesocket(s);

              break;
            }

            tcp_conn_t* conn  = new tcp_conn_t(s, &mcbpool);    // this caused by listen socket, so tcp_conn is a factory method
            client_t*   c     = new client_t(cid_pool.get(), conn, addr_ip4.sin_addr.S_un.S_addr, addr_ip4.sin_port);

            FUSION_ENSURE(conn, continue);
            FUSION_ENSURE(c, { delete c; continue; } );
            FUSION_INFO("new client socket=%d cid=%d", s, c->cid_);

            char buff[33] = {0};

            ::_snprintf(buff, sizeof buff - 1, "%d.%d.%d.%d:%d",
              addr_ip4.sin_addr.S_un.S_un_b.s_b1,
              addr_ip4.sin_addr.S_un.S_un_b.s_b2,
              addr_ip4.sin_addr.S_un.S_un_b.s_b3,
              addr_ip4.sin_addr.S_un.S_un_b.s_b4,
              ::ntohs(addr_ip4.sin_port)
            );

            accepted = true;
            FUSION_ENSURE((c->address_ = ::strdup(buff)), { delete c; break; });
            FUSION_ENSURE(clients->add(c), { delete c; break; });
            populate_wha();
          }
        
          if (!accepted && !force_accept)
            FUSION_WARN("accept=%d", ::WSAGetLastError());
        }

        break;

      case WAIT_OBJECT_0 + 1:
        FUSION_DEBUG("timeout");

        if (toq.get(mcb)) {
          FUSION_ENSURE(mcb->expired_, break);
//        FUSION_INFO(">>>>>>>>> mid=%d rc=%d src=%d dst=%d seq=%d len=%d", mcb->mid_, mcb->rc_, mcb->src_, mcb->dst_, mcb->seq_, mcb->len_);
          FUSION_VERIFY(mcb->rc_ > 0, " mid=%s rc=%d src=%d dst=%d seq=%d len=%d", enumstr((_md_sys_t)mcb->mid_), mcb->rc_, mcb->src_, mcb->dst_, mcb->seq_, mcb->len_);

          pq.put(std::make_pair(mcb, mcb->src_));
        }

        break;

      case WAIT_OBJECT_0 + 2: {
          FUSION_DEBUG("udp");

          WSANETWORKEVENTS nevts;

          if (::WSAEnumNetworkEvents(udp_socket, udp_event, &nevts)) {
            FUSION_DEBUG("WSAEnumNetworkEvents=%d", ::WSAGetLastError());

            break;
          }

          client_t* c;

          if (nevts.lNetworkEvents & FD_READ) {
            if (udp_read(c, mcb)) {
              FUSION_ASSERT(c);
              FUSION_ASSERT(mcb);

              pq.put(std::make_pair(mcb, c->cid_));
            }
            else if (c) {
              cleanup(c);
              populate_wha();
            }
          }
          else
            FUSION_WARN("got udp event other then read: %d", nevts.lNetworkEvents);
        }

        break;

#ifdef  USE_STOP_EVENT
      case WAIT_OBJECT_0 + STOP_EVENT_IDX:
        FUSION_INFO("stopping...");

        stop = true;

        break;
#endif

#ifdef  MBM_LISTEN_TIMER_MSECS
      case WAIT_OBJECT_0 + LISTEN_TIMER_EVENT_IDX:
        FUSION_DEBUG("listen timer - forcing accept");

        force_accept = true;

        goto try_accept;
#endif

#ifdef  TEST_PARTIAL_WRITES
      case WAIT_OBJECT_0 + FAKE_RESUME_EVENT_IDX:
        FUSION_WARN("resuming after fake partial write");

        ::ResetEvent(fake_write_resume_event);

        for (auto I = clients->begin(), E = clients->end(); I != E; ++I)
          I->second->conn_->resume_write();

        break;
#endif

      default:
        // client read/write/disconnect
        if (index >= WAIT_OBJECT_0 + FIXED_EVENTS_NR && index < WAIT_OBJECT_0 + FIXED_EVENTS_NR + clients->size()) {
          size_t idx  = index - WAIT_OBJECT_0 - FIXED_EVENTS_NR;
          client_t* c = clients->array[idx];
          WSANETWORKEVENTS nevts;

          if (::WSAEnumNetworkEvents(((tcp_conn_t*)clients->array[idx]->conn_)->socket_, clients->array[idx]->conn_->wait_handle(), &nevts)) {
            FUSION_DEBUG("WSAEnumNetworkEvents=%d", ::WSAGetLastError());

            cleanup(c);
            populate_wha();

            break;
          }

          if (nevts.lNetworkEvents & FD_CLOSE) {
            FUSION_DEBUG("FD_CLOSE=%d idx=%d cid=%d", nevts.iErrorCode[FD_CLOSE_BIT], idx, c->cid_);

            cleanup(c);
            populate_wha();

            break;
          }

          if (nevts.lNetworkEvents & FD_READ) {
            FUSION_DEBUG("FD_READ=%d", nevts.iErrorCode[FD_READ_BIT]);
            FUSION_DEBUG("idx=%d cid=%d: data read", idx, c->cid_);

            bool  first = true;
            bool  rc;

            do {
              rc = c->conn_->read_mcb(first, &mcb);
              first = false;

              if (mcb) {
                mcb->src_ = c->cid_;
                pq.put(std::make_pair(mcb, c->cid_));
              }
            } while (rc);

            if (opt_wha_shuffle)
              populate_wha();
          }

          if (nevts.lNetworkEvents & FD_WRITE) {
            FUSION_DEBUG("FD_WRITE=%d", nevts.iErrorCode[FD_WRITE_BIT]);

            c->conn_->resume_write();
          }
        }
        else if (index >= WAIT_ABANDONED_0 && index < WAIT_OBJECT_0 + FIXED_EVENTS_NR + clients->size()) {
          size_t idx = index - WAIT_ABANDONED_0 - FIXED_EVENTS_NR;

          FUSION_DEBUG("idx=%d cid=%d: abandoned", idx, clients->array[idx]->cid_);

          cleanup(clients->array[idx]);
          populate_wha();

          break;
        }
        else {
          FUSION_ERROR("unexpected wait result=%d", index);

          // PANIC;
          stop = true;

          break;
        }

        break;

      case WAIT_IO_COMPLETION:
        continue;

      case WAIT_FAILED:       // bad client connection?
        FUSION_WARN("WAIT_FAILED");

        populate_wha();

        continue;
      }

do_dispatch:
      std::pair<mcb_t*, cid_t> pair;

      if (pq.get(pair)) {
        dist_list_placeholder.clear();
        dispatch(pair.first, pair.second, dist_list_placeholder);
      }
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  sys_cb_t handle_sys_msgs(bool registered, mid_t m) {
    if (!registered)
      switch (m) {
      case MD_SYS_ECHO_REQUEST:             return sys_echo_request;
      case MD_SYS_REGISTER_REQUEST:         return sys_reg_request;
      default:                              return sys_unexpected;
      }

    // only redigstered
    switch (m) {
    case MD_SYS_STATUS:                     return sys_unexpected;
    case MD_SYS_ECHO_REQUEST:               return sys_echo_request;
    case MD_SYS_ECHO_REPLY:                 return sys_unexpected;

    // time synchronization
    case MD_SYS_TIMESYNC_REQUEST:           return sys_unexpected;
    case MD_SYS_TIMESYNC_REPLY:             return sys_timesync_reply;

    // registration/termination
    case MD_SYS_REGISTER_REQUEST:           return sys_reg_request;
    case MD_SYS_REGISTER_REPLY:             return sys_unexpected;
    case MD_SYS_UNREGISTER_REQUEST:         return sys_unreg_request;
//  case MD_SYS_UNREGISTER_REPLY:           return sys_unreg_reply;
    case MD_SYS_STOP_REQUEST:               return sys_unexpected;
//  case MD_SYS_STOP_REPLY:                 return sys_unexpected;
    case MD_SYS_TERMINATE_REQUEST:          return sys_unexpected;

    // clients
    case MD_SYS_QUERY_CLIENTS_REQUEST:      return sys_unexpected;
    case MD_SYS_QUERY_CLIENT_BY_ID_REQUEST: return sys_unexpected;
    case MD_SYS_QUERY_CLIENTS_REPLY:        return sys_unexpected;
    case MD_SYS_QUERY_CLIENT_REPLY:         return sys_unexpected;

    // user
//  case MD_SYS_QUERY_USER_REQUEST:         return sys_unexpected;
//  case MD_SYS_QUERY_USER_REPLY:           return sys_unexpected;

    // groups
    case MD_SYS_QUERY_GROUP_REQUEST:        return sys_unexpected;
    case MD_SYS_QUERY_GROUP_REPLY:          return sys_unexpected;

    // message
    case MD_SYS_MOPEN_REQUEST:              return sys_mopen_request;
    case MD_SYS_MCLOSE_REQUEST:             return sys_mclose_request;
    case MD_SYS_MLINK_REQUEST:              return sys_mlink_request;
    case MD_SYS_MUNLINK_REQUEST:            return sys_munlink_request;
    case MD_SYS_MUNLINK2_REQUEST:           return sys_munlink_request;
    case MD_SYS_MMOVE_REQUEST:              return sys_mmove_request;
    case MD_SYS_MLIST_REQUEST:              return sys_mlist_request;

    // message attribues
//  case MD_SYS_MSTAT_BY_ID_REQUEST:        sys_unexpected;
//  case MD_SYS_MSTAT_BY_NAME_REQUEST:      sys_unexpected;
//  case MD_SYS_MATTR_READ_REQUEST:         sys_unexpected;
//  case MD_SYS_MATTR_WRITE_REQUEST:        sys_unexpected;
//  case MD_SYS_MATTR_BULK_READ_REQUEST:    sys_unexpected;
//  case MD_SYS_MATTR_BULK_WRITE_REQUEST:   sys_unexpected;
//  case MD_SYS_MLIST_REPLY:                sys_unexpected;
//  case MD_SYS_MSTAT_REPLY:                sys_unexpected;
//  case MD_SYS_MATTR_READ_REPLY:           sys_unexpected;
//  case MD_SYS_MATTR_BULK_READ_REPLY:      sys_unexpected;

    // subscribe
    case MD_SYS_SUBSCRIBE_REQUEST:          return sys_subscribe_request;
    case MD_SYS_UNSUBSCRIBE_REQUEST:        return sys_unsubscribe_request;

    // sys info
    case MD_SYS_SYSINFO_REQUEST:            return sys_info_request;

    default:                                return sys_unexpected;
    }
  }

#if DEBUG_HEAP > 0
  void release(mcb_t* mcb) {
    RELEASE(mcbpool, mcb);
  }
#endif
}

////////////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv) {
#if DEBUG_HEAP > 0
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF|_CRTDBG_LEAK_CHECK_DF|_CRTDBG_CHECK_CRT_DF);
  _CrtSetReportMode(_CRT_WARN,    _CRTDBG_MODE_FILE);
  _CrtSetReportMode(_CRT_ERROR,   _CRTDBG_MODE_FILE);
  _CrtSetReportMode(_CRT_ASSERT,  _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN,    _CRTDBG_FILE_STDERR);
  _CrtSetReportFile(_CRT_ERROR,   _CRTDBG_FILE_STDERR);
  _CrtSetReportFile(_CRT_ASSERT,  _CRTDBG_FILE_STDERR);
#endif

  const char* port    = "3001";
  const char* ifc     = "0.0.0.0";

  for (int i = 1; i < argc; ++i) {
    size_t len;

    if (     (len = 7)  && !::strncmp(argv[i], "--port=", len))
      port = argv[i] + len;
    else if ((len = 6)  && !::strncmp(argv[i], "--ifc=", len))
      ifc = argv[i] + len;
    else if ((len = 7)  && !::strncmp(argv[i], "--host=", len))
      ifc = argv[i] + len;
    else if ((len = 9)  && !::strncmp(argv[i], "--config=", len))
      opt_confdir = argv[i] + len;
    else if ((len = 16) && !::strncmp(argv[i], "--prealloc-mcbs=", len))
      opt_prealloc_mcbs = ::atoi(argv[i] + len);
    else if ((len = 17) && !::strncmp(argv[i], "--max-alloc-mcbs=", len))
      opt_max_alloc_mcbs = ::atoi(argv[i] + len);
    else if ((len = 17) && !::strncmp(argv[i], "--listen-backlog=", len))
      opt_listen_backlog = ::atoi(argv[i] + len);
    else if (              !::strcmp(argv[i], "--verbose") || !::strcmp(argv[i], "-v"))
      opt_verbose += 1;
    else if (              !::strcmp(argv[i],  "--check"))
      opt_check = true;
    else if (              !::strcmp(argv[i],  "--no-check"))
      opt_check = false;
#ifdef  MBM_LISTEN_TIMER_MSECS
    else if ((len = 15) && !::strncmp(argv[i], "--listen-timer=", len))
      opt_listen_timer = ::atoi(argv[i] + len);
#endif
    else if (!::strcmp(argv[i],                "--write-thru-persistent-data"))
      opt_write_thru_persistent_data = true;
    else if (!::strcmp(argv[i],                "--no-write-thru-persistent-data"))
      opt_write_thru_persistent_data = false;
    else if (              !::strcmp(argv[i],  "--wha-shuffle"))
      opt_wha_shuffle = true;
    else if (              !::strcmp(argv[i],  "--no-wha-shuffle"))
      opt_wha_shuffle = false;
    else if (              !::strcmp(argv[i],  "--dump-tags"))
      opt_dump_tags = 1;
    else if (              !::strcmp(argv[i],  "--dump-links"))
      opt_dump_tags = 2;
    else if (              !::strcmp(argv[i],  "--dump-tags-value"))
      opt_dump_tags_value  = true;
    else if (              !::strcmp(argv[i],  "--dump-tags-mid"))
      opt_dump_tags_mid   = true;
    else if (              !::strcmp(argv[i],  "--dump-tags-size"))
      opt_dump_tags_size  = true;
    else if (              !::strcmp(argv[i],  "--dump-tags-type"))
      opt_dump_tags_type  = true;
    else if (              !::strcmp(argv[i],  "--dump-tags-all")) {
      opt_dump_tags_mid   = true;
      opt_dump_tags_value = true;
      opt_dump_tags_size  = true;
      opt_dump_tags_type  = true;
    }
    else if (!::strcmp(argv[i], "--version")) {
      fprintf(stderr, "Fusion server (a.k.a. mbm). Version v%d.%d.%d[%s]\n", nf::ver.mini.maj, nf::ver.mini.min, nf::ver.build, nf::ver.name);

      return 0;
    }
    else if (!::strcmp(argv[i], "--help")) {
usage:
      fprintf(stderr, "\
Usage: %s [--ifc=INTERFACE] [--port=PORT] [--config=path-to-config] [--prealloc-mcbs=N] [--max-alloc-mcbs=N]\n\
  --ifc=IP4            - interface mbm listens on;                       default interface is 0.0.0.0 (any available)\n\
  --port=NNN           - port mbm listens on;                            default port is 3001\n\
  --config=PATH        - directory mbm uses as config;                   default config path is current directory\n\
  --prealloc-mcbs=NNN  - mbm pre-allocates this number of mcbs;          default is %d\n\
  --max-alloc-mcbs=NNN - mbm keeps this number of mcbs without releasing them; default is %d\n\
  --listen-backlog=NNN - listen using this backlog;                      default is %d\n\
  --[no-]wha-shuffle     - [do not] randomize clients position in wha\n\
  --[no-]check           - [skip] configuration consistency check\n\
  --[no-]write-thru-persistent-data\n\
                       - [do not] write thru persistent data\n"
#ifdef  MBM_LISTEN_TIMER_MSECS
"\
  --listen-timer=NNN   - _hack_ to use timer period (ms) to force accept connection, 0 - never; default is %d ms\n"
#endif
"\
  --dump-tags          - dump tags\n\
  --dump-tags-value    - add persistent data values\n\
  --dump-tags-mid      - add mid\n\
  --dump-tags-type     - add type\n\
  --dump-tags-size     - add size\n\
  --dump-tags-all      - add all attributes\n\
  --dump-links         - dump linked tags\n\
",
        argv[0], PREALLOCATED_MCBS, 16 * PREALLOCATED_MCBS, MBM_LISTEN_BACKLOG
#ifdef  MBM_LISTEN_TIMER_MSECS
      , MBM_LISTEN_TIMER_MSECS
#endif
        );

      return 1;
    }
    else {
      FUSION_ERROR("Unknown option: %s", argv[i]);
      goto usage;
    }
  }

  switch (opt_dump_tags) {
  case 1:
    FUSION_INFO("dumping all tags...");

    nf::md_check(nf::MD_CHECK_DUMP_TAGS, opt_confdir, opt_dump_tags_value, opt_dump_tags_mid, opt_dump_tags_size, opt_dump_tags_type);

    exit(0);

  case 2:
    FUSION_INFO("dumping linked tags...");

    nf::md_check(nf::MD_CHECK_DUMP_LINKS, opt_confdir, opt_dump_tags_value, opt_dump_tags_mid, opt_dump_tags_size, opt_dump_tags_type);

    exit(0);
  }

  if (opt_prealloc_mcbs > opt_max_alloc_mcbs)
    opt_max_alloc_mcbs = opt_prealloc_mcbs;

  nf::port = ::atoi(port);
  nf::host = ::resolve_address(ifc);

  {
    struct in_addr addr;
    addr.s_addr = nf::host;

    ::fprintf(stdout, "Fusion server (a.k.a. mbm) v%d.%d.%d [%s]\n", nf::ver.mini.maj, nf::ver.mini.min, nf::ver.build, nf::ver.name);

    if (opt_verbose > 0)
      ::fprintf(stdout,
        "Using following options:\n"
        "\tInterface=%s:%d\n"
        "\tConfiguration directory=%s\n"
        "\tPreallocated mcbs=%d\n"
        "\tMax allocated mcbs=%d\n"
        "\tListen backlog=%d\n"
        "\tListen timer=%d ms\n"
        "\tShuffle wait handle array=%s\n"
        "\tWrite-thru persistent data=%s\n",
        ::inet_ntoa(addr),
        nf::port,
        opt_confdir,
        opt_prealloc_mcbs,
        opt_max_alloc_mcbs,
        opt_listen_backlog,
        opt_listen_timer,
        opt_wha_shuffle ? "yes" : "no",
        opt_write_thru_persistent_data ? "yes" : "no"
      );

    if (opt_verbose > 1)
      ::fprintf(stdout,
        "Compiled-in options:\n"
        "\tCHECK_UNIQ_MBPOOL_PUT=" STRINGIFY(CHECK_UNIQ_MBPOOL_PUT) "\n"
        "\tCLOCK_SYNC_MIN_PERIOD=" STRINGIFY(CLOCK_SYNC_MIN_PERIOD) "\n"
        "\tDEBUG_HEAP=" STRINGIFY(DEBUG_HEAP) "\n"
        "\tDISPOSE_MBPOOL=" STRINGIFY(DISPOSE_MBPOOL) "\n"
        "\tMAX_MESSAGE_NAME_LENGTH=" STRINGIFY(MAX_MESSAGE_NAME_LENGTH) "\n"
        "\tMBM_ENFORCE_INIT_PERSISTENT_MESSAGE=" STRINGIFY(MBM_ENFORCE_INIT_PERSISTENT_MESSAGE) "\n"
        "\tMBM_DEFAULT_OUTPUT_QUEUE_LIMIT=" STRINGIFY(MBM_DEFAULT_OUTPUT_QUEUE_LIMIT) "\n"
        "\tMBM_DEFAULT_PROFILE=" STRINGIFY(MBM_DEFAULT_PROFILE) "\n"
        "\tMBM_LISTEN_BACKLOG=" STRINGIFY(MBM_LISTEN_BACKLOG) "\n"
        "\tMBM_DEFAULT_OUTPUT_QUEUE_LIMIT=" STRINGIFY(MBM_DEFAULT_OUTPUT_QUEUE_LIMIT) "\n"
        "\tMBM_PERMISSIVE_PROFILE=" STRINGIFY(MBM_PERMISSIVE_PROFILE) "\n"
        "\tMBM_PQ_SORTING=" STRINGIFY(MBM_PQ_SORTING) "\n"
        "\tMBM_SUBSCRBE_IS_ALWAYS_PUBLISH=" STRINGIFY(MBM_SUBSCRBE_IS_ALWAYS_PUBLISH) "\n"
        "\tMBM_TCP_NO_DELAY=" STRINGIFY(MBM_TCP_NO_DELAY) "\n"
        "\tUSE_VECTORED_IO=" STRINGIFY(USE_VECTORED_IO) "\n"
      );
  }

  ::SetConsoleCtrlHandler(nf::on_break, TRUE);

  nf::result_t e;

  if (opt_check)
    FUSION_ENSURE((e = nf::md_check(nf::MD_CHECK_READONLY, opt_confdir)) == nf::ERR_OK, return 1, ": %s", nf::enumstr(e));

  WSADATA wsaData;

  FUSION_ENSURE(WSAStartup(MAKEWORD(2, 2), &wsaData) == 0, return 1);

  FUSION_ENSURE((e = nf::md_init(opt_confdir)) == nf::ERR_OK, return 1, ": %s", nf::enumstr(e));

  nf::mcbpool.set_prealloc(opt_prealloc_mcbs);
  nf::mcbpool.set_max_alloc(opt_max_alloc_mcbs);

  nf::clients = new nf::clients_t();

  nf::run();

  delete nf::clients;

  nf::md_fini();

  nf::need_cleanup = false;

  ::WSACleanup();

#if DEBUG_HEAP > 0
  google::protobuf::ShutdownProtobufLibrary();
#endif

  return 0;
}
