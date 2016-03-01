/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "include/nf.h"	    // FUSION
#include "include/nf_macros.h"
#include "include/mb.h"	    // FUSION MESSAGE BUS
#include "include/nf_mcb.h"	// FUSION MCB
#include "include/tsc.h"

# define NOGDI
# define _WINSOCKAPI_
#include <windows.h>

nf::client_t client("truedrill data simulator");
nf::result_t e;
int delay = 100;

double surface_wob                            = 25.;
double surface_wob_setpoint                   = 25.;
double surface_wob_setpoint_command           = 25.;
double highspeed_downhole_wob                 = 15.;
int autodriller_loop_in_control               = 0;
bool remote_control_request                   = false;
bool remote_control_enabled                   = false;
bool user_remote_control_toggle               = false;

const char* msg_surface_wob                   = "surface_wob";
const char* msg_surface_wob_setpoint          = "surface_wob_setpoint";
const char* msg_surface_wob_setpoint_command  = "surface_wob_setpoint_command";
const char* msg_highspeed_downhole_wob        = "highspeed_downhole_wob";
const char* msg_autodriller_loop_in_control   = "autodriller_loop_in_control";
const char* msg_remote_control_request        = "remote_control_request";
const char* msg_remote_control_enabled        = "remote_control_enabled";
const char* msg_user_remote_control_toggle    = "user_remote_control_toggle";

nf::mid_t m_surface_wob;
nf::mid_t m_surface_wob_setpoint;
nf::mid_t m_surface_wob_setpoint_command;
nf::mid_t m_highspeed_downhole_wob;
nf::mid_t m_autodriller_loop_in_control;
nf::mid_t m_remote_control_request;
nf::mid_t m_remote_control_enabled;
nf::mid_t m_user_remote_control_toggle;

size_t  nr = 0;

#define MCREATE(N,F,T,V) {                              \
  using namespace nf;                                   \
  size_t size = sizeof V;                               \
                                                        \
  result_t e = client.mcreate(N, F, MT_EVENT, T, size); \
                                                        \
  if (e != ERR_OK) {                                    \
    FUSION_DEBUG("client.mcreate(%s)=%d", N, e);        \
                                                        \
    return 1;                                           \
  }                                                     \
}

////////////////////////////////////////////////////////////////////////////////
nf::result_t __stdcall td_cb(nf::mid_t m, size_t len, const void *data) {
  const nf::mcb_t* mcb = client.get_mcb();
  ++nr;

//  ::fprintf(stdout, " src=%d ", mcb->src_);

  if (m_surface_wob == m) {
    FUSION_ASSERT(len == sizeof surface_wob);

//    ::fprintf(stdout, "cb:\tm_surface_wob=%g\n", *(double*)data);

    surface_wob = *(double*)data;
  }
  else if (m_surface_wob_setpoint == m) {
    FUSION_ASSERT(len == sizeof surface_wob_setpoint);

//    ::fprintf(stdout, "cb:\tm_surface_wob_setpoint=%g\n", *(bool*)data);

    surface_wob_setpoint = *(double*)data;
  }
  else if (m_highspeed_downhole_wob == m) {
    FUSION_ASSERT(len == sizeof highspeed_downhole_wob);

//    ::fprintf(stdout, "cb:\tm_highspeed_downhole_wob\n");

    highspeed_downhole_wob = *(double*)data;
  }
  else if (m_autodriller_loop_in_control == m) {
    FUSION_ASSERT(len == sizeof autodriller_loop_in_control);

//    ::fprintf(stdout, "cb:\tm_autodriller_loop_in_control\n");

    autodriller_loop_in_control = *(int*)data;
  }
  else if (m_remote_control_enabled == m) {
    FUSION_ASSERT(len == sizeof remote_control_enabled);

//    ::fprintf(stdout, "cb:\tmsg_remote_control_enabled=%c\n", *(bool*)data ? 'y' : 'n');

    remote_control_enabled = *(bool*)data;
  }
  else
    FUSION_DEBUG("unexpected message=%d", m);

  remote_control_enabled = m_surface_wob_setpoint_command > 20;

  return nf::ERR_OK;
}

