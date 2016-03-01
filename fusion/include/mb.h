/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_MB_H
#define		FUSION_MB_H

#include "include/nf.h"
#include "include/configure.h"
#include "include/version.h"

class mcb_sysinfo_request;
class mcb_sysinfo_reply;

namespace nf {
  // forward declarations
  struct internal_client_t;
  struct mcb_t;

  typedef result_t (__stdcall *callback_t)(mid_t mid, size_t len, const void *data);
  typedef result_t (__stdcall *callback_method_t)(callback_t, void* cookie, mid_t mid, size_t len, const void *data);

  struct client_t { ////////////////////////////////////////////////////////////
    FUSION_EXPORT FUSION_CALLING_CONVENTION client_t(
      FUSION_IN const char* name); /*user friendly, optional, non-unique name/description*/

    FUSION_EXPORT FUSION_CALLING_CONVENTION ~client_t();

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION reg_callback_method(
      FUSION_IN     callback_method_t,    // callback function
      FUSION_IN     void*,                // user cookie
      FUSION_INOUT  cmi_t&);              // callback method id
    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION unreg_callback_method(
      FUSION_IN     cmi_t);               // callback method id returned from reg_callback_method

    FUSION_EXPORT const mcb_t* FUSION_CALLING_CONVENTION get_mcb();    // 0 if called outside of callback

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION reg(              // register/connect client
      FUSION_IN const char* connection,   // connection string
      FUSION_IN const char* profile);     // profile

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION reg(              // register/connect client
      FUSION_IN const char* connection,   // connection string
      FUSION_IN size_t profiles_nr,       // number of profiles
      FUSION_IN const char* profile, ...);// first profile

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION reg(              // register/connect client
      FUSION_IN const char* connection,   // connection string
      FUSION_IN size_t profiles_nr,       // number of profiles
      FUSION_IN const char** profilevec); // profiles

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION unreg();          // unregister/disconnect, if connected
    FUSION_EXPORT void FUSION_CALLING_CONVENTION terminate();

    ////////////////////////////////////////////////////////////////////////////

    FUSION_EXPORT bool FUSION_CALLING_CONVENTION registered();         // true if successfully registered/connected
    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION dispatch(bool process_all_messages); // dispatch pending messages to user callbacks (non blocking)
    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION dispatch(size_t timeout_msecs, bool process_all_messages); // dispatch pending messages to user callbacks (blocking)

    // LEVEL 0: SIMPLIFIED API /////////////////////////////////////////////////

    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION read_int    (FUSION_IN const char*, FUSION_OUT int&);
    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION read_double (FUSION_IN const char*, FUSION_OUT double&);
    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION read_bool   (FUSION_IN const char*, FUSION_OUT bool&);
    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION read_string (FUSION_IN const char*, FUSION_INOUT size_t&, char*);
    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION read_blob   (FUSION_IN const char*, FUSION_INOUT size_t&, void*);

    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION write_int   (FUSION_IN const char*, FUSION_IN int);
    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION write_double(FUSION_IN const char*, FUSION_IN double);
    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION write_bool  (FUSION_IN const char*, FUSION_IN bool);
    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION write_string(FUSION_IN const char*, FUSION_IN size_t, const char*);
    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION write_blob  (FUSION_IN const char*, FUSION_IN size_t, const void*);

    // LEVEL I /////////////////////////////////////////////////////////////////

    FUSION_EXPORT cid_t FUSION_CALLING_CONVENTION id() const;          // after client successfully registered

    FUSION_EXPORT const char* FUSION_CALLING_CONVENTION name() const;  // name, as provided during initialization

    // clients /////////////////////////////////////////////////////////////////
    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION cquery(// query registered clients
      FUSION_INOUT size_t& size,        // buffer size: if 0 is input, then call just returns required size
      FUSION_OUT size_t& nr,            // number of names returned
      FUSION_OUT const char**& names);  // array of names

    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION cquery(// query registered clients
      FUSION_INOUT size_t& len,         // buffer size: if 0 is input, then call just returns required size
      FUSION_OUT const char*& names,    // buffer that holds clients separated by "sep"
      FUSION_IN const char* sep);	      // separator string

    FUSION_EXPORT FUSION_DEPRECATRED cid_t FUSION_CALLING_CONVENTION id(// get used or group id, CID_NONE if error
      FUSION_IN const char* name);		  // user or group name

