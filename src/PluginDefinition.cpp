//this file is part of EditorConfig plugin for Notepad++
//
//Copyright (C)2003 Don HO <donho@altern.org>
//Copyright (C)2025 Sven Strickroth <email@cs-ware.de>
//Copyright (C)2011-2016 EditorConfig Team <https://editorconfig.org>
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#include "PluginDefinition.hpp"
#include "menuCmdID.hpp"
#include "DlgAbout.hpp"

#include <map>
#include <memory>
#include <vector>

//
// The plugin data that Notepad++ needs
//
FuncItem funcItem[nbFunc];

//
// The data of Notepad++ that you can use in your plugin commands
//
NppData nppData;

//
// Handle to the Notepad++ instance
//
HINSTANCE hInst;

//
// Initialize your plugin data here
// It will be called while plugin loading
void pluginInit(HANDLE hModule)
{
    hInst = (HINSTANCE) hModule;
}

//
// Here you can do the clean up, save the parameters (if any) for the next session
//
void pluginCleanUp()
{
}

//
// Parse the EditorConfig file
//
static bool parseConfig(editorconfig_handle eh)
{
    WCHAR wcFileName[MAX_PATH];
    // get the file name
    if (::SendMessage(nppData._nppHandle, NPPM_GETFULLCURRENTPATH, sizeof(wcFileName), reinterpret_cast<LPARAM>(wcFileName)) == FALSE)
        return false;
    const size_t bytes_needed = wcstombs(nullptr, wcFileName, 0);
    if (bytes_needed == static_cast<size_t>(-1))
        return false;
    std::unique_ptr<char[]> fileName = std::unique_ptr<char[]>(new (std::nothrow) char[bytes_needed + 1]);
    if (!fileName)
        return false;
    if (wcstombs(fileName.get(), wcFileName, bytes_needed) == static_cast<size_t>(-1))
        return false;

    // start parsing
    int err_num;
    if ((err_num = editorconfig_parse(fileName.get(), eh)) != 0 &&
            /* Ignore full path error, whose error code is
             * EDITORCONFIG_PARSE_NOT_FULL_PATH */
            err_num != EDITORCONFIG_PARSE_NOT_FULL_PATH) {
        std::tstringstream err_msg;
        err_msg << TEXT("EditorConfig Error: ") << err_num;
        ::MessageBox(NULL, err_msg.str().c_str(),
                TEXT("EditorConfig"), MB_OK);
        editorconfig_handle_destroy(eh);
        return false;
    }
    return true;
}

//
// Get the current scintilla
//
static HWND getCurrentScintilla()
{
    int which = -1;
    SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM) &which);
    if (which == -1)
        return NULL;
    return((which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle);
}

//
// Set the syntax highlighting to "INI" for .editorconfig files
//
void setSyntaxFromFilename()
{
    // Retrieve the filename
    WCHAR szFilename[_MAX_PATH];
    SendMessage(nppData._nppHandle, NPPM_GETFILENAME, _MAX_PATH, (LPARAM) szFilename);

    if (wcscmp(szFilename, L".editorconfig") == 0)
        SendMessage(nppData._nppHandle, NPPM_SETCURRENTLANGTYPE, 0, L_INI);
}

//
// Encoding handling
//

// The values returned by NPPM_GETBUFFERENCODING (UniMode of Notepad++)
enum
{
    NPP_UNI_8BIT = 0,       // ANSI
    NPP_UNI_UTF8 = 1,       // UTF-8 with BOM
    NPP_UNI_16BE = 2,       // UTF-16 BE with BOM
    NPP_UNI_16LE = 3,       // UTF-16 LE with BOM
    NPP_UNI_COOKIE = 4,     // UTF-8 without BOM
    NPP_UNI_7BIT = 5,
    NPP_UNI_16BE_NOBOM = 6,
    NPP_UNI_16LE_NOBOM = 7
};

// The values of the EditorConfig "charset" property
enum EcCharset
{
    EC_CHARSET_NONE = 0,    // not specified or unsupported
    EC_CHARSET_LATIN1,
    EC_CHARSET_UTF8,
    EC_CHARSET_UTF8_BOM,
    EC_CHARSET_UTF16BE,
    EC_CHARSET_UTF16LE
};

// Set as soon as Notepad++ has finished starting up
static bool nppReady = false;

// Set while onBeforeSave() is running
static bool inBeforeSave = false;

// Set while the encoding check shows a message box
static bool inEncodingCheck = false;

// The buffers the encoding was already checked for, with the path they had
static std::map<uptr_t, std::wstring> checkedBuffers;

