/*
 *  FUSION
 *  Copyright (c) 2014 Alex Kosterin
 */

#ifndef STATUS_CLIENT_H
# define STATUS_CLIENT_H

# include "nfs.h"

namespace nf {
  struct client_t;
}

namespace nfs {
  //////////////////////////////////////////////////////////////////////////////
  nf::result_t init_client(
    FUSION_IN nf::client_t&,                                        // existing Fusion client object
    FUSION_IN int32_t retransmit_period = DEFAULT_RETRANSMIT_PERIOD_MSEC, // period of time, client must set its status to be considere 'alive'
                                                                    // negative value makes library to re-transmit
                                                                    // last set status and description automatically,
                                                                    // e.g. -60000 means auto retransmittion every minute
    FUSION_IN const char* status_name = STATUS_NAME,                // Fusion tag name used for sending statuses
    FUSION_IN const char* status_notify_name = STATUS_NOTIFY_NAME); // Fusion tag name used for requesting status send

  //////////////////////////////////////////////////////////////////////////////
  nf::result_t fini_client();

  //////////////////////////////////////////////////////////////////////////////
  nf::result_t set_status(
    FUSION_IN status_code_t code,                                   // Status enum code
    FUSION_IN const char* description = 0, ...);                    // User status description in printf format style.
                                                                    // By convention STATUS_GREEN does not require a description,
                                                                    // but this is not enforced

  //////////////////////////////////////////////////////////////////////////////
  nf::result_t set_status_retransmit_period(
    FUSION_IN size_t retranslate_period_msec);                      // see comment for init_client above

  //////////////////////////////////////////////////////////////////////////////
  size_t get_status_retranslate_period();
}

#endif  //STATUS_CLIENT_H
