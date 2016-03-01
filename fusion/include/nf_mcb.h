/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_NF_MCB__H
#define		FUSION_NF_MCB__H

#include <include/nf.h>
#include <include/nf_internal.h>

#ifdef	WIN32
# define NOGDI
# define _WINSOCKAPI_
# include <windows.h>
#endif

namespace nf {
#pragma pack(push, mcb, 1)
  //////////////////////////////////////////////////////////////////////////////
  struct mcb_xfer_t {       // message control block: wire transfered
    mid_t			      mid_;							// message id/topic/channel etc
    cid_t			      src_;							// message source
    cid_t			      dst_;							// message destination: (ALL|SUB|GRP|CID)|NOSELF
    seq_t			      seq_;							// message sequence number, generated on originsator side
    msecs_t		      org_;							// timestamp message created

    // request & reply; note: message can be both request and reply
    seq_t			      req_seq_;					// is a reply if != 0
    bool			      request_;				  // is a request if true
  };

  //////////////////////////////////////////////////////////////////////////////
  struct mcb_t : mcb_xfer_t { // message control block
    pri_t			      prio_;						// priority: used for pq sorting
    uint32_t	      timeout_;					// timeout msecs (~50 days)

    unsigned	      rc_;  				    // reference count; @atomic where it should be atomic: yes mb, no mbm@
    bool            expired_;
    ::HANDLE        wevent_;          // write completion event
//  unsigned short  type_: 4;         // ???
    unsigned short	len_;					    // if len < sizeof (u) data is embedded

    union {
      // -- beginning of embedded data --
      char          data_[sizeof(double)];
      bool          bool_;
      uint8_t       char_;
      int16_t       short_;           // @ENDIANESS@
      uint16_t      ushort_;          // @ENDIANESS@
      int32_t       int_;             // @ENDIANESS@
      uint32_t      uint_;            // @ENDIANESS@
      msecs_t       msecs_;           // @ENDIANESS@
      float         float_;           // @ENDIANESS@
      double        double_;          // @ENDIANESS@

      // convinience
      mid_t         mid_;             // @ENDIANESS@
      cid_t         cid_;             // @ENDIANESS@
      result_t	    error_;	  			  // @ENDIANESS@

      struct {
        mid_t       mid_;             // @ENDIANESS@
        int16_t     rnr_;             // @ENDIANESS@ number of readers; if -1 then 1 exclusive reader
        int16_t     wnr_;             // @ENDIANESS@ number of writers; if -1 then 1 exclusive writer
      } notify;                       // note: if client openes for r/w, then both counters are bumped

      // -- or pointer to data buffer
      void*         pdata_;
    } u;

    size_t xfer_size();
    void* data()              { return len_ > sizeof u ? u.pdata_ : &u.data_; }
    const void* data() const  { return len_ > sizeof u ? u.pdata_ : &u.data_; }
    void data(const void* d, size_t len);
    void reset_data();
    void init(mid_t mid, cid_t src, cid_t dst, seq_t seq, bool request = false/*MD_SYS_NONE*/, seq_t req_seq = 0, uint32_t timeout = 0);
    void incoming_init(size_t len);
    void copy(void* buff);
    void fini();
    void set_prio();
    time_t org_time();

#if  (MCBPOOL_KEEP_COUNT > 0)
    static size_t nr__;

    mcb_t();
    mcb_t(const mcb_t*);
    ~mcb_t();
#endif
  };
#pragma pack(pop, mcb)
}

#endif FUSION_NF_MCB__H
