/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <stdio.h>
#include <Winsock2.h>
#include <include/configure.h>
#include <include/nf_macros.h>
#include <include/resolve.h>

#pragma comment(lib, "ws2_32.lib")

ULONG resolve_address(const char* name) {
  FUSION_ENSURE(name, return INADDR_NONE);

  if (!name)
    return INADDR_NONE;

  struct hostent *he = ::gethostbyname(name);

  if (!he) {
    switch (DWORD rc = ::WSAGetLastError()) {
    case WSAHOST_NOT_FOUND:
      FUSION_ERROR("Host %s not found", name);          break;

    case WSANO_DATA:
      FUSION_ERROR("No data record found: %s", name);   break;

    default:
      FUSION_ERROR("gethostbyname(%s)=%ld", name, rc);  break;
    }

    return INADDR_NONE;
  }

  switch (he->h_addrtype) {
  case AF_INET:
    break;

  case AF_INET6:
    FUSION_ERROR("addrtype=AF_INET6");

    return INADDR_NONE;

  case AF_NETBIOS:
    FUSION_ERROR("addrtype=AF_NETBIOS");

    return INADDR_NONE;

  default:
    FUSION_ERROR("Unknown addrtype=%d", he->h_addrtype);

    return INADDR_NONE;
  }

  FUSION_ENSURE(he->h_addrtype == AF_INET,  return INADDR_NONE);
  FUSION_ENSURE(he->h_addr_list[0] != 0,    return INADDR_NONE);

  ULONG resolved = INADDR_NONE;

  for (int i = 0; he->h_addr_list[i]; ++i) {
    struct in_addr addr;

    addr.s_addr = *(u_long*)he->h_addr_list[i];

    if (i)
      FUSION_DEBUG("ignoring IP address: %s", ::inet_ntoa(addr));
    else {
      FUSION_DEBUG("using IP address: %s", ::inet_ntoa(addr));

      resolved = addr.s_addr;
    }
  }

  return resolved;
}
