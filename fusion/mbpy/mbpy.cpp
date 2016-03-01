/*
 *  FUSION
 *  Copyright (c) 2014 Alex Kosterin
 */

#define HAVE_ROUND

#include "Python.h"
#include "structmember.h"
//#include "timefuncs.h"

#include "include/nf.h"
#include "include/mb.h"
#include "include/lock.h"
#include "include/nf_macros.h"
#include "include/nf_mcb.h"
#include "include/tsc.h"

#include <map>

#define USE_PYTHON_DELAYED_CALL       1
#define USE_ALLOW_TREADS_IN_DISPATCH  1

#define MAX_CUNCURRENT_CLIENTS  4
#define N2(N_1, N_2)      N_1 ## N_2
#define N3(N_1, N_2, N_3) N_1 ## N_2 ## N_3

#define FREE_POS  (-1)

#if PY_MAJOR_VERSION >= 3
# define  PyInt_Type                PyLong_Type
# define  PyInt_Check               PyLong_Check
# define  PyInt_AS_LONG             PyLong_AsLong
# define  _PyInt_AsInt              PyLong_AsLong

# define  PyString_Type             PyBytes_Type
# define  PyString_Check            PyBytes_Check

# define  PyString_FromString       PyBytes_FromString
# define  PyString_AsString         PyBytes_AsString
# define  PyString_AsStringAndSize  PyBytes_AsStringAndSize
#endif

static PyObject *error__; // set to exception object in init
static struct _srwlock_t lock__;

typedef struct { ///////////////////////////////////////////////////////////////
  PyObject_HEAD

  size_t      pos_;     // index into client detail table (cbs)
} PyMbClientObject;

struct msginfo_t {
  size_t      sz_;
  nf::mtype_t mode_;
  bool        read_;
  bool        write_;
  PyObject*   ocb_;
  PyObject*   oadaptor_;
};

struct client_wrapper_t { //////////////////////////////////////////////////////
  nf::client_t*                   client_;
  std::map<nf::mid_t, msginfo_t>  msginfo_;
  const char*                     connect_;
  const char*                     profile_;
  nf::cmi_t                       cmi_;
  PyThreadState*                  save_;
};

static client_wrapper_t cw_table__[MAX_CUNCURRENT_CLIENTS] { ///////////////////
  {0},
  {0},
  {0},
  {0},
};

static nf::result_t python_callback__(client_wrapper_t* cw, nf::mid_t mid, size_t len, const void *data) {
  auto I = cw->msginfo_.find(mid);

  if (I == cw->msginfo_.end())
    return nf::ERR_UNEXPECTED;

  nf::result_t e = nf::ERR_OK;
  PyObject *r = 0, *a;

  if (len == 0)
    Py_INCREF(Py_None), a = Py_None;
  else if (I->second.oadaptor_ == (PyObject*)Py_None)
    Py_INCREF(Py_None), a = Py_None;
  else if (I->second.oadaptor_ == (PyObject*)&PyBool_Type)
    a = Py_BuildValue("Hb", mid, *(bool*)data);
  else if (I->second.oadaptor_ == (PyObject*)&PyInt_Type)
    a = Py_BuildValue("Hi", mid, *(int*)data);
  else if (I->second.oadaptor_ == (PyObject*)&PyFloat_Type)
    a = Py_BuildValue("Hd", mid, *(double*)data);
  else if (I->second.oadaptor_ == (PyObject*)&PyString_Type)
    a = Py_BuildValue("Hs#", mid, data, len);
  else if (I->second.oadaptor_) {
    if (PyObject* v = PyObject_CallMethod(I->second.oadaptor_, "unpack", "s#", data, len)) {
      a = Py_BuildValue("HO", mid, v);

      Py_DECREF(v);
    }
  }
  else
    a = Py_BuildValue("Hs#", mid, data, len);

  if (a)
    r = PyObject_CallObject(I->second.ocb_, a);

  if (PyErr_Occurred())
    e = nf::ERR_UNEXPECTED;
  else if (r == Py_None)
    e = nf::ERR_OK;
  else if (PyInt_Check(r))
    e = (nf::result_t)PyInt_AS_LONG(r);
  else
    e = nf::ERR_UNEXPECTED;

  Py_DECREF(a);
  Py_XDECREF(r);

  return e;
}

static nf::result_t cb__(client_wrapper_t* cw, nf::mid_t mid, size_t len, const void *data) {
  PyGILState_STATE gstate = PyGILState_Ensure();

//FUSION_INFO("PyGILState_STATE=%sLOCKED\n", gstate ? "UN" : ""); //@
//::printf("PyGILState_STATE=%sLOCKED\n", gstate ? "UN" : ""); //@

  nf::result_t e = python_callback__(cw, mid, len, data);

  PyGILState_Release(gstate);

  return e;
}

struct delayed_args_t { ////////////////////////////////////////////////////////
  int       pos_;
  size_t    len_;
  void*     data_;
  nf::mid_t mid_;

  delayed_args_t(int pos, nf::mid_t mid, size_t len, const void *data) : pos_(pos), mid_(mid), len_(len), data_(0) {
    if (len) {
      data_ = ::malloc(len);
      ::memcpy(data_, data, len);
    }
  }

  ~delayed_args_t() {
    ::free(data_);
  }
};

static int async_trampoline__(void *arg) { /////////////////////////////////////
  delayed_args_t* a = (delayed_args_t*)arg;

  if (a->pos_ != FREE_POS && cw_table__[a->pos_].client_)
    python_callback__(&cw_table__[a->pos_], a->mid_, a->len_, a->data_);

  delete a;

  return 0;
}

static nf::result_t __stdcall async_callback__(nf::callback_t, void* cookie, nf::mid_t mid, size_t len, const void *data) {
#if USE_PYTHON_DELAYED_CALL
  delayed_args_t* a = new delayed_args_t((int)cookie, mid, len, data);

  if (Py_AddPendingCall(async_trampoline__, a) == -1) {
    delete a;
  
    return nf::ERR_UNEXPECTED;
  }

  return nf::ERR_OK;
#else
  PyGILState_STATE gstate = PyGILState_Ensure();

  FUSION_INFO("PyGILState_STATE=%sLOCKED\n", gstate ? "UN" : ""); //@
  ::printf("PyGILState_STATE=%sLOCKED\n", gstate ? "UN" : ""); //@

  nf::result_t e = python_callback__(&cw_table__[(int)cookie], mid, len, data);

  PyGILState_Release(gstate);

  return e;
#endif
}

#define trampoline(N)                                                                               \
static nf::result_t __stdcall N3(trampoline, N, __)(nf::mid_t mid, size_t len, const void *data) {  \
  client_wrapper_t* cw = cw_table__ + N;                                                            \
                                                                                                    \
  if (!cw->client_)                                                                                 \
    return nf::ERR_UNEXPECTED;                                                                      \
                                                                                                    \
  return cb__(cw, mid, len, data);                                                                  \
}                                                                                                   \
                                                                                                    \
// MAX_CONCURRENT_CLIENTS
trampoline(0)
trampoline(1)
trampoline(2)
trampoline(3)

static nf::result_t (__stdcall *trampolines__[])(nf::mid_t, size_t, const void*) {
  trampoline0__,
  trampoline1__,
  trampoline2__,
  trampoline3__,
};

static PyMemberDef client_memberlist__[] = {
  {0},
};

/* Create a new, uninitialized client object. */
static PyObject* client_new__(PyTypeObject *type, PyObject *args, PyObject *kwds) {
  PyObject *n = type->tp_alloc(type, 0);

  if (n)
    ((PyMbClientObject*)n)->pos_ = FREE_POS;

  return n;
}

static inline client_wrapper_t* get_wrapper__(PyMbClientObject* c) { ///////////
  if (c->pos_ == FREE_POS || !cw_table__[c->pos_].client_) {
    PyErr_SetString(error__, "client is NULL");

    return NULL;
  }

  return &cw_table__[c->pos_];
}

