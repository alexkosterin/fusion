/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "include/nf.h"	    // FUSION
#include "include/mb.h"	    // FUSION MESSAGE BUS
#include "include/nf_macros.h"
#include "include/nf_mcb.h"	// FUSION MCB
#include "include/tsc.h"

#include "logger.pb.h"

#include <Rpc.h>
#pragma comment(lib, "Rpcrt4.lib")

#include "driver.h"
#include "utils.h"

#include <map>

#define FORA_MSECS  (3600 * 1000)

enum vtype_t {
  VT_DATA_BOTH,   // self contained arvive
  VT_DATA_ONLY,   // partial arvive, data only
  VT_VDATA_ONLY,  // partial arvive, vdata only
};

#pragma pack(push, 1)

struct header_t {
  char        signature_[8];  // 'NFLOG MY'
  nf::msecs_t ts_org_;        // volume time stamp origin
  int64_t     data_offset_;
  int64_t     vdata_offset_;  // if we have vdata

  struct {
    bool    pos16_:1;         // using char or short for tag index field (tags < 256?)
    bool    have_tsc_:1;      // storing timestamp
    bool    have_src_:1;      // storing sender cli
    bool    have_dst_:1;      // storing receiver cli (direct or broadcast)
    bool    have_seq_:1;      // storing sender sequence
    bool    have_val_:1;      // storing value
  } schema;

  struct {
    bool    finalized_: 1;    // was closed cleanly
    bool    empty_:     1;    // any data captured
  } flags;

  vtype_t   vol_type_;
  int       vol_nr_;          // 0, 1, 2...
  UUID      guid_;
};

union data_t {
  bool      bool_;
  short     short_;
  int       int_;
  double    double_;

  union {
    int64_t offset_;

    struct {
      size_t          __unused_offset_lo__;
      unsigned short  __unused_offset_hi__;
      unsigned short  len_;
    } v;
  };
};

struct less {
  bool operator()(const char* a, const char* b) {
    return ::strcmp(a, b) < 0;
  }
};

typedef std::map<const char*, int, less> str2int_t;

static header_t hdr = {
  { 'N', 'F', 'L', 'O', 'G', 'O', 'N', 'E', },
  0, 0, 0,
  { false, },
  { false, true },
};

#pragma pack(pop)

static FILE   *fd__             = 0;
static FILE   *vfd__            = 0;

static int64_t vpos__           = 0;  // vdata position
static int64_t vol_size__       = 0;
static int64_t vol_msg_nr__     = 0;
static int     vol_nr__         =-1;  // current volume number

static const msgs_t* msgs__     = 0;

static char* vol_name__         = 0;
static char* vol_vname__        = 0;
static bool have_vdata__        = false;

static size_t blob_len__        = 0;
static void* blob__             = 0;

#define CHECK_WRITE_ERROR(RC, FD)           \
  if (RC != 1) {                            \
    FUSION_ERROR("fwrite=%d", ::ferror(FD));\
                                            \
    return false;                           \
  }

#define CHECK_READ_ERROR(RC, FD)            \
  if (RC != 1) {                            \
    FUSION_ERROR("fread=%d", ::ferror(FD)); \
                                            \
    return false;                           \
  }

////////////////////////////////////////////////////////////////////////////////
struct vcache_t : public std::map<std::string, const data_t> {
  bool find(const void* v, size_t sz, data_t& d) {
    const std::string s((const char*)v, sz);
    auto P = map::insert(std::make_pair(s, d));
    bool found = !P.second;

    if (found)
      d = P.first->second;

    return found;
  }
};

////////////////////////////////////////////////////////////////////////////////
struct driver_v1_t : public driver_t {
  // fusion capture
  virtual bool init_capture(const msgs_t& msgs, const char* params, const header&);
  virtual bool cb(size_t pos, const nf::mcb_t*);
  virtual bool fini_capture();

  virtual bool init_volume(const char* dir, const char* tempate_name);
  virtual bool fini_volume();
  virtual bool term_volume();

  virtual const char* vol_name()  { return vol_name__; }
  virtual size_t vol_nr()         { return vol_nr__; }
  virtual int64_t vol_msg_nr()    { return vol_msg_nr__; }
  virtual int64_t vol_size()      { return vol_size__; }

  // dump
  virtual bool dump(const char* name);
  virtual bool dump_header(const char* name);
  virtual bool dump_csv(const char* name);

  // playback
  // ...

  vcache_t vcache_;
};

