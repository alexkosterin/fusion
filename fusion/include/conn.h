/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_CONN_H
#define		FUSION_CONN_H

#include "include/nf_macros.h"
#include "include/mcbpool.h"
#include "include/lock.h"
#include <list>

#define INIT_SEND_BUF (4096)
#define MAX_RECIEVE_BUF (0)

namespace nf {
  struct mcbpool_t;

  struct conn_t {
    typedef std::list<mcb_t*> wqueue_t;

    char*       rd_buff_;
    size_t	    rd_alloc_, rd_unproc_;

    size_t      wremain_;
    wqueue_t    wqueue_;

    mcbpool_t*  mcbpool_;

    _srwlock_t  lock_;

    struct {
      uint32_t  period_;
      msecs_t   clock_delta_;   // difference between server & client clocks -> (S - C)
      uint32_t  latency_;       // latency for round trip
    } clock_sync_;

    conn_t(mcbpool_t* p) :
      rd_buff_((char*)malloc(INIT_SEND_BUF)), rd_alloc_(INIT_SEND_BUF), rd_unproc_(0), wremain_(0), mcbpool_(p)
    {
      clock_sync_.clock_delta_  = 0;
      clock_sync_.latency_      = 0;
      clock_sync_.period_       = 0;
    }

    virtual ~conn_t() {
      wlock_t lock(lock_);

      while (!wqueue_.empty()) {
        RELEASE((*mcbpool_), wqueue_.front());
        wqueue_.pop_front();
      }

      ::free(rd_buff_);
    };

    virtual bool open() = 0;
    virtual bool close() = 0;

    virtual bool availread(size_t&) = 0;
    virtual bool read(size_t&, void*) = 0;
    virtual bool resume_write() = 0;
    virtual void shutdown() = 0;

    ////////////////////////////////////////////////////////////////////////////
    size_t packet_size(char* buff) {
      return (size_t)((unsigned char*)rd_buff_)[0] + (size_t)(((size_t)((unsigned char*)rd_buff_)[1]) << 8);
    }

  private:
    virtual bool write(size_t&, const void*) = 0;

    ////////////////////////////////////////////////////////////////////////////
    bool _read_mcb_helper(mcb_t** mcb) {
      if (FUSION_PACKET_HEADER_LEN < rd_unproc_) {
        size_t len = packet_size(rd_buff_) + FUSION_PACKET_HEADER_LEN;

        FUSION_DEBUG("len=%d rd_unproc_=%d", len, rd_unproc_);
        FUSION_ASSERT(len >= sizeof mcb_xfer_t);

        if (rd_unproc_ >= len) {
          *mcb = mcbpool_->incoming_get(len - FUSION_PACKET_HEADER_LEN);

          (*mcb)->copy(rd_buff_ + FUSION_PACKET_HEADER_LEN);
          (*mcb)->set_prio();
          clock_client2server(*mcb);

          rd_unproc_ -= len;
          ::memcpy(rd_buff_, rd_buff_ + len, rd_unproc_);
        }
      }

      if (FUSION_PACKET_HEADER_LEN < rd_unproc_) {
        size_t len = FUSION_PACKET_HEADER_LEN + packet_size(rd_buff_);

        return rd_unproc_ >= len;
      }

      return false;
    }

  public:
    ////////////////////////////////////////////////////////////////////////////
    /* Use pattern:
     * do {
     *   mcb_t* mcb;
     *   bool rc = read_mcb(&mcb);
     *
     *   if (mcb)
     *     pq->add(mcb);
     * } while (rc);
     */
    bool read_mcb(bool first, mcb_t** mcb) {
      size_t len;

      *mcb = 0;

      _read_mcb_helper(mcb);

      if (*mcb)
        return true;
      else if (!first)
        return false;
      else if (!availread(len))
        return false;

      if (MAX_RECIEVE_BUF && len > MAX_RECIEVE_BUF)
          len = MAX_RECIEVE_BUF;

      if (rd_alloc_ < rd_unproc_ + len) {
          rd_alloc_ = rd_unproc_ + len;

          char* t = (char*)::realloc(rd_buff_, rd_alloc_);

          FUSION_VERIFY(t);

          rd_buff_ = t;
      }

      size_t read_len = len;

      if (!read(read_len, rd_buff_ + rd_unproc_))
        return false;

      FUSION_DEBUG("read len=%d", read_len);

      if (!read_len) // @@win@@
        return false;

      FUSION_ASSERT(read_len == len);

      rd_unproc_ += read_len;

      return _read_mcb_helper(mcb);
    }

