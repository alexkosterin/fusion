using System;
using System.Text;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Diagnostics;

namespace csmb {
  [UnmanagedFunctionPointer(CallingConvention.StdCall)]
  public delegate mb.result_t callback_void(ushort mid, uint len, ref short data); //@@

  [UnmanagedFunctionPointer(CallingConvention.StdCall)]
  public delegate mb.result_t callback_bool(ushort mid, uint len, ref bool data);

  [UnmanagedFunctionPointer(CallingConvention.StdCall)]
  public delegate mb.result_t callback_int(ushort mid, uint len, ref int data);

  [UnmanagedFunctionPointer(CallingConvention.StdCall)]
  public delegate mb.result_t callback_double(ushort mid, uint len, ref double data);

  [UnmanagedFunctionPointer(CallingConvention.StdCall)]
  public delegate mb.result_t callback_string(ushort mid, uint len, [MarshalAs(UnmanagedType.LPStr)] string data);

  [UnmanagedFunctionPointer(CallingConvention.StdCall)]
  public delegate mb.result_t method_callback(IntPtr _cb_ignore_, IntPtr cookie, ushort mid, uint len, IntPtr data);

  public class mb {
    //typedef uint16_t  mtype_t;		// message mode bits
    //typedef uint16_t  oflags_t;   // open flags
    //typedef uint16_t  sflags_t;   // subscribe flags

    //typedef uint16_t	mid_t;			// message descriptor id
    //typedef uint16_t  cid_t;			// client id

    //typedef uint16_t  uid_t;			// user id
    //typedef uint16_t  gid_t;			// group id

    //typedef int64_t		msecs_t;		// time stamp in millisecons

    //typedef uint16_t  pri_t;			// priority
    //typedef uint32_t	seq_t;			// message sequence number
    //typedef uint32_t  cmi_t;      // callback method id

    // Note:
    // uint16_t => ushort
    // uint32_t => uint
    // int64_t  => ulong

    // <<_callbacks map>> is used to keep callbacks from being garbage collected.
    private static Dictionary<ushort, object> _manualCallbacks = new Dictionary<ushort, object>();
    private static Dictionary<uint, object> _automaticCallbacks = new Dictionary<uint, object>();

    private static void _remember_callback(ushort mid, object cb) {
      _manualCallbacks.Add(mid, cb);
    }

    private static void _forget_callback(ushort mid) {
      _manualCallbacks.Remove(mid);
    }

    private static void _remember_callback(uint mask, object cb) {
      _automaticCallbacks.Add(mask, cb);
    }

    private static void _forget_callback(uint mask) {
      _automaticCallbacks.Remove(mask);
    }

    private static void _forget_callbacks() {
      _manualCallbacks.Clear();
      _automaticCallbacks.Clear();
    }

    public enum _cid_t
    {
      CID_NONE			= 0x0000,	// none
      CID_GROUP	    = 0x1000,
      CID_NOSELF		= 0x2000,	//
      CID_PUB	      = 0x4000,	// all publicly subscribed clients
      CID_ALL			  = 0x8000,	//

      CID_CLIENT    = 0x0FFF,

      CID_SYS	      = 0x0001,
      CID_SYS_GROUP = CID_GROUP | CID_SYS,
    };

    // subscribe options /////////////////////////////////////////////////////////
    public enum _sflags_t {
      SF_NONE				= 0,
      SF_PRIVATE		= 0,      // only explicit sends
      SF_PUBLISH		= 1,      // implicit: explicit + publish
    };

    // callback method masks /////////////////////////////////////////////////////
    public enum _cmi_t {
      CM_MANUAL     = 1,      // execute callback(s) manualy by calling dispatch()
   };

    // open flags ////////////////////////////////////////////////////////////////
    public enum _oflags_t {
      O_RDWR = 0x0000,	        // desired access - publish/post/post/send && subscribe
      O_RDONLY = 0x0010,	      // desired access - subscribe
      O_WRONLY = 0x0020,	      // desired access - publish/post/post/send
      O_CREATE = 0x0040,	      // if the message name does not exist it will be created
      O_EXCL = 0x0080,	        // ensure that this call creates the message name;
                                // if this flag is specified in conjunction with O_CREAT,
                                // and message name already exists, then open() will fail.
      O_HINTID = 0x0100,	      // hint to try to use passed mid value
      O_NOATIME = 0x0200,       // do not update access time
      O_TEMPORARY = 0x0400,	    // delete message name on last descriptor closed
      O_EDGE_TRIGGER = 0x0800,  // route only if new value deffers from old;
                                // message must be MT_EVENT|MT_PERSISTENT

