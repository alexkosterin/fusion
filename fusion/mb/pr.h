/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_PR_H
#define		FUSION_PR_H

#include <map>
#include <include/lock.h>

#if	defined(WIN32)
# define NOGDI
# include <windows.h>
#	include <process.h>
#endif

namespace nf {
  struct mcb_t;

  struct pending_req_t;
  struct internal_client_t;

  struct pending_req_t {
    HANDLE        event_;
    const mcb_t*  req_;
    mcb_t*        rep_;

    pending_req_t(const mcb_t* req, HANDLE event);
    pending_req_t(const mcb_t* req);
    ~pending_req_t();
    bool wait(uint32_t msecs);
    virtual void completion() {}
  };

  struct pending_reqs_t : std::map<seq_t, pending_req_t*> {
    _srwlock_t    lock_;

    pending_req_t* get(mcb_t* mcb);
    pending_req_t* get(seq_t seq);
    void put(pending_req_t& pr);
  };
}

#endif		//FUSION_PR_H
