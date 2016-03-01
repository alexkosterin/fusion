/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <iostream>
#include <windows.h>
#include "include/nf.h"
#include "include/mb.h"
#include "include/nf_macros.h"

size_t rec_nr = 0;
size_t snd_nr = 0;
nf::cmi_t cmi;

static nf::result_t __stdcall callback(nf::mid_t mid, size_t len, const void *data) {
	++rec_nr;
	return nf::ERR_OK;
}

static nf::result_t __stdcall mcallback(nf::result_t (__stdcall *cb)(nf::mid_t, size_t, const void*), void* cookie, nf::mid_t mid, size_t len, const void *data) {
  return cb ? cb(mid, len, data) : nf::ERR_OK;
}

nf::client_t client("ex6");

int main() {
	nf::mid_t   m_read, m_write;
	nf::result_t e;
	nf::mtype_t type;
	int delay   = 0;
	size_t size = 0;

	// Default port/host
	const char* port  = "3001";
	const char* host  = "127.0.0.1";
	char connect[250] = {};
	const char* prof  = "test";
	const char* smsg  = 0;
	const char* rmsg  = 0;
  bool asynch       = false;
  size_t nr         = 1000;
  nf::mtype_t st    = 0;
  nf::mtype_t rt    = 0;
  size_t waitrnr    = 0;
  bool exclusive    = false;

	// Command line arguments
	for (int i = 1; i < __argc; ++i) {
		if (__argv[i]) {
			size_t len;
			if ((len = 7) && !::strncmp(__argv[i],        "--port=", len))
				port = __argv[i] + len;
			else if ((len = 7) && !::strncmp(__argv[i],   "--host=", len))
				host = __argv[i] + len;
			else if ((len = 10) && !::strncmp(__argv[i],  "--profile=", len))
				prof = __argv[i] + len;
			else if ((len = 8) && !::strncmp(__argv[i],   "--delay=", len))
				delay = atoi(__argv[i] + len);
			else if ((len = 6) && !::strncmp(__argv[i],   "--snd=", len))
				smsg = __argv[i] + len;
			else if ((len = 6) && !::strncmp(__argv[i],   "--rcv=", len))
				rmsg = __argv[i] + len;
			else if (             !::strcmp(__argv[i],    "--async"))
				asynch = true;
			else if ((len = 5) && !::strncmp(__argv[i],   "--nr=", len))
				nr = ::atoi(__argv[i] + len);
			else if (             !::strcmp(__argv[i],    "--stype=event"))
        st = nf::MT_EVENT;
			else if (             !::strcmp(__argv[i],    "--stype=data"))
        st = nf::MT_DATA;
			else if (             !::strcmp(__argv[i],    "--rtype=event"))
        rt = nf::MT_EVENT;
			else if (             !::strcmp(__argv[i],    "--rtype=data"))
        rt = nf::MT_DATA;
			else if ((len = 10) && !::strncmp(__argv[i],  "--waitrnr=", len))
				waitrnr = ::atoi(__argv[i] + len);
			else if (              !::strcmp(__argv[i],   "--exclusive"))
				exclusive = true;
      else
        FUSION_FATAL("unknown option: %s", __argv[i]);
		}
	}

  if (!smsg && !rmsg) {
    FUSION_FATAL("smgs or rmsg");
  }

	// register client with message bus
	_snprintf(connect, sizeof(connect) - 1, "type=tcp host=%s port=%s", host, port);

  e = client.reg(connect, prof);

  FUSION_VERIFY(e == nf::ERR_OK, "e=%s", nf::result_to_str(e));

	// Create message
  if (smsg && st) {
	  e = client.mcreate(smsg, nf::O_RDWR, st, m_write, size);

    FUSION_VERIFY(e == nf::ERR_OK, "%s e=%s", smsg, nf::result_to_str(e));

    e = client.mclose(m_write);

    FUSION_VERIFY(e == nf::ERR_OK, "e=%s", nf::result_to_str(e));
  }

  if (rmsg && rt) {
	  e = client.mcreate(rmsg, nf::O_RDWR, rt, m_read, size);

    FUSION_VERIFY(e == nf::ERR_OK, "%s e=%s", rmsg, nf::result_to_str(e));

    e = client.mclose(m_read);

    FUSION_VERIFY(e == nf::ERR_OK, "e=%s", nf::result_to_str(e));
  }

	// Open message
  if (smsg) {
	  e = client.mopen(smsg, exclusive ? nf::O_EXCL|nf::O_WRONLY : nf::O_WRONLY, type, m_write, size);

    FUSION_VERIFY(e == nf::ERR_OK, "e=%s", nf::result_to_str(e));
  }

  if (rmsg) {
	  e = client.mopen(rmsg, exclusive ? nf::O_EXCL|nf::O_RDONLY : nf::O_RDONLY, type, m_read, size);

    FUSION_VERIFY(e == nf::ERR_OK, "e=%s", nf::result_to_str(e));

    // Subscribe to message
    if (asynch) {
  	  e = client.reg_callback_method(mcallback, 0, cmi);

      FUSION_VERIFY(e == nf::ERR_OK, "e=%s", nf::result_to_str(e));
    }

  	e = client.subscribe(m_read, nf::SF_PUBLISH, asynch ? cmi : nf::CM_MANUAL, callback);

    FUSION_VERIFY(e == nf::ERR_OK, "e=%s", nf::result_to_str(e));
  }

  while (smsg && e == nf::ERR_OK && snd_nr < nr) {
    if (snd_nr < nr) {
			++snd_nr;

			e = client.publish(m_write, size, 0);

      FUSION_VERIFY(e == nf::ERR_OK, "e=%s", nf::result_to_str(e));
		}

    if (rmsg && !asynch) {
      e = client.dispatch(true, 1);

      FUSION_VERIFY(e == nf::ERR_OK, "e=%s", nf::result_to_str(e));
    }

    ::Sleep(delay);
	}

  if (waitrnr)
    while (e == nf::ERR_OK && rec_nr < waitrnr)
      e = client.dispatch(true, 1);

  printf("s=%d r=%d e=%s\n", snd_nr, rec_nr, nf::result_to_str(e));

	return 0;
}
