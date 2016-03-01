/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <include/configure.h>
#include <include/nf_macros.h>
#include <include/nf_mcb.h>
#include <include/tsc.h>
#include <include/enumstr.h>

#include <stdio.h>
#include <stdlib.h>

namespace nf {
  //////////////////////////////////////////////////////////////////////////////
  size_t mcb_t::xfer_size() {
    return sizeof(mcb_xfer_t) + len_;
  }

  //////////////////////////////////////////////////////////////////////////////
  void mcb_t::data(const void* d, size_t len) {
    len_ = len;

    if (len > sizeof u) {
      u.pdata_ = ::malloc(len);
      ::memcpy(u.pdata_, d, len);
    }
    else
      ::memcpy(&u.data_, d, len);
  }

  //////////////////////////////////////////////////////////////////////////////
  void mcb_t::reset_data() {
    if (len_ > sizeof u)
      ::free(u.pdata_);

    len_ = 0;
  }

  //////////////////////////////////////////////////////////////////////////////
  void mcb_t::init(mid_t mid, cid_t src, cid_t dst, seq_t seq, bool request, seq_t req_seq, uint32_t timeout) {
    // common
    org_      = now_msecs();
    seq_      = seq;
//  rc_       = 1;
    expired_  = false;
    wevent_   = INVALID_HANDLE_VALUE;

    set_prio();

    // routing
    mid_      = mid;
    dst_      = dst;
    src_      = src;

    // timeout
    timeout_  = timeout;

    // request/reply
    request_  = request;
    req_seq_  = req_seq;

    // data
    len_      = 0;
    u.pdata_  = 0;
  }

  //////////////////////////////////////////////////////////////////////////////
  void mcb_t::incoming_init(size_t len) {
//    rc_       = 1;
    timeout_  = 0;
    expired_  = false;
    wevent_   = INVALID_HANDLE_VALUE;

    set_prio();

    // request/reply
    req_seq_  = 0;
    request_  = false;

    // data
    len_      = len - sizeof mcb_xfer_t;
    u.pdata_  = len_ > sizeof u ? ::malloc(len_) : 0;
  }

  //////////////////////////////////////////////////////////////////////////////
  void mcb_t::copy(void* buff) {
    ::memmove(this, (char*)buff, sizeof mcb_xfer_t);

    // optionally copy data
    if (len_ > sizeof u) {
      FUSION_ASSERT(u.pdata_);

      ::memmove(u.pdata_, (char*)buff + sizeof mcb_xfer_t, len_);
    }
    else if (len_ > 0)
      ::memmove(&u.data_, (char*)buff + sizeof mcb_xfer_t, len_);

    FUSION_DEBUG("mid=%s src=%d dst=%d seq=%d len=%d", enumstr((_md_sys_t)mid_), src_, dst_, seq_, len_);
  }

  //////////////////////////////////////////////////////////////////////////////
  void mcb_t::fini() {
    if (len_ > sizeof u) {
      FUSION_ASSERT(u.pdata_);

      ::free(u.pdata_);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  time_t mcb_t::org_time() {
    return msecs_to_unix(org_);
  }

  //////////////////////////////////////////////////////////////////////////////
  void mcb_t::set_prio() {
		switch (mid_) {
    case MD_SYS_TIMESYNC_REQUEST:
    case MD_SYS_TIMESYNC_REPLY:
			prio_ = PRIORITY_TIMESYNC;
      break;

    default:
      prio_ = PRIORITY_LO;
      break;
    }
  }

#if (MCBPOOL_KEEP_COUNT > 0)
  size_t mcb_t::nr__;

  mcb_t::mcb_t() : rc_(0) {
    ++nr__;
  }

  mcb_t::mcb_t(const mcb_t* m) {
    ++nr__;

    ::memcpy(this, m, sizeof mcb_t);
    rc_ = 0;
  }

  mcb_t::~mcb_t() {
    --nr__;
  }
#endif
}