static EcCharset parseCharset(const char* value)
{
    if (strcmp(value, "latin1") == 0)
        return EC_CHARSET_LATIN1;
    if (strcmp(value, "utf-8") == 0)
        return EC_CHARSET_UTF8;
    if (strcmp(value, "utf-8-bom") == 0)
        return EC_CHARSET_UTF8_BOM;
    if (strcmp(value, "utf-16be") == 0)
        return EC_CHARSET_UTF16BE;
    if (strcmp(value, "utf-16le") == 0)
        return EC_CHARSET_UTF16LE;
    return EC_CHARSET_NONE;
}

// The "Convert to ..." menu command of Notepad++ for a charset
static int convertCommand(EcCharset charset)
{
    switch (charset)
    {
        case EC_CHARSET_LATIN1:     return IDM_FORMAT_CONV2_ANSI;
        case EC_CHARSET_UTF8:       return IDM_FORMAT_CONV2_AS_UTF_8;
        case EC_CHARSET_UTF8_BOM:   return IDM_FORMAT_CONV2_UTF_8;
        case EC_CHARSET_UTF16BE:    return IDM_FORMAT_CONV2_UTF_16BE;
        case EC_CHARSET_UTF16LE:    return IDM_FORMAT_CONV2_UTF_16LE;
        default:                    return 0;
    }
}

// "latin1" is mapped to the "ANSI" encoding of Notepad++, which is the
// ANSI code page of the system (e.g. Windows-1252)
static std::wstring charsetName(EcCharset charset)
{
    switch (charset)
    {
        case EC_CHARSET_LATIN1:
        {
            std::wstringstream name;
            name << L"latin1 (ANSI, code page " << ::GetACP() << L")";
            return name.str();
        }
        case EC_CHARSET_UTF8:       return L"utf-8 (UTF-8 without BOM)";
        case EC_CHARSET_UTF8_BOM:   return L"utf-8-bom (UTF-8 with BOM)";
        case EC_CHARSET_UTF16BE:    return L"utf-16be (UTF-16 BE)";
        case EC_CHARSET_UTF16LE:    return L"utf-16le (UTF-16 LE)";
        default:                    return L"";
    }
}

static const wchar_t* uniModeName(int uniMode)
{
    switch (uniMode)
    {
        case NPP_UNI_8BIT:          return L"ANSI";
        case NPP_UNI_UTF8:          return L"UTF-8 with BOM";
        case NPP_UNI_16BE:          return L"UTF-16 BE with BOM";
        case NPP_UNI_16LE:          return L"UTF-16 LE with BOM";
        case NPP_UNI_COOKIE:        return L"UTF-8 without BOM";
        case NPP_UNI_7BIT:          return L"7-bit ASCII";
        case NPP_UNI_16BE_NOBOM:    return L"UTF-16 BE without BOM";
        case NPP_UNI_16LE_NOBOM:    return L"UTF-16 LE without BOM";
        default:                    return L"unknown";
    }
}

static bool matchesCharset(int uniMode, EcCharset charset)
{
    switch (charset)
    {
        case EC_CHARSET_LATIN1:     return uniMode == NPP_UNI_8BIT;
        case EC_CHARSET_UTF8:       return uniMode == NPP_UNI_COOKIE;
        case EC_CHARSET_UTF8_BOM:   return uniMode == NPP_UNI_UTF8;
        case EC_CHARSET_UTF16BE:    return uniMode == NPP_UNI_16BE || uniMode == NPP_UNI_16BE_NOBOM;
        case EC_CHARSET_UTF16LE:    return uniMode == NPP_UNI_16LE || uniMode == NPP_UNI_16LE_NOBOM;
        default:                    return true;
    }
}

//
// Was the current buffer opened with a specific character set (menu
// "Encoding" > "Character sets")? NPPM_GETBUFFERENCODING can't tell (it
// reports "UTF-8 without BOM" for them), so look at the checked menu item.
//
static bool hasExplicitCharacterSet()
{
    HMENU hMenu = reinterpret_cast<HMENU>(::SendMessage(nppData._nppHandle, NPPM_GETMENUHANDLE, NPPMAINMENU, 0));
    if (hMenu == NULL)
        return false;

    for (int id = IDM_FORMAT_ENCODE; id <= IDM_FORMAT_ENCODE_END; ++id)
    {
        const UINT state = ::GetMenuState(hMenu, id, MF_BYCOMMAND);
        if (state != static_cast<UINT>(-1) && (state & MF_CHECKED))
            return true;
    }
    return false;
}

