#include "global.h"
#include "page.h"
#include "file.h"
#include "inifile.h"
#include "dialogs.h"
#include "sixpad.h"
#include<sstream>
#include<unordered_map>
#include<fcntl.h>
using namespace std;

#define FF_CASE 1
#define FF_REGEX 2
#define FF_UPWARDS 4

struct TextDeleted: UndoState {
int start, end;
tstring text;
int select;
TextDeleted (int s, int e, const tstring& t, int b = false): start(s), end(e), text(t), select(b) {}
void Redo (Page&);
void Undo (Page&);
bool Join (UndoState&);
int GetTypeId () { return 2; }
};

struct TextInserted: UndoState {
int pos;
tstring text;
bool select;
TextInserted (int s, const tstring& t, bool b = false): pos(s), text(t), select(b) {}
void Redo (Page&);
void Undo (Page&);
bool Join (UndoState&);
int GetTypeId () { return 1; }
};

struct TextReplaced: UndoState {
int pos;
tstring oldText, newText;
bool select;
TextReplaced (int p, const tstring& o, const tstring& n, bool b = true): pos(p), oldText(o), newText(n), select(b)  {}
void Undo (Page&);
void Redo (Page&);
int GetTypeId () { return 3; }
};

struct FindData {
tstring findText, replaceText;
int flags;
inline FindData (const tstring& s=TEXT(""), const tstring& r=TEXT(""), int f=0) : findText(s), replaceText(r), flags(f) {}
inline bool operator== (const FindData& f) { return f.findText==findText && f.replaceText==replaceText && f.flags==flags; }
inline bool operator!= (const FindData& f) { return !(*this==f); }
};
static list<FindData> finds;

void SetClipboardText (const tstring&);
tstring GetClipboardText (void);
void PrepareSmartPaste (tstring& text, const tstring& indent);
tstring GetMenuName (HMENU, UINT, BOOL);
void SetMenuName (HMENU, UINT, BOOL, LPCTSTR);

Page::~Page () { 
}

void Page::SetName (const tstring& n) { 
name = n;
onattrChange(shared_from_this(), PA_NAME, name);
}

void Page::SetEncoding (int e) {
encoding = e;
onattrChange(shared_from_this(), PA_ENCODING, e);
}

void Page::SetLineEnding (int e) {
if (e<0 || e>4) return;
lineEnding = e;
onattrChange(shared_from_this(), PA_LINE_ENDING, e);
}

void Page::SetIndentationMode (int e) {
if (e<0 || e>8) return;
indentationMode = e;
onattrChange(shared_from_this(), PA_INDENTATION_MODE, e);
}

void Page::SetTabWidth (int e) {
if (e<1 || e>8) return;
tabWidth = e;
e*=4; // 1 character corresponds to 4 dialog units
SendMessage(zone, EM_SETTABSTOPS, 1, &e);
InvalidateRect(zone, NULL, TRUE);
onattrChange(shared_from_this(), PA_TAB_WIDTH, e);
}

void Page::SetAutoLineBreak (bool b) {
if (!!b == !!(flags&PF_AUTOLINEBREAK) ) return; // no change, no op
if (b) flags |= PF_AUTOLINEBREAK;
else flags &=~PF_AUTOLINEBREAK;
bool modified  = IsModified();
CreateZone(sp->tabctl);
SetModified(modified);
onattrChange(shared_from_this(), PA_AUTOLINEBREAK, b);
}

bool Page::Close () { 
if (!onclose(shared_from_this() )) return false;
if (flags&PF_WRITETOSTDOUT) { 
setmode(fileno(stdout),O_BINARY);
printf("%s", SaveData().c_str() );
fflush(stdout);
onclosed(shared_from_this());
return true;
}
if (!IsModified()) {
onclosed(shared_from_this());
return true;
}
int re = MessageBox2(sp->win, name, tsnprintf(512, msg("%s has been modified."), name.c_str()), tsnprintf(512, msg("Save changes to %s?"), name.c_str()), {msg("&Save"), msg("Do&n't save"), msg("&Cancel")}, MB2_ICONEXCLAMATION);
if (re==0) {
bool result = Save();
if (result) onclosed(shared_from_this());
return result;
}
else if (re==1) {
onclosed(shared_from_this());
return true;
}
return false;
}

void Page::SetCurrentPosition (int pos) {
if (!zone) return;
SendMessage(zone, EM_SETSEL, pos, pos);
if (IsWindowVisible(zone)) SendMessage(zone, EM_SCROLLCARET, 0, 0);
}

int Page::GetCurrentPosition () {
int pos=0;
SendMessage(zone, EM_GETSEL, 0, &pos);
return pos;
}

void Page::SetCurrentPositionLC (int line, int col) {
if (line>=0 && col>=0) SetCurrentPosition(GetLineStartIndex(line) + col);
}

bool Page::IsEmpty ()  {
return file.size()<=0 && GetWindowTextLength(zone)<=0;
}

bool Page::IsModified () {
return !zone || !!SendMessage(zone, EM_GETMODIFY, 0, 0);
}

void Page::SetModified (bool b) {
SendMessage(zone, EM_SETMODIFY, b, 0);
}

bool Page::IsReadOnly () {
return !!(flags&PF_READONLY);
}

void Page::SetReadOnly (bool b) {
if (b) flags |= PF_READONLY;
else flags &= ~PF_READONLY;
SendMessage(zone, EM_SETREADONLY, b, 0);
}

void Page::Copy ()  { SendMessage(zone, WM_COPY, 0, 0); }
void Page::Cut ()  { SendMessage(zone, WM_CUT, 0, 0); }
void Page::Paste ()  { SendMessage(zone, WM_PASTE, 0, 0); }

void Page::SelectAll () {
SendMessage(zone, EM_SETSEL, 0, -1);
}

void Page::GetSelection (int& start, int& end) {
SendMessage(zone, EM_GETSEL, &start, &end);
}

tstring Page::GetSelectedText ()  {
return EditGetSelectedText(zone);
}

int Page::GetTextLength () {
return GetWindowTextLength(zone);
}

tstring Page::GetText ()  {
return GetWindowText(zone);
}

tstring Page::GetLine (int line) {
return EditGetLine(zone, line);
}

int Page::GetLineCount ()  {
return SendMessage(zone, EM_GETLINECOUNT, 0, 0);
}

int Page::GetLineLength (int line) {
return SendMessage(zone, EM_LINELENGTH, GetLineStartIndex(line), 0);
}

int Page::GetLineStartIndex (int line) {
return SendMessage(zone, EM_LINEINDEX, line, 0);
}

int Page::GetLineOfPos (int pos) {
return SendMessage(zone, EM_LINEFROMCHAR, pos, 0);
}

int Page::GetLineSafeStartIndex (int line) {
int pos = GetLineStartIndex(line);
tstring text = GetLine(line);
int offset = text.find_first_not_of(TEXT("\t "));
if (offset<0 || offset>=text.size()) offset = text.size();
return pos+offset;
}

int Page::GetLineIndentLevel (int line) {
tstring text = GetLine(line);
int pos = text.find_first_not_of(TEXT("\t "));
if (pos<0 || pos>=text.size()) pos=text.size();
return pos / max(1, indentationMode);
}

tstring Page::GetTextSubstring (int start, int end) {
return EditGetSubstring(zone, start, end);
}

void Page::SetSelection (int start, int end) {
SendMessage(zone, EM_SETSEL, start, end);
if (IsWindowVisible(zone)) SendMessage(zone, EM_SCROLLCARET, 0, 0);
}

