/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#define MCREATE(N, F, T, M, Z) {                      \
  using namespace nf;                                 \
                                                      \
  const size_t sz  = Z;                               \
  result_t e = client.mcreate(N, F, T, M, sz);        \
                                                      \
  if (e != ERR_OK) {                                  \
    FUSION_DEBUG("client.mcreate(%s)=%d", N, e);      \
    stop = true;                                      \
    return e;                                         \
  }                                                   \
}

#define MOPEN(N, F, T, M, Z) {                        \
  using namespace nf;                                 \
                                                      \
  size_t sz;                                          \
  mtype_t t;                                          \
  result_t e = client.mopen(N, F, t, M, sz);          \
                                                      \
  if (e != ERR_OK) {                                  \
    FUSION_DEBUG("client.mopen(%s)=%d", N, e);        \
    stop = true;                                      \
    return e;                                         \
  }                                                   \
                                                      \
  FUSION_ASSERT(sz == Z);                             \
  FUSION_ASSERT(t  == (T));                           \
}

#define SUBSCRIBE(M, F, CB) {                         \
  using namespace nf;                                 \
                                                      \
  result_t e = client.subscribe(M, F, CM_MANUAL, CB); \
                                                      \
  if (e != ERR_OK) {                                  \
    FUSION_DEBUG("subscribe " STRINGIFY(M) "=%d", e); \
    stop = true;                                      \
    return e;                                         \
  }                                                   \
}

#define PUBLISH(M, V) {                               \
  using namespace nf;                                 \
                                                      \
  result_t e = client.publish(M, sizeof V, &V);       \
                                                      \
  if (e != ERR_OK) {                                  \
    stop = true;                                      \
    FUSION_DEBUG("subscribe " STRINGIFY(M) "=%d", e); \
  }                                                   \
}


// these should go to COMMON INTER-APP PLACE
const char* AUTODRILLER_PARAMETER_IN_CONTROL                            = "autodriller_parameter_in_control";
const char* DEFAULT_DOWNHOLE_WEIGHT_ON_BIT                              = "default_downhole_weight_on_bit";
const char* DEFAULT_HOOK_LOAD                                           = "default_hook_load";
const char* DEFAULT_STANDPIPE_PRESSURE                                  = "default_standpipe_pressure";
const char* DEFAULT_SURFACE_WEIGHT_ON_BIT                               = "default_surface_weight_on_bit";
const char* SURFACE_WEIGHT_ON_BIT_CONTROL_REQUEST                       = "surface_weight_on_bit_control_request";
const char* SURFACE_WEIGHT_ON_BIT_SETPOINT                              = "surface_weight_on_bit_setpoint";
const char* SURFACE_WEIGHT_ON_BIT_SETPOINT_CONTROL_REQUEST              = "surface_weight_on_bit_setpoint_control_request";
const char* SURFACE_WEIGHT_ON_BIT_SETPOINT_CONTROL_GRANTED              = "surface_weight_on_bit_setpoint_control_granted";
const char* SURFACE_WEIGHT_ON_BIT_SETPOINT_MAXIMUM_OPERATING_LIMIT      = "surface_weight_on_bit_setpoint_maximum_operating_limit";
const char* SURFACE_WEIGHT_ON_BIT_SETPOINT_MINIMUM_OPERATING_LIMIT      = "surface_weight_on_bit_setpoint_minimum_operating_limit";

const size_t SZ_AUTODRILLER_PARAMETER_IN_CONTROL                        = 4;
const size_t SZ_DEFAULT_DOWNHOLE_WEIGHT_ON_BIT                          = 8;
const size_t SZ_DEFAULT_HOOK_LOAD                                       = 8;
const size_t SZ_DEFAULT_STANDPIPE_PRESSURE                              = 8;
const size_t SZ_DEFAULT_SURFACE_WEIGHT_ON_BIT                           = 8;
const size_t SZ_SURFACE_WEIGHT_ON_BIT_CONTROL_REQUEST                   = 1;
const size_t SZ_SURFACE_WEIGHT_ON_BIT_SETPOINT                          = 8;
const size_t SZ_SURFACE_WEIGHT_ON_BIT_SETPOINT_CONTROL_REQUEST          = 1;
const size_t SZ_SURFACE_WEIGHT_ON_BIT_SETPOINT_CONTROL_GRANTED          = 1;
const size_t SZ_SURFACE_WEIGHT_ON_BIT_SETPOINT_MAXIMUM_OPERATING_LIMIT  = 8;
const size_t SZ_SURFACE_WEIGHT_ON_BIT_SETPOINT_MINIMUM_OPERATING_LIMIT  = 8;

// these are td20 specific
const char* TDDS_DRILLER_REMOTE_CONTROL_TOGGLE_INPUT                    = "tdds_driller_remote_control_toggle_input";
const char* TDDS_SIMULATOR_CYCLE_TIME_INTERVAL                          = "tdds_simulator_cycle_time_interval";

const size_t SZ_TDDS_DRILLER_REMOTE_CONTROL_TOGGLE_INPUT                = 1;
const size_t SZ_TDDS_SIMULATOR_CYCLE_TIME_INTERVAL                      = 4;