//
// Decode one UTF-8 sequence.
// Returns the length of the sequence, 0 when it is not valid.
//
static size_t decodeUtf8(const unsigned char* p, size_t avail, unsigned int& codePoint)
{
    if (avail == 0)
        return 0;

    const unsigned char c = p[0];
    if (c < 0x80)
    {
        codePoint = c;
        return 1;
    }

    size_t length;
    unsigned int minimum;
    if ((c & 0xE0) == 0xC0)
    {
        length = 2;
        minimum = 0x80;
        codePoint = c & 0x1F;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        length = 3;
        minimum = 0x800;
        codePoint = c & 0x0F;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        length = 4;
        minimum = 0x10000;
        codePoint = c & 0x07;
    }
    else
        return 0;

    if (avail < length)
        return 0;

    for (size_t i = 1; i < length; ++i)
    {
        if ((p[i] & 0xC0) != 0x80)
            return 0;
        codePoint = (codePoint << 6) | (p[i] & 0x3F);
    }

    // Overlong sequences, surrogates and values beyond Unicode are invalid
    if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
        return 0;

    return length;
}

//
// Look at the raw bytes of the document: are they pure ASCII, valid UTF-8?
//
static void analyseDocument(HWND curScintilla, bool& isAscii, bool& isValidUtf8)
{
    isAscii = true;
    isValidUtf8 = true;

    const size_t length = static_cast<size_t>(::SendMessage(curScintilla, SCI_GETLENGTH, 0, 0));
    const unsigned char* text = reinterpret_cast<const unsigned char*>(::SendMessage(curScintilla, SCI_GETCHARACTERPOINTER, 0, 0));
    if (text == nullptr)
        return;

    for (size_t i = 0; i < length;)
    {
        if (text[i] < 0x80)
        {
            ++i;
            continue;
        }

        isAscii = false;

        unsigned int codePoint;
        const size_t sequence = decodeUtf8(text + i, length - i, codePoint);
        if (sequence == 0)
        {
            isValidUtf8 = false;
            return;
        }
        i += sequence;
    }
}

//
// Convert a code point to UTF-16. Returns the number of WCHARs.
//
static int toUtf16(unsigned int codePoint, WCHAR wc[2])
{
    if (codePoint >= 0x10000)
    {
        codePoint -= 0x10000;
        wc[0] = static_cast<WCHAR>(0xD800 + (codePoint >> 10));
        wc[1] = static_cast<WCHAR>(0xDC00 + (codePoint & 0x3FF));
        return 2;
    }

    wc[0] = static_cast<WCHAR>(codePoint);
    return 1;
}

//
// Can the code point be stored in the ANSI code page of the system
// without changing it?
//
static bool isRepresentableInAnsi(unsigned int codePoint)
{
    WCHAR wc[2];
    const int wcLength = toUtf16(codePoint, wc);

    char mb[8];
    BOOL usedDefaultChar = FALSE;
    const int mbLength = ::WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wc, wcLength, mb, sizeof(mb), NULL, &usedDefaultChar);
    return mbLength > 0 && !usedDefaultChar;
}

struct UnmappableChar
{
    intptr_t        pos;
    unsigned int    codePoint;
};

//
// Find the characters of the document that would get lost when it is
// converted to ANSI. Returns the number of those characters, the first
// maxReported ones are added to reported.
//
static size_t findUnmappableChars(HWND curScintilla, std::vector<UnmappableChar>& reported, size_t maxReported)
{
    // Only when Scintilla holds the document as UTF-8, and not when the
    // ANSI code page of the system is UTF-8 itself
    if (::SendMessage(curScintilla, SCI_GETCODEPAGE, 0, 0) != SC_CP_UTF8 || ::GetACP() == CP_UTF8)
        return 0;

    const size_t length = static_cast<size_t>(::SendMessage(curScintilla, SCI_GETLENGTH, 0, 0));
    const unsigned char* text = reinterpret_cast<const unsigned char*>(::SendMessage(curScintilla, SCI_GETCHARACTERPOINTER, 0, 0));
    if (text == nullptr)
        return 0;

    size_t count = 0;
    for (size_t i = 0; i < length;)
    {
        if (text[i] < 0x80)
        {
            ++i;
            continue;
        }

        unsigned int codePoint;
        const size_t sequence = decodeUtf8(text + i, length - i, codePoint);
        if (sequence == 0)
        {
            ++i;
            continue;
        }

        if (!isRepresentableInAnsi(codePoint))
        {
            ++count;
            if (reported.size() < maxReported)
            {
                UnmappableChar unmappable;
                unmappable.pos = static_cast<intptr_t>(i);
                unmappable.codePoint = codePoint;
                reported.push_back(unmappable);
            }
        }
        i += sequence;
    }

    return count;
}

static std::wstring getCurrentFileName()
{
    WCHAR fileName[MAX_PATH];
    fileName[0] = 0;
    ::SendMessage(nppData._nppHandle, NPPM_GETFILENAME, MAX_PATH, reinterpret_cast<LPARAM>(fileName));
    return fileName;
}

