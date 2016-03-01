/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <stdio.h>
#include <sys/stat.h>
#include <algorithm>
#include <set>

#include "include/sm.h"
#include "include/md.h"
#include "include/tsc.h"
#include "include/lock.h"
#include "include/enumstr.h"
#include "md_internal.h"
#include "pcb.h"
#include "bintxt.h"

#define FUSION_FILE_ACCESS_TYPE (GENERIC_READ | GENERIC_WRITE)

#define READABLE(of)        ((of & O_WRONLY) != O_WRONLY)
#define WRITEABLE(of)       ((of & O_RDONLY) != O_RDONLY)
#define EXCL_READABLE(of)   ((of & O_EXCL) && READABLE(of))
#define EXCL_WRITEABLE(of)  ((of & O_EXCL) && WRITEABLE(of))

namespace nf {
  // module ////////////////////////////////////////////////////////////////////
  static const char*    conf_path__     = 0;
  const char*           profile_path__  = 0;
  const char*           dp__            = MBM_DEFAULT_PROFILE;

  static ccbs_t	        ccbs__;
  static mdescs_t       mds__;
  static midpool_t      midpool__;
  static ::HANDLE       lock_fd__ = INVALID_HANDLE_VALUE;  // used as a lock

  /////////////////////////////////////////////////////////////////////////////
  md_t::md_t(::HANDLE hfile) : imp_(hfile), temp_(0), xread_(0), xwrite_(0), reads_(0), writes_(0), last_len_(0), last_data_(0), last_sender_(CID_NONE)
  {
    FUSION_DEBUG("%x", this);
  }

