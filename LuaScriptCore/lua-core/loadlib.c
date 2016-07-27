/*
** $Id: loadlib.c,v 1.127 2015/11/23 11:30:45 roberto Exp $
** Dynamic library loader for Lua
** See Copyright Notice in lua.h
**
** This module contains an implementation of loadlib for Unix systems
** that have dlfcn, an implementation for Windows, and a stub for other
** systems.
*/

#define loadlib_c
#define LUA_LIB

#include "lprefix.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


/*
** LUA_PATH_VAR and LUA_CPATH_VAR are the names of the environment
** variables that Lua check to set its paths.
*/
#if !defined(LUA_PATH_VAR)
#define LUA_PATH_VAR	"LUA_PATH"
#endif

#if !defined(LUA_CPATH_VAR)
#define LUA_CPATH_VAR	"LUA_CPATH"
#endif

#define LUA_PATHSUFFIX		"_" LUA_VERSION_MAJOR "_" LUA_VERSION_MINOR

#define LUA_PATHVARVERSION		LUA_PATH_VAR LUA_PATHSUFFIX
#define LUA_CPATHVARVERSION		LUA_CPATH_VAR LUA_PATHSUFFIX

/*
** LUA_PATH_SEP is the character that separates templates in a path.
** LUA_PATH_MARK is the string that marks the substitution points in a
** template.
** LUA_EXEC_DIR in a Windows path is replaced by the executable's
** directory.
** LUA_IGMARK is a mark to ignore all before it when building the
** luaopen_ function name.
*/
#if !defined (LUA_PATH_SEP)
#define LUA_PATH_SEP		";"
#endif
#if !defined (LUA_PATH_MARK)
#define LUA_PATH_MARK		"?"
#endif
#if !defined (LUA_EXEC_DIR)
#define LUA_EXEC_DIR		"!"
#endif
#if !defined (LUA_IGMARK)
#define LUA_IGMARK		"-"
#endif


/*
** LUA_CSUBSEP is the character that replaces dots in submodule names
** when searching for a C loader.
** LUA_LSUBSEP is the character that replaces dots in submodule names
** when searching for a Lua loader.
*/
#if !defined(LUA_CSUBSEP)
#define LUA_CSUBSEP		LUA_DIRSEP
#endif

#if !defined(LUA_LSUBSEP)
#define LUA_LSUBSEP		LUA_DIRSEP
#endif


/* prefix for open functions in C libraries */
#define LUA_POF		"luaopen_"

/* separator for open functions in C libraries */
#define LUA_OFSEP	"_"


/*
** unique key for table in the registry that keeps handles
** for all loaded C libraries
*/
static const int CLIBS = 0;

#define LIB_FAIL	"open"

#define setprogdir(L)		((void)0)


/*
** system-dependent functions
*/

/*
** unload library 'lib'
*/
static void lsys_unloadlib (void *lib);

/*
** load C library in file 'path'. If 'seeglb', load with all names in
** the library global.
** Returns the library; in case of error, returns NULL plus an
** error string in the stack.
*/
static void *lsys_load (NameDef(lua_State) *L, const char *path, int seeglb);

/*
** Try to find a function named 'sym' in library 'lib'.
** Returns the function; in case of error, returns NULL plus an
** error string in the stack.
*/
static NameDef(lua_CFunction) lsys_sym (NameDef(lua_State) *L, void *lib, const char *sym);




#if defined(LUA_USE_DLOPEN)	/* { */
/*
** {========================================================================
** This is an implementation of loadlib based on the dlfcn interface.
** The dlfcn interface is available in Linux, SunOS, Solaris, IRIX, FreeBSD,
** NetBSD, AIX 4.2, HPUX 11, and  probably most other Unix flavors, at least
** as an emulation layer on top of native functions.
** =========================================================================
*/

#include <dlfcn.h>

/*
** Macro to convert pointer-to-void* to pointer-to-function. This cast
** is undefined according to ISO C, but POSIX assumes that it works.
** (The '__extension__' in gnu compilers is only to avoid warnings.)
*/
#if defined(__GNUC__)
#define cast_func(p) (__extension__ (NameDef(lua_CFunction))(p))
#else
#define cast_func(p) ((NameDef(lua_CFunction))(p))
#endif


