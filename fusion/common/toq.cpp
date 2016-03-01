/*
 *	FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include "include/toq.h"
#include "include/tsc.h"
#include "mb/mb_.h"

#ifdef	WIN32
# define WAIT_FOREVER 0x7FFFFFFFFFFFFFFFLL
#endif

namespace nf {
	template <typename MCB>
	toq_t<MCB>::toq_t() { ////////////////////////////////////////////////////////
#ifdef	WIN32
		timer_ = ::CreateWaitableTimer(
			NULL,               // default security attributes
			TRUE,               // manual-reset event
			NULL								// timer name
		);

		LARGE_INTEGER liDueTime;

		liDueTime.QuadPart = WAIT_FOREVER;

		BOOL rc = SetWaitableTimer(timer_, &liDueTime, 0, NULL, NULL, 0);

		ASSERT(rc);
#else
#endif
	}

	template <typename MCB>
	toq_t<MCB>::~toq_t() { ///////////////////////////////////////////////////////
#ifdef	WIN32
		ASSERT(timer_ != NULL);

		::CloseHandle(timer_);
#else
#endif
	}

	template <typename MCB>
	void toq_t<MCB>::put(msecs_t t, MCB& m) { ///////////////////////////////////
		ASSERT(timer_ != NULL);
		ASSERT(m.rc_ > 0);

		wlock_t lock(lock_);

		queue_.insert(std::make_pair<msecs_t, mb::mcb_t*>(t, &m));
		resched_();
	}

	template <typename MCB>
	bool toq_t<MCB>::get(MCB** m) { //////////////////////////////////////////////
		ASSERT(timer_ != NULL);

		wlock_t lock(lock_);

		if (queue_.empty())
			return false;

		queue_t::iterator I = queue_.begin();
		msecs_t next				= I->first;

#ifdef	WIN32
		msecs_t now = now_msecs();
		bool ready	= (now >= next);

		if (ready) {
			*m = I->second;

			ASSERT(m);
			ASSERT(m->rc_ > 0);

			queue_.erase(I);
		}

		resched_();

		return ready;
#else
#endif
	}

	template <typename MCB>
	bool toq_t<MCB>::rem(MCB& m) { ///////////////////////////////////////////////
		ASSERT(m.rc_ > 0);

		wlock_t lock(lock_);

		for (queue_t::iterator I = queue_.begin(); I != queue_.end(); ++I)
			if (I->second == &m) {
				queue_.erase(I);
				resched_();

				return true;
			}

		return false;
	}

#ifdef	WIN32
	template <typename MCB>
	HANDLE toq_t<MCB>::wait_handle() { ///////////////////////////////////////////
		ASSERT(timer_ != NULL);

		return timer_;
	}
#else
#endif

	template <typename MCB>
	void toq_t<MCB>::resched_() {
		LARGE_INTEGER liDueTime;

		if (queue_.empty()) {
			liDueTime.QuadPart = WAIT_FOREVER;
			BOOL rc = SetWaitableTimer(timer_, &liDueTime, 0, NULL, NULL, 0);

			ASSERT(rc);
		}
		else {
			queue_t::iterator I	= queue_.begin();
			msecs_t next				= I->first;
			msecs_t now					= now_msecs();

			if (now < next)
				liDueTime.QuadPart = 10000LL * (now - next);
			else
				liDueTime.QuadPart = 0;	// activate timer now

			BOOL rc = SetWaitableTimer(timer_, &liDueTime, 0, NULL, NULL, 0);
		}
	}
}	// nf
