/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_NF__H
#define		FUSION_NF__H

#include "configure.h"
#include "include/tsc.h"

#include <string.h>

#ifdef	WIN32
# define NOGDI
# define _WINSOCKAPI_
# include <windows.h>
typedef void *WSAEVENT;
#endif

#define   FUSION_PACKET_HEADER_LEN (2)

namespace nf {
  // system message descriptors ////////////////////////////////////////////////
  enum _md_sys_t {
    MD_SYS_ANY	= 0xFFFF,       // wait for any reply
    MD_SYS_NONE	= 0,					  // do not wait for reply

    MD_SYS_STATUS = 1,				  // acknowledgement with ok/error

    // echo
    MD_SYS_ECHO_REQUEST,
    MD_SYS_ECHO_REPLY,

    // time synchronization
    MD_SYS_TIMESYNC_REQUEST,
    MD_SYS_TIMESYNC_REPLY,

    // client registration/termination
    MD_SYS_REGISTER_REQUEST,
    MD_SYS_REGISTER_REPLY,
    MD_SYS_UNREGISTER_REQUEST,
    MD_SYS_UNREGISTER_REPLY,

    MD_SYS_STOP_REQUEST,
//  MD_SYS_STOP_REPLY, => MD_SYS_STATUS
    MD_SYS_TERMINATE_REQUEST,

    MD_SYS_QUERY_CLIENTS_REQUEST,
    MD_SYS_QUERY_CLIENTS_REPLY,

    MD_SYS_QUERY_CLIENT_BY_ID_REQUEST,
    MD_SYS_QUERY_CLIENT_REPLY,

    //MD_SYS_QUERY_USER_REQUEST,
    //MD_SYS_QUERY_USER_REPLY,

    MD_SYS_QUERY_GROUP_REQUEST,
    MD_SYS_QUERY_GROUP_REPLY,

    // messages
    MD_SYS_MOPEN_REQUEST,
    MD_SYS_MOPEN_REPLY,
    MD_SYS_MCLOSE_REQUEST,
    MD_SYS_MUNLINK_REQUEST,   // unlink by name
    MD_SYS_MUNLINK2_REQUEST,  // unlink by mid
    MD_SYS_MLINK_REQUEST,
    MD_SYS_MMOVE_REQUEST,
    MD_SYS_MLIST_REQUEST,
    MD_SYS_MLIST_REPLY,

    //MD_SYS_MSTAT_BY_ID_REQUEST,
    //MD_SYS_MSTAT_BY_NAME_REQUEST,
    //MD_SYS_MSTAT_REPLY,

    //// message attributes
    //MD_SYS_MATTR_READ_REQUEST,
    //MD_SYS_MATTR_READ_REPLY,

    //MD_SYS_MATTR_WRITE_REQUEST,

    //MD_SYS_MATTR_BULK_READ_REQUEST,
    //MD_SYS_MATTR_BULK_READ_REPLY,

    //MD_SYS_MATTR_BULK_WRITE_REQUEST,

    // subscription
    MD_SYS_SUBSCRIBE_REQUEST,
    MD_SYS_UNSUBSCRIBE_REQUEST,

    // notification
    MD_SYS_NOTIFY_OPEN,
    MD_SYS_NOTIFY_SUBSCRIBE,
    MD_SYS_NOTIFY_CONFIGURE,

    // sys info
    MD_SYS_SYSINFO_REQUEST, // need better name
    MD_SYS_SYSINFO_REPLY,   // need better name

    MD_SYS_LAST_ = 1023,
  };

  enum _pri_t : pri_t {
    PRIORITY_LO		    = (pri_t)0xFFFFFFFF,
    PRIORITY_HI		    = (pri_t)0x00000000,
    PRIORITY_TIMESYNC = PRIORITY_HI,
  };

  struct strless {
    bool operator()(const char* a, const char* b) const {
      return ::strcmp(a, b) < 0;
    }
  };

  struct striless {
    bool operator()(const char* a, const char* b) const {
      return ::stricmp(a, b) < 0;
	  }
  };
}

// @@@@ MCB & DATA life cycle management @@@@
#if defined(COMPILING_FUSION_MB)
#define RELEASE(P,M)  { if (!InterlockedDecrement(&M->rc_)) P.put(M); }
#define REFERENCE(M)  { InterlockedIncrement(&M->rc_); }
#else
#define RELEASE(P,M)  { if (!--M->rc_) P.put(M); }
#define REFERENCE(M)  { ++M->rc_; }
#endif

#include <include/nf_mcb.h>

#endif		//FUSION_NF__H