static void lsys_unloadlib (void *lib) {
  dlclose(lib);
}


static void *lsys_load (NameDef(lua_State) *L, const char *path, int seeglb) {
  void *lib = dlopen(path, RTLD_NOW | (seeglb ? RTLD_GLOBAL : RTLD_LOCAL));
  if (lib == NULL) NameDef(lua_pushstring)(L, dlerror());
  return lib;
}


static NameDef(lua_CFunction) lsys_sym (NameDef(lua_State) *L, void *lib, const char *sym) {
  NameDef(lua_CFunction) f = cast_func(dlsym(lib, sym));
  if (f == NULL) NameDef(lua_pushstring)(L, dlerror());
  return f;
}

/* }====================================================== */



#elif defined(LUA_DL_DLL)	/* }{ */
/*
** {======================================================================
** This is an implementation of loadlib for Windows using native functions.
** =======================================================================
*/

#include <windows.h>

#undef setprogdir

/*
** optional flags for LoadLibraryEx
*/
#if !defined(LUA_LLE_FLAGS)
#define LUA_LLE_FLAGS	0
#endif


static void setprogdir (NameDef(lua_State) *L) {
  char buff[MAX_PATH + 1];
  char *lb;
  DWORD nsize = sizeof(buff)/sizeof(char);
  DWORD n = GetModuleFileNameA(NULL, buff, nsize);
  if (n == 0 || n == nsize || (lb = strrchr(buff, '\\')) == NULL)
    luaL_error(L, "unable to get ModuleFileName");
  else {
    *lb = '\0';
    luaL_gsub(L, lua_tostring(L, -1), LUA_EXEC_DIR, buff);
    lua_remove(L, -2);  /* remove original string */
  }
}


static void pusherror (NameDef(lua_State) *L) {
  int error = GetLastError();
  char buffer[128];
  if (FormatMessageA(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
      NULL, error, 0, buffer, sizeof(buffer)/sizeof(char), NULL))
    NameDef(lua_pushstring)(L, buffer);
  else
    NameDef(lua_pushfstring)(L, "system error %d\n", error);
}

static void lsys_unloadlib (void *lib) {
  FreeLibrary((HMODULE)lib);
}


static void *lsys_load (NameDef(lua_State) *L, const char *path, int seeglb) {
  HMODULE lib = LoadLibraryExA(path, NULL, LUA_LLE_FLAGS);
  (void)(seeglb);  /* not used: symbols are 'global' by default */
  if (lib == NULL) pusherror(L);
  return lib;
}


static NameDef(lua_CFunction) lsys_sym (NameDef(lua_State) *L, void *lib, const char *sym) {
  NameDef(lua_CFunction) f = (NameDef(lua_CFunction))GetProcAddress((HMODULE)lib, sym);
  if (f == NULL) pusherror(L);
  return f;
}

/* }====================================================== */


#else				/* }{ */
/*
** {======================================================
** Fallback for other systems
** =======================================================
*/

#undef LIB_FAIL
#define LIB_FAIL	"absent"


#define DLMSG	"dynamic libraries not enabled; check your Lua installation"


static void lsys_unloadlib (void *lib) {
  (void)(lib);  /* not used */
}


static void *lsys_load (NameDef(lua_State) *L, const char *path, int seeglb) {
  (void)(path); (void)(seeglb);  /* not used */
  lua_pushliteral(L, DLMSG);
  return NULL;
}


static NameDef(lua_CFunction) lsys_sym (NameDef(lua_State) *L, void *lib, const char *sym) {
  (void)(lib); (void)(sym);  /* not used */
  lua_pushliteral(L, DLMSG);
  return NULL;
}

/* }====================================================== */
#endif				/* } */


/*
** return registry.CLIBS[path]
*/
static void *checkclib (NameDef(lua_State) *L, const char *path) {
  void *plib;
  NameDef(lua_rawgetp)(L, LUA_REGISTRYINDEX, &CLIBS);
  NameDef(lua_getfield)(L, -1, path);
  plib = NameDef(lua_touserdata)(L, -1);  /* plib = CLIBS[path] */
  lua_pop(L, 2);  /* pop CLIBS table and 'plib' */
  return plib;
}