////////////////////////////////////////////////////////////////////////////////
bool driver_v1_t::init_capture(const msgs_t& msgs, const char* params, const header& h) {
  FUSION_ASSERT(!fd__);
  FUSION_ASSERT(!vfd__);

  hdr.schema.pos16_ = msgs.size() > 0xFF;

  msgs__ = &msgs;

  for (size_t i = 0; i < msgs.size(); ++i)
    if (msgs[i].size_ == -1) {
      have_vdata__ = true;

      break;
    }

  blob_len__  = h.ByteSize();
  blob__      = ::malloc(blob_len__);
  FUSION_VERIFY(h.SerializeToArray(blob__, blob_len__));

  //hdr.signature_[8];  // 'NFLOG MY'
  //hdr.ts_org_
  //hdr.data_offset_
  //hdr.vdata_offset_
  //hdr.schema
  const char* p;
  int         v;

  hdr.schema.have_tsc_ = true;
  hdr.schema.have_src_ = true;
  hdr.schema.have_dst_ = false;
  hdr.schema.have_seq_ = false;
  hdr.schema.have_val_ = true;

  if (p = ::strstr(params, "keep_tsc"))
    if (1 == ::sscanf(p, "keep_tsc = %d", &v))
      hdr.schema.have_tsc_ = (bool)v;

  if (p = ::strstr(params, "keep_src"))
    if (1 == ::sscanf(p, "keep_src = %d", &v))
      hdr.schema.have_src_ = (bool)v;

  if (p = ::strstr(params, "keep_dst"))
    if (1 == ::sscanf(p, "keep_dst = %d", &v))
      hdr.schema.have_dst_ = (bool)v;

  if (p = ::strstr(params, "keep_seq"))
    if (1 == ::sscanf(p, "keep_seq = %d", &v))
      hdr.schema.have_seq_ = (bool)v;

  if (p = ::strstr(params, "keep_val"))
    if (1 == ::sscanf(p, "keep_val = %d", &v))
      hdr.schema.have_val_ = (bool)v;

  //hdr.flags.empty_
  //hdr.flags.finalized_
  //hdr.vol_type_
  //hdr.vol_nr_
  //hdr.guid_

  char g[64];

  if (!(p = ::strstr(params, "guid")) || (1 != ::sscanf(p, "guid = { %64s }", g)) || (!str2guid(g, hdr.guid_))) {
    RPC_STATUS rs = ::UuidCreate(&hdr.guid_);

    FUSION_ENSURE(rs == RPC_S_OK, return false);
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
bool driver_v1_t::fini_capture() {
  ::free(blob__);

  ::free(vol_name__);
  ::free(vol_vname__);

  return false;
}

////////////////////////////////////////////////////////////////////////////////
bool driver_v1_t::init_volume(const char* dir, const char* tempate_name) {
  vpos__        = 0;
  vol_size__    = 0;
  vol_msg_nr__  = 0;

  ::free(vol_name__);
  ::free(vol_vname__);

  ++vol_nr__;

  char g[512];

  ::_snprintf(g, sizeof g, "%.8x-%.4x-%.4x-%.2x%.2x-%.2x%.2x%.2x%.2x%.2x%.2x",
    hdr.guid_.Data1, hdr.guid_.Data2, hdr.guid_.Data3,
    hdr.guid_.Data4[0], hdr.guid_.Data4[1], hdr.guid_.Data4[2], hdr.guid_.Data4[3],
    hdr.guid_.Data4[4], hdr.guid_.Data4[5], hdr.guid_.Data4[6], hdr.guid_.Data4[7]
  );

  char* tmp = expand_name(tempate_name ? tempate_name : "log-%N-{%G}.data", vol_nr__, g);

  if (!tmp) {
    FUSION_ERROR("invalid template name");

    return false;
  }

  sanitize_name(tmp);

  if (dir) {
    char* t = subst_dir(tmp, dir);

    ::free(tmp);
    tmp = t;
  }

  vol_name__ = subst_ext(tmp, "data");

  ::free(tmp);

  fd__ = ::fopen(vol_name__, "wb");

  FUSION_ENSURE(fd__, return false);

  if (have_vdata__) {
    vol_vname__ = subst_ext(vol_name__, "vdat");

    vfd__ = ::fopen(vol_vname__, "wb");

    FUSION_ENSURE(vfd__, { ::fclose(fd__); return false; });
  }

  //hdr.signature_[8];  // 'NFLOG MY'
  hdr.ts_org_           = nf::now_msecs() - FORA_MSECS;
  hdr.data_offset_      = sizeof hdr + blob_len__;
  hdr.vdata_offset_     = 0;
  //hdr.schema
  hdr.flags.empty_      = true;
  hdr.flags.finalized_  = false;
  hdr.vol_type_         = have_vdata__ ? VT_DATA_ONLY : VT_DATA_BOTH ;
  hdr.vol_nr_           = vol_nr__;
  //hdr.guid_

  size_t rc;

  FUSION_ENSURE((rc = ::fwrite(&hdr,   sizeof hdr, 1, fd__)) == 1, { term_volume(); return false; });
  FUSION_ENSURE((rc = ::fwrite(blob__, blob_len__, 1, fd__)) == 1, { term_volume(); return false; });

  vcache_.clear();

  return true;
}

////////////////////////////////////////////////////////////////////////////////
bool driver_v1_t::fini_volume() {
  //hdr.signature_[8];  // 'NFLOG MY'
  //hdr.ts_org_
  //hdr.data_offset_
  hdr.vdata_offset_     = ::_ftelli64(fd__);
  //hdr.schema
  //hdr.flags.empty_
  hdr.flags.finalized_  = true;
  hdr.vol_type_         = VT_DATA_BOTH;
  //hdr.vol_nr_
  //hdr.guid_

  // append v...
  if (have_vdata__) {
    FUSION_ASSERT(vfd__);
    FUSION_ASSERT(vol_vname__);

    int rc = ::fclose(vfd__);

    FUSION_ASSERT(rc == 0);

    vfd__ = ::fopen(vol_vname__, "rb");

    FUSION_ASSERT(vfd__);

    while (vpos__--) { // @optimize@ - we do not use vdata, so...
      char c;
      size_t rc = ::fread(&c, 1, 1, vfd__);

      if (rc != 1) {
        FUSION_ERROR("fread=%d", ::GetLastError());

        hdr.flags.finalized_ = false;
        hdr.vol_type_        = VT_DATA_ONLY;

        goto done;
      }

      rc = ::fwrite(&c, 1, 1, fd__);

      if (rc != 1) {
        FUSION_ERROR("fread=%d", ::GetLastError());

        hdr.flags.finalized_ = false;
        hdr.vol_type_        = VT_DATA_ONLY;

        goto done;
      }
    }

    FUSION_ASSERT(::_ftelli64(fd__) == vol_size__);

done:
    if (vfd__) {
      ::fclose(vfd__);
      ::unlink(vol_vname__);
    }
  }

  ::fseek(fd__, 0L, SEEK_SET);

  FUSION_VERIFY(::fwrite(&hdr, sizeof hdr, 1, fd__) == 1);

  if (fd__)
    ::fclose(fd__);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
bool driver_v1_t::term_volume() {
  if (fd__)
    ::fclose(fd__);

  if (vfd__)
    ::fclose(vfd__);

  ::unlink(vol_name__);
  ::unlink(vol_vname__);

  ::free(vol_name__);
  ::free(vol_vname__);

  return false;
}

////////////////////////////////////////////////////////////////////////////////
bool driver_v1_t::cb(size_t pos, const nf::mcb_t* mcb) {
  FUSION_ENSURE(mcb, return false);
  //@@FUSION_ASSERT(pos < 6);

  size_t rc = ::fwrite(&pos, hdr.schema.pos16_ ? sizeof(short) : sizeof(char), 1, fd__);

  CHECK_WRITE_ERROR(rc, fd__);

  hdr.flags.empty_ = false;

  if (hdr.schema.have_tsc_) {
    int64_t d = mcb->org_ - hdr.ts_org_;

    FUSION_ASSERT(d > 0 && d < 0xFFFFFFFF);

    unsigned int ud = (unsigned int)d;

    rc = ::fwrite(&ud, sizeof ud, 1, fd__);

    CHECK_WRITE_ERROR(rc, fd__);
  }

  if (hdr.schema.have_src_) {
    rc = ::fwrite(&mcb->src_, sizeof mcb->src_, 1, fd__);

    CHECK_WRITE_ERROR(rc, fd__);
  }

  if (hdr.schema.have_dst_) {
    rc = ::fwrite(&mcb->dst_, sizeof mcb->dst_, 1, fd__);

    CHECK_WRITE_ERROR(rc, fd__);
  }

  if (hdr.schema.have_seq_) {
    rc = ::fwrite(&mcb->seq_, sizeof mcb->seq_, 1, fd__);

    CHECK_WRITE_ERROR(rc, fd__);
  }

  if (hdr.schema.have_val_) {
    size_t written;
    data_t d;
    size_t size = (*msgs__)[pos].size_;

    FUSION_ASSERT(size == -1 || size == mcb->len_);

    // FIXED
    if (size != -1 && size <= sizeof(d)) {
      if (mcb->len_) {
        rc = ::fwrite(mcb->data(), mcb->len_, 1, fd__);

        CHECK_WRITE_ERROR(rc, fd__);
      }

      written = mcb->len_;
    }
    // VDATA
    else {
      d.offset_ = vpos__;
      d.v.len_  = mcb->len_;

      bool cached = vcache_.find(mcb->data(), mcb->len_, d);

      rc = ::fwrite(&d, sizeof(d), 1, fd__);

      CHECK_WRITE_ERROR(rc, fd__);

      if (!cached) {
        rc = ::fwrite(mcb->data(), mcb->len_, 1, vfd__);

        CHECK_WRITE_ERROR(rc, vfd__);

        vpos__ += mcb->len_;
      }

      written = sizeof(d);
    }

    if (written < sizeof(d)) {
      static const data_t padding__ = { 0 };

      rc = ::fwrite(&padding__, sizeof(d) - written, 1,  fd__);

      CHECK_WRITE_ERROR(rc, fd__);
    }
  }

  ++vol_msg_nr__;

  vol_size__ = ::_ftelli64(fd__);

  if (vfd__)
    vol_size__ += ::_ftelli64(vfd__);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
bool driver_v1_t::dump(const char* name) {
  FILE* fd = ::fopen(name, "rb");

  FUSION_ASSERT(fd, "Cannot open %s", name);

  if (!fd)
    return false;

  header_t h;

  size_t rc = ::fread(&h, sizeof h, 1, fd);

  FUSION_ASSERT(rc == 1, "File size too small: %s", name);

  if (rc != 1) {
    ::fclose(fd);

    return false;
  }

  if (::memcmp(h.signature_, hdr.signature_, sizeof hdr.signature_)) {
    FUSION_ERROR("Wrong logger version %s", name);

    ::fclose(fd);

    return false;
  }

  header  sh;

  {
    size_t  len = (size_t)(h.data_offset_ - sizeof h);
    void*   buf = ::alloca(len);

    rc = ::fread(buf, len, 1, fd);

    FUSION_ASSERT(rc == 1, "Soft header seems corrupted: %s", name);
    FUSION_VERIFY(sh.ParseFromArray(buf, len), "Soft header seems corrupted: %s", name);
  }

  while (::_ftelli64(fd) < h.vdata_offset_) {
    size_t idx = 0;
    nf::cid_t src, dst;
    nf::seq_t seq;

    rc = ::fread(&idx, h.schema.pos16_ ? 2 : 1, 1, fd);

    CHECK_READ_ERROR(rc, fd);

    ::fprintf(stdout, "idx=%d", idx);

    if (h.schema.have_tsc_) {
      unsigned int d;

      rc = ::fread(&d, sizeof d, 1, fd);

      CHECK_READ_ERROR(rc, fd);

      int64_t ts = (int64_t)d + h.ts_org_;
      time_t t      = nf::msecs_to_unix(ts);
      tm* timeinfo  = ::gmtime(&t);
      char buff[128];

      ::strftime(buff, sizeof buff, "%Y-%m-%d %H:%M:%S UTC", timeinfo);
      ::fprintf(stdout, " ts=%s [%lld]", buff, ts);
    }

    if (h.schema.have_src_) {
      rc = ::fread(&src, sizeof src, 1, fd);

      CHECK_READ_ERROR(rc, fd);

      ::fprintf(stdout, " src=%0.4X", src);
    }

    if (h.schema.have_dst_) {
      rc = ::fread(&dst, sizeof dst, 1, fd);

      CHECK_READ_ERROR(rc, fd);

      ::fprintf(stdout, " dst=%0.4X", dst);
    }

    if (h.schema.have_seq_) {
      rc = ::fread(&seq, sizeof seq, 1, fd);

      CHECK_READ_ERROR(rc, fd);

      ::fprintf(stdout, " seq=%0.8d", seq);
    }

    if (h.schema.have_val_) {
      data_t data;

      rc = ::fread(&data, sizeof data, 1, fd);

      CHECK_READ_ERROR(rc, fd);

      if (sh.tags(idx).size() != -1 && sh.tags(idx).size() <= sizeof(data)) {
        if (sh.tags(idx).has_format()) {
          if (sh.tags(idx).has_format() && (::strcmp(sh.tags(idx).format().c_str(), "%s") == 0))
            ::fprintf(stdout, " \"%.*s\"", sizeof(data), data);
          else if (sh.tags(idx).has_format() && (::strcmp(sh.tags(idx).format().c_str(), "%g") == 0))
            ::fprintf(stdout, " %g", data.double_);
          else if (sh.tags(idx).has_format() && (::strcmp(sh.tags(idx).format().c_str(), "%d") == 0))
            ::fprintf(stdout, " %d", data.int_);
          else if (sh.tags(idx).has_format() && (::strcmp(sh.tags(idx).format().c_str(), "%x") == 0))
            ::fprintf(stdout, " %x", data.int_);
        }
        else
          switch (sh.tags(idx).size()) {
          case 0:
            break;

          case 1:
            ::fprintf(stdout, " %d", data.bool_);
            break;

          case 2:
            ::fprintf(stdout, " %d", data.short_);
            break;

          case 4:
            ::fprintf(stdout, " %d", data.int_);
            break;

          case 8:
            ::fprintf(stdout, " %g", data.double_);
            break;
          }
      }
      else {
        ::fprintf(stdout, " vdata: [offset=%llX len=%d]", 0x0000FFFFFFFFFFFFLL & data.offset_, data.v.len_);

        if (sh.tags(idx).has_format() && (::strcmp(sh.tags(idx).format().c_str(), "%s") == 0)) {
          char* buff  = (char*)::malloc(data.v.len_);
          __int64 pos = ::_ftelli64(fd);

          FUSION_ASSERT(pos != -1L);

          if (::_fseeki64(fd, (0x0000FFFFFFFFFFFFLL & data.offset_) + h.vdata_offset_, SEEK_SET) != 0)
            return false;

          rc = ::fread(buff, data.v.len_, 1, fd);

          CHECK_READ_ERROR(rc, fd);

          ::fprintf(stdout, " \"%.*s\"", data.v.len_, buff);

          if (::_fseeki64(fd, pos, SEEK_SET) != 0)
            return false;

          ::free(buff);
        }
      }

      ::fprintf(stdout, "\n");
    }
  }

  ::fclose(fd);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
bool driver_v1_t::dump_header(const char* name) {
  FILE* fd = ::fopen(name, "rb");

  FUSION_ASSERT(fd, "Cannot open %s", name);

  if (!fd)
    return false;

  header_t h;

  size_t rc = ::fread(&h, sizeof h, 1, fd);

  FUSION_ASSERT(rc == 1, "File size too small: %s", name);

  if (rc != 1) {
    ::fclose(fd);

    return false;
  }

  if (::memcmp(h.signature_, hdr.signature_, sizeof hdr.signature_)) {
    FUSION_ERROR("Wrong logger version %s", name);

    ::fclose(fd);

    return false;
  }

  ::fprintf(stdout, "time-stamp org: %lld\n", h.ts_org_ + FORA_MSECS);
  ::fprintf(stdout, "data offset:    %lld\n", h.data_offset_);
  ::fprintf(stdout, "vdata offset:   %lld\n", h.vdata_offset_);

  ::fprintf(stdout, "\nschema:\n");
  ::fprintf(stdout, "  pos16:        %d\n",   h.schema.pos16_);
  ::fprintf(stdout, "  have_tsc:     %d\n",   h.schema.have_tsc_);
  ::fprintf(stdout, "  have_src:     %d\n",   h.schema.have_src_);
  ::fprintf(stdout, "  have_dst:     %d\n",   h.schema.have_dst_);
  ::fprintf(stdout, "  have_seq:     %d\n",   h.schema.have_seq_);
  ::fprintf(stdout, "  have_val:     %d\n",   h.schema.have_val_);

  ::fprintf(stdout, "\nflags\n");
  ::fprintf(stdout, "  empty:        %d\n",   h.flags.empty_);
  ::fprintf(stdout, "  finalized:    %d\n",   h.flags.finalized_);

  ::fprintf(stdout, "vol type:       %s\n",   h.vol_type_ == VT_DATA_BOTH ? "VT_DATA_BOTH" : (h.vol_type_ == VT_DATA_ONLY ? "VT_DATA_ONLY" : "VT_VDATA_ONLY"));
  ::fprintf(stdout, "vol number:     %d\n",   h.vol_nr_);
  ::fprintf(stdout, "run guid:       %.8x-%.4x-%.4x-%.2x%.2x-%.2x%.2x%.2x%.2x%.2x%.2x\n",
    h.guid_.Data1, h.guid_.Data2, h.guid_.Data3,
    h.guid_.Data4[0], h.guid_.Data4[1], h.guid_.Data4[2], h.guid_.Data4[3],
    h.guid_.Data4[4], h.guid_.Data4[5], h.guid_.Data4[6], h.guid_.Data4[7]
  );

  size_t len = (size_t)(h.data_offset_ - sizeof h);
  header sh;

  FUSION_ASSERT(len < 4096, "Soft header seems corrupted: %s", name);

  void* buf = ::alloca(len);

  rc = ::fread(buf, len, 1, fd);

  FUSION_ASSERT(rc == 1, "Soft header seems corrupted: %s", name);

  FUSION_VERIFY(sh.ParseFromArray(buf, len), "Soft header seems corrupted: %s", name);

  ::fprintf(stdout, "\nsoft header:\n");

  if (sh.has_comment())
    ::fprintf(stdout, "  comment=%s\n", sh.comment().c_str());

  if (sh.has_cmdline())
    ::fprintf(stdout, "  command line=%s\n", sh.cmdline().c_str());

  if (sh.has_date())
    ::fprintf(stdout, "  date=%s\n", sh.date().c_str());

  if (sh.has_user())
    ::fprintf(stdout, "  user=%s\n", sh.user().c_str());

  if (sh.has_host())
    ::fprintf(stdout, "  host=%s\n", sh.host().c_str());

  if (sh.has_os())
    ::fprintf(stdout, "  os=%s\n", sh.os().c_str());

  ::fprintf(stdout, "\n  tags by index:\n");

  for (int i = 0; i < sh.tags_size(); ++i)
    ::fprintf(stdout, "    idx=%0.2d %s\tsize=%d%s%s\tmid=%0.4d\n",
      sh.tags(i).index(),
      sh.tags(i).name().c_str(),
      sh.tags(i).size(),
      sh.tags(i).has_format() ? "\tformat=" : "",
      sh.tags(i).has_format() ? sh.tags(i).format().c_str() : "",
      sh.tags(i).mid()
    );

  ::fprintf(stdout, "\n  tags by name:\n");

  str2int_t s2i;

  for (int i = 0; i < sh.tags_size(); ++i)
    s2i.insert(std::make_pair(sh.tags(i).name().c_str(), sh.tags(i).index()));

  for (str2int_t::const_iterator I = s2i.cbegin(), E = s2i.cend(); I != E; ++I)
    ::fprintf(stdout, "    %s\tidx=%0.2d\tsize=%d%s%s\tmid=%04d\n",
      sh.tags(I->second).name().c_str(),
      sh.tags(I->second).index(),
      sh.tags(I->second).size(),
      sh.tags(I->second).has_format() ? "\tformat=" : "",
      sh.tags(I->second).has_format() ? sh.tags(I->second).format().c_str() : "",
      sh.tags(I->second).mid()
    );

  ::fprintf(stdout, "\n  tags by mid:\n");

   std::multimap<int, int> i2i;

  for (int i = 0; i < sh.tags_size(); ++i)
    i2i.insert(std::make_pair(sh.tags(i).mid(), sh.tags(i).index()));

  for (std::map<int, int>::const_iterator I = i2i.cbegin(), E = i2i.cend(); I != E; ++I)
    ::fprintf(stdout, "    mid=%0.4d %s\tidx=%0.2d\tsize=%d%s%s\n",
      sh.tags(I->second).mid(),
      sh.tags(I->second).name().c_str(),
      sh.tags(I->second).index(),
      sh.tags(I->second).size(),
      sh.tags(I->second).has_format() ? "\tformat=" : "",
      sh.tags(I->second).has_format() ? sh.tags(I->second).format().c_str() : ""
    );

  ::fclose(fd);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
bool driver_v1_t::dump_csv(const char* name) {
  FUSION_ERROR("Not implemented");

  return false;
}

////////////////////////////////////////////////////////////////////////////////
driver_t* driver_t::create(const char* sig) {
  if (::strcmp(sig, "V1") == 0)
    return new driver_v1_t();

  return 0;
}