  //////////////////////////////////////////////////////////////////////////////
  md_t::~md_t() {
    FUSION_DEBUG("%x", this);

    free((void*)last_data_);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t md_t::create(mtype_t type, size_t size) {
    atime_      = ctime_  = mtime_  = now_msecs();
    mid_        = midpool__.get();
    mtype_      = type;
    size_       = size;
//  reads_      = 1;
//  writes_     = 1;
    last_sender_= CID_NONE;
    last_len_   = 0;
    last_data_  = 0;
//  nlink_      = fi.nNumberOfLinks;

    result_t e;

    if ((e = imp_.init(this, atime_, ctime_, mtime_)) != ERR_OK || (e = imp_.write(this)) != ERR_OK) {
      FUSION_ERROR("imp_.init or imp_.write=%d", e);

      midpool__.put(mid_);

      return e;
    }

    FUSION_DEBUG("allocated mids=%d", midpool__.nr());

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t md_t::read() {
    size_t    len       = 0;
    void*     data      = 0;
    bool      has_value = false;
    result_t  e         = imp_.read(mtype_, mid_, size_, atime_, ctime_, mtime_, has_value, &len, &data);

    if (e != ERR_OK) {
      FUSION_WARN("imp_.read()=%d", e);

      return e;
    }

    last_sender_  = has_value ? CID_SYS : CID_NONE;
    last_len_     = len;
    free(last_data_);
    last_data_    = data; // take ownership

//  reads_        = 1;
//  writes_       = 1;
//  nlink_ = fi.nNumberOfLinks;

    return ERR_OK;
  }

  // md_t::write ///////////////////////////////////////////////////////////////
  result_t md_t::write() {
    return imp_.write(this);
  }

  // md_t::remove //////////////////////////////////////////////////////////////
  result_t md_t::remove() {
    result_t e = imp_.remove(this);

    if (e != ERR_OK)
      FUSION_WARN("imp_.remove()=%d", e);

    return e;
  }

  // md_t::close ///////////////////////////////////////////////////////////////
  result_t md_t::close(bool read, bool write, bool excl_read, bool excl_write) {
    FUSION_VERIFY(FUSION_IMPLIES(excl_read, !read),      "excl_read=%d implies read=%d",    excl_read, read);
    FUSION_VERIFY(FUSION_IMPLIES(excl_write, !write),    "excl_write=%d implies write=%d",  excl_write, write);

    FUSION_VERIFY(FUSION_IMPLIES(read, !excl_read),      "read=%d implies excl_read=%d",    read, excl_read);
    FUSION_VERIFY(FUSION_IMPLIES(write, !excl_write),    "write=%d implies excl_write=%d",  write, excl_write);

    FUSION_VERIFY(FUSION_IMPLIES(excl_read, xread_),    "excl_read=%d implies xread_=%d",   excl_read, xread_);
    FUSION_VERIFY(FUSION_IMPLIES(excl_write, xwrite_),  "excl_write=%d implies xwrite_=%d", excl_write, xwrite_);

    FUSION_VERIFY(FUSION_IMPLIES(read, reads_),         "read=%d implies reads=%d",         read, reads_);
    FUSION_VERIFY(FUSION_IMPLIES(write, writes_),       "write=%d implies writes=%d",       write, writes_);

    result_t e = ERR_OK;

    if (read)
      --reads_;

    if (write)
      --writes_;

    if (excl_read)
      xread_ = 0;

    if (excl_write)
      xwrite_ = 0;

    if (!reads_ && !writes_ && !xread_ && !xwrite_) {
      if (temp_)
        e = imp_.remove(this);
      else if (mtype_ & MT_PERSISTENT && last_sender_ != CID_NONE) /*@*/
        e = imp_.write(this);

      //imp_.close(this); done in descrtuctor
      mds__.erase(mid_);

      delete this;

      return e;
    }

    return e;
  }

  // client control block //////////////////////////////////////////////////////
  ccb_t::ccb_t(cid_t cid, const char* name, pcb_t* dp) : cid_(cid), name_(_strdup(name)), dp_(dp) {
    profiles_.insert(std::make_pair(::strdup(dp_->id_), dp_));
  }

  ccb_t::~ccb_t() {
    free((void*)name_);

    for (pcbs_t::iterator I = profiles_.begin(), E = profiles_.end(); I != E; ++I) {
      ::free((void*)I->first);

#if 0 // note: all profiles are really managed by pcbs__
      delete I->second;
#endif
    }

    profiles_.clear();
  }

  ccb_t* ccb_t::create(cid_t cid, const char* name, const char* profile) {
    pcb_t* p = pcb_t::create(profile);

    return (p && p->enabled_) ? new ccb_t(cid, name, p) : 0;
  }

  //////////////////////////////////////////////////////////////////////////////
  static bool validate_name(const char* name) {
    return !::strpbrk(name, INVALID_NAME_CHARS) && (strlen(name) <= MAX_MESSAGE_NAME_LENGTH);
  }

  static bool check_initialized() {
    return conf_path__ || profile_path__ || lock_fd__ != INVALID_HANDLE_VALUE;
  }

  static bool check_consistent() {
    return (conf_path__ && profile_path__ && lock_fd__ != INVALID_HANDLE_VALUE) ||
           (!conf_path__ && !profile_path__ && lock_fd__ == INVALID_HANDLE_VALUE);
  }

  //////////////////////////////////////////////////////////////////////////////
  static void _init(const char* root_path, const char* profile_path) {
    char path[MAX_PATH + 1] = {0};

    conf_path__ = ::strdup(root_path ? root_path : ".");

    size_t len = ::strlen(conf_path__);

    if (conf_path__[len - 1] == '/' || conf_path__[len - 1] == '\\')
      ((char*)(conf_path__))[len - 1] = 0;

    if (profile_path)
      profile_path__ = ::strdup(profile_path);
    else {
      ::_snprintf(path, MAX_PATH, "%s/" PROFILE_DIRNAME, conf_path__);
      profile_path__ = ::strdup(path);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t _set_lock() {
    char path[MAX_PATH + 1] = {0};

    ::_snprintf(path, MAX_PATH, "%s/" LOCK_FILENAME, conf_path__);

    lock_fd__ = ::CreateFile(path, FILE_ALL_ACCESS, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

    if (lock_fd__ == INVALID_HANDLE_VALUE) {
      FUSION_ERROR("CreateFile=%d", ::GetLastError());

      return ::GetLastError() == ERROR_SHARING_VIOLATION ? ERR_CONFIGURATION_LOCK : ERR_CONFIGURATION;
    }

    if (::GetLastError() == ERROR_ALREADY_EXISTS)
      FUSION_WARN("Found exising lock. Was Fusion crashed?");

    size_t sz = ::_snprintf(0, 0, "pid=%d cmd=%s", ::GetCurrentProcessId(), ::GetCommandLine());
    char*   s = (char*)::alloca(sz);
    DWORD  done;

    ::_snprintf(s, sz, "pid=%d cmd=%s", ::GetCurrentProcessId(), ::GetCommandLine());
    ::WriteFile(lock_fd__, s, sz, &done, 0);

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  static void _remove_lock() {
    if (lock_fd__ != INVALID_HANDLE_VALUE) {
      ::CloseHandle(lock_fd__);
      lock_fd__ = INVALID_HANDLE_VALUE;

      char path[MAX_PATH + 1] = {0};

      ::_snprintf(path, sizeof path, "%s/" LOCK_FILENAME, conf_path__);

      if (!::DeleteFile(path))
        FUSION_WARN("DeleteFile(\"%s\")=%d", path, ::GetLastError());
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  static void _fini() {
    free((void*)conf_path__);
    conf_path__ = 0;

    free((void*)profile_path__);
    profile_path__ = 0;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t _delete_unrelated();
  static result_t _check_fix();
  static result_t _check_readonly();
  static bool _check_dir(int check, char* path, int nested = 0);

  static bool   _dump_tag_data      = false;
  static bool   _dump_tag_mid       = false;
  static bool   _dump_tag_size      = false;
  static bool   _dump_tag_type      = false;
  static size_t _dump_tag_link_nr_  = 1;

  result_t FUSION_CALLING_CONVENTION md_check(
    int flags,
    const char* root_path,
    bool add_data,
    bool add_mid,
    bool add_size,
    bool add_type
  )
  {
    _init(root_path, 0);

    result_t e = _set_lock();

    if (e != ERR_OK) {
      _fini();

      return e;
    }

    switch (flags) {
    case MD_CHECK_NONE:
      break;

    case MD_CHECK_READONLY:
      e = _check_readonly();
      break;

    case MD_CHECK_REBUILD_INDEX:
      e = _check_fix();
      break;

    case MD_CHECK_DELETE_UNRELATED:
      e = _delete_unrelated();
      break;

    case MD_CHECK_DUMP_TAGS:
    case MD_CHECK_DUMP_LINKS:
      {
        char path[MAX_PATH + 1];

        ::strncpy(path, conf_path__, sizeof path);

        _dump_tag_data      = add_data;
        _dump_tag_mid       = add_mid;
        _dump_tag_size      = add_size;
        _dump_tag_type      = add_type;
        _dump_tag_link_nr_  = (flags == MD_CHECK_DUMP_TAGS) ? 1 : 2;

        _check_dir(MD_CHECK_DUMP_LINKS, path);
      }

      break;
    }

    _remove_lock();
    _fini();

    return e;
  }

  result_t FUSION_CALLING_CONVENTION md_init(const char* root_path, const char* profile_path) {
    if (conf_path__ || profile_path__)
      return ERR_INITIALIZED;

    _init(root_path, profile_path);

    result_t e = _set_lock();

    if (e != ERR_OK) {
      _fini();

      return e;
    }

    midpool__.reset();

    char tmp[MAX_PATH + 1];

    ::strncpy(tmp, root_path, sizeof tmp);

    return _check_dir(MD_CHECK_REBUILD_INDEX, tmp) ? ERR_OK : ERR_MESSAGE_FORMAT;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_fini() {
    FUSION_ASSERT(check_initialized() && check_consistent());

    if (!conf_path__ || !profile_path__ || !lock_fd__)
      return ERR_INITIALIZED;

    for (auto I = mds__.cbegin(); I != mds__.cend(); ++I)
      I->second->write();

    _remove_lock();
    _fini();

    return ERR_OK;
  }

  // client ////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////////
  bool FUSION_CALLING_CONVENTION md_is_registered_client(cid_t c) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    return (ccbs__.find(c) != ccbs__.end());
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_reg_client(cid_t cid, const char* name, size_t profiles_nr, const char** profiles) {
    FUSION_ASSERT(check_initialized() && check_consistent());
    FUSION_ASSERT(profiles);

    if (ccbs__.find(cid) != ccbs__.end()) {
      FUSION_WARN("aleady registered");

      return ERR_REGISTERED;
    }

    const char* dp = profiles_nr > 0 ? profiles[0] : dp__;

    // first profile is special -> default profile
    ccb_t* ccb = ccb_t::create(cid, name, dp);

    if (!ccb) {
      FUSION_WARN("ccb_t::create(name=%s, profile=%s) failed", name, dp);

      return ERR_PERMISSION;
    }

    // multi-profiles
    for (size_t i = 1; i < profiles_nr; ++i) {
      pcb_t* p = pcb_t::create(profiles[i]);

      if (p && p->enabled_)
        ccb->profiles_.insert(std::make_pair(::strdup(profiles[i]), p));
      else  {
        delete ccb;

        return ERR_PERMISSION;
      }
    }

    if (ccb->profiles_.size() > 1 && !ccb->dp_->allow_multi_profiles_) {
      delete ccb;

      return ERR_PERMISSION;
    }

    ccbs__.insert(std::make_pair(cid, ccb));

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_unreg_client(cid_t c) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    ccbs_t::const_iterator I = ccbs__.find(c);

    if (I == ccbs__.cend())
      return ERR_REGISTERED;

    ccb_t* ccb = I->second;

    ccbs__.erase(I);

    for (auto M = ccb->omsgs_.begin(), E = ccb->omsgs_.end(); M != E; ++M) {
      FUSION_ASSERT(M->second.md_);
      FUSION_INFO("oflags_=%x", M->second.oflags_); //@@@@

      bool xr = EXCL_READABLE(M->second.oflags_);
      bool xw = EXCL_WRITEABLE(M->second.oflags_);
      bool r  = xr ? false : READABLE(M->second.oflags_);
      bool w  = xw ? false : WRITEABLE(M->second.oflags_);

      M->second.md_->close(r, w, xr, xw);
    }

    delete ccb;

    return ERR_OK;
  }

  // messages //////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_check_size(cid_t c, mid_t m, size_t len) {
    ccbs_t::const_iterator I = ccbs__.find(c);

    if (I == ccbs__.cend())
      return ERR_REGISTERED;

    ccb_t* ccb = I->second;

    FUSION_ENSURE(ccb, return ERR_UNEXPECTED);

    ccb_mds_t::const_iterator K = ccb->omsgs_.find(m);

    if (K == ccb->omsgs_.cend()) {
      FUSION_WARN("cid=%d mid=%d not opened", c, m);

      return ERR_OPEN;
    }

    if (K->second.md_->size_ != -1)
      return K->second.md_->size_ == len ? ERR_OK : ERR_MESSAGE_SIZE;

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t static parse_namex(cid_t c, const char* namex, const char*& name, ccb_t*& ccb, pcb_t*& dp, pcb_t*& ep) {
    const char *sc = namex ? ::strchr(namex, PROFILE_MESSAGE_SEPARATOR_CHAR) : 0;

    if (sc)
      *((char*)sc) = 0;

    const char* pname	= sc ? namex : 0;
    name	= sc ? sc + 1 : namex;

    {
      ccbs_t::const_iterator I = ccbs__.find(c);

      if (I == ccbs__.cend())
        return ERR_REGISTERED;

      if (name && !validate_name(name))
        return ERR_MESSAGE_NAME;

      ccb = I->second;
    }

    FUSION_ASSERT(ccb);
    FUSION_ASSERT(ccb->dp_);

    dp = ccb->dp_;
    ep = 0;

    if (pname) {
      pcbs_t::const_iterator I = ccb->profiles_.find(pname);

      if (I != ccb->profiles_.cend())
        ep = I->second;
    }
    else
      ep = dp;

    if (!ep)
      return ERR_PERMISSION;

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t static _open(cid_t c, const char* namex, unsigned oflags, mtype_t& mtype, mid_t& m, size_t& size, bool& created) {
    FUSION_ASSERT(check_initialized() && check_consistent());
    FUSION_ASSERT(namex);

    pcb_t *dp, *ep;
    ccb_t* ccb;
    const char* name;
    std::string n = namex;
    result_t e = parse_namex(c, namex, name, ccb, dp, ep);

    if (e != ERR_OK)
      return e;

    FUSION_ASSERT(dp);
    FUSION_ASSERT(ep);

    bool    create = oflags & O_CREATE;
    char    path[MAX_PATH + 1] = {0};
    DWORD   create_disposition = create ? (oflags & O_EXCL ? CREATE_NEW : OPEN_ALWAYS) : OPEN_EXISTING;
    HANDLE  h = INVALID_HANDLE_VALUE;
    size_t  sharing_retry_nr = 0;

    if (create && !dp->allow_modify_messages_)
      return ERR_PERMISSION;

    if (!dp->allow_paths_)
      if (::strchr(name, '/') || ::strchr(name, '\\'))
        return ERR_PERMISSION;

    if (!dp->allow_absolute_path_)
      if (*name == '/' || *name == '\\' || ::strstr(name, ".."))
        return ERR_PERMISSION;

    if (create) {
      // verify type: for now just check for garbage
      if (mtype & ~(MT_TYPE_MASK | MT_PERSISTENT))
        return ERR_MESSAGE_TYPE;
    }

sharing_retry:
    if (sharing_retry_nr && sharing_retry_nr >= SHARING_RETRY_MAX)
      return ERR_IO;

    if (*name == '/' || *name == '\\') {
      ::_snprintf(path, MAX_PATH, "%s/%s", conf_path__, name);
      h = ::CreateFile(path, FUSION_FILE_ACCESS_TYPE, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 0, create_disposition, FILE_ATTRIBUTE_NORMAL|FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, 0);

      FUSION_DEBUG("1: %s/%x=%d", path, create_disposition, ::GetLastError());
    }
    else if (create) {
      if (*name == '/' || *name == '\\')
        ::_snprintf(path, MAX_PATH, "%s/%s", conf_path__, name + 1);
      else if (ep->home_)
        ::_snprintf(path, MAX_PATH, "%s/%s/%s", conf_path__, ep->home_, name);
      else
        return ERR_PERMISSION;

      h = ::CreateFile(path, FUSION_FILE_ACCESS_TYPE, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 0, create_disposition, FILE_ATTRIBUTE_NORMAL|FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, 0);
      FUSION_DEBUG("2: %s/%x=%d", path, create_disposition, ::GetLastError());
    }
    else {
      for (size_t i = 0; i < ep->paths_.size(); ++i) {
        ::_snprintf(path, MAX_PATH, "%s/%s/%s", conf_path__, ep->paths_[i], name);

        h = ::CreateFile(path, FUSION_FILE_ACCESS_TYPE, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 0, create_disposition, FILE_ATTRIBUTE_NORMAL|FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, 0);

        FUSION_DEBUG("3: %s/%x=%d", path, create_disposition, ::GetLastError());

        if (h != INVALID_HANDLE_VALUE)
          break;

        if (::GetLastError() == ERROR_ACCESS_DENIED)
          return ERR_PERMISSION;
      }
    }

    if (h == INVALID_HANDLE_VALUE) {
      FUSION_DEBUG("CreateFile(%s)=%d", path, ::GetLastError());

      switch (::GetLastError()) {
      case ERROR_SHARING_VIOLATION: ::Sleep(10); ++sharing_retry_nr; goto sharing_retry;  // @@
      case ERROR_ACCESS_DENIED:     return ERR_PERMISSION;
      case ERROR_FILE_EXISTS:       return ERR_ALREADY_EXIST;
      case ERROR_FILE_NOT_FOUND:    return ERR_OPEN;
      default:                      return ERR_MESSAGE;
      }
    }

    created = true;

    switch (create_disposition) {
    case OPEN_EXISTING:
      created = false;
      break;

    case CREATE_NEW:
      created = true;
      break;

    case OPEN_ALWAYS:
      created = ::GetLastError() != ERROR_ALREADY_EXISTS;
      break;

    default:
      FUSION_ASSERT(false);
    }

    md_t* md = new md_t(h);

    if (!created)
      e = md->read();
    else if (create)
      e = md->create(mtype, size);
    else {
      FUSION_ERROR("unexpected error");

      e = ERR_UNEXPECTED;
    }

    if (e != ERR_OK) {
      delete md;

      return e;
    }

    if (create) {
      if (mtype != md->mtype_)
        return ERR_MESSAGE_TYPE;

      if (size != md->size_)
        return ERR_MESSAGE_SIZE;
    }
    else {
      mtype = md->mtype_;
      size  = md->size_;
    }

    m = md->mid_;

    // check open
    if (ccb->omsgs_.find(m) != ccb->omsgs_.end()) {
      FUSION_WARN("cid=%d mid=%d already opened", c, m);

      delete md;

      return ERR_OPEN;
    }

    // merge existing
    mdescs_t::const_iterator I = mds__.find(m);

    if (I != mds__.cend()) {
      FUSION_ASSERT(m                       == md->mid_);
      FUSION_ASSERT(I->second->mid_         == md->mid_);
      FUSION_ASSERT(I->second->mtype_       == md->mtype_);
      FUSION_ASSERT(I->second->size_        == md->size_);
      FUSION_ASSERT(I->second->imp_.cookie_ == md->imp_.cookie_);

      bool r  = READABLE(oflags);
      bool w  = WRITEABLE(oflags);
      bool xr = EXCL_READABLE(oflags);
      bool xw = EXCL_WRITEABLE(oflags);

      // x r/w violation
      bool xrv = false, xwv = false;
      md_t* t = I->second;

      if ((xrv |= (t->xread_  && r)) ||
          (xwv |= (t->xwrite_ && w)) ||
          (xrv |= (t->reads_  && xr)) ||
          (xwv |= (t->writes_ && xw))
        ) {
        FUSION_WARN("cid=%d mid=%d: exclusive %s%s%s violation", c, m, xrv ? "read" : "", xrv && xwv ? "+" : "", xwv ? "write" : "");

        delete md;

        return ERR_PERMISSION;
      }

      delete md;
    
      md = I->second;
    }
    else {
      md->temp_ = created && oflags & O_TEMPORARY;
      mds__.insert(std::make_pair(m, md));
    }

    FUSION_VERIFY(FUSION_IMPLIES(md->xread_,  !READABLE(oflags)));
    FUSION_VERIFY(FUSION_IMPLIES(md->xread_,  !EXCL_READABLE(oflags)));
    FUSION_VERIFY(FUSION_IMPLIES(md->xwrite_, !WRITEABLE(oflags)));
    FUSION_VERIFY(FUSION_IMPLIES(md->xwrite_, !EXCL_WRITEABLE(oflags)));

    FUSION_VERIFY(FUSION_IMPLIES(READABLE(oflags), !md->xread_));
    FUSION_VERIFY(FUSION_IMPLIES(EXCL_READABLE(oflags), !md->reads_));
    FUSION_VERIFY(FUSION_IMPLIES(WRITEABLE(oflags), !md->xwrite_));
    FUSION_VERIFY(FUSION_IMPLIES(EXCL_WRITEABLE(oflags), !md->writes_));

    if (EXCL_READABLE(oflags))
      md->xread_ = 1;
    else
      md->reads_ += READABLE(oflags);

    if (EXCL_WRITEABLE(oflags))
      md->xwrite_ = 1;
    else
      md->writes_ += WRITEABLE(oflags);

    client_md_t cm;

    cm.md_      = md;
    cm.oflags_  = oflags;
#if (MD_KEEP_NAME > 0)
    cm.name_    = n;
#endif
#if (MD_KEEP_PATH > 0)
    cm.path_    = path;
#endif

    ccb->omsgs_.insert(std::make_pair(m, cm));

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_create(cid_t c, const char* name, oflags_t oflags, mtype_t type, mid_t& m, size_t size, bool& created) {
    return _open(c, name, oflags, type, m, size, created);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_open(cid_t c, const char* name, oflags_t oflags, mtype_t& type, mid_t& m, size_t& size) {
    bool created;

    return _open(c, name, oflags, type, m, size, created);
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_close(cid_t c, mid_t m) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    ccb_t* ccb;

    {
      ccbs_t::const_iterator I = ccbs__.find(c);

      if (I == ccbs__.cend()) {
        FUSION_WARN("cid=%d mid=%d: cid not registered", c, m);

        return ERR_REGISTERED;
      }

      ccb = I->second;
    }

    FUSION_ASSERT(ccb);

    ccb_mds_t::const_iterator I = ccb->omsgs_.find(m);

    if (I == ccb->omsgs_.cend()) {
      FUSION_WARN("cid=%d mid=%d: mid not opened", c, m);

      return ERR_OPEN;
    }

    FUSION_ASSERT(I->first == m);

    md_t* md  = I->second.md_;

    bool xr = EXCL_READABLE(I->second.oflags_);
    bool xw = EXCL_WRITEABLE(I->second.oflags_);
    bool r  = xr ? false : READABLE(I->second.oflags_);
    bool w  = xw ? false : WRITEABLE(I->second.oflags_);

    FUSION_INFO("oflags_=%x", I->second.oflags_); //@@@@
    FUSION_ASSERT(md);
    FUSION_ASSERT(md->mid_ == m);

    size_t nr = ccb->omsgs_.erase(m);

    FUSION_ASSERT(nr == 1, "One open descriptor per client");

    return md->close(r, w, xr, xw);
  }

  //////////////////////////////////////////////////////////////////////////////
  static void _do_dir(const char* path, const char* mask, std::set<const char*, striless>& ns) {
    char w[MAX_PATH + 1];

    ::_snprintf(w, sizeof w, "%s\\%s", path, mask ? mask : "*.*");

    WIN32_FIND_DATA fd;
    HANDLE h = ::FindFirstFile(w, &fd);

    if (h != INVALID_HANDLE_VALUE) {
      do
        if (!(fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_DEVICE))) {
          std::set<const char*, striless>::const_iterator I = ns.find(fd.cFileName);

          if (I == ns.cend())
            ns.insert(::strdup(fd.cFileName));
        }
      while (::FindNextFile(h, &fd));

      ::FindClose(h);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_mlist(cid_t c, const char* profile, const char* mask, size_t& nr, char*& names_buff) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    pcb_t       *dp, *ep;
    ccb_t*      ccb;
    char        path[MAX_PATH + 1] = {0};
    const char* t;
    result_t    e;

    if (profile) {
      ::_snprintf(path, sizeof path, "%s:", profile);

      e = parse_namex(c, path, t, ccb, dp, ep);
    }
    else
      e = parse_namex(c, 0, t, ccb, dp, ep);

    if (e != ERR_OK)
      return e;

    if (!ccb->dp_->allow_mlist_)
      return ERR_PERMISSION;

    FUSION_ASSERT(dp);
    FUSION_ASSERT(ep);

    std::set<const char*, striless> ns;

    for (size_t i = 0; i < ep->paths_.size(); ++i) {
      ::_snprintf(path, sizeof path, "%s\\%s", conf_path__, ep->paths_[i]);
      _do_dir(path, mask ? mask : "*.*", ns);
    }

    size_t sz = ns.size() * sizeof(const char*);

    //calc length
    for (std::set<const char*, striless>::const_iterator I = ns.cbegin(); I != ns.cend(); ++I)
      sz += ::strlen(*I) + 1;

    char* buff  = sz ? (char*)::malloc(sz) : 0;
    char** pidx = (char**)buff;
    size_t ofs  = ns.size() * sizeof(const char*);
    size_t idx  = 0;

    //fill buffer, initialize pointers
    for (std::set<const char*, striless>::const_iterator I = ns.cbegin(); I != ns.cend(); ++I) {
      ::strcpy(pidx[idx++] = buff + ofs, *I);
      ofs += ::strlen(*I) + 1;
    }

    nr = ns.size();
    names_buff = buff;

    //destroy ns properly
    for (std::set<const char*, striless>::const_iterator I = ns.cbegin(); I != ns.cend(); ++I)
      ::free((void*)*I);

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_unlink(cid_t c, const char* namex) {
    FUSION_ASSERT(check_initialized() && check_consistent());
    FUSION_ASSERT(namex);

    pcb_t *dp , *ep;
    ccb_t* ccb;
    const char* name;
    result_t e = parse_namex(c, namex, name, ccb, dp, ep);

    if (e != ERR_OK)
      return e;

    FUSION_ASSERT(dp);
    FUSION_ASSERT(ep);

    char path[MAX_PATH + 1] = {0};

    if (*name == '/' || *name == '\\')
      ::_snprintf(path, MAX_PATH, "%s/%s", conf_path__, name + 1);
    else
      ::_snprintf(path, MAX_PATH, "%s/%s/%s", conf_path__, ep->home_, name);

    HANDLE h = ::CreateFile(path, FILE_ALL_ACCESS, FILE_SHARE_READ|FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, 0);

    if (h == INVALID_HANDLE_VALUE) {
      FUSION_WARN("::CreateFile(path=\"%s\",...)=%d", path, ::GetLastError());

      switch (::GetLastError()) {
      case ERROR_ACCESS_DENIED:   return ERR_PERMISSION;
      case ERROR_FILE_EXISTS:     return ERR_OPEN;
      case ERROR_FILE_NOT_FOUND:  return ERR_OPEN;
      default:                    return ERR_UNEXPECTED;
      }
    }

    unsigned  type = MT_EVENT;
    md_t*     md = new md_t(h);

    FUSION_ASSERT(md);

    if (!md)
      return ERR_MEMORY;

    e = md->read();

    if (e != ERR_OK) {
      delete md;

      return e;
    }

    mid_t   m   = md->mid_;
    mtype_t t   = md->mtype_;
    size_t  sz  = md->size_;
    int64_t ck  = md->imp_.cookie_;

    delete md;

    mdescs_t::const_iterator I = mds__.find(m);

    if (I != mds__.cend()) {
      FUSION_ASSERT(I->second->mid_         == m);
      FUSION_ASSERT(I->second->mtype_       == t);
      FUSION_ASSERT(I->second->size_        == sz);
      FUSION_ASSERT(I->second->imp_.cookie_ == ck);

      return I->second->remove();
    }

    if (::DeleteFile(path)) {
      // there can be links into it...
      //midpool__._put(m);

      return ERR_OK;
    }

    switch (::GetLastError()) {
    case ERROR_ACCESS_DENIED:   return ERR_PERMISSION;
    default:                    return ERR_UNEXPECTED;
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_unlink(cid_t c, mid_t m) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    ccb_t*  ccb = 0;
    md_t*   md  = 0;

    {
      ccbs_t::const_iterator I = ccbs__.find(c);

      if (I == ccbs__.cend())
        return ERR_REGISTERED;

      ccb = I->second;
    }

    FUSION_ASSERT(ccb);

    if (!ccb)
      return ERR_PARAMETER;

    {
      ccb_mds_t::const_iterator I = ccb->omsgs_.find(m);

      if (I == ccb->omsgs_.cend())
        return ERR_OPEN;

      md = I->second.md_;
    }

    FUSION_ASSERT(md);

    if (!md)
      return ERR_PARAMETER;

    return md->remove();
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_ln(cid_t c, const char* fromx, const char* targetx, bool soft) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    if (soft)
      return ERR_IMPLEMENTED;

    if (!fromx || !targetx)
      return ERR_PARAMETER;

    pcb_t *dp, *ep0, *ep1;
    const char *from, *target;
    ccb_t* ccb;
    result_t e = parse_namex(c, fromx, from, ccb, dp, ep0);

    if (e != ERR_OK)
      return e;

    e = parse_namex(c, targetx, target, ccb, dp, ep1);

    if (e != ERR_OK)
      return e;

    FUSION_ASSERT(dp);
    FUSION_ASSERT(ep0);
    FUSION_ASSERT(ep1);

    if (!dp->allow_modify_messages_)
      return ERR_PERMISSION;

    if (!dp->allow_paths_)
      if (::strchr(from, '/') || ::strchr(from, '\\') || ::strchr(target, '/') || ::strchr(target, '\\'))
        return ERR_PERMISSION;

    if (!dp->allow_absolute_path_)
      if (*from == '/' || *from == '\\' || *target == '/' || *target == '\\' || ::strstr(from, "..") || ::strstr(target, ".."))
        return ERR_PERMISSION;

    //ep0->home_
    char _from[MAX_PATH + 1] = {0};
    char _trgt[MAX_PATH + 1] = {0};

    if (*from == '/' || *from == '\\')
      ::_snprintf(_from, MAX_PATH, "%s/%s", conf_path__, from + 1);
    else
      ::_snprintf(_from, MAX_PATH, "%s/%s/%s", conf_path__, ep0->home_, from);

    if (*target == '/' || *target == '\\')
      ::_snprintf(_trgt, MAX_PATH, "%s/%s", conf_path__, target + 1);
    else
      ::_snprintf(_trgt, MAX_PATH, "%s/%s/%s", conf_path__, ep1->home_, target);

    if (!::CreateHardLink(_from, _trgt, 0)) {
      FUSION_WARN("CreateHardLink(%s, %s)=%d", _from, _trgt, ::GetLastError());

      switch (::GetLastError()) {
      case ERROR_ACCESS_DENIED:   return ERR_PERMISSION;
      case ERROR_FILE_NOT_FOUND:  return ERR_MESSAGE;
      case ERROR_ALREADY_EXISTS:  return ERR_ALREADY_EXIST;
      default:                    return ERR_UNEXPECTED;
      }
    }

    return ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_mv(cid_t c, const char* from, const char* to) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    return ERR_IMPLEMENTED;
  }

  // messages: query

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_query(cid_t c, const char* mask, size_t& len, char* result, const char* sep) {		// returns list of matching messages
    FUSION_ASSERT(check_initialized() && check_consistent());

    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  static void _copy_stat(stat_t& data, md_t* md) {
    FUSION_ASSERT(md);

    data.st_mid		= md->mid_;
    data.st_size	= md->size_;
    data.st_type	= md->mtype_;

    data.st_atime = md->atime_;
    data.st_ctime = md->ctime_;
    data.st_mtime = md->mtime_;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_stat(cid_t c, const char* name, stat_t& data) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    return ERR_IMPLEMENTED;
  }

  /////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_stat(mid_t m, stat_t& stat) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    mdescs_t::const_iterator I = mds__.find(m);

    if (I != mds__.cend()) {
      _copy_stat(stat, I->second);

      return ERR_OK;
    }

    return ERR_MESSAGE;
  }

  // messages: attributes

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_mattr_read(cid_t c, mid_t m, const char* key, size_t& len, char* value) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_mattr_write(cid_t c, mid_t m, const char* key, char* value) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_mattr_read(cid_t c, mid_t m, size_t& len, char* text) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_mattr_write(cid_t c, mid_t m, size_t len, char* text) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    return ERR_IMPLEMENTED;
  }

  // maintanance

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_save(size_t& len, char* text, const char* sep) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_load(size_t len, const char* text, const char* sep) {
    FUSION_ASSERT(check_initialized() && check_consistent());

    return ERR_IMPLEMENTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  struct mprops_t {
    mtype_t type_;
    size_t  sz_;
    bool    has_value_;
    size_t  data_len_;
    void*   data_;

    mprops_t() : has_value_(false), data_len_(0), data_(0) {}
    mprops_t(const mprops_t& o) : type_(o.type_), sz_(o.sz_), has_value_(o.has_value_), data_len_(o.data_len_), data_(::memcpy(::malloc(o.data_len_), o.data_, o.data_len_)) {}
    mprops_t& operator=(const mprops_t& o) {
      type_       = o.type_;
      sz_         = o.sz_;
      has_value_  = o.has_value_;
      data_len_   = o.data_len_;
      data_       = ::memcpy(::malloc(o.data_len_), o.data_, o.data_len_);

      return *this;
    }
    ~mprops_t() { if (data_) ::free(data_); }
  };

  typedef std::map<mid_t, int64_t> mid_cookies_t;
  typedef std::map<mid_t, std::pair<mprops_t, std::vector<std::string>>> mid2props_t;

  static bool _check_dir(int check, char* path, int nested) {
    static mid_cookies_t  cookies;
    static mid2props_t     dirs;
    bool rc = true;
    size_t len = ::strlen(path);

    if (nested == 0)
      cookies.clear();

    ::strncat(path + ::strlen(path), "\\*.*", MAX_PATH - ::strlen(path));

    ::WIN32_FIND_DATA fd;
    ::HANDLE hFind = ::FindFirstFile(path, &fd);

    if (hFind == INVALID_HANDLE_VALUE)
      return false;

    path[len] = 0;

    do {
      if ((fd.dwFileAttributes & (FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_DEVICE)) == 0) {
        char p[MAX_PATH + 1];

        if (::_snprintf(p, sizeof p, "%s\\%s", path, fd.cFileName) >= sizeof p) {
          FUSION_WARN("warning: path is too long: \"%s/%s\"", path, fd.cFileName);

          continue;
        }

        if (nested == 0 && ::stricmp(fd.cFileName, LOCK_FILENAME) == 0) {
          FUSION_DEBUG("ignore lock file: \"%s\"", p);

          continue;
        }

        ::HANDLE h = ::CreateFile(p, FILE_ALL_ACCESS, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

        if (h != INVALID_HANDLE_VALUE) {
          imp_t     imp(h);
          mid_t     m;
          mprops_t  x;
          msecs_t   tt;
          result_t  e = imp.read(x.type_, m, x.sz_, tt, tt, tt, x.has_value_, &x.data_len_, &x.data_);
          ::BY_HANDLE_FILE_INFORMATION fi;
          bool      rc1 = ::GetFileInformationByHandle(h, &fi) == TRUE;

          FUSION_ASSERT(rc1);

          int64_t cookie   = fi.nFileIndexLow + ((int64_t)fi.nFileIndexHigh << 32);
          bool    softlink = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
          bool    link     = false;

          if (e == ERR_OK) {
            mid_cookies_t::const_iterator I = cookies.find(m);

            if (I != cookies.cend()) {
              link = I->second == cookie;

              if (!link) {
                rc = false;
                e = ERR_CONFIGURATION;

                FUSION_ERROR("duplicate %s mid=%d cookie=%lld", p, m, cookie);

                continue;
              }

              if (check == MD_CHECK_DUMP_LINKS) {
                auto I = dirs.find(m);

                FUSION_ASSERT(I != dirs.cend());

                I->second.second.push_back(p);
              }
            }
            else {
              cookies.insert(std::make_pair(m, cookie));

              if (check == MD_CHECK_DUMP_LINKS) {
                std::pair<mprops_t, std::vector<std::string>> xx;

                xx.first = x;
                xx.second.push_back(p);

                dirs.insert(std::make_pair(m, xx));
              }
            }
          }

          if (check == MD_CHECK_DELETE_UNRELATED) {
            if (e != ERR_OK) {
              ::fprintf(stderr, "delete unrelated: \"%s\"\n", p);
              BOOL rc = ::DeleteFile(p);
              FUSION_ASSERT(rc);
            }
          }

          if (check == MD_CHECK_READONLY || check == MD_CHECK_REBUILD_INDEX) {
            if (e == ERR_OK)
              FUSION_DEBUG("found message file%s: \"%s\" mid=%d cookie=%lld", softlink ? " (soft link)" : link ? " (hard link)" : "", p, m, cookie);
            else
              FUSION_WARN("found unrelated file: \"%s\"", p);
          }

          if (check == MD_CHECK_REBUILD_INDEX) {
            mid_t m1 = midpool__.get(m);

            FUSION_DEBUG("'%s'+%d", p, m);

//          if (e == ERR_OK && !(softlink || link)) {
//            FUSION_ASSERT(m == m1);

//            if (m != m1)
//              rc = false;
//          }
          }
        }
        else
          FUSION_WARN("%s - %d", p, ::GetLastError());
      }
      else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (::strcmp(fd.cFileName, ".") && ::strcmp(fd.cFileName, "..")) {
          char p[MAX_PATH + 1];

          ::_snprintf(p, sizeof p, "%s\\%s", path, fd.cFileName);

          if (!::strcmp(fd.cFileName, PROFILE_DIRNAME)) {
            FUSION_DEBUG("ignore profiles directory: \"%s\"", p);

            continue;
          }

          rc &= _check_dir(check, p, nested + 1);
        }
      }
    }
    while (::FindNextFile(hFind, &fd));

    ::FindClose(hFind);

    if (nested == 0) {
      if (check == MD_CHECK_DUMP_LINKS) {
        size_t conf_len = ::strlen(conf_path__);

        for (auto I = dirs.begin(), E = dirs.end(); I != E; ++I) {
          if (I->second.second.size() >= _dump_tag_link_nr_) {
            if (_dump_tag_mid)
              printf("mid:%d\t", I->first);

            if (_dump_tag_size)
              printf("size:%d\t", I->second.first.sz_);

            if (_dump_tag_type)
              printf("type:%d\t", I->second.first.type_);

            if (_dump_tag_data && I->second.first.has_value_) {
              {
                char txt[1024];
                char bin[1024];

                int tlen = txtlen(I->second.first.data_len_, (const char*)I->second.first.data_);

                FUSION_ASSERT(tlen > 0);
                FUSION_ASSERT(bin2txt(I->second.first.data_len_, (const char*)I->second.first.data_, sizeof txt, txt) == tlen);

                tlen = txt2bin(tlen, txt, sizeof bin, bin);

                FUSION_ASSERT(tlen != -1 && tlen <= (int)I->second.first.data_len_);
                FUSION_ASSERT(::memcmp(I->second.first.data_, bin, I->second.first.data_len_) == 0);
              }

              printf("data:");
              bin2txt(stdout, I->second.first.data_len_, (const char*)I->second.first.data_);
              printf("\t");
            }

            for (auto J = I->second.second.cbegin(), K = I->second.second.cend(); J != K; ++J) {
              if (J != I->second.second.cbegin())
                printf("\t");

              printf("%s", J->c_str() + conf_len);
            }

            printf("\n");
          }
        }

        dirs.clear();
      }
      else if (rc)
        switch (check) {
        case MD_CHECK_REBUILD_INDEX:    FUSION_INFO("configuration index built, allocated mids=%d", midpool__.nr()); break;
        case MD_CHECK_READONLY:         FUSION_INFO("configuration read-only scan done"); break;
        case MD_CHECK_DELETE_UNRELATED: FUSION_INFO("configuration unrelated files removed"); break;
        }
    }

    return rc;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t _delete_unrelated() {
    char path[MAX_PATH + 1];

    ::strncpy(path, conf_path__, sizeof path);

    bool rc = _check_dir(MD_CHECK_DELETE_UNRELATED, path);

    return rc ? ERR_OK : ERR_CONFIGURATION;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t _check_fix() {
    char path[MAX_PATH + 1];

    midpool__.reset();

    ::strncpy(path, conf_path__, sizeof path);

    return _check_dir(MD_CHECK_REBUILD_INDEX, path) ? ERR_OK : ERR_CONFIGURATION;
  }

  //////////////////////////////////////////////////////////////////////////////
  static result_t _check_readonly() {
    char path[MAX_PATH + 1];

    ::strncpy(path, conf_path__, sizeof path);

    bool rc = _check_dir(MD_CHECK_READONLY, path);

    return rc ? ERR_OK : ERR_CONFIGURATION;
  }

  //////////////////////////////////////////////////////////////////////////////
  const md_t* FUSION_CALLING_CONVENTION md_get_internal_descriptor(mid_t m) {
    mdescs_t::const_iterator I = mds__.find(m);

    if (I == mds__.cend())
      return 0;

    return I->second;;
  }

  //////////////////////////////////////////////////////////////////////////////
  bool FUSION_CALLING_CONVENTION md_flush_internal_descriptor(const md_t* md) {
    FUSION_ENSURE(md, return false; );

    return ((md_t*)md)->write() == ERR_OK;
  }

  //////////////////////////////////////////////////////////////////////////////
  result_t FUSION_CALLING_CONVENTION md_access(cid_t c, mid_t m, op_t op) {
    ccb_t*    ccb;
    md_t*     md;
    unsigned  oflags;

    {
      ccbs_t::const_iterator I = ccbs__.find(c);

      if (I == ccbs__.cend())
        return ERR_REGISTERED;

      ccb = I->second;
    }

    if (m < MD_SYS_LAST_)
      switch (m) {
      case MD_SYS_STATUS:
      case MD_SYS_ECHO_REPLY:
        return ERR_OK;

      case MD_SYS_ECHO_REQUEST:
        return ccb->dp_->allow_send_echo_ ? ERR_OK: ERR_PERMISSION;

      case MD_SYS_STOP_REQUEST:
        return ccb->dp_->allow_send_stop_ ? ERR_OK: ERR_PERMISSION;

      case MD_SYS_TERMINATE_REQUEST:
        return ccb->dp_->allow_send_terminate_ ? ERR_OK: ERR_PERMISSION;

      case MD_SYS_SYSINFO_REQUEST:
        return ccb->dp_->allow_allow_use_system_info ? ERR_OK: ERR_PERMISSION;

      default:
        return ERR_PERMISSION;
      }

    {
      ccb_mds_t::const_iterator I = ccb->omsgs_.find(m);

      if (I == ccb->omsgs_.cend())
        return ERR_OPEN;

      md      = I->second.md_;
      oflags  = I->second.oflags_;
    }

    FUSION_ASSERT(md);
    FUSION_ASSERT(md->mid_ == m);
    FUSION_ASSERT(ccb);

    switch (op) {
    case MD_ACCESS_OPERATION_READ:
      return oflags & O_WRONLY ? ERR_PERMISSION : ERR_OK;

    case MD_ACCESS_OPERATION_WRITE:
      return oflags & O_RDONLY ? ERR_PERMISSION : ERR_OK;
    }

    return ERR_UNEXPECTED;
  }

  //////////////////////////////////////////////////////////////////////////////
  void FUSION_CALLING_CONVENTION md_foreach_message(void* ctx, cid_t c, bool (*pred)(void* ctx, unsigned oflags), void (*cb)(void* ctx, const ccb_t* ccb)) {
    FUSION_ASSERT(cb);

    auto I = ccbs__.find(c);

    if (I != ccbs__.end()) {
      ccb_t* ccb = I->second;

      cb(ctx, ccb);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  void FUSION_CALLING_CONVENTION md_foreach_message(void* ctx, mid_t m, bool (*pred)(void* context, unsigned oflags), void (*cb)(void* context, const md_t* md, const ccb_t* ccb)) {
    FUSION_ASSERT(cb);

    for (auto I = ccbs__.cbegin(); I != ccbs__.cend(); ++I) {
      cid_t  cid = I->first;
      ccb_t* ccb = I->second;

      FUSION_ASSERT(ccb);

      for (auto J = ccb->omsgs_.cbegin(), E = ccb->omsgs_.cend(); J != E; ++J)
        if ((m == MD_SYS_ANY || m == J->first) && (!pred || pred(ctx, J->second.oflags_)))
          cb(ctx, J->second.md_, ccb);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  void FUSION_CALLING_CONVENTION md_foreach_message(void* ctx, cid_t c, mid_t m, bool (*pred)(void* ctx, unsigned oflags), void (*cb)(void* ctx, const md_t* md, const ccb_t* ccb)) {
    FUSION_ASSERT(cb);

    auto I = ccbs__.find(c);

    if (I != ccbs__.end()) {
      ccb_t* ccb = I->second;

      for (auto J = ccb->omsgs_.cbegin(), E = ccb->omsgs_.cend(); J != E; ++J)
        if ((m == MD_SYS_ANY || m == J->first) && (!pred || pred(ctx, J->second.oflags_)))
          cb(ctx, J->second.md_, ccb);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  profile_opts_t* FUSION_CALLING_CONVENTION md_get_default_profile(cid_t c) {
    ccb_t*  ccb = 0;

    {
      ccbs_t::const_iterator I = ccbs__.find(c);

      if (I == ccbs__.cend())
        return 0;

      ccb = I->second;
    }

    return ccb->dp_;
  }

  //////////////////////////////////////////////////////////////////////////////
  namespace {
    struct pod_md_t {
      _md_t pod_;
      char  fill_[sizeof md_t - sizeof _md_t];
    };

    static pod_md_t special_mds__[] = {
      {{ MD_SYS_STATUS,             MT_EVENT,  4, }},
      {{ MD_SYS_ECHO_REQUEST,       MT_EVENT, -1, }},
      {{ MD_SYS_ECHO_REPLY,         MT_EVENT, -1, }},
      {{ MD_SYS_STOP_REQUEST,       MT_EVENT,  4, }},
      {{ MD_SYS_TERMINATE_REQUEST,  MT_EVENT,  4, }},
    };

    struct __module_init__ {
      __module_init__() {
        mds__.insert(std::make_pair(MD_SYS_STATUS,            (md_t*)special_mds__ + 0));
        mds__.insert(std::make_pair(MD_SYS_ECHO_REQUEST,      (md_t*)special_mds__ + 1));
        mds__.insert(std::make_pair(MD_SYS_ECHO_REPLY,        (md_t*)special_mds__ + 2));
        mds__.insert(std::make_pair(MD_SYS_STOP_REQUEST,      (md_t*)special_mds__ + 3));
        mds__.insert(std::make_pair(MD_SYS_TERMINATE_REQUEST, (md_t*)special_mds__ + 4));
      };
    } __module_init__;
  }
}
