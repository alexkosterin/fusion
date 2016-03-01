/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_MD_INTERNAL_H
#define		FUSION_MD_INTERNAL_H

#include "include/configure.h"
#include "include/nf.h"
#include "include/nf_internal.h"
#include "include/nf_macros.h"
#include "include/idpool.h"
#include "text_imp.h"
#include <map>
#include <string>

namespace nf {
  // message descriptor ////////////////////////////////////////////////////////
  struct _md_t {
    mid_t			mid_;
    mtype_t	  mtype_;	  	  // event or stream, static or dynamic
    size_t  	size_;		    // message payload size undefined if -1
  };

  struct md_t : public _md_t {
    mutable msecs_t
              atime_,	  	  // access: read
              ctime_, 		  // change: write
              mtime_;		    // meta: ?

    unsigned	temp_:1;      // delete when nopen_ goes zero
    unsigned	xread_:1;     // no more opening for reading
    unsigned	xwrite_:1;    // no more opening for writing
    unsigned	reads_;		    // nr of messages opened for read
    unsigned	writes_;      // nr of messages opened for write
                            // for MT_PERSISTENT events only

    // ** persistentcy **
    mutable cid_t   last_sender_;  // last client that sent data
    mutable size_t  last_len_;
    mutable void*   last_data_;
    // ** persistentcy **

    imp_t     imp_;         // concrete implementation of 'message file'

    md_t(::HANDLE hfile);
    ~md_t();

    result_t create(mtype_t type, size_t size);
    result_t read();
    result_t write();
    result_t remove();
    result_t close(bool read, bool write, bool excl_read, bool excl_write);

    void data(size_t len, void* d) const {
      FUSION_ASSERT(!len || d);

      free(last_data_);
      last_len_ = len;

      if (last_len_) {
        last_data_ = ::malloc(last_len_);

        FUSION_ASSERT(last_data_);

        ::memcpy(last_data_, d, last_len_);
      }
      else
        last_data_ = 0;
    }
  };

  struct pcb_t;         // profile control block
  struct ccb_t;         // client control block
  struct md_t;          // message descriptor

#if	defined(WIN32)
  struct pcbs_t: std::map<const char*, pcb_t*, striless> {
    ~pcbs_t();
  };
#else
  typedef std::map<const char*, pcb_t*, strless> pcbs_t;
#endif
  typedef std::map<cid_t, ccb_t*>       ccbs_t;
  typedef std::map<mid_t, md_t*>        mdescs_t;
  typedef idpool_t<16, 4, 1024, -1>     midpool_t;

  //////////////////////////////////////////////////////////////////////////////
  struct client_md_t {
    md_t*       md_;
    unsigned	  oflags_;

#if (MD_KEEP_NAME > 0)
    std::string name_;    // as passed by client
#endif
#if (MD_KEEP_PATH > 0)
    std::string path_;    // as it was resolved using profile
#endif
  };

  typedef std::map<mid_t, client_md_t> ccb_mds_t;

  // client control block //////////////////////////////////////////////////////
  struct ccb_t {
    cid_t       cid_;
    char*       name_;        // client friendly name/description
    pcb_t*      dp_;          // default profile
    pcbs_t      profiles_;    // all profiles by name
    ccb_mds_t   omsgs_;       // open message descriptors

    ccb_t(cid_t cid, const char* name, pcb_t* dp);
   ~ccb_t();
    static ccb_t* create(cid_t cid, const char* name, const char* profile);
  };
}

#endif  FUSION_MD_INTERNAL_H
