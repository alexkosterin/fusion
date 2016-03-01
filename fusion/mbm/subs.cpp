/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include "subs.h"
#include "include/nf_internal.h"

namespace nf {
  //////////////////////////////////////////////////////////////////////////////
  sub_dcr_t* subs_t::get(mid_t mid, cid_t cid) {
    std::pair<subs_t::iterator, subs_t::iterator> p = equal_range(mid);

    for (subs_t::iterator I = p.first; I != p.second; ++I)
      if (I->second.cid_ == cid)
        return &I->second;

    switch (mid) {
    case MD_SYS_ECHO_REQUEST:
    case MD_SYS_ECHO_REPLY:
    case MD_SYS_STOP_REQUEST:
    case MD_SYS_TERMINATE_REQUEST:
      static sub_dcr_t sys_dcr__ = { CID_ALL, true };
      return &sys_dcr__;
    }

    return 0;
  }

  ////////////////////////////////////////////////////////////////////////////
  bool subs_t::put(mid_t mid, bool publish, cid_t cid) {
    sub_dcr_t sd;

    sd.publish_ = publish;
    sd.cid_     = cid;

    if (get(mid, cid))
      return false;

    insert(std::make_pair(mid, sd));

    return true;
  }

  ////////////////////////////////////////////////////////////////////////////
  bool subs_t::del(mid_t mid, cid_t cid) {
    for (subs_t::iterator I = begin(), E = end(); I != E; ++I)
      if (mid == I->first) {
        std::pair<subs_t::iterator, subs_t::iterator> p = equal_range(mid);

        for (subs_t::iterator J = p.first; J != p.second; ++J)
          if (J->second.cid_ == cid) {
            erase(J);

            return true;
          }
      }

    return false;
  }

  ////////////////////////////////////////////////////////////////////////////
  bool subs_t::del(cid_t cid) {
    size_t nr = 0;
more:
    for (subs_t::iterator I = begin(), E = end(); I != E; ++I) {
      mid_t mid = I->first;

      std::pair<subs_t::iterator, subs_t::iterator> p = equal_range(mid);

      for (subs_t::iterator J = p.first; J != p.second; ++J)
        if (J->second.cid_ == cid) {
          erase(J);
          ++nr;

          goto more;
        }
    }

    return nr > 0;
  }
}
