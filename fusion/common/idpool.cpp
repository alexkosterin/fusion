/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include "include/idpool.h"

#if 0
  void idpool_static_tests() {
    { idpool_t<4, 4, 15, -1> t;  t.load(0, 0); }  // ok
    { idpool_t<4, 4, 16, -1> t;  t.load(0, 0); }  // bad __RESERVED_LESS_THEN_MAX
    { idpool_t<4, 4,  1,  0> t;  t.load(0, 0); }  // ok
    { idpool_t<4, 4,  1, 16> t;  t.load(0, 0); }  // ok
    { idpool_t<4, 4,  0,  0> t;  t.load(0, 0); }  // bad __BAD_WITHIN_MIN_MAX_RANGE
    { idpool_t<4, 4,  0, 15> t;  t.load(0, 0); }  // bad __BAD_WITHIN_MIN_MAX_RANGE
    { idpool_t<10, 1<<9, 0, -1>  t;  t.load(0, 0); }  // ok
    { idpool_t<10, 1<<23, 0, -1> t;  t.load(0, 0); }  // bad __BIGGEST_STEP, __STEP_FOLD_CAPACITY
    { idpool_t<4, 3, 0, -1> t;   t.load(0, 0); }  // bad __STEP_FOLD_CAPACITY
  }

  //@@@@!!!###
  cid_pool_t::element_t b[cid_pool_t::ARRAY_SIZE] = {
    1,          // 1
    0x00000010, // 1
    0x01020408, // 4
    0x000000ff, // 8
    0xffffffff, // 32
  };            // => 46

  cid_pool.load(cid_pool_t::ARRAY_SIZE, &b);
  //@@@@!!!###
#endif