void Page::SetText (const tstring& str) {
int start, end;
SendMessage(zone, EM_GETSEL, &start, &end);
SetWindowText(zone, str);
SendMessage(zone, EM_SETSEL, start, end);
if (IsWindowVisible(zone)) SendMessage(zone, EM_SCROLLCARET, 0, 0);
}

void Page::ReplaceTextRange (int start, int end, const tstring& newStr, bool keepOldSelection) {
int oldStart, oldEnd;
if (start>0 && end>0 && start>end) { int x=start; start=end; end=x; }
SendMessage(zone, EM_GETSEL, &oldStart, &oldEnd);
tstring oldStr = GetTextSubstring(start, end);
if (start>=0||end>=0) SendMessage(zone, EM_SETSEL, start, end);
SendMessage(zone, EM_REPLACESEL, 0, newStr.c_str());
if (keepOldSelection) SendMessage(zone, EM_SETSEL, oldStart, oldEnd);
else SendMessage(zone, EM_SETSEL, start, start+newStr.size() );
if (IsWindowVisible(zone)) SendMessage(zone, EM_SCROLLCARET, 0, 0);
PushUndoState(shared_ptr<UndoState>(new TextReplaced( start, oldStr, newStr, !keepOldSelection )));
}

void Page::SetSelectedText (const tstring& str) {
int start;
SendMessage(zone, EM_GETSEL, &start, 0);
SendMessage(zone, EM_REPLACESEL, 0, str.c_str());
SendMessage(zone, EM_SETSEL, start, start+str.size());
if (IsWindowVisible(zone)) SendMessage(zone, EM_SCROLLCARET, 0, 0);
}

PyObject* CreatePyPageObject (shared_ptr<Page>);
PyObject* Page::GetPyData () {
if (!pyData) pyData.assign( CreatePyPageObject(shared_from_this()) ,true);
pyData.incref();
return *pyData;
}

bool Page::FindNext () {
if (finds.size()<=0) { FindDialog(); return false; }
FindData& fd = finds.front();
int pos;
SendMessage(zone, EM_GETSEL, 0, &pos);
tstring text = GetWindowText(zone);
auto p = preg_search(text, fd.findText, pos, !(fd.flags&FF_CASE), !(fd.flags&FF_REGEX));
if (p.first>=0 && p.second>=0) {
SendMessage(zone, EM_SETSEL, p.first, p.second);
SendMessage(zone, EM_SCROLLCARET, 0, 0);
return true;
}
else {
MessageBeep(MB_ICONASTERISK);
return false;
}}

bool Page::FindPrev () {
HWND& edit = zone;
if (finds.size()<=0) { FindDialog(); return false; }
FindData& fd = finds.front();
int pos;
SendMessage(edit, EM_GETSEL, &pos, 0);
tstring text = GetWindowText(edit);
auto p = preg_rsearch(text, fd.findText, pos, !(fd.flags&FF_CASE), !(fd.flags&FF_REGEX));
if (p.first>=0 && p.second>=0) {
SendMessage(edit, EM_SETSEL, p.first, p.second);
SendMessage(edit, EM_SCROLLCARET, 0, 0);
return true;
}
else {
MessageBeep(MB_ICONASTERISK);
return false;
}}

bool Page::Find (const tstring& searchText, bool scase, bool regex, bool up, bool stealthty) {
FindData fd(searchText, TEXT(""), (scase?FF_CASE:0) | (regex?FF_REGEX:0) | (up?FF_UPWARDS:0) );
auto it = find(finds.begin(), finds.end(), fd);
if (it!=finds.end()) { finds.erase(it); stealthty=false; }
finds.push_front(fd);
bool re;
if (up) re = FindPrev();
else re = FindNext();
if (stealthty) finds.pop_front();
return re;
}

void Page::FindReplace (const tstring& searchText, const tstring& replaceText, bool scase, bool regex, bool stealthty) {
if (!stealthty) {
FindData fd(searchText, replaceText, (scase?FF_CASE:0) | (regex?FF_REGEX:0) );
auto it = find(finds.begin(), finds.end(), fd);
if (it!=finds.end()) finds.erase(it); 
finds.push_front(fd);
}
int start, end;
SendMessage(zone, EM_GETSEL, &start, &end);
tstring oldText = GetWindowText(zone);
if (start!=end) oldText = tstring(oldText.begin()+start, oldText.begin()+end);
tstring newText = preg_replace(oldText, searchText, replaceText, !scase, !regex);
if (start!=end) {
SendMessage(zone, EM_REPLACESEL, 0, newText.c_str() );
SendMessage(zone, EM_SETSEL, start, start+newText.size());
}
else {
SetWindowText(zone, newText);
SendMessage(zone, EM_SETSEL, start, end);
}
if (IsWindowVisible(zone)) SendMessage(zone, EM_SCROLLCARET, 0, 0);
SendMessage(zone, EM_SETMODIFY, true, 0);
PushUndoState(std::shared_ptr<UndoState>(new TextReplaced( start!=end? start : 0, oldText, newText, start!=end)));
}

static inline tstring FileNameToPageName (Page& p, const tstring& file) {
int pos = file.find_last_of(TEXT("\\/"));
if (pos==tstring::npos) pos = -1;
return file.substr(1+pos);
}

static bool GlobMatches (const tstring& file, const tstring& iniglob) {
wostringstream out;
tstring glob = replace_all_copy(iniglob, TEXT("**"), TEXT("\x1F"));
bool ignore=false;
for (int i=0, n=glob.size(); i<n; i++) {
wchar_t ch = glob[i];
if (ignore) { ignore=false, out<<ch; continue; }
switch(ch){
case '\\': ignore=true; out << '\\'; break;
case '\x1F': out << TEXT(".*"); break;
case '*': out << TEXT("[^/\\\\]*"); break;
case '?': out << TEXT("[^/\\\\]"); break;
case '.': out << TEXT("\\."); break;
case '!': if (i>0&&glob[i -1]=='[') out << (wchar_t)'^'; else out << (wchar_t)'!'; break;
case '{': {
int j = glob.find('}', i+1);
tstring lst = glob.substr(i+1, j-i-1);
i = j;
out << TEXT("(?:") << replace_all_copy(lst, TEXT(","), TEXT("|")) << TEXT(")");
}break;
case '#': case '|': case '&': case '<': case '>': break; // forbidden characters in file names
case '(': case ')': case '+': case '$': case '^': out << '\\' << ch; break;
default: out << ch; break;
}}
tstring pattern = out.str();
auto re = preg_search(file, pattern, 0, true);
return re.first>=0 && re.second>=0 && re.first<=file.size() && re.second<=file.size();
}

static void ReadDotEditorconfigs (const tstring& file, IniFile& ini) {
list<IniFile> iniFiles = { IniFile() };
tstring file2 = file;
while(true){
int pos = file2.find_last_of(TEXT("\\/"));
bool cont = pos>=2&&pos<file2.size();
if (cont) file2 = file2.substr(0, pos+1) + TEXT(".editorconfig");
else break;
IniFile& ini = iniFiles.back();
if (ini.load(file2)) {
if (ini.get("root", false)) break;
else iniFiles.push_back(IniFile());
}
if (!cont) break;
file2 = file2.substr(0,pos);
}
while(iniFiles.size()>1) {
IniFile &first = iniFiles.front(), &second = *(++iniFiles.begin());
second.fusion(first);
iniFiles.pop_front();
}
for (auto sect: iniFiles.back().sections) {
if (!GlobMatches(file, toTString(sect.first))) continue;
ini.fusion(*sect.second);
}}