/*
** registry.CLIBS[path] = plib        -- for queries
** registry.CLIBS[#CLIBS + 1] = plib  -- also keep a list of all libraries
*/
static void addtoclib (NameDef(lua_State) *L, const char *path, void *plib) {
  NameDef(lua_rawgetp)(L, LUA_REGISTRYINDEX, &CLIBS);
  NameDef(lua_pushlightuserdata)(L, plib);
  NameDef(lua_pushvalue)(L, -1);
  NameDef(lua_setfield)(L, -3, path);  /* CLIBS[path] = plib */
  NameDef(lua_rawseti)(L, -2, NameDef(luaL_len)(L, -2) + 1);  /* CLIBS[#CLIBS + 1] = plib */
  lua_pop(L, 1);  /* pop CLIBS table */
}


/*
** __gc tag method for CLIBS table: calls 'lsys_unloadlib' for all lib
** handles in list CLIBS
*/
static int gctm (NameDef(lua_State) *L) {
  NameDef(lua_Integer) n = NameDef(luaL_len)(L, 1);
  for (; n >= 1; n--) {  /* for each handle, in reverse order */
    NameDef(lua_rawgeti)(L, 1, n);  /* get handle CLIBS[n] */
    lsys_unloadlib(NameDef(lua_touserdata)(L, -1));
    lua_pop(L, 1);  /* pop handle */
  }
  return 0;
}



/* error codes for 'lookforfunc' */
#define ERRLIB		1
#define ERRFUNC		2

/*
** Look for a C function named 'sym' in a dynamically loaded library
** 'path'.
** First, check whether the library is already loaded; if not, try
** to load it.
** Then, if 'sym' is '*', return true (as library has been loaded).
** Otherwise, look for symbol 'sym' in the library and push a
** C function with that symbol.
** Return 0 and 'true' or a function in the stack; in case of
** errors, return an error code and an error message in the stack.
*/
static int lookforfunc (NameDef(lua_State) *L, const char *path, const char *sym) {
  void *reg = checkclib(L, path);  /* check loaded C libraries */
  if (reg == NULL) {  /* must load library? */
    reg = lsys_load(L, path, *sym == '*');  /* global symbols if 'sym'=='*' */
    if (reg == NULL) return ERRLIB;  /* unable to load library */
    addtoclib(L, path, reg);
  }
  if (*sym == '*') {  /* loading only library (no function)? */
    NameDef(lua_pushboolean)(L, 1);  /* return 'true' */
    return 0;  /* no errors */
  }
  else {
    NameDef(lua_CFunction) f = lsys_sym(L, reg, sym);
    if (f == NULL)
      return ERRFUNC;  /* unable to find function */
    lua_pushcfunction(L, f);  /* else create new function */
    return 0;  /* no errors */
  }
}


static int ll_loadlib (NameDef(lua_State) *L) {
  const char *path = luaL_checkstring(L, 1);
  const char *init = luaL_checkstring(L, 2);
  int stat = lookforfunc(L, path, init);
  if (stat == 0)  /* no errors? */
    return 1;  /* return the loaded function */
  else {  /* error; error message is on stack top */
    NameDef(lua_pushnil)(L);
    lua_insert(L, -2);
    NameDef(lua_pushstring)(L, (stat == ERRLIB) ?  LIB_FAIL : "init");
    return 3;  /* return nil, error message, and where */
  }
}



/*
** {======================================================
** 'require' function
** =======================================================
*/


static int readable (const char *filename) {
  FILE *f = fopen(filename, "r");  /* try to open file */
  if (f == NULL) return 0;  /* open failed */
  fclose(f);
  return 1;
}


static const char *pushnexttemplate (NameDef(lua_State) *L, const char *path) {
  const char *l;
  while (*path == *LUA_PATH_SEP) path++;  /* skip separators */
  if (*path == '\0') return NULL;  /* no more templates */
  l = strchr(path, *LUA_PATH_SEP);  /* find next separator */
  if (l == NULL) l = path + strlen(path);
  NameDef(lua_pushlstring)(L, path, l - path);  /* template */
  return l;
}


