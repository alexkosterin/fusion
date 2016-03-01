/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		COMPAGE_TCPCONN_H
#define		COMPAGE_TCPCONN_H

#include "include/configure.h"
#include "include/nf.h"
#include "include/conn.h"

typedef UINT_PTR  SOCKET;

namespace nf {
  struct tcp_conn_t : conn_t {
    SOCKET			socket_;
    char*				host_;
    int					port_;

    tcp_conn_t(const char* a, int p, mcbpool_t* pool);
    tcp_conn_t(SOCKET s, mcbpool_t* pool);
    ~tcp_conn_t();
    bool open();
    bool close();
    bool availread(size_t&);
    bool read(size_t& len, void* buff);
    bool write(size_t&, const void*);
    bool resume_write();
    void shutdown();

//  bool _write(size_t len, const void* buff, bool flush);

#ifdef	WIN32
    WSAEVENT	  event_;
    HANDLE wait_handle();
#endif
  };
}

#endif	//COMPAGE_TCPCONN_H
