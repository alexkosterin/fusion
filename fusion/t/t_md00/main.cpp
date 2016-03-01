/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

/*
 *  Test for md library
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/nf_macros.h"
#include "include/md.h"

int main(int argc, char** argv) {
  nf::cid_t c = 1;
  nf::uid_t u = 1000;
  nf::gid_t g = 1000;
  nf::result_t e;
  nf::mid_t m;

  const char* cn	= *argv;
  const char* mn	= "t0";
  const char* mn1 = "t1";
  const char* mn2 = "t2";
  const char* mn3 = "<*invalid name*>";

  using namespace nf;
  nf::mtype_t type;
  size_t size = -1;

  e = md_open(c, mn, O_RDWR, type, m, size);
  printf("close(%d, %d)=>%d\n", c, m, e);

  FUSION_ASSERT(e == nf::ERR_REGISTERED);

  const char* dump =
    "name=SYSMSG-A { id=0 uid=0 gid=0 size=-1 type=0 access=4294967295 atime=0 ctime=0 mtime=0 }\n"
    "name=SYSMSG-B { id=1 uid=0 gid=0 size=-1 type=0 access=4294967295 atime=0 ctime=0 mtime=0 }\n"
    "name=SYSMSG-C { id=2 uid=0 gid=0 size=-1 type=0 access=4294967295 atime=0 ctime=0 mtime=0 }\n"
    "name=SYSMSG-D { id=3 uid=0 gid=0 size=-1 type=0 access=4294967295 atime=0 ctime=0 mtime=0 }\n"
    "name=SYSMSG-E { id=4 uid=0 gid=0 size=-1 type=0 access=4294967295 atime=0 ctime=0 mtime=0 }";

  e = md_load(::strlen(dump), dump, "\n");
  printf("load(%d, dump=>%d\n", ::strlen(dump), e);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = md_reg_client(c, cn, 0, 0);
  printf("reg_client(%d, \"%s\", %d, %d)=>%d\n", c, cn, u, g, e);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = md_open(c, mn, O_RDWR, type, m, size);
  printf("open(%d, \"%s\", O_RDWR, MT_EVENT|MT_USER, %d)=>%d\n", c, mn, m, e);
  FUSION_ASSERT(e == nf::ERR_MESSAGE);

  e = md_open(c, mn3, O_RDWR|O_CREATE, type, m, size);
  printf("open(%d, \"%s\", O_RDWR|O_CREATE, MT_EVENT|MT_USER, %d)=>%d\n", c, mn3, m, e);
  FUSION_ASSERT(e == nf::ERR_MESSAGE);

  e = md_open(c, mn, O_RDWR|O_CREATE, type, m, size);
  printf("open(%d, \"%s\", O_RDWR|O_CREATE, MT_EVENT|MT_USER, %d)=>%d\n", c, mn, m, e);
  FUSION_ASSERT(e == nf::ERR_OK);

  if (e == nf::ERR_OK) {
    e = md_close(c, m);
    printf("close(%d, %d)=>%d\n", c, m, e);
  }

  e = md_ln(c, mn1, mn2);
  printf("ln(%d, \"%s\", \"%s\")=>%d\n", c, mn1, mn2, e);
  FUSION_ASSERT(e == nf::ERR_MESSAGE);

  e = md_ln(c, mn1, mn);
  printf("ln(%d, \"%s\", \"%s\")=>%d\n", c, mn1, mn, e);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = md_mv(c, mn1, mn2);
  printf("mv(%d, \"%s\", \"%s\")=>%d\n", c, mn1, mn2, e);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = md_open(c, mn, O_RDWR, type, m, size);
  printf("open(%d, \"%s\", O_RDWR, MT_EVENT|MT_USER, %d)=>%d\n", c, mn, m, e);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = md_open(c, mn2, O_RDWR, type, m, size);
  printf("open(%d, \"%s\", O_RDWR, MT_EVENT|MT_USER, %d)=>%d\n", c, mn, m, e);
  FUSION_ASSERT(e == nf::ERR_OPEN);

  e = md_mv(c, mn1, mn2);
  printf("mv(%d, \"%s\", \"%s\")=>%d\n", c, mn1, mn2, e);
  FUSION_ASSERT(e == nf::ERR_MESSAGE);

  size_t len = 0;
  char buf[10000];

  e = md_query(c, "t", len, buf, "\n");
  printf("query(%d, \"t\", %d, buf, \"\\n\")=>%d\n", c, len, e);
  if (e == nf::ERR_OK) printf("buff=%s\n", buf);
  FUSION_ASSERT(e == nf::ERR_OK);

  len = 2;
  e = md_query(c, "t", len, buf, ";");
  printf("query(%d, \"t\", %d, buf, \"\\n\")=>%d\n", c, len, e);
  if (e == nf::ERR_TRUNCATED) printf("buff={%s}\n", buf);
  FUSION_ASSERT(e == nf::ERR_TRUNCATED);

  len = 3;
  e = md_query(c, "t", len, buf, ";");
  printf("query(%d, \"t\", %d, buf, \"\\n\")=>%d\n", c, len, e);
  if (e == nf::ERR_TRUNCATED) printf("buff={%s}\n", buf);
  FUSION_ASSERT(e == nf::ERR_TRUNCATED);

  len = 4;
  e = md_query(c, "t", len, buf, ";");
  printf("query(%d, \"t\", %d, buf, \"\\n\")=>%d\n", c, len, e);
  if (e == nf::ERR_TRUNCATED) printf("buff={%s}\n", buf);
  FUSION_ASSERT(e == nf::ERR_TRUNCATED);

  len = sizeof(buf) - 1;
  e = md_query(c, "t", len, buf, ";");
  printf("query(%d, \"t\", %d, buf, \"\\n\")=>%d\n", c, len, e);
  if (e == nf::ERR_OK) printf("buff={%s}\n", buf);
  FUSION_ASSERT(e == nf::ERR_OK);

  len = sizeof(buf) - 1;
  e = md_query(c, "0", len, buf, ";");
  printf("query(%d, \"0\", %d, buf, \"\\n\")=>%d\n", c, len, e);
  if (e == nf::ERR_OK) printf("buff={%s}\n", buf);
  FUSION_ASSERT(e == nf::ERR_OK);

  len = 20;
  e = md_save(len, buf, "\n");
  printf("save(%d, %d, buf)=>%d\n", c, len, e);
  if (e == nf::ERR_OK) printf("buff={%s}\n", buf);
  FUSION_ASSERT(e == nf::ERR_TRUNCATED);

  len = sizeof(buf) - 1;
  e = md_save(len, buf, "\n");
  printf("save(%d, %d, buf)=>%d\n", c, len, e);
  if (e == nf::ERR_OK) printf("buff={%s}\n", buf);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = md_unlink(c, mn);
  printf("unlink(%d, \"%s\")=>%d\n", c, mn, e);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = md_unlink(c, mn1);
  printf("unlink(%d, \"%s\")=>%d\n", c, mn1, e);
  FUSION_ASSERT(e == nf::ERR_MESSAGE);

  stat_t sd;

  e = md_stat(c, mn2, sd);
  printf("stat(%d, %s, sd)=>%d\n", c, mn2, e);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = md_unlink(c, mn2);
  printf("unlink(%d, \"%s\")=>%d\n", c, mn2, e);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = md_close(c, m);
  printf("close(%d, %d)=>%d\n", c, m, e);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = md_stat(c, mn, sd);
  printf("stat(%d, %s, sd)=>%d\n", c, mn, e);
  FUSION_ASSERT(e == nf::ERR_MESSAGE);

  e = md_unreg_client(c);
  printf("unreg_client(%d)=>%d\n", c, e);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = md_close(c, m);
  printf("close(%d, %d)=>%d\n", c, m, e);
  FUSION_ASSERT(e == nf::ERR_REGISTERED);

  return 0;
}