static const char *searchpath (NameDef(lua_State) *L, const char *name,
                                             const char *path,
                                             const char *sep,
                                             const char *dirsep) {
  NameDef(luaL_Buffer) msg;  /* to build error message */
  NameDef(luaL_buffinit)(L, &msg);
  if (*sep != '\0')  /* non-empty separator? */
    name = NameDef(luaL_gsub)(L, name, sep, dirsep);  /* replace it by 'dirsep' */
  while ((path = pushnexttemplate(L, path)) != NULL) {
    const char *filename = NameDef(luaL_gsub)(L, lua_tostring(L, -1),
                                     LUA_PATH_MARK, name);
    lua_remove(L, -2);  /* remove path template */
    if (readable(filename))  /* does file exist and is readable? */
      return filename;  /* return that file name */
    NameDef(lua_pushfstring)(L, "\n\tno file '%s'", filename);
    lua_remove(L, -2);  /* remove file name */
    NameDef(luaL_addvalue)(&msg);  /* concatenate error msg. entry */
  }
  NameDef(luaL_pushresult)(&msg);  /* create error message */
  return NULL;  /* not found */
}


static int ll_searchpath (NameDef(lua_State) *L) {
  const char *f = searchpath(L, luaL_checkstring(L, 1),
                                luaL_checkstring(L, 2),
                                luaL_optstring(L, 3, "."),
                                luaL_optstring(L, 4, LUA_DIRSEP));
  if (f != NULL) return 1;
  else {  /* error message is on top of the stack */
    NameDef(lua_pushnil)(L);
    lua_insert(L, -2);
    return 2;  /* return nil + error message */
  }
}


static const char *findfile (NameDef(lua_State) *L, const char *name,
                                           const char *pname,
                                           const char *dirsep) {
  const char *path;
  NameDef(lua_getfield)(L, lua_upvalueindex(1), pname);
  path = lua_tostring(L, -1);
  if (path == NULL)
    NameDef(luaL_error)(L, "'package.%s' must be a string", pname);
  return searchpath(L, name, path, ".", dirsep);
}


static int checkload (NameDef(lua_State) *L, int stat, const char *filename) {
  if (stat) {  /* module loaded successfully? */
    NameDef(lua_pushstring)(L, filename);  /* will be 2nd argument to module */
    return 2;  /* return open function and file name */
  }
  else
    return NameDef(luaL_error)(L, "error loading module '%s' from file '%s':\n\t%s",
                          lua_tostring(L, 1), filename, lua_tostring(L, -1));
}


static int searcher_Lua (NameDef(lua_State) *L) {
  const char *filename;
  const char *name = luaL_checkstring(L, 1);
  filename = findfile(L, name, "path", LUA_LSUBSEP);
  if (filename == NULL) return 1;  /* module not found in this path */
  return checkload(L, (luaL_loadfile(L, filename) == LUA_OK), filename);
}


/*
** Try to find a load function for module 'modname' at file 'filename'.
** First, change '.' to '_' in 'modname'; then, if 'modname' has
** the form X-Y (that is, it has an "ignore mark"), build a function
** name "luaopen_X" and look for it. (For compatibility, if that
** fails, it also tries "luaopen_Y".) If there is no ignore mark,
** look for a function named "luaopen_modname".
*/
static int loadfunc (NameDef(lua_State) *L, const char *filename, const char *modname) {
  const char *openfunc;
  const char *mark;
  modname = NameDef(luaL_gsub)(L, modname, ".", LUA_OFSEP);
  mark = strchr(modname, *LUA_IGMARK);
  if (mark) {
    int stat;
    openfunc = NameDef(lua_pushlstring)(L, modname, mark - modname);
    openfunc = NameDef(lua_pushfstring)(L, LUA_POF"%s", openfunc);
    stat = lookforfunc(L, filename, openfunc);
    if (stat != ERRFUNC) return stat;
    modname = mark + 1;  /* else go ahead and try old-style name */
  }
  openfunc = NameDef(lua_pushfstring)(L, LUA_POF"%s", modname);
  return lookforfunc(L, filename, openfunc);
}


static int searcher_C (NameDef(lua_State) *L) {
  const char *name = luaL_checkstring(L, 1);
  const char *filename = findfile(L, name, "cpath", LUA_CSUBSEP);
  if (filename == NULL) return 1;  /* module not found in this path */
  return checkload(L, (loadfunc(L, filename, name) == 0), filename);
}