//
// When the document contains characters that can't be stored in ANSI,
// ask the user what to do. Returns true if the document can be converted.
//
static bool confirmConvertToAnsi(HWND curScintilla, int uniMode)
{
    const size_t maxReported = 5;
    std::vector<UnmappableChar> reported;
    const size_t count = findUnmappableChars(curScintilla, reported, maxReported);
    if (count == 0)
        return true;

    std::wstringstream msg;
    msg << getCurrentFileName() << L"\n\n"
        << L"The EditorConfig charset of this file is " << charsetName(EC_CHARSET_LATIN1)
        << L", but " << count << (count == 1 ? L" character" : L" characters")
        << L" can't be stored in that encoding:\n\n";

    for (size_t i = 0; i < reported.size(); ++i)
    {
        const intptr_t pos = reported[i].pos;
        const intptr_t line = ::SendMessage(curScintilla, SCI_LINEFROMPOSITION, pos, 0);
        const intptr_t lineStart = ::SendMessage(curScintilla, SCI_POSITIONFROMLINE, line, 0);
        const intptr_t column = ::SendMessage(curScintilla, SCI_COUNTCHARACTERS, lineStart, pos);

        WCHAR wc[3] = { 0, 0, 0 };
        toUtf16(reported[i].codePoint, wc);

        WCHAR hex[16];
        swprintf(hex, 16, L"U+%04X", reported[i].codePoint);

        msg << L"    Line " << (line + 1) << L", column " << (column + 1) << L": " << wc << L" (" << hex << L")\n";
    }
    if (count > reported.size())
        msg << L"    ... and " << (count - reported.size()) << L" more\n";

    msg << L"\nConvert the file anyway? These characters will be lost.\n\n"
        << L"Yes: convert to ANSI\n"
        << L"No: keep the current encoding (" << uniModeName(uniMode) << L")";

    return ::MessageBox(nppData._nppHandle, msg.str().c_str(), TEXT("EditorConfig"),
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

//
// Convert the current buffer to the charset of the EditorConfig
//
static void convertToCharset(HWND curScintilla, EcCharset charset)
{
    const int command = convertCommand(charset);
    if (command == 0)
        return;

    if (charset == EC_CHARSET_LATIN1)
    {
        const uptr_t bufferId = ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);
        const int uniMode = static_cast<int>(::SendMessage(nppData._nppHandle, NPPM_GETBUFFERENCODING, bufferId, 0));

        // Notepad++ only converts to ANSI when the buffer is in one of the
        // Unicode modes. Don't bother the user when nothing will happen.
        if (uniMode != NPP_UNI_8BIT && !hasExplicitCharacterSet() && !confirmConvertToAnsi(curScintilla, uniMode))
            return;
    }

    ::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, command);
}

