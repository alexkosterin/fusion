/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_NTFSSTREAMS_H
#define		FUSION_NTFSSTREAMS_H

#include <include/configure.h>
#include <md/md_internal.h>

namespace nf {
  struct md_t;

  struct imp_t {
    ::HANDLE  hdata_;   // data handle
    ::HANDLE  hmid_;    // mid handle
    ::HANDLE  htype_;   // type handle
    ::HANDLE  hsize_;   // size handle
    int64_t   cookie_;

    void init();
    void fini();

    result_t read(const char* fname, bool existing, ::HANDLE hdata, unsigned* ptype, mid_t* pm, size_t* psize, size_t* plen, void** ppdata);
    result_t write(md_t* md);
    result_t remove(md_t* md);
    result_t close(md_t* md);
  };
}

#endif  //FUSION_NTFSSTREAMS_H
