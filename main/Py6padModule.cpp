#include "global.h"
#include "strings.hpp"
#include "IniFile.h"
#include "File.h"
#include "python34.h"
#include "Resource.h"
#include "Thread.h"
#include "sixpad.h"
#include "UniversalSpeech.h"
using namespace std;

extern IniFile config, msgs;
extern tstring appPath, appDir, appName, configFileName, appLocale;
extern vector<tstring> argv;

tstring ConsoleRead (void);
void ConsolePrint (const tstring& str, bool say);
void SetClipboardText (const tstring&);
tstring GetClipboardText (void);
tstring msg (const char* name);
void ReloadConfig();

bool PyRegister_Window (PyObject* m); 
bool PyRegister_Page(PyObject* m);
bool PyRegister_MenuItem (PyObject* m);
bool PyRegister_TaskDialog (PyObject* m);
PyObject* CreatePyWindowObject ();

static int PyInclude (const string& fn) {
bool result = false;
FILE* fp = fopen(fn.c_str(), "r");
if (fp) {
result = !PyRun_SimpleFile(fp, fn.c_str() );
fclose(fp);
if (!result) PyErr_Print();
}
return result;
}

static tstring ConsoleReadImpl (void) {
tstring s;
Py_BEGIN_ALLOW_THREADS
s = ConsoleRead();
Py_END_ALLOW_THREADS
return s;
}

static optional<string> PyGetConfig (const string& key, OPT, optional<string> def) {
auto it = config.find(key);
if (it!=config.end()) return it->second;
else return def;
}

static void PySetConfig (const string& key, const string& value, OPT, bool allowMulti) {
if (!key.empty()&&!value.empty()) config.set2(string(key), string(value), allowMulti);
}
static constexpr const char* confGetMulti_KWLST[] = {"key", "value", "multiple", NULL};

static vector<string> PyGetConfigMulti (const string& key) {
vector<string> list;
for (auto it=config.find(key); it!=config.end(); ++it) list.push_back(it->second);
return list;
}

static bool LoadDLLExtension (const string& name) {
HINSTANCE dll = LoadLibrary(toTString(name).c_str());
if (!dll) return false;
SixpadDLLInitFunc func = (SixpadDLLInitFunc)GetProcAddress(dll, "SixpadDLLInit");
if (!func) {
FreeLibrary(dll);
return false;
}
RunSync([&]()mutable{ func(&sp); });
return true;
}

static void PyLoadExtension (const string& name) {
if (ends_with(name, ".py")) PyInclude(name);
else if (ends_with(name, ".dll")) LoadDLLExtension(name);
else if (!PyImport_ImportModule(name.c_str())) PyErr_Print();
}

static bool PyLoadLang (const tstring& langfile) {
return msgs.load(langfile);
}

static void PyBraille (const tstring& str) {
brailleDisplay(str.c_str());
}

static bool PySayStr (const tstring& str, OPT, bool interrupt) {
return speechSay(str.c_str(), interrupt);
}

static bool PyIsUIThread () {
return IsUIThread();
}

static PyMethodDef _6padMainDefs[] = {
// Translation management
PyDecl("msg", msg),

// Configuration management
PyDecl("getConfig", PyGetConfig),
PyDeclKW("setConfig", PySetConfig, confGetMulti_KWLST),
PyDecl("getConfigAsList", PyGetConfigMulti),
PyDecl("reloadConfig", ReloadConfig),

// Clipboard management
PyDecl("setClipboardText", SetClipboardText),
PyDecl("getClipboardText", GetClipboardText),

// Speech and screen readers
PyDecl("stopSpeech", speechStop),
PyDecl("say", PySayStr),
PyDecl("braille", PyBraille),

// Extension, includes and other general functions
PyDecl("include", PyInclude),
PyDecl("loadExtension", PyLoadExtension),
PyDecl("loadTranslation", PyLoadLang),
PyDecl("isUIThread", PyIsUIThread),
PyDecl("preg_replace", preg_replace),

// Overload of print, to be able to print in python console GUI
PyDecl("sysPrint", ConsolePrint),
// Console read for interactive interpreter ithin python console GUI
PyDecl("sysRead", ConsoleReadImpl),
PyDeclEnd
};

static PyModuleDef _6padMainMod = {
PyModuleDef_HEAD_INIT,
"sixpad",
NULL, -1,  _6padMainDefs 
};

PyMODINIT_FUNC PyInit_6padMain (void) {
PyObject* mod = PyModule_Create(&_6padMainMod);
PyRegister_Window(mod);
PyRegister_MenuItem(mod);
PyRegister_Page(mod);
PyRegister_TaskDialog(mod);
//PyRegister_MyObj(mod);
PyModule_AddObject(mod, "window", CreatePyWindowObject() );
PyModule_AddObject(mod, "locale", Py_BuildValue("u", appLocale.c_str()));
PyModule_AddObject(mod, "appdir", Py_BuildValue("u", appDir.c_str()));
PyModule_AddObject(mod, "appfullpath", Py_BuildValue("u", appPath.c_str()));
PyModule_AddObject(mod, "appname", Py_BuildValue("u", appName.c_str()));
PyModule_AddObject(mod, "configfile", Py_BuildValue("u", configFileName.c_str()));
return mod;
}

void PyStart (void) {
wstring modulePath = toWString( appDir + TEXT("\\python34.zip;") + appDir + TEXT("\\lib;") + appDir + TEXT("\\plugins") );
Py_SetPath( modulePath.c_str() );
Py_SetProgramName(const_cast<wchar_t*>(toWString(argv[0]).c_str()));
PyImport_AppendInittab("sixpad", PyInit_6padMain);;
Py_Initialize();
{
int argc=0;
wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &argc);
args[0][0]=0;
PySys_SetArgvEx(argc, args, false);
LocalFree(args);
}
PyEval_InitThreads();
GIL_PROTECT
{
Resource res(TEXT("init.py"),257);
auto code = res.copy();
bool failed = !!PyRun_SimpleString(&code[0]);
if (failed) exit(1);
}
RunSync([](){});//Barrier to wait for the main loop to start
if (!sp.nacked) {
auto p=config.equal_range("extension"); 
for(auto it=p.first; it!=p.second; ++it) {
string name = it->second;
PyLoadExtension(name);
}}
string pyfn = toString(appDir + TEXT("\\") + appName + TEXT(".py") , CP_ACP);
PyInclude(pyfn);
for (auto arg: argv) {
if (starts_with(arg, TEXT("/extension="))) PyLoadExtension(toString(arg.substr(11)));
else if (starts_with(arg, TEXT("/run="))) PyInclude(toString(arg.substr(5)));
}
// Ohter initialization stuff goes here
if (sp.headless) RunSync([&]()mutable{ PostQuitMessage(0); });
if (PyRun_SimpleString("import code") || PyRun_SimpleString("code.interact(banner='')") ) exit(1);
}