//
// Compare the encoding Notepad++ uses for the current buffer with the
// charset of the EditorConfig and tell the user when they differ.
// This is done once per buffer, unless force is set.
//
static void checkEncoding(HWND curScintilla, EcCharset charset, bool force)
{
    if (charset == EC_CHARSET_NONE || !nppReady || inBeforeSave || inEncodingCheck)
        return;

    const uptr_t bufferId = ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);

    WCHAR path[MAX_PATH];
    path[0] = 0;
    ::SendMessage(nppData._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH, reinterpret_cast<LPARAM>(path));

    if (!force)
    {
        const std::map<uptr_t, std::wstring>::const_iterator it = checkedBuffers.find(bufferId);
        if (it != checkedBuffers.end() && it->second == path)
            return;
    }
    checkedBuffers[bufferId] = path;

    const int uniMode = static_cast<int>(::SendMessage(nppData._nppHandle, NPPM_GETBUFFERENCODING, bufferId, 0));
    if (uniMode < 0)
        return;

    const bool explicitCharacterSet = hasExplicitCharacterSet();
    if (explicitCharacterSet)
    {
        // Notepad++ keeps the selected character set when converting to
        // ANSI, and it can't be compared with "latin1" from here.
        if (charset == EC_CHARSET_LATIN1)
            return;
    }
    else if (matchesCharset(uniMode, charset))
        return;

    // In these modes Scintilla holds the bytes of the file as they are
    const bool rawBytes = !explicitCharacterSet &&
            (uniMode == NPP_UNI_8BIT || uniMode == NPP_UNI_COOKIE || uniMode == NPP_UNI_7BIT);

    bool isAscii, isValidUtf8;
    analyseDocument(curScintilla, isAscii, isValidUtf8);

    // A pure ASCII file is the same in ANSI and in UTF-8 without BOM
    if (rawBytes && isAscii && (charset == EC_CHARSET_LATIN1 || charset == EC_CHARSET_UTF8))
        return;

    // Do the bytes of the file tell that Notepad++ picked the wrong encoding?
    int reinterpretCommand = 0;
    const wchar_t* reason = L"";
    const wchar_t* reinterpretAs = L"";
    if (rawBytes && !isAscii)
    {
        if (uniMode == NPP_UNI_COOKIE && charset == EC_CHARSET_LATIN1 && !isValidUtf8)
        {
            reinterpretCommand = IDM_FORMAT_ANSI;
            reason = L"its content is not valid UTF-8";
            reinterpretAs = L"ANSI";
        }
        else if (uniMode == NPP_UNI_8BIT && (charset == EC_CHARSET_UTF8 || charset == EC_CHARSET_UTF8_BOM) && isValidUtf8)
        {
            reinterpretCommand = IDM_FORMAT_AS_UTF_8;
            reason = L"its content is valid UTF-8";
            reinterpretAs = L"UTF-8";
        }
    }

    std::wstringstream msg;
    msg << getCurrentFileName() << L"\n\n"
        << L"Notepad++ opened this file ";
    if (explicitCharacterSet)
        msg << L"with a specific character set";
    else
        msg << L"as " << uniModeName(uniMode);
    msg << L", but the EditorConfig charset is " << charsetName(charset) << L".\n\n";

    inEncodingCheck = true;

    if (reinterpretCommand != 0)
    {
        msg << L"Notepad++ probably detected the encoding incorrectly, because " << reason << L".\n\n";

        if (::SendMessage(curScintilla, SCI_GETMODIFY, 0, 0) != 0)
        {
            // Reinterpreting would garble the changes made in the wrong encoding
            msg << L"The file has unsaved changes, so it is left as it is. "
                << L"Please check the encoding (menu \"Encoding\") before you save it.";
            ::MessageBox(nppData._nppHandle, msg.str().c_str(), TEXT("EditorConfig"), MB_OK | MB_ICONWARNING);
        }
        else
        {
            msg << L"Reinterpret the file as " << reinterpretAs << L"? The file on disk is not changed by this.";
            if (::MessageBox(nppData._nppHandle, msg.str().c_str(), TEXT("EditorConfig"), MB_YESNO | MB_ICONWARNING) == IDYES)
                ::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, reinterpretCommand);
        }
    }
    else
    {
        msg << L"Convert it now? The file on disk changes when you save it.\n\n"
            << L"If you choose No, the file is converted the next time you save it.";
        if (::MessageBox(nppData._nppHandle, msg.str().c_str(), TEXT("EditorConfig"), MB_YESNO | MB_ICONWARNING) == IDYES)
            convertToCharset(curScintilla, charset);
    }

    inEncodingCheck = false;
}

//
// Apply the EditorConfig settings to the current buffer
//
static void applyConfig(bool forceEncodingCheck)
{
    int name_value_count;
    editorconfig_handle eh;

    // Parse the EditorConfig file
    eh = editorconfig_handle_init();
    if (!eh || !parseConfig(eh))
        return;

    // Get the current scintilla
    HWND curScintilla = getCurrentScintilla();
    if (curScintilla == NULL)
        return;

    struct
    {
        const char*     indent_style;
#define INDENT_SIZE_TAB (-1000) // indent_size = -1000 means indent_size = tab
        int             indent_size;
        int             tab_width;
        const char*     end_of_line;
        EcCharset       charset;
    } ecConf; // obtained EditorConfig settings will be here

    memset(&ecConf, 0, sizeof(ecConf));

    // apply the settings
    name_value_count = editorconfig_handle_get_name_value_count(eh);

    for (int i = 0; i < name_value_count; ++i) {
        const char* name;
        const char* value;

        editorconfig_handle_get_name_value(eh, i, &name, &value);

        if (!strcmp(name, "indent_style"))
            ecConf.indent_style = value;
        else if (!strcmp(name, "tab_width"))
            ecConf.tab_width = atoi(value);
        else if (!strcmp(name, "indent_size")) {
            int     value_i = atoi(value);

            if (!strcmp(value, "tab"))
                ecConf.indent_size = INDENT_SIZE_TAB;
            else if (value_i > 0)
                ecConf.indent_size = value_i;
        }
        else if (!strcmp(name, "end_of_line"))
            ecConf.end_of_line = value;
        else if (!strcmp(name, "charset"))
            ecConf.charset = parseCharset(value);
    }

    if (ecConf.indent_style) {
        if (!strcmp(ecConf.indent_style, "tab"))
            ::SendMessage(curScintilla, SCI_SETUSETABS, (WPARAM)true, 0);
        else if (!strcmp(ecConf.indent_style, "space"))
            ::SendMessage(curScintilla, SCI_SETUSETABS, (WPARAM)false, 0);
    }
    if (ecConf.indent_size > 0) {
        ::SendMessage(curScintilla, SCI_SETINDENT,
                (WPARAM)ecConf.indent_size, 0);

        // We set the tab width here, so that this could be overrided then
        // if ecConf.tab_wdith > 0
        ::SendMessage(curScintilla, SCI_SETTABWIDTH,
                (WPARAM)ecConf.indent_size, 0);
    }

    if (ecConf.tab_width > 0)
        ::SendMessage(curScintilla, SCI_SETTABWIDTH,
                (WPARAM)ecConf.tab_width, 0);

    if (ecConf.indent_size == INDENT_SIZE_TAB)
        // set indent_size to tab_width here
        ::SendMessage(curScintilla, SCI_SETINDENT,
                (WPARAM)::SendMessage(curScintilla, SCI_GETTABWIDTH, 0, 0), 0);

    // set eol
    if (ecConf.end_of_line) {
        if (!strcmp(ecConf.end_of_line, "lf"))
            ::SendMessage(curScintilla, SCI_SETEOLMODE,
                    (WPARAM)SC_EOL_LF, 0);
        else if (!strcmp(ecConf.end_of_line, "cr"))
            ::SendMessage(curScintilla, SCI_SETEOLMODE,
                    (WPARAM)SC_EOL_CR, 0);
        else if (!strcmp(ecConf.end_of_line, "crlf"))
            ::SendMessage(curScintilla, SCI_SETEOLMODE,
                    (WPARAM)SC_EOL_CRLF, 0);
    }

    editorconfig_handle_destroy(eh);

    // Does the encoding of the buffer match the charset?
    checkEncoding(curScintilla, ecConf.charset, forceEncodingCheck);
}

