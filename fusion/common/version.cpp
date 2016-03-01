/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include "include/nf_macros.h"
#include "include/version.h"
#include "include/genver.h"

namespace nf {
  version_t ver = {
    {
      VER_MAJ,
      VER_MIN,
    },

    GENVER_BUILD,
    GENVER_STRING,
  };
}
