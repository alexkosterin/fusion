/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include "md_internal.h"
#include "pcb.h"

#include "string.h"

#define PROFILE_LINE_MAX  MAX_PATH
#define NO_HOME           "<>"
#define TEMP_BUFF_SIZE    80

#if !defined(HAVE_STRNDUP)
char* strndup(const char* src, size_t len) {
  char* dst = (char*)::malloc(len + 1);

  FUSION_ASSERT(dst);

  ::strncpy(dst, src, len);
  dst[len] = 0;

  return dst;
}
#endif

namespace nf {
  extern const char* profile_path__;

  pcb_t::pcb_t(const char* id) : enabled_(true), home_(0),
#if (MBM_PERMISSIVE_PROFILE > 0)
    allow_paths_(true), allow_modify_messages_(true), allow_modify_groups_(true), allow_send_echo_(true), allow_send_stop_(true), allow_send_terminate_(true), allow_mlist_(true), allow_multi_profiles_(true); allow_allow_use_system_info(true), allow_absolute_path_(true)
#else
    allow_paths_(false), allow_modify_messages_(false), allow_modify_groups_(false), allow_send_echo_(true), allow_send_stop_(false), allow_send_terminate_(false), allow_mlist_(false), allow_multi_profiles_(false), allow_allow_use_system_info(false), allow_absolute_path_(false)
#endif
  {
    FUSION_ASSERT(id);
    id_ = _strdup(id);

    clock_sync_period_ = 0;
#if (MBM_PERMISSIVE_PROFILE > 0)
    output_queue_limit_ = 0;
#else
    output_queue_limit_ = MBM_DEFAULT_OUTPUT_QUEUE_LIMIT;
#endif
 }

  pcb_t::~pcb_t() {
    ::free((void*)id_);
    ::free((void*)home_);

    for (auto I = paths_.begin(), E = paths_.end(); I != E; ++I)
      ::free((void*)*I);
  }

  static pcbs_t	pcbs__;

  pcbs_t::~pcbs_t() {
#if DEBUG_HEAP > 0
    if (this == &pcbs__)
      for (auto I = cbegin(), E = cend(); I != E; ++I)
        delete I->second;
#endif
  }