//
// Menu command "Reload EditorConfig for this file"
//
void loadConfig()
{
    applyConfig(true);
}

//
// The notifications of Notepad++
//
void onBufferActivated()
{
    setSyntaxFromFilename();
    applyConfig(false);
}

void onReady()
{
    // No message boxes while Notepad++ is starting, so check the buffer
    // that is active now
    nppReady = true;
    applyConfig(false);
}

void onFileBeforeClose(uptr_t bufferId)
{
    checkedBuffers.erase(bufferId);
}

//
// Is the char c a newline character?
//
static bool isNewline(char c)
{
    return c == '\n' || c == '\r';
}

//
// insert the final newline or remove any remaining newlines
//
void insertFinalNewline(bool insert)
{
    HWND curScintilla = getCurrentScintilla();
    if (curScintilla == NULL)
        return;

    // Get the last line number
    intptr_t lastLine = ::SendMessage(curScintilla, SCI_GETLINECOUNT, 0, 0) - 1;

    // Get the length of that last line
    intptr_t length = ::SendMessage(curScintilla, SCI_LINELENGTH, lastLine, 0);

    // Do we need to insert a final newline?
    if (insert)
    {
        // If there is an empty last line, we're done
        if (length == 0)
            return;

        // What type of EOL must be inserted?
        char eolBuf[10];
        eolBuf[0] = 0;
        int eolMode = static_cast<int>(::SendMessage(curScintilla, SCI_GETEOLMODE, 0, 0));
        switch (eolMode)
        {
            case SC_EOL_CRLF:
                strcpy(eolBuf, "\r\n");
                break;
            case SC_EOL_CR:
                strcpy(eolBuf, "\r");
                break;
            case SC_EOL_LF:
                strcpy(eolBuf, "\n");
                break;
        }

        // Add the EOL at the end of the document
        length  = strlen(eolBuf);
        if (length != 0)
            SendMessage(curScintilla, SCI_APPENDTEXT, length, (LPARAM) eolBuf);

        return;
    }

    // Is there an non-empty last line, we're done
    if (length != 0)
        return;

    // Find the last non-newline character
    length = SendMessage(curScintilla, SCI_GETTEXTLENGTH, 0, 0);
    intptr_t pos = length;
    while (pos > 0 && isNewline((char) SendMessage(curScintilla, SCI_GETCHARAT, pos - 1, 0)))
        pos--;

    // Remove the final newline(s)
    SendMessage(curScintilla, SCI_DELETERANGE, pos, length - pos);
}

