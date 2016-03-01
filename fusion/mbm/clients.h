/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 *  All Rights Reserved
 */

#ifndef		FUSION_CLIENTS_H
#define		FUSION_CLIENTS_H

#include "include/nf.h"
#include "include/configure.h"

#include "include/conn.h"
#include "include/tcpconn.h"

#include <map>
#include <set>
#include <vector>

#include <inaddr.h>

namespace nf {
  struct profile_t;

  typedef std::set<std::pair<cid_t, seq_t>> pending_client_reqs_t;

  struct client_t { ////////////////////////////////////////////////////////////
    cid_t           cid_;
    UUID						uuid_;
    bool            registered_;
    conn_t*         conn_;              // client connection
    profile_opts_t* profile_opts_;

    const char*     address_;           //

    // for udp
    ULONG           addr_;
    USHORT          port_;

    pending_client_reqs_t pcrs_;

    client_t(cid_t cid, conn_t* conn, ULONG addr, USHORT port);
    ~client_t();
  };

  struct clients_t : std::map<cid_t, client_t*> { //////////////////////////////
    clients_t();
    ~clients_t();

    typedef std::vector<client_t*>  array_t;
    typedef std::map<std::pair<ULONG, USHORT>, client_t*> apmap_t;

    array_t array;
    apmap_t apmap;

    client_t* get(cid_t cid) {
      clients_t::const_iterator I = find(cid);
      return I != cend() ? I->second : 0;
    }

    bool add(client_t* c);
    bool del(client_t* c);
    bool del(cid_t cid);
    void shuffle();
    client_t* find_by_uuid(const UUID*);
    const client_t* find_by_ap(const IN_ADDR& a, USHORT p);
  };
}

#endif		//FUSION_CLIENTS_H
