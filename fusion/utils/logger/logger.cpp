/*
 *  FUSION
 *  Copyright (c) 2013-2014 Alex Kosterin
 */

#include <set>
#include <map>
#include <vector>
#include <time.h>

#include "include/nf.h"	    // FUSION
#include "include/mb.h"	    // FUSION MESSAGE BUS
#include "include/nf_macros.h"
#include "include/nf_mcb.h"	// FUSION MCB
#include "include/tsc.h"

#include "logger.pb.h"
#include "mcb.proto/mcb.pb.h"

#include "driver.h"
#include "utils.h"

#include "libs/getopt.h"
#include <Rpc.h>

#include "include/nfs_client.h"
#include "include/nfel_client.h"

nf::client_t* pclient;
nf::result_t e;

volatile bool stop        = false;
volatile bool need_cleanup= true;
const char* comment       = 0;
const char* user          = 0;

int64_t total_max_time    = 0;
int64_t total_max_msg_nr  = 0;
int64_t total_max_size    = 0;

int64_t total_msg_nr      = 0;
int64_t total_size        = 0;
nf::msecs_t total_start_ts;

int64_t vol_max_time      = 0;
int64_t vol_max_msg_nr    = 0;
int64_t vol_max_size      = 0;
nf::msecs_t vol_start_ts;

size_t total_vol_nr_max   = 0;

const nf::mid_t CLIENT_NAME_PSEUDO_MID    = 1;
const nf::mid_t CLIENT_PROFILE_PSEUDO_MID = 2;
const nf::mid_t CLIENT_ADDRESS_PSEUDO_MID = 3;
const nf::mid_t CLIENT_GUID_PSEUDO_MID    = 4;

#define PSEUDO_TAG_CLIENT_NAME    "*client-name"
#define PSEUDO_TAG_CLIENT_PROFILE "*client-profile"
#define PSEUDO_TAG_CLIENT_ADDRESS "*client-address"
#define PSEUDO_TAG_CLIENT_GUID    "*client-guid"

enum mode_t {
  _CAPTURE,
  _DUMP,
  _PLAYBACK,
};

const char* opt_name          = "logger";
const char* opt_guid          = 0;
const char* opt_dir           = "./";
const char* opt_template      = "log-%N.dat";
const char* opt_comment       = 0;
int opt_verbose               = 0;
int opt_period                = 1000;

bool opt_resolve_client_name  = false;
bool opt_resolve_client_prof  = false;
bool opt_resolve_client_addr  = false;
bool opt_resolve_client_guid  = false;
bool opt_have_tsc             = true;
bool opt_have_src             = true;
bool opt_have_dst             = false;
bool opt_have_seq             = false;
bool opt_have_val             = true;

const char* opt_on_vol_fini   = 0;
const char* opt_on_vol_error  = 0;

bool opt_status               = false;
int  opt_status_period        = 0;

static size_t __type_size[type_t::_LAST] = {
 -2,    // will be filled by mopen call
  0,
  1,
  2,
  4,
  4,
  8,
 -1,
 -3,    // must/will be overridden by user settings
};

struct msg_internal_desc_t {
  nf::mid_t mid_;
  type_t    type_;
  size_t    size_;
};

struct less {
  bool operator()(const char* s1, const char* s2) const {
    return ::stricmp(s1, s2) < 0;
  }
};

typedef std::map<nf::mid_t, size_t> mpos_t;

static mpos_t  m2idx__;
static msgs_t  msgs__;
static driver_t& drv = *driver_t::create("V1");

struct client_info_t {
  bool        logged_;  // logged into current volume
  const char* name_;    // free form info string

  client_info_t(const char* name) : name_(::strdup(name)), logged_(false) {}
  ~client_info_t() { ::free((void*)name_); }
};

typedef std::map<nf::cid_t, client_info_t*> client_infos_t;

static client_infos_t cis__;

////////////////////////////////////////////////////////////////////////////////
nf::result_t get_sysinfo() {
  mcb_sysinfo_request q;
  mcb_sysinfo_reply   r;
  nf::result_t e;

  q.set_flags(SYS_CLIENTS|CLI_NAME);
  q.add_cids(nf::CID_ALL|nf::CID_NOSELF);

  size_t req_len  = q.ByteSize();
  char*  req_data = (char*)::alloca(req_len);

  q.SerializeToArray(req_data, req_len);

  size_t      rep_len;
  const void* rep_data;
  nf::mid_t   rep_mid;

  if ((e = pclient->request(nf::MD_SYS_SYSINFO_REQUEST, nf::CID_SYS, req_len, req_data, rep_mid, rep_len, rep_data, 60000)) != nf::ERR_OK)
    return e;

  FUSION_ASSERT(rep_mid == nf::MD_SYS_SYSINFO_REPLY);

  if (!r.ParseFromArray(rep_data, rep_len))
    return nf::ERR_UNEXPECTED;

  std::map<nf::cid_t, int> cids;

  for (int i = 0; i < r.clients_size(); ++i)
    if (r.clients(i).has_cid())
      cids.insert(std::make_pair(r.clients(i).cid(), i));

  /*remove stale clients*/
  for (client_infos_t::iterator I = cis__.begin(), E = cis__.end(); I != E;)
    if (cids.find(I->first) == cids.end()) {
      delete(I->second);
      I = cis__.erase(I);

      continue;
    }
    else
      ++I;

  /*add new clients*/
  for (std::map<nf::cid_t, int>::iterator I = cids.begin(), E = cids.end(); I != E; ++I)
    if (cis__.find(I->first) == cis__.end()) {
      FUSION_ASSERT(r.clients(I->second).has_name());

      cis__.insert(std::make_pair(I->first, new client_info_t(r.clients(I->second).name().c_str())));
    }

  return nf::ERR_OK;
}