    // messages ////////////////////////////////////////////////////////////////
    //  similarily to fs...
    //  mostly forwards to mdm with user credentials
    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION mcreate(          // create message descriptor
      FUSION_IN const char* msg,        // message name (fully qualified or short)
      FUSION_IN oflags_t flags,         // O_RDONLY, O_WRONLY, O_RDWR, O_CREATE(implied), O_EXCL, O_NOATIME, O_HINTID
      FUSION_IN mtype_t mode,           // MT_EVENT, MT_STREAM
      FUSION_OUT mid_t&,                // out
      FUSION_IN size_t size);           // in; -1 means variable size

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION mcreate(          // create message descriptor
      FUSION_IN const char* msg,        // message name (fully qualified or short)
      FUSION_IN oflags_t flags,         // O_RDONLY, O_WRONLY, O_RDWR, O_CREATE(implied), O_EXCL, O_NOATIME, O_HINTID
      FUSION_IN mtype_t mode,           // MT_EVENT, MT_STREAM
      FUSION_OUT mid_t&,                // out
      FUSION_IN size_t size,            // in; -1 means variable size
      FUSION_IN size_t len,             // MT_PERSISTENT: length of data
      FUSION_IN const void* data);      // MT_PERSISTENT: data

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION mopen(            // open message descriptor
      FUSION_IN const char* msg,        // message name (fully qualified or short)
      FUSION_IN oflags_t flags,         // O_RDONLY, O_WRONLY, O_RDWR, O_EXCL, O_NOATIME, O_HINTID
      FUSION_OUT mtype_t& mode,         // out, as was created: MT_EVENT, MT_STREAM, etc
      FUSION_OUT mid_t&,                // out
      FUSION_OUT size_t& size);         // out; -1 means variable size

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION mclose( ///////////// close opened message descriptor
      FUSION_IN mid_t m);

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION munlink(          // unlink/remove message name
      FUSION_IN const char* msg);

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION munlink(          // unlink/remove message name
      FUSION_IN mid_t msg);

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION mlink(            // create link
      FUSION_IN const char* from,
      FUSION_IN const char* to);

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION mmove(            // move/rename message name
      FUSION_IN const char* msg);

    // messages: query
    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION mlist(// list/query message names
      FUSION_IN const char* profile,    // 0 - default
      FUSION_IN const char* mask,       // 0 - means "*.*
      FUSION_OUT size_t& names_nr,      // number of names returned
      FUSION_OUT char**& names);		    // array of names; must be freed by user: free((void*)names)
                                        // access: name = names[i]

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION mstat(             // stat message by name
      FUSION_IN const char* msg,        // message name
      FUSION_OUT int& data);            // TBD

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION mstat(             // stat message by descriptor
      FUSION_IN mid_t m,                // message descriptor
      FUSION_OUT int& data);            // TBD

    // messages: attributes
    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION mattr_read( // read message attribute
      FUSION_IN mid_t m,                // message descriptor; must be opened for TBD
      FUSION_IN const char* key,        // attribute key
      FUSION_INOUT size_t& len,         // attribute value length; if 0 is input, then call just returns required size
      FUSION_IN char* value);           // pointer to buffer

    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION mattr_write(// write/overwrite message attribute
      FUSION_IN mid_t m,                // message descriptor; must be opened for TBD
      FUSION_IN const char* key,        // attribute key
      FUSION_IN char* value);           // pointer to value

    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION mattrs_read(// read all message attributes in 'key=val' format
      FUSION_IN mid_t m,                // message descriptor; must be opened for TBD
      FUSION_INOUT size_t& size,        // buffer size; if 0 is input, then call just returns required size
      FUSION_INOUT size_t& nr,          // number of attributes pairs returned
      FUSION_OUT char**& pairs);       	// pointer to buffer

    FUSION_EXPORT FUSION_DEPRECATRED result_t FUSION_CALLING_CONVENTION mattrs_write(// rewrite all message attributes in 'key=val' format
      FUSION_IN mid_t m,                // message descriptor; must be opened for TBD
      FUSION_INOUT size_t& nr,          // number of attributes pairs
      FUSION_OUT char** pairs);       	// pointer to pairs