////////////////////////////////////////////////////////////////////////////////
int td() {
  MCREATE(msg_surface_wob,                  O_RDONLY, m_surface_wob,                   surface_wob);
  MCREATE(msg_surface_wob_setpoint,         O_RDONLY, m_surface_wob_setpoint,          surface_wob_setpoint);
  MCREATE(msg_highspeed_downhole_wob,       O_RDONLY, m_highspeed_downhole_wob,        highspeed_downhole_wob);
  MCREATE(msg_autodriller_loop_in_control,  O_RDONLY, m_autodriller_loop_in_control,   autodriller_loop_in_control);
  MCREATE(msg_remote_control_enabled,       O_RDONLY, m_remote_control_enabled,        remote_control_enabled);

  MCREATE(msg_remote_control_request,       O_WRONLY, m_remote_control_request,        remote_control_request);
  MCREATE(msg_surface_wob_setpoint_command, O_WRONLY, m_surface_wob_setpoint_command,  surface_wob_setpoint_command);
  MCREATE(msg_user_remote_control_toggle,   O_WRONLY, m_user_remote_control_toggle,    user_remote_control_toggle);

  //////////////////////////////////////////////////////////////////////////////////////////////////
  e = client.subscribe(m_surface_wob, nf::SF_PUBLISH, nf::CM_MANUAL, td_cb);

  if (e != nf::ERR_OK) {
    FUSION_DEBUG("subscribe m_surface_wob=%d", e);

    return 1;
  }

  e = client.subscribe(m_surface_wob_setpoint, nf::SF_PUBLISH, nf::CM_MANUAL, td_cb);

  if (e != nf::ERR_OK) {
    FUSION_DEBUG("subscribe m_surface_wob_setpoint=%d", e);

    return 1;
  }

  e = client.subscribe(m_highspeed_downhole_wob, nf::SF_PUBLISH, nf::CM_MANUAL, td_cb);

  if (e != nf::ERR_OK) {
    FUSION_DEBUG("subscribe m_highspeed_downhole_wob=%d", e);

    return 1;
  }

  e = client.subscribe(m_autodriller_loop_in_control, nf::SF_PUBLISH, nf::CM_MANUAL, td_cb);

  if (e != nf::ERR_OK) {
    FUSION_DEBUG("subscribe m_autodriller_loop_in_control=%d", e);

    return 1;
  }

  e = client.subscribe(m_remote_control_enabled, nf::SF_PUBLISH, nf::CM_MANUAL, td_cb);

  if (e != nf::ERR_OK) {
    FUSION_DEBUG("subscribe m_remote_control_enabled=%d", e);

    return 1;
  }

  int inc = 1;

  if (surface_wob_setpoint_command >= 50.)
    inc = -1;
  else if (surface_wob_setpoint_command <= 0.)
    inc = 1;

  nf::msecs_t ms0 = nf::now_msecs();

  while (client.dispatch(delay, true) == nf::ERR_OK) {
    nf::msecs_t ms1 = nf::now_msecs();

    if (ms1 - ms0 > 1000) {
      ::fprintf(stdout, "%d\n", nr);
      ms0 = ms1;
      nr = 0;
    }

    //...
    if (surface_wob_setpoint_command <= 0. || surface_wob_setpoint_command >= 50.)
      inc = -inc;

    surface_wob_setpoint_command += 0.1 * inc;

//  ::fprintf(stdout, "publish m_surface_wob_setpoint_command(%d) surface_wob_setpoint_command=%g\n", m_surface_wob_setpoint_command, surface_wob_setpoint_command);
//  ::fprintf(stdout, ".");

    client.publish(m_surface_wob_setpoint_command, sizeof surface_wob_setpoint_command, &surface_wob_setpoint_command);

    client.dispatch(true);
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
nf::result_t __stdcall sim_cb(nf::mid_t m, size_t len, const void *data) {
  ++nr;
  const nf::mcb_t* mcb = client.get_mcb();

//  ::fprintf(stdout, "src=%d ", mcb->src_);

  if (m_surface_wob_setpoint_command == m) {
    FUSION_ASSERT(len == sizeof surface_wob_setpoint_command);

//    ::fprintf(stdout, "cb:\tsurface_wob_setpoint_command=%g\n", *(double*)data);

    surface_wob_setpoint_command = *(double*)data;
  }
  else if (m_remote_control_request == m) {
    FUSION_ASSERT(len == sizeof remote_control_request);

//    ::fprintf(stdout, "cb:\remote_control_request=%g\n", *(bool*)data);

    remote_control_request = *(bool*)data;
  }
  else if (m_user_remote_control_toggle == m) {
    FUSION_ASSERT(len == 0);

//    ::fprintf(stdout, "cb:\remote_control_request\n");

    user_remote_control_toggle = true;
  }
  else
    FUSION_DEBUG("unexpected message=%d", m);

  remote_control_enabled = m_surface_wob_setpoint_command > 20;

  return nf::ERR_OK;
}

////////////////////////////////////////////////////////////////////////////////
int sim() {
  MCREATE(msg_surface_wob_setpoint_command, O_RDONLY, m_surface_wob_setpoint_command,  surface_wob_setpoint_command);
  MCREATE(msg_user_remote_control_toggle,   O_RDONLY, m_user_remote_control_toggle,    user_remote_control_toggle); //#
  MCREATE(msg_remote_control_request,       O_RDONLY, m_remote_control_request,        remote_control_request);

  MCREATE(msg_surface_wob,                  O_WRONLY, m_surface_wob,                   surface_wob);
  MCREATE(msg_surface_wob_setpoint,         O_WRONLY, m_surface_wob_setpoint,          surface_wob_setpoint);
  MCREATE(msg_highspeed_downhole_wob,       O_WRONLY, m_highspeed_downhole_wob,        highspeed_downhole_wob);
  MCREATE(msg_autodriller_loop_in_control,  O_WRONLY, m_autodriller_loop_in_control,   autodriller_loop_in_control); //#
  MCREATE(msg_remote_control_enabled,       O_WRONLY, m_remote_control_enabled,        remote_control_enabled);

  e = client.subscribe(m_surface_wob_setpoint_command, nf::SF_PUBLISH, nf::CM_MANUAL, sim_cb);

  if (e != nf::ERR_OK) {
    FUSION_DEBUG("subscribe m_surface_wob_setpoint_command=%d", e);

    return 1;
  }

  e = client.subscribe(m_remote_control_request, nf::SF_PUBLISH, nf::CM_MANUAL, sim_cb);

  if (e != nf::ERR_OK) {
    FUSION_DEBUG("subscribe m_remote_control_request=%d", e);

    return 1;
  }

  e = client.subscribe(m_user_remote_control_toggle, nf::SF_PUBLISH, nf::CM_MANUAL, sim_cb);

  if (e != nf::ERR_OK) {
    FUSION_DEBUG("subscribe m_user_remote_control_toggle=%d", e);

    return 1;
  }

  nf::msecs_t ms0 = nf::now_msecs();

  while (client.dispatch(delay, true) == nf::ERR_OK) {
    nf::msecs_t ms1 = nf::now_msecs();

    if (ms1 - ms0 > 1000) {
      ::fprintf(stdout, "%d\n", nr);
      ms0 = ms1;
      nr = 0;
    }

    //...
    if (remote_control_enabled) {
      user_remote_control_toggle = false;

      remote_control_request = !remote_control_request;
      remote_control_enabled = !remote_control_request;

//    client.publish(m_remote_control_enabled, sizeof remote_control_enabled, &remote_control_enabled);
    }
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
int main(int argc, const char** argv) {
  const char* port    = "3001";
  const char* host    = "127.0.0.1";
  const char* profile = "truedrill";
  bool truedrill      = false;
  char connect[250]   = {};

  for (int i = 1; i < argc; ++i)
    if (     !::strncmp(argv[i], "--port=", 7))
      port = argv[i] + 7;
    else if (!::strncmp(argv[i], "--host=", 7))
      host = argv[i] + 7;
    else if (!::strncmp(argv[i], "--profile=", 10))
      profile = argv[i] + 10;
    else if (!::strncmp(argv[i], "--delay=", 8))
      delay = ::atoi(argv[i] + 8);
    else if (!::strncmp(argv[i], "--runas=", 8))
      truedrill = !::stricmp(argv[i] + 8, "truedrill");
    else {
      fprintf(stderr, "usage: %s --host=IP4ADDR --port=PORT --profile=PROFILE [--runas=truedrill]\n", argv[0]);

      return 1;
    }

  _snprintf(connect, sizeof connect - 1, "type=tcp host=%s port=%s", host, port);

  fprintf(stdout, "host=%s port=%s profile=%s\n", host, port, profile);

  e = client.reg(connect, profile);

  if (e != nf::ERR_OK) {
    FUSION_DEBUG("client.reg=%d", e);

    return 1;
  }

  return truedrill ? td() : sim();
}

