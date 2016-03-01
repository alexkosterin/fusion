/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_SM_H
#define		FUSION_SM_H

#include "include/nf.h"
#include "include/configure.h"

namespace SM {
  enum {
    SEC_CREATE_MESSAGE,
    SEC_OPEN_MESSAGE,
    SEC_UNLINK_MESSAGE,
    SEC_LINK_MESSAGE,
    SEC_QUERY_MESSAGE,
    SEC_STAT_MESSAGE,
    SEC_SUBSCRIBE_MESSAGE,
    SEC_PUBLISH_MESSAGE,
  };

  enum {
    ACCESS_ALL = 0xFFFFFFFF,
  };
}

#define PERM_CHECK(SEC_OPEN_MESSAGE, ...) (true)

#endif		//FUSION_SM_H
