/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <stdio.h>
#include <stdlib.h>
#include <set>
#include <map>

#include "include/nf.h"	    // FUSION
#include "include/mb.h"	    // FUSION MESSAGE BUS
#include "include/nf_macros.h"
#include "include/nf_mcb.h"	// FUSION MCB

#include "rate.h"

#define DINA_DEFAULT_NAME "easy dina"

nf::client_t* pclient = 0;

nf::result_t e;
nf::mid_t m;
static bool     stop    = false;
static bool show_src    = false;
static bool show_dst    = false;
static bool show_seq    = false;
static bool show_org    = false;
static bool show_frq    = false;
static const char* name = DINA_DEFAULT_NAME;
static int size;
static const char* val;

#define MCREATE(N, F, T, M, Z) {                                    \
  using namespace nf;                                               \
                                                                    \
  const size_t _sz  = Z;                                            \
  result_t e = pclient->mcreate(N, F, T, M, _sz);                   \
                                                                    \
  if (e != ERR_OK) {                                                \
    FUSION_WARN("pclient->mcreate(%s)=%s", N, result_to_str(e));    \
                                                                    \
    return e;                                                       \
  }                                                                 \
}

#define MCREATEP(N, F, T, M, Z, L, D) {                             \
  using namespace nf;                                               \
                                                                    \
  const size_t _sz  = Z;                                            \
  result_t e = pclient->mcreate(N, F, T, M, _sz, L, D);             \
                                                                    \
  if (e != ERR_OK) {                                                \
    FUSION_WARN("pclient->mcreate(%s)=%s", N, result_to_str(e));    \
                                                                    \
    return e;                                                       \
  }                                                                 \
}

#define MOPEN(N, F, T, M, Z) {                                      \
  using namespace nf;                                               \
                                                                    \
  size_t _sz;                                                       \
  mtype_t _t;                                                       \
  result_t e = pclient->mopen(N, F, _t, M, _sz);                    \
                                                                    \
  if (e != ERR_OK) {                                                \
    FUSION_WARN("pclient->mopen(%s)=%s", N, result_to_str(e));      \
																											              \
		return e;                                                       \
  }                                                                 \
                                                                    \
  Z = _sz;                                                          \
  T = _t;                                                           \
}

#define SUBSCRIBE(M, F, CB) {                                       \
  using namespace nf;                                               \
                                                                    \
  result_t e = pclient->subscribe(M, F, CM_MANUAL, CB);             \
                                                                    \
  if (e != ERR_OK) {                                                \
    FUSION_WARN("subscribe " STRINGIFY(M) "=%s", result_to_str(e)); \
                                                                    \
    return e;                                                       \
  }                                                                 \
}

#define MDELETE(M) {                                                \
  using namespace nf;                                               \
                                                                    \
  result_t e = pclient->munlink(M);                                 \
                                                                    \
  if (e != ERR_OK) {                                                \
    FUSION_WARN("munlink " STRINGIFY(M) "=%s", result_to_str(e));   \
                                                                    \
    return e;                                                       \
  }                                                                 \
}

enum class type_t {
  _NONE,
  _VOID,
  _BOOL,
  _INT,
  _DBL,
  _STR,
  _INT64,
  _SIZE,
};

enum class mode_t {
  _NONE,
  _READ,
  _WRITE,
  _CREATE,
  _CREATE_PERSISTENT,
  _DELETE,
  _LINK,
  _LIST,
};

static type_t type = type_t::_NONE;

static size_t get_type_size(type_t t) {
  switch (t) {
  case type_t::_NONE:   return 0;
  case type_t::_VOID:   return 0;
  case type_t::_BOOL:   return 1;
  case type_t::_INT:    return 4;
  case type_t::_DBL:    return 8;
  case type_t::_STR:    return -1;
  case type_t::_INT64:  return 8;
  case type_t::_SIZE:   return size;
  }

  return 0;
}

static union {
  bool        b_;
  int         i_;
  int64_t     i64_;
  double      d_;
  const char* s_;
} v;