string Page::SaveData () {
tstring str = GetText();
optional<tstring> re = onsave(shared_from_this(), str);
if (re) str = *re;
if (lineEnding==LE_UNIX) str = replace_all_copy(str, TEXT("\r\n"), TEXT("\n"));
else if (lineEnding==LE_MAC) str = replace_all_copy(str, TEXT("\r\n"), TEXT("\r"));
else if (lineEnding==LE_RS) str = replace_all_copy(str, TEXT("\r\n"), TEXT("\x1E"));
else if (lineEnding==LE_LS) str = replace_all_copy(str, TEXT("\r\n"), TEXT("\x2028"));
string cstr = ConvertToEncoding(str, encoding);
return cstr;
}

bool Page::SaveFile (const tstring& newFile) {
if (flags&PF_NOSAVE) return false;
if ((flags&PF_MUSTSAVEAS) && newFile.size()<=0) return false;
if (newFile.size()>0) {
file = newFile; 
SetName(FileNameToPageName(*this, file));
flags&=~(PF_MUSTSAVEAS|PF_READONLY);
int editorConfigOverride = sp->config->get("editorConfigOverride", 1);
if (editorConfigOverride>0) {
IniFile& ini = dotEditorConfig;
ReadDotEditorconfigs(file, ini);
int le = elt(to_upper_copy(ini.get("end_of_line",string("0"))), lineEnding, {"CRLF", "LF", "CR", "RS", "LS"});
int enc = eltm(to_lower_copy(ini.get("charset",string("0"))), encoding, {{"latin1", 1252}, {"latin-1", 1252}, {"utf-8", 65001}, {"utf8", 65001}, {"utf-16le", 1200}, {"utf-16be", 1201}, {"utf-8-bom", 65002}});
int im = elt(to_lower_copy(ini.get("indent_style",string("0"))), indentationMode, {"tab", "space"});
bool trimEol = ini.get("trim_trailing_whitespace", false); //todo: flag and apply this setting
bool eofNewline = ini.get("insert_final_newline", false); //todo: flag and apply this setting
if (im) im = ini.get("indent_size", 4);
if (le!=lineEnding) SetLineEnding(le);
if (enc!=encoding) SetEncoding(enc);
if (im!=indentationMode) SetIndentationMode(im);
}}
optional<tstring> re = onbeforeSave(shared_from_this(), file);
if (re) file = *re;
if (file.size()<=0) return false;
string cstr = SaveData();
SetModified(false);
File fd(file, true);
if (!fd) return false;
fd.writeFully(cstr.data(), cstr.size());
lastSave = GetCurTime();
return true; 
}

bool Page::Save (bool saveAs) {
if (saveAs || file.size()<=0 || (flags&PF_MUSTSAVEAS)) {
tstring newFile = FileDialog(sp->win, FD_SAVE, file, msg("Save as") );
if (newFile.size()<=0) return false;
if (!SaveFile(newFile)) return false;
}
else if (!SaveFile()) return false;
onsaved(shared_from_this());
return true;
}

int Page::LoadFile (const tstring& filename, bool guessFormat) {
if (filename.size()<=0 && (flags&PF_NORELOAD)) return 0;
if (filename.size()>0) file = filename;
if (file.size()<=0) return 0;
name = FileNameToPageName(*this, file);
File fd(file);
if (!fd) return -GetLastError();
int editorConfigOverride = (!guessFormat?0: sp->config->get("editorConfigOverride", 1));
if (!guessFormat || editorConfigOverride<=0) return LoadData(fd.readFully(), guessFormat);
IniFile& ini = dotEditorConfig;
ReadDotEditorconfigs(file, ini);
if (editorConfigOverride==2) {
lineEnding = elt(to_upper_copy(ini.get("end_of_line",string("0"))), sp->config->get("defaultLineEnding", LE_DOS), {"CRLF", "LF", "CR", "RS", "LS"});
encoding = eltm(to_lower_copy(ini.get("charset",string("0"))), sp->config->get("defaultEncoding", (int)GetACP()), {{"latin1", 1252}, {"latin-1", 1252}, {"utf-8", 65001}, {"utf8", 65001}, {"utf-16le", 1200}, {"utf-16be", 1201}, {"utf-8-bom", 65002}});
indentationMode = elt(to_lower_copy(ini.get("indent_style",string("0"))), sp->config->get("defaultIndentationMode", 0), {"tab", "space"});
if (indentationMode) indentationMode = ini.get("indent_size", indentationMode);
tabWidth = ini.get("tab_width", ini.get("indent_size", indentationMode));
guessFormat=false;
}
auto result = LoadData(fd.readFully(), guessFormat);
if (editorConfigOverride>=1) {
lineEnding = elt(to_upper_copy(ini.get("end_of_line",string("0"))), lineEnding, {"CRLF", "LF", "CR", "RS", "LS"});
encoding = eltm(to_lower_copy(ini.get("charset",string("0"))), encoding, {{"latin1", 1252}, {"latin-1", 1252}, {"utf-8", 65001}, {"utf8", 65001}, {"utf-16le", 1200}, {"utf-16be", 1201}, {"utf-8-bom", 65002}});
indentationMode = elt(to_lower_copy(ini.get("indent_style",string("0"))), indentationMode, {"tab", "space"});
if (indentationMode) indentationMode = ini.get("indent_size", indentationMode);
tabWidth = ini.get("tab_width", ini.get("indent_size", indentationMode));
if (!ini.get("_6p_auto_indent", true)) flags |= PF_NOAUTOINDENT;
if (ini.get("_6p_auto_line_break", false)) flags |= PF_AUTOLINEBREAK;
if (!ini.get("_6p_smart_home", true)) flags |= PF_NOSMARTHOME;
if (!ini.get("_6p_safe_indent", true)) flags |= PF_NOSAFEINDENT;
if (!ini.get("_6p_smart_paste", true)) flags |= PF_NOSMARTPASTE;
bool trimEol = ini.get("trim_trailing_whitespace", false); //todo: flag this setting
bool eofNewline = ini.get("insert_final_newline", false); //todo: flag this setting
}
return result;
}

bool Page::LoadData (const string& str, bool guessFormat) {
tstring text = TEXT("");
if (guessFormat) { encoding=-1; lineEnding=-1; indentationMode=-1; tabWidth=-3; }
if (encoding<0) encoding = guessEncoding( (const unsigned char*)(str.data()), str.size(), sp->config->get("defaultEncoding", (int)GetACP()) );
text = ConvertFromEncoding(str, encoding);
if (lineEnding<0) lineEnding = guessLineEnding(text.data(), text.size(), sp->config->get("defaultLineEnding", LE_DOS)  );
if (lineEnding==LE_UNIX) text = replace_all_copy(text, TEXT("\n"), TEXT("\r\n"));
else if (lineEnding==LE_MAC) text = replace_all_copy(text, TEXT("\r"), TEXT("\r\n"));
else if (lineEnding==LE_RS) text = replace_all_copy(text, TEXT("\x1E"), TEXT("\r\n"));
else if (lineEnding==LE_LS) {
text = replace_all_copy(text, TEXT("\x2028"), TEXT("\r\n"));
text = replace_all_copy(text, TEXT("\x2029"), TEXT("\r\n\r\n"));
}
if (indentationMode<0) indentationMode = guessIndentationMode(text.data(), text.size(), sp->config->get("defaultIndentationMode", 0)  );
if (indentationMode>0) tabWidth = indentationMode;
else tabWidth = sp->config->get("defaultTabWidth", 4);
optional<tstring> re = onload(shared_from_this(), text);
if (re) text = *re;
lastSave = GetCurTime();
SetText(text);
return true;
}

