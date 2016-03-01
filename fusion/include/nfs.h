/*
 *  FUSION
 *  Copyright (c) 2014 Alex Kosterin
 */

#ifndef STATUS_COMMON_H
# define STATUS_COMMON_H

# define STATUS_NAME                     "$status"
# define STATUS_NOTIFY_NAME              "$status-notify"
# define STATUS_MAX_STRING               (1024)
# define DEFAULT_RETRANSMIT_PERIOD_MSEC  (30000)
# define MIN_RETRANSMIT_PERIOD_MSEC      (2000)

namespace nfs {
  enum status_code_t: char {
    // RESERVED
    STATUS_UNKNOWN  = -1,
    STATUS_TERM     = -2,             // either deduced by monitor library, or sent by client library on exit

    // COLOURS
    STATUS_GRAY     = STATUS_UNKNOWN, //          no description required
    STATUS_GREEN    = 0,              // OK,      no description required
    STATUS_YELLOW,                    // WARNING, needs description
    STATUS_RED,                       // ERROR,   needs description

    // SYNONYMS
    STATUS_OK       = STATUS_GREEN,
    STATUS_WARN     = STATUS_YELLOW,
    STATUS_ERROR    = STATUS_RED,

    // USER CODES
    STATUS_USER     = STATUS_RED + 1,
  };

# pragma pack(push, status_repr_t, 1)
  struct status_repr_t {
    status_code_t status_;
    size_t        retrans_;           // status retranslate period, msecs
    char          desc_[1];           // optional description, zstring
  };
}
#pragma pack(pop, status_repr_t)

#endif  //STATUS_COMMON_H