////////////////////////////////////////////////////////////////////////////////
nf::result_t __stdcall callback(nf::mid_t _m, size_t len, const void *data) {
  FUSION_ASSERT(type == type_t::_STR || get_type_size(type) == len, "len=%d\n", len);

  const nf::mcb_t* mcb = pclient->get_mcb();

  FUSION_ASSERT(mcb);

  if (_m == m) {
    if (show_frq) {
      static rate_t freq(20); // frequency over last 20 seconds
      freq.increment();
      ::fprintf(stdout,"frq=%.2fHz ", freq.rate());
    }

    if (show_src)
      ::fprintf(stdout, "src=%0.4x ", mcb->src_);

    if (show_dst)
      ::fprintf(stdout, "dst=%0.4x ", mcb->dst_);

    if (show_seq)
      ::fprintf(stdout, "seq=%0.8x ", mcb->seq_);

    if (show_org)
      ::fprintf(stdout, "org=%lld ", mcb->org_);

    switch (type) {
    case type_t::_VOID:
      ::fprintf(stdout, "*\n");
      break;

    case type_t::_BOOL:
      ::fprintf(stdout, "%s\n", *(bool*)data ? "true" : "false");
      break;

    case type_t::_INT:
      ::fprintf(stdout, "%d\n", *(int*)data);
      break;

    case type_t::_DBL:
      ::fprintf(stdout, "%g\n", *(double*)data);
      break;

    case type_t::_STR:
      ::fprintf(stdout, "%.*s\n", len, (const char*)data);
      break;

    case type_t::_INT64:
      ::fprintf(stdout, "%lld\n", *(int64_t*)data);
      break;

    case type_t::_SIZE:
      ::fprintf(stdout, "[");

      for (int i = 0; i < size; ++i)
        ::fprintf(stdout, "%s%d", i ? ", " : "", *((unsigned char*)data + i));

      ::fprintf(stdout, "]\n");

      break;

    default:
      ::fprintf(stdout, "m=%d len=%d\n", m, len);
    }
  }

  return nf::ERR_OK;
}

