/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_MCB_POOL_H
#define		FUSION_MCB_POOL_H

#include "include/nf_internal.h"
#include "include/lock.h"
#include <vector>

namespace nf {
  struct mcb_t;

  struct mcbpool_t : std::vector<mcb_t*> {
    mcbpool_t(size_t prealloc = 0, size_t max_free = 0);
    ~mcbpool_t();

    mcb_t* get();
    mcb_t* incoming_get(size_t len);
    mcb_t* outgoing_get(mid_t mid, cid_t src, cid_t dst, seq_t seq, bool request = false, seq_t req_seq = 0, uint32_t timeout = 0);
    void put(mcb_t*);
    void set_prealloc(size_t);
    void set_max_alloc(size_t);

    _srwlock_t  lock_;
    size_t      max_free_;
  };
}
#endif    //FUSION_MCB_POOL_H
