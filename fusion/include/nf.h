/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_FUSION_H
#define		FUSION_FUSION_H

# include <stdint.h>

namespace nf {
  typedef uint16_t  mtype_t;		// message mode bits
  typedef uint16_t  oflags_t;   // open flags
  typedef uint16_t  sflags_t;   // subscribe flags

  typedef uint16_t	mid_t;			// message descriptor id
  typedef uint16_t  cid_t;			// client id

  typedef uint16_t  uid_t;			// user id
  typedef uint16_t  gid_t;			// group id

  typedef int64_t		msecs_t;		// time stamp in millisecons

  typedef uint16_t  pri_t;			// priority
  typedef uint32_t	seq_t;			// message sequence number
  typedef uint32_t  cmi_t;      // callback method id

  // predefines client ids and masks ///////////////////////////////////////////
  enum /*: cid_t*/ {
    CID_NONE			= cid_t(0x0000),	// none
    CID_GROUP	    = cid_t(0x1000),
    CID_NOSELF		= cid_t(0x2000),	//
    CID_PUB	      = cid_t(0x4000),	// all publicly subscribed clients
    CID_ALL			  = cid_t(0x8000),	//

    CID_CLIENT    = cid_t(0x0FFF),

    CID_SYS	      = cid_t(0x0001),
    CID_SYS_GROUP = cid_t(CID_GROUP | CID_SYS),
  };

  inline bool CID_IS_SYS(cid_t c)           { return c == nf::CID_SYS; }
  inline bool CID_IS_ALL(cid_t c)           { return c == nf::CID_ALL; }
  inline bool CID_IS_ALL_NOSELF(cid_t c)    { return c == cid_t(nf::CID_ALL|nf::CID_NOSELF); }
  inline bool CID_IS_GRP(cid_t c)           { return cid_t(c & nf::CID_GROUP) != CID_NONE; }
  inline bool CID_IS_GRP_NOSELF(cid_t c)    { return c == cid_t(nf::CID_GROUP|nf::CID_NOSELF); }
  inline bool CID_IS_PUB(cid_t c)           { return c == nf::CID_PUB; }
  inline bool CID_IS_PUB_NOSELF(cid_t c)    { return c == cid_t(nf::CID_PUB|nf::CID_NOSELF); }
  inline bool CID_IS_CLIENT(cid_t c)        { return cid_t(c & nf::CID_CLIENT) != CID_NONE; }
  inline bool CID_IS_CLIENT_NOSELF(cid_t c) { return cid_t(c & nf::CID_NOSELF) != CID_NONE && cid_t(c & nf::CID_CLIENT) != CID_NONE; }

  // subscribe options /////////////////////////////////////////////////////////
  enum _sflags_t/*: unsigned*/ {
    SF_NONE				= 0,
    SF_PRIVATE		= 0,      // only explicit sends
    SF_PUBLISH		= 1,      // implicit: explicit + publish
  };

  // callback method masks /////////////////////////////////////////////////////
  enum /*: cmi_t*/ {
    CM_MANUAL     = 1,      // execute callback(s) manualy by calling dispatch()

    // user registered ids will go here:
    // CM_QT,
    // CM_MFC,
    // CM_NET...
  };

  // open flags ////////////////////////////////////////////////////////////////
  enum _oflags_t /*: oflags_t*/ {
    O_RDWR              = 0x0000,	// desired access - publish/post/post/send && subscribe
    O_RDONLY            = 0x0010,	// desired access - subscribe
    O_WRONLY            = 0x0020,	// desired access - publish/post/post/send
    O_CREATE            = 0x0040,	// if the message name does not exist it will be created
    O_EXCL              = 0x0080,	// ensure that this call creates the message name;
                                  // if this flag is specified in conjunction with O_CREAT,
                                  // and message name already exists, then open() will fail.
    O_HINTID            = 0x0100,	// hint to try to use passed mid value
    O_NOATIME           = 0x0200, // do not update access time
    O_TEMPORARY         = 0x0400,	// delete message name on last descriptor closed
    O_EDGE_TRIGGER      = 0x0800, // route only if new value deffers from old;
                                  // message must be MT_EVENT|MT_PERSISTENT

    O_NOTIFY_OPEN       = 0x1000, // notify when number of clients opened this message changes
    O_NOTIFY_SUBSCRIBE  = 0x2000, // notify when number of clients subscribed to this message changes
    O_NOTIFY_CONFIGURE  = 0x4000, // notify when configuration for this message changes