#define GET_WRAPPER_SAFE(C)                 \
  client_wrapper_t* cw = get_wrapper__(C);  \
                                            \
  if (!cw)                                  \
    return NULL;

// Deallocate a object in response to the last Py_DECREF(). 
//  First mark cws table slot free.
static void client_dealloc__(PyMbClientObject* self) { /////////////////////////
  PyMbClientObject* c  = (PyMbClientObject*)self;

  if (client_wrapper_t* cw = get_wrapper__(c)) {
    wlock_t l(lock__);

    if (c->pos_ != FREE_POS) {
      if (cw->cmi_)
        cw->client_->unreg_callback_method(cw->cmi_);

      nf::_delete_client(cw->client_);
      
      for (auto I = cw->msginfo_.begin(), E = cw->msginfo_.end(); I != E; ++I) {
        Py_XDECREF(I->second.ocb_);
        Py_XDECREF(I->second.oadaptor_);
      }
      
      ::free((void*)cw->connect_);
      ::free((void*)cw->profile_);
      c->pos_ = FREE_POS; 

      cw->profile_  = 0;
      cw->connect_  = 0;
      cw->client_   = 0;
      cw->cmi_      = 0;
    }
  }
}

static PyObject* client_repr__(PyMbClientObject* self) { ///////////////////////
  char buf[512] = "<Client object>";
  PyMbClientObject* c = (PyMbClientObject*)self;

  if (client_wrapper_t* cw = get_wrapper__(c)) {
    if (!cw->client_->registered())
      PyOS_snprintf(buf, sizeof(buf), "<Client object, name='%s'>", cw->client_->name());
    else {
      size_t rw = 0, r = 0, w = 0, s = 0;

      for (auto I = cw->msginfo_.begin(), E = cw->msginfo_.end(); I != E; ++I) {
        if (I->second.read_ && I->second.write_)
          ++rw;
        else if (I->second.read_)
          ++r;
        else if (I->second.write_)
          ++w;

        if (I->second.ocb_)
          ++s;
      }

      PyOS_snprintf(buf, sizeof(buf), "<Client object, %s name='%s' profile='%s' id=%d msgs=%d [rw=%d r=%d w=%d s=%d]>", cw->connect_, cw->client_->name(), cw->profile_, cw->client_->id(), cw->msginfo_.size(), rw, r, w, s);
    }
  }

  return PyString_FromString(buf);
}

/*Initialize a new client object. */
static int client_initobj__(PyObject* self, PyObject *args, PyObject *kwds) { //
  static char *keywords__[] = { "name", 0 };

  PyMbClientObject* c = (PyMbClientObject*)self;
  const char* name = "";

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s:Client", keywords__, &name))
    return -1;

  {
    wlock_t l(lock__);

    for (int i = 0; i < MAX_CUNCURRENT_CLIENTS; ++i)
      if (!cw_table__[i].client_) {
        cw_table__[c->pos_ = i].client_ = nf::_create_client(name);

        return 0;
      }
  }

  return -1;  // no cws free slots...
}

static PyObject* client_reg__(PyObject* self, PyObject *args, PyObject *kwds) {
  static char *keywords__[] = { "profile", "host", "port", "profiles", 0 };

  PyMbClientObject* c = (PyMbClientObject*)self;
  GET_WRAPPER_SAFE(c);

  char connect[250];
  const char* host = "127.0.0.1";
  const char* port = "3001";
  const char* profile = "";
  PyObject*   oprofs = 0;
  size_t      profs_nr;
  char**      vprofs;

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|ssO:reg", keywords__, &profile, &host, &port, &oprofs))
    return NULL;

  _snprintf(connect, sizeof connect - 1, "type=tcp host=%s port=%s", host, port);

  if (oprofs) {
    if (!PyList_Check(oprofs)) {
      Py_XDECREF(oprofs);
      PyErr_SetString(error__, "profiles must be a list of strings");

      return NULL;
    }

    size_t profs_sz = ::strlen(profile) + 1;

    profs_nr = PyList_Size(oprofs);

    for (size_t i = 0; i < profs_nr; ++i) {
      PyObject* os = PyList_GetItem(oprofs, i);

      if (!PyString_Check(os)) {
        Py_XDECREF(oprofs);
        PyErr_SetString(error__, "profiles must be a list of strings");

        return NULL;
      }

      profs_sz += ::strlen(PyString_AsString(os)) + 1;
    }

    char* buff  = (char*)alloca(profs_sz);
    char* p     = buff;

    // put default profile at the beginning of vprofs
    ++profs_nr;
    vprofs    = (char**)alloca((profs_nr) * sizeof(char*));
    vprofs[0] = p;
    ::strcpy(p, profile);
    p += ::strlen(profile) + 1;

    for (size_t i = 1; i < profs_nr; ++i) {
      const char* s = PyString_AsString(PyList_GetItem(oprofs, i - 1));

      vprofs[i] = p;
      ::strcpy(p, s);
      p += ::strlen(s) + 1;
    }            
  }

  nf::result_t e;

  Py_BEGIN_ALLOW_THREADS
  if (oprofs)
    e = cw->client_->reg(connect, profs_nr, (const char**)vprofs);
  else
    e = cw->client_->reg(connect, profile);
  Py_END_ALLOW_THREADS

  Py_XDECREF(oprofs);

  if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  cw->connect_  = ::strdup(connect);
  cw->profile_  = ::strdup(profile);
  cw->cmi_      = 0;

  Py_RETURN_NONE;
}

