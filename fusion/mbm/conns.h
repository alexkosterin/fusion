/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_CONNS_H
#define		FUSION_CONNS_H

#include <vector>
#include <map>
#include "include/nf.h"
#include "include/conn.h"

namespace nf {
  typedef std::pair<cid_t, conn_t*> conns_data_t;
  cid_t get_next_cid();

  struct conns_t : std::vector<conns_data_t>  { ////////////////////////////////
    conns_t()   {}
    ~conns_t()  {}

    void add(conn_t* c) { //////////////////////////////////////////////////////
      push_back(std::make_pair(get_next_cid(), c));
    }

    bool get(size_t index, conns_data_t* result) { /////////////////////////////
      if (index < size()) {
        conns_data_t r = at(index);

        if (result)
          *result = r;

        return true;
      }

      return false;
    }

    bool del(size_t index) { ///////////////////////////////////////////////////
      if (index < size())
        erase(begin() + index);

      return false;
    }
  };
}

#endif    //FUSION_CONNS_H

