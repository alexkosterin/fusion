/*
 *  FUSION
 *  Copyright (c) 2014 Alex Kosterin
 */

#ifndef EVENTLOG_CLIENT_H
# define EVENTLOG_CLIENT_H

# include "include/nfel.h"

namespace nf {
  struct client_t;
}

namespace nfel {
  //////////////////////////////////////////////////////////////////////////////
  nf::result_t init_client(
    FUSION_IN nf::client_t&,                          // existing Fusion client object
    FUSION_IN const char* tag_name = EVENTLOG_NAME);  // Fusion tag name used for event logging

  //////////////////////////////////////////////////////////////////////////////
  nf::result_t fini_client();

  //////////////////////////////////////////////////////////////////////////////
  nf::result_t log(
    FUSION_IN severity_t severity,                    // event severity
    FUSION_IN const char* description, ...);          // event describtion in printf format style. May be trancated, if too long (1024)
}

#endif  //EVENTLOG_CLIENT_H