      O_NOTIFY_OPEN = 0x1000,   // notify when number of clients opened this message changes
      O_NOTIFY_SUBSCRIBE = 0x2000, // notify when number of clients subscribed to this message changes
      O_NOTIFY_CONFIGURE = 0x4000, // notify when configuration for this message changes

      O_VALIDATE_MASK = 0x7FF0, // used for parameter validation
    };

  // message types /////////////////////////////////////////////////////////////
    public enum _mtype_t /*: mtype_t*/ {
      MT_EVENT = 0x0000,
      MT_STREAM = 0x0001,
      MT_GROUP = 0x0002,
      MT_CLIENT = 0x0003,
      MT_TYPE_MASK = 0x0003,

      // MT_EVENT only
      MT_PERSISTENT = 0x0008, // event, last value is preserved

      MT_VALIDATE_MASK = 0x000F, // used for parameter validation
    };

    public enum result_t {
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
      ERR_MESSAGE_INVALID,			    // /message/ name is invalid
      ERR_MESSAGE_FORMAT,				    //
      ERR_OPEN,									    // /message/ already open, or was not open

      ERR_TOO_MANY_CLIENTS,			    //
      ERR_TOO_MANY_GROUPS,			    //

      ERR_TRUNCATED,						    // data truncated
      ERR_MEMORY,								    //

      ERR_MULTI_ACK,			          // message with multiple destinations can not be acknowledged
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

      ERR_USER = 1024,              // user-defined messages start here
    };

    [StructLayout (LayoutKind.Sequential)]
    public struct mcb_t {           // message control block: wire transfered
      ushort mid_;							    // message id/topic/channel etc
      ushort src_;							    // message source
      ushort dst_;							    // message destination: (ALL|SUB|GRP|CID)|NOSELF
      uint   seq_;							    // message sequence number, generated on originsator side
      public long   org_;							    // timestamp message created

      // request & reply; note: message can be both request and reply
      uint req_seq_;					      	// is a reply if != 0
      bool request_;				            // is a request if true
    };

    public class client_t: IDisposable {
      private IntPtr client_;