bool Page::CheckFileModification () {
if (file.size()<=0) return false;
unsigned long long lastMod = GetFileTime(file.c_str(), LAST_MODIFIED_TIME);
return lastMod>0 && lastSave>0 && lastMod>lastSave;
}

static tstring StatusBarUpdate (HWND hEdit, HWND status, Page* p) {
int spos=-1, epos=-1;
SendMessage(hEdit, EM_GETSEL, &spos, &epos);
int sline = SendMessage(hEdit, EM_LINEFROMCHAR, spos, 0);
int scolumn = spos - SendMessage(hEdit, EM_LINEINDEX, sline, 0);
if (spos!=epos) {
int eline = SendMessage(hEdit, EM_LINEFROMCHAR, epos, 0);
int ecolumn = epos - SendMessage(hEdit, EM_LINEINDEX, eline, 0);
return tsnprintf(512, msg("Li %d, Col %d to Li %d, Col %d"), 1+sline, 1+scolumn, 1+eline, 1+ecolumn);
} else {
int nlines = SendMessage(hEdit, EM_GETLINECOUNT, 0, 0);
int max = GetWindowTextLength(hEdit);
int prc = max? 100 * spos / max :0;
return tsnprintf(512, msg("Li %d, Col %d.\t%d%%, %d lines"), 1+sline, 1+scolumn, prc, nlines);
}}

void Page::UpdateStatusBar (HWND hStatus) {
tstring text = StatusBarUpdate(zone, hStatus, this);
optional<tstring> re = onstatus(shared_from_this(), text);
if (re) text = *re;
SetWindowText(hStatus, text);
}

static INT_PTR CALLBACK GoToLineDlgProc (HWND hwnd, UINT umsg, WPARAM wp, LPARAM lp) {
static Page* page = 0;
switch (umsg) {
case WM_INITDIALOG : {
page = (Page*)(lp);
HWND edit = page->zone;
int num = SendMessage(edit, EM_GETLINECOUNT, 0, 0);
SetWindowText(hwnd, msg("Go to line") );
SetDlgItemText(hwnd, 1001, tsnprintf(128, msg("Enter a line number between 1 and %d"), num)+TEXT(":") );
SetDlgItemText(hwnd, IDOK, msg("&OK") );
SetDlgItemText(hwnd, IDCANCEL, msg("Ca&ncel") ); 
num = SendMessage(edit, EM_LINEFROMCHAR, -1, 0);
SetDlgItemInt(hwnd, 1002, num+1, FALSE);
}return TRUE;
case WM_COMMAND :
switch (LOWORD(wp)) {
case IDOK : {
HWND edit = page->zone;
tstring tmp = GetDlgItemText(hwnd, 1002);
int num = toInt(tmp);
if (tmp[0]=='+' || tmp[0]=='-') num += SendMessage(edit, EM_LINEFROMCHAR, -1, 0);
else --num;
int max = SendMessage(edit, EM_GETLINECOUNT, 0, 0);
if (num<0) num=0;
else if (num>=max) num=max-1;
int pos = SendMessage(edit, EM_LINEINDEX, num, 0);
page->SetCurrentPosition(pos);
}
case IDCANCEL : EndDialog(hwnd, wp); return TRUE;
}}
return FALSE;
}

void Page::GoToDialog () {
DialogBoxParam(dllHinstance, IDD_GOTOLINE, sp->win, GoToLineDlgProc, this);
}

static INT_PTR CALLBACK FindReplaceDlgProc (HWND hwnd, UINT umsg, WPARAM wp, LPARAM lp) {
static Page* page = 0;
switch (umsg) {
case WM_INITDIALOG : {
bool findOnly = (lp&1)==0;
page = (Page*)(lp&0xFFFFFFFCL);
FindData fd = finds.size()>0? finds.front() : FindData(TEXT(""), TEXT(""), 0);
SetWindowText(hwnd, msg(findOnly? "Find" : "Search and replace"));
SetDlgItemText(hwnd, IDOK, msg(!findOnly? "Replace &all" : "&OK") );
SetDlgItemText(hwnd, IDCANCEL, msg("Ca&ncel"));
SetDlgItemText(hwnd, 2000, msg("&Search for") + TEXT(":") );
SetDlgItemText(hwnd, 2001, msg("&Replace with") + TEXT(":") );
SetDlgItemText(hwnd, 2002, msg("Direction")+TEXT(":") );
SetDlgItemText(hwnd, 1003, msg("&Case sensitive"));
SetDlgItemText(hwnd, 1004, msg("Regular e&xpression"));
SetDlgItemText(hwnd, 1005, msg("&Up"));
SetDlgItemText(hwnd, 1006, msg("&Down"));
EnableDlgItem(hwnd, 1002, !findOnly);
EnableDlgItem(hwnd, 1005, findOnly);
EnableDlgItem(hwnd, 1006, findOnly);
SetDlgItemText(hwnd, 1001, fd.findText.c_str() );
SetDlgItemText(hwnd, 1002, fd.replaceText.c_str() );
SendMessage(GetDlgItem(hwnd, 1003), BM_SETCHECK, (fd.flags&FF_CASE)?BST_CHECKED:BST_UNCHECKED, 0);
SendMessage(GetDlgItem(hwnd, 1004), BM_SETCHECK, (fd.flags&FF_REGEX)?BST_CHECKED:BST_UNCHECKED, 0);
SendMessage(GetDlgItem(hwnd, (fd.flags&FF_UPWARDS)?1005:1006), BM_SETCHECK, BST_CHECKED, 0);
HWND hFindCb = GetDlgItem(hwnd,1001), hReplCb = GetDlgItem(hwnd,1002);
for (FindData& f: finds) {
SendMessage(hFindCb, CB_ADDSTRING, 0, f.findText.c_str() );
SendMessage(hReplCb, CB_ADDSTRING, 0, f.replaceText.c_str() );
}
return TRUE;
}//WM_INITDIALOG
case WM_COMMAND :
switch (LOWORD(wp)) {
case IDOK : {
BOOL sr = IsDlgItemEnabled(hwnd, 1002);
BOOL searchCase = IsDlgButtonChecked(hwnd, 1003);
BOOL searchRegex = IsDlgButtonChecked(hwnd, 1004);
BOOL searchUp = IsDlgButtonChecked(hwnd, 1005);
tstring searchText = GetDlgItemText(hwnd, 1001);
tstring replaceText = GetDlgItemText(hwnd, 1002);
if (searchRegex) try {
preg_check(searchText, true);
} catch (const exception& e) {
MessageBox(hwnd, toTString(e.what()).c_str(), msg("Error").c_str(), MB_OK | MB_ICONERROR);
SetDlgItemFocus(hwnd, 1001);
return true;
}
if (sr) page->FindReplace(searchText, replaceText, searchCase, searchRegex, false);
else page->Find(searchText, searchCase, searchRegex, searchUp, false);
}
case IDCANCEL : EndDialog(hwnd, wp); return TRUE;
}}
return FALSE;
}

static inline void FindReplaceDlg2 (Page& tp, bool replace) {
DWORD val = (DWORD)&tp;
if (replace) val++;
DialogBoxParam(dllHinstance, IDD_SEARCHREPLACE, sp->win, FindReplaceDlgProc, val);
}

void Page::FindDialog () {
FindReplaceDlg2(*this,false);
}

void Page::FindReplaceDialog () {
FindReplaceDlg2(*this,true);
}

