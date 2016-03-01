/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <map>
#include <set>

#include "include/nf.h"	    // FUSION
#include "include/nf_macros.h"
#include "include/mb.h"	    // FUSION MESSAGE BUS
#include "include/nf_mcb.h"	// FUSION MCB
#include "include/tsc.h"

#include "mcb.proto/mcb.pb.h"

#if	defined(WIN32)
# define NOGDI
# include <windows.h>
# include <Rpc.h>
# pragma comment(lib, "Rpcrt4.lib")
#endif

# define NOGDI
# define _WINSOCKAPI_
#include <windows.h>


////////////////////////////////////////////////////////////////////////////////
int main(int argc, const char** argv) {
  const char* name    = "Fusion System Info";
  const char* port    = "3001";
  const char* host    = "127.0.0.1";
  const char* profile = "unknown";
  char connect[250]   = {};
  nf::result_t e;

  std::vector<unsigned short> mids;
  std::vector<unsigned short> cids;

  bool  mid_mode                = false;
  bool  cid_mode                = false;

  bool  opt_no_self             = true;
  bool  opt_symbolic_oflags     = true;
  int   opt_msg_rpt             = 1;
  char  opt_msg_rpt_mode        = '>';    // '<' less-equal, '=' exactly, '>' more-equal
  const char* opt_graph         = 0;
  std::set<std::pair<int, int>> opt_exclude_clients_edges;
  bool opt_grapth_closed_path   = false;
  bool opt_graph_interconnected = false;
  bool opt_graph_directed       = false;
  bool opt_graph_edge_mid       = false;
  bool opt_graph_tags_mid       = false;

  int  opt_timeout              = 60000;

  int opt_flags   =
    COM_AVAIL_MCBS|COM_ALLOCATED_MCBS|
    CLI_NAME|CLI_ADDRESS|CLI_DEFAULT_PROFILE|
    MSG_NAME|MSG_OFLAG;

  for (int i = 1; i < argc; ++i)
    if (     !::strncmp(argv[i], "--port=", 7))
      port = argv[i] + 7;
    else if (!::strncmp(argv[i], "--profile=", 10))
      profile = argv[i] + 10;
    else if (!::strncmp(argv[i], "--host=", 7))
      host = argv[i] + 7;
    else if (!::strncmp(argv[i], "--name=", 7))
      name = argv[i] + 7;
    else if (!::strncmp(argv[i], "--timeout=", 10))
      opt_timeout = ::atoi(argv[i] + 10);
    else if (!::strcmp(argv[i], "--help"))
      goto do_help;

    else if (!::strncmp(argv[i], "--mid=", 6)) {
      FUSION_ASSERT(::atoi(argv[i] + 6) > nf::MD_SYS_LAST_);

      mids.push_back(::atoi(argv[i] + 6));

      mid_mode = false;
      cid_mode = false;
    }
    else if (!::strcmp(argv[i], "--mid")) {
      mid_mode = true;
      cid_mode = false;
    }
    else if (!::strncmp(argv[i], "--cid=", 6)) {
      FUSION_ASSERT(::atoi(argv[i] + 6) > 0);

      cids.push_back(::atoi(argv[i] + 6));

      mid_mode = false;
      cid_mode = false;
    }
    else if (!::strcmp(argv[i], "--cid")) {
      mid_mode = false;
      cid_mode = true;
    }

    else if (!::strcmp(argv[i], "--no_self"))
      opt_no_self = true;
    else if (!::strcmp(argv[i], "--self"))
      opt_no_self = false;

    else if (!::strcmp(argv[i], "--sys_clients"))
      opt_flags |= SYS_CLIENTS;
    else if (!::strcmp(argv[i], "--no_sys_clients"))
      opt_flags &= ~SYS_CLIENTS;
    else if (!::strcmp(argv[i], "--sys_messages"))
      opt_flags |= SYS_MESSAGES;
    else if (!::strcmp(argv[i], "--no_sys_messages"))
      opt_flags &= ~SYS_MESSAGES;
    else if (!::strcmp(argv[i], "--sys_client_messages"))
      opt_flags |= SYS_CLIENT_MESSAGES;
    else if (!::strcmp(argv[i], "--no_sys_client_messages"))
      opt_flags &= ~SYS_CLIENT_MESSAGES;

    else if (!::strcmp(argv[i], "--com_all"))
      opt_flags |= COM_AVAIL_MCBS|COM_ALLOCATED_MCBS|COM_PREALLOC_MCBS|COM_MAX_ALLOC_MCBS;
    else if (!::strcmp(argv[i], "--com_none"))
      opt_flags &= ~(COM_AVAIL_MCBS|COM_ALLOCATED_MCBS|COM_PREALLOC_MCBS|COM_MAX_ALLOC_MCBS);

    else if (!::strcmp(argv[i], "--com_avail_mcbs"))
      opt_flags |= COM_AVAIL_MCBS;
    else if (!::strcmp(argv[i], "--no_com_avail_mcbs"))
      opt_flags &= ~COM_AVAIL_MCBS;
    else if (!::strcmp(argv[i], "--com_allocated_mcbs"))
      opt_flags |= COM_ALLOCATED_MCBS;
    else if (!::strcmp(argv[i], "--no_com_allocated_mcbs"))
      opt_flags &= ~COM_ALLOCATED_MCBS;
    else if (!::strcmp(argv[i], "--com_prealloc_mcbs"))
      opt_flags |= COM_PREALLOC_MCBS;
    else if (!::strcmp(argv[i], "--no_com_prealloc_mcbs"))
      opt_flags &= ~COM_PREALLOC_MCBS;
    else if (!::strcmp(argv[i], "--com_max_alloc_mcbs"))
      opt_flags |= COM_MAX_ALLOC_MCBS;
    else if (!::strcmp(argv[i], "--no_com_max_alloc_mcbs"))
      opt_flags &= ~COM_MAX_ALLOC_MCBS;

    else if (!::strcmp(argv[i], "--cli_all"))
      opt_flags |= CLI_NAME|CLI_ADDRESS|CLI_DEFAULT_PROFILE|CLI_UUID|CLI_PROFILES|CLI_CONN_LATENCY|CLI_QUEUE_SIZE|CLI_GROUPS|CLI_START_TIME|CLI_SYNC_PERIOD;
    else if (!::strcmp(argv[i], "--cli_none"))
      opt_flags &= ~(CLI_NAME|CLI_ADDRESS|CLI_DEFAULT_PROFILE|CLI_UUID|CLI_PROFILES|CLI_CONN_LATENCY|CLI_QUEUE_SIZE|CLI_GROUPS|CLI_START_TIME|CLI_SYNC_PERIOD);

    else if (!::strcmp(argv[i], "--cli_name"))
      opt_flags |= CLI_NAME;
    else if (!::strcmp(argv[i], "--no_cli_name"))
      opt_flags &= ~CLI_NAME;
    else if (!::strcmp(argv[i], "--cli_address"))
      opt_flags |= CLI_ADDRESS;
    else if (!::strcmp(argv[i], "--no_cli_address"))
      opt_flags &= ~CLI_ADDRESS;
    else if (!::strcmp(argv[i], "--cli_profile"))
      opt_flags |= CLI_DEFAULT_PROFILE;
    else if (!::strcmp(argv[i], "--no_cli_profile"))
      opt_flags &= ~CLI_DEFAULT_PROFILE;
    else if (!::strcmp(argv[i], "--cli_uuid"))
      opt_flags |= CLI_UUID;
    else if (!::strcmp(argv[i], "--no_cli_uuid"))
      opt_flags &= ~CLI_UUID;
    else if (!::strcmp(argv[i], "--cli_profiles"))
      opt_flags |= CLI_PROFILES;
    else if (!::strcmp(argv[i], "--no_cli_profiles"))
      opt_flags &= ~CLI_PROFILES;
    else if (!::strcmp(argv[i], "--cli_conn_latency"))
      opt_flags |= CLI_CONN_LATENCY;
    else if (!::strcmp(argv[i], "--no_cli_conn_latency"))
      opt_flags &= ~CLI_CONN_LATENCY;
    else if (!::strcmp(argv[i], "--cli_queue_limit"))
      opt_flags |= CLI_QUEUE_LIMIT;
    else if (!::strcmp(argv[i], "--no_cli_queue_limit"))
      opt_flags &= ~CLI_QUEUE_LIMIT;
    else if (!::strcmp(argv[i], "--cli_queue_size"))
      opt_flags |= CLI_QUEUE_SIZE;
    else if (!::strcmp(argv[i], "--no_cli_queue_size"))
      opt_flags &= ~CLI_QUEUE_SIZE;
    else if (!::strcmp(argv[i], "--cli_groups"))
      opt_flags |= CLI_GROUPS;
    else if (!::strcmp(argv[i], "--no_cli_groups"))
      opt_flags &= ~CLI_GROUPS;
    else if (!::strcmp(argv[i], "--cli_start_time"))
      opt_flags |= CLI_START_TIME;
    else if (!::strcmp(argv[i], "--no_cli_start_time"))
      opt_flags &= ~CLI_START_TIME;
    else if (!::strcmp(argv[i], "--cli_sync_period"))
      opt_flags |= CLI_SYNC_PERIOD;
    else if (!::strcmp(argv[i], "--no_cli_sync_period"))
      opt_flags &= ~CLI_SYNC_PERIOD;

    else if (!::strcmp(argv[i], "--msg_all"))
      opt_flags |= MSG_NAME|MSG_PATH|MSG_OFLAG|MSG_SFLAG|MSG_OPEN_NR|MSG_SUBS_NR|MSG_SND_NR|MSG_RCV_NR|MSG_SND_AVG|MSG_RCV_AVG;
    else if (!::strcmp(argv[i], "--msg_none"))
      opt_flags &= ~(MSG_NAME|MSG_PATH|MSG_OFLAG|MSG_SFLAG|MSG_OPEN_NR|MSG_SUBS_NR|MSG_SND_NR|MSG_RCV_NR|MSG_SND_AVG|MSG_RCV_AVG);

    else if (!::strcmp(argv[i], "--msg_name"))
      opt_flags |= MSG_NAME;
    else if (!::strcmp(argv[i], "--no_msg_name"))
      opt_flags &= ~MSG_NAME;
    else if (!::strcmp(argv[i], "--msg_path"))
      opt_flags |= MSG_PATH;
    else if (!::strcmp(argv[i], "--no_msg_path"))
      opt_flags &= ~MSG_PATH;
    else if (!::strcmp(argv[i], "--msg_oflag"))
      opt_flags |= MSG_OFLAG;
    else if (!::strcmp(argv[i], "--no_msg_oflag"))
      opt_flags &= ~MSG_OFLAG;
    else if (!::strcmp(argv[i], "--msg_sflag"))
      opt_flags |= MSG_SFLAG;
    else if (!::strcmp(argv[i], "--no_msg_sflag"))
      opt_flags &= ~MSG_SFLAG;
    else if (!::strcmp(argv[i], "--msg_open_nr"))
      opt_flags |= MSG_OPEN_NR;
    else if (!::strcmp(argv[i], "--no_msg_open_nr"))
      opt_flags &= ~MSG_OPEN_NR;
    else if (!::strcmp(argv[i], "--msg_subs_nr"))
      opt_flags |= MSG_SUBS_NR;
    else if (!::strcmp(argv[i], "--no_msg_subs_nr"))
      opt_flags &= ~MSG_SUBS_NR;
    else if (!::strcmp(argv[i], "--msg_snd_nr"))
      opt_flags |= MSG_SND_NR;
    else if (!::strcmp(argv[i], "--no_msg_snd_nr"))
      opt_flags &= ~MSG_SND_NR;
    else if (!::strcmp(argv[i], "--msg_rcv_nr"))
      opt_flags |= MSG_RCV_NR;
    else if (!::strcmp(argv[i], "--no_msg_rcv_nr"))
      opt_flags &= ~MSG_RCV_NR;
    else if (!::strcmp(argv[i], "--msg_snd_avg"))
      opt_flags |= MSG_SND_AVG;
    else if (!::strcmp(argv[i], "--no_msg_snd_avg"))
      opt_flags &= ~MSG_SND_AVG;
    else if (!::strcmp(argv[i], "--msg_rcv_avg"))
      opt_flags |= MSG_RCV_AVG;
    else if (!::strcmp(argv[i], "--no_msg_rcv_avg"))
      opt_flags &= ~MSG_RCV_AVG;
    else if (!::strncmp(argv[i], "--msg_repeat=", 13)) {
      opt_msg_rpt = ::atoi(argv[i] + 14);

      if (opt_msg_rpt < 0)
        opt_msg_rpt = -opt_msg_rpt;

      switch (argv[i][14]) {
      case '-': opt_msg_rpt_mode = '<'; break;
      case '+': opt_msg_rpt_mode = '>'; break;
      default:  opt_msg_rpt_mode = '='; break;
      }

      //::fprintf(stdout, "opt_msg_rpt_mode=%c%d\n", opt_msg_rpt_mode, opt_msg_rpt);
    }

    else if (!::strncmp(argv[i], "--graph=", 8))
      opt_graph = argv[i] + 8;
    else if (!::strncmp(argv[i], "--graph_exclude_clients_edge=", 29)) {
      int cid0, cid1;

      int rc = sscanf(argv[i] + 29, "%d , %d", &cid0, &cid1);

      if (cid0 > cid1)
        std::swap(cid0, cid1);

      opt_exclude_clients_edges.insert(std::make_pair(cid0, cid1));

      FUSION_ASSERT(rc == 2);
    }
    else if (!::strcmp(argv[i], "--graph_closed_path")) {
      opt_grapth_closed_path    = true;
      opt_graph_interconnected  = false;
      opt_graph_directed        = false;
    }
    else if (!::strcmp(argv[i], "--graph_interconnected")) {
      opt_grapth_closed_path    = false;
      opt_graph_interconnected  = true;
      opt_graph_directed        = false;
    }
    else if (!::strcmp(argv[i], "--graph_directed")) {
      opt_grapth_closed_path    = false;
      opt_graph_interconnected  = false;
      opt_graph_directed        = true;
    }
    else if (!::strcmp(argv[i], "--graph_show_mid_on_edges"))
      opt_graph_edge_mid        = true;
    else if (!::strcmp(argv[i], "--graph_show_mid_on_tags"))
      opt_graph_tags_mid        = true;

    // must be the last option
    else if (!::strncmp(argv[i], "--", 2))
      goto do_unknown_option;

    else {
      if (cid_mode) {
        FUSION_ASSERT(::atoi(argv[i]) > 0);

        cids.push_back(::atoi(argv[i]));
      }
      else if (mid_mode) {
        FUSION_ASSERT(::atoi(argv[i]) > nf::MD_SYS_LAST_);

        mids.push_back(::atoi(argv[i]));
      }
      else {
do_unknown_option:
        ::fprintf(stderr, "unknown option: %s\n", argv[i]);
do_help:
        ::fprintf(stderr, "usage: %s --host=IP4ADDR --port=PORT --profile=PROFILE [--name=NAME] mids cids options\n\
\n\
mids:\n\
  --mid=MID\n\
  --mid MID...\n\
cids:\n\
  --cid=CID\n\
  --cid CID...\n\
options\n\
  --no_self              --self\n\
\n\
  --sys_clients          --no_sys_clients\n\
  --sys_messages         --no_sys_messages\n\
  --sys_client_messages  --no_sys_client_messages\n\
\n\
  --com_all              --com_none\n\
  --com_avail_mcbs*      --no_com_avail_mcbs\n\
  --com_allocated_mcbs*  --no_com_allocated_mcbs\n\
  --com_prealloc_mcbs    --no_com_prealloc_mcbs\n\
  --com_max_alloc_mcbs   --no_com_max_alloc_mcbs\n\
\n\
  --cli_all              --cli_none\n\
  --cli_name*            --no_cli_name\n\
  --cli_address*         --no_cli_address\n\
  --cli_profile*         --no_cli_profile\n\
  --cli_uuid             --no_cli_uuid\n\
  --cli_profiles         --no_cli_profiles\n\
  --cli_conn_latency     --no_cli_conn_latency\n\
  --cli_queue_limit      --no_cli_queue_limit\n\
  --cli_queue_size       --no_cli_queue_size\n\
  --cli_groups           --no_cli_groups\n\
  --cli_start_time       --no_cli_start_time\n\
  --cli_sync_period      --no_cli_sync_period\n\
\n\
  --msg_all              --msg_none\n\
  --msg_name*            --no_msg_name\n\
  --msg_path             --no_msg_path\n\
  --msg_oflag*           --no_msg_oflag\n\
  --msg_sflag            --no_msg_sflag\n\
  --msg_open_nr          --no_msg_open_nr\n\
  --msg_subs_nr          --no_msg_subs_nr\n\
  --msg_snd_nr           --no_msg_snd_nr\n\
  --msg_rcv_nr           --no_msg_rcv_nr\n\
  --msg_snd_avg          --no_msg_snd_avg\n\
  --msg_rcv_avg          --no_msg_rcv_avg\n\
  --msg_repeat=[-+]N     '+' means equal or more, '-' equal or less, otherwise equal\n\
    default is 1\n\
\n\
  --graph=FILE\n\
  --graph_closed_path\n\
  --graph_directed\n\
  --graph_exclude_clients_edge=cid1,cid2\n\
  --graph_interconnected\n\
  --graph_show_mid_on_edges\n\
  --graph_show_mid_on_tags\n\
", argv[0]);
        return 1;
      }
    }

  ::fprintf(stdout, "host=%s port=%s profile=%s\n", host, port, profile);

  _snprintf(connect, sizeof connect - 1, "type=tcp host=%s port=%s", host, port);

  nf::client_t client(name);

  e = client.reg(connect, profile);

  if (e != nf::ERR_OK) {
    FUSION_DEBUG("client.reg=%d", e);

    return 1;
  }

  mcb_sysinfo_request q;

  if (opt_graph)
    opt_flags |= SYS_CLIENTS|CLI_NAME|CLI_DEFAULT_PROFILE|SYS_CLIENT_MESSAGES|MSG_NAME|MSG_OFLAG|MSG_SFLAG;

  q.set_flags(opt_flags);

  if (cids.size())
    for (auto I = cids.cbegin(), E = cids.cend(); I != E; ++I)
      q.add_cids(*I);
  else
    q.add_cids(opt_no_self ? nf::CID_ALL|nf::CID_NOSELF : nf::CID_ALL);

  if (mids.size())
    for (auto I = mids.cbegin(), E = mids.cend(); I != E; ++I)
      q.add_mids(*I);
  else
    q.add_mids(nf::MD_SYS_ANY);

  size_t req_len  = q.ByteSize();
  char*  req_data = (char*)::alloca(req_len);

  q.SerializeToArray(req_data, req_len);

  nf::mid_t   rep_mid;
  size_t      rep_len;
  const void* rep_data;
  std::map<int, const char*> cnames;

  if ((e = client.request(nf::MD_SYS_SYSINFO_REQUEST, nf::CID_SYS, req_len, req_data, rep_mid, rep_len, rep_data, opt_timeout)) == nf::ERR_OK) {
    mcb_sysinfo_reply p;

    FUSION_ASSERT(rep_mid == nf::MD_SYS_SYSINFO_REPLY);

    if (p.ParseFromArray(rep_data, rep_len)) {
      int flags = p.flags();

      if (flags & COM_AVAIL_MCBS)
        ::fprintf(stdout, "available-mcbs=%d\n", p.common().avail_mcbs());

      if (flags & COM_ALLOCATED_MCBS)
        ::fprintf(stdout, "allocated-mcbs=%d\n", p.common().allocated_mcbs());

      if (flags & COM_PREALLOC_MCBS)
        ::fprintf(stdout, "preallocated-mcbs=%d\n", p.common().prealloc_mcbs());

      if (flags & COM_MAX_ALLOC_MCBS)
        ::fprintf(stdout, "max allocate-mcbs=%d\n", p.common().max_alloc_mcbs());

      for (int i = 0; i < p.clients_size(); ++i) {
        ::fprintf(stdout, "#%d\t", i + 1);

        if (p.clients(i).has_cid())
          ::fprintf(stdout, "cid=%d\t", p.clients(i).cid());

        if (p.clients(i).has_name())
          ::fprintf(stdout, "name='%s'\t", p.clients(i).name().c_str());

        if (p.clients(i).has_cid() && p.clients(i).has_name()) {
          char buff[1024];

          _snprintf(buff, sizeof buff, "%s:%s", p.clients(i).name().c_str(), p.clients(i).default_profile().c_str());
          cnames[p.clients(i).cid()] = ::strdup(buff);
        }

        if (p.clients(i).has_address())
          ::fprintf(stdout, "address='%s'\t", p.clients(i).address().c_str());

        if (p.clients(i).has_default_profile())
          ::fprintf(stdout, "profile='%s'\t", p.clients(i).default_profile().c_str());

        if (flags & CLI_PROFILES) {
          ::fprintf(stdout, "profiles=[");

          for (int j = 0; j < p.clients(i).profiles_size(); ++j)
            ::fprintf(stdout, "%s'%s'", j == 0 ? "": " ", p.clients(i).profiles(j).c_str());

          ::fprintf(stdout, "]\t");
        }

        if (p.clients(i).has_output_queue_limit())
          ::fprintf(stdout, "output-queue-limit=%d\t", p.clients(i).output_queue_limit());

        if (p.clients(i).has_output_queue_size())
          ::fprintf(stdout, "output-queue-size=%d\t", p.clients(i).output_queue_size());

        if (p.clients(i).has_start_time())
          ::fprintf(stdout, "start-time=%d\t", p.clients(i).start_time());

        if (p.clients(i).has_uuid()) {
          UUID* uuid = (UUID*)p.clients(i).uuid().c_str();

          ::fprintf(stdout, "uuid={%0.8X-%0.4X-%0.4X-%0.2X%0.2X-%0.2X%0.2X%0.2X%0.2X%0.2X%0.2X}\t", 
            uuid->Data1, uuid->Data2, uuid->Data3,
            uuid->Data4[0], uuid->Data4[1], uuid->Data4[2], uuid->Data4[3],
            uuid->Data4[4], uuid->Data4[5], uuid->Data4[6], uuid->Data4[7]
            );
        }

        if (p.clients(i).has_connection_latency())
          ::fprintf(stdout, "latency=%d\t", p.clients(i).connection_latency());

        ::fprintf(stdout, "\n");
      }

      std::map<int, size_t> msgs_nr;

      for (int i = 0; i < p.messages_size(); ++i)
        if (p.messages(i).has_mid()) {
          int mid = p.messages(i).mid();
          auto I = msgs_nr.find(mid);

          if (I != msgs_nr.end())
            ++msgs_nr[mid];
          else
            msgs_nr.insert(std::make_pair(mid, 1));
        }

      if (!opt_graph) {
        for (int i = 0, idx = 0; i < p.messages_size(); ++i) {
          bool or = false;
          int mid = p.messages(i).mid();
          auto I  = msgs_nr.find(mid);

          switch (opt_msg_rpt_mode) {
          case '<':
            if (I->second > (size_t)opt_msg_rpt)
              continue;

            break;

          case  '=':
            if (I->second != (size_t)opt_msg_rpt)
              continue;

            break;

          case  '>':
            if (I->second < (size_t)opt_msg_rpt)
              continue;

            break;

          default:
            FUSION_ASSERT(0);
          }

          ++idx;
          ::fprintf(stdout, "#%d\t", idx);

          if (p.messages(i).has_mid())
            ::fprintf(stdout, "mid=%d\t", p.messages(i).mid());

          if (p.messages(i).has_cid())
            ::fprintf(stdout, "cid=%d\t", p.messages(i).cid());

          if (p.messages(i).has_oflags()) {
            ::fprintf(stdout, "oflags=");

            if (!(p.messages(i).oflags() & (nf::O_RDONLY | nf::O_WRONLY))) {
              ::fprintf(stdout, "%sO_RDWR", or ? "|" : ""); or = true;
            }

            if (p.messages(i).oflags() & nf::O_RDONLY) {
              ::fprintf(stdout, "%sO_RDONLY", or ? "|" : ""); or = true;
            }

            if (p.messages(i).oflags() & nf::O_WRONLY) {
              ::fprintf(stdout, "%sO_WRONLY", or ? "|" : ""); or = true;
            }

            if (p.messages(i).oflags() & nf::O_EXCL) {
              ::fprintf(stdout, "%sO_EXCL", or ? "|" : ""); or = true;
            }

            if (p.messages(i).oflags() & nf::O_NOATIME) {
              ::fprintf(stdout, "%sO_NOATIME", or ? "|" : ""); or = true;
            }

            if (p.messages(i).oflags() & nf::O_TEMPORARY) {
              ::fprintf(stdout, "%sO_TEMPORARY", or ? "|" : ""); or = true;
            }

            if (p.messages(i).oflags() & nf::O_EDGE_TRIGGER) {
              ::fprintf(stdout, "%sO_EDGE_TRIGGER", or ? "|" : ""); or = true;
            }

            if (p.messages(i).oflags() & nf::O_NOTIFY_OPEN) {
              ::fprintf(stdout, "%sO_NOTIFY_OPEN", or ? "|" : ""); or = true;
            }

            if (p.messages(i).oflags() & nf::O_NOTIFY_SUBSCRIBE) {
              ::fprintf(stdout, "%sO_NOTIFY_SUBSCRIBE", or ? "|" : ""); or = true;
            }

            if (p.messages(i).oflags() & nf::O_NOTIFY_CONFIGURE) {
              ::fprintf(stdout, "%O_NOTIFY_CONFIGURE", or ? "|" : ""); or = true;
            }

            ::fprintf(stdout, "\t", p.messages(i).oflags());
          }

          if (p.messages(i).has_sflags())
            ::fprintf(stdout, "sflags=%s\t", p.messages(i).sflags() == 0 ? "SF_PRIVATE" : "SF_PUBLISH");

          if (p.messages(i).has_open_nr())
            ::fprintf(stdout, "open_nr=%d\t", p.messages(i).open_nr());

          if (p.messages(i).has_subs_nr())
            ::fprintf(stdout, "subs_nr=%d\t", p.messages(i).subs_nr());

          if (p.messages(i).has_name())
            ::fprintf(stdout, "name='%s'\t", p.messages(i).name().c_str());

          if (p.messages(i).has_path())
            ::fprintf(stdout, "path='%s'\t", p.messages(i).path().c_str());

          if (p.messages(i).has_rcv_nr())
            ::fprintf(stdout, "rcv_nr=%d\t", p.messages(i).rcv_nr());

          if (p.messages(i).has_snd_nr())
            ::fprintf(stdout, "snd_nr=%d\t", p.messages(i).snd_nr());

          ::fprintf(stdout, "\n");
        }
      }
      else {
        FILE* g = ::fopen(opt_graph, "w");

        FUSION_ASSERT(g);

        int cid = 0;

        ::fprintf(g, "\n%s \"%s:%s\" {\n", opt_graph_directed ? "digraph" : "graph", host, port);
        ::fprintf(g, "  size=\"10.5,16.5\"\n");
        ::fprintf(g, "  page=\"11,17\"\n\n");

        std::set<int> mids1;

        for (int i = 0; i < p.messages_size(); ++i) {
          int mid = p.messages(i).mid();
          auto I  = msgs_nr.find(mid);

          if (!p.messages(i).has_cid())
            continue;

          switch (opt_msg_rpt_mode) {
          case '<':
            if (I->second > (size_t)opt_msg_rpt)
              continue;

            break;

          case  '=':
            if (I->second != (size_t)opt_msg_rpt)
              continue;

            break;

          case  '>':
            if (I->second < (size_t)opt_msg_rpt)
              continue;

            break;

          default:
            FUSION_ASSERT(0);
          }

          mids1.insert(mid);

          if (cid != p.messages(i).cid()) {
            if (cid)
              ::fprintf(g, "  }\n\n");

            cid = p.messages(i).cid();

            auto N = cnames.find(cid);
            const char* cname = N != cnames.end() ? N->second : "?";

            ::fprintf(g, "\
  subgraph cluster%d {\n\
    node [shape=box,style=filled,color=white];\n\
    style=filled;\n\
    color=lightgrey;\n\
    label = \"%s\";\n\
\n"
            , cid, cname);
          }

          if (opt_graph_tags_mid)
            ::fprintf(g, "    \"%d:%s\"\t[label=\"%s:%d\"]\n", cid, p.messages(i).name().c_str(), p.messages(i).name().c_str(), p.messages(i).mid());
          else
            ::fprintf(g, "    \"%d:%s\"\t[label=\"%s\"]\n", cid, p.messages(i).name().c_str(), p.messages(i).name().c_str());
        }

        if (cid)
          ::fprintf(g, "  }\n\n");

        for (auto I = mids1.cbegin(), E = mids1.cend(); I != E; ++I) {
          auto J  = msgs_nr.find(*I);

          if (J->second < 2)
            continue;

          if (!opt_exclude_clients_edges.empty()) {
            int cid0, cid1;
            size_t nr = 0;
            bool exclude = false;

            for (int i = 0; i < p.messages_size(); ++i) {
              if (*I == p.messages(i).mid()) {
                switch (nr++) {
                case 0:
                  cid0 = p.messages(i).cid();
                  break;

                case 1:
                  cid1 = p.messages(i).cid();

                  if (cid0 > cid1)
                    std::swap(cid0, cid1);

                  exclude = opt_exclude_clients_edges.find(std::make_pair(cid0, cid1)) != opt_exclude_clients_edges.cend();
                  break;

                default:
                  exclude = false;
                  break;
                }
              }
            }

            if (exclude)
              continue;
          }

          if (opt_graph_directed) {
            for (int i = 0; i < p.messages_size(); ++i)
              if (*I == p.messages(i).mid()) {
                for (int j = 0; j < p.messages_size(); ++j)
                  if (*I == p.messages(j).mid()) {
                    int of0 = p.messages(i).oflags();
                    int of1 = p.messages(j).oflags();

                    if (of0 & nf::O_RDONLY || of1 & nf::O_WRONLY)
                      continue;

                    bool sf1 = p.messages(j).has_sflags(); //

                    if (opt_graph_edge_mid)
                      ::fprintf(g, "  \"%d:%s\" -> \"%d:%s\"\t[arrowhead=%snormal;label=\"%d\"]\n", p.messages(i).cid(), p.messages(i).name().c_str(), p.messages(j).cid(), p.messages(j).name().c_str(), sf1 ? "" : "o", p.messages(i).mid());
                    else
                      ::fprintf(g, "  \"%d:%s\" -> \"%d:%s\"\t[arrowhead=%snormal]\n", p.messages(i).cid(), p.messages(i).name().c_str(), p.messages(j).cid(), p.messages(j).name().c_str(), sf1 ? "" : "o");
                  }
              }
          }
          else if (opt_graph_interconnected) {
            for (int i = 0; i < p.messages_size(); ++i)
              if (*I == p.messages(i).mid()) {
                for (int j = i + 1; j < p.messages_size(); ++j)
                  if (*I == p.messages(j).mid())
                    if (opt_graph_edge_mid)
                      ::fprintf(g, "  \"%d:%s\" -- \"%d:%s\t[label=\"%d\"]\n", p.messages(i).cid(), p.messages(i).name().c_str(), p.messages(j).cid(), p.messages(j).name().c_str(), p.messages(i).mid());
                    else
                      ::fprintf(g, "  \"%d:%s\" -- \"%d:%s\"\n", p.messages(i).cid(), p.messages(i).name().c_str(), p.messages(j).cid(), p.messages(j).name().c_str());
              }
          }
          else {
            size_t nr = 0;
            int first;

            for (int i = 0; i < p.messages_size(); ++i) {
              if (*I == p.messages(i).mid())
                if (!nr++) {
                  first = i;
                  ::fprintf(g, "  \"%d:%s\"", p.messages(i).cid(), p.messages(i).name().c_str());
                }
                else
                  ::fprintf(g, " -- \"%d:%s\"", p.messages(i).cid(), p.messages(i).name().c_str());
            }

            if (opt_grapth_closed_path && nr > 2)
              if (opt_graph_edge_mid)
                ::fprintf(g, " -- \"%d:%s\"\t[label=\"%d\"]", p.messages(first).cid(), p.messages(first).name().c_str(), p.messages(first).mid());
              else
                ::fprintf(g, " -- \"%d:%s\"", p.messages(first).cid(), p.messages(first).name().c_str());
          }

          ::fprintf(g, "\n");
        }

        ::fprintf(g, "}\n");
        ::fclose(g);
      }
    }
  }

  client.unreg();

  return e == nf::ERR_OK ? 0 : 1;
}