////////////////////////////////////////////////////////////////////////////////
void reset_client_infos() {
  for (client_infos_t::iterator I = cis__.begin(), E = cis__.end(); I != E; ++I)
    I->second->logged_ = false;
}

// HACK HACK HACK //////////////////////////////////////////////////////////////
#if  (MCBPOOL_KEEP_COUNT > 0)
nf::mcb_t::mcb_t()  {}
nf::mcb_t::~mcb_t() {}
#endif

////////////////////////////////////////////////////////////////////////////////
nf::result_t __stdcall callback(nf::mid_t m, size_t len, const void *data) {
  mpos_t::iterator I  = m2idx__.find(m);
  nf::result_t e      = nf::ERR_OK;

  FUSION_ENSURE(I != m2idx__.cend(), return nf::ERR_UNEXPECTED);

  const nf::mcb_t* mcb = pclient->get_mcb();

  if (opt_resolve_client_name) {
    client_infos_t::iterator I = cis__.find(mcb->src_);

    if (I == cis__.end())
      // refresh sysinfo
      if (get_sysinfo() != nf::ERR_OK)
        goto log;

    // do it again with new sysinfo
    I = cis__.find(mcb->src_);

    if (I == cis__.end()) {
      FUSION_WARN("cid=%d not found in sysinfo...", m);

      goto log;
    }

    if (!I->second->logged_) {
      static nf::mcb_t mcb_;
      static bool mcb_initialized_ = false;

      if (!mcb_initialized_) {
        mcb_initialized_ = true;

        mcb_.mid_     = CLIENT_NAME_PSEUDO_MID;
        mcb_.dst_     = pclient->id();
        mcb_.seq_     = 0;
        mcb_.req_seq_ = 0;
        mcb_.request_ = 0;
      }
      else
        ++mcb_.seq_;

      mcb_.src_ = I->first;
      mcb_.org_ = nf::now_msecs();
      mcb_.len_ = ::strlen(I->second->name_);

      if (mcb_.len_ > sizeof mcb_.u)
        mcb_.u.pdata_ = (void*)I->second->name_;
      else
        ::strcpy(mcb_.u.data_, I->second->name_);

      if (!drv.cb(m2idx__[CLIENT_NAME_PSEUDO_MID], &mcb_)) {
        stop  = true;
        e     = nf::ERR_UNEXPECTED;

        FUSION_ERROR("driver failed to log client info...");
      }

      I->second->logged_ = true;
    }
  }

log:
  if (!drv.cb(I->second, mcb)) {
    stop  = true;
    e     = nf::ERR_UNEXPECTED;

    FUSION_ERROR("driver failed to log message...");
  }

  return nf::ERR_OK;
}

////////////////////////////////////////////////////////////////////////////////
void on_vol_error(const char* vol_name) {
  if (opt_on_vol_error)
    _system(opt_on_vol_error, vol_name, 0);
}

////////////////////////////////////////////////////////////////////////////////
void on_vol_fini(const char* vol_name) {
  if (opt_on_vol_fini)
    _system(opt_on_vol_fini, vol_name, 0);
}

////////////////////////////////////////////////////////////////////////////////
BOOL WINAPI on_ctrl_break(DWORD CtrlType) {
  FUSION_DEBUG("CtrlType=%d", CtrlType);

  stop = true;

  // give slack terminating main thread
  // Windows will terminate process in 5 secs anyways
  do {
    ::Sleep(0);
  } while (need_cleanup);

  FUSION_DEBUG("stop=%d need_cleanup=%d", stop, need_cleanup);

  return TRUE;
}

////////////////////////////////////////////////////////////////////////////////
const char* parse_msg(const char* name) {
  if (const char *sc = ::strchr(name, PROFILE_MESSAGE_SEPARATOR_CHAR)) {
    *((char*)sc) = 0;

    const char* p = ::strdup(name);

    *((char*)sc) = PROFILE_MESSAGE_SEPARATOR_CHAR;

    return p;
  }

  return 0;
}

enum { FV_ROTATE, FV_LIMIT, FV_USER_STOP, FV_ERROR };

enum {
  LONGOPT_PERIOD = 1000,
  LONGOPT_PLAYBACK,
  LONGOPT_RECORD,
  LONGOPT_DUMP,
  LONGOPT_DUMP_HEADER,
  LONGOPT_DUMP_CSV,
  LONGOPT_FORCE,

  LONGOPT_ADD_CLIENT_NAME,
  LONGOPT_ADD_CLIENT_PROFILE,
  LONGOPT_ADD_CLIENT_ADDRESS,
  LONGOPT_ADD_CLIENT_GUID,

