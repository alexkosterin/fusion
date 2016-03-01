/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_TOQ_H
#define		FUSION_TOQ_H

#if	defined(WIN32)
# define NOGDI
# include <windows.h>
#else
#endif

#include "include/configure.h"
#include "include/lock.h"
#include "include/tsc.h"
#include <map>

#ifdef	WIN32
# define WAIT_FOREVER 0x7FFFFFFFFFFFFFFFLL
#endif

namespace nf {

  struct mcb_t;

#if DEBUG_HEAP > 0
  extern void release(mcb_t*);
#endif

  template <typename MCB> class toq_t {
  public:
    toq_t() {
#ifdef	WIN32
      timer_ = ::CreateWaitableTimer(
        NULL,               // default security attributes
        TRUE,               // manual-reset event
        NULL                // timer name
      );

      FUSION_VERIFY(timer_);

      LARGE_INTEGER liDueTime;

      liDueTime.QuadPart = WAIT_FOREVER;

      BOOL rc = SetWaitableTimer(timer_, &liDueTime, 0, NULL, NULL, 0);

      FUSION_VERIFY(rc);
#else
#endif
    }

    ~toq_t() {
#ifdef	WIN32
    FUSION_ASSERT(timer_);

    ::CloseHandle(timer_);
#endif
#if DEBUG_HEAP > 0
    for (auto I = queue_.cbegin(), E = queue_.cend(); I != E; ++I)
      while (I->second->rc_)
        release(I->second);
#endif
    }

    void put(msecs_t msecs, MCB* m) {
      FUSION_ASSERT(m);
      FUSION_ASSERT(timer_ != NULL);
      FUSION_ASSERT(m->rc_ > 0);
      FUSION_ASSERT(!m->expired_);

      wlock_t lock(lock_);

      queue_.insert(std::make_pair(now_msecs() + msecs, m));
      resched_();
    }

    bool get(MCB*& m) {
      FUSION_ASSERT(timer_ != NULL);

      wlock_t lock(lock_);

      if (queue_.empty())
        return false;

      queue_t::iterator I = queue_.begin();
      msecs_t next				= I->first;

#ifdef	WIN32
      msecs_t now = now_msecs();
      bool ready	= (now >= next);

      if (ready) {
        m = I->second;

        FUSION_ASSERT(m);
        FUSION_ASSERT(m->rc_ > 0);
        FUSION_ASSERT(!m->expired_);

        m->expired_ = true;
        queue_.erase(I);
      }

      resched_();

      return ready;
#else
#endif
    }

    bool del(MCB& m) {
//      FUSION_ASSERT(m.rc_ > 0);

      wlock_t lock(lock_);

      for (queue_t::iterator I = queue_.begin(), E = queue_.end(); I != E; ++I)
        if (I->second == &m) {
          queue_.erase(I);
          resched_();

          return true;
        }

      return false;
    }

#ifdef	WIN32
    HANDLE wait_handle() {
      FUSION_ASSERT(timer_ != NULL);

      return timer_;
  }
#else
#endif

  private:
    typedef std::multimap<msecs_t, MCB*>	queue_t;

#ifdef	WIN32
    _srwlock_t	lock_;
    queue_t			queue_;
    ::HANDLE		timer_;

    void resched_() {
      LARGE_INTEGER liDueTime;

      if (queue_.empty()) {
        liDueTime.QuadPart = WAIT_FOREVER;
        BOOL rc = ::SetWaitableTimer(timer_, &liDueTime, 0, NULL, NULL, 0);

        FUSION_ASSERT(rc);
      }
      else {
        queue_t::iterator I	= queue_.begin();
        msecs_t next				= I->first;
        msecs_t now					= now_msecs();

        //FILETIME now;
        //::GetSystemTimeAsFileTime(&now);

        if (now < next)
          liDueTime.QuadPart = 10000LL * (now - next);
        else
          liDueTime.QuadPart = 0;	// activate timer now

        BOOL rc = ::SetWaitableTimer(timer_, &liDueTime, 0, NULL, NULL, 0);
      }
    }
#else
#endif
  };
}

#endif		//FUSION_TOQ_H

