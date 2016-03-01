/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_PQ_H
#define		FUSION_PQ_H

//#include <set>
#include <list>
#include <algorithm>
#include "include/lock.h"

  //////////////////////////////////////////////////////////////////////////////
template<typename E, bool SORT(E, E)> struct pq_t {
  typedef	std::list<E>	repr_t;

  repr_t	    queue_;
  _srwlock_t	lock_;

public:
  void put(E e) {	//////////////////////////////////////////////////////////////
    wlock_t _(lock_);

    queue_.push_back(e);

#if (MBM_PQ_SORTING > 0)
    std::stable_sort(queue_.begin(), queue_.end(), SORT);
#endif
  }

  bool get(E& e) {	////////////////////////////////////////////////////////////
    wlock_t wlock(lock_);

    if (!queue_.empty()) {
      e = queue_.front();
      queue_.pop_front();

      return true;
    }

    return false;
  }

  bool empty() { ///////////////////////////////////////////////////////////////
    wlock_t rlock(lock_);

    return queue_.empty();
  }
};

#endif	//FUSION_PQ_H
