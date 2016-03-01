// app_common.h

#ifndef APP_COMMON_H
#define APP_COMMON_H

// TO DO: This file may be duplicated in various solutions. Move to Fusion common in future.

enum class fusion_app_in_control_ids
{
	none=0,
	td=1,
	ds=2,
	dhds=3,
	rtads=4
};

// Autodriller parameter-in-control states
enum class autodriller_control_parameters
{
	none=0,
	ROP=1,
	WOB=2,
	torque=3,
	diffP=4
};

#endif // APP_COMMON_H