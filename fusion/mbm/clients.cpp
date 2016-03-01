/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include "include/configure.h"
#include "include/nf_macros.h"
#include "include/md.h"

#include "clients.h"
#include <algorithm>

namespace nf {
  client_t::client_t(cid_t cid, conn_t* conn, ULONG addr, USHORT port) : cid_(cid), registered_(false), conn_(conn), addr_(addr), port_(port), address_(0) {
    FUSION_ASSERT(conn_);

    ::memset(&uuid_, 0, sizeof uuid_);
  }

  //////////////////////////////////////////////////////////////////////////////
  client_t::~client_t() {
    FUSION_DEBUG("destroy client=%d", cid_);

    if (registered_) {
      result_t e = md_unreg_client(cid_);

      FUSION_ASSERT(e == ERR_OK);
    }

    delete conn_;
    ::free((void*)address_);
  }

  //////////////////////////////////////////////////////////////////////////////
  clients_t::clients_t() {}

  //////////////////////////////////////////////////////////////////////////////
  clients_t::~clients_t() {
#if DEBUG_HEAP > 0
    while (size())
      del(begin()->second);
#endif
  }

  //////////////////////////////////////////////////////////////////////////////
  bool clients_t::add(client_t* c) {
    FUSION_ASSERT(array.size() == size());

    if (insert(std::make_pair(c->cid_, c)).second) {
      array.push_back(c);
      apmap.insert(std::make_pair(std::make_pair(c->addr_, c->port_), c));  //@@check

      return true;
    }

    return false;
  }

  //////////////////////////////////////////////////////////////////////////////
  bool clients_t::del(client_t* c) {
    FUSION_ENSURE(c, return false);

    return del(c->cid_);
  }

  //////////////////////////////////////////////////////////////////////////////
  bool clients_t::del(cid_t cid) {
    FUSION_ASSERT(array.size() == size());

    if (erase(cid)) {
      for (array_t::iterator I = array.begin(), E = array.end(); I != E; ++I)
        if ((*I)->cid_ == cid) {
          client_t* c = *I;
          array.erase(I);
          apmap.erase(std::make_pair(c->addr_, c->port_));  //@@check

          FUSION_ASSERT(array.size() == size());

          delete c;

          return true;
        }

        FUSION_ASSERT(0, "map and array disagree");
    }

    return false;
  }

  //////////////////////////////////////////////////////////////////////////////
  client_t* clients_t::find_by_uuid(const UUID* uuid) {
    // check uuid is zero
    for (const char *p = (const char*)uuid; p < ((const char*)uuid + sizeof uuid); ++p)
      if (*p)
        goto nonzero;

    return 0;

nonzero:
    for (const_iterator I = cbegin(); I != cend(); ++I)
      if (::memcmp(uuid, &I->second->uuid_, sizeof uuid) == 0)
        return I->second;

    return 0;
  }

  //////////////////////////////////////////////////////////////////////////////
  // this must be quick
  const client_t* clients_t::find_by_ap(const IN_ADDR& a, USHORT p) {
    apmap_t::const_iterator I = apmap.find(std::make_pair(a.S_un.S_addr, p));

    return I == apmap.cend() ? 0 : I->second;
  }

  //////////////////////////////////////////////////////////////////////////////
  void clients_t::shuffle() {
    FUSION_ASSERT(array.size() == size());

    std::random_shuffle(array.begin(), array.end());
  }
}
