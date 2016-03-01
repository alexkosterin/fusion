/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_PROFILE_H
#define		FUSION_PROFILE_H

#include "include/nf.h"
#include "include/configure.h"
#include "include/md.h"

#include <vector>

#define PROFILE_LINE_MAX  MAX_PATH
#define NO_HOME           "<>"

namespace nf {

  // profile control block /////////////////////////////////////////////////////
  struct pcb_t : profile_opts_t {
    char*		            id_; // id (=filename)
    bool                enabled_;
    char*               home_;
    std::vector<char*>  paths_;
    bool                allow_paths_;
    bool                allow_absolute_path_;
    bool                allow_modify_messages_;
    bool                allow_modify_groups_;
    bool                allow_send_echo_;
    bool                allow_send_stop_;
    bool                allow_send_terminate_;
    bool                allow_mlist_;
    bool                allow_multi_profiles_;
    bool                allow_allow_use_system_info;

    pcb_t(const char* id);
    ~pcb_t();

    struct link_t {
       link_t*      prev_;
       const char*  name_;
    };

    static pcb_t* create(const char* name, pcb_t* pcb = 0, link_t* parent = 0);
  };
}

#endif
