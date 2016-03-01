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
#include "messages.h"	      // FUSION MCB

# define NOGDI
# define _WINSOCKAPI_
#include <windows.h>

enum {
  MODE_INSTALL,
  MODE_UNINSTALL,
  MODE_APP,
  MODE_SIM,
};

static nf::client_t client("Quarkonics app template");
static nf::result_t e;
static int    delay = 1000;
static bool   stop  = false;
static size_t nr    = 0;
static int    mode  = MODE_APP;

double  EMS_Accl_X;
double  EMS_Accl_Y;
double  EMS_Accl_Z;
double  EMS_AxialAccl_Max;
double  EMS_AxialAccl_Mean;
double  EMS_AxialAccl_Min;
double  EMS_AxialAccl_Std;
bool    EMS_BitOnBottom;
double  EMS_Block_Position;
double  EMS_DiffPressure;
double  EMS_Lateral_Max;
double  EMS_Lateral_Mean;
double  EMS_Lateral_Min;
double  EMS_Lateral_Std;
double  EMS_RPM_Downhole;
double  EMS_RPM_Min;
double  EMS_RPM_Max;
double  EMS_RPM_Std;
double  EMS_Torque_Downhole;
double  EMS_WOB_Downhole;
double  EMS_WOB_Error_Downhole;
double  EMS_Torsional_Max;
double  EMS_Torsional_Mean;
double  EMS_Torsional_Min;
double  EMS_Torsional_Std;
double  EMS_Downhole_time;
double  Surface_WOB_SP;
double  Surface_RPM_SP;

double  Surface_WOB_SPCmd;
double  Surface_RPM_SPCmd;

const char* MSG_EMS_ACCL_X              = "EMS.Accl_X";
const char* MSG_EMS_ACCL_Y              = "EMS.Accl_Y";
const char* MSG_EMS_ACCL_Z              = "EMS.Accl_Z";
const char* MSG_EMS_AXIALACCL_MAX       = "EMS.AxialAccl_Max";
const char* MSG_EMS_AXIALACCL_MEAN      = "EMS.AxialAccl_Mean";
const char* MSG_EMS_AXIALACCL_MIN       = "EMS.AxialAccl_Min";
const char* MSG_EMS_AXIALACCL_STD       = "EMS.AxialAccl_Std";
const char* MSG_EMS_BITONBOTTOM         = "EMS.BitOnBottom";
const char* MSG_EMS_BLOCK_POSITION      = "EMS.Block_Position";
const char* MSG_EMS_DIFFPRESSURE        = "EMS.DiffPressure";
const char* MSG_EMS_LATERAL_MAX         = "EMS.Lateral_Max";
const char* MSG_EMS_LATERAL_MEAN        = "EMS.Lateral_Mean";
const char* MSG_EMS_LATERAL_MIN         = "EMS.Lateral_Min";
const char* MSG_EMS_LATERAL_STD         = "EMS.Lateral_Std";
const char* MSG_EMS_RPM_DOWNHOLE        = "EMS.RPM_Downhole";
const char* MSG_EMS_RPM_MIN             = "EMS.RPM_Min";
const char* MSG_EMS_RPM_MAX             = "EMS.RPM_Max";
const char* MSG_EMS_RPM_STD             = "EMS.RPM_Std";
const char* MSG_EMS_TORQUE_DOWNHOLE     = "EMS.Torque_Downhole";
const char* MSG_EMS_WOB_DOWNHOLE        = "EMS.WOB_Downhole";
const char* MSG_EMS_WOB_ERROR_DOWNHOLE  = "EMS.WOB_Error_Downhole";
const char* MSG_EMS_TORSIONAL_MAX       = "EMS.Torsional_Max";
const char* MSG_EMS_TORSIONAL_MEAN      = "EMS.Torsional_Mean";
const char* MSG_EMS_TORSIONAL_MIN       = "EMS.Torsional_Min";
const char* MSG_EMS_TORSIONAL_STD       = "EMS.Torsional_Std";
const char* MSG_EMS_DOWNHOLE_TIME       = "EMS.Downhole_time";
const char* MSG_SURFACE_WOB_SP          = "Surface.WOB_SP";
const char* MSG_SURFACE_RPM_SP          = "Surface.RPM_SP";