static PyObject* client_unreg__(PyObject* self) { //////////////////////////////
  PyMbClientObject* c = (PyMbClientObject*)self;
  nf::result_t e;

  GET_WRAPPER_SAFE(c);

  Py_BEGIN_ALLOW_THREADS
  e = cw->client_->unreg();
  Py_END_ALLOW_THREADS

  if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* client_name__(PyObject* self) { ///////////////////////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  return PyString_FromString(cw->client_->name());
}

static PyObject* client_registered__(PyObject* self) { /////////////////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  return Py_BuildValue("b", cw->client_->registered());
}

static PyObject* client_id__(PyObject* self) { /////////////////////////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  return Py_BuildValue("H", cw->client_->id());
}

static PyObject* client_dispatch__(PyObject* self, PyObject *args, PyObject *kwds) {
  static char*      keywords__[] = { "all", "timeout", 0 };

  PyMbClientObject* c = (PyMbClientObject*)self;
  bool              all = true;
  int               timeout = 0;

  GET_WRAPPER_SAFE(c);

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|bi:dispatch", keywords__, &all, &timeout))
    return NULL;

  nf::result_t e;

#if USE_ALLOW_TREADS_IN_DISPATCH
  PyGILState_STATE gstate = PyGILState_Ensure();

//FUSION_INFO("PyGILState_STATE=%sLOCKED\n", gstate ? "UN" : ""); //@
//::printf("PyGILState_STATE=%sLOCKED\n", gstate ? "UN" : ""); //@

  e = cw->client_->dispatch(timeout, all);

  PyGILState_Release(gstate);
#else
  Py_BEGIN_ALLOW_THREADS
  e = cw->client_->dispatch(timeout, all);
  Py_END_ALLOW_THREADS
#endif

  if (PyErr_Occurred())
    return NULL;
  else if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* client_mcreate__(PyObject* self, PyObject *args, PyObject *kwds) {
  static char*      keywords__[] = { "name", "flags", "mode", "size", "data", 0 };

  PyMbClientObject* c = (PyMbClientObject*)self;
  nf::result_t      e;
  const char*       name;
  nf::oflags_t      flags;
  const char*       sflags  = 0;
  nf::mtype_t       mode;
  const char*       smode   = 0;
  nf::mid_t         mid;
  size_t            size;
  PyObject*         of      = 0;
  PyObject*         om      = 0;
  PyObject*         odata   = 0;

  GET_WRAPPER_SAFE(c);

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "sOOi|O:mcreate", keywords__, &name, &of, &om, &size, &odata))
    return NULL;

  Py_XDECREF(odata);

  if (PyInt_Check(of)) {
    if (PyInt_Check(om)) {
      if (!PyArg_ParseTupleAndKeywords(args, kwds, "sHHH|O:mcreate", keywords__, &name, &flags, &mode, &size, &odata)) {
        Py_XDECREF(of);
        Py_XDECREF(om);
        
        return NULL;
      }
    }
    else if (!PyArg_ParseTupleAndKeywords(args, kwds, "sHHs|O:mcreate", keywords__, &name, &flags, &smode, &size, &odata)) {
      Py_XDECREF(of);

      return NULL;
    }
  }
  else if (PyInt_Check(om)) {
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ssHH|O:mcreate", keywords__, &name, &sflags, &mode, &size, &odata)) {
      Py_XDECREF(om);

      return NULL;
    }
  }
  else if (!PyArg_ParseTupleAndKeywords(args, kwds, "ssHs|O:mcreate", keywords__, &name, &sflags, &smode, &size, &odata))
    return NULL;

  if (sflags) {
    for (size_t i = 0; i < ::strlen(sflags); ++i)
      if (!::strchr("rwxosct", sflags[i])) {
        char buf[512];

        PyOS_snprintf(buf, sizeof(buf), "Invalid flags '%s': '%c'", sflags, sflags[i]);
        PyErr_SetString(error__, buf);

        return NULL;
      }

    if (::strchr(sflags, 'r') && ::strchr(sflags, 'w'))
      flags = nf::O_RDWR;
    else if (::strchr(sflags, 'r'))
      flags = nf::O_RDONLY;
    else if (::strchr(sflags, 'w'))
      flags = nf::O_WRONLY;
    else {
      char buf[512];

      PyOS_snprintf(buf, sizeof(buf), "Invalid flags '%s': 'r' or 'w' must present", sflags);
      PyErr_SetString(error__, buf);

      return NULL;
    }

    if (::strchr(sflags, 'x'))
      flags |= nf::O_EXCL;

    if (::strchr(sflags, 'o'))
      flags |= nf::O_NOTIFY_OPEN;

    if (::strchr(sflags, 's'))
      flags |= nf::O_NOTIFY_SUBSCRIBE;

    if (::strchr(sflags, 'c'))
      flags |= nf::O_CREATE;

    if (::strchr(sflags, 't'))
      flags |= nf::O_TEMPORARY;
  }

  if (smode) {
    if (!::strcmp(smode, "event"))
      mode = nf::MT_EVENT;
    else if (!::strcmp(smode, "persistent-event"))
      mode = nf::MT_EVENT | nf::MT_PERSISTENT;
    else if (!::strcmp(smode, "group"))
      mode = nf::MT_GROUP;
    else if (!::strcmp(smode, "data"))
      mode = nf::MT_DATA;
    else if (!::strcmp(smode, "stream"))
      mode = nf::MT_STREAM;
    else {
      char buf[512];

      PyOS_snprintf(buf, sizeof(buf), "Invalid mode: '%s'. Must be: event|persistent-event|data|group|stream", smode);
      PyErr_SetString(error__, buf);

      return NULL;
    }
  }

  if (odata) {
    char      xxx[8];
    void*     buf = xxx;
    size_t    len = size;
    Py_buffer bufo;

    if (PyInt_Check(odata) && (len == 1 || len == 2 || len == 4 || len == 8)) {
      Py_XDECREF(of);
      Py_XDECREF(om);

      if (!PyArg_ParseTupleAndKeywords(args, kwds, "sOOi|i:mcreate", keywords__, &name, &of, &om, &size, xxx)) {
        Py_XDECREF(odata);
        
        return NULL;
      }    
    }
    else if (PyFloat_Check(odata) && (len == 4 || len == 8)) {
      Py_XDECREF(of);
      Py_XDECREF(om);

      if (!PyArg_ParseTupleAndKeywords(args, kwds, len == 4 ? "sOOi|f:mcreate" : "sHHi|d:mcreate", keywords__, &name, &of, &om, &size, xxx)) {
        Py_XDECREF(odata);
        
        return NULL;
      }
    }
    else if (PyBool_Check(odata) && len == 1) {
      Py_XDECREF(of);
      Py_XDECREF(om);

      if (!PyArg_ParseTupleAndKeywords(args, kwds, "sOOi|i:mcreate", keywords__, &name, &of, &om, &size, xxx)) {
        Py_XDECREF(odata);
        
        return NULL;
      }
    }
    else if (PyString_Check(odata)) {
      if (!PyArg_ParseTupleAndKeywords(args, kwds, "sOOi|z*:mcreate", keywords__, &name, &of, &om, &size, &bufo)) {
        Py_XDECREF(odata);
        
        return NULL;
      }

      if (len != -1 && len != bufo.len) {
        PyErr_SetString(error__, "ERR_MESSAGE_SIZE");

        return NULL;
      }

      buf = bufo.buf;
      len = bufo.len;
    }

    mode |= nf::MT_PERSISTENT;

    Py_BEGIN_ALLOW_THREADS
    e = cw->client_->mcreate(name, flags, mode, mid, size, len, buf);
    Py_END_ALLOW_THREADS
  }
  else {
    Py_BEGIN_ALLOW_THREADS
    e = cw->client_->mcreate(name, flags, mode, mid, size);
    Py_END_ALLOW_THREADS
  }

  Py_XDECREF(of);
  Py_XDECREF(om);
  Py_XDECREF(odata);

  if (e == nf::ERR_OK) {
    msginfo_t mi = { size, mode, ((flags & nf::O_WRONLY) != nf::O_WRONLY), ((flags & nf::O_RDONLY) != nf::O_RDONLY), 0, 0 };

    cw->msginfo_.insert(std::make_pair(mid, mi));

    return Py_BuildValue("H", mid);
  }
  else if (e == nf::ERR_OPEN && mid != 0) {
    char buf[120];

    PyOS_snprintf(buf, sizeof(buf), "%s: already opened, mid=%d.", nf::result_to_str(e), mid);
    PyErr_SetString(error__, buf);
  }

  PyErr_SetString(error__, nf::result_to_str(e));

  return NULL;
}

