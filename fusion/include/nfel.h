/*
 *  FUSION
 *  Copyright (c) 2014 Alex Kosterin
 */

#ifndef EVENTLOG_H
# define EVENTLOG_H

# define EVENTLOG_NAME         "$log"
# define EVENTLOG_MAX_STRING   (1024)

# define SV_NAME_UNKNOWN       "Unknown"
# define SV_NAME_NOISE         "Noise"
# define SV_NAME_INFO          "Info"
# define SV_NAME_WARN          "Warn"
# define SV_NAME_ERROR         "Error"
# define SV_NAME_FATAL         "Fatal"
# define SV_NAME_CATASTROFIC   "Catastrofic"

namespace nfel {
  enum severity_t: char {
    SV_UNKNOWN  = 0,
    SV_NOISE,
    SV_INFO,
    SV_WARN,
    SV_ERROR,
    SV_FATAL,
    SV_CATASTROFIC,
  };

  inline const char* severity_to_str(severity_t s) {
    switch (s) {
    default:
    case SV_UNKNOWN:        return SV_NAME_UNKNOWN;
    case SV_NOISE:          return SV_NAME_NOISE;
    case SV_INFO:           return SV_NAME_INFO;
    case SV_WARN:           return SV_NAME_WARN;
    case SV_ERROR:          return SV_NAME_ERROR;
    case SV_FATAL:          return SV_NAME_FATAL;
    case SV_CATASTROFIC:    return SV_NAME_CATASTROFIC;
    }
  }
}

#endif  //EVENTLOG_H