//
// Fix the indentation of the document
//
void fixIndentation()
{
    const HWND curScintilla = getCurrentScintilla();
    if (curScintilla == NULL)
        return;

    // Make sure the undo works as expected
    SendMessage(curScintilla, SCI_BEGINUNDOACTION, 0, 0);

    // Get the number of lines in the document
    const int linecount = static_cast<int>(SendMessage(curScintilla, SCI_GETLINECOUNT, 0, 0)) - 1;

    // Go through the lines
    for (int line = 1; line <= linecount; line++)
    {
        // Get the indentation level of the current line
        const int level = static_cast<int>(SendMessage(curScintilla, SCI_GETLINEINDENTATION, line, 0));

        // Increase and decrease the indention level to reset the indentation style
        SendMessage(curScintilla, SCI_SETLINEINDENTATION, line, level + 1);
        SendMessage(curScintilla, SCI_SETLINEINDENTATION, line, level);
    }

    SendMessage(curScintilla, SCI_ENDUNDOACTION, 0, 0);
}

static void selectBufferId(HWND hWnd, uptr_t bufferId)
{
    const int32_t docPosInfo = static_cast<int32_t>(::SendMessage(hWnd, NPPM_GETPOSFROMBUFFERID, bufferId, 0));
    SendMessage(hWnd, NPPM_ACTIVATEDOC, docPosInfo >> 30, docPosInfo & 0x3FFFFFFF);
}

void onBeforeSave(HWND hWnd, uptr_t idFrom)
{
    // Activating a buffer must not trigger the encoding check
    inBeforeSave = true;

    const uptr_t keepBufferID = ::SendMessage(hWnd, NPPM_GETCURRENTBUFFERID, 0, 0);
    selectBufferId(hWnd, idFrom);

    editorconfig_handle eh = editorconfig_handle_init();

    if (!eh || !parseConfig(eh))
    {
        selectBufferId(hWnd, keepBufferID);
        inBeforeSave = false;
        return;
    }

    enum {
        NPPEC_BOOLVAL_UNSPECIFIED = -1,
        NPPEC_BOOLVAL_FALSE = 0,
        NPPEC_BOOLVAL_TRUE = 1
    };

    int trim_trailing_whitespace = NPPEC_BOOLVAL_UNSPECIFIED;
    int insert_final_newline = NPPEC_BOOLVAL_UNSPECIFIED;
    int end_of_line = 0;
    EcCharset charset = EC_CHARSET_NONE;

    int name_value_count = editorconfig_handle_get_name_value_count(eh);
    for (int i = 0; i < name_value_count; ++i) {
        const char* name;
        const char* value;

        editorconfig_handle_get_name_value(eh, i, &name, &value);

        if (trim_trailing_whitespace == NPPEC_BOOLVAL_UNSPECIFIED
                && strcmp(name, "trim_trailing_whitespace") == 0) {
            if (strcmp(value, "true") == 0) {
                trim_trailing_whitespace = NPPEC_BOOLVAL_TRUE;
            }
            continue;
        }

        if (insert_final_newline == NPPEC_BOOLVAL_UNSPECIFIED
                && strcmp(name, "insert_final_newline") == 0) {
            if (strcmp(value, "true") == 0) {
                insert_final_newline = NPPEC_BOOLVAL_TRUE;
            } else if (strcmp(value, "false") == 0) {
                insert_final_newline = NPPEC_BOOLVAL_FALSE;
            }
            continue;
        }

        // Need to convert the end of lines?
        if (strcmp(name, "end_of_line") == 0) {
            if (strcmp(value, "lf") == 0)
                end_of_line = IDM_FORMAT_TOUNIX;
            else if (strcmp(value, "crlf") == 0)
                end_of_line = IDM_FORMAT_TODOS;
            else if (strcmp(value, "cr") == 0)
                end_of_line = IDM_FORMAT_TOMAC;
            continue;
        }

        // Need to convert the charset of the document?
        if (strcmp(name, "charset") == 0)
        {
            charset = parseCharset(value);
            continue;
        }
    }

    // Save the folding behavior and set it to 0 to keep folds from opening
    // when modifying the file.
    HWND curScintilla = getCurrentScintilla();
    int automatic_fold = static_cast<int>(::SendMessage(curScintilla, SCI_GETAUTOMATICFOLD, 0, 0));
    SendMessage(curScintilla, SCI_SETAUTOMATICFOLD, 0, 0);

    // Make sure there is no active selection
    const int curPos = static_cast<int>(SendMessage(curScintilla, SCI_GETCURRENTPOS, 0, 0));
    SendMessage(curScintilla, SCI_SETEMPTYSELECTION, curPos, 0);

    // Trailing whitespace needs to be trimmed before 'insert_final_newline' is
    // applied.
    if (trim_trailing_whitespace == NPPEC_BOOLVAL_TRUE) {
        SendMessage(hWnd, NPPM_MENUCOMMAND, 0, IDM_EDIT_TRIMTRAILING);
    }

    if (insert_final_newline != NPPEC_BOOLVAL_UNSPECIFIED) {
        insertFinalNewline(insert_final_newline == NPPEC_BOOLVAL_TRUE);
    }

    if (end_of_line != 0) {
        SendMessage(hWnd, NPPM_MENUCOMMAND, 0, end_of_line);
    }

    // Notepad++ does nothing when the buffer already has that encoding. The
    // user is asked first when characters would get lost.
    if (charset != EC_CHARSET_NONE) {
        convertToCharset(curScintilla, charset);
    }

    // Restore the folding behavior
    SendMessage(curScintilla, SCI_SETAUTOMATICFOLD, automatic_fold, 0);

    editorconfig_handle_destroy(eh);

    selectBufferId(hWnd, keepBufferID);
    inBeforeSave = false;
}

