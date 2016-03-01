/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include "include/lock.h"

_srwlock_t::_srwlock_t() {
    ::InitializeSRWLock(this);
}

wlock_t::wlock_t(_srwlock_t& l) : lock_(l) {
  ::AcquireSRWLockExclusive(&lock_);
}

wlock_t::~wlock_t() {
  ::ReleaseSRWLockExclusive(&lock_);
}

rlock_t::rlock_t(_srwlock_t& l) : lock_(l) {
  ::AcquireSRWLockShared(&lock_);
}

rlock_t::~rlock_t() {
  ::ReleaseSRWLockShared(&lock_);
}

