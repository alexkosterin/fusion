/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_TEXT_IMP_H
#define		FUSION_TEXT_IMP_H

#include <include/configure.h>
#include <md/md_internal.h>

namespace nf {
  struct md_t;

  struct imp_t {
    ::HANDLE  hfile_;
    int64_t   cookie_;

    imp_t(::HANDLE hfile);
    ~imp_t();

    result_t read(mtype_t& type, mid_t& m, size_t& size, msecs_t& atime, msecs_t& ctime, msecs_t& mtime, bool& has_value, size_t* plen, void** ppdata);
    result_t init(md_t* md, msecs_t& atime, msecs_t& ctime, msecs_t& mtime);
    result_t write(md_t* md);
    result_t remove(md_t* md);
  };
}

#endif  //FUSION_TEXT_IMP_H
