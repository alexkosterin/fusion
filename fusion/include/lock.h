/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_LOCK_H
#define		FUSION_LOCK_H

#include  "include/configure.h"
# include <windows.h>

struct _srwlock_t : ::SRWLOCK {
  _srwlock_t();
};

class wlock_t {
  _srwlock_t& lock_;

public:
  wlock_t(_srwlock_t& l);
  ~wlock_t();
};

class rlock_t {
  _srwlock_t& lock_;

public:
  rlock_t(_srwlock_t& l);
  ~rlock_t();
};

#endif	//FUSION_LOCK_H