const char* MSG_SURFACE_WOB_SPCMD       = "Surface.WOB_SPCmd";
const char* MSG_SURFACE_RPM_SPCMD       = "Surface.RPM_SPCmd";

nf::mid_t m_EMS_Accl_X;
nf::mid_t m_EMS_Accl_Y;
nf::mid_t m_EMS_Accl_Z;
nf::mid_t m_EMS_AxialAccl_Max;
nf::mid_t m_EMS_AxialAccl_Mean;
nf::mid_t m_EMS_AxialAccl_Min;
nf::mid_t m_EMS_AxialAccl_Std;
nf::mid_t m_EMS_BitOnBottom;
nf::mid_t m_EMS_Block_Position;
nf::mid_t m_EMS_DiffPressure;
nf::mid_t m_EMS_Lateral_Max;
nf::mid_t m_EMS_Lateral_Mean;
nf::mid_t m_EMS_Lateral_Min;
nf::mid_t m_EMS_Lateral_Std;
nf::mid_t m_EMS_RPM_Downhole;
nf::mid_t m_EMS_RPM_Min;
nf::mid_t m_EMS_RPM_Max;
nf::mid_t m_EMS_RPM_Std;
nf::mid_t m_EMS_Torque_Downhole;
nf::mid_t m_EMS_WOB_Downhole;
nf::mid_t m_EMS_WOB_Error_Downhole;
nf::mid_t m_EMS_Torsional_Max;
nf::mid_t m_EMS_Torsional_Mean;
nf::mid_t m_EMS_Torsional_Min;
nf::mid_t m_EMS_Torsional_Std;
nf::mid_t m_EMS_Downhole_time;
nf::mid_t m_Surface_WOB_SP;
nf::mid_t m_Surface_RPM_SP;

nf::mid_t m_Surface_WOB_SPCmd;
nf::mid_t m_Surface_RPM_SPCmd;

////////////////////////////////////////////////////////////////////////////////
BOOL WINAPI on_ctrl_break(DWORD CtrlType) {
  stop = true;

  return TRUE;
}

////////////////////////////////////////////////////////////////////////////////
nf::result_t __stdcall callback(nf::mid_t m, size_t len, const void *data) {
  fprintf(stdout, "callback: m=%d len=%d\n", m, len);

  return nf::ERR_OK;
}