static int EZGetNextParagPos (HWND hEdit, int pos) {
int nl=0, len = GetWindowTextLength(hEdit);
HLOCAL hLoc = (HLOCAL)SendMessage(hEdit, EM_GETHANDLE, 0, 0);
LPCTSTR text = (LPCTSTR)LocalLock(hLoc);
while(pos<len) {
TCHAR c = text[pos++];
if (c=='\n' && ++nl>=2) break;
else if (c>32) nl=0;
}
for (int i=pos; i<len && text[i]<=32; i++) if (text[i]=='\n') pos=i+1;
LocalUnlock(hLoc);
return pos;
}

static int EZGetPrevParagPos (HWND hEdit, int pos) {
int nl=0, realBegPos=pos;
HLOCAL hLoc = (HLOCAL)SendMessage(hEdit, EM_GETHANDLE, 0, 0);
LPCTSTR text = (LPCTSTR)LocalLock(hLoc);
while (pos>0 && text[--pos]<=32);
while(pos>0) {
TCHAR c = text[--pos];
if (c=='\n' && ++nl>=2) break;
else if (c>32) { nl=0; realBegPos=pos; }
}
LocalUnlock(hLoc);
return realBegPos;
}

static int EZGetNextBracketPos (HWND hEdit, int pos) {
int len = GetWindowTextLength(hEdit);
HLOCAL hLoc = (HLOCAL)SendMessage(hEdit, EM_GETHANDLE, 0, 0);
LPCTSTR text = (LPCTSTR)LocalLock(hLoc);
pos++;
while(pos<len) {
if (text[pos++]=='}' && (text[pos]=='\n' || text[pos]=='\r')) break;
}
LocalUnlock(hLoc);
return pos;
}

static int EZGetPrevBracketPos (HWND hEdit, int pos) {
int len = GetWindowTextLength(hEdit);
HLOCAL hLoc = (HLOCAL)SendMessage(hEdit, EM_GETHANDLE, 0, 0);
LPCTSTR text = (LPCTSTR)LocalLock(hLoc);
while(pos>0) {
if (text[--pos]=='{' && (text[pos+1]=='\n' || text[pos+1]=='\r')) break;
}
LocalUnlock(hLoc);
return pos;
}

static int EZGetEndIndentedBlockPos (HWND hEdit, int pos) {
int ln = SendMessage(hEdit, EM_LINEFROMCHAR, pos, 0), maxline = SendMessage(hEdit, EM_GETLINECOUNT, 0, 0), maxpos = GetWindowTextLength(hEdit);
tstring indent = EditGetLine(hEdit, ln, pos);
int n = indent.find_first_not_of(TEXT("\t \xA0"));
if (n<0 || n>=indent.size()) n=indent.size();
indent = indent.substr(0,n);
while(++ln<maxline) {
tstring line = EditGetLine(hEdit, ln);
if (!starts_with(line, indent)) break;
}
n = SendMessage(hEdit, EM_LINEINDEX, ln -1, 0);
n +=  SendMessage(hEdit, EM_LINELENGTH, n, 0);
if (pos==n && n<maxpos -2) return EZGetEndIndentedBlockPos(hEdit, pos+2);
return n;
}

static int EZGetStartIndentedBlockPos (HWND hEdit, int pos) {
int ln = SendMessage(hEdit, EM_LINEFROMCHAR, pos, 0);
tstring indent = EditGetLine(hEdit, ln, pos);
int n = indent.find_first_not_of(TEXT("\t \xA0"));
if (n<0 || n>=indent.size()) n=indent.size();
indent = indent.substr(0,n);
while(--ln>=0) {
tstring line = EditGetLine(hEdit, ln);
if (!starts_with(line, indent)) break;
}
n = SendMessage(hEdit, EM_LINEINDEX, ln+1, 0);
if (pos>=n && pos<=n+indent.size() && n>2) return EZGetStartIndentedBlockPos(hEdit, n -2);
return n;
}

static void EZTextInserted (Page* curPage, HWND hwnd, const tstring& text, bool tryToJoin = true) {
int selStart, selEnd;
SendMessage(hwnd, EM_GETSEL, &selStart, &selEnd);
if (selStart!=selEnd) curPage->PushUndoState(shared_ptr<UndoState>(new TextDeleted(selStart, selEnd, EditGetSubstring(hwnd, selStart, selEnd), true) ));
curPage->PushUndoState(shared_ptr<UndoState>(new TextInserted(selStart, text, false )) ,tryToJoin);
}

static bool EZHandleBackspace (Page* curPage, HWND hwnd) {
int selStart, selEnd;
SendMessage(hwnd, EM_GETSEL, &selStart, &selEnd);
if (selStart!=selEnd) curPage->PushUndoState(shared_ptr<UndoState>(new TextDeleted(selStart, selEnd, EditGetSubstring(hwnd, selStart, selEnd), true) ));
else if (selStart<=0) return false;
tstring text = EditGetSubstring(hwnd, selStart -1, selStart);
if (text[0]=='\n') {
text = EditGetSubstring(hwnd, selStart -2, selStart);
curPage->PushUndoState(shared_ptr<UndoState>(new TextDeleted(selStart -2, selStart, text, false) ));
return false;
}
else if (!(curPage->flags&PF_NOSAFEINDENT) && (text[0]==' ' || text[0]=='\t')) {
int ln = SendMessage(hwnd, EM_LINEFROMCHAR, selStart, 0);
int li = SendMessage(hwnd, EM_LINEINDEX, ln, 0);
tstring line = EditGetLine(hwnd, ln);
int pos = line.find_first_not_of(TEXT("\t "));
if (pos<0 || pos>line.size()) pos=line.size();
if (selStart<li+pos) { // Within the indent characters, prevent accidental erase
MessageBeep(MB_OK);
return true;
}}
curPage->PushUndoState(shared_ptr<UndoState>(new TextDeleted(selStart -1, selStart, text, false) ));
return false;
}

static bool EZHandleDel (Page* curPage, HWND hwnd, bool normal) {
int selStart, selEnd;
SendMessage(hwnd, EM_GETSEL, &selStart, &selEnd);
if (selStart!=selEnd) {
curPage->PushUndoState(shared_ptr<UndoState>(new TextDeleted(selStart, selEnd, EditGetSubstring(hwnd, selStart, selEnd), true) ));
return false;
}
else if (selStart>=GetWindowTextLength(hwnd)) return false;
tstring text = EditGetSubstring(hwnd, selStart, selStart+1);
if (text==TEXT("\r")) {
if (normal) text = EditGetSubstring(hwnd, selStart, selStart+2);
else {
text = EditGetSubstring(hwnd, selStart, selStart+100);
int pos = text.find_first_not_of(TEXT(" \t\r\n"));
if (pos<2 || pos>=text.size()) pos=2;
if (pos==3 && text[2]==' ') pos=2;
text = text.substr(0, pos);
}
curPage->PushUndoState(shared_ptr<UndoState>(new TextDeleted(selStart, selStart+text.size(), text, 2) ));
SendMessage(hwnd, EM_SETSEL, selStart, selStart+text.size());
}
else curPage->PushUndoState(shared_ptr<UndoState>(new TextDeleted(selStart, selStart+1, text, 2) ));
return false;
}