  LONGOPT_TOTAL_MAX_TIME,
  LONGOPT_TOTAL_MAX_MSG,
  LONGOPT_TOTAL_MAX_SIZE,
  LONGOPT_TOTAL_VOL_NR,

  LONGOPT_VOL_MAX_TIME,
  LONGOPT_VOL_MAX_MSG,
  LONGOPT_VOL_MAX_SIZE,

  LONGOPT_KEEP_ALL,
  LONGOPT_KEEP_TS,
  LONGOPT_KEEP_SRC,
  LONGOPT_KEEP_DST,
  LONGOPT_KEEP_SEQ,
  LONGOPT_KEEP_VAL,

  LONGOPT_SKIP_ALL,
  LONGOPT_SKIP_TS,
  LONGOPT_SKIP_SRC,
  LONGOPT_SKIP_DST,
  LONGOPT_SKIP_SEQ,
  LONGOPT_SKIP_VAL,

  LONGOPT_VOID,
  LONGOPT_STRING,
  LONGOPT_BYTES,
  LONGOPT_FLOAT,
  LONGOPT_DOUBLE,
  LONGOPT_BOOL,
  LONGOPT_INT,
  LONGOPT_SHORT,

  LONGOPT_ON_VOL_FINI,
  LONGOPT_ON_VOL_ERROR,

  LONGOPT_REPORT_STATUS,
};

struct option longopts[] = {
  { "port",           required_argument,  NULL, 'p',                    1, "Port to connect to Fusion",                                           "3001" },
  { "host",           required_argument,  NULL, 'H',                    1, "Fusion server address",                                               "localhost" },
  { "profile",        required_argument,  NULL, 'P',                    1, "Profile used with Fusion" },
  { "name",           required_argument,  NULL, 'n',                    1, "How Fusion will know logger",                                         "logger" },

  { "dir",            required_argument,  NULL, 'd',                    2, "Output directory",                                                    "./" },
  { "template",       required_argument,  NULL, 't',                    2, "File name template, following abbrevations will be expand like follows:\n\
    %a  Abbreviated weekday name\n\
    %A  Full weekday name\n\
    %b  Abbreviated month name\n\
    %B  Full month name\n\
    %c  Date and time representation appropriate for locale\n\
    %d  Day of month as decimal number (01 – 31)\n\
    %H  Hour in 24-hour format (00 – 23)\n\
    %I  Hour in 12-hour format (01 – 12)\n\
    %G  \"Run\" GUID\n\
    %j  Day of year as decimal number (001 – 366)\n\
    %m  Month as decimal number (01 – 12)\n\
    %M  Minute as decimal number (00 – 59)\n\
    %N  Volume numer 0, 1, ..., 10, ... Use %NNN for this: 000, 001, ..., 010, ...\n\
    %p  Current locale's A.M./P.M. indicator for 12-hour clock\n\
    %S  Second as decimal number (00 – 59)\n\
    %U  Week of year as decimal number, with Sunday as first day of week (00 – 53)\n\
    %w  Weekday as decimal number (0 – 6; Sunday is 0)\n\
    %W  Week of year as decimal number, with Monday as first day of week (00 – 53)\n\
    %x  Date representation for current locale\n\
    %X  Time representation for current locale\n\
    %y  Year without century, as decimal number (00 – 99)\n\
    %Y  Year with century, as decimal number\n\
    %z, %Z  Either the time-zone name or time zone abbreviation, depending on registry settings; no characters if time zone is unknown\n\
    %%  Percent sign\n" },

  { "comment",        required_argument,  NULL, 'c',                    2, "Comment for \"run\"" },
  { "guid",           required_argument,  NULL, 'g',                    2, "GUID for \"run\"" },
  { "period",         required_argument,  NULL, LONGOPT_PERIOD,         2, "Disptch timeout (responsiveness)",                                    "1000 msecs" },
  { "playback",       required_argument,  NULL, LONGOPT_PLAYBACK,       2, "?"  },
  { "record",         optional_argument,  NULL, LONGOPT_RECORD,         2, "Log events",                                                          "Yes" },
  { "dump",           required_argument,  NULL, LONGOPT_DUMP,           2, "Dump" },
  { "dump-header",    required_argument,  NULL, LONGOPT_DUMP_HEADER,    2, "Dump header" },
  { "dump-csv",       required_argument,  NULL, LONGOPT_DUMP_CSV,       2, "Dump csv <<TODO>>" },
  { "force",          no_argument,        NULL, LONGOPT_FORCE,          2, "Ignore non-fatal errors",                                             "Yes" },

  { "tag",            required_argument,  NULL, 'm',                    3, "Add tag for logging" },
  { "message",        required_argument,  NULL, 'm',                    3, "Add tag for logging, same as --tag" },
    { "void",         no_argument,        NULL, LONGOPT_VOID,           3, "Tag does not have any data" },
    { "string",       no_argument,        NULL, LONGOPT_STRING,         3, "Tag is string, variable length" },
    { "bytes",        no_argument,        NULL, LONGOPT_BYTES,          3, "Tag is array of bytes, fixed length" },
    { "float",        no_argument,        NULL, LONGOPT_FLOAT,          3, "Tag is float, 4 bytes" },
    { "double",       no_argument,        NULL, LONGOPT_DOUBLE,         3, "Tag is float, 8 bytes" },
    { "bool",         no_argument,        NULL, LONGOPT_BOOL,           3, "Tag is bool,  1 byte" },
    { "int",          no_argument,        NULL, LONGOPT_INT,            3, "Tag is int,   4 bytes" },
    { "short",        no_argument,        NULL, LONGOPT_SHORT,          3, "Tag is short, 2 bytes" },
    { "auto",         no_argument,        NULL, 'a',                    3, "Size of tag determined by Fusion" },
      { "format",     optional_argument,  NULL, 'f',                    3, "Format string", "Depends on type: int - %d, float, double - %g, etc." },