static int searcher_Croot (NameDef(lua_State) *L) {
  const char *filename;
  const char *name = luaL_checkstring(L, 1);
  const char *p = strchr(name, '.');
  int stat;
  if (p == NULL) return 0;  /* is root */
  NameDef(lua_pushlstring)(L, name, p - name);
  filename = findfile(L, lua_tostring(L, -1), "cpath", LUA_CSUBSEP);
  if (filename == NULL) return 1;  /* root not found */
  if ((stat = loadfunc(L, filename, name)) != 0) {
    if (stat != ERRFUNC)
      return checkload(L, 0, filename);  /* real error */
    else {  /* open function not found */
      NameDef(lua_pushfstring)(L, "\n\tno module '%s' in file '%s'", name, filename);
      return 1;
    }
  }
  NameDef(lua_pushstring)(L, filename);  /* will be 2nd argument to module */
  return 2;
}


static int searcher_preload (NameDef(lua_State) *L) {
  const char *name = luaL_checkstring(L, 1);
  NameDef(lua_getfield)(L, LUA_REGISTRYINDEX, "_PRELOAD");
  if (NameDef(lua_getfield)(L, -1, name) == LUA_TNIL)  /* not found? */
    NameDef(lua_pushfstring)(L, "\n\tno field package.preload['%s']", name);
  return 1;
}


static void findloader (NameDef(lua_State) *L, const char *name) {
  int i;
  NameDef(luaL_Buffer) msg;  /* to build error message */
  NameDef(luaL_buffinit)(L, &msg);
  /* push 'package.searchers' to index 3 in the stack */
  if (NameDef(lua_getfield)(L, lua_upvalueindex(1), "searchers") != LUA_TTABLE)
    NameDef(luaL_error)(L, "'package.searchers' must be a table");
  /*  iterate over available searchers to find a loader */
  for (i = 1; ; i++) {
    if (NameDef(lua_rawgeti)(L, 3, i) == LUA_TNIL) {  /* no more searchers? */
      lua_pop(L, 1);  /* remove nil */
      NameDef(luaL_pushresult)(&msg);  /* create error message */
      NameDef(luaL_error)(L, "module '%s' not found:%s", name, lua_tostring(L, -1));
    }
    NameDef(lua_pushstring)(L, name);
    lua_call(L, 1, 2);  /* call it */
    if (lua_isfunction(L, -2))  /* did it find a loader? */
      return;  /* module loader found */
    else if (NameDef(lua_isstring)(L, -2)) {  /* searcher returned error message? */
      lua_pop(L, 1);  /* remove extra return */
      NameDef(luaL_addvalue)(&msg);  /* concatenate error message */
    }
    else
      lua_pop(L, 2);  /* remove both returns */
  }
}


static int ll_require (NameDef(lua_State) *L) {
  const char *name = luaL_checkstring(L, 1);
  NameDef(lua_settop)(L, 1);  /* _LOADED table will be at index 2 */
  NameDef(lua_getfield)(L, LUA_REGISTRYINDEX, "_LOADED");
  NameDef(lua_getfield)(L, 2, name);  /* _LOADED[name] */
  if (NameDef(lua_toboolean)(L, -1))  /* is it there? */
    return 1;  /* package is already loaded */
  /* else must load package */
  lua_pop(L, 1);  /* remove 'getfield' result */
  findloader(L, name);
  NameDef(lua_pushstring)(L, name);  /* pass name as argument to module loader */
  lua_insert(L, -2);  /* name is 1st argument (before search data) */
  lua_call(L, 2, 1);  /* run loader to load module */
  if (!lua_isnil(L, -1))  /* non-nil return? */
    NameDef(lua_setfield)(L, 2, name);  /* _LOADED[name] = returned value */
  if (NameDef(lua_getfield)(L, 2, name) == LUA_TNIL) {   /* module set no value? */
    NameDef(lua_pushboolean)(L, 1);  /* use true as result */
    NameDef(lua_pushvalue)(L, -1);  /* extra copy to be returned */
    NameDef(lua_setfield)(L, 2, name);  /* _LOADED[name] = true */
  }
  return 1;
}