////////////////////////////////////////////////////////////////////////////////
int install() {
  MCREATE(MSG_EMS_ACCL_X,             O_RDONLY, MT_EVENT,               m_EMS_Accl_X,             sizeof EMS_Accl_X);
  MCREATE(MSG_EMS_ACCL_Y,             O_RDONLY, MT_EVENT,               m_EMS_Accl_Y,             sizeof EMS_Accl_Y);
  MCREATE(MSG_EMS_ACCL_Z,             O_RDONLY, MT_EVENT,               m_EMS_Accl_Z,             sizeof EMS_Accl_Z);
  MCREATE(MSG_EMS_AXIALACCL_MAX,      O_RDONLY, MT_EVENT,               m_EMS_AxialAccl_Max,      sizeof EMS_AxialAccl_Max);
  MCREATE(MSG_EMS_AXIALACCL_MEAN,     O_RDONLY, MT_EVENT,               m_EMS_AxialAccl_Mean,     sizeof EMS_AxialAccl_Mean);
  MCREATE(MSG_EMS_AXIALACCL_MIN,      O_RDONLY, MT_EVENT,               m_EMS_AxialAccl_Min,      sizeof EMS_AxialAccl_Min);
  MCREATE(MSG_EMS_AXIALACCL_STD,      O_RDONLY, MT_EVENT,               m_EMS_AxialAccl_Std,      sizeof EMS_AxialAccl_Std);
  MCREATE(MSG_EMS_BITONBOTTOM,        O_RDONLY, MT_EVENT|MT_PERSISTENT, m_EMS_BitOnBottom,        sizeof EMS_BitOnBottom);
  MCREATE(MSG_EMS_BLOCK_POSITION,     O_RDONLY, MT_EVENT,               m_EMS_Block_Position,     sizeof EMS_Block_Position);
  MCREATE(MSG_EMS_DIFFPRESSURE,       O_RDONLY, MT_EVENT,               m_EMS_DiffPressure,       sizeof EMS_DiffPressure);
  MCREATE(MSG_EMS_LATERAL_MAX,        O_RDONLY, MT_EVENT,               m_EMS_Lateral_Max,        sizeof EMS_Lateral_Max);
  MCREATE(MSG_EMS_LATERAL_MEAN,       O_RDONLY, MT_EVENT,               m_EMS_Lateral_Mean,       sizeof EMS_Lateral_Mean);
  MCREATE(MSG_EMS_LATERAL_MIN,        O_RDONLY, MT_EVENT,               m_EMS_Lateral_Min,        sizeof EMS_Lateral_Min);
  MCREATE(MSG_EMS_LATERAL_STD,        O_RDONLY, MT_EVENT,               m_EMS_Lateral_Std,        sizeof EMS_Lateral_Std);
  MCREATE(MSG_EMS_RPM_DOWNHOLE,       O_RDONLY, MT_EVENT,               m_EMS_RPM_Downhole,       sizeof EMS_RPM_Downhole);
  MCREATE(MSG_EMS_RPM_MIN,            O_RDONLY, MT_EVENT,               m_EMS_RPM_Min,            sizeof EMS_RPM_Min);
  MCREATE(MSG_EMS_RPM_MAX,            O_RDONLY, MT_EVENT,               m_EMS_RPM_Max,            sizeof EMS_RPM_Max);
  MCREATE(MSG_EMS_RPM_STD,            O_RDONLY, MT_EVENT,               m_EMS_RPM_Std,            sizeof EMS_RPM_Std);
  MCREATE(MSG_EMS_TORQUE_DOWNHOLE,    O_RDONLY, MT_EVENT,               m_EMS_Torque_Downhole,    sizeof EMS_Torque_Downhole);
  MCREATE(MSG_EMS_WOB_DOWNHOLE,       O_RDONLY, MT_EVENT,               m_EMS_WOB_Downhole,       sizeof EMS_WOB_Downhole);
  MCREATE(MSG_EMS_WOB_ERROR_DOWNHOLE, O_RDONLY, MT_EVENT,               m_EMS_WOB_Error_Downhole, sizeof EMS_WOB_Error_Downhole);
  MCREATE(MSG_EMS_TORSIONAL_MAX,      O_RDONLY, MT_EVENT,               m_EMS_Torsional_Max,      sizeof EMS_Torsional_Max);
  MCREATE(MSG_EMS_TORSIONAL_MEAN,     O_RDONLY, MT_EVENT,               m_EMS_Torsional_Mean,     sizeof EMS_Torsional_Mean);
  MCREATE(MSG_EMS_TORSIONAL_MIN,      O_RDONLY, MT_EVENT,               m_EMS_Torsional_Min,      sizeof EMS_Torsional_Min);
  MCREATE(MSG_EMS_TORSIONAL_STD,      O_RDONLY, MT_EVENT,               m_EMS_Torsional_Std,      sizeof EMS_Torsional_Std);
  MCREATE(MSG_EMS_DOWNHOLE_TIME,      O_RDONLY, MT_EVENT,               m_EMS_Downhole_time,      sizeof EMS_Downhole_time);
  MCREATE(MSG_SURFACE_WOB_SP,         O_RDONLY, MT_EVENT,               m_Surface_WOB_SP,         sizeof Surface_WOB_SP);
  MCREATE(MSG_SURFACE_RPM_SP,         O_RDONLY, MT_EVENT,               m_Surface_RPM_SP,         sizeof Surface_RPM_SP);
  MCREATE(MSG_SURFACE_WOB_SPCMD,      O_RDONLY, MT_EVENT,               m_Surface_WOB_SPCmd,      sizeof Surface_WOB_SPCmd);
  MCREATE(MSG_SURFACE_RPM_SPCMD,      O_RDONLY, MT_EVENT,               m_Surface_RPM_SPCmd,      sizeof Surface_RPM_SPCmd);

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
int uninstall() {
  return 1;
}

////////////////////////////////////////////////////////////////////////////////
int app() {
  MOPEN(MSG_EMS_ACCL_X,               O_RDONLY, MT_EVENT,               m_EMS_Accl_X,             sizeof EMS_Accl_X);
  MOPEN(MSG_EMS_ACCL_Y,               O_RDONLY, MT_EVENT,               m_EMS_Accl_Y,             sizeof EMS_Accl_Y);
  MOPEN(MSG_EMS_ACCL_Z,               O_RDONLY, MT_EVENT,               m_EMS_Accl_Z,             sizeof EMS_Accl_Z);
  MOPEN(MSG_EMS_AXIALACCL_MAX,        O_RDONLY, MT_EVENT,               m_EMS_AxialAccl_Max,      sizeof EMS_AxialAccl_Max);
  MOPEN(MSG_EMS_AXIALACCL_MEAN,       O_RDONLY, MT_EVENT,               m_EMS_AxialAccl_Mean,     sizeof EMS_AxialAccl_Mean);
  MOPEN(MSG_EMS_AXIALACCL_MIN,        O_RDONLY, MT_EVENT,               m_EMS_AxialAccl_Min,      sizeof EMS_AxialAccl_Min);
  MOPEN(MSG_EMS_AXIALACCL_STD,        O_RDONLY, MT_EVENT,               m_EMS_AxialAccl_Std,      sizeof EMS_AxialAccl_Std);
  MOPEN(MSG_EMS_BITONBOTTOM,          O_RDONLY, MT_EVENT|MT_PERSISTENT, m_EMS_BitOnBottom,        sizeof EMS_BitOnBottom);
  MOPEN(MSG_EMS_BLOCK_POSITION,       O_RDONLY, MT_EVENT,               m_EMS_Block_Position,     sizeof EMS_Block_Position);
  MOPEN(MSG_EMS_DIFFPRESSURE,         O_RDONLY, MT_EVENT,               m_EMS_DiffPressure,       sizeof EMS_DiffPressure);
  MOPEN(MSG_EMS_LATERAL_MAX,          O_RDONLY, MT_EVENT,               m_EMS_Lateral_Max,        sizeof EMS_Lateral_Max);
  MOPEN(MSG_EMS_LATERAL_MEAN,         O_RDONLY, MT_EVENT,               m_EMS_Lateral_Mean,       sizeof EMS_Lateral_Mean);
  MOPEN(MSG_EMS_LATERAL_MIN,          O_RDONLY, MT_EVENT,               m_EMS_Lateral_Min,        sizeof EMS_Lateral_Min);
  MOPEN(MSG_EMS_LATERAL_STD,          O_RDONLY, MT_EVENT,               m_EMS_Lateral_Std,        sizeof EMS_Lateral_Std);
  MOPEN(MSG_EMS_RPM_DOWNHOLE,         O_RDONLY, MT_EVENT,               m_EMS_RPM_Downhole,       sizeof EMS_RPM_Downhole);
  MOPEN(MSG_EMS_RPM_MIN,              O_RDONLY, MT_EVENT,               m_EMS_RPM_Min,            sizeof EMS_RPM_Min);
  MOPEN(MSG_EMS_RPM_MAX,              O_RDONLY, MT_EVENT,               m_EMS_RPM_Max,            sizeof EMS_RPM_Max);
  MOPEN(MSG_EMS_RPM_STD,              O_RDONLY, MT_EVENT,               m_EMS_RPM_Std,            sizeof EMS_RPM_Std);
  MOPEN(MSG_EMS_TORQUE_DOWNHOLE,      O_RDONLY, MT_EVENT,               m_EMS_Torque_Downhole,    sizeof EMS_Torque_Downhole);
  MOPEN(MSG_EMS_WOB_DOWNHOLE,         O_RDONLY, MT_EVENT,               m_EMS_WOB_Downhole,       sizeof EMS_WOB_Downhole);
  MOPEN(MSG_EMS_WOB_ERROR_DOWNHOLE,   O_RDONLY, MT_EVENT,               m_EMS_WOB_Error_Downhole, sizeof EMS_WOB_Error_Downhole);
  MOPEN(MSG_EMS_TORSIONAL_MAX,        O_RDONLY, MT_EVENT,               m_EMS_Torsional_Max,      sizeof EMS_Torsional_Max);
  MOPEN(MSG_EMS_TORSIONAL_MEAN,       O_RDONLY, MT_EVENT,               m_EMS_Torsional_Mean,     sizeof EMS_Torsional_Mean);
  MOPEN(MSG_EMS_TORSIONAL_MIN,        O_RDONLY, MT_EVENT,               m_EMS_Torsional_Min,      sizeof EMS_Torsional_Min);
  MOPEN(MSG_EMS_TORSIONAL_STD,        O_RDONLY, MT_EVENT,               m_EMS_Torsional_Std,      sizeof EMS_Torsional_Std);
  MOPEN(MSG_EMS_DOWNHOLE_TIME,        O_RDONLY, MT_EVENT,               m_EMS_Downhole_time,      sizeof EMS_Downhole_time);
  MOPEN(MSG_SURFACE_WOB_SP,           O_RDONLY, MT_EVENT,               m_Surface_WOB_SP,         sizeof Surface_WOB_SP);
  MOPEN(MSG_SURFACE_RPM_SP,           O_RDONLY, MT_EVENT,               m_Surface_RPM_SP,         sizeof Surface_RPM_SP);
  MOPEN(MSG_SURFACE_WOB_SPCMD,        O_WRONLY, MT_EVENT,               m_Surface_WOB_SPCmd,      sizeof Surface_WOB_SPCmd);
  MOPEN(MSG_SURFACE_RPM_SPCMD,        O_WRONLY, MT_EVENT,               m_Surface_RPM_SPCmd,      sizeof Surface_RPM_SPCmd);

  SUBSCRIBE(m_EMS_Accl_X,             SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_Accl_Y,             SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_Accl_Z,             SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_AxialAccl_Max,      SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_AxialAccl_Mean,     SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_AxialAccl_Min,      SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_AxialAccl_Std,      SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_BitOnBottom,        SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_Block_Position,     SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_DiffPressure,       SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_Lateral_Max,        SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_Lateral_Mean,       SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_Lateral_Min,        SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_Lateral_Std,        SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_RPM_Downhole,       SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_RPM_Min,            SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_RPM_Max,            SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_RPM_Std,            SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_Torque_Downhole,    SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_WOB_Downhole,       SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_WOB_Error_Downhole, SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_Torsional_Max,      SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_Torsional_Mean,     SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_Torsional_Min,      SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_Torsional_Std,      SF_PUBLISH,   callback);
  SUBSCRIBE(m_EMS_Downhole_time,      SF_PUBLISH,   callback);
  SUBSCRIBE(m_Surface_WOB_SP,         SF_PUBLISH,   callback);
  SUBSCRIBE(m_Surface_RPM_SP,         SF_PUBLISH,   callback);

  while (!stop && client.dispatch(delay, true) == nf::ERR_OK) {
    /****/;
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
VOID APIENTRY on_hi_timer(LPVOID lpArgToCompletionRoutine, DWORD dwTimerLowValue, DWORD dwTimerHighValue) {
  EMS_Accl_X          = 100.0 + ::rand() / 1000.0;
  EMS_Accl_Y          = ::rand() / 2;
  EMS_Accl_Z          = ::rand() / 3;
  EMS_BitOnBottom     = ::rand() > (RAND_MAX/2);
  EMS_Block_Position  = ::rand() / 4;
  EMS_DiffPressure    = ::rand() / 5;
  EMS_RPM_Downhole    = ::rand() / 6;
  EMS_Torque_Downhole = ::rand() / 7;
  EMS_WOB_Downhole    = ::rand() / 8;
  EMS_RPM_Max         = ::rand() / 9;
  EMS_Downhole_time   = ::rand() / 10;

  PUBLISH(m_EMS_Accl_X,           EMS_Accl_X);
  PUBLISH(m_EMS_Accl_Y,           EMS_Accl_Y);
  PUBLISH(m_EMS_Accl_Z,           EMS_Accl_Z);
  PUBLISH(m_EMS_BitOnBottom,      EMS_BitOnBottom);
  PUBLISH(m_EMS_Block_Position,   EMS_Block_Position);
  PUBLISH(m_EMS_DiffPressure,     EMS_DiffPressure);
  PUBLISH(m_EMS_RPM_Downhole,     EMS_RPM_Downhole);
  PUBLISH(m_EMS_Torque_Downhole,  EMS_Torque_Downhole);
  PUBLISH(m_EMS_WOB_Downhole,     EMS_WOB_Downhole);
  PUBLISH(m_EMS_RPM_Max,          EMS_RPM_Max);
  PUBLISH(m_EMS_Downhole_time,    EMS_Downhole_time);
}

////////////////////////////////////////////////////////////////////////////////
VOID APIENTRY on_md_timer(LPVOID lpArgToCompletionRoutine, DWORD dwTimerLowValue, DWORD dwTimerHighValue) {
  Surface_WOB_SP  =       ::rand();
  Surface_RPM_SP  =       ::rand();

  PUBLISH(m_Surface_WOB_SP,       Surface_WOB_SP);
  PUBLISH(m_Surface_RPM_SP,       Surface_RPM_SP);
}

////////////////////////////////////////////////////////////////////////////////
VOID APIENTRY on_lo_timer(LPVOID lpArgToCompletionRoutine, DWORD dwTimerLowValue, DWORD dwTimerHighValue) {
  EMS_AxialAccl_Max       = ::rand();
  EMS_AxialAccl_Mean      = ::rand();
  EMS_AxialAccl_Min       = ::rand();
  EMS_AxialAccl_Std       = ::rand();
  EMS_Lateral_Max         = ::rand();
  EMS_Lateral_Mean        = ::rand();
  EMS_Lateral_Min         = ::rand();
  EMS_Lateral_Std         = ::rand();
  EMS_RPM_Min             = ::rand();
  EMS_RPM_Max             = ::rand();
  EMS_RPM_Std             = ::rand();
  EMS_WOB_Error_Downhole  = ::rand();
  EMS_Torsional_Max       = ::rand();
  EMS_Torsional_Mean      = ::rand();
  EMS_Torsional_Min       = ::rand();
  EMS_Torsional_Std       = ::rand();

  PUBLISH(m_EMS_AxialAccl_Max,      EMS_AxialAccl_Max);
  PUBLISH(m_EMS_AxialAccl_Mean,     EMS_AxialAccl_Mean);
  PUBLISH(m_EMS_AxialAccl_Min,      EMS_AxialAccl_Min);
  PUBLISH(m_EMS_AxialAccl_Std,      EMS_AxialAccl_Std);
  PUBLISH(m_EMS_Lateral_Max,        EMS_Lateral_Max);
  PUBLISH(m_EMS_Lateral_Mean,       EMS_Lateral_Mean);
  PUBLISH(m_EMS_Lateral_Min,        EMS_Lateral_Min);
  PUBLISH(m_EMS_Lateral_Std,        EMS_Lateral_Std);
  PUBLISH(m_EMS_RPM_Min,            EMS_RPM_Min);
  PUBLISH(m_EMS_RPM_Max,            EMS_RPM_Max);
  PUBLISH(m_EMS_RPM_Std,            EMS_RPM_Std);
  PUBLISH(m_EMS_WOB_Error_Downhole, EMS_WOB_Error_Downhole);
  PUBLISH(m_EMS_Torsional_Max,      EMS_Torsional_Max);
  PUBLISH(m_EMS_Torsional_Mean,     EMS_Torsional_Mean);
  PUBLISH(m_EMS_Torsional_Min,      EMS_Torsional_Min);
  PUBLISH(m_EMS_Torsional_Std,      EMS_Torsional_Std);
}

////////////////////////////////////////////////////////////////////////////////
int sim() {
  MOPEN(MSG_EMS_ACCL_X,             O_WRONLY, MT_EVENT,               m_EMS_Accl_X,              sizeof EMS_Accl_X);
  MOPEN(MSG_EMS_ACCL_Y,             O_WRONLY, MT_EVENT,               m_EMS_Accl_Y,              sizeof EMS_Accl_Y);
  MOPEN(MSG_EMS_ACCL_Z,             O_WRONLY, MT_EVENT,               m_EMS_Accl_Z,              sizeof EMS_Accl_Z);
  MOPEN(MSG_EMS_AXIALACCL_MAX,      O_WRONLY, MT_EVENT,               m_EMS_AxialAccl_Max,       sizeof EMS_AxialAccl_Max);
  MOPEN(MSG_EMS_AXIALACCL_MEAN,     O_WRONLY, MT_EVENT,               m_EMS_AxialAccl_Mean,      sizeof EMS_AxialAccl_Mean);
  MOPEN(MSG_EMS_AXIALACCL_MIN,      O_WRONLY, MT_EVENT,               m_EMS_AxialAccl_Min,       sizeof EMS_AxialAccl_Min);
  MOPEN(MSG_EMS_AXIALACCL_STD,      O_WRONLY, MT_EVENT,               m_EMS_AxialAccl_Std,       sizeof EMS_AxialAccl_Std);
  MOPEN(MSG_EMS_BITONBOTTOM,        O_WRONLY, MT_EVENT|MT_PERSISTENT, m_EMS_BitOnBottom,         sizeof EMS_BitOnBottom);
  MOPEN(MSG_EMS_BLOCK_POSITION,     O_WRONLY, MT_EVENT,               m_EMS_Block_Position,      sizeof EMS_Block_Position);
  MOPEN(MSG_EMS_DIFFPRESSURE,       O_WRONLY, MT_EVENT,               m_EMS_DiffPressure,        sizeof EMS_DiffPressure);
  MOPEN(MSG_EMS_LATERAL_MAX,        O_WRONLY, MT_EVENT,               m_EMS_Lateral_Max,         sizeof EMS_Lateral_Max);
  MOPEN(MSG_EMS_LATERAL_MEAN,       O_WRONLY, MT_EVENT,               m_EMS_Lateral_Mean,        sizeof EMS_Lateral_Mean);
  MOPEN(MSG_EMS_LATERAL_MIN,        O_WRONLY, MT_EVENT,               m_EMS_Lateral_Min,         sizeof EMS_Lateral_Min);
  MOPEN(MSG_EMS_LATERAL_STD,        O_WRONLY, MT_EVENT,               m_EMS_Lateral_Std,         sizeof EMS_Lateral_Std);
  MOPEN(MSG_EMS_RPM_DOWNHOLE,       O_WRONLY, MT_EVENT,               m_EMS_RPM_Downhole,        sizeof EMS_RPM_Downhole);
  MOPEN(MSG_EMS_RPM_MIN,            O_WRONLY, MT_EVENT,               m_EMS_RPM_Min,             sizeof EMS_RPM_Min);
  MOPEN(MSG_EMS_RPM_MAX,            O_WRONLY, MT_EVENT,               m_EMS_RPM_Max,             sizeof EMS_RPM_Max);
  MOPEN(MSG_EMS_RPM_STD,            O_WRONLY, MT_EVENT,               m_EMS_RPM_Std,             sizeof EMS_RPM_Std);
  MOPEN(MSG_EMS_TORQUE_DOWNHOLE,    O_WRONLY, MT_EVENT,               m_EMS_Torque_Downhole,     sizeof EMS_Torque_Downhole);
  MOPEN(MSG_EMS_WOB_DOWNHOLE,       O_WRONLY, MT_EVENT,               m_EMS_WOB_Downhole,        sizeof EMS_WOB_Downhole);
  MOPEN(MSG_EMS_WOB_ERROR_DOWNHOLE, O_WRONLY, MT_EVENT,               m_EMS_WOB_Error_Downhole,  sizeof EMS_WOB_Error_Downhole);
  MOPEN(MSG_EMS_TORSIONAL_MAX,      O_WRONLY, MT_EVENT,               m_EMS_Torsional_Max,       sizeof EMS_Torsional_Max);
  MOPEN(MSG_EMS_TORSIONAL_MEAN,     O_WRONLY, MT_EVENT,               m_EMS_Torsional_Mean,      sizeof EMS_Torsional_Mean);
  MOPEN(MSG_EMS_TORSIONAL_MIN,      O_WRONLY, MT_EVENT,               m_EMS_Torsional_Min,       sizeof EMS_Torsional_Min);
  MOPEN(MSG_EMS_TORSIONAL_STD,      O_WRONLY, MT_EVENT,               m_EMS_Torsional_Std,       sizeof EMS_Torsional_Std);
  MOPEN(MSG_EMS_DOWNHOLE_TIME,      O_WRONLY, MT_EVENT,               m_EMS_Downhole_time,       sizeof EMS_Downhole_time);
  MOPEN(MSG_SURFACE_WOB_SP,         O_WRONLY, MT_EVENT,               m_Surface_WOB_SP,          sizeof Surface_WOB_SP);
  MOPEN(MSG_SURFACE_RPM_SP,         O_WRONLY, MT_EVENT,               m_Surface_RPM_SP,          sizeof Surface_RPM_SP);

  HANDLE ht_hi  = ::CreateWaitableTimer(0, FALSE, 0);
  HANDLE ht_lo  = ::CreateWaitableTimer(0, FALSE, 0);
  HANDLE ht_md  = ::CreateWaitableTimer(0, FALSE, 0);
  LARGE_INTEGER DueTime;

  DueTime.QuadPart = -1000000;

  ::SetWaitableTimer(ht_hi, &DueTime,   12, on_hi_timer, 0, TRUE);
  ::SetWaitableTimer(ht_md, &DueTime, 1000, on_md_timer, 0, TRUE);
  ::SetWaitableTimer(ht_lo, &DueTime, 2500, on_lo_timer, 0, TRUE);

  while (!stop && client.dispatch(delay, true) == nf::ERR_OK) {
    /****/;
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
int main(int argc, const char** argv) {
  const char* port    = "3001";
  const char* host    = "127.0.0.1";
  const char* profile = "eds";
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
    else if (!::strncmp(argv[i], "--runas=", 8)) {
      if (!::strcmp(argv[i] + 8, "app"))
        mode = MODE_APP;
      else if (!::strcmp(argv[i] + 8, "sim"))
        mode = MODE_SIM;
      else if (!::strcmp(argv[i] + 8, "install"))
        mode = MODE_INSTALL;
      else if (!::strcmp(argv[i] + 8, "uninstall"))
        mode = MODE_UNINSTALL;
      else
        goto usage;
    }
    else {
usage:
      fprintf(stderr, "usage: %s --host=IP4ADDR --port=PORT --profile=PROFILE [--delay=N] [--runas={install|uninstall|app|sim}]\n", argv[0]);

      return 1;
    }

  _snprintf(connect, sizeof connect - 1, "type=tcp host=%s port=%s", host, port);

  fprintf(stdout, "host=%s port=%s profile=%s\n", host, port, profile);

  e = client.reg(connect, profile);

  if (e != nf::ERR_OK) {
    FUSION_DEBUG("client.reg=%d", e);

    return 1;
  }

  ::SetConsoleCtrlHandler(on_ctrl_break, TRUE);

  int rc = 0;

  switch (mode) {
  case MODE_INSTALL:    rc = install();   break;
  case MODE_UNINSTALL:  rc = uninstall(); break;
  case MODE_APP:        rc = app();       break;
  case MODE_SIM:        rc = sim();       break;
  }

  return rc;
}