////////////////////////////////////////////////////////////////////////////////
BOOL WINAPI on_ctrl_break(DWORD CtrlType) {
  stop = true;

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

struct less {
  bool operator()(const char *s1, const char *s2) const {
    return ::stricmp(s1, s2) < 0;
  }
};

////////////////////////////////////////////////////////////////////////////////
void help(const char* prog) {
  ::fprintf(stderr, "Help: %s\n", prog);
  ::fprintf(stderr, "\
  [--host=HOST] [--port=PORT] --profile=PROFILE MODE\n\
  MODE: create, delete, link, read, or write.\n\
    create: create new tag, or persistent tag with initial value\n\
    syntax: [--mtype=(data|event|stram)] (--create=TAG|--create-persist=TAG)\n\
      if creating persistent tag, then you must provide inital value using either: --string=..., --int[64]=..., --double=..., or --bool=1|0\n\
\n\
    delete: delete existing tag\n\
      syntax: --delete=TAG\n\
\n\
    link: create a link to existing tag\n\
      syntax: --link=NEW-TAG --target=EXISTING-TAG\n\
\n\
    list: list available tags\n\
      syntax: --link[=profile]\n\
\n\
    read: display/spy on values published for given tag\n\
      sytax: (--bool|--int|--double|--string) --read --msg=TAG\n\
      optionally you may want to add:\n\
      --show-src - to show message source client id;\n\
      --show-dst - to show message destination client (pseudo) id;\n\
      --show-org - to show message origination time stamp;\n\
      --show-seq - to show message sequence number;\n\
      --show-frq - to show message frequency;\n\
      press ^C to exit\n\
\n\
    write: publish value(s) for given tag\n\
      syntax:(--bool|--int|--double|--string) --write --msg=TAG\n\
      then follow prompt, enter empty value or press ^C to exit\n\
\n\
\n\
  TAG-NAME can be prefixed with profile name: PROFILE:NAME\n\
  if profile portion is missing, then default profile is assumed (the one provided with --profile=...)\n\
\n\
  default host 127.0.0.1\n\
  default port 3001\n\
");
}

////////////////////////////////////////////////////////////////////////////////
void write(const char* buff) {
  switch (type) {
  case type_t::_VOID:
    pclient->publish(m, 0, 0);
    break;

  case type_t::_BOOL: {
      if (!::stricmp(buff, "1")||!::stricmp(buff, "T")||!::stricmp(buff, "TRUE") ||!::stricmp(buff, "Y")||!::stricmp(buff, "YES")||
          !::stricmp(buff, "0")||!::stricmp(buff, "F")||!::stricmp(buff, "FALSE")||!::stricmp(buff, "N")||!::stricmp(buff, "NO")) {

        bool v = !::stricmp(buff, "1")||!::stricmp(buff, "T")||!::stricmp(buff, "TRUE")||!::stricmp(buff, "Y")||!::stricmp(buff, "YES");
        e = pclient->publish(m, sizeof v, &v);
      }
      else
        ::fprintf(stdout, "bad format\n");
    }
    break;

  case type_t::_INT: {
      int v = ::atoi(buff);

      e = pclient->publish(m, sizeof v, &v);
    }
    break;

  case type_t::_INT64: {
      int64_t v;

      ::sscanf(buff, "%lld", &v);
      e = pclient->publish(m, sizeof v, &v);
    }
    break;

  case type_t::_DBL: {
      double v = ::atof(buff);
      e = pclient->publish(m, sizeof v, &v);
    }
    break;

  case type_t::_STR:
    e = pclient->publish(m, ::strlen(buff), buff);
    break;

  case type_t::_SIZE:
  default:
    e = nf::ERR_IMPLEMENTED;
    FUSION_INFO("do not know how to write this type=%d", type);
  }

  if (e != nf::ERR_OK)
    FUSION_ERROR("pclient->publish=%s", nf::result_to_str(e));
}

////////////////////////////////////////////////////////////////////////////////
int main(int argc, const char** argv) {
  const char* port    = "3001";
  const char* host    = "127.0.0.1";
  const char* profile = 0;
  const char* msg     = 0;
  const char* link    = 0;
  const char* list    = 0;
  const char* mask    = 0;
  mode_t      mode = mode_t::_NONE;
  size_t      repeat_nr = 0;
  size_t      period = 1000;
  std::set<const char*, less> profiles;
  nf::mtype_t mtype = nf::MT_EVENT;

	for (int i = 1; i < argc; ++i) {
    const char* t;

    if (     !::strncmp(argv[i], "--port=", 7))
      port = argv[i] + 7;
    else if (!::strncmp(argv[i], "--host=", 7))
      host = argv[i] + 7;
    else if (!::strncmp(argv[i], "--profile=", 10))
			profile = argv[i] + 10;
    else if (!::strncmp(argv[i], "--msg=", 6)) {
      msg = argv[i] + 6;
      t = parse_msg(msg);
      if (t) profiles.insert(t);
    }
    else if (!::strncmp(argv[i], "--message=", 10)) {
      msg = argv[i] + 10;
      t = parse_msg(msg);
      if (t) profiles.insert(t);
    }
    else if (!::strcmp(argv[i], "--mtype=data"))
      mtype = nf::MT_DATA;
    else if (!::strcmp(argv[i], "--mtype=event"))
      mtype = nf::MT_EVENT;
    else if (!::strcmp(argv[i], "--mtype=stream"))
      mtype = nf::MT_STREAM;
    else if (!::strcmp(argv[i], "--void"))
      type = type_t::_VOID;
    else if (!::strncmp(argv[i], "--string=", 9)) {
      if (mode == mode_t::_NONE || mode == mode_t::_CREATE)
        mode = mode_t::_CREATE_PERSISTENT;

      type = type_t::_STR;
      v.s_ = argv[i] + 9;
    }
    else if (!::strcmp(argv[i], "--string"))
      type = type_t::_STR;
    else if (!::strncmp(argv[i], "--double=", 9)) {
      if (mode == mode_t::_NONE || mode == mode_t::_CREATE)
        mode = mode_t::_CREATE_PERSISTENT;

      type = type_t::_DBL;
      v.d_ = ::atof(argv[i] + 9);
    }
    else if (!::strcmp(argv[i], "--double"))
      type = type_t::_DBL;
    else if (!::strncmp(argv[i], "--bool=", 7)) {
      if (mode == mode_t::_NONE || mode == mode_t::_CREATE)
        mode = mode_t::_CREATE_PERSISTENT;

      type = type_t::_BOOL;
      const char* buff = argv[i] + 7;

      FUSION_ASSERT(!::stricmp(buff, "1")||!::stricmp(buff, "T")||!::stricmp(buff, "TRUE") ||!::stricmp(buff, "Y")||!::stricmp(buff, "YES")||
          !::stricmp(buff, "0")||!::stricmp(buff, "F")||!::stricmp(buff, "FALSE")||!::stricmp(buff, "N")||!::stricmp(buff, "NO"));

      bool b = !::stricmp(buff, "1")||!::stricmp(buff, "T")||!::stricmp(buff, "TRUE")||!::stricmp(buff, "Y")||!::stricmp(buff, "YES");

      v.b_ = b;
    }
    else if (!::strcmp(argv[i], "--bool"))
      type = type_t::_BOOL;
    else if (!::strncmp(argv[i], "--int=", 6)) {
      if (mode == mode_t::_NONE || mode == mode_t::_CREATE)
        mode = mode_t::_CREATE_PERSISTENT;

      type = type_t::_INT;
      v.i_ = ::atoi(argv[i] + 9);
    }
    else if (!::strcmp(argv[i], "--int"))
      type = type_t::_INT;
    else if (!::strncmp(argv[i], "--int64=", 8)) {
      if (mode == mode_t::_NONE || mode == mode_t::_CREATE)
        mode = mode_t::_CREATE_PERSISTENT;

      type = type_t::_INT64;
      ::sscanf(argv[i] + 8, "%lld", &v.i64_);
    }
    else if (!::strcmp(argv[i], "--int64"))
      type = type_t::_INT64;
    else if (!::strncmp(argv[i], "--size=", 7)) {
      type = type_t::_SIZE;
      size = ::atoi(argv[i] + 7);
    }
    else if (!::strcmp(argv[i], "--read"))
      mode = mode_t::_READ;
    else if (!::strncmp(argv[i], "--write=", 8)) {
      mode = mode_t::_WRITE;
      val = argv[i] + 8;
    }
    else if (!::strcmp(argv[i], "--write"))
      mode = mode_t::_WRITE;
    else if (!::strncmp(argv[i], "--period=", 9))
      period = ::atoi(argv[i] + 9);
    else if (!::strncmp(argv[i], "--repeat=", 9))
      repeat_nr = ::atoi(argv[i] + 9);
    else if (!::strncmp(argv[i], "--create-persist=", 17)) {
      mode = mode_t::_CREATE_PERSISTENT;
      msg = argv[i] + 17;
      t = parse_msg(msg);
      if (t) profiles.insert(t);
    }
    else if (!::strncmp(argv[i], "--create-persistent=", 20)) {
      mode = mode_t::_CREATE_PERSISTENT;
      msg = argv[i] + 20;
      t = parse_msg(msg);
      if (t) profiles.insert(t);
    }
    else if (!::strncmp(argv[i], "--create-persist", 16))
      mode = mode_t::_CREATE_PERSISTENT;
    else if (!::strncmp(argv[i], "--create-persistent", 19))
      mode = mode_t::_CREATE_PERSISTENT;
    else if (!::strncmp(argv[i], "--create=", 9)) {
      mode = mode_t::_CREATE_PERSISTENT;
      msg = argv[i] + 9;
      t = parse_msg(msg);
      if (t) profiles.insert(t);
    }
    else if (!::strncmp(argv[i], "--create", 8)) {
      if (mode != mode_t::_CREATE_PERSISTENT)
        mode = mode_t::_CREATE;
    }
    else if (!::strncmp(argv[i], "--delete=", 9)) {
      mode = mode_t::_DELETE;
      msg = argv[i] + 9;
      t = parse_msg(msg);
      if (t) profiles.insert(t);
    }
    else if (!::strncmp(argv[i], "--delete", 8))
      mode = mode_t::_DELETE;
    else if (!::strncmp(argv[i], "--link=", 7)) {
      mode = mode_t::_LINK;
      link = argv[i] + 7;
      t = parse_msg(link);
      if (t) profiles.insert(t);
    }
    else if (!::strncmp(argv[i], "--target=", 9)) {
      msg = argv[i] + 9;
      t = parse_msg(msg);
      if (t) profiles.insert(t);
    }
    else if (!::strcmp(argv[i],  "--list"))
      mode = mode_t::_LIST;
    else if (!::strncmp(argv[i], "--list=", 7)) {
      const char* p = argv[i] + 7;

      list = 0;
      mask = 0;

      if (char* sep = (char*)::strchr(p, ':')) {
        *sep = 0;

        if (sep != p)
          profiles.insert(list = p);

        if (sep[1])
          mask = sep + 1;
      }
      else
        mask = p;

      mode = mode_t::_LIST;
    }
    else if (!::strcmp(argv[i],  "--show-src"))
      show_src = true;
    else if (!::strcmp(argv[i],  "--show-dst"))
      show_dst = true;
    else if (!::strcmp(argv[i],  "--show-seq"))
      show_seq = true;
    else if (!::strcmp(argv[i],  "--show-org"))
      show_org = true;
    else if (!::strcmp(argv[i],  "--show-frq"))
      show_frq = true;
    else if (!::strncmp(argv[i], "--name=", 7))
      name = argv[i + 7];
    else if (!::strcmp(argv[i],  "--help")) {
      help(argv[0]);

      return 1;
    }
    else {
      ::fprintf(stderr, "Unknown argument=%s\n", argv[i]);
      help(argv[0]);

      return 1;
    }
  }

  FUSION_VERIFY(profile);
  FUSION_VERIFY(mode == mode_t::_LIST || msg);
  FUSION_VERIFY(mode != mode_t::_NONE);
  FUSION_VERIFY(mode == mode_t::_LINK || mode == mode_t::_DELETE || mode == mode_t::_LIST || type != type_t::_NONE);

  FUSION_DEBUG("host=%s port=%s profile=%s msg=%s mode=%d", host, port, profile, msg, mode);

  nf::client_t client(name);
  pclient = &client;

  {
    char connect[250];

    _snprintf(connect, sizeof connect - 1, "type=tcp host=%s port=%s", host, port);

    std::vector<const char*> pv;

    pv.push_back(profile);
    pv.insert(pv.end(), profiles.cbegin(), profiles.cend());

    FUSION_ENSURE(nf::ERR_OK == (e = client.reg(connect, pv.size(), &pv[0])), FUSION_FATAL("client.reg=%s", nf::result_to_str(e)));
  }

	nf::mtype_t mt = (mode == mode_t::_CREATE_PERSISTENT) ? nf::MT_EVENT|nf::MT_PERSISTENT : mtype;
  size_t      sz = get_type_size(type);

  switch (mode) {
  case mode_t::_READ:
    MOPEN(msg, O_RDONLY, mt, m, sz)
    break;

  case mode_t::_WRITE:
    MOPEN(msg, O_WRONLY, mt, m, sz)
    FUSION_INFO("m=%d\n", m);
    break;

  case mode_t::_CREATE:
    MCREATE(msg, O_RDONLY, mt, m, sz)
    FUSION_INFO("m=%d\n", m);
    break;

  case mode_t::_CREATE_PERSISTENT:
    if (type == type_t::_STR)
      MCREATEP(msg, O_RDONLY, mt, m, sz, ::strlen(v.s_) + 1, (void*)v.s_)
    else
      MCREATEP(msg, O_RDONLY, mt, m, sz, sz, &v)

    FUSION_INFO("m=%d\n", m);

    break;

  case mode_t::_DELETE:
    MDELETE(msg);

    break;

  case mode_t::_LINK:
    e = client.mlink(link, msg);

    if (e != nf::ERR_OK)
      FUSION_INFO("client.mlink=%s", nf::result_to_str(e));

    break;

  case mode_t::_LIST:
    {
      size_t nr;
      char** names;

      e = client.mlist(list, mask, nr, names);

      if (e != nf::ERR_OK)
        FUSION_INFO("client.mlink=%s", nf::result_to_str(e));
      else {
        for (size_t i = 0; i < nr; ++i)
          ::fprintf(stdout, "%s\n", names[i]);

        ::free(names);
      }
    }

    break;
  }

  ::SetConsoleCtrlHandler(on_ctrl_break, TRUE);

  if (mode == mode_t::_READ) {
    SUBSCRIBE(m, nf::SF_PUBLISH, callback);

    while (!stop && client.dispatch(period, true) == nf::ERR_OK) {
      /****/;
    }
  }
  else if (mode == mode_t::_WRITE) {
    if (val)
      write(val);
    else {
      ::fprintf(stdout, "Enter empty string to quit.\n");

      while (!stop && client.dispatch(period, true) == nf::ERR_OK) {
        char buff[4096];

        ::fprintf(stdout, ">");
        gets(buff);

        size_t len = ::strlen(buff);

        if (len == 0) {
          ::fprintf(stdout, " quitting...\n");

          break;
        }

        write(buff);
      }
    }
  }

  e = client.unreg();

  if (e != nf::ERR_OK) {
    FUSION_INFO("client.unreg=%s", nf::result_to_str(e));

    return 1;
  }

  return 0;
}