static PyObject* client_mopen__(PyObject* self, PyObject *args, PyObject *kwds) {
  static char* keywords__[] = { "name", "flags", "mode", "size", 0 };

  PyMbClientObject* c = (PyMbClientObject*)self;
  nf::result_t      e;
  const char*       name;
  nf::oflags_t      flags   = 0;
  const char*       sflags  = 0;
  nf::mtype_t       mode;
  const char*       smode   = 0;
  size_t            size;

  GET_WRAPPER_SAFE(c);

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "sH|Hi:mopen", keywords__, &name, &flags, &mode, &size))
    PyErr_Clear();
    
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "ss|Hi:mopen", keywords__, &name, &sflags, &mode, &size))
    return NULL;

  if (sflags) {
    for (size_t i = 0; i < ::strlen(sflags); ++i)
      if (!::strchr("rwxosct", sflags[i])) {
        char buf[512];

        PyOS_snprintf(buf, sizeof(buf), "Invalid flags '%s': '%c'", sflags, sflags[i]);
        PyErr_SetString(error__, buf);

        return NULL;
      }

    if (::strchr(sflags, 'r') && ::strchr(sflags, 'w'))
      flags = nf::O_RDWR;
    else if (::strchr(sflags, 'r'))
      flags = nf::O_RDONLY;
    else if (::strchr(sflags, 'w'))
      flags = nf::O_WRONLY;
    else {
      char buf[512];

      PyOS_snprintf(buf, sizeof(buf), "Invalid flags '%s': 'r' or 'w' must present", sflags);
      PyErr_SetString(error__, buf);

      return NULL;
    }

    if (::strchr(sflags, 'x'))
      flags |= nf::O_EXCL;

    if (::strchr(sflags, 'o'))
      flags |= nf::O_NOTIFY_OPEN;

    if (::strchr(sflags, 's'))
      flags |= nf::O_NOTIFY_SUBSCRIBE;

    if (::strchr(sflags, 'c'))
      flags |= nf::O_CREATE;

    if (::strchr(sflags, 't'))
      flags |= nf::O_TEMPORARY;
  }

  nf::mtype_t mode_bak;
  size_t      size_bak;
  bool        bmode = false;
  bool        bsize = false;

  if (kwds && PyDict_GetItemString(kwds, "mode")) {
    bmode     = true;
    mode_bak  = mode;
  }

  if (kwds && PyDict_GetItemString(kwds, "size")) {
    bsize     = true;
    size_bak  = size;
  }

  nf::mid_t mid;

  Py_BEGIN_ALLOW_THREADS
  e = cw->client_->mopen(name, flags, mode, mid, size);
  Py_END_ALLOW_THREADS

  if (e == nf::ERR_OK) {
    char buf[64];

    if (bmode && mode_bak != mode) {
      cw->client_->mclose(mid);

      PyOS_snprintf(buf, sizeof(buf), "%s mode: given=%d actual=%d.", nf::result_to_str(nf::ERR_PARAMETER), mode_bak, mode);
      PyErr_SetString(error__, buf);

      return NULL;
    }

    if (bsize && size_bak != size) {
      cw->client_->mclose(mid);

      PyOS_snprintf(buf, sizeof(buf), "%s size: given=%d actual=%d.", nf::result_to_str(nf::ERR_PARAMETER), size_bak, size);
      PyErr_SetString(error__, buf);

      return NULL;
    }

    if (e == nf::ERR_OK) {
      msginfo_t mi = { size, mode, ((flags & nf::O_WRONLY) != nf::O_WRONLY), ((flags & nf::O_RDONLY) != nf::O_RDONLY), 0, 0 };

//    FUSION_INFO("mid=%d mode=%d sz=%d r=%d w=%d cb=%X adaptor=%X", mid, mi.mode_, mi.sz_, mi.read_, mi.write_, mi.cb_, mi.adaptor_);

//    FUSION_INFO("sz=%d", cw->msginfo_.size());
      auto X = cw->msginfo_.insert(std::make_pair(mid, mi));
//    FUSION_INFO("sz=%d", cw->msginfo_.size());

      //FUSION_INFO("mid=%d mode=%d sz=%d r=%d w=%d cb=%X adaptor=%X", X.first->, X.second.mode_, X.second.sz_, X.second.read_, X.second.write_, X.second.cb_, X.second.adaptor_);

//    auto I = cw->msginfo_.find(mid);
      
//    FUSION_INFO("mid=%d mode=%d sz=%d r=%d w=%d cb=%X adaptor=%X", I->first, I->second.mode_, I->second.sz_, I->second.read_, I->second.write_, I->second.cb_, I->second.adaptor_);

//    for (auto I = cw->msginfo_.begin(), E = cw->msginfo_.end(); I != E; ++I)
//      FUSION_INFO("mid=%d mode=%d sz=%d r=%d w=%d cb=%X adaptor=%X", I->first, I->second.mode_, I->second.sz_, I->second.read_, I->second.write_, I->second.cb_, I->second.adaptor_);
    }

    return Py_BuildValue("H", mid);
  }
  else if (e == nf::ERR_OPEN && mid != 0) {
    char buf[120];

    PyOS_snprintf(buf, sizeof(buf), "%s: already opened, mid=%d.", nf::result_to_str(e), mid);
    PyErr_SetString(error__, buf);
  }

  PyErr_SetString(error__, nf::result_to_str(e));

  return NULL;
}

static PyObject* client_mclose__(PyObject* self, PyObject* args) { /////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  nf::mid_t mid;

  if (!PyArg_ParseTuple(args, "H", &mid))
    return NULL;

  nf::result_t e;

  Py_BEGIN_ALLOW_THREADS
  e = cw->client_->mclose(mid);
  Py_END_ALLOW_THREADS

  if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  auto I = cw->msginfo_.find(mid);

  if (I != cw->msginfo_.end()) {
    Py_XDECREF(I->second.ocb_);
    I->second.ocb_ = 0;

    Py_XDECREF(I->second.oadaptor_);
    I->second.oadaptor_ = 0;

    cw->msginfo_.erase(I);
  }

  Py_RETURN_NONE;
}

static PyObject* client_sub__(PyObject* self, PyObject *args, PyObject *kwds) { 
  static char *keywords__[] = { "mid", "callback", "adaptor", "async", 0 };

  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  nf::mid_t mid;
  PyObject* ocb       = 0;
  PyObject* oadaptor  = 0;
  bool      async     = false;

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "HO|Ob:sub", keywords__, &mid, &ocb, &oadaptor, &async))
    return NULL;

  if (!PyCallable_Check(ocb)) {
    PyErr_SetString(error__, "callback must be callable");

    Py_XDECREF(ocb);
    Py_XDECREF(oadaptor);

    return NULL;
  }

  nf::result_t e;

  if (async && cw->cmi_ == 0) { 
    e = cw->client_->reg_callback_method(async_callback__, (void*)c->pos_, cw->cmi_);

    if (e != nf::ERR_OK) {
      PyErr_SetString(error__, nf::result_to_str(e));

      return NULL;
    }
  }

  Py_BEGIN_ALLOW_THREADS
  e = cw->client_->subscribe(mid, nf::SF_PUBLISH, async ? cw->cmi_ : nf::CM_MANUAL, trampolines__[c->pos_]);
  Py_END_ALLOW_THREADS

  if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  if (ocb) {
    Py_INCREF(ocb);
    cw->msginfo_[mid].ocb_ = ocb;
  }

  if (oadaptor) {
    Py_INCREF(oadaptor);
    cw->msginfo_[mid].oadaptor_ = oadaptor;
  }

  Py_RETURN_NONE;
}