void showEditorConfigSettings()
{
    int name_value_count;
    editorconfig_handle eh;

    // Parse the EditorConfig file
    eh = editorconfig_handle_init();
    if (!eh || !parseConfig(eh))
        return;

    std::tstringstream settings;
    settings << "Current EditorConfig settings:" << std::endl << std::endl;

    // get the settings
    name_value_count = editorconfig_handle_get_name_value_count(eh);

    for (int i = 0; i < name_value_count; ++i) {
        const char* name;
        const char* value;

        editorconfig_handle_get_name_value(eh, i, &name, &value);
        settings << name << " = " << value << std::endl;
    }

    editorconfig_handle_destroy(eh);

    // The charset is only applied when the file is saved, so show what
    // Notepad++ uses right now
    const uptr_t bufferId = ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);
    const int uniMode = static_cast<int>(::SendMessage(nppData._nppHandle, NPPM_GETBUFFERENCODING, bufferId, 0));
    settings << std::endl << "Current encoding in Notepad++: ";
    if (hasExplicitCharacterSet())
        settings << "specific character set";
    else
        settings << uniModeName(uniMode);
    settings << std::endl;

    MessageBox(NULL, settings.str().c_str(), TEXT("EditorConfig"), MB_OK);
}

//
// Initialization of your plugin commands
// You should fill your plugins commands here
//
void commandMenuInit()
{
    //--------------------------------------------//
    //-- STEP 3. CUSTOMIZE YOUR PLUGIN COMMANDS --//
    //--------------------------------------------//
    // with function :
    // setCommand(int index,                      // zero based number to indicate the order of command
    //            TCHAR *commandName,             // the command name that you want to see in plugin menu
    //            PFUNCPLUGINCMD functionPointer, // the symbol of function (function pointer) associated with this command. The body should be defined below. See Step 4.
    //            ShortcutKey *shortcut,          // optional. Define a shortcut to trigger this command
    //            bool check0nInit                // optional. Make this menu item be checked visually
    //            );
    int id = 0;
    setCommand(id++, TEXT("Reload EditorConfig for this file"), loadConfig, NULL, false);
    setCommand(id++, TEXT("Show EditorConfig settings for this file"), showEditorConfigSettings, NULL, false);
    setCommand(id++, TEXT(""), NULL, NULL, false);  // Separator
    setCommand(id++, TEXT("Fix indentation"), fixIndentation, NULL, false);
    setCommand(id++, TEXT(""), NULL, NULL, false);  // Separator
    setCommand(id++, TEXT("About..."), showAboutDlg, NULL, false);
}

//
// Here you can do the clean up (especially for the shortcut)
//
void commandMenuCleanUp()
{
    // Don't forget to deallocate your shortcut here
}

//
// This function help you to initialize your plugin commands
//
bool setCommand(size_t index, TCHAR *cmdName, PFUNCPLUGINCMD pFunc, ShortcutKey *sk, bool check0nInit)
{
    if (index >= nbFunc)
        return false;

    lstrcpy(funcItem[index]._itemName, cmdName);
    funcItem[index]._pFunc = pFunc;
    funcItem[index]._init2Check = check0nInit;
    funcItem[index]._pShKey = sk;

    return true;
}

//
// Center the window, relative the NPP-window
//
void centerWindow(HWND hDlg)
{
    RECT rc;
    GetClientRect(nppData._nppHandle, &rc);

    POINT center;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    center.x = rc.left + (w / 2);
    center.y = rc.top + (h / 2);
    ClientToScreen(nppData._nppHandle, &center);

    RECT dlgRect;
    GetClientRect(hDlg, &dlgRect);
    int x = center.x - ((dlgRect.right - dlgRect.left) / 2);
    int y = center.y - ((dlgRect.bottom - dlgRect.top) / 2);

    SetWindowPos(hDlg, HWND_TOP, x, y, -1, -1, SWP_NOSIZE | SWP_SHOWWINDOW);
}