    O_VALIDATE_MASK     = 0x7FF0, // used for parameter validation
  };

  // message types /////////////////////////////////////////////////////////////
  enum _mtype_t /*: mtype_t*/ {
    MT_EVENT            = 0x0000,
    MT_DATA             = 0x0001,
    MT_STREAM           = 0x0002,
    MT_GROUP            = 0x0003,
    MT_CLIENT           = 0x0004,
    MT_TYPE_MASK        = 0x0007,

    // MT_EVENT only
    MT_PERSISTENT       = 0x0008, // event, last value is preserved

    MT_VALIDATE_MASK    = 0x000F, // used for parameter validation
  };

  // message stat //////////////////////////////////////////////////////////////
  struct stat_t {
    mid_t     st_mid;
    unsigned  st_type;
    unsigned  st_nlink;
    //uid_t   st_uid;
    //uid_t   st_gid;
    int       st_size;
    msecs_t   st_atime,           // access
              st_ctime,           // change
              st_mtime;           // meta data
  };

  // message access bits ///////////////////////////////////////////////////////
  // execute permission is undefined ///////////////////////////////////////////
  enum access_t /*: unsigned*/ {
    M_IRWXU             = 00700,	// user (file owner) has read, write and execute permission
    M_IRUSR             = 00400,	// user has read permission
    M_IWUSR             = 00200,	// user has write permission
    M_IXUSR             = 00100,	// user has execute permission
    M_IRWXG             = 00070,	// group has read, write and execute permission
    M_IRGRP             = 00040,	// group has read permission
    M_IWGRP             = 00020,	// group has write permission
    M_IXGRP             = 00010,	// group has execute permission
    M_IRWXO             = 00007,	// others have read, write and execute permission
    M_IROTH             = 00004,	// others have read permission
    M_IWOTH             = 00002,	// others have write permission
    M_IXOTH             = 00001,	// others have execute permission
  };

  // error codes ///////////////////////////////////////////////////////////////
  enum result_t/*: uint16_t*/ {
    ERR_OK = 0,
    ERR_REGISTERED,						    // client is registered/or un registered

    ERR_CONFIGURATION,            // bad/malformed configuration
    ERR_CONFIGURATION_LOCK,       // can not acquire configuration lock

    ERR_SUBSCRIBED,					      // already subscribed

    ERR_READONLY,							    //
    ERR_WRITEONLY,						    //

    ERR_SUBSCRIBERS,					    // no subscribers for given message
    ERR_CLIENT,								    // no client
    ERR_GROUP,								    // no group

    ERR_PERMISSION,						    // no permission for operation
    ERR_MESSAGE,							    // /message/ not found
    ERR_MESSAGE_SIZE, 				    // /message/ size mismatch
    ERR_ALREADY_EXIST,				    // /message/ already exists
    ERR_MESSAGE_TYPE,					    // /message/ type is wrong
    ERR_MESSAGE_NAME,			        // /message/ name is invalid
    ERR_MESSAGE_FORMAT,				    //
    ERR_OPEN,									    // /message/ already open, or was not open

    ERR_TOO_MANY_CLIENTS,			    //
    ERR_TOO_MANY_GROUPS,			    //

    ERR_TRUNCATED,						    // data truncated
    ERR_MEMORY,								    //

//  ERR_MULTI_ACK,			          // message with multiple destinations can not be acknowledged
    ERR_INVALID_DESTINATION,      //
    ERR_INVALID_SOURCE,           //

    ERR_VERSION,	                //

    ERR_CONNECTION,		            // connection
    ERR_TIMEOUT,		              // timeout
    ERR_PARAMETER,						    // invalid argument(s)
    ERR_IMPLEMENTED,					    // not implemented
    ERR_INITIALIZED,					    // initialized, not initialized

    ERR_IO,								        // read/write error
    ERR_WIN32,								    // win32 error

    ERR_UNEXPECTED,						    // unexpected

    ERR_CONTEXT,                  // reply not from callback, e.t.c.
    //ERR_NOT_REQUEST,              // not a request reply
    //ERR_NOT_REPLY,                // not a request reply
    //ERR_ALREADY_REPLIED,          //
    ERR_IGNORE,                   // reply not from callback

    ERR_USER  = 1024,             // user-defined message start here
  };
}

#endif	//FUSION_FUSION_Hdir
