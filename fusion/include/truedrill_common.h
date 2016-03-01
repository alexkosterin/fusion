/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

// truedrill_common.h

#ifndef TRUEDRILL_COMMON_H
#define TRUEDRILL_COMMON_H

// TO DO: This file may be duplicated in various solutions. Move to Fusion common in future.

enum class truedrill_operating_modes
{
	dead=0,
	idle=1,
	run=2
};

enum class dwob_control_levels
{
	unknown=0,
	not_in_control=1,
	active_control=2,
	limited_control=3,
	waiting=4,
	error=5
};

enum dwob_control_methods
{
	projection=0,
	PID=1
};

#endif // TRUEDRILL_COMMON_H
