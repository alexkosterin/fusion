/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef DRIVER__H

#include <stdint.h>
#include <vector>

namespace nf { struct mcb_t; };
class header;

enum type_t {
  _AUTO,    // will substitude to _SIZE on mopen()
  _VOID,
  _BOOL,    // size = 1, format %d
  _SHORT,   // size = 2, format %d
  _INT,     // size = 4, format %d
  _FLT,     // size = 4, format %g
  _DBL,     // size = 8, format %g
  _STR,     // size is variable, format %s
  _SIZE,    // size is constant, and defined by msg.size_,
            // can be useful for unsupported types (int64), or a placeholder for user structures;

  _LAST,
};

struct msg_desc_t {
//nf::mid_t   mid_;
  size_t      idx_;
  const char* name_;
  type_t      type_;
  const char* fmt_;
  size_t      size_;
};

typedef std::vector<msg_desc_t> msgs_t;

struct driver_t {
  // fusion capture
  virtual bool init_capture(const msgs_t&, const char* params, const header&) = 0;
  virtual bool cb(size_t pos, const nf::mcb_t*) = 0;
  virtual bool fini_capture() = 0;

  virtual bool init_volume(const char* dir, const char* tempate_name) = 0;
  virtual bool fini_volume() = 0;
  virtual bool term_volume() = 0;

  virtual const char* vol_name() = 0;
  virtual size_t vol_nr() = 0;
  virtual int64_t vol_msg_nr() = 0;
  virtual int64_t vol_size() = 0;

  // dump
  virtual bool dump(const char* name) = 0;
  virtual bool dump_header(const char* name) = 0;
  virtual bool dump_csv(const char* name) = 0;

  // playback
  // ...

  static driver_t* create(const char* sig);
};

#endif //DRIVER__H
