/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <stdio.h>
#include <string.h>

#include "include/nf.h"	// FUSION
#include "include/nf_macros.h"
#include "include/mb.h"	// FUSION MESSAGE BUS
#include "include/tsc.h"

#define NOGDI
#define _WINSOCKAPI_
#include <windows.h>
#include <wincon.h>

#include <vector>
#include <algorithm>

#define MAX_MESSAGES (16)

struct msg_t {
  const char* name_;    // message name
  nf::mid_t   mid_;     // message descriptor
  size_t      size_;    // message size, -1 if variable
  FILE*       fd_;      // file descriptod for gicen message

  // data section
  int         msecs_;
  size_t      len_;     // used for variable size messages
  void*       data_;
};

static msg_t msgs[MAX_MESSAGES] = {};
static size_t msg_nr = 0;
static nf::msecs_t org;

static bool mycompare(int a, int b) {
   return msgs[a].msecs_ < msgs[b].msecs_;
}

static bool stop = false;
static size_t msg_done = 0;

////////////////////////////////////////////////////////////////////////////////
//
// Reading next data associated with message in msgs[i] array
// Actual data will be kept in data section of msg_t struct
//
bool readmsg(size_t i) {
  size_t sz = ::fread(&msgs[i].msecs_, sizeof msgs[i].msecs_, 1, msgs[i].fd_);

  if (sz != 1)
    return false;

  if (msgs[i].size_ != -1) {
    if (!msgs[i].data_)
      msgs[i].data_ = ::malloc(msgs[i].size_);
  }
  else {
    sz = ::fread(&msgs[i].len_, sizeof msgs[i].len_, 1, msgs[i].fd_);

    if (sz != 1)
      return false;

    msgs[i].data_ = ::realloc(msgs[i].data_, msgs[i].size_);
  }

  sz = ::fread(msgs[i].data_, msgs[i].len_, 1, msgs[i].fd_);

  if (sz != 1)
    return false;

  return true;
}

////////////////////////////////////////////////////////////////////////////////
//
// Write next data associated with message in msgs[i] array
// This is fusion callback function that is triggered when a message arrives.
//
nf::result_t __stdcall writemsg(nf::mid_t mid, size_t len, const void *data) {
  static bool first_write = true;
  nf::msecs_t now = nf::now_msecs();

  // Find index into 'msgs[]' associated with given 'mid'.
  for (size_t i = 0; i < msg_nr; ++i)
    if (msgs[i].mid_ == mid) {
      FUSION_ASSERT(msgs[i].size_ == -1 || msgs[i].size_ == len);

      msgs[i].msecs_  = (int)(now - org);

      size_t sz = ::fwrite(&msgs[i].msecs_, sizeof msgs[i].msecs_, 1, msgs[i].fd_);

      FUSION_ASSERT(sz == 1);

      if (msgs[i].size_ == -1) {
        sz = ::fwrite(&len, sizeof len, 1, msgs[i].fd_);

        FUSION_ASSERT(sz == 1);
      }

      if (len) {
        sz = ::fwrite(data, len, 1, msgs[i].fd_);

        FUSION_ASSERT(sz == 1);
      }

      break;
    }

  ++msg_done;

  return nf::ERR_OK;
}

////////////////////////////////////////////////////////////////////////////////
void usage(const char* prog) {
  fprintf(stderr, "usage: %s --host=IP4ADDR --port=PORT [--dir=PATH] --mode=play|record --mssage=NAME [--message=NAME]...\n", prog);
  exit(1);
}

////////////////////////////////////////////////////////////////////////////////
BOOL WINAPI on_break(DWORD CtrlType) {
  stop = true;

  return TRUE;
}

