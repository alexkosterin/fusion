/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_MD_H
#define		FUSION_MD_H

#include "include/nf.h"
#include "include/configure.h"

namespace nf {
  struct md_t;
  struct ccb_t;

  enum check_t {
    MD_CHECK_NONE             = 0,
    MD_CHECK_READONLY         = 1,
    MD_CHECK_REBUILD_INDEX    = 2,
    MD_CHECK_DELETE_UNRELATED = 3,
    MD_CHECK_DUMP_TAGS        = 4,
    MD_CHECK_DUMP_LINKS       = 5,
  };

  enum op_t {
    MD_ACCESS_OPERATION_READ  = 0,
    MD_ACCESS_OPERATION_WRITE = 1,
  };

  //////////////////////////////////////////////////////////////////////////////
  // user exposed part of profile
  struct profile_opts_t {
    unsigned int clock_sync_period_;	// milliseconds, <= 0 -> no sync
    unsigned int output_queue_limit_;	// 0 - no limit
  };

  //////////////////////////////////////////////////////////////////////////////
  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_access(
    FUSION_IN     cid_t,
    FUSION_IN     mid_t,
    FUSION_IN     op_t);

  //////////////////////////////////////////////////////////////////////////////
  FUSION_EXPORT profile_opts_t* FUSION_CALLING_CONVENTION md_get_default_profile(
    FUSION_IN     cid_t);

  //////////////////////////////////////////////////////////////////////////////
  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_check(
    FUSION_IN     int flags,
    FUSION_IN     const char* root_dir,
    FUSION_IN     bool add_data = false,
    FUSION_IN     bool add_mid  = false,
    FUSION_IN     bool add_size = false,
    FUSION_IN     bool add_type = false);

  //////////////////////////////////////////////////////////////////////////////
  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_init(
    FUSION_IN     const char* root_dir,
    FUSION_IN     const char* profile_dir = 0);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_fini();

  // user/client ///////////////////////////////////////////////////////////////
  FUSION_EXPORT bool FUSION_CALLING_CONVENTION md_is_registered_client(
    FUSION_IN     cid_t c);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_reg_client (
    FUSION_IN     cid_t c,
    FUSION_IN     const char* name,
    FUSION_IN     size_t profiles_nr,
    FUSION_IN     const char** profiles);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_unreg_client(
    FUSION_IN     cid_t c);

  // messages //////////////////////////////////////////////////////////////////
  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_check_size(
    FUSION_IN     cid_t c,
    FUSION_IN     mid_t m,
    FUSION_IN     size_t size
  );

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_create(
    FUSION_IN     cid_t c,
    FUSION_IN     const char* name,
    FUSION_IN     oflags_t flags,
    FUSION_IN     mtype_t type,
    FUSION_OUT    mid_t& m,
    FUSION_IN     size_t size,
    FUSION_OUT    bool& created);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_open(
    FUSION_IN     cid_t c,
    FUSION_IN     const char* name,
    FUSION_IN     oflags_t flags,
    FUSION_OUT    mtype_t& type,
    FUSION_OUT    mid_t& m,
    FUSION_OUT    size_t& size);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_close(
    FUSION_IN     cid_t c,
    FUSION_IN     mid_t m);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_unlink(
    FUSION_IN     cid_t c,
    FUSION_IN     const char* name);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_unlink(
    FUSION_IN     cid_t c,
    FUSION_IN     mid_t m);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_ln(
    FUSION_IN     cid_t c,
    FUSION_IN     const char* from,
    FUSION_IN     const char* target,
    FUSION_IN     bool soft = false);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_mv(
    FUSION_IN     cid_t c,
    FUSION_IN     const char* from,
    FUSION_IN     const char* to);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_mlist(
    FUSION_IN     cid_t c,
    FUSION_IN     const char* profile,
    FUSION_IN     const char* mask,
    FUSION_INOUT  size_t& nr,
    FUSION_INOUT  char*& buff);

  // messages: query
  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_query(      		// returns list of matching messages
    FUSION_IN     cid_t c,
    FUSION_IN     const char* mask,
    FUSION_INOUT  size_t& len,
    FUSION_IN     char* names,
    FUSION_IN     const char* sep);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_grep(           // returns list of matching messages
    FUSION_IN     cid_t c,
    FUSION_IN     const char* regexp,
    FUSION_INOUT  size_t& len,
    FUSION_IN     char* names,
    FUSION_IN     const char* sep);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_stat(
    FUSION_IN     cid_t c,
    FUSION_IN     const char* name,
    FUSION_OUT    stat_t& data);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_stat(
    FUSION_IN     mid_t m,
    FUSION_OUT    stat_t& data);

  FUSION_EXPORT const md_t* FUSION_CALLING_CONVENTION md_get_internal_descriptor(
    FUSION_IN     mid_t m);

  FUSION_EXPORT bool FUSION_CALLING_CONVENTION md_flush_internal_descriptor(
    FUSION_IN     const md_t*);

  // messages: attributes
  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_mattr_read(
    FUSION_IN     cid_t c,
    FUSION_IN     mid_t m,
    FUSION_IN     const char* key,
    FUSION_INOUT  size_t& len,
    FUSION_IN     char* value);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_mattr_write(
    FUSION_IN     cid_t c,
    FUSION_IN     mid_t m,
    FUSION_IN     const char* key,
    FUSION_IN     char* value);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_mattrs_read(	  // returns attrs as text (key=val\n)*
    FUSION_IN     cid_t c,
    FUSION_IN     mid_t m,
    FUSION_IN     size_t& len,
    FUSION_IN     char* text);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_mattrs_write(   // ovwewrites all attrs, use to trancate
    FUSION_IN     cid_t c,
    FUSION_IN     mid_t m,
    FUSION_IN     size_t len,
    FUSION_IN     char* text);

  // maintanance
  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_save(
    FUSION_INOUT  size_t& len,
    FUSION_IN     char* text,
    FUSION_IN     const char* sep);

  FUSION_EXPORT result_t FUSION_CALLING_CONVENTION md_load(
    FUSION_IN     size_t len,
    FUSION_IN     const char* text,
    FUSION_IN     const char* sep);

  FUSION_EXPORT void FUSION_CALLING_CONVENTION md_foreach_message(
    FUSION_IN     void* ctx,
    FUSION_IN     cid_t c,
    FUSION_IN     bool (*predicate)(void* ctx, unsigned oflags),
    FUSION_IN     void (*callback)(void* ctx, const ccb_t* ccb));

  FUSION_EXPORT void FUSION_CALLING_CONVENTION md_foreach_message(
    FUSION_IN     void* ctx,
    FUSION_IN     mid_t m,
    FUSION_IN     bool (*predicate)(void* ctx, unsigned oflags),
    FUSION_IN     void (*callback)(void* ctx, const md_t* md, const ccb_t* ccb));

  FUSION_EXPORT void FUSION_CALLING_CONVENTION md_foreach_message(
    FUSION_IN     void* ctx,
    FUSION_IN     cid_t c,
    FUSION_IN     mid_t m,
    FUSION_IN     bool (*predicate)(void* ctx, unsigned oflags),
    FUSION_IN     void (*callback)(void* ctx, const md_t* md, const ccb_t* ccb));
}

#endif		//FUSION_MD_H
