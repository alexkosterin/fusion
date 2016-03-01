/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include "include/tcpconn.h"
#include "include/resolve.h"

#if	defined(WIN32)
# include <Winsock2.h>
#	pragma comment(lib, "ws2_32.lib")
#endif

namespace nf {
  //////////////////////////////////////////////////////////////////////////////
  tcp_conn_t::tcp_conn_t(const char* a, int p, mcbpool_t* pool) :
    conn_t(pool),
    host_(::strdup(a)),
    port_(p),
    socket_(INVALID_SOCKET),
    event_(::WSACreateEvent())
  {
    FUSION_ASSERT(event_ != NULL);
  }

  //////////////////////////////////////////////////////////////////////////////
  tcp_conn_t::tcp_conn_t(SOCKET s, mcbpool_t* pool) :
    conn_t(pool),
    host_(0),
    port_(0),
    socket_(s),
    event_(::WSACreateEvent())
  {
    FUSION_ASSERT(socket_ != INVALID_SOCKET);
    FUSION_ASSERT(event_ != NULL);

    int rc = ::WSAEventSelect(socket_, event_, FD_READ|FD_WRITE|FD_CLOSE);

    if (rc == SOCKET_ERROR) {
      FUSION_DEBUG("WSAEventSelect=%d", ::WSAGetLastError());

      ::closesocket(socket_);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  tcp_conn_t::~tcp_conn_t() {
    free((void*)host_);
    ::closesocket(socket_);
    ::WSACloseEvent(event_);
  }

  //////////////////////////////////////////////////////////////////////////////
  bool tcp_conn_t::open() {
    if (socket_ != INVALID_SOCKET)
      return true;

    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in sa;

    sa.sin_family       = AF_INET;
    sa.sin_port         = htons(port_);
    sa.sin_addr.s_addr  = resolve_address(host_);

    FUSION_ASSERT(sa.sin_addr.s_addr != INADDR_NONE);

    if (sa.sin_addr.s_addr == INADDR_NONE)
      return false;

    if (::connect(socket_, (SOCKADDR*)&sa, sizeof sa) == SOCKET_ERROR) {
      FUSION_DEBUG("connect=%d", ::WSAGetLastError());

      socket_ = INVALID_SOCKET;

      return false;
    }

#if (MB_TCP_NO_DELAY > 0)
    BOOL v = 1;
    int rc = ::setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, (char*)&v, sizeof v);

    if (rc == SOCKET_ERROR)
      FUSION_WARN("setsockopt(TCPNODELAY)=%d", ::WSAGetLastError());
#endif

    if (::WSAEventSelect(socket_, event_, FD_READ|FD_WRITE|FD_CLOSE) == SOCKET_ERROR) {
      FUSION_DEBUG("WSAEventSelect=%d", ::WSAGetLastError());

      ::closesocket(socket_);
      socket_ = INVALID_SOCKET;

      return false;
    }

    return true;
  }

  //////////////////////////////////////////////////////////////////////////////
  bool tcp_conn_t::close() {
    bool rc = ::closesocket(socket_) != SOCKET_ERROR;

    socket_ = INVALID_SOCKET;

    return rc;
  }

  //////////////////////////////////////////////////////////////////////////////
  bool tcp_conn_t::read(size_t& len, void* buff) {
    FUSION_ASSERT(socket_ != INVALID_SOCKET);

    int rlen = ::recv(socket_, (char*)buff, len, 0);

    if (rlen == SOCKET_ERROR) {
      FUSION_DEBUG("WSAGetLastError=%d", ::WSAGetLastError());

      if (::WSAGetLastError() == WSAEWOULDBLOCK)
        rlen = 0;
      else
        return false;
    }

    len = rlen;

    return true;
  }

  //////////////////////////////////////////////////////////////////////////////
  bool tcp_conn_t::availread(size_t& len) {
    FUSION_ASSERT(socket_ != INVALID_SOCKET);

    DWORD dw;

    int rc = ::WSAIoctl(socket_, FIONREAD, 0, 0, &len, sizeof len, &dw, 0, 0);

    if (rc == SOCKET_ERROR) {
      FUSION_DEBUG("WSAGetLastError=%d", ::WSAGetLastError());

      return ::WSAGetLastError() != WSAEWOULDBLOCK;
    }

    FUSION_ASSERT(dw == sizeof len);
    FUSION_DEBUG("len=%d", len);

    return true;
  }

#include <time.h>

#if !defined(COMPILING_FUSION_MB) && defined(TEST_PARTIAL_WRITES)
  extern HANDLE fake_write_resume_event;
#endif

  //////////////////////////////////////////////////////////////////////////////
  bool tcp_conn_t::write(size_t& len, const void* buff) {
#if !defined(COMPILING_FUSION_MB)
#if  defined(TEST_PARTIAL_WRITES)
    static bool bsrand = false;

    if (!bsrand) {
      srand(time(NULL));
      bsrand = true;
    }

    if ((rand() % TEST_PARTIAL_WRITES) == 0) {
      size_t l = 1 + rand() % len;  // random 1..len
      size_t t = ::send(socket_, (const char*)buff, l, 0);

      if (t == SOCKET_ERROR) {
        int rc = ::WSAGetLastError();

        if (rc != WSAEWOULDBLOCK) {
          FUSION_WARN("send(%d, buff, %d, 0)=%d", socket_, len, ::WSAGetLastError());

          return false;
        }
        else
          t = 0;
      }

      if (t < len)
        ::SetEvent(fake_write_resume_event);

      len = t;
    }
    else
#endif
    {
      size_t t = ::send(socket_, (const char*)buff, len, 0);

      if (t == SOCKET_ERROR) {
        int rc = ::WSAGetLastError();

        if (rc != WSAEWOULDBLOCK) {
          FUSION_WARN("send(%d, buff, %d, 0)=%d", socket_, len, ::WSAGetLastError());

          return false;
        }
        else {
          FUSION_WARN("send(%d, buff, %d, 0)=WSAEWOULDBLOCK", socket_, len);

          t = 0;
        }
      }

      len = t;
    }
#else
    size_t t = ::send(socket_, (const char*)buff, len, 0);

    if (t == SOCKET_ERROR)
      if (::WSAGetLastError() != WSAEWOULDBLOCK)
        return false;
      else
        t = 0;

    len = t;
#endif

    return true;
  }

  //////////////////////////////////////////////////////////////////////////////
  bool tcp_conn_t::resume_write() {
    wlock_t lock(lock_);

    while (!wqueue_.empty()) {
      mcb_t* mcb = wqueue_.front();

      if (_write(mcb)) {
        wqueue_.pop_front();
        RELEASE((*mcbpool_), mcb);
      }
      else
        break;
    }

    int e = ::WSAGetLastError();

    return e == 0 || e == WSAEWOULDBLOCK;
  }

#ifdef	WIN32
  //////////////////////////////////////////////////////////////////////////////
  HANDLE tcp_conn_t::wait_handle() {
    return event_;
  }
#else
#endif

  //////////////////////////////////////////////////////////////////////////////
  void tcp_conn_t::shutdown() {
    ::shutdown(socket_, SD_BOTH);
  }


  //////////////////////////////////////////////////////////////////////////////

  conn_t* create_conn(const char* s, mcbpool_t* p) {
#define HOST_SIZE 120

    char host[HOST_SIZE];
    int  port;

    if (sscanf(s, "type = tcp host = %" STRINGIFY(HOST_SIZE) "s port = %d", host, &port) != 2)
      return 0;

    return new tcp_conn_t(host, port, p);
  }

  namespace {
    struct _initialize {
      _initialize()		{
        WSADATA wsaData;
        int err = ::WSAStartup(MAKEWORD(2, 2), &wsaData);

        FUSION_VERIFY(err == 0);
      }

      ~_initialize()	{
        ::WSACleanup();
      }
    } _;
  }
}

