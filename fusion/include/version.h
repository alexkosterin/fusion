/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_VERSION_H
#define		FUSION_VERSION_H

#define VER_MAJ 0
#define VER_MIN 5

#ifndef APSTUDIO_INVOKED
#include <stdint.h>

namespace nf {
  struct mini_version_t {
    uint8_t         maj;
    uint8_t         min;
  };

  struct version_t {
    mini_version_t  mini;
    uint16_t	      build;
    const char*	    name;
  };
}
#endif    //APSTUDIO_INVOKED
#endif    //FUSION_VERSION_H