/* }====================================================== */



/*
** {======================================================
** 'module' function
** =======================================================
*/
#if defined(LUA_COMPAT_MODULE)

/*
** changes the environment variable of calling function
*/
static void set_env (NameDef(lua_State) *L) {
  NameDef(lua_Debug) ar;
  if (NameDef(lua_getstack)(L, 1, &ar) == 0 ||
      NameDef(lua_getinfo)(L, "f", &ar) == 0 ||  /* get calling function */
      NameDef(lua_iscfunction)(L, -1))
    luaL_error(L, "'module' not called from a Lua function");
  NameDef(lua_pushvalue)(L, -2);  /* copy new environment table to top */
  NameDef(lua_setupvalue)(L, -2, 1);
  lua_pop(L, 1);  /* remove function */
}


static void dooptions (NameDef(lua_State) *L, int n) {
  int i;
  for (i = 2; i <= n; i++) {
    if (lua_isfunction(L, i)) {  /* avoid 'calling' extra info. */
      NameDef(lua_pushvalue)(L, i);  /* get option (a function) */
      NameDef(lua_pushvalue)(L, -2);  /* module */
      lua_call(L, 1, 0);
    }
  }
}


static void modinit (NameDef(lua_State) *L, const char *modname) {
  const char *dot;
  NameDef(lua_pushvalue)(L, -1);
  NameDef(lua_setfield)(L, -2, "_M");  /* module._M = module */
  NameDef(lua_pushstring)(L, modname);
  NameDef(lua_setfield)(L, -2, "_NAME");
  dot = strrchr(modname, '.');  /* look for last dot in module name */
  if (dot == NULL) dot = modname;
  else dot++;
  /* set _PACKAGE as package name (full module name minus last part) */
  NameDef(lua_pushlstring)(L, modname, dot - modname);
  NameDef(lua_setfield)(L, -2, "_PACKAGE");
}


static int ll_module (NameDef(lua_State) *L) {
  const char *modname = luaL_checkstring(L, 1);
  int lastarg = NameDef(lua_gettop)(L);  /* last parameter */
  luaL_pushmodule(L, modname, 1);  /* get/create module table */
  /* check whether table already has a _NAME field */
  if (NameDef(lua_getfield)(L, -1, "_NAME") != LUA_TNIL)
    lua_pop(L, 1);  /* table is an initialized module */
  else {  /* no; initialize it */
    lua_pop(L, 1);
    modinit(L, modname);
  }
  NameDef(lua_pushvalue)(L, -1);
  set_env(L);
  dooptions(L, lastarg);
  return 1;
}


static int ll_seeall (NameDef(lua_State) *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  if (!NameDef(lua_getmetatable)(L, 1)) {
    NameDef(lua_createtable)(L, 0, 1); /* create new metatable */
    NameDef(lua_pushvalue)(L, -1);
    NameDef(lua_setmetatable)(L, 1);
  }
  lua_pushglobaltable(L);
  NameDef(lua_setfield)(L, -2, "__index");  /* mt.__index = _G */
  return 0;
}

#endif
/* }====================================================== */



/* auxiliary mark (for internal use) */
#define AUXMARK		"\1"


/*
** return registry.LUA_NOENV as a boolean
*/
static int noenv (NameDef(lua_State) *L) {
  int b;
  NameDef(lua_getfield)(L, LUA_REGISTRYINDEX, "LUA_NOENV");
  b = NameDef(lua_toboolean)(L, -1);
  lua_pop(L, 1);  /* remove value */
  return b;
}


static void setpath (NameDef(lua_State) *L, const char *fieldname, const char *envname1,
                                   const char *envname2, const char *def) {
  const char *path = getenv(envname1);
  if (path == NULL)  /* no environment variable? */
    path = getenv(envname2);  /* try alternative name */
  if (path == NULL || noenv(L))  /* no environment variable? */
    NameDef(lua_pushstring)(L, def);  /* use default */
  else {
    /* replace ";;" by ";AUXMARK;" and then AUXMARK by default path */
    path = NameDef(luaL_gsub)(L, path, LUA_PATH_SEP LUA_PATH_SEP,
                              LUA_PATH_SEP AUXMARK LUA_PATH_SEP);
    NameDef(luaL_gsub)(L, path, AUXMARK, def);
    lua_remove(L, -2);
  }
  setprogdir(L);
  NameDef(lua_setfield)(L, -2, fieldname);
}