  { "resolve-client-name",    no_argument, NULL, LONGOPT_ADD_CLIENT_NAME,     3, "Add clients names to log" },
  { "resolve-client-profile", no_argument, NULL, LONGOPT_ADD_CLIENT_PROFILE,  3, "Add clients profiles to log" },
  { "resolve-client-address", no_argument, NULL, LONGOPT_ADD_CLIENT_ADDRESS,  3, "Add clients addresses to log" },
  { "resolve-client-guid",    no_argument, NULL, LONGOPT_ADD_CLIENT_GUID,     3, "Add clients guids to log" },

  { "total-max-time", required_argument,  NULL, LONGOPT_TOTAL_MAX_TIME, 4, "Logging stops if total time exceeds this. 0 - unlim",                 "0" },
  { "total-max-msg",  required_argument,  NULL, LONGOPT_TOTAL_MAX_MSG,  4, "Logging stops if total number of events exceeds this. 0 - unlim",     "0" },
  { "total-max-size", required_argument,  NULL, LONGOPT_TOTAL_MAX_SIZE, 4, "Logging stops if total files size exceeds this. 0 - unlim",           "0" },
  { "total-vol-nr",   required_argument,  NULL, LONGOPT_TOTAL_VOL_NR,   4, "Max number of volumes. 0 - unlim",                                    "0" },

  { "vol-max-time",   required_argument,  NULL, LONGOPT_VOL_MAX_TIME,   4, "Start new volume if volume time exceeds this. 0 - never",             "1 hour" },
  { "vol-max-msg",    required_argument,  NULL, LONGOPT_VOL_MAX_MSG,    4, "Start new volume if number of volume events exceeds this. 0 - never", "0" },
  { "vol-max-size",   required_argument,  NULL, LONGOPT_VOL_MAX_SIZE,   4, "Start new volume if volume file size exceeds this. 0 - never",        "2GB" },

  { "keep-all",       no_argument,        NULL, LONGOPT_KEEP_ALL,       5, "Write all data and metadata: timestamp, source, destination, sequence, and data" },
  { "keep-ts",        no_argument,        NULL, LONGOPT_KEEP_TS,        5, "Write timestamp",   "Yes" },
  { "keep-src",       no_argument,        NULL, LONGOPT_KEEP_SRC,       5, "Write source",      "Yes" },
  { "keep-dst",       no_argument,        NULL, LONGOPT_KEEP_DST,       5, "Write destination", "No"  },
  { "keep-seq",       no_argument,        NULL, LONGOPT_KEEP_SEQ,       5, "Write sequence",    "No"  },
  { "keep-val",       no_argument,        NULL, LONGOPT_KEEP_VAL,       5, "Write data",        "Yes" },

  { "skip-all",       no_argument,        NULL, LONGOPT_SKIP_ALL,       5, "Ignore all data and metadata: timestamp, source, destination, sequence, and data" },
  { "skip-ts",        no_argument,        NULL, LONGOPT_SKIP_TS,        5, "Ignore timestamp",  "No"  },
  { "skip-src",       no_argument,        NULL, LONGOPT_SKIP_SRC,       5, "Ignore source",     "No"  },
  { "skip-dst",       no_argument,        NULL, LONGOPT_SKIP_DST,       5, "Ignore destination","Yes" },
  { "skip-seq",       no_argument,        NULL, LONGOPT_SKIP_SEQ,       5, "Ignore sequence",   "Yes" },
  { "skip-val",       no_argument,        NULL, LONGOPT_SKIP_VAL,       5, "Ignore data",       "No"  },

  { "on-vol-fini",    required_argument,  NULL, LONGOPT_ON_VOL_FINI,    6, "Run script on succesful volume completion", "None" },
  { "on-error",       required_argument,  NULL, LONGOPT_ON_VOL_ERROR,   6, "Run script on volume error",                "None" },
  { "report-status",  optional_argument,  NULL, LONGOPT_REPORT_STATUS,  6, "Report status",                             "No" },

  { "verbose",        optional_argument,  NULL, 'v',                    7, "Set verbosity" },
  { "version",        no_argument,        NULL, 'V',                    7, "Show version and exit" },
  { "help",           no_argument,        NULL, 'h',                    7, "Show this screen" },

  {0, no_argument, NULL, 0}
};

