/*
 *  FUSION
 *  Copyright (c) 2014 Alex Kosterin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <include/nf.h>
#include <include/nf_internal.h>
#include <include/mb.h>
#include <include/nf_macros.h>
#include "include/nfs_monitor.h"
#include "include/nfs_client.h"
#include "include/nfel_client.h"
#include <windows.h>

int period                = 0;
nfs::status_code_t status = nfs::STATUS_UNKNOWN;
const char* desc          = 0;
nf::client_t* client      = 0;

static bool wait_for_input() {
  while (true) {
    INPUT_RECORD irs[512];
    DWORD nr = 0;
    static HANDLE h = ::GetStdHandle(STD_INPUT_HANDLE);

    if (!::PeekConsoleInput(h, irs, FUSION_ARRAY_SIZE(irs), &nr))
      return false;

    for (int i = 0; i < nr; ++i)
      if (irs[i].EventType == KEY_EVENT && irs[i].Event.KeyEvent.wVirtualKeyCode == VK_RETURN)
        return true;

    if (client->dispatch(true, 1) != nf::ERR_OK)
      return false;
  }
}

static nf::result_t enum_callback(nfs::status_code_t code, nf::msecs_t ts, nf::cid_t client_id, const char* client_name, const char* desc, void*) {
  ::fprintf(stdout, "->\tcode=%d client=\"%s\"%s%s\n", code, client_name, desc ? "\tdescription=" : "", desc ? desc : "");

  return nf::ERR_OK;
}

static nf::result_t callback(nfs::status_code_t code, nf::msecs_t ts, nf::cid_t client_id, const char* client_name, const char* desc, void*) {
  ::fprintf(stdout, "\ncode=%d client=\"%s\"%s%s\n", code, client_name, desc ? "\tdescription=" : "", desc ? desc : "");

  return nf::ERR_OK;
}

static nf::result_t __stdcall on_stop(nf::mid_t mid, size_t len, const void *data) {
  char buff[64];
  int reason = *(int*)data;
  nf::result_t e;

  if (const nf::mcb_t* mcb = client->get_mcb())
    if (!mcb->request_)
      return nf::ERR_IGNORE;

  ::_snprintf(buff, sizeof buff, "Stopping, reason=%d", reason);

  if (::MessageBox(NULL, buff, "Monitoring Test App", MB_OKCANCEL) == IDOK) {
    e = nf::ERR_OK;

    client->reply(nf::MD_SYS_STATUS, sizeof e, &e);

    ::exit(reason);
    
    return e;  // never got here :-(
  }

  e = nf::ERR_IGNORE;

  client->reply(nf::MD_SYS_STATUS, sizeof e, &e);

  return nf::ERR_OK;
}

static nf::result_t __stdcall on_terminate(nf::mid_t mid, size_t len, const void *data) {
  char buff[64];
  int reason = *(int*)data;

  ::_snprintf(buff, sizeof buff, "Terminating, reason=%d", reason);

  ::MessageBox(NULL, buff, "Monitoring Test App", MB_OK);

  return nf::ERR_OK; // ignored
}

////////////////////////////////////////////////////////////////////////////////
void do_monitor() {
  nfel::log(nfel::SV_NOISE, "Starting monitoring...");

  if (nfs::init_monitor(*client, callback) != nf::ERR_OK)
    return;

  client->subscribe(nf::MD_SYS_STOP_REQUEST, nf::SF_PUBLISH, nf::CM_MANUAL, on_stop);
  client->subscribe(nf::MD_SYS_TERMINATE_REQUEST, nf::SF_PUBLISH, nf::CM_MANUAL, on_terminate);

  nfel::log(nfel::SV_INFO, "Monitoring initialized");

  char          buff[4096];
  bool          stop = false;
  nf::cid_t     cid;
  int           reason;
  nf::result_t  e;

  while (!stop) {
    ::fprintf(stdout, "Enter: e - enumerate clients, s cli code - terminate, t cli code - terminate, q - quit: ");

    if (!wait_for_input())
      goto exit;

    ::gets(buff);

    if (::strlen(buff) == 0)
      continue;

    switch (buff[0]) {
    case 'e':  case 'E':
      nfel::log(nfel::SV_INFO, "Got enumerate command");
      ::fprintf(stdout, "Clients: [\n");
      FUSION_ENSURE(nf::ERR_OK == enum_monitor_clients(enum_callback), { stop = true; break; });
      ::fprintf(stdout, "]\n");

      break;

    case 't':  case 'T':
      if (2 == ::_snscanf(buff+1, sizeof buff, " %5d %5d ", &cid, &reason)) {
        nfel::log(nfel::SV_INFO, "Got terminate command: cid=%d reason=%d", cid, reason);

        e = client->send(nf::MD_SYS_TERMINATE_REQUEST, cid, sizeof reason, &reason, 10000);

        ::fprintf(stdout, "\t%s\n", nf::result_to_str(e));
      }
      else
        ::fprintf(stdout, "\tFormat error: t cid reason\n");

      break;

    case 's':  case 'S':
      if (2 == ::_snscanf(buff+1, sizeof buff, " %5d %5d ", &cid, &reason)) {
        nfel::log(nfel::SV_INFO, "Got stop command: cid=%d reason=%d", cid, reason);

        e = client->send(nf::MD_SYS_STOP_REQUEST, cid, sizeof reason, &reason, 10000);

        ::fprintf(stdout, "\t%s\n", nf::result_to_str(e));
      }
      else
        ::fprintf(stdout, "\tFormat error: t cid reason\n");

      break;

    case 'q':  case 'Q':
      nfel::log(nfel::SV_INFO, "Got quit command");
      stop = true;

      break;

    default:
      nfel::log(nfel::SV_WARN, "Unrecognized command: ", buff);
    }
  }

exit:
  nfel::log(nfel::SV_NOISE, "Finalizing monitoring...");
  nfs::fini_monitor();
  nfel::log(nfel::SV_INFO, "Finalized monitoring");
}

////////////////////////////////////////////////////////////////////////////////
void do_client() {
  char buff[4096] = {0};
  nfs::status_code_t cs = nfs::STATUS_UNKNOWN;
  nf::result_t e;

  nfel::log(nfel::SV_NOISE, "Starting status client(%d)...", client->id());

  if (period == 0)
    e = nfs::init_client(*client);
  else
    e = nfs::init_client(*client, period);

  if (e != nf::ERR_OK) {
    nfel::log(nfel::SV_ERROR, "Got error=%s", nf::result_to_str(e));

    return;
  }

  client->subscribe(nf::MD_SYS_STOP_REQUEST, nf::SF_PUBLISH, nf::CM_MANUAL, on_stop);
  client->subscribe(nf::MD_SYS_TERMINATE_REQUEST, nf::SF_PUBLISH, nf::CM_MANUAL, on_terminate);

  nfel::log(nfel::SV_INFO, "Started status client");

  if (status != nfs::STATUS_UNKNOWN)
    nfs::set_status(status, desc);

  bool stop = false;

  while (!stop) {
    ::fprintf(stdout, "Enter: s - set status, p - pause/unpause, q - quit: ");

    if (!wait_for_input())
      goto exit;

    ::gets(buff);

    nfel::log(nfel::SV_INFO, "Get command=%s", buff);

    if (::strlen(buff) != 1) {
      nfel::log(nfel::SV_WARN, "Unrecognized command=%s", buff);

      continue;
    }

    switch (buff[0]) {
    case 's':  case 'S':
      ::fprintf(stdout, "Enter status code: r|y|g|c|p|t: // 'p' will set description as PID, t as TID, c as CID");

      if (!wait_for_input())
        goto exit;

      ::gets(buff);

      if (::strlen(buff) != 1)
        continue;

      switch (buff[0]) {
      case 'r':  case 'R':
        cs = nfs::STATUS_RED; break;

      case 'y':  case 'Y':
        cs = nfs::STATUS_YELLOW; break;

      case 'g':  case 'G':
        cs = nfs::STATUS_GREEN; break;

      case 'C': case 'c': case 'P':  case 'p':  case 'T':  case 't':
        cs = nfs::status_code_t(buff[0]); break;

      default:
        continue;
      }

      switch (cs) {
      case nfs::STATUS_GREEN:
        buff[0] = 0;

        FUSION_ENSURE(nf::ERR_OK == set_status(cs, buff), { stop = true; break; });

        ::fprintf(stdout, "Status set: %d \"%s\"\n", cs, buff);

        break;

      case nfs::STATUS_YELLOW:
      case nfs::STATUS_RED:
        ::fprintf(stdout, "Enter description: ");

        if (!wait_for_input())
          goto exit;

        ::gets(buff);

        FUSION_ENSURE(nf::ERR_OK == set_status(cs, buff), { stop = true; break; });

        ::fprintf(stdout, "Status set: %d \"%s\"\n", cs, buff);

        break;

      case nfs::status_code_t('p'):
      case nfs::status_code_t('P'):
        FUSION_ENSURE(nf::ERR_OK == set_status(cs, "pid->%d", ::GetCurrentProcessId()), { stop = true; break; });

        ::fprintf(stdout, "Status set: %d \"%s\"\n", "pid", buff);

        break;

      case nfs::status_code_t('t'):
      case nfs::status_code_t('T'):
        FUSION_ENSURE(nf::ERR_OK == set_status(cs, "tid->%d", ::GetCurrentThreadId()), { stop = true; break; });

        ::fprintf(stdout, "Status set: %d \"%s\"\n", "tid", buff);

        break;

      case nfs::status_code_t('c'):
      case nfs::status_code_t('C'):
        FUSION_ENSURE(nf::ERR_OK == set_status(cs, "cid->%d", client->id()), { stop = true; break; });

        ::fprintf(stdout, "Status set: %d \"%s\"\n", "cid", buff);

        break;

      case nfs::STATUS_TERM:
      case nfs::STATUS_GRAY:
        FUSION_ASSERT(0);

        FUSION_ENSURE(nf::ERR_OK == set_status(cs, buff), { stop = true; break; });

        break;
      };

      continue;

    case 'p':  case 'P':
      ::fprintf(stdout, "Nothing for now...\n");

      continue;

    case 'q':  case 'Q':
      stop = true;
    }
  }

exit:
  nfel::log(nfel::SV_NOISE, "Finalizing status client...");
  nfs::fini_client();
  nfel::log(nfel::SV_INFO, "Finalized status client");
}

////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[]) {
  const char* name    = "status test";
  const char* port    = "3001";
  const char* host    = "127.0.0.1";
  const char* profile = "unknown";
  char connect[250]   = {};
  nf::result_t e;
  enum { MONITOR, CLIENT } mode = MONITOR;

  for (int i = 1; i < argc; ++i)
    if (     !::strncmp(argv[i], "--port=", 7))
      port = argv[i] + 7;
    else if (!::strncmp(argv[i], "--profile=", 10))
      profile = argv[i] + 10;
    else if (!::strncmp(argv[i], "--host=", 7))
      host = argv[i] + 7;
    else if (!::strncmp(argv[i], "--name=", 7))
      name = argv[i] + 7;
    else if (!::strcmp(argv[i],  "--monitor"))
      mode = MONITOR;
    else if (!::strcmp(argv[i],  "--client"))
      mode = CLIENT;
    else if (!::strncmp(argv[i], "--period=", 9))
      period = ::atoi(argv[i] + 9);
    else if (!::strncmp(argv[i], "--status=", 9))
      status = nfs::status_code_t(::atoi(argv[i] + 9));
    else if (!::strncmp(argv[i], "--desc=", 7))
      desc = argv[i] + 7;
    else if (!::strcmp(argv[i],  "--help"))
      goto do_help;
    else {
      ::fprintf(stderr, "unknown option: %s\n", argv[i]);

do_help:
      ::fprintf(stderr, "usage: TODO\n");

      return 1;
    }

  ::fprintf(stdout, "host=%s port=%s profile=%s\n", host, port, profile);

  ::_snprintf(connect, sizeof connect - 1, "type=tcp host=%s port=%s", host, port);

  nf::client_t c(name);

  e = c.reg(connect, profile);

  if (e != nf::ERR_OK) {
    FUSION_DEBUG("client.reg=%d", e);

    return 1;
  }

  client = &c;
  nfel::init_client(c);

  switch (mode) {
  case MONITOR:
    do_monitor();

    break;

  case CLIENT:
    do_client();

    break;
  }

  nfel::fini_client();

  return 0;
}

