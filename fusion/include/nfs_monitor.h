/*
 *  FUSION
 *  Copyright (c) 2014 Alex Kosterin
 */

#ifndef STATUS_MONITOR_H
# define STATUS_MONITOR_H

# include "nfs.h"

namespace nf {
  struct client_t;
}

namespace nfs {
  typedef nf::result_t (*status_monitor_callback_t)(
    status_code_t code,
    nf::msecs_t   orig,         // timestamp of message creation
    nf::cid_t     client_id,
    const char*   client_name,  // enum_monitor_clients: may be NULL, if show_incomlete is true
    const char*   description,
    void*         cookie);

  //////////////////////////////////////////////////////////////////////////////
  nf::result_t init_monitor(
    FUSION_IN nf::client_t& client,                                 // existing Fusion client object
    FUSION_IN status_monitor_callback_t callback,                   // to be called on every resolved status message received
                                                                    // can be NULL
    FUSION_IN void* cookie = 0,
    FUSION_IN const char* status_name = STATUS_NAME,                // Fusion tag name used for sending statuses
    FUSION_IN const char* status_notify_name = STATUS_NOTIFY_NAME); // Fusion tag name used for requesting status send

  //////////////////////////////////////////////////////////////////////////////
  nf::result_t fini_monitor();

  //////////////////////////////////////////////////////////////////////////////
  nf::result_t enum_monitor_clients(
    FUSION_IN status_monitor_callback_t callback,                   // to be called for every monitored client
                                                                    // can not be NULL
    FUSION_IN bool show_incomlete = false);
}

#endif  //STATUS_MONITOR_H