////////////////////////////////////////////////////////////////////////////////
void help(const char* prog, int desc_pos = 24, int dflt_pos = 90) {
  ::fprintf(stderr, "Help: %s\n", prog);

  for (int section = 0; section < 10; ++section) {
    bool fst = section != 0;

    for (int i = 0; i < FUSION_ARRAY_SIZE(longopts) - 1; ++i)
      if (longopts[i].help_section_ == section) {
        if (fst) {
          fst = false;
          ::fprintf(stderr, "\n");
        }

        int pos = 0;

        if (longopts[i].val < 127 && ::isprint(longopts[i].val))
          pos += ::fprintf(stderr, "-%c,", longopts[i].val);

        if (longopts[i].help_text_)
          pos += ::fprintf(stderr, "--%s", longopts[i].name);

        switch (longopts[i].has_arg) {
        case required_argument: pos += ::fprintf(stderr, "=... ");    break;
        case optional_argument: pos += ::fprintf(stderr, "[=...] ");  break;
        default:                pos += ::fprintf(stderr, " ");        break;
        }

        if (longopts[i].help_text_) {
          while (pos < desc_pos)
            pos += ::fprintf(stderr, " ");

          if (longopts[i].help_text_)
            pos += ::fprintf(stderr, "%s ", longopts[i].help_text_);
        }

        if (longopts[i].help_defalult_) {
          while (pos < dflt_pos)
            pos += ::fprintf(stderr, " ");

          pos += ::fprintf(stderr, "default: %s", longopts[i].help_defalult_);
        }

        ::fprintf(stderr, "\n");
      }
  }
}

////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* const * argv) {
  const char* port    = "3001";
  const char* host    = "127.0.0.1";
  const char* profile = 0;
  mode_t      mode = mode_t::_CAPTURE;
  double      playback_speed = 1.;
  size_t      period = 1000;
  std::set<const char*, less> profiles;
  msg_desc_t  md = { 0, 0, type_t::_AUTO, 0, 0 };
  bool        opt_force   = false;
  header      hdr;
  nf::result_t e;

  opterr = 1; // let getopt() print error messages...

  while (true) {
    int c, i = 0;

    switch (c = getopt_long(argc, argv, "p:H:P:n:c:d:t:g:m:af:hvV", longopts, &i)) {
    case ':':
    case '?':
      help(argv[0]);
      exit(1);

    case 'p': port          = optarg;       break;
    case 'H': host          = optarg;       break;
    case 'P': profile       = optarg;       break;
    case 'n': opt_name      = optarg;       break;
    case 'c': opt_comment   = optarg;       break;
    case 'd': opt_dir       = optarg;       break;
    case 't': opt_template  = optarg;       break;

    case LONGOPT_PERIOD:   period = ::atoi(optarg);     break;
    case LONGOPT_PLAYBACK: mode = mode_t::_CAPTURE;     break;
    case LONGOPT_RECORD:
      mode = mode_t::_PLAYBACK;

      if (optarg)
        playback_speed = ::atof(optarg);

      break;

    case LONGOPT_DUMP:
      return drv.dump(optarg) ? 0 : 1;

    case LONGOPT_DUMP_HEADER:
      return drv.dump_header(optarg) ? 0 : 1;

    case LONGOPT_DUMP_CSV:
      return drv.dump_csv(optarg) ? 0 : 1;

    case LONGOPT_FORCE:
      opt_force = true;
      break;

    case LONGOPT_VOID:    md.type_  = type_t::_VOID;    md.size_ = __type_size[md.type_]; break;
    case LONGOPT_STRING:  md.type_  = type_t::_STR;     md.size_ = __type_size[md.type_]; md.fmt_ = "%s"; break;
    case LONGOPT_BYTES :  md.type_  = type_t::_SIZE;    md.size_ = __type_size[md.type_]; break;
    case LONGOPT_FLOAT :  md.type_  = type_t::_FLT;     md.size_ = __type_size[md.type_]; md.fmt_ = "%g"; break;
    case LONGOPT_DOUBLE:  md.type_  = type_t::_DBL;     md.size_ = __type_size[md.type_]; md.fmt_ = "%g"; break;
    case LONGOPT_BOOL:    md.type_  = type_t::_BOOL;    md.size_ = __type_size[md.type_]; md.fmt_ = "%d"; break;
    case LONGOPT_INT:     md.type_  = type_t::_INT;     md.size_ = __type_size[md.type_]; md.fmt_ = "%d"; break;
    case LONGOPT_SHORT:   md.type_  = type_t::_SHORT;   md.size_ = __type_size[md.type_]; md.fmt_ = "%d"; break;
    case 'a':             md.type_  = type_t::_AUTO;    md.size_ = __type_size[md.type_]; break;

    case 'f':             md.fmt_   = optarg;           break;

    case LONGOPT_ADD_CLIENT_NAME:
      // do it only once
      if (!opt_resolve_client_name) {
        opt_resolve_client_name = true;

        msg_desc_t d = md;

        md.name_  = PSEUDO_TAG_CLIENT_NAME;
        md.size_  = -1;
        md.fmt_   = "%s";
        md.idx_   = m2idx__[CLIENT_NAME_PSEUDO_MID] = msgs__.size();
        md.type_  = _STR;

        msgs__.push_back(md);

        tag* t = hdr.add_tags();

        t->set_name(md.name_);
        t->set_size(md.size_);
        t->set_format(md.fmt_);
        t->set_index(md.idx_);
        t->set_mid(CLIENT_NAME_PSEUDO_MID);

        md = d;
      }

      break;

    case LONGOPT_ADD_CLIENT_PROFILE:
    case LONGOPT_ADD_CLIENT_ADDRESS:
    case LONGOPT_ADD_CLIENT_GUID:
      FUSION_WARN("option %s not implemented", argv[optind - 1]);

      break;

    case 'm':
      /*add tag using current type and format*/
      if (const char* p = parse_msg(md.name_ = optarg))
        profiles.insert(p);

      msgs__.push_back(md);

      {
        tag* t = hdr.add_tags();

        t->set_name(md.name_);
        t->set_size(md.size_);
        t->set_index(-1);

        if (md.fmt_)
          t->set_format(md.fmt_);
      }

      break;

    case LONGOPT_TOTAL_MAX_TIME:  total_max_time    = parse_time(optarg); break;
    case LONGOPT_TOTAL_MAX_MSG:   total_max_msg_nr  = ::atoi(optarg); break;
    case LONGOPT_TOTAL_MAX_SIZE:  total_max_size    = parse_size(optarg); break;
    case LONGOPT_TOTAL_VOL_NR:    total_vol_nr_max  = ::atoi(optarg); break;

    case LONGOPT_VOL_MAX_TIME:    vol_max_time      = parse_time(optarg); break;
    case LONGOPT_VOL_MAX_MSG :    vol_max_msg_nr    = ::atoi(optarg); break;
    case LONGOPT_VOL_MAX_SIZE:    vol_max_size      = parse_size(optarg); break;

    case LONGOPT_KEEP_ALL:
      opt_have_tsc  = true;
      opt_have_src  = true;
      opt_have_dst  = true;
      opt_have_seq  = true;
      opt_have_val  = true;
      break;

    case LONGOPT_KEEP_TS:   opt_have_tsc  = true;     break;
    case LONGOPT_KEEP_SRC:  opt_have_src  = true;     break;
    case LONGOPT_KEEP_DST:  opt_have_dst  = true;     break;
    case LONGOPT_KEEP_SEQ:  opt_have_seq  = true;     break;
    case LONGOPT_KEEP_VAL:  opt_have_val  = true;     break;

    case LONGOPT_SKIP_ALL:
      opt_have_tsc  = false;
      opt_have_src  = false;
      opt_have_dst  = false;
      opt_have_seq  = false;
      opt_have_val  = false;
      break;

    case LONGOPT_SKIP_TS:   opt_have_tsc  = false;     break;
    case LONGOPT_SKIP_SRC:  opt_have_src  = false;     break;
    case LONGOPT_SKIP_DST:  opt_have_dst  = false;     break;
    case LONGOPT_SKIP_SEQ:  opt_have_seq  = false;     break;
    case LONGOPT_SKIP_VAL:  opt_have_val  = false;     break;

    case LONGOPT_ON_VOL_FINI:
      opt_on_vol_fini = optarg;

      break;

    case LONGOPT_ON_VOL_ERROR:
      opt_on_vol_error = optarg;

      break;

    case LONGOPT_REPORT_STATUS:
      opt_status = true;
      opt_status_period = optarg ? ::atoi(optarg) : -DEFAULT_RETRANSMIT_PERIOD_MSEC;

      // ensure 'automatic' status retransmission
      if (opt_status_period > 0)
        opt_status_period = -opt_status_period;

      break;

    case 'g':
      opt_guid = optarg;

      break;

    case 'v':
      if (optarg)
        opt_verbose = ::atoi(optarg);
      else
        ++opt_verbose;

      break;

    case 'V':
      ::fprintf(stdout, "Logger version: %d.%d", VER_MAJ, VER_MIN);
      exit(0);

      break;

    case 'h':
      help(argv[0]);
      exit(1);

      break;

    case EOF:
      goto verify_opts;
    }

    if (opt_verbose > 3)
      fprintf(stderr, "c=%c i=%d optarg=%s\targ=%s\n", c, i, optarg, optind ? argv[optind] : "");
  }

