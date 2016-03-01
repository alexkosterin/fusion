/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include "include/nf.h"
#include "include/nf_macros.h"
#include "include/mcbpool.h"
#include "include/enumstr.h"

namespace nf {
  //////////////////////////////////////////////////////////////////////////////
  mcbpool_t::mcbpool_t(size_t prealloc, size_t max_free) {
    if (max_free < prealloc)
      max_free_ = prealloc;

    for (size_t i = 0; i < prealloc; ++i) {
      mcb_t* mcb = new mcb_t;

#if (MCBPOOL_KEEP_COUNT == 0)
      mcb->rc_ = 0;
#endif
      push_back(mcb);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  mcbpool_t::~mcbpool_t() {
#if DISPOSE_MBPOOL || DEBUG_HEAP
    wlock_t lock(lock_);

    FUSION_DEBUG("freeing %d mcbs", size());

    for (const_iterator I = cbegin(); I != cend(); ++I)
      delete *I;
#endif
  }

//////////////////////////////////////////////////////////////////////////////
  mcb_t* mcbpool_t::get() {
    mcb_t* mcb;

    {
#if defined(COMPILING_FUSION_MB)
      wlock_t lock(lock_);
#endif

      if (empty())
        mcb = new mcb_t;
      else {
        mcb = back();

        if (mcb->rc_ > 0) {
          FUSION_ERROR("rc=%d mid=%s src=%d dst=%d sq=%ld len=%d", mcb->rc_, enumstr((_md_sys_t)mcb->mid_), mcb->src_, mcb->dst_, mcb->seq_, mcb->len_); //@

          mcb->rc_ = 0;
        }

        pop_back();
      }
    }

    mcb->rc_ = 1;

    FUSION_VERIFY(mcb); //@

    return mcb;
  }

//////////////////////////////////////////////////////////////////////////////
  mcb_t* mcbpool_t::incoming_get(size_t len) {
    mcb_t* mcb = get();

    mcb->incoming_init(len);

    return mcb;
  }

  //////////////////////////////////////////////////////////////////////////////
  mcb_t* mcbpool_t::outgoing_get(mid_t mid, cid_t src, cid_t dst, seq_t seq, bool request, seq_t req_seq, uint32_t timeout) {
    mcb_t* mcb = get();

    mcb->init(mid, src, dst, seq, request, req_seq, timeout);

    return mcb;
  }

  //////////////////////////////////////////////////////////////////////////////
  void mcbpool_t::put(mcb_t* mcb) {
    FUSION_ASSERT(mcb);
//  FUSION_INFO("mid=%d rc=%d src=%d dst=%d seq=%d len=%d", mcb->mid_, mcb->rc_, mcb->src_, mcb->dst_, mcb->seq_, mcb->len_); //@
    FUSION_VERIFY(!mcb->rc_); //@

#if  (CHECK_UNIQ_MBPOOL_PUT > 0)
    {
#if defined(COMPILING_FUSION_MB)
      wlock_t lock(lock_);
#endif

      for (const_iterator I = cbegin(); I != cend(); ++I)
      FUSION_ASSERT(*I != mcb);
    }
#endif

    mcb->fini();

#if defined(COMPILING_FUSION_MB)
    wlock_t lock(lock_);
#endif

    if (size() < max_free_)
      push_back(mcb);
    else
      delete mcb;
  }

  //////////////////////////////////////////////////////////////////////////////
  void mcbpool_t::set_prealloc(size_t val) {
#if defined(COMPILING_FUSION_MB)
    wlock_t lock(lock_);
#endif

    if (capacity() < val)
      reserve(val);

    size_t i;

    try {
      for (i = size(); i < val; ++i) {
        mcb_t* mcb = new mcb_t;

#if (MCBPOOL_KEEP_COUNT == 0)
        mcb->rc_ = 0;
#endif
        push_back(mcb);
      }
    }
    catch (...) {
      FUSION_FATAL("Too many mcbs to allocate=%d! Try %d.", val, i - 1);
    }

    if (max_free_ < val)
      max_free_ = val;
  }

  //////////////////////////////////////////////////////////////////////////////
  void mcbpool_t::set_max_alloc(size_t val) {
    max_free_ = val;

    if (size() > max_free_)
      set_prealloc(max_free_);
  }
}
