/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "include/nf.h"	// FUSION
#include "include/nf_macros.h"
#include "include/mb.h"	// FUSION MESSAGE BUS

nf::mid_t
  m_read_write,
  m_readonly,
  m_writeonly,
  m_group;

// callback function
//  data type is void*, as only data type can be defferent for
//  deffrent message ids
nf::result_t __stdcall on_event(nf::mid_t mid, size_t len, const void *data) {
  nf::result_t e;

  if (mid == m_read_write)	{
    printf("received m_read_write: %s\n", (const char*)data);
    e = nf::ERR_OK;
  }
  else if (mid == m_readonly)	{
    printf("received m_readonly: %s\n", (const char*)data);
    e = nf::ERR_OK;
  }
  else if (mid == m_read_write)	{
    printf("received m_writeonly: must not happen\n");
    e = nf::ERR_UNEXPECTED;
  }
  else 	{
    printf("received something else: %d, must not happen\n", mid);
    e = nf::ERR_UNEXPECTED;
  }

  // note: result is meaningful only for sends (not publish or posts)
  return e;
}

int main(int argc, const char** argv)
{
  const char* port = "3333";
  const char* host = "127.0.0.1";
  const char* user = "none";
  const char* group= "none";
  char connect[250] = {};
  nf::mtype_t type;
  size_t  size;

  for (int i = 1; i < argc; ++i)
    if (!::strncmp(argv[i], "--port=", 7))
      port = argv[i] + 7;
    else if (!::strncmp(argv[i], "--host=", 7))
      host = argv[i] + 7;
    else if (!::strncmp(argv[i], "--user=", 7))
      user = argv[i] + 7;
    else if (!::strncmp(argv[i], "--group=", 8))
      group = argv[i] + 7;
    else {
      fprintf(stderr, "usage: %s --host=IP4ADDR --port=PORT --user=UID --group=GID\n", argv[0]);

      return 1;
    }

  _snprintf(connect, sizeof connect - 1, "type=tcp host=%s port=%s", host, port);

  nf::result_t e;

  // declare client for fusion
  nf::client_t client(
    "example1"		// user friendly client name; used for debugging
  );

  // register client with message bus
  e = client.reg(connect, "");

  if (e != nf::ERR_OK) {
    printf("failed to register client: %d\n", e);
    return 1;
  }

  printf("client %s id=%d v%d.%d\n", client.name(), client.id(), client.version().mini.maj, client.version().mini.min);

  // open/create read/write message descriptor for "MESSAGE01"
  // will be able to publish and subscribe
  size = 0;
  e = client.mcreate("MESSAGE01", nf::O_RDWR, nf::MT_EVENT, m_read_write, size);
  FUSION_ASSERT(e == nf::ERR_OK);

  // exclusively create readonly message descriptor for "MESSAGE02"
  // if message already exists, then fail
  // will be able to subscribe, not publish
  size = 0;
  e = client.mcreate("MESSAGE02", nf::O_RDONLY|nf::O_EXCL, nf::MT_EVENT, m_readonly, size);
  FUSION_ASSERT(e == nf::ERR_OK);

  // open writeonly message descriptor for "MESSAGE03"
  // will be able to publish, not subscribe
  size = 0;
  e = client.mopen("MESSAGE03", nf::O_WRONLY, type, m_writeonly, size);
  FUSION_ASSERT(e == nf::ERR_OK);
  FUSION_ASSERT(type == nf::MT_EVENT);

  // subscrite
  e = client.subscribe(m_read_write, nf::SF_PUBLISH, nf::CM_MANUAL, on_event);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = client.subscribe(m_readonly, nf::SF_PUBLISH, nf::CM_MANUAL, on_event);
  FUSION_ASSERT(e == nf::ERR_OK);

  // this will produce error, as m_writeonly is only for writing
  e = client.subscribe(m_writeonly, nf::SF_PUBLISH, nf::CM_MANUAL, on_event);
  FUSION_ASSERT(e == nf::ERR_WRITEONLY);

  const char* pub_msg		= "pub test message";
  const char* post_msg	= "post test message";
  const char* send_msg	= "sent test message";

  while (client.registered()) {
    client.dispatch(true);

    // publish, post, send... what is the defference?
    // -	publishing goes only to those who subscribed to a message
    // -	posting goes to ALL or GROUP or INDIVIDUAL client, no acknowledment
    // -	sending goes to INDIVIDUAL client, with timed-out acknowledment

    // publish -----------------------------------------------------------------
    // message will be delivered to all subscriber
    e = client.publish(m_read_write, ::strlen(pub_msg) + 1, pub_msg);
    FUSION_ASSERT(e == nf::ERR_OK);

    // publishing readonly message is an error
    e = client.publish(m_readonly, ::strlen(pub_msg) + 1, pub_msg);
    FUSION_ASSERT(e == nf::ERR_READONLY);


    // post --------------------------------------------------------------------
    // message can be delivared:
    // - to all clients (maybe ignored if there is no callback registered)
    e = client.post(m_read_write, nf::CID_ALL, ::strlen(post_msg) + 1, post_msg);

    // - to all clients, but self
    e = client.post(m_read_write, nf::CID_ALL|nf::CID_NOSELF, ::strlen(post_msg) + 1, post_msg);
    FUSION_ASSERT(e == nf::ERR_OK);

    // - to group
    m_group = client.id("ex1-group");

    if (m_group != nf::CID_NONE) {
      FUSION_ASSERT(nf::CID_IS_GRP(m_group));

      e = client.post(m_read_write, m_group, ::strlen(post_msg) + 1, post_msg);
      FUSION_ASSERT(e == nf::ERR_IMPLEMENTED); // not implemented

      // - to group, but self
      e = client.post(m_read_write, m_group|nf::CID_NOSELF, ::strlen(post_msg) + 1, post_msg);
      FUSION_ASSERT(e == nf::ERR_IMPLEMENTED); // not implemented
    }

    // - posting to CID_SUBSCRIBED to all subscribed is same as publishing
    e = client.post(m_read_write, nf::CID_PUB, ::strlen(post_msg) + 1, post_msg);
    FUSION_ASSERT(e == nf::ERR_OK);

    // posting readonly message is an error
    e = client.post(m_readonly, client.id(), ::strlen(post_msg) + 1, post_msg);
    FUSION_ASSERT(e == nf::ERR_READONLY);

    // - to subscribed, is same as publishing, but can ignore self
    e = client.post(m_read_write, nf::CID_PUB|nf::CID_NOSELF, ::strlen(post_msg) + 1, post_msg);
    FUSION_ASSERT(e == nf::ERR_OK);


    // send --------------------------------------------------------------------
    // send cannot be done to all or group
    e = client.send(m_read_write, nf::CID_ALL, ::strlen(send_msg) + 1, send_msg, 5000 /*5 sec*/);
    FUSION_ASSERT(e == nf::ERR_MULTI_ACK);

    // sending readonly message is an error
    e = client.send(m_readonly, client.id(), ::strlen(send_msg) + 1, send_msg, 5000 /*5 sec*/);
    FUSION_ASSERT(e == nf::ERR_READONLY);

    // send to self with timeout 5 secs
    e = client.send(m_read_write, client.id(), ::strlen(send_msg) + 1, send_msg, 5000 /*5 sec*/);
    FUSION_ASSERT(e == nf::ERR_OK);

    // send to self with an infinite wait time
    e = client.send(m_read_write, client.id(), ::strlen(send_msg) + 1, send_msg, 0);
    FUSION_ASSERT(e == nf::ERR_OK);
  }

  // when done close message descriptors
  e = client.mclose(m_read_write);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = client.mclose(m_readonly);
  FUSION_ASSERT(e == nf::ERR_OK);

  e = client.mclose(m_writeonly);
  FUSION_ASSERT(e == nf::ERR_OK);

  // may remove private message names
  e = client.munlink("MESSAGE01");
  FUSION_ASSERT(e == nf::ERR_OK);

  e = client.munlink("MESSAGE02");
  FUSION_ASSERT(e == nf::ERR_OK);

  if (client.unreg())
    return 1;

  return 0;
}