      // general
      [DllImport("mb.dll", EntryPoint = "?_create_client@nf@@YGPAUclient_t@1@PBD@Z", ExactSpelling = true, CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
      private static extern IntPtr _create([MarshalAs(UnmanagedType.LPStr)] string name);

      [DllImport("mb.dll", EntryPoint = "?_delete_client@nf@@YGXPAUclient_t@1@@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern void _delete(IntPtr client);

      [DllImport("mb.dll", EntryPoint = "?name@client_t@nf@@QBGPBDXZ", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern IntPtr _name(IntPtr client);

      [DllImport("mb.dll", EntryPoint = "?id@client_t@nf@@QBGGXZ", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern ushort _id(IntPtr clent);

      // non blocking callback
      [DllImport("mb.dll", EntryPoint = "?reg_callback_method@client_t@nf@@QAG?AW4result_t@2@P6G?AW432@P6G?AW432@GIPBX@ZPAXGI0@Z2AAI@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _reg_callback_method(IntPtr clent, method_callback callback, IntPtr cookie, out uint mask);

      [DllImport("mb.dll", EntryPoint = "?unreg_callback_method@client_t@nf@@QAG?AW4result_t@2@I@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _unreg_callback_method(IntPtr clent, uint mask);

      [DllImport("mb.dll", EntryPoint = "?get_mcb@client_t@nf@@QAGPBUmcb_t@2@XZ", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern mcb_t _get_mcb(IntPtr clent);

      // register
      [DllImport("mb.dll", EntryPoint = "?reg@client_t@nf@@QAG?AW4result_t@2@PBD0@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _reg(IntPtr clent, [MarshalAs(UnmanagedType.LPStr)] string connect, [MarshalAs(UnmanagedType.LPStr)] string profile);

      [DllImport("mb.dll", EntryPoint = "?reg@client_t@nf@@QAA?AW4result_t@2@PBDI0ZZ", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
      private static extern result_t _reg2(IntPtr clent, [MarshalAs(UnmanagedType.LPStr)] string connect, uint profiles_nr, [MarshalAs(UnmanagedType.LPStr)] string profile, [MarshalAs(UnmanagedType.LPStr)] string profile2);

      [DllImport("mb.dll", EntryPoint = "?reg@client_t@nf@@QAA?AW4result_t@2@PBDI0ZZ", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
      private static extern result_t _reg3(IntPtr clent, [MarshalAs(UnmanagedType.LPStr)] string connect, uint profiles_nr, [MarshalAs(UnmanagedType.LPStr)] string profile, [MarshalAs(UnmanagedType.LPStr)] string profile2, [MarshalAs(UnmanagedType.LPStr)] string profile3);

      [DllImport("mb.dll", EntryPoint = "?unreg@client_t@nf@@QAG?AW4result_t@2@XZ", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _unreg(IntPtr clent);

      // mcreate/mopen/mclose/munlink
      [DllImport("mb.dll", EntryPoint = "?mcreate@client_t@nf@@QAG?AW4result_t@2@PBDGGAAGI@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _mcreate(IntPtr clent, [MarshalAs(UnmanagedType.LPStr)] string name, ushort flags, ushort mode, out ushort mid, int size);

      // next 4 create and initialize (persistent tag)
      [DllImport("mb.dll", EntryPoint = "?mcreate@client_t@nf@@QAG?AW4result_t@2@PBDGGAAGIIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _mcreate(IntPtr clent, [MarshalAs(UnmanagedType.LPStr)] string name, ushort flags, ushort mode, out ushort mid, int size, int len, ref bool val);

      [DllImport("mb.dll", EntryPoint = "?mcreate@client_t@nf@@QAG?AW4result_t@2@PBDGGAAGIIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _mcreate(IntPtr clent, [MarshalAs(UnmanagedType.LPStr)] string name, ushort flags, ushort mode, out ushort mid, int size, int len, ref int val);

      [DllImport("mb.dll", EntryPoint = "?mcreate@client_t@nf@@QAG?AW4result_t@2@PBDGGAAGIIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _mcreate(IntPtr clent, [MarshalAs(UnmanagedType.LPStr)] string name, ushort flags, ushort mode, out ushort mid, int size, int len, ref double val);

      [DllImport("mb.dll", EntryPoint = "?mcreate@client_t@nf@@QAG?AW4result_t@2@PBDGGAAGIIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _mcreate(IntPtr clent, [MarshalAs(UnmanagedType.LPStr)] string name, ushort flags, ushort mode, out ushort mid, int size, int len, [MarshalAs(UnmanagedType.LPStr)] string val);


      [DllImport("mb.dll", EntryPoint = "?mopen@client_t@nf@@QAG?AW4result_t@2@PBDGAAG1AAI@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _mopen(IntPtr clent, [MarshalAs(UnmanagedType.LPStr)] string name, ushort flags, out ushort mode, out ushort mid, out int size);

      [DllImport("mb.dll", EntryPoint = "?mclose@client_t@nf@@QAG?AW4result_t@2@G@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _mclose(IntPtr clent, ushort mid);

      [DllImport("mb.dll", EntryPoint = "?munlink@client_t@nf@@QAG?AW4result_t@2@G@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _munlink(IntPtr clent, ushort mid);

      [DllImport("mb.dll", EntryPoint = "?munlink@client_t@nf@@QAG?AW4result_t@2@PBD@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _munlink(IntPtr clent, [MarshalAs(UnmanagedType.LPStr)] string name);

      // subscribe
      [DllImport("mb.dll", EntryPoint = "?subscribe@client_t@nf@@QAG?AW4result_t@2@GHIP6G?AW432@GIPBX@Z@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _subscribe(IntPtr clent, ushort mid, int flags, uint cmi_mask, callback_void cb);

      [DllImport("mb.dll", EntryPoint = "?subscribe@client_t@nf@@QAG?AW4result_t@2@GHIP6G?AW432@GIPBX@Z@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _subscribe(IntPtr clent, ushort mid, int flags, uint cmi_mask, callback_bool cb);

      [DllImport("mb.dll", EntryPoint = "?subscribe@client_t@nf@@QAG?AW4result_t@2@GHIP6G?AW432@GIPBX@Z@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _subscribe(IntPtr clent, ushort mid, int flags, uint cmi_mask, callback_int cb);

      [DllImport("mb.dll", EntryPoint = "?subscribe@client_t@nf@@QAG?AW4result_t@2@GHIP6G?AW432@GIPBX@Z@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _subscribe(IntPtr clent, ushort mid, int flags, uint cmi_mask, callback_double cb);

      [DllImport("mb.dll", EntryPoint = "?subscribe@client_t@nf@@QAG?AW4result_t@2@GHIP6G?AW432@GIPBX@Z@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _subscribe(IntPtr clent, ushort mid, int flags, uint cmi_mask, callback_string cb);

      [DllImport("mb.dll", EntryPoint = "?unsubscribe@client_t@nf@@QAG?AW4result_t@2@G@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _unsubscribe(IntPtr clent, ushort mid);

      // publish
      [DllImport("mb.dll", EntryPoint = "?publish@client_t@nf@@QAG?AW4result_t@2@GIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _publish(IntPtr clent, ushort mid, uint len, IntPtr ptr);

      [DllImport("mb.dll", EntryPoint = "?publish@client_t@nf@@QAG?AW4result_t@2@GIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _publish(IntPtr clent, ushort mid, uint len, ref bool data);

      [DllImport("mb.dll", EntryPoint = "?publish@client_t@nf@@QAG?AW4result_t@2@GIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _publish(IntPtr clent, ushort mid, uint len, ref int data);

      [DllImport("mb.dll", EntryPoint = "?publish@client_t@nf@@QAG?AW4result_t@2@GIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _publish(IntPtr clent, ushort mid, uint len, ref double data);

      [DllImport("mb.dll", EntryPoint = "?publish@client_t@nf@@QAG?AW4result_t@2@GIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _publish(IntPtr clent, ushort mid, uint len, [MarshalAs(UnmanagedType.LPStr)] string data);

      // post
      [DllImport("mb.dll", EntryPoint = "?post@client_t@nf@@QAG?AW4result_t@2@GGIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _post(IntPtr clent, ushort mid, ushort cid, uint len, IntPtr ptr);

      [DllImport("mb.dll", EntryPoint = "?post@client_t@nf@@QAG?AW4result_t@2@GGIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _post(IntPtr clent, ushort mid, ushort cid, uint len, ref bool data);

      [DllImport("mb.dll", EntryPoint = "?post@client_t@nf@@QAG?AW4result_t@2@GGIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _post(IntPtr clent, ushort mid, ushort cid, uint len, ref int data);

      [DllImport("mb.dll", EntryPoint = "?post@client_t@nf@@QAG?AW4result_t@2@GGIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _post(IntPtr clent, ushort mid, ushort cid, uint len, ref double data);

      [DllImport("mb.dll", EntryPoint = "?post@client_t@nf@@QAG?AW4result_t@2@GGIPBX@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _post(IntPtr clent, ushort mid, ushort cid, uint len, [MarshalAs(UnmanagedType.LPStr)] string data);

      // dispatch
      [DllImport("mb.dll", EntryPoint = "?dispatch@client_t@nf@@QAG?AW4result_t@2@I_N@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _dispatch(IntPtr clent, uint max_timeout_msecs, bool all);

      [DllImport("mb.dll", EntryPoint = "?dispatch@client_t@nf@@QAG?AW4result_t@2@_N@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _dispatch(IntPtr clent, bool all);

      // timestamp
      [DllImport("mb.dll", EntryPoint = "?get_timestamp@client_t@nf@@QAG?AW4result_t@2@PA_J@Z", ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
      private static extern result_t _get_timestamp(IntPtr client, out long timestamp);

      public client_t(string name) {
        try {
          client_ = _create(name);
        }
        catch (Exception e) {
          Debug.Assert(false, "Constructor threw exception.", e.ToString());
        }
      }

      public void Dispose() {
        try {
          _delete(client_);
        }
        catch (Exception e) {
          Debug.Assert(false, "Dispose() threw exception.", e.ToString());
        }

        client_ = IntPtr.Zero;
      }

      public string name() {
        try {
          return Marshal.PtrToStringAnsi(_name(client_));
        }
        catch (Exception e) {
          Debug.Assert(false, "name() threw exception.", e.ToString());

          return null;
        }
      }

      public result_t dispatch(uint max_timeout_msecs, bool all) {
        try {
          return _dispatch(client_, max_timeout_msecs, all);
        }
        catch (Exception e) {
          Debug.Assert(false, "dispatch() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t dispatch(bool all) {
        try {
          return _dispatch(client_, all);
        }
        catch (Exception e) {
          Debug.Assert(false, "dispatch() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public uint id() {
        try {
          return _id(client_);
        }
        catch (Exception e) {
          Debug.Assert(false, "id() threw exception.", e.ToString());

          return (uint)_cid_t.CID_NONE;
        }
      }

      public result_t reg(string connect, string profile, string profile2) {
        try {
          return _reg2(client_, connect, 2, profile, profile2);
        }
        catch (Exception e) {
          Debug.Assert(false, "reg() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t reg(string connect, string profile, string profile2, string profile3) {
        try {
          return _reg3(client_, connect, 3, profile, profile2, profile3);
        }
        catch (Exception e) {
          Debug.Assert(false, "reg() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t reg(string connect, string profile) {
        try {
          return _reg(client_, connect, profile);
        }
        catch (Exception e) {
          Debug.Assert(false, "reg() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t unreg() {
        try {
          _forget_callbacks();

          return _unreg(client_);
        }
        catch (Exception e) {
          Debug.Assert(false, "unreg() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t reg_callback_method(method_callback callback, IntPtr cookie, out uint mask) {
        mask = 0;
        result_t result = result_t.ERR_OK;

        try {
            result = _reg_callback_method(client_, callback, cookie, out mask);
            if (result == result_t.ERR_OK) {
                _remember_callback(mask, callback);
            }
            return result;
        }
        catch (Exception e) {
          Debug.Assert(false, "reg_callback_method() threw exception.", e.ToString());

          return result;
        }
      }

      public result_t unreg_callback_method(uint mask) {
        result_t result = result_t.ERR_OK;
        try {
            result  = _unreg_callback_method(client_, mask);
            if (result == result_t.ERR_OK) {
                _forget_callback(mask);
            }
            return result;
        }
        catch (Exception e) {
          Debug.Assert(false, "unreg_callback_method() threw exception.", e.ToString());

          return result;
        }
      }

      public result_t mcreate(string name, ushort flags, ushort mode, out ushort mid, int size, int _dummy_) {
        try {
          return _mcreate(client_, name, flags, mode, out mid, size);
        }
        catch (Exception e) {
          Debug.Assert(false, "mcreate() threw exception.", e.ToString());

          mid = 0;

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t mcreate(string name, ushort flags, ushort mode, out ushort mid, bool val) {
        try {
          return _mcreate(client_, name, flags, mode, out mid, 1, 1, ref val);
        }
        catch (Exception e) {
          Debug.Assert(false, "mcreate() threw exception.", e.ToString());

          mid = 0;

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t mcreate(string name, ushort flags, ushort mode, out ushort mid, int val) {
        try {
          return _mcreate(client_, name, flags, mode, out mid, 4, 4, ref val);
        }
        catch (Exception e) {
          Debug.Assert(false, "mcreate() threw exception.", e.ToString());

          mid = 0;

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t mcreate(string name, ushort flags, ushort mode, out ushort mid, double val) {
        try {
          return _mcreate(client_, name, flags, mode, out mid, 8, 8, ref val);
        }
        catch (Exception e) {
          Debug.Assert(false, "mcreate() threw exception.", e.ToString());

          mid = 0;

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t mcreate(string name, ushort flags, ushort mode, out ushort mid, string val) {
        try {
          return _mcreate(client_, name, flags, mode, out mid, -1, val.Length + 1, val);
        }
        catch (Exception e) {
          Debug.Assert(false, "mcreate() threw exception.", e.ToString());

          mid = 0;

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t mopen(string name, ushort flags, out ushort mode, out ushort mid, out int size) {
        try {
          return _mopen(client_, name, flags, out mode, out mid, out size);
        }
        catch (Exception e) {
          Debug.Assert(false, "mopen() threw exception.", e.ToString());

          mode  = 0;
          mid   = 0;
          size  = 0;

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t mclose(ushort mid) {
        try {
          return _mclose(client_, mid);
        }
        catch (Exception e) {
          Debug.Assert(false, "mclose() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t munlink(string name) {
        try {
          return _munlink(client_, name);
        }
        catch (Exception e) {
          Debug.Assert(false, "munlink() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t munlink(ushort mid) {
        try {
          return _munlink(client_, mid);
        }
        catch (Exception e) {
          Debug.Assert(false, "munlink() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t subscribe(ushort mid, int flags, uint mask, callback_void cb) {
        try {
          result_t e = _subscribe(client_, mid, flags, mask, cb);

          if (e == result_t .ERR_OK)
            _remember_callback(mid, cb);

          return e;
        }
        catch (Exception e) {
          Debug.Assert(false, "subscribe() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t subscribe(ushort mid, int flags, uint mask, callback_bool cb) {
        try {
          result_t e = _subscribe(client_, mid, flags, mask, cb);

          if (e == result_t.ERR_OK)
            _remember_callback(mid, cb);

          return e;
        }
        catch (Exception e) {
          Debug.Assert(false, "subscribe() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t subscribe(ushort mid, int flags, uint mask, callback_int cb) {
        try {
          result_t e = _subscribe(client_, mid, flags, mask, cb);

          if (e == result_t.ERR_OK)
            _remember_callback(mid, cb);

          return e;
        }
        catch (Exception e) {
          Debug.Assert(false, "subscribe() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t subscribe(ushort mid, int flags, uint mask, callback_double cb) {
        try {
          result_t e = _subscribe(client_, mid, flags, mask, cb);

          if (e == result_t.ERR_OK)
            _remember_callback(mid, cb);

          return e;
        }
        catch (Exception e) {
          Debug.Assert(false, "subscribe() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t subscribe(ushort mid, int flags, uint mask, callback_string cb) {
        try {
          result_t e = _subscribe(client_, mid, flags, mask, cb);

          if (e == result_t.ERR_OK)
            _remember_callback(mid, cb);

          return e;
        }
        catch (Exception e) {
          Debug.Assert(false, "subscribe() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t unsubscribe(ushort mid) {
        try {
          result_t e = _unsubscribe(client_, mid);

          if (e == result_t.ERR_OK)
            _forget_callback(mid);

          return e;
        }
        catch (Exception e) {
          Debug.Assert(false, "unsubscribe() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t publish(ushort mid) {
        try {
          return _publish(client_, mid, 0, IntPtr.Zero);
        }
        catch (Exception e) {
          Debug.Assert(false, "publish() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t publish(ushort mid, bool data) {
        try {
          return _publish(client_, mid, 1, ref data);
        }
        catch (Exception e) {
          Debug.Assert(false, "publish() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t publish(ushort mid, int data) {
        try {
          return _publish(client_, mid, 4, ref data);
        }
        catch (Exception e) {
          Debug.Assert(false, "publish() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
     }

      public result_t publish(ushort mid, double data) {
        try {
          return _publish(client_, mid, 8, ref data);
        }
        catch (Exception e) {
          Debug.Assert(false, "publish() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t publish(ushort mid, string data) {
        try {
          return _publish(client_, mid, (uint)data.Length + 1, data);
        }
        catch (Exception e) {
          Debug.Assert(false, "publish() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t post(ushort mid, ushort cid) {
        try {
          return _post(client_, mid, cid, 0, IntPtr.Zero);
        }
        catch (Exception e) {
          Debug.Assert(false, "post() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t post(ushort mid, ushort cid, bool data) {
        try {
          return _post(client_, mid, cid, 1, ref data);
        }
        catch (Exception e) {
          Debug.Assert(false, "post() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t post(ushort mid, ushort cid, int data) {
        try {
          return _post(client_, mid, cid, 4, ref data);
        }
        catch (Exception e) {
          Debug.Assert(false, "post() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t post(ushort mid, ushort cid, double data) {
        try {
          return _post(client_, mid, cid, 8, ref data);
        }
        catch (Exception e) {
          Debug.Assert(false, "post() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public result_t post(ushort mid, ushort cid, string data) {
        try {
          return _post(client_, mid, cid, (uint)data.Length + 1, data);
        }
        catch (Exception e) {
          Debug.Assert(false, "publish() threw exception.", e.ToString());

          return result_t.ERR_UNEXPECTED;
        }
      }

      public mcb_t get_mcb() {
        return _get_mcb(client_);
      }

      public result_t get_timestamp(out long ticks) {
        return _get_timestamp(client_, out ticks);
      }
    }
  }
}