static const NameDef(luaL_Reg) pk_funcs[] = {
  {"loadlib", ll_loadlib},
  {"searchpath", ll_searchpath},
#if defined(LUA_COMPAT_MODULE)
  {"seeall", ll_seeall},
#endif
  /* placeholders */
  {"preload", NULL},
  {"cpath", NULL},
  {"path", NULL},
  {"searchers", NULL},
  {"loaded", NULL},
  {NULL, NULL}
};


static const NameDef(luaL_Reg) ll_funcs[] = {
#if defined(LUA_COMPAT_MODULE)
  {"module", ll_module},
#endif
  {"require", ll_require},
  {NULL, NULL}
};


static void createsearcherstable (NameDef(lua_State) *L) {
  static const NameDef(lua_CFunction) searchers[] =
    {searcher_preload, searcher_Lua, searcher_C, searcher_Croot, NULL};
  int i;
  /* create 'searchers' table */
  NameDef(lua_createtable)(L, sizeof(searchers)/sizeof(searchers[0]) - 1, 0);
  /* fill it with predefined searchers */
  for (i=0; searchers[i] != NULL; i++) {
    NameDef(lua_pushvalue)(L, -2);  /* set 'package' as upvalue for all searchers */
    NameDef(lua_pushcclosure)(L, searchers[i], 1);
    NameDef(lua_rawseti)(L, -2, i+1);
  }
#if defined(LUA_COMPAT_LOADERS)
  NameDef(lua_pushvalue)(L, -1);  /* make a copy of 'searchers' table */
  NameDef(lua_setfield)(L, -3, "loaders");  /* put it in field 'loaders' */
#endif
  NameDef(lua_setfield)(L, -2, "searchers");  /* put it in field 'searchers' */
}


/*
** create table CLIBS to keep track of loaded C libraries,
** setting a finalizer to close all libraries when closing state.
*/
static void createclibstable (NameDef(lua_State) *L) {
  lua_newtable(L);  /* create CLIBS table */
  NameDef(lua_createtable)(L, 0, 1);  /* create metatable for CLIBS */
  lua_pushcfunction(L, gctm);
  NameDef(lua_setfield)(L, -2, "__gc");  /* set finalizer for CLIBS table */
  NameDef(lua_setmetatable)(L, -2);
  NameDef(lua_rawsetp)(L, LUA_REGISTRYINDEX, &CLIBS);  /* set CLIBS table in registry */
}


LUAMOD_API int NameDef(luaopen_package) (NameDef(lua_State) *L) {
  createclibstable(L);
  luaL_newlib(L, pk_funcs);  /* create 'package' table */
  createsearcherstable(L);
  /* set field 'path' */
  setpath(L, "path", LUA_PATHVARVERSION, LUA_PATH_VAR, LUA_PATH_DEFAULT);
  /* set field 'cpath' */
  setpath(L, "cpath", LUA_CPATHVARVERSION, LUA_CPATH_VAR, LUA_CPATH_DEFAULT);
  /* store config information */
  lua_pushliteral(L, LUA_DIRSEP "\n" LUA_PATH_SEP "\n" LUA_PATH_MARK "\n"
                     LUA_EXEC_DIR "\n" LUA_IGMARK "\n");
  NameDef(lua_setfield)(L, -2, "config");
  /* set field 'loaded' */
  NameDef(luaL_getsubtable)(L, LUA_REGISTRYINDEX, "_LOADED");
  NameDef(lua_setfield)(L, -2, "loaded");
  /* set field 'preload' */
  NameDef(luaL_getsubtable)(L, LUA_REGISTRYINDEX, "_PRELOAD");
  NameDef(lua_setfield)(L, -2, "preload");
  lua_pushglobaltable(L);
  NameDef(lua_pushvalue)(L, -2);  /* set 'package' as upvalue for next lib */
  NameDef(luaL_setfuncs)(L, ll_funcs, 1);  /* open lib into global table */
  lua_pop(L, 1);  /* pop global table */
  return 1;  /* return 'package' table */
}