    // subscription ////////////////////////////////////////////////////////////
    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION subscribe(
      FUSION_IN mid_t m,                // message descriptor, must be opened for reading
      FUSION_IN int flags,              // SF_NONE, SF_PRIVATE, SF_PUBLISH
      FUSION_IN cmi_t cmi_mask,         // CM_MANUAL, or user registered ids
      FUSION_IN callback_t);            // callback function

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION unsubscribe(FUSION_IN mid_t);

    // post/send ///////////////////////////////////////////////////////////////
    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION publish( // post to all subscribed
      FUSION_IN mid_t m,                // message descriptor, must be opened for reading
      FUSION_IN size_t len,             // payload length
      FUSION_IN const void* data);      // pointer to payload; can be 0, if length is 0

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION post(    // post to ALL or GROUP or INDIVIDUAL
      FUSION_IN mid_t m,                // message descriptor, must be opened for reading
      FUSION_IN cid_t c,                // destination
      FUSION_IN size_t len,             // payload length
      FUSION_IN const void* data);	    // pointer to payload; can be 0, if length is 0

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION send(    // post with acknowledgement
      FUSION_IN mid_t m,                // message descriptor, must be opened for reading
      FUSION_IN cid_t c,                // destination
      FUSION_IN size_t len,             // payload length
      FUSION_IN const void* data,	      // pointer to payload; can be 0, if length is 0
      uint32_t timeout);	              // acknowledgement timeout msecs; if acknowledgement comes after timeout, it is dropped

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION request( // blocks till receives REPLY or error
      FUSION_IN mid_t m,                // message descriptor, must be opened for reading
      FUSION_IN cid_t c,                // destination, cannot be all, or group, etc.
      FUSION_IN size_t len,             // payload length
      FUSION_IN const void* data,       // pointer to payload; can be 0, if length is 0
      FUSION_OUT mid_t& rep_m,          // reply message descriptor
      FUSION_INOUT size_t& rep_len,     // reply size;
      FUSION_OUT const void*& rep_data, // reply buffer pointer
      uint32_t timeout);								// acknowledgement timeout msecs; if acknowledgement comes after timeout, it is dropped

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION send(   // low-level primitive that implements all above forms
      FUSION_IN mcb_t* mcb);            // message control block

    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION reply( // send reply from callback, non-blocking
      FUSION_IN mid_t m,                // reply message descriptor
      FUSION_IN size_t len,             // reply payload length
      FUSION_IN const void* data);      // reply payload

    // Info
    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION sysinfo(
      FUSION_IN const mcb_sysinfo_request& req, // request
      FUSION_INOUT mcb_sysinfo_reply& rep,      // reply
      FUSION_IN unsigned timeout = 0);          // timeout

    // For use within callbacks - calls get_mcb, and returns the timestamp of
    //  the mcb.  Used by .net wrapper, to avoid marshaling the entire struct.
    //  Timestamp is ticks (100ns intervals) since Jan. 1, 1601 (UTC).
    FUSION_EXPORT result_t FUSION_CALLING_CONVENTION get_timestamp(
      FUSION_OUT msecs_t* msg_timestamp);       // timestamp of current message

    // on XXX handlers
    FUSION_EXPORT void FUSION_CALLING_CONVENTION set_on_connect_handler(
      FUSION_IN void (FUSION_CALLING_CONVENTION *callback)());                // user-supplied callback
    FUSION_EXPORT void FUSION_CALLING_CONVENTION set_on_disconnect_handler(
      FUSION_IN void (FUSION_CALLING_CONVENTION *callback)());                // user-supplied callback

  private:
    internal_client_t&	pimp_;
    result_t _mopen(const char* name, int oflags, int mode, mid_t& mid, size_t& size, bool have_size);
  };

  FUSION_EXPORT const version_t& FUSION_CALLING_CONVENTION version(); // client library version

  FUSION_EXPORT const char* FUSION_CALLING_CONVENTION result_to_str(
    FUSION_IN result_t rc);           // error code

	// .NET helpers ////////////////////////////////////////////////////////////
	FUSION_EXPORT client_t* FUSION_CALLING_CONVENTION _create_client(
		FUSION_IN const char* name);

	FUSION_EXPORT void FUSION_CALLING_CONVENTION _delete_client(
		FUSION_IN client_t* client);
}

#endif		//FUSION_MB_H