static LRESULT EZHandleEnter (Page* page, HWND hEdit, bool noAutoIndent) {
int pos=0, nLine=0, addIndent=0;
SendMessage(hEdit, EM_GETSEL, &pos, 0);
nLine = SendMessage(hEdit, EM_LINEFROMCHAR, pos, 0);
tstring addString, line = EditGetLine(hEdit, nLine, pos);
any re = page->onenter(page->shared_from_this(), line, nLine);
if (!re.empty()) {
{ bool* B = any_cast<bool>(&re); if (B&&!*B) return false; }
{ int* I = any_cast<int>(&re); if (I) addIndent = *I; }
{ tstring* S = any_cast<tstring>(&re); if (S) addString = *S; }
}
pos = noAutoIndent? 0 : line.find_first_not_of(TEXT("\t \xA0"));
if (pos<0 || pos>=line.size()) pos=line.size();
if (addIndent<0) pos = max(0, pos + addIndent * max(1, page->indentationMode));
line = line.substr(0,pos);
if (addIndent>0) for (int i=0, n=min(addIndent,100); i<n; i++) {
if (page->indentationMode<=0) line += TEXT("\t");
else line += tstring(page->indentationMode, ' ');
}
tstring repl = TEXT("\r\n") + line + addString;
EZTextInserted(page, hEdit, repl, false);
SendMessage(hEdit, EM_REPLACESEL, TRUE, (LPARAM)repl.c_str() );
SendMessage(hEdit, EM_SCROLLCARET, 0, 0);
return true;
}

static LRESULT EZHandleHome (HWND hEdit, bool normal) {
int pos=0, nLine=0, offset=0;
SendMessage(hEdit, EM_GETSEL, &pos, 0);
nLine = SendMessage(hEdit, EM_LINEFROMCHAR, pos, 0);
offset = SendMessage(hEdit, EM_LINEINDEX, nLine, 0);
if (normal) {
SendMessage(hEdit, EM_SETSEL, offset, offset);
return true;
}
tstring line = EditGetLine(hEdit, nLine, pos);
pos = line.find_first_not_of(TEXT("\t \xA0"));
if (pos<0 || pos>=line.size()) pos=line.size();
SendMessage(hEdit, EM_SETSEL, offset+pos, offset+pos);
return true;
}

static LRESULT EZHandleShiftHome (HWND hEdit, bool normal) {
if (normal) return false;
int ss=0, se=0, ssL=0, seL=0, offset=0;
SendMessage(hEdit, EM_GETSEL, &ss, &se);
ssL = SendMessage(hEdit, EM_LINEFROMCHAR, ss, 0);
seL = SendMessage(hEdit, EM_LINEFROMCHAR, se, 0);
if (ssL!=seL) return false; // selection spends multiple lines, normal shift+home behavior
offset = SendMessage(hEdit, EM_LINEINDEX, ssL, 0);
tstring line = EditGetLine(hEdit, ssL);
se = line.find_first_not_of(TEXT("\t \xA0"));
if (se<0 || se>=line.size()) se=line.size();
SendMessage(hEdit, EM_SETSEL, ss, offset+se);
return true;
}

template<class F> static LRESULT EZHandleMoveDown (HWND hEdit, const F& f, bool moveHome=true) {
int pos=0;
SendMessage(hEdit, EM_GETSEL, 0, &pos);
pos = f(hEdit, pos);
SendMessage(hEdit, EM_SETSEL, pos, pos);
if (moveHome) return EZHandleHome(hEdit, false);
else return true;
}

template <class F> static LRESULT EZHandleMoveUp (HWND hEdit, const F& f, bool moveHome=true) {
int pos=0;
SendMessage(hEdit, EM_GETSEL, 0, &pos);
pos = f(hEdit, pos);
SendMessage(hEdit, EM_SETSEL, pos, pos);
if (moveHome) return EZHandleHome(hEdit, false);
else return true;
}

template <class F> static LRESULT EZHandleSelectDown (HWND hEdit, const F& f) {
int spos=0, pos=0;
SendMessage(hEdit, EM_GETSEL, &spos, &pos);
pos = f(hEdit, pos);
SendMessage(hEdit, EM_SETSEL, spos, pos);
return true;
}

template <class F> static LRESULT EZHandleSelectUp (HWND hEdit, const F& f) {
int spos=0, pos=0;
SendMessage(hEdit, EM_GETSEL, &spos, &pos);
pos = f(hEdit, pos);
SendMessage(hEdit, EM_SETSEL, spos, pos);
return true;
}

static LRESULT EZHandleTab (Page* curPage, HWND hEdit) {
int sPos=0, ePos=0;
SendMessage(hEdit, EM_GETSEL, &sPos, &ePos);
int sLine = SendMessage(hEdit, EM_LINEFROMCHAR, sPos, 0);
int sOffset = SendMessage(hEdit, EM_LINEINDEX, sLine, 0);
if (sPos!=ePos) { // There is a selection, indent/deindent
int eLine = SendMessage(hEdit, EM_LINEFROMCHAR, ePos, 0);
int eOffset = SendMessage(hEdit, EM_LINEINDEX, eLine, 0);
int eLineLen = SendMessage(hEdit, EM_LINELENGTH, ePos, 0);
tstring oldStr = EditGetSubstring(hEdit, sOffset, eOffset + eLineLen);
tstring newStr = oldStr;
tstring indent = curPage->indentationMode==0? TEXT("\t") : tstring(curPage->indentationMode, ' ');
if (IsShiftDown()) newStr = preg_replace(newStr, TEXT("^")+indent, TEXT(""));
else newStr = preg_replace(newStr, TEXT("^"), indent);
SendMessage(hEdit, EM_SETSEL, sOffset, eOffset+eLineLen);
SendMessage(hEdit, EM_REPLACESEL, 0, newStr.c_str());
SendMessage(hEdit, EM_SETSEL, sOffset, sOffset+newStr.size());
curPage->PushUndoState(shared_ptr<UndoState>(new TextReplaced( sOffset, oldStr, newStr, true )));
}
else { // There is no selection
tstring line = EditGetLine(hEdit, sLine, sPos);
int pos = line.find_first_not_of(TEXT("\t \xA0"));
if (pos<0) pos = line.size();
if (sPos > sOffset+pos) return curPage->indentationMode>0 || IsShiftDown();
if (IsShiftDown()) {
for (int i=0; i<1 || i<curPage->indentationMode; i++) SendMessage(hEdit, WM_CHAR, VK_BACK, 0);
return true;
}
else { // shift not down
if (curPage->indentationMode>0) for (int i=0; i<curPage->indentationMode; i++) SendMessage(hEdit, WM_CHAR, 32, 0);
return curPage->indentationMode>0;
}}}