static PyObject* client_unsub__(PyObject* self, PyObject* args) { //////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  nf::mid_t mid;

  if (!PyArg_ParseTuple(args, "H:unsub", &mid))
    return NULL;

  nf::result_t e;

  Py_BEGIN_ALLOW_THREADS
  e = cw->client_->unsubscribe(mid);
  Py_END_ALLOW_THREADS

  if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  auto I = cw->msginfo_.find(mid);

  if (I != cw->msginfo_.end()) {
    Py_XDECREF(I->second.ocb_);
    I->second.ocb_ = 0;

    Py_XDECREF(I->second.oadaptor_);
    I->second.oadaptor_ = 0;
  }
  else {
    PyErr_SetString(error__, "ERR_UNEXPECTED");

    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* client_get_mcb__(PyObject* self) { ////////////////////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  if (const nf::mcb_t* mcb = cw->client_->get_mcb())
    return Py_BuildValue(
      "{sHsHsHsKsksz#sbsi}",
        "mid",  mcb->mid_,
        "src",  mcb->src_,
        "dst",  mcb->dst_,
        "org",  nf::msecs_to_unix(mcb->org_),
        "sq",   mcb->seq_,
        "data", mcb->data(), mcb->len_,
        "req",  mcb->request_,
        "rsq",  mcb->req_seq_
      );

  Py_RETURN_NONE;
}

static bool _pack_params__(nf::mid_t mid, PyObject* oadaptor, PyObject* odata, size_t& len, void*& buf, char* xxx) {
  if (oadaptor) {
    if (oadaptor == (PyObject*)Py_None)
      goto _none;
    else if (oadaptor == (PyObject*)&PyBool_Type)
      goto _bool;
    else if (oadaptor == (PyObject*)&PyInt_Type)
      goto _int;
    else if (oadaptor == (PyObject*)&PyFloat_Type)
      goto _float;
    else if (oadaptor == (PyObject*)&PyString_Type)
      goto _string;

    PyObject* os = PyObject_CallMethod(oadaptor, "pack", "O", odata);

    if (!os)
      return false;

    Py_ssize_t slen;

    if (PyString_AsStringAndSize(os, (char**)&buf, &slen) == -1)
      return false;

    Py_DECREF(os);

    if (len != -1 && len != slen) {
      PyErr_SetString(error__, nf::result_to_str(nf::ERR_PARAMETER));

      return false;
    }
  }
  else if (Py_None == odata && (len == -1 || len == 0)) {
_none:
    len = 0;
    buf = 0;
  }
  else 
_int:
  if (PyInt_Check(odata) && (len == -1 || len == 8)) {
    long v = PyInt_AS_LONG(odata);

    if (len == -1)
      len = sizeof(v);

    ::memcpy(xxx, &v, len);
    buf = xxx;
  }
  else if (PyInt_Check(odata) && (len == 1)) {
    char v = (char)PyInt_AS_LONG(odata);

    if (len == -1)
      len = sizeof(v);

    ::memcpy(xxx, &v, len);
    buf = xxx;
  }
  else if (PyInt_Check(odata) && (len == 2)) {
    short v = (short)PyInt_AS_LONG(odata);

    if (len == -1)
      len = sizeof(v);

    ::memcpy(xxx, &v, len);
    buf = xxx;
  }
  else if (PyInt_Check(odata) && (len == 4)) {
    int v = (int)PyInt_AS_LONG(odata);

    if (len == -1)
      len = sizeof(v);

    ::memcpy(xxx, &v, len);
    buf = xxx;
  }
  else 
_float:
  if (PyFloat_Check(odata) && (len == -1 || len == 8)) {
    double v = PyFloat_AS_DOUBLE(odata);

    if (len == -1)
      len = sizeof(v);

    ::memcpy(xxx, &v, len);
    buf = xxx;
  }
  else if (PyFloat_Check(odata) && (len == 4)) {
    float v = (float)PyFloat_AS_DOUBLE(odata);

    if (len == -1)
      len = sizeof(v);

    ::memcpy(xxx, &v, len);
    buf = xxx;
  }
  else if (PyBool_Check(odata) && (len == -1 || len == 1)) {
_bool:
    bool v = PyInt_AS_LONG(odata);

    if (len == -1)
      len = sizeof(v);

    ::memcpy(xxx, &v, len);
    buf = xxx;
  }
  else if (PyString_Check(odata)) {
_string:
    Py_ssize_t slen;

    if (PyString_AsStringAndSize(odata, (char**)&buf, &slen) == -1)
      return false;

    if (len != -1 && len != slen) {
      PyErr_SetString(error__, nf::result_to_str(nf::ERR_PARAMETER));

      return false;
    }
  }
  else {
    PyErr_SetString(error__, nf::result_to_str(nf::ERR_PARAMETER));

    return false;
  }

  return true;
}

static PyObject* client_pub__(PyObject* self, PyObject* args) { ////////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  nf::mid_t mid;
  PyObject* odata;

  if (!PyArg_ParseTuple(args, "HO:pub", &mid, &odata))
    return NULL;

  auto I = cw->msginfo_.find(mid);

  if (I == cw->msginfo_.end()) {
    PyErr_SetString(error__, nf::result_to_str(nf::ERR_OPEN));

    return NULL;
  }

  char    xxx[8];
  void*   buf = xxx;
  size_t  len = I->second.sz_;
  bool    rc  = _pack_params__(mid, I->second.oadaptor_, odata, len, buf, xxx);
  
  if (!rc)
    return NULL;

  nf::result_t e;

  Py_BEGIN_ALLOW_THREADS
  e = cw->client_->publish(mid, len, buf);
  Py_END_ALLOW_THREADS

  if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* client_post__(PyObject* self, PyObject* args) { ///////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  nf::mid_t mid;
  nf::cid_t dst;
  PyObject* odata;

  if (!PyArg_ParseTuple(args, "HHO:post", &mid, &dst, &odata))
    return NULL;

  auto I = cw->msginfo_.find(mid);

  if (I == cw->msginfo_.end()) {
    PyErr_SetString(error__, nf::result_to_str(nf::ERR_OPEN));

    return NULL;
  }

  char    xxx[8];
  void*   buf = xxx;
  size_t  len = I->second.sz_;
  bool    rc  = _pack_params__(mid, I->second.oadaptor_, odata, len, buf, xxx);
  
  if (!rc)
    return NULL;

  nf::result_t e;

  Py_BEGIN_ALLOW_THREADS
  e = cw->client_->post(mid, dst, len, buf);
  Py_END_ALLOW_THREADS

  if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* client_send__(PyObject* self, PyObject* args) { ///////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  nf::mid_t mid;
  nf::cid_t dst;
  PyObject* odata;
  int       timeout = INFINITE;
  size_t    len;
  PyObject* oadaptor = 0;

  if (!PyArg_ParseTuple(args, "HHO|i:send", &mid, &dst, &odata, &timeout))
    return NULL;

  if (mid <= nf::MD_SYS_LAST_) { //@@ should go to mb.dll
    switch (mid) {
    case nf::MD_SYS_STATUS:
    case nf::MD_SYS_STOP_REQUEST:
    case nf::MD_SYS_TERMINATE_REQUEST:
      len       = 4; 
      oadaptor  = (PyObject*)&PyInt_Type;

      break;

    case nf::MD_SYS_ECHO_REQUEST:
    case nf::MD_SYS_ECHO_REPLY:  
      len       = -1; 
      oadaptor  = (PyObject*)&PyString_Type;

      break;

    default:
      PyErr_SetString(error__, nf::result_to_str(nf::ERR_OPEN));

      return NULL;
    }
  }
  else {
    auto I = cw->msginfo_.find(mid);

    if (I == cw->msginfo_.end()) {
      PyErr_SetString(error__, nf::result_to_str(nf::ERR_OPEN));

      return NULL;
    }

    len       = I->second.sz_;
    oadaptor  = I->second.oadaptor_;
  }

  char  xxx[8];
  void* buf = xxx;
  bool  rc  = _pack_params__(mid, oadaptor, odata, len, buf, xxx);

  if (!rc)
    return NULL;

  nf::result_t e;

  Py_BEGIN_ALLOW_THREADS
  e = cw->client_->send(mid, dst, len, buf, timeout);
  Py_END_ALLOW_THREADS

  if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* client_request__(PyObject* self, PyObject* args) { ////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  nf::mid_t mid;
  nf::cid_t dst;
  PyObject* odata;
  int       timeout = INFINITE;
  size_t    len;
  PyObject* oadaptor = 0;

  if (!PyArg_ParseTuple(args, "HHO|i:request", &mid, &dst, &odata, &timeout))
    return NULL;

  if (mid <= nf::MD_SYS_LAST_) { //@@ should go to mb.dll
    switch (mid) {
    case nf::MD_SYS_STATUS:
    case nf::MD_SYS_STOP_REQUEST:
    case nf::MD_SYS_TERMINATE_REQUEST:
      len       = 4; 
      oadaptor  = (PyObject*)&PyInt_Type;

      break;

    case nf::MD_SYS_ECHO_REQUEST:
    case nf::MD_SYS_ECHO_REPLY:  
      len       = -1; 
      oadaptor  = (PyObject*)&PyString_Type;

      break;

    default:
      PyErr_SetString(error__, nf::result_to_str(nf::ERR_OPEN));

      return NULL;
    }
  }
  else {
    auto I = cw->msginfo_.find(mid);

    if (I == cw->msginfo_.end()) {
      PyErr_SetString(error__, nf::result_to_str(nf::ERR_OPEN));

      return NULL;
    }

    len       = I->second.sz_;
    oadaptor  = I->second.oadaptor_;
  }

  char  xxx[8];
  void* buf = xxx;
  bool  rc  = _pack_params__(mid, oadaptor, odata, len, buf, xxx);

  if (!rc)
    return NULL;

  nf::result_t  e;
  nf::mid_t     rep_mid;
  size_t        rep_len;
  const void*   rep_data;

  Py_BEGIN_ALLOW_THREADS
  e = cw->client_->request(mid, dst, len, buf, rep_mid, rep_len, rep_data, timeout);
  Py_END_ALLOW_THREADS

  if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  return Py_BuildValue("(Hs#)", rep_mid, rep_data, rep_len);
}

static PyObject* client_reply__(PyObject* self, PyObject* args) { ////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  nf::mid_t mid;
  PyObject* odata;

  if (!PyArg_ParseTuple(args, "HO:reply", &mid, &odata))
    return NULL;

  auto I = cw->msginfo_.find(mid);

  if (I == cw->msginfo_.end()) {
    PyErr_SetString(error__, nf::result_to_str(nf::ERR_OPEN));

    return NULL;
  }

  char    xxx[8];
  void*   buf = xxx;
  size_t  len = I->second.sz_;
  bool    rc  = _pack_params__(mid, I->second.oadaptor_, odata, len, buf, xxx);
  
  if (!rc)
    return NULL;

  nf::result_t e;

  Py_BEGIN_ALLOW_THREADS
  e = cw->client_->reply(mid, len, buf);
  Py_END_ALLOW_THREADS

  if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* client_mlink__(PyObject* self, PyObject* args) { //////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  const char *from, *to;
  int         timeout = INFINITE;

  if (!PyArg_ParseTuple(args, "ss:link", &from, &to))
    return NULL;

  nf::result_t e;

  Py_BEGIN_ALLOW_THREADS
  e = cw->client_->mlink(from, to);
  Py_END_ALLOW_THREADS

  if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* client_munlink__(PyObject* self, PyObject* args) { ////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  PyObject* o;
  int       timeout = INFINITE;

  if (!PyArg_ParseTuple(args, "O", &o))
    return NULL;

  nf::result_t e;

  if (PyString_Check(o)) {
    int i = _PyInt_AsInt(o);

    Py_BEGIN_ALLOW_THREADS
    e = cw->client_->munlink((nf::mid_t)i);
    Py_END_ALLOW_THREADS
  }
  else if (PyInt_Check(o)) {
    const char* s = PyString_AsString(o);

    Py_BEGIN_ALLOW_THREADS
    e = cw->client_->munlink(s);
    Py_END_ALLOW_THREADS
  }
  else {
    PyErr_SetString(error__, "arg must string or integer");

    return NULL;
  }

  if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* client_mmove__(PyObject* self, PyObject* args) { //////////////
  PyErr_SetString(error__, "not implemented");

  return NULL;
}

static PyObject* client_msize__(PyObject* self, PyObject* args) { //////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  nf::mid_t mid;

  if (!PyArg_ParseTuple(args, "H:size", &mid))
    return NULL;

  auto I = cw->msginfo_.find(mid);

  if (I != cw->msginfo_.end())
    return Py_BuildValue("H", I->second.sz_);
  else
    Py_RETURN_NONE;
}

static PyObject* client_mmode__(PyObject* self, PyObject* args) { //////////////
  PyMbClientObject* c = (PyMbClientObject*)self;

  GET_WRAPPER_SAFE(c);

  nf::mid_t mid;

  if (!PyArg_ParseTuple(args, "H", &mid))
    return NULL;

  auto I = cw->msginfo_.find(mid);

  if (I != cw->msginfo_.end())
    return Py_BuildValue("H", I->second.mode_);
  else
    Py_RETURN_NONE;
}

static PyObject* client_mlist__(PyObject* self, PyObject *args, PyObject *kwds) {
  static char *keywords__[] = { "profile", "mask", 0 };

  const char* profile = 0;
  const char* mask    = 0;

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ss:list", keywords__, &profile, &mask))
    return NULL;

  PyMbClientObject* c = (PyMbClientObject*)self;
  size_t nr     = 0;
  char** names  = 0;
  nf::result_t e;

  GET_WRAPPER_SAFE(c);

  Py_BEGIN_ALLOW_THREADS
  e = cw->client_->mlist(profile, mask, nr, names);
  Py_END_ALLOW_THREADS

  if (e != nf::ERR_OK) {
    PyErr_SetString(error__, nf::result_to_str(e));

    return NULL;
  }

  PyObject* res = PyList_New(nr);

  for (size_t i = 0; i < nr; ++i)
    PyList_SET_ITEM(res, i, Py_BuildValue("s", names[i]));

  ::free((void*)names);

  return res;
}

static PyObject* client_enter__(PyObject *self) { //////////////////////////
  Py_INCREF(self);

  return (PyObject*)self;
}

static PyObject* client_exit__(PyObject *self, PyObject *args) { ///////////////
  Py_INCREF(self);

  return (PyObject*)self;

#if 0
  PyMbClientObject* c  = (PyMbClientObject*)self;

  if (client_wrapper_t* cw = get_wrapper__(c)) {
    wlock_t l(lock__);

    if (c->pos_ != -1) {
      nf::_delete_client(cw->client_);
      cw->client_ = 0;
      cw->msginfo_.clear();
      ::free((void*)cw->connect_);
      cw->connect_ = 0;
      ::free((void*)cw->profile_);
      cw->profile_ = 0;
//    c->pos_ = -1; //@
    }

    Py_RETURN_NONE;
  }

  PyErr_SetString(error__, "something went wrong...");

  return NULL;
#endif
}

static PyMethodDef client_methods__[] = { //////////////////////////////////////
  { "get_mcb",    (PyCFunction)client_get_mcb__,  METH_NOARGS,                "Get current mcb. Valid only within callback context." },

  { "registered", (PyCFunction)client_registered__,METH_NOARGS,               "True if client is registered." },
  { "id",         (PyCFunction)client_id__,       METH_NOARGS,                "Return client id." },
  { "name",       (PyCFunction)client_name__,     METH_NOARGS,                "Get client name." },

  { "reg",        (PyCFunction)client_reg__,      METH_VARARGS|METH_KEYWORDS, "Register client with fusion server. Syntax: reg(profile[host=HOST, port=PORT, profiles=(list)])" },
  { "unreg",      (PyCFunction)client_unreg__,    METH_NOARGS,                "Unregister client." },
  { "dispatch",   (PyCFunction)client_dispatch__, METH_VARARGS|METH_KEYWORDS, "Dispatch callbacks." },

  { "create",     (PyCFunction)client_mcreate__,  METH_VARARGS|METH_KEYWORDS, "Create and/or open mesasge, group, or plugin." },
  { "open",       (PyCFunction)client_mopen__,    METH_VARARGS|METH_KEYWORDS, "Open mesasge, group, or plugin" },
  { "close",      (PyCFunction)client_mclose__,   METH_VARARGS,               "Close message, group, or plugin" },

  { "sub",        (PyCFunction)client_sub__,      METH_VARARGS|METH_KEYWORDS, "Subscribe - associate callback with message. Syntax: sub(mid, callback[, adaptor=bool|int|num|str])" },
  { "unsub",      (PyCFunction)client_unsub__,    METH_VARARGS,               "Unsubscribe." },

  { "pub",        (PyCFunction)client_pub__,      METH_VARARGS,               "Publish." },
  { "post",       (PyCFunction)client_post__,     METH_VARARGS,               "Post." },
  { "send",       (PyCFunction)client_send__,     METH_VARARGS,               "Send." },
  { "request",    (PyCFunction)client_request__,  METH_VARARGS,               "Request." },
  { "reply",      (PyCFunction)client_reply__,    METH_VARARGS,               "Reply." },

  { "link",      (PyCFunction)client_mlink__,     METH_VARARGS,               "Link message." },
  { "unlink",    (PyCFunction)client_munlink__,   METH_VARARGS,               "Unlink message." },
  { "move",      (PyCFunction)client_mmove__,     METH_VARARGS,               "Move message." },

  { "size",      (PyCFunction)client_msize__,     METH_VARARGS,               "Get size of open message." },
  { "mode",      (PyCFunction)client_mmode__,     METH_VARARGS,               "Get mode of open message." },
  { "list",      (PyCFunction)client_mlist__,     METH_VARARGS|METH_KEYWORDS, "List messages." },

  { "__enter__",  (PyCFunction)client_enter__,    METH_NOARGS },
  { "__exit__",   (PyCFunction)client_exit__,     METH_VARARGS },

  { NULL, NULL }  /* sentinel */
};

static PyTypeObject client_type__ = { //////////////////////////////////////////
  PyVarObject_HEAD_INIT(0, 0)         /* Must fill in type value later */
  "mb.Client",                                /* tp_name */
  sizeof(PyMbClientObject),                   /* tp_basicsize */
  0,                                          /* tp_itemsize */
  (destructor)client_dealloc__,               /* tp_dealloc */
  0,                                          /* tp_print */
  0,                                          /* tp_getattr */
  0,                                          /* tp_setattr */
  0,                                          /* tp_compare */
  (reprfunc)client_repr__,                    /* tp_repr */
  0,                                          /* tp_as_number */
  0,                                          /* tp_as_sequence */
  0,                                          /* tp_as_mapping */
  0,                                          /* tp_hash */
  0,                                          /* tp_call */
  0,                                          /* tp_str */
  PyObject_GenericGetAttr,                    /* tp_getattro */
  0,                                          /* tp_setattro */
  0,                                          /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   /* tp_flags */
  "Fusion client library",                    /* tp_doc */
  0,                                          /* tp_traverse */
  0,                                          /* tp_clear */
  0,                                          /* tp_richcompare */
  0,                                          /* tp_weaklistoffset */
  0,                                          /* tp_iter */
  0,                                          /* tp_iternext */
  client_methods__,                           /* tp_methods */
  client_memberlist__,                        /* tp_members */
  0,                                          /* tp_getset */
  0,                                          /* tp_base */
  0,                                          /* tp_dict */
  0,                                          /* tp_descr_get */
  0,                                          /* tp_descr_set */
  0,                                          /* tp_dictoffset */
  client_initobj__,                           /* tp_init */
  PyType_GenericAlloc,                        /* tp_alloc */
  client_new__,                               /* tp_new */
  PyObject_Del,                               /* tp_free */
};

#define CID_IS_(N)                                                        \
static PyObject* N3(CID_IS_, N, __)(PyObject* /*self*/, PyObject* args) { \
  int i;                                                                  \
                                                                          \
  if (!PyArg_ParseTuple(args, "i", &i))                                   \
    return NULL;                                                          \
                                                                          \
  return Py_BuildValue("b", nf::N2(CID_IS_, N)(i));                       \
}

CID_IS_(SYS)
CID_IS_(ALL)
CID_IS_(ALL_NOSELF)
CID_IS_(GRP)
CID_IS_(GRP_NOSELF)
CID_IS_(PUB)
CID_IS_(PUB_NOSELF)
CID_IS_(CLIENT)
CID_IS_(CLIENT_NOSELF)

#undef CID_IS_

static PyObject* error_to_string__(PyObject* /*self*/, PyObject* args) { ///////
  int e;

  if (!PyArg_ParseTuple(args, "i", &e))
    return NULL;

  return Py_BuildValue("s", nf::result_to_str((nf::result_t)e));
}

static PyObject* version__(PyObject* /*self*/, PyObject* /*args*/) { ///////////
  const nf::version_t& v = nf::version();

  return Py_BuildValue("bbHs", v.mini.maj, v.mini.min, v.build, v.name);
}

static PyMethodDef methods__[] = { /////////////////////////////////////////////
  { "CID_IS_SYS",            CID_IS_SYS__,           METH_VARARGS },
  { "CID_IS_ALL",            CID_IS_ALL__,           METH_VARARGS },
  { "CID_IS_ALL_NOSELF",     CID_IS_ALL_NOSELF__,    METH_VARARGS },
  { "CID_IS_GRP",            CID_IS_GRP__,           METH_VARARGS },
  { "CID_IS_GRP_NOSELF",     CID_IS_GRP_NOSELF__,    METH_VARARGS },
  { "CID_IS_PUB",            CID_IS_PUB__,           METH_VARARGS },
  { "CID_IS_PUB_NOSELF",     CID_IS_PUB_NOSELF__,    METH_VARARGS },
  { "CID_IS_CLIENT",         CID_IS_CLIENT__,        METH_VARARGS },
  { "CID_IS_CLIENT_NOSELF",  CID_IS_CLIENT_NOSELF__, METH_VARARGS },
  { "result_to_str",         error_to_string__,      METH_VARARGS,   "Convert error code to string." },
  { "version",               version__,              METH_NOARGS,    "Return version as tuple (maj, min, build, name)." },

  { NULL, NULL } /* sentinel */
};

struct module_state {
    PyObject *error;
};

#if PY_MAJOR_VERSION >= 3
# define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))

static PyObject* error_out__(PyObject *m) { ////////////////////////////////////
    struct module_state *st = GETSTATE(m);

    PyErr_SetString(st->error, "something bad happened");

    return NULL;
}

static PyMethodDef myextension_methods[] = { ///////////////////////////////////
    {"error_out", (PyCFunction)error_out__, METH_NOARGS, NULL},
    {NULL, NULL}
};

static int traverse__(PyObject *m, visitproc visit, void *arg) {
    Py_VISIT(GETSTATE(m)->error);
    return 0;
}

static int clear__(PyObject *m) { //////////////////////////////////////////////
    Py_CLEAR(GETSTATE(m)->error);
    return 0;
}

static struct PyModuleDef moduledef__ = { //////////////////////////////////////
  PyModuleDef_HEAD_INIT,
  "mb",
  NULL,
  sizeof(struct module_state),
  methods__,
  NULL,
  traverse__,
  clear__,
  NULL
};

# define INITERROR return NULL

extern "C" __declspec(dllexport) PyObject* PyInit_mb(void)
#else
# define INITERROR return

PyMODINIT_FUNC initmb(void)
#endif
{ //////////////////////////////////////////////////////////////////////////////
#if PY_MAJOR_VERSION >= 3
    PyObject *mod = PyModule_Create(&moduledef__);
#else
    PyObject* mod = Py_InitModule("mb", methods__);
#endif

  if (mod == NULL)
    INITERROR;

  if (PyType_Ready(&client_type__) < 0)
    INITERROR;

  Py_TYPE(&client_type__) = &PyType_Type;

  Py_INCREF((PyObject*)&client_type__);

  if (PyModule_AddObject(mod, "Client", (PyObject*)&client_type__) != 0)
    INITERROR;

  PyModule_AddIntConstant(mod, "CID_NONE",                nf::CID_NONE);
  PyModule_AddIntConstant(mod, "CID_GROUP",               nf::CID_GROUP);
  PyModule_AddIntConstant(mod, "CID_NOSELF",              nf::CID_NOSELF);
  PyModule_AddIntConstant(mod, "CID_PUB",                 nf::CID_PUB);
  PyModule_AddIntConstant(mod, "CID_ALL",                 nf::CID_ALL);
  PyModule_AddIntConstant(mod, "CID_CLIENT",              nf::CID_CLIENT);
  PyModule_AddIntConstant(mod, "CID_SYS",                 nf::CID_SYS);
  PyModule_AddIntConstant(mod, "CID_SYS_GROUP",           nf::CID_SYS_GROUP);

  // TODO: make script to generate following automatically...

  PyModule_AddIntConstant(mod, "SF_NONE",                 nf::SF_NONE);
  PyModule_AddIntConstant(mod, "SF_PRIVATE",              nf::SF_PRIVATE);
  PyModule_AddIntConstant(mod, "SF_PUBLISH",              nf::SF_PUBLISH);

  PyModule_AddIntConstant(mod, "CM_MANUAL",               nf::CM_MANUAL);

  PyModule_AddIntConstant(mod, "O_RDWR",                  nf::O_RDWR);
  PyModule_AddIntConstant(mod, "O_RDONLY",                nf::O_RDONLY);
  PyModule_AddIntConstant(mod, "O_WRONLY",                nf::O_WRONLY);
  PyModule_AddIntConstant(mod, "O_CREATE",                nf::O_CREATE);
  PyModule_AddIntConstant(mod, "O_EXCL",                  nf::O_EXCL);
  PyModule_AddIntConstant(mod, "O_HINTID",                nf::O_HINTID);
  PyModule_AddIntConstant(mod, "O_NOATIME",               nf::O_NOATIME);
  PyModule_AddIntConstant(mod, "O_TEMPORARY",             nf::O_TEMPORARY);
  PyModule_AddIntConstant(mod, "O_EDGE_TRIGGER",          nf::O_EDGE_TRIGGER);
  PyModule_AddIntConstant(mod, "O_NOTIFY_OPEN",           nf::O_NOTIFY_OPEN);
  PyModule_AddIntConstant(mod, "O_NOTIFY_SUBSCRIBE",      nf::O_NOTIFY_SUBSCRIBE);
  PyModule_AddIntConstant(mod, "O_NOTIFY_CONFIGURE",      nf::O_NOTIFY_CONFIGURE);
  PyModule_AddIntConstant(mod, "O_VALIDATE_MASK",         nf::O_VALIDATE_MASK);

  PyModule_AddIntConstant(mod, "MT_EVENT",                nf::MT_EVENT);
  PyModule_AddIntConstant(mod, "MT_DATA",                 nf::MT_DATA);
  PyModule_AddIntConstant(mod, "MT_STREAM",               nf::MT_STREAM);
  PyModule_AddIntConstant(mod, "MT_GROUP",                nf::MT_GROUP);
  PyModule_AddIntConstant(mod, "MT_CLIENT",               nf::MT_CLIENT);
  PyModule_AddIntConstant(mod, "MT_TYPE_MASK",            nf::MT_TYPE_MASK);
  PyModule_AddIntConstant(mod, "MT_PERSISTENT",           nf::MT_PERSISTENT);
  PyModule_AddIntConstant(mod, "MT_VALIDATE_MASK",        nf::MT_VALIDATE_MASK);

  PyModule_AddIntConstant(mod, "ERR_OK",                  nf::ERR_OK);
  PyModule_AddIntConstant(mod, "ERR_REGISTERED",          nf::ERR_REGISTERED);
  PyModule_AddIntConstant(mod, "ERR_CONFIGURATION",       nf::ERR_CONFIGURATION);
  PyModule_AddIntConstant(mod, "ERR_CONFIGURATION_LOCK",  nf::ERR_CONFIGURATION_LOCK);
  PyModule_AddIntConstant(mod, "ERR_SUBSCRIBED",          nf::ERR_SUBSCRIBED);
  PyModule_AddIntConstant(mod, "ERR_READONLY",            nf::ERR_READONLY);
  PyModule_AddIntConstant(mod, "ERR_WRITEONLY",           nf::ERR_WRITEONLY);
  PyModule_AddIntConstant(mod, "ERR_SUBSCRIBERS",         nf::ERR_SUBSCRIBERS);
  PyModule_AddIntConstant(mod, "ERR_CLIENT",              nf::ERR_CLIENT);
  PyModule_AddIntConstant(mod, "ERR_GROUP",               nf::ERR_GROUP);
  PyModule_AddIntConstant(mod, "ERR_PERMISSION",          nf::ERR_PERMISSION);
  PyModule_AddIntConstant(mod, "ERR_MESSAGE",             nf::ERR_MESSAGE);
  PyModule_AddIntConstant(mod, "ERR_MESSAGE_SIZE",        nf::ERR_MESSAGE_SIZE);
  PyModule_AddIntConstant(mod, "ERR_ALREADY_EXIST",       nf::ERR_ALREADY_EXIST);
  PyModule_AddIntConstant(mod, "ERR_MESSAGE_TYPE",        nf::ERR_MESSAGE_TYPE);
  PyModule_AddIntConstant(mod, "ERR_MESSAGE_NAME",        nf::ERR_MESSAGE_NAME);
  PyModule_AddIntConstant(mod, "ERR_MESSAGE_FORMAT",      nf::ERR_MESSAGE_FORMAT);
  PyModule_AddIntConstant(mod, "ERR_OPEN",                nf::ERR_OPEN);
  PyModule_AddIntConstant(mod, "ERR_TOO_MANY_CLIENTS",    nf::ERR_TOO_MANY_CLIENTS);
  PyModule_AddIntConstant(mod, "ERR_TOO_MANY_GROUPS",     nf::ERR_TOO_MANY_GROUPS);
  PyModule_AddIntConstant(mod, "ERR_TRUNCATED",           nf::ERR_TRUNCATED);
  PyModule_AddIntConstant(mod, "ERR_MEMORY",              nf::ERR_MEMORY);
  PyModule_AddIntConstant(mod, "ERR_INVALID_DESTINATION", nf::ERR_INVALID_DESTINATION);
  PyModule_AddIntConstant(mod, "ERR_INVALID_SOURCE",      nf::ERR_INVALID_SOURCE);
  PyModule_AddIntConstant(mod, "ERR_VERSION",             nf::ERR_VERSION);
  PyModule_AddIntConstant(mod, "ERR_CONNECTION",          nf::ERR_CONNECTION);
  PyModule_AddIntConstant(mod, "ERR_TIMEOUT",             nf::ERR_TIMEOUT);
  PyModule_AddIntConstant(mod, "ERR_PARAMETER",           nf::ERR_PARAMETER);
  PyModule_AddIntConstant(mod, "ERR_IMPLEMENTED",         nf::ERR_IMPLEMENTED);
  PyModule_AddIntConstant(mod, "ERR_INITIALIZED",         nf::ERR_INITIALIZED);
  PyModule_AddIntConstant(mod, "ERR_IO",                  nf::ERR_IO);
  PyModule_AddIntConstant(mod, "ERR_WIN32",               nf::ERR_WIN32);
  PyModule_AddIntConstant(mod, "ERR_UNEXPECTED",          nf::ERR_UNEXPECTED);
  PyModule_AddIntConstant(mod, "ERR_CONTEXT",             nf::ERR_CONTEXT);
  PyModule_AddIntConstant(mod, "ERR_IGNORE",              nf::ERR_IGNORE);
  PyModule_AddIntConstant(mod, "ERR_USER",                nf::ERR_USER);

  PyModule_AddIntConstant(mod, "MD_SYS_STATUS",           nf::MD_SYS_STATUS);
  PyModule_AddIntConstant(mod, "MD_SYS_ECHO_REQUEST",     nf::MD_SYS_ECHO_REQUEST);
  PyModule_AddIntConstant(mod, "MD_SYS_ECHO_REPLY",       nf::MD_SYS_ECHO_REPLY);
  PyModule_AddIntConstant(mod, "MD_SYS_STOP_REQUEST",     nf::MD_SYS_STOP_REQUEST);
  PyModule_AddIntConstant(mod, "MD_SYS_TERMINATE_REQUEST",nf::MD_SYS_TERMINATE_REQUEST);
  PyModule_AddIntConstant(mod, "MD_SYS_NOTIFY_OPEN",      nf::MD_SYS_NOTIFY_OPEN);
  PyModule_AddIntConstant(mod, "MD_SYS_NOTIFY_SUBSCRIBE", nf::MD_SYS_NOTIFY_SUBSCRIBE);
  PyModule_AddIntConstant(mod, "MD_SYS_NOTIFY_CONFIGURE", nf::MD_SYS_NOTIFY_CONFIGURE);

#if PY_MAJOR_VERSION >= 3
  return mod;
#else
  error__ = PyErr_NewException("mb.error", NULL, NULL);
  Py_INCREF(error__);

  PyModule_AddObject(mod, "error", error__);
#endif
}
