/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include "include/nf.h"
#include "include/nf_macros.h"
#include "include/nf_internal.h"
#include "include/mb.h"
#include "pr.h"

namespace nf {
  //////////////////////////////////////////////////////////////////////////////
  pending_req_t::pending_req_t(const mcb_t* req, HANDLE evt) : event_(evt), req_(req), rep_(0)
  {
    FUSION_ASSERT(req);
    FUSION_ASSERT(event_);

    ::ResetEvent(event_);
  }

  __declspec(thread) HANDLE event__ = 0;

  //////////////////////////////////////////////////////////////////////////////
  pending_req_t::pending_req_t(const mcb_t* req) : req_(req), rep_(0)
  {
    if (event__ == 0)
      event__ = ::CreateEvent(NULL, FALSE, FALSE, NULL);

    event_ = event__;

    FUSION_ASSERT(req);
    FUSION_ASSERT(event_);
  }

  //////////////////////////////////////////////////////////////////////////////
  pending_req_t::~pending_req_t() {}

  //////////////////////////////////////////////////////////////////////////////
  bool pending_req_t::wait(uint32_t msecs) {
    while (1)
      switch (::WaitForSingleObjectEx(event_, msecs, TRUE)) {
      case WAIT_OBJECT_0:       return true;
      case WAIT_IO_COMPLETION:  continue;
      default:                  return false;
      }
  }

  //////////////////////////////////////////////////////////////////////////////
  pending_req_t* pending_reqs_t::get(seq_t seq) {
    rlock_t lock(lock_);

    pending_reqs_t::iterator I = find(seq);

    if (I != end()) {
      pending_req_t* pr = I->second;

      FUSION_ASSERT(pr);
      FUSION_ASSERT(pr->event_);

      erase(I);

      return pr;
    }

    return 0;
  }

  //////////////////////////////////////////////////////////////////////////////
  pending_req_t* pending_reqs_t::get(mcb_t* rep) {
    FUSION_ASSERT(rep);

    return get(rep->req_seq_);
  }

  //////////////////////////////////////////////////////////////////////////////
  void pending_reqs_t::put(pending_req_t& pr) {
    wlock_t lock(lock_);

    FUSION_ASSERT(find(pr.req_->seq_) == end());

    insert(std::make_pair(pr.req_->seq_, &pr));
  }
}