static LRESULT CALLBACK EditProc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclassId, Page* curPage) {
switch(msg){
case WM_CHAR: {
TCHAR cc = LOWORD(wp);
any re = curPage->onkeyPress(curPage->shared_from_this(), tstring(&cc,1));
if (!re.empty()) {
{ bool *B = any_cast<bool>(&re); if (B&&!*B) return true; }
{ int* I = any_cast<int>(&re); if (I) wp = *I; }
tstring* S = any_cast<tstring>(&re);
if (S) {
SendMessage(hwnd, EM_REPLACESEL, 0, S->c_str() );
EZTextInserted(curPage, hwnd, *S); 
return true;
}}
switch(cc) {
case VK_RETURN: 
return EZHandleEnter(curPage, hwnd, curPage->flags&PF_NOAUTOINDENT);
case VK_TAB: 
if (EZHandleTab(curPage, hwnd)) return true; 
EZTextInserted(curPage, hwnd, TEXT("\t")); 
break;
case VK_BACK:  if (EZHandleBackspace(curPage, hwnd)) return true; break;
default: 
EZTextInserted(curPage, hwnd, tstring(1,LOWORD(wp)) ); 
break;
}}break;//WM_CHAR
case WM_KEYUP: case WM_SYSKEYUP: {
int kc = LOWORD(wp) | GetCurrentModifiers();
if (!curPage->onkeyUp(curPage->shared_from_this(), kc)) return true;
curPage->UpdateStatusBar(sp->status);
}break;//WM_KEYUP
case WM_KEYDOWN :  case WM_SYSKEYDOWN:  {
int kc = LOWORD(wp) | GetCurrentModifiers();
if (!curPage->onkeyDown(curPage->shared_from_this(), kc)) return true;
switch(kc){
case VK_DOWN | VKM_CTRL | VKM_SHIFT : return EZHandleSelectDown(hwnd, EZGetNextParagPos);
case VK_DOWN | VKM_CTRL: return EZHandleMoveDown(hwnd, EZGetNextParagPos);
case VK_DOWN | VKM_ALT | VKM_SHIFT: return EZHandleSelectDown(hwnd, EZGetNextBracketPos);
case VK_DOWN | VKM_ALT: return EZHandleMoveDown(hwnd, EZGetNextBracketPos, false);
case VK_UP | VKM_SHIFT | VKM_CTRL: return EZHandleSelectUp(hwnd, EZGetPrevParagPos);
case VK_UP | VKM_CTRL: return EZHandleMoveUp(hwnd, EZGetPrevParagPos);
case VK_UP | VKM_ALT | VKM_SHIFT: return EZHandleSelectUp(hwnd, EZGetPrevBracketPos);
case VK_UP | VKM_ALT: return EZHandleMoveUp(hwnd, EZGetPrevBracketPos, false);
case VK_LEFT | VKM_ALT | VKM_SHIFT: return EZHandleSelectUp(hwnd, EZGetStartIndentedBlockPos);
case VK_LEFT | VKM_ALT: return EZHandleMoveUp(hwnd, EZGetStartIndentedBlockPos);
case VK_RIGHT | VKM_ALT | VKM_SHIFT: return EZHandleSelectDown(hwnd, EZGetEndIndentedBlockPos);
case VK_RIGHT | VKM_ALT: return EZHandleMoveDown(hwnd, EZGetEndIndentedBlockPos, false);
case VK_HOME: return EZHandleHome(hwnd, curPage->flags&PF_NOSMARTHOME);
case VK_HOME | VKM_ALT: return EZHandleHome(hwnd, true);
case VK_HOME | VKM_SHIFT: if (EZHandleShiftHome(hwnd, curPage->flags&PF_NOSMARTHOME)) return true; break;
case VK_HOME | VKM_SHIFT | VKM_ALT: if (EZHandleShiftHome(hwnd, true)) return true; break;
case VK_DELETE:  if (EZHandleDel(curPage, hwnd, curPage->flags&PF_NOSAFEINDENT)) return true; break;
}}break;//WM_KEYDOWN/WM_SYSKEYDOWN
case WM_PASTE : {
int start, end;
SendMessage(hwnd, EM_GETSEL, &start, &end);
tstring line = EditGetLine(hwnd);
tstring str = GetClipboardText();
if (!(curPage->flags&PF_NOSMARTPASTE)) {
int pos = line.find_first_not_of(TEXT(" \t"));
if (pos>=line.size()) pos=line.size();
tstring indent = line.substr(0,pos);
PrepareSmartPaste(str, indent);
}
if (start!=end) curPage->PushUndoState(shared_ptr<UndoState>(new TextDeleted(start, end, EditGetSubstring(hwnd, start, end), true) ));
curPage->PushUndoState(shared_ptr<UndoState>(new TextInserted(start, str, false )));
SendMessage(hwnd, EM_REPLACESEL, 0, str.c_str());
return true;
}break;//WM_PASTE
case WM_COPY: {
int spos=0, epos=0;
SendMessage(hwnd, EM_GETSEL, &spos, &epos);
if (spos==epos) {
tstring str = EditGetLine(hwnd);
SetClipboardText(str);
return true;
}}break;//WM_COPY
case WM_CUT: {
int spos=0, epos=0;
SendMessage(hwnd, EM_GETSEL, &spos, &epos);
if (spos==epos) {
int lnum = SendMessage(hwnd, EM_LINEFROMCHAR, spos, 0);
int lindex = SendMessage(hwnd, EM_LINEINDEX, lnum, 0);
int llen = SendMessage(hwnd, EM_LINELENGTH, spos, 0);
curPage->PushUndoState(shared_ptr<UndoState>(new TextDeleted(lindex, lindex+llen+2, EditGetSubstring(hwnd, lindex, lindex+llen+2), false) ));
SendMessage(hwnd, EM_SETSEL, lindex+llen, lindex+llen+2);
SendMessage(hwnd, EM_REPLACESEL, 0, 0);
SendMessage(hwnd, EM_SETSEL, lindex, lindex+llen);
}
else curPage->PushUndoState(shared_ptr<UndoState>(new TextDeleted(spos, epos, EditGetSubstring(hwnd, spos, epos), true) ));
}break;//WM_CUT
case WM_CONTEXTMENU:
if (curPage->oncontextMenu(curPage->shared_from_this(), GetCurrentModifiers() )) {
POINT p;
GetCursorPos(&p);
HMENU menu = GetSubMenu(GetMenu(sp->win), 1);
TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, p.x, p.y, 0, hwnd, NULL);
}
return true;
case WM_LBUTTONUP:
curPage->UpdateStatusBar(sp->status);
break;
case WM_MOUSEWHEEL: {
int delta = -GET_WHEEL_DELTA_WPARAM(wp);
SendMessage(hwnd, EM_SCROLL, delta>0? SB_LINEDOWN : SB_LINEUP, 0);
}return true;
case EM_CANUNDO: return curPage->undoStates.size()>0;
case EM_EMPTYUNDOBUFFER: curPage->undoStates.erase(curPage->undoStates.begin(), curPage->undoStates.end()); return true;
case WM_UNDO: case EM_UNDO: curPage->Undo(); return true;
}//switch(msg)
return DefSubclassProc(hwnd, msg, wp, lp);
}

void Page::CreateZone (HWND parent, bool subclass) {
static int count = 0;
tstring text;
int ss=0, se=0;
if (zone) {
SendMessage(zone, EM_GETSEL, &ss, &se);
text = GetWindowText(zone);
DestroyWindow(zone);
}
HWND hEdit  = CreateWindowEx(0, TEXT("EDIT"), TEXT(""),
WS_CHILD | WS_TABSTOP | WS_VSCROLL | WS_BORDER | ES_MULTILINE | ES_NOHIDESEL | ES_AUTOVSCROLL | ((flags&PF_AUTOLINEBREAK)? 0:ES_AUTOHSCROLL|WS_HSCROLL),
10, 10, 400, 400,
parent, (HMENU)(IDC_EDITAREA + count++), sp->hinstance, NULL);
SendMessage(hEdit, EM_SETLIMITTEXT, 1073741823, 0);
SendMessage(hEdit, WM_SETFONT, sp->font, TRUE);
{ int x=tabWidth>0? tabWidth*4:16; SendMessage(hEdit, EM_SETTABSTOPS, 1, &x); }
SetWindowText(hEdit, text.c_str());
SendMessage(hEdit, EM_SETSEL, ss, se);
SendMessage(hEdit, EM_SCROLLCARET, 0, 0);
if (subclass) SetWindowSubclass(hEdit, (SUBCLASSPROC)EditProc, 0, (DWORD_PTR)this);
zone=hEdit;
}

void Page::HideZone () {
ShowWindow(zone, SW_HIDE);
EnableWindow(zone, FALSE);
for (auto& it: groups) HidePageGroup(it.second);
}