  pcb_t* pcb_t::create(const char* name, pcb_t* pcb, link_t* parent) {
		FUSION_ASSERT(name);

    if (!pcb) {
      pcbs_t::const_iterator I = pcbs__.find(name);

      if (I != pcbs__.cend())
        return I->second;
    }

    char buff[PROFILE_LINE_MAX + 1] = {0};
    char tmp[TEMP_BUFF_SIZE + 1] = {0};
    char path[MAX_PATH + 1] = {0};
    char home[MAX_PATH + 1] = {0};
    std::vector<char*> paths;
    bool enabled = true;
    unsigned int clock_sync_period = 0;

    for (link_t* plink = parent; plink; plink = plink->prev_)
      if (::strcmp(name, plink->name_) == 0) {
        FUSION_WARN("[%s] ignore circular include '%s'", plink->name_, name);

        return 0;
      }

    link_t link = { parent, name };
    int size = ::_snprintf(path, sizeof path, "%s/%s", profile_path__, name);

    FUSION_ASSERT(size < MAX_PATH);

    FILE* fd = ::fopen(path, "r+t");

    if (!fd) {
      FUSION_WARN("::fopen(\"%s\", \"r+t\")=%d", path, errno);

      return 0;
    }

    if (!pcb) {
      pcb = new pcb_t(name);
      pcbs__.insert(std::make_pair(pcb->id_, pcb));
    }

    while (::fgets(buff, PROFILE_LINE_MAX, fd)) {
      size_t nr   = ::strspn(buff, " \t\n\r");
      size_t len  = ::strlen(buff);

      // line is empty
      if (nr == len)
        continue;

      // line is a comment
      if (::strchr(PROFILE_COMMENT_CHARS, buff[nr]))
        continue;

      // chomp line
      while (len && --len) {
        switch(buff[len]) {
        case '\n':
        case '\r':
          buff[len] = 0;
          continue;
        }

        break;
      }

      if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " home = %" STRINGIFY(MAX_PATH) "s", home)) {
        ::free(pcb->home_);
        pcb->home_ = ::strdup(home);
      }
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " enabled = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        pcb->enabled_ = !::strcmp(tmp, "1") || !::stricmp(tmp, "yes") || !::stricmp(tmp, "y");
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " path = %" STRINGIFY(MAX_PATH) "s", path)) {
        const char *pc = path;

        while (pc && *pc) {
          const char *pc1 = ::strchr(pc, PROFILE_PATH_SEPARATOR_CHAR);

          if (size_t len  = pc1 ? pc1 - pc : ::strlen(pc)) {
            char*  path   = ::strndup(pc, len);

            if (len)
              pcb->paths_.push_back(path);

            pc = pc1 ? pc1 + 1 : pc1;
          }
        }
      }
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " include %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp)) {
        if (!pcb_t::create(tmp, pcb, &link))
          FUSION_WARN("profile='%s': include profile \"%s\" not found", name, tmp);
      }
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " clock-sync-period = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp)) {
        pcb->clock_sync_period_ = ::atoi(tmp);

        if (pcb->clock_sync_period_ && pcb->clock_sync_period_ < CLOCK_SYNC_MIN_PERIOD) {
          FUSION_WARN("profile='%s': directive 'clock-sync-period' reset to %d, given=%d ", name, CLOCK_SYNC_MIN_PERIOD, pcb->clock_sync_period_);

          pcb->clock_sync_period_ = CLOCK_SYNC_MIN_PERIOD;
        }
      }
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " output-queue-limit = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp)) {
				pcb->output_queue_limit_ = ::atoi(tmp);

        FUSION_INFO("profile='%s': output-queue-limit=%d ", name, pcb->output_queue_limit_);
			}
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " user = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        FUSION_WARN("profile='%s': ignore 'user' directive=%s", name, buff);
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " password = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        FUSION_WARN("profile='%s': ignore 'password' directive=%s", name, buff);
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " group = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        FUSION_WARN("profile='%s': ignore 'group' directive=%s", name, buff);
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " allow-paths = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        pcb->allow_paths_ = !::strcmp(tmp, "1") || !::stricmp(tmp, "yes") || !::stricmp(tmp, "y");
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " allow-absolute-path = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        pcb->allow_absolute_path_ = !::strcmp(tmp, "1") || !::stricmp(tmp, "yes") || !::stricmp(tmp, "y");
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " allow-modify-messages = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        pcb->allow_modify_messages_ = !::strcmp(tmp, "1") || !::stricmp(tmp, "yes") || !::stricmp(tmp, "y");
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " allow-modify-groups = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        pcb->allow_modify_groups_ = !::strcmp(tmp, "1") || !::stricmp(tmp, "yes") || !::stricmp(tmp, "y");
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " allow-send-echo = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        pcb->allow_send_echo_ = !::strcmp(tmp, "1") || !::stricmp(tmp, "yes") || !::stricmp(tmp, "y");
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " allow-send-stop = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        pcb->allow_send_stop_ = !::strcmp(tmp, "1") || !::stricmp(tmp, "yes") || !::stricmp(tmp, "y");
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " allow-send-terminate = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        pcb->allow_send_terminate_ = !::strcmp(tmp, "1") || !::stricmp(tmp, "yes") || !::stricmp(tmp, "y");
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " allow-mlist = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        pcb->allow_mlist_ = !::strcmp(tmp, "1") || !::stricmp(tmp, "yes") || !::stricmp(tmp, "y");
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " allow-multi-profiles = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        pcb->allow_multi_profiles_ = !::strcmp(tmp, "1") || !::stricmp(tmp, "yes") || !::stricmp(tmp, "y");
      else if (1 == ::_snscanf(buff, PROFILE_LINE_MAX, " allow-use-system-info = %" STRINGIFY(TEMP_BUFF_SIZE) "s", tmp))
        pcb->allow_allow_use_system_info = !::strcmp(tmp, "1") || !::stricmp(tmp, "yes") || !::stricmp(tmp, "y");

      else
        FUSION_WARN("profile='%s': ignore line=%s", name, buff);
    }

    ::fclose(fd);

    // top-level
    if (!parent) {
      if (!pcb->home_) {
        FUSION_INFO("profile='%s': no 'home' defined", name);

        pcb->home_ = ::strdup(NO_HOME);
      }
      else
        pcb->paths_.insert(pcb->paths_.begin(), ::strdup(pcb->home_));
    }

    return pcb;
  }
}