////////////////////////////////////////////////////////////////////////////////
int main(int argc, const char** argv) {
  nf::client_t client("simple simulator: player/recorder");
  const char* port    = "3001";
  const char* host    = "127.0.0.1";
  const char* profile = "unknown";
  const char* dir     = "./";
  double rate         = 1.;
  char connect[250]   = {};
  bool play           = true;
  size_t err_nr       = 0;
  nf::result_t e;

  //
  // Parameter parsing
  //

  for (int i = 1; i < argc; ++i) {
    size_t len;

    if (     (len = 7) && ::strncmp(argv[i], "--port=", len) == 0)
      port  = argv[i] + len;
    else if ((len = 7) && ::strncmp(argv[i], "--host=", len) == 0)
      host  = argv[i] + len;
    else if ((len = 10) && ::strncmp(argv[i], "--profile=", len) == 0)
      profile  = argv[i] + len;
    else if ((len = 13) && ::strncmp(argv[i],"--mode=record", len) == 0)
      play = false;
    else if ((len = 8) && ::strncmp(argv[i], "--record", len) == 0)
      play = false;
    else if ((len = 11) && ::strncmp(argv[i],"--mode=play", len) == 0)
      play = true;
    else if ((len = 6) && ::strncmp(argv[i], "--play", len) == 0)
      play = true;
    else if ((len = 6) && ::strncmp(argv[i], "--dir=", len) == 0)
      dir   = argv[i] + len;
    else if ((len = 7) && ::strncmp(argv[i], "--rate=", len) == 0)
      rate  = ::atof(argv[i] + len);
    else if (
      ((len = 6) && ::strncmp(argv[i], "--msg=", len) == 0)  ||
      ((len =10) && ::strncmp(argv[i], "--message=", len) == 0)
      ) {
      if (msg_nr >= MAX_MESSAGES) {
        fprintf(stderr, "error: too many messages, max=%d\n", MAX_MESSAGES);
        ++err_nr;
      }

      char buff[MAX_PATH] = {0};
      int sz = 0;

      size_t nr = ::_snscanf(argv[i] + len, ::strlen(argv[i]) - len, "%" STRINGIFY(MAX_PATH) "[^:]:%d", buff, &sz);

      if (nr != 2) {
        fprintf(stderr, "error: message format=<message-name>:<size>\t\"%s\"\n", argv[i] + len);
        ++err_nr;
      }

      if (sz < -1 || sz > 32000) {
        fprintf(stderr, "error: message size is out of range=-1..32000\t\"%s\"\n", argv[i] + len);
        ++err_nr;
      }

      msgs[msg_nr].name_ = ::strdup(buff);
      msgs[msg_nr].size_ = sz;
      ++msg_nr;
    }
    else {
      fprintf(stderr, "error: arg=%d\n", argv[i]);
      ++err_nr;
    }
  }

  if (err_nr)
    usage(argv[0]);

  fprintf(stderr, "%s: using host=%s port=%s profile=%s dir=%s rate=%g messages=", argv[0], host, port, profile, dir, rate);

  for (size_t i = 0; i < msg_nr; ++i)
    fprintf(stderr, "\"%s:%d\"%s", msgs[i].name_, msgs[i].size_, i < msg_nr - 1 ? ", " : "");

  fprintf(stderr, "\n");

  if (!msg_nr) {
    fprintf(stderr, "error: no messages given\n", MAX_MESSAGES);
    usage(argv[0]);
  }

  if (rate < 0.) {
    fprintf(stderr, "error: rate must be greater then 0.\n", MAX_MESSAGES);
    usage(argv[0]);
  }

  ::SetConsoleCtrlHandler(on_break, TRUE);

  //
  // Register with fusion
  //

  _snprintf(connect, sizeof connect - 1, "type=tcp host=%s port=%s", host, port);

  if ((e = client.reg(connect, profile)) != nf::ERR_OK) {
    fprintf(stderr, "error: reg=%d\n", e);

    return 1;
  }

  FUSION_DEBUG("client id=%d", client.id());

  err_nr  = 0;

  //
  // Open messages
  //

  for (size_t i = 0; i < msg_nr; ++i) {
    msgs[i].size_   = -1;
    msgs[i].msecs_  = 0;
    nf::mtype_t mt;

    if ((e = client.mopen(msgs[i].name_, play ? nf::O_WRONLY : nf::O_RDONLY , mt, msgs[i].mid_, msgs[i].size_)) != nf::ERR_OK) {
      fprintf(stderr, "error: mopen(%s)=%d\n", msgs[i].name_, e);
      ++err_nr;

      continue;
    }

    if (msgs[i].size_ != -1)
      msgs[i].len_ = msgs[i].size_;
  }

  //
  // Open files to read/write message content
  //

  char buff[MAX_PATH + 1];

  org = nf::now_msecs();

  for (size_t i = 0; i < msg_nr; ++i) {
    ::_snprintf(buff, sizeof buff - 1, "%s/%s.data", dir, msgs[i].name_);

    if (!(msgs[i].fd_ = ::fopen(buff, play ? "rb" : "wb"))) {
      ::fprintf(stderr, "error: fopen(%s)\n", msgs[i].name_);
      ++err_nr;

      continue;
    }

    size_t sz;

    if (play) {
      sz = ::fread(&org, sizeof org, 1, msgs[i].fd_);

      FUSION_ASSERT(sz == 1);

      ::fread(&sz, sizeof sz, 1, msgs[i].fd_);

      FUSION_ASSERT(sz == 1);

      if (sz != msgs[i].size_) {
        ::fprintf(stderr, "error: message size mismatch %s: fusion=%d vs file=%d)\n", msgs[i].name_, sz, msgs[i].size_);
        ++err_nr;
      }
    }
    else  {
      sz = ::fwrite(&org, sizeof org, 1, msgs[i].fd_);

      FUSION_ASSERT(sz == 1);

      sz = ::fwrite(&msgs[i].size_, sizeof msgs[i].size_, 1, msgs[i].fd_);

      FUSION_ASSERT(sz == 1);
    }
  }

  if (!err_nr) {
    if (play) {
      // Paying work loop
      std::vector<int> queue;

      for (size_t i = 0; i < msg_nr; ++i)
        if (readmsg(i))
          queue.push_back(i);

      while (!stop && !queue.empty()) {
        std::sort(queue.begin(), queue.end(), mycompare);

        size_t p = queue.front();

        ::Sleep((DWORD)((double)msgs[p].msecs_ / rate));

        if (nf::ERR_OK == client.publish(msgs[p].mid_, msgs[p].len_, msgs[p].data_))
          ++msg_done;

        for (size_t i = 1; i < queue.size(); ++i)
          msgs[queue[i]].msecs_ -= msgs[p].msecs_;

        if (!readmsg(p))
          queue.erase(queue.begin());
      }
    }
    else {
      // Recording work loop
      for (size_t i = 0; i < msg_nr; ++i)
        if ((e = client.subscribe(msgs[i].mid_, nf::SF_PUBLISH, nf::CM_MANUAL, writemsg)) != nf::ERR_OK) {
          fprintf(stderr, "warn: mclose(%s)=%d\n", msgs[i].name_, e);
          goto done;
        }

      while (!stop && client.registered()) {
        client.dispatch(true);
        ::Sleep(1);     // do not goble all CPU
      }
    }
  }

  ::Sleep(1);     // @@

  //
  // Close messages
  // note: mclose() also does unsubscribe()
  //

  for (size_t i = 0; i < msg_nr; ++i) {
    fclose(msgs[i].fd_);
    client.mclose(msgs[i].mid_);

    if (msgs[i].data_)
      free(msgs[i].data_);
  }

  //
  // Unregister
  //
done:
  e = client.unreg();

  if (e != nf::ERR_OK)
    FUSION_DEBUG("unreg=%d", e);

  fprintf(stderr, "%s messages %s: %d", argv[0], play ? "published": "received", msg_done);

  return 0;
}