verify_opts:
  if (opt_verbose > 2) {
    fprintf(stderr, "Command line args: ");

    for (int i = 1; i < argc; ++i)
      fprintf(stderr, "%s ", argv[i]);

    fprintf(stderr, "\n\n");
  }

  if (opt_verbose > 2) {
    fprintf(stderr, "\
Options as parsed:\n\
  port=%s\n\
  host=%s\n\
  profile=%s\n\
  name=%s\n\
  comment=%s\n\
  verbose=%d\n\
  dir=%s\n\
  template=%s\n\
  period=%d\n\
  mode=%d\n\
\n\
  total_max_time=%lld\n\
  total_max_msg_nr=%lld\n\
  total_max_size=%lld\n\
  total_vol_nr_max=%d\n\
\n\
  vol_max_time=%lld\n\
  vol_max_msg_nr=%lld\n\
  vol_max_size=%lld\n\
\n\
  have-tsc=%d\n\
  have-src=%d\n\
  have-dst=%d\n\
  have-seq=%d\n\
  have-val=%d\n\
\n\
  guid=%s\n\n\
",
      port,
      host,
      profile,
      opt_name,
      opt_comment,
      opt_verbose,
      opt_dir,
      opt_template,
      period,
      mode,

      total_max_time,
      total_max_msg_nr,
      total_max_size,
      total_vol_nr_max,

      vol_max_time,
      vol_max_msg_nr,
      vol_max_size,

      opt_have_tsc,
      opt_have_src,
      opt_have_dst,
      opt_have_seq,
      opt_have_val,

      opt_guid
    );

    fprintf(stderr, "  tags:\n");

    for (size_t i = 0; i < msgs__.size(); ++i)
      fprintf(stderr, "    index=%d\tname='%s'\ttype=%d\tformat=%s\n",
        msgs__[i].idx_,
        msgs__[i].name_,
        msgs__[i].type_,
        msgs__[i].fmt_ ? msgs__[i].fmt_ : ""
      );
  }

  FUSION_ENSURE(profile, return 1);

  nf::client_t client(opt_name);

  pclient = &client;

  {
    char connect[250];

    _snprintf(connect, sizeof connect - 1, "type=tcp host=%s port=%s", host, port);

    std::vector<const char*> pv;

    pv.push_back(profile);
    pv.insert(pv.end(), profiles.cbegin(), profiles.cend());

    FUSION_ENSURE(nf::ERR_OK == (e = client.reg(connect, pv.size(), &pv[0])), FUSION_FATAL("client.reg=%s", nf::result_to_str(e)));
  }

  FUSION_ENSURE(mode == _CAPTURE, return 1);

  if (opt_status && nfs::init_client(client, opt_status_period) != nf::ERR_OK) {
      FUSION_WARN("Fail to initialize status monitoring");

      opt_status = false;
    }

  for (size_t i = 0; i < msgs__.size(); ++i) {
	  nf::mtype_t mt;
    size_t      sz;
    nf::mid_t   m;

    if (hdr.mutable_tags(i)->has_mid()) {
      if (hdr.mutable_tags(i)->mid() < 1024/*pseudo mids*/)
        continue;

      FUSION_FATAL("Unknown pseudo mid=%d", hdr.mutable_tags(i)->mid());
    }

    e = client.mopen(msgs__[i].name_, nf::O_RDONLY, mt, m, sz);

    if (e == nf::ERR_OPEN && m != 0) {
      FUSION_ASSERT(m2idx__.find(m) != m2idx__.end());
      FUSION_INFO("tag duplicate: '%s' '%s'", msgs__[i].name_, msgs__[m2idx__[m]].name_);

      msgs__[i].idx_ = m2idx__[m];

      hdr.mutable_tags(i)->set_index(msgs__[i].idx_);
      hdr.mutable_tags(i)->set_mid(m);

      if (msgs__[i].size_ != msgs__[m2idx__[m]].size_) {
        FUSION_WARN("overriding size: %d->%d", msgs__[i].size_, msgs__[m2idx__[m]].size_);

        msgs__[i].size_ = msgs__[m2idx__[m]].size_;
        hdr.mutable_tags(i)->set_size(msgs__[i].size_);
      }

      // msgs__[i].type_ = ??? // @ensure type compatibe@
      // msgs__[i].fmt_ = ??? // @ensure format compatibe@

      continue;
    }

    if (e != nf::ERR_OK)
      if (opt_force) {
        FUSION_WARN("client.mopen(%s)=%s", msgs__[i].name_, result_to_str(e));

        msgs__[i].idx_ = -1;

        continue;
      }
      else
        FUSION_FATAL("client.mopen(%s)=%s", msgs__[i].name_, result_to_str(e));

    m2idx__[m] = msgs__[i].idx_ = i;

    hdr.mutable_tags(i)->set_index(i);
    hdr.mutable_tags(i)->set_mid(m);

    switch (msgs__[i].type_) {
    case _VOID:
    case _BOOL:
    case _SHORT:
    case _INT:
    case _FLT:
    case _DBL:
    case _STR:
      if (__type_size[msgs__[i].type_] != sz)
        if (opt_force)
          FUSION_WARN("size mismatch: %d/%d", __type_size[msgs__[i].type_], sz);
        else
          FUSION_ASSERT(__type_size[msgs__[i].type_] == sz);

      break;

    case _SIZE:
      if (msgs__[i].size_ != sz)
        if (opt_force) {
          FUSION_WARN("size mismatch: %d/%d", msgs__[i].size_, sz);

          msgs__[i].size_ = sz;
          hdr.mutable_tags(i)->set_size(sz);
        }
        else
          FUSION_VERIFY(msgs__[i].size_ == sz);

      break;

    case _AUTO:
      /* our best guess */
      switch (msgs__[i].size_ = sz) {
      case 0:
        msgs__[i].type_ = _VOID;
        break;

      case 1:
        msgs__[i].type_ = _BOOL;
        break;

      case 2:
        msgs__[i].type_ = _SHORT;
        break;

      case 4:
        msgs__[i].type_ = _INT;
        break;

      case 8:
        msgs__[i].type_ = _DBL;
        break;

      default:
        msgs__[i].type_ = _SIZE;
        break;
      }

      hdr.mutable_tags(i)->set_size(sz);
      break;

    default:
      FUSION_ASSERT(0, "invalid type");

      break;
    }
  }

  size_t subs_nr = 0;

  for (mpos_t::const_iterator I = m2idx__.cbegin(), E = m2idx__.cend(); I != E; ++I) {
    if (I->first == CLIENT_NAME_PSEUDO_MID)
      continue;

    nf::result_t e = client.subscribe(I->first, nf::SF_PUBLISH, nf::CM_MANUAL, callback);

    if (e != nf::ERR_OK) {
      if (opt_force)
        FUSION_WARN("subscribe %s=%s",  msgs__[I->second].name_, result_to_str(e));
      else
        FUSION_FATAL("subscribe %s=%s", msgs__[I->second].name_, result_to_str(e));

      continue;
    }

    ++subs_nr;
  }

  if (!subs_nr) {
    if (opt_status)
      nfs::set_status(nfs::STATUS_WARN, "Nothing to log! Exiting...");

    FUSION_WARN("Nothing to log! Exiting...");

    return 1;
  }

  // fill-in "soft" header //
  {
    char buff[1024];
    DWORD len;

    if (opt_comment)
      hdr.set_comment(opt_comment);

    hdr.set_cmdline(::GetCommandLine());

    time_t      t;
    struct tm*  timeinfo;

    time(&t);
    timeinfo = ::gmtime(&t);

    ::strftime(buff, sizeof buff, "%Y-%m-%d %H:%M:%S GMT", timeinfo);

    hdr.set_date(buff);

    len = sizeof buff;

    if (::GetUserName(buff, &len))
      hdr.set_user(buff);

    len = sizeof buff;

    if (::GetComputerName(buff, &len))
      hdr.set_host(buff);
  }

  char* params = (char*)::alloca(1024);
  size_t params_len = 1023;

  if (opt_guid)
    ::_snprintf(params, params_len, "keep_tsc=%d keep_src=%d keep_dst=%d keep_seq=%d keep_val=%d guid={%s}",
      opt_have_tsc,
      opt_have_src,
      opt_have_dst,
      opt_have_seq,
      opt_have_val,
      opt_guid
    );
  else
    ::_snprintf(params, params_len, "keep_tsc=%d keep_src=%d keep_dst=%d keep_seq=%d keep_val=%d",
      opt_have_tsc,
      opt_have_src,
      opt_have_dst,
      opt_have_seq,
      opt_have_val
    );

  if (!drv.init_capture(msgs__, params, hdr))
    return 1;

  FUSION_VERIFY(::SetConsoleCtrlHandler(on_ctrl_break, TRUE));

  if (drv.init_volume(opt_dir, opt_template)) {
    if (opt_status)
      nfs::set_status(nfs::STATUS_OK);

    total_start_ts = vol_start_ts = nf::now_msecs();

    while (!stop && (e = client.dispatch(period, true)) == nf::ERR_OK) {
      nf::msecs_t ts = nf::now_msecs();

      bool is_tm;
      bool is_nr;
      bool is_sz;

      //** check full stop **//
      if (
        (is_tm = (total_max_time   && (ts - total_start_ts)             >= total_max_time)) ||
        (is_nr = (total_max_msg_nr && (total_msg_nr + drv.vol_msg_nr()) >= total_max_msg_nr)) ||
        (is_sz = (total_max_size   && (total_size + drv.vol_size())     >= total_max_size))
        )
      {
        FUSION_INFO("Finishing volume and stopping due to %s [%lld]", is_tm ? "total time" : (is_nr ? "total events number" : "total size"), is_tm ? total_max_time : is_nr ? total_max_msg_nr : total_max_size);

        if (drv.fini_volume())
          on_vol_fini(drv.vol_name());
        else
          on_vol_error(drv.vol_name());

        goto done;
      }

      //** check volume rotate **//
      else if (
        (is_tm = (vol_max_time   && (ts - vol_start_ts) >= vol_max_time)) ||
        (is_nr = (vol_max_msg_nr && drv.vol_msg_nr()    >= vol_max_msg_nr)) ||
        (is_sz = (vol_max_size   && drv.vol_size()      >= vol_max_size))
        )
      {
        FUSION_INFO("Finishing volume due to %s [%lld]", is_tm ? "time" : (is_nr ? "events number" : "size"), is_tm ? vol_max_time : is_nr ? (int64_t)drv.vol_msg_nr() : (int64_t)drv.vol_size());

        total_msg_nr += drv.vol_msg_nr();
        total_size   += drv.vol_size();
        vol_start_ts  = ts;

        if (drv.fini_volume()) {
          on_vol_fini(drv.vol_name());

          if (total_vol_nr_max && (drv.vol_nr() + 1) >= total_vol_nr_max) {
            FUSION_INFO("Stopping: reach max volume number %d", total_vol_nr_max);

            goto done;
          }

          // on volume init...
          reset_client_infos();

          if (!drv.init_volume(opt_dir, opt_template)) {
            on_vol_error(drv.vol_name());

            goto done;
          }
        }
        else {
          on_vol_error(drv.vol_name());

          goto done;
        }
      }
    }

    if (e != nf::ERR_OK)
      FUSION_ERROR("dispatch=%s", nf::result_to_str(e));

    FUSION_DEBUG("stop=%d", stop);

    if (drv.fini_volume())
      on_vol_fini(drv.vol_name());
    else
      on_vol_error(drv.vol_name());
  }
  else
    on_vol_error(drv.vol_name());

done:
  FUSION_DEBUG("done: stop=%d", stop);

  drv.fini_capture();

  if (opt_status)
    nfs::fini_client();

  need_cleanup = false;

  return 0;
}