    ////////////////////////////////////////////////////////////////////////////
    bool write_mcb(mcb_t* mcb) {
      {
        wlock_t lock(lock_);

        REFERENCE(mcb)
        wqueue_.push_back(mcb);
      }

      return resume_write();
    }

    ////////////////////////////////////////////////////////////////////////////
    bool _write(mcb_t* mcb) {
      FUSION_ASSERT(mcb);

      size_t len = FUSION_PACKET_HEADER_LEN + mcb->xfer_size();
      size_t o, i;

      if (wremain_ == 0)
        wremain_ = FUSION_PACKET_HEADER_LEN + mcb->xfer_size();

      if ((len - wremain_) < FUSION_PACKET_HEADER_LEN) {
        unsigned short h = (unsigned short)mcb->xfer_size();
        size_t offset = len - wremain_;
        o = i = FUSION_PACKET_HEADER_LEN - (len - wremain_);

        if (!write(o, ((char*)&h) + offset))
          return false;

        wremain_ -= o;

        if (o < i)
          return false;
      }

      if ((len - wremain_) < (FUSION_PACKET_HEADER_LEN + sizeof mcb_xfer_t)) {
        size_t offset = len - wremain_ - FUSION_PACKET_HEADER_LEN;
        o = i = sizeof mcb_xfer_t - offset;

#if defined(COMPILING_FUSION_MB)
        if (!write(o, ((char*)mcb) + offset))
          return false;
#else
        if (__need_to_convert_clock(mcb)) {
          mcb_t tmp(mcb);

          clock_server2client(&tmp);

          if (!write(o, ((char*)&tmp) + offset)) {
            FUSION_FATAL("incomplete write on stack mcb");

            return false;
          }
        }
        else if (!write(o, ((char*)mcb) + offset))
          return false;
#endif
        wremain_ -= o;

        if (o < i)
          return false;
      }

      if (wremain_ > 0) {
        size_t offset = len - wremain_ - FUSION_PACKET_HEADER_LEN - sizeof mcb_xfer_t;
        bool rc;

        o = i = mcb->len_ - offset;

        if (!mcb->len_)
          rc = true;
        else if (mcb->len_ <= sizeof mcb->u)
          rc = write(o, ((char*)mcb->u.data_) + offset);
        else
          rc = write(o, ((char*)mcb->u.pdata_) + offset);

        if (!rc)
          return false;

        wremain_ -= o;

        if (o < i)
          return false;
      }

      if (wremain_ == 0) {
#ifdef  WIN32
        if (mcb->wevent_ != INVALID_HANDLE_VALUE) {
          ::SetEvent(mcb->wevent_);
          mcb->wevent_ = INVALID_HANDLE_VALUE;
        }
#endif

        return true;
      }

      return false;
    }

    ////////////////////////////////////////////////////////////////////////////
    bool __need_to_convert_clock(mcb_t* mcb) {
      if (mcb->mid_ == MD_SYS_TIMESYNC_REQUEST || mcb->mid_ == MD_SYS_TIMESYNC_REPLY)
        return false;

      return clock_sync_.period_ != 0;
    }

    ////////////////////////////////////////////////////////////////////////////
    void clock_client2server(mcb_t* mcb) {
#if _DEBUG
      if (!__need_to_convert_clock(mcb))
        return;
#endif // _DEBUG

      mcb->org_ -= clock_sync_.clock_delta_;
    }

    ////////////////////////////////////////////////////////////////////////////
    void clock_server2client(mcb_t* mcb) {
#if _DEBUG
      if (!__need_to_convert_clock(mcb))
        return;
#endif // _DEBUG

      mcb->org_ += clock_sync_.clock_delta_;
    }

#ifdef	WIN32
    virtual HANDLE wait_handle() = 0;
#else
#endif
  };

  conn_t* create_conn(const char*, mcbpool_t*);
}

#endif		//FUSION_CONN_H
