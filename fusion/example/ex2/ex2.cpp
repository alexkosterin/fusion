/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/nf.h"	// FUSION
#include "include/nf_macros.h"
#include "include/mb.h"	// FUSION MESSAGE BUS

# define NOGDI
# define _WINSOCKAPI_
#include <windows.h>

nf::client_t client("ex2");
nf::result_t e;

size_t rec_nr = 0;
size_t snd_nr = 0;
int i;

// this callback will run in main thread; can block
nf::result_t __stdcall cb(nf::mid_t mid, size_t len, const void *data) {
  FUSION_ASSERT(len == 4);

  ::fprintf(stdout, "cb:\tdata=%d snd=%d rec=%d\n", *(int*)data, snd_nr, rec_nr);
  ++rec_nr;

//client.post(mid, nf::CID_PUB|nf::CID_NOSELF, 4, &i);

  return nf::ERR_OK;
}

// this callback will run in client worker thread; it must _not_ block
nf::result_t __stdcall mdcb(nf::callback_t, void*, nf::mid_t mid, size_t len, const void *data) {
  ::fprintf(stdout, "mdcb:\tsnd=%d rec=%d\n", snd_nr, rec_nr);
//++rec_nr;

  const nf::mcb_t* mcb = client.get_mcb();

  FUSION_ASSERT(mcb);

  return nf::ERR_OK;
}

int main(int argc, const char** argv)
{
  const char* port    = "3001";
  const char* host    = "127.0.0.1";
  const char* profile = "";
  const char* msg1    = "test";
  const char* msg2    = "best";
  char connect[250]   = {};
  size_t size, M = 1, N = 1000, delay = 1;

  for (int i = 1; i < argc; ++i)
    if (!::strncmp(argv[i], "--port=", 7))
      port = argv[i] + 7;
    else if (!::strncmp(argv[i], "--host=", 7))
      host = argv[i] + 7;
    else if (!::strncmp(argv[i], "--profile=", 10))
      profile = argv[i] + 10;
    else if (!::strncmp(argv[i], "--M=", 4))
      M = atoi(argv[i] + 4);
    else if (!::strncmp(argv[i], "--N=", 4))
      N = atoi(argv[i] + 4);
    else if (!::strncmp(argv[i], "--delay=", 8))
      delay = atoi(argv[i] + 8);
    else if (!::strncmp(argv[i], "--msg1=", 7))
      msg1 = argv[i] + 7;
    else if (!::strncmp(argv[i], "--msg2=", 7))
      msg2 = argv[i] + 7;
    else {
      fprintf(stderr, "usage: %s --host=IP4ADDR --port=PORT --user=UID --group=GID\n", argv[0]);

      return 1;
    }

  _snprintf(connect, sizeof connect - 1, "type=tcp host=%s port=%s", host, port);

  fprintf(stdout, "host=%s port=%s profile=%s M=%d N=%d delay=%d msg1=%s msg2=%s\n", host, port, profile, M, N, delay, msg1, msg2);

#if 1
  size_t x = 0;

  nf::cmi_t MY_DELIVERY_METHOD;

  client.reg_callback_method(mdcb, 0, MY_DELIVERY_METHOD);

  for (size_t i = 0; i < M; ++i) {
    if ((e = client.reg(connect, profile)) == nf::ERR_OK) {
      FUSION_DEBUG("client id=%d", client.id());

      nf::mid_t mid1 = 0;
      nf::mid_t mid_best = 0;
      size = 4;

      if (nf::ERR_OK == client.mcreate(msg1, nf::O_RDWR, nf::MT_EVENT, mid1, size) &&
          nf::ERR_OK == client.mcreate(msg2, nf::O_RDWR, nf::MT_EVENT, mid_best, size)) {
//      e = client.subscribe(mid1, nf::SF_PUBLISH, nf::CM_MANUAL, cb);
        e = client.subscribe(mid1, nf::SF_PUBLISH, MY_DELIVERY_METHOD, cb);

        if (e != nf::ERR_OK)
          FUSION_DEBUG("subscribe=%d", e);

        e = client.subscribe(mid_best, nf::SF_PUBLISH, nf::CM_MANUAL, cb);

        if (e != nf::ERR_OK)
          FUSION_DEBUG("subscribe=%d", e);

        rec_nr = 0;
        snd_nr = 0;

//      test message size checks...
//      size   = 2;

        while (rec_nr < N) {
          ::Sleep(delay);
//        ::fprintf(stdout, "snd=%d rec=%d\n", snd_nr, rec_nr);

          if (snd_nr < N) {
//          ::Sleep(1);
            ++snd_nr; ++x;

            e = client.publish(mid1, size, &x);

            if (e != nf::ERR_OK)
              FUSION_DEBUG("publish=%d", e);

            e = client.publish(mid_best, size, &x);

            if (e != nf::ERR_OK)
              FUSION_DEBUG("publish=%d", e);
          }

          // trigger mannual delivery
          client.dispatch(true);
        }

        e = client.unsubscribe(mid1);

        if (e != nf::ERR_OK)
          FUSION_DEBUG("unsubscribe=%d", e);

        e = client.unsubscribe(mid_best);

        if (e != nf::ERR_OK)
          FUSION_DEBUG("unsubscribe=%d", e);
      }
      else {
        FUSION_DEBUG("mopen mid=%d", mid1);

        e = client.mclose(mid1);

        FUSION_DEBUG("mopen mid=%d", mid_best);

        e = client.mclose(mid_best);
      }

      e = client.unreg();

      if (e != nf::ERR_OK)
        FUSION_DEBUG("unreg=%d", e);
    }
    else
      FUSION_DEBUG("reg=%d", e);

    //printf(".");
    //::Sleep((1000 * rand())/RAND_MAX);;
  }
#else

  if ((e = client.reg(connect, user, group)) == nf::ERR_OK) {
    FUSION_DEBUG("client id=%d", client.id());

    nf::mid_t mid = 0;

    e = client.mopen("test", nf::O_RDWR|nf::O_CREATE, nf::MT_EVENT, mid, 4);

    if (e == nf::ERR_OK) {
      snd_nr = 0;

      for (int j = 0; j < 1000; ++j) {
        while (snd_nr < 1000) {
  //      ::fprintf(stdout, "snd=%d rec=%d\n", snd_nr, rec_nr);

          ++snd_nr;
          e = client.publish(mid, 4, &i);

          if (e != nf::ERR_OK)
            FUSION_DEBUG("publish=%d", e);
        }

        snd_nr = 0;
 //     ::fprintf(stderr, "j=%d\n", j);
      }
    }
    else
      FUSION_DEBUG("mopen mid=%d", mid);

    ::Sleep(10);
    e = client.mclose(mid);

    if (e != nf::ERR_OK)
      FUSION_DEBUG("unsubscribe=%d", e);

    ::Sleep(10);
    e = client.unreg();

    if (e != nf::ERR_OK)
      FUSION_DEBUG("unreg=%d", e);
  }
  else
    FUSION_DEBUG("reg=%d", e);
#endif

  ::Sleep(1);

  return 0;
}
