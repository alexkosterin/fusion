/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_SUBS_H
#define		FUSION_SUBS_H

#include "include/nf.h"
#include "include/configure.h"

#include <map>

namespace nf {
  struct client_t;

  struct sub_dcr_t {
    cid_t     cid_;
    bool      publish_;
  };

  //////////////////////////////////////////////////////////////////////////////
  struct subs_t : std::multimap<mid_t, sub_dcr_t> {
    sub_dcr_t* get(mid_t mid, cid_t cid);
    bool put(mid_t mid, bool publish, cid_t cid);
    bool del(mid_t mid, cid_t cid);
    bool del(cid_t cid);
  };
}

#endif		//FUSION_SUBS_H