void Page::HidePageGroup (shared_ptr<PageGroup> group) {
if (!group) return;
for (auto& item: group->menus) {
item.label = GetMenuString(item.menu, item.id, MF_BYCOMMAND);
item.name = GetMenuName(item.menu, item.id, FALSE);
RemoveMenu(item.menu, item.id, MF_BYCOMMAND);
}}

void Page::ShowPageGroup (shared_ptr<PageGroup> group) {
if (!group) return;
for (auto& item: group->menus) {
InsertMenu(item.menu, item.pos, MF_BYPOSITION | MF_STRING | item.flags, item.id, item.label.c_str() );
if (item.name.size()>0) SetMenuName(item.menu, item.id, FALSE, item.name.c_str());
}}

void Page::ShowZone (const RECT& r) {
for (auto& it: groups) ShowPageGroup(it.second);
DrawMenuBar(sp->win);
EnableWindow(zone, TRUE);
SetWindowPos(zone, NULL,
r.left+3, r.top+3, r.right - r.left -6, r.bottom - r.top -6,
SWP_NOZORDER | SWP_SHOWWINDOW);
SendMessage(zone, EM_SCROLLCARET, 0, 0);
}

void Page::AddPageGroup (shared_ptr<PageGroup> group) {
if (!group) return;
auto it = groups.find(group->name);
if (it!=groups.end()) return;
groups[group->name] = group;
ShowPageGroup(group);
}

void Page::RemovePageGroup (shared_ptr<PageGroup> group) {
if (!group) return;
auto it = groups.find(group->name);
if (it==groups.end()) return;
groups.erase(group->name);
HidePageGroup(group);
}

void Page::Focus () {
onattrChange(shared_from_this(), PA_FOCUS, nullptr);
}

void Page::FocusZone () {
SetFocus(zone);
}

void Page::ResizeZone (const RECT& r) {
MoveWindow(zone, r.left+3, r.top+3, r.right-r.left -6, r.bottom-r.top -6, true);
}

void Page::SetFont (HFONT font) {
if (zone) SendMessage(zone, WM_SETFONT, font, true);
}

void export PageReplaceIndent (shared_ptr<Page> page, int oldIndent, int newIndent) {
tstring text = page->GetText();
tstring indentUnit(max(newIndent,1), newIndent<=0? '\t' : ' ');
vector<tstring> lines = split(text, TEXT("\n"));
for (tstring& line: lines) {
int pos = line.find_first_not_of(TEXT("\t "));
if (pos<0 || pos>=line.size()) pos = line.size();
int count = pos / max(1, oldIndent);
line.replace(0, pos, count*indentUnit);
}
text = join(lines, TEXT("\n"));
page->SetText(text);
}

void Page::PushUndoState (shared_ptr<UndoState> u, bool tryToJoin) {
if (curUndoState<undoStates.size()) undoStates.erase(undoStates.begin() + curUndoState, undoStates.end() );
if (tryToJoin && curUndoState>0 && curUndoState<=undoStates.size() && undoStates[curUndoState -1]->Join(*u)) return;
if (undoStates.size()>=50) undoStates.erase(undoStates.begin());
undoStates.push_back(u);
curUndoState = undoStates.size();
}

void Page::Undo () {
if (curUndoState<1 || curUndoState>undoStates.size()) {
MessageBeep(MB_OK);
return;
}
undoStates[--curUndoState]->Undo(*this);
}

void Page::Redo () {
if (curUndoState>=undoStates.size()) {
MessageBeep(MB_OK);
return;
}
undoStates[curUndoState++]->Redo(*this);
}

void TextDeleted::Redo (Page& p) {
SendMessage(p.zone, EM_SETSEL, start, end);
SendMessage(p.zone, EM_REPLACESEL, 0, 0);
if (IsWindowVisible(p.zone)) SendMessage(p.zone, EM_SCROLLCARET, 0, 0);
}

void TextDeleted::Undo (Page& p) {
SendMessage(p.zone, EM_SETSEL, start, start);
SendMessage(p.zone, EM_REPLACESEL, 0, text.c_str() );
if (select==2) SendMessage(p.zone, EM_SETSEL, start, start);
else if (select) SendMessage(p.zone, EM_SETSEL, start, end);
if (IsWindowVisible(p.zone)) SendMessage(p.zone, EM_SCROLLCARET, 0, 0);
}

void TextInserted::Redo (Page& p) {
SendMessage(p.zone, EM_SETSEL, pos, pos);
SendMessage(p.zone, EM_REPLACESEL, 0, text.c_str() );
if (select)SendMessage(p.zone, EM_SETSEL, pos, pos+text.size() );
if (IsWindowVisible(p.zone)) SendMessage(p.zone, EM_SCROLLCARET, 0, 0);
}

void TextInserted::Undo (Page& p) {
SendMessage(p.zone, EM_SETSEL, pos, pos+text.size());
SendMessage(p.zone, EM_REPLACESEL, 0, 0);
if (IsWindowVisible(p.zone)) SendMessage(p.zone, EM_SCROLLCARET, 0, 0);
}

bool TextInserted::Join (UndoState& u0) {
if (u0.GetTypeId()!=1) return false;
TextInserted& u = static_cast<TextInserted&>(u0);
if (u.pos == pos + text.size() ) {
text += u.text;
return true;
}
return false;
}

bool TextDeleted::Join (UndoState& u0) {
if (u0.GetTypeId()!=2) return false;
TextDeleted& u = static_cast<TextDeleted&>(u0);
if (u.end==start) {
text = u.text + text;
start = u.start;
return true;
}
else if (start==u.start) {
text += u.text;
end += (u.end-u.start);
return true;
}
return false;
}

void TextReplaced::Redo (Page& p) {
SendMessage(p.zone, EM_SETSEL, pos, pos+oldText.size());
SendMessage(p.zone, EM_REPLACESEL, 0, newText.c_str() );
if (select) SendMessage(p.zone, EM_SETSEL, pos, pos+newText.size());
if (IsWindowVisible(p.zone)) SendMessage(p.zone, EM_SCROLLCARET, 0, 0);
}

void TextReplaced::Undo (Page& p) {
SendMessage(p.zone, EM_SETSEL, pos, pos+newText.size());
SendMessage(p.zone, EM_REPLACESEL, 0, oldText.c_str() );
if (select) SendMessage(p.zone, EM_SETSEL, pos, pos+oldText.size());
if (IsWindowVisible(p.zone)) SendMessage(p.zone, EM_SCROLLCARET, 0, 0);
}

unordered_map<int,connection> connections;

int export AddSignalConnection (const connection& con) {
for (auto it=connections.begin(); it!=connections.end(); ) {
if (!it->second.connected()) connections.erase(it++);
else ++it;
}
static int i=100;
connections[++i] = con;
return i;
}

connection export RemoveSignalConnection (int id) {
connection con = connections[id];
for (auto it=connections.begin(); it!=connections.end(); ) {
if (!it->second.connected()) connections.erase(it++);
else ++it;
}
return con;
}

int Page::AddEvent (const string& type, PyGenericFunc cb) {
connection con;
if(false){}
#define E(n) else if (type==#n) con = on##n .connect(AsPyFunc<typename decltype(on##n)::signature_type>(cb.o));
E(keyDown) E(keyUp) E(keyPress)
E(save) E(beforeSave) E(load)
E(attrChange) E(status) E(fileDropped) E(contextMenu) E(enter)
E(activated) E(deactivated)
#undef E
if (con.connected()) return AddSignalConnection(con);
else return 0;
}

bool Page::RemoveEvent (const string& type, int id) {
connection con = RemoveSignalConnection(id);
bool re = con.connected();
con.disconnect();
return re;
}


