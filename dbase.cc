// -----------------------------------------------------------------------------
// Copyright (c) 2019 kallup.jens@web.de
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
// THE USE OR OTHER DEALINGS IN THE SOFTWARE. 
// -----------------------------------------------------------------------------
# include <stdio.h>
# include <string.h>
# include <stdlib.h>
# include <stdint.h>
# include <sys/io.h>         // replacement for "sys/types.h"

# include <iostream>
# include <cstring>
# include <cstdlib>
# include <cstdint>
# include <string>
# include <fstream>          // file input/output
# include <sstream>          // for code-holder
# include <vector>           // app parameter's
# include <map>              // new-object parameters's
# include <exception>        // for exception handĺer's
# include <cctype>
using namespace std;

#ifdef DEBUG
bool dBaseDebugFlag = true;     // enabled  debug?
# else                          // else
bool dBaseDebugFlag = false;    // disabled debug?
#endif

#define ECTX_UNKNOWN    0      // context: unknow (throw)
#define ECTX_COMMENT    1      //  -"-  unterminated comment
#define ECTX_DEFAULT    2      //  -"-  else

int line_row;       // current row
int line_col;       // current column

std::ifstream yyin;    // input source file
std::ofstream yyout;   // output target file

char tok_ch;         // current token char

int ident_pos = 1;  // for string ident

std::string       str_para;
std::stringstream str_exec;

std::string scriptName;     // name of the script
std::string str_token;      // current parsed token

std::string st1_token;                // first  token string
std::string st2_token;                // second token string

const int MAX_OBJECTS   = 2048;       // max objects in class
const int MAX_TOKEN_LEN = 255;        // maximum ident-token length
const int RULE_END      = 10000;      // max rules

bool comment_start_on = false;   // in C comment block ?
int  comment_start_row;          // possible comment start
int  comment_start_col;          //  -"-

bool yyerror = false;            // checker

// ------------------------------
// token spec.stuffc ...
// ------------------------------
enum class E_TOKEN : unsigned int {
    E_IDENT      = 0x001,     // identifier
    E_NUMBER     = 0x002,     // number
    E_ASSIGN     = 0x004,     // =
    E_FALSE      = 0x006,     // bool false
    E_TRUE       = 0x008,     // bool true
    E_COMMENT    = 0x010,
    E_NUMBER_DEC = 0x002,
    E_NUMBER_HEX = 0x002,
    E_NUMBER_FLT = 0x002,
    E_EOF        = 0x020,     // EOF - end of file
    E_PARAMETER  = 0x100,
    E_LOCAL      = 0x101,
    E_CBREAK     = 0x102,
    E_COMMA      = 0x103,
    E_UNKNOWN    = 0x000
};
E_TOKEN token_state = E_TOKEN::E_UNKNOWN;    // state/ctx of parsed token

E_TOKEN token       = E_TOKEN::E_UNKNOWN;    // current token
E_TOKEN token_prev  = E_TOKEN::E_UNKNOWN;    // prev.   token
E_TOKEN token_next  = E_TOKEN::E_UNKNOWN;    // next    token

std::string token_number;     // parsed number

// function's forward declarations --------------------------
int yygetc    ();
E_TOKEN getNumber ();
E_TOKEN getToken  ();

// ---------------------------------------------
// exception class for common dBase error's ...
// ---------------------------------------------
std::string putErrorMsg(const std::string &str)
{
    std::stringstream ss;
    std::string res;
    ss << "error at "
       << dec << line_row << ":"
       << dec << line_col << ": "
       << str;
    res.append(ss.str());
    return res;
}
// -----------------------------------------
class dBaseError: public exception {
    std::stringstream msg;
public:
    explicit dBaseError(const std::string & arg = "syntax error") {
        msg << putErrorMsg(arg);
        message.append(msg.str());
    }
    std::string message;
};
// ----------------------------------------------------------------------------
class dBaseSyntaxEOF:    public   dBaseError { public:
      explicit dBaseSyntaxEOF()   { dBaseError("End Of File.");  } };
// ----------------------------------------------------------------------------
class dBaseSyntaxError:  public   dBaseError { public:
      explicit dBaseSyntaxError() { dBaseError("syntax error."); } };
// ----------------------------------------------------------------------------
class dBaseUnknownError: public   dBaseError { public:
    explicit dBaseUnknownError()  { dBaseError("unknown error."); } };
// ----------------------------------------------------------------------------
class dBaseCommentError: public   dBaseError { public:
    explicit dBaseCommentError()  { dBaseError("unterminated comment."); } };
// ----------------------------------------------------------------------------
class dBaseDefaultError: public   dBaseError { public:
    explicit dBaseDefaultError()  { dBaseError("default error."); } };

// --------------------------------
// A class for debuging output ...
// --------------------------------
#ifdef DEBUG
class dBaseDebug {
public:
    dBaseDebug() { }
    dBaseDebug & operator << (const std::string & str) {
        std::stringstream ss;
        if (dBaseDebugFlag == true) {
            ss << "debug: " << str << std::endl;
            std::cerr << ss.str();
        }
    }
    dBaseDebug & operator () () {
        return *this;
    }
    std::string operator << (const char * str) {
        std::stringstream ss;
        if (dBaseDebugFlag == true) {
            ss << "debug: " << str << std::endl;
            std::cerr << ss.str();
        }   return ss.str();
    }
};
static class dBaseDebug dbDebug;
#endif

// ----------------------------------------------------------------------------
struct dBaseAppObject {
    std::vector<std::string> prg_header;
    //
    std::vector<std::string> app_parameter;
    std::vector<std::string> app_header;
    std::vector<std::string> app_ctor;
    std::vector<std::string> app_dtor;
    std::vector<std::string> app_footer;
    //
    std::vector<std::string> prg_footer;
};
// ----------------------------------------------------------------------------
dBaseAppObject app_object;

// ----------------------------------------------------------------------------
// This function "skips" all comments in the form of:
// 
//    C++     like one liner: //
//    C       like block:     /* */
//    dBase 1 like one liner: **
//    dBase 2 like one liner: &&
//
// Input : nothing
// Output: n.a.
//
// Notes :
//    You can single block's of C /* */ comments. But this is not limited to
//    nested block's in the form of /* block1 /* block2 ... */ */.
//    **, and && are dbase one liners,
// ----------------------------------------------------------------------------
inline void getEmpties(char ch)
{
    bool flag = true;

    if (ch == ' ' ) { line_col += 1; } else
    if (ch == '\t') { line_col += 8; } else
    if (ch == '\n') {
        line_row += 1;
        line_col  = 1;
    }
}

void skip_comment()
{
    static int nestedComment = 1;
    for (;;) {
        tok_ch = yygetc();
        getEmpties(tok_ch);
        if (tok_ch == '/') {
            tok_ch = yygetc();
            getEmpties(tok_ch);
            if (tok_ch == '*') {
                ++nestedComment;
                continue;
            }
        }
        else if (tok_ch == '*') {
            tok_ch = yygetc();
            getEmpties(tok_ch);
            if (tok_ch == '/') {
                if (--nestedComment <= 0) {
                    break;
                }
            }
        }
        else if (yyin.eof())
        throw dBaseCommentError();
    }
}

// ----------------------------------------------------------------------------
// This function "get" exactly "one" char from input stream buffer.
//
// Input : nothing
// Output: char, get from input stream.
//
// Notes :
//    If EOF - end of file found, the exception dBaseSyntaxEOF is throw.
//    It is a non-error exception, but to mark, the input stream ends.
// ----------------------------------------------------------------------------
int yygetc() {
    yyin.get(tok_ch);           // get char from yyin stream
    //
    if (yyin.eof())             // check, eof == true ?
    throw dBaseSyntaxEOF();     // throw non-error
    //
    return tok_ch;              // return scaned above char
}

// ----------------------------------------------------------------------------
// This function "get" chars in one line as one line comment ...
//
// Input : nothing
// Output: nothing
//
// Notes:
//   yyin.eof() check for end of file. If it found, dBase throws
//   a non-error SyntaxEOF exception.
//   Else, if newline char \n was found, do increment source line row,
//   and reset line column to 1.
// ----------------------------------------------------------------------------
void get_oneline_comment()
{
    while (true) {
        tok_ch = yygetc();
        if (tok_ch == '\n') {
            line_col  = 1;
            line_row += 1;
            break;
        }
    }
}

// ----------------------------------------------------------------------------
// This function scans the source stream of ocurence of a possible token.
//
// Input : nothing
// Output: E_TOKEN   ; the token "enum" number
//
// Notes : n.a.
// ----------------------------------------------------------------------------
E_TOKEN getToken()
{
    token = E_TOKEN::E_IDENT;
    int len = 0;

    while (1) {
        tok_ch = yygetc();
        line_col += 1;
        if (isalnum(tok_ch)
        ||  isalpha(tok_ch)
        ||  tok_ch == '_'
        ||  tok_ch == '.'  ) {
            if (++len >= MAX_TOKEN_LEN)
            break;
            line_col += 1;
            str_token.append(1,tok_ch);
        }   else {
            yyin.putback(tok_ch);
            break;
        }
    }

    // ----------------------------------------------
    // check, and return "token" when form match ...
    // ----------------------------------------------
    if (str_token == std::string("parameter")) token = E_TOKEN::E_PARAMETER; else
    if (str_token == std::string(","        )) token = E_TOKEN::E_COMMA;     else
                                               token = E_TOKEN::E_IDENT;

    return token;
}

// ----------------------------------------------------------------------------
// This function "skips" all white space's and comment lines or comment blocks.
//
// Input : nothing
// Output: int char   ; parsed char
//
// Notes:  n.a.
// ----------------------------------------------------------------------------
int skip_white_spaces()
{
    tok_ch = yygetc();

    if (tok_ch == '*') {
        tok_ch = yygetc();
        if (tok_ch == '*') {
            get_oneline_comment();
            tok_ch = skip_white_spaces();
        }   else {
            yyerror = true;
            throw dBaseSyntaxError();
        }
    }   else
    if (tok_ch == '&') {
        tok_ch = yygetc();
        if (tok_ch == '&') {
            get_oneline_comment();
            tok_ch = skip_white_spaces();
        }   else {
            yyerror = true;
            throw dBaseSyntaxError();
        }
    }   else
    if (tok_ch == '/') {
        tok_ch = yygetc();
        if (tok_ch == '/') {
            get_oneline_comment();
            tok_ch = skip_white_spaces();
        }   else
        if (tok_ch == '*') {
            skip_comment();
            tok_ch = skip_white_spaces();
        }   else {
            yyerror = true;
            throw dBaseSyntaxError();
        }
    }   else

    if (tok_ch == ' ' ) { line_col += 1; } else
    if (tok_ch == '\t') { line_col += 8; } else
    if (tok_ch == '\n') {
        line_col  = 1;
        line_row += 1;
        tok_ch = skip_white_spaces();
        return tok_ch;
    }   else

    if (isalnum(tok_ch)
    ||  tok_ch == '_') {
        str_token.clear();
        yyin.putback(tok_ch);
        token = getToken();
    }   else

    if (tok_ch == ',') { token = E_TOKEN::E_COMMA;  } else
    if (tok_ch == '=') { token = E_TOKEN::E_ASSIGN; } else

    throw dBaseSyntaxError();

    return tok_ch;
}

/*
E_TOKEN handle_false_true() {
    tok_ch = yyin.get();
    line_col += 1;
    if (tok_ch == 'f' || tok_ch == 'F') {       // false
        tok_ch = yyin.get();
        line_col += 1;
        if (tok_ch == '.') {
            token = E_TOKEN::E_FALSE;
            return token;
        }
        else throw dBaseSyntaxError();
    }   else
    if (tok_ch == 't' || tok_ch == 'T') {       // true
        tok_ch = yyin.get();
        line_col += 1;
        if (tok_ch == '.') {
            token = E_TOKEN::E_TRUE;
            return token;
        }
        else throw dBaseSyntaxError();
    }   else throw dBaseSyntaxError();
    return token;
}
*/

// -------------------------
// parse a token number ...
// -------------------------
E_TOKEN get_hex()
{
    for (;;) {
        str_token.append(1,tok_ch);
        tok_ch = yyin.get();
        if (isxdigit(tok_ch))
        continue;
        else break;
    }
    yyin.putback(tok_ch);
    return E_TOKEN::E_NUMBER;
}
E_TOKEN get_num()
{
    for (;;) {
        str_token.append(1,tok_ch);
        tok_ch = yyin.get();
        if (isdigit(tok_ch))
        continue;
        else break;
    }
    yyin.putback(tok_ch);
    return E_TOKEN::E_NUMBER;
}
E_TOKEN getNumber()
{
    token = E_TOKEN::E_NUMBER;
    str_token.append(1,tok_ch);

    if (tok_ch == '0') {
        tok_ch = yyin.get();
        if (tok_ch == '.')                  { return get_num(); } else
        if (tok_ch == 'x' || tok_ch == 'X') { return get_hex(); } else
        if (isdigit(tok_ch))                { return get_num(); } else
        throw dBaseSyntaxError();
    }
    else if (tok_ch >= '1' && tok_ch <= '9') {
        str_token.append(1,tok_ch);
        tok_ch = yyin.get();
        if (tok_ch == '.')   { return get_num(); } else
        if (isdigit(tok_ch)) { return get_num(); } else
        throw dBaseSyntaxError();
    }
    return token;
}

bool yyexpect(char ch)
{
    if (skip_white_spaces() == ch)
    return true; else
    return false;
}

void handle_parameter()
{
    for (;;) {
        str_token.clear();
        tok_ch = skip_white_spaces();
        if (str_token.size() > 0)   {
            app_object.app_parameter.push_back(str_token);
        }
        if (tok_ch == ',') {
            str_token.clear();
            tok_ch = skip_white_spaces();

            if (token == E_TOKEN::E_IDENT) {
                tok_ch = skip_white_spaces();
                if (tok_ch == ',')                    
                continue;
            }
        }
    }
}

void yyparse()
{
    tok_ch = skip_white_spaces();
    if (token == E_TOKEN::E_PARAMETER) {
        tok_ch = skip_white_spaces();
        handle_parameter();
    }
}

// ----------------------------------------------------------------------------
// This template function remove/cancel/write stuff for the end-process
// of transpille ...
//
// Input : T  = dBaseClass's
//         bool errFlag         ; default is true = error
// Output: return value for end-up return to console/gui
//
// Note  : n.a.
// ----------------------------------------------------------------------------
int complex_finish(bool errFlag);

template <typename T>
int complex_finish(const T &t, bool errFlag = true)
{
    std::string str_error;
    str_error.append(t.message);

    // ---------------------------------
    // check, and close file stream ...
    // ---------------------------------
    yyin .close();
    yyout.close();

    std::cout << std::endl;

    // catch was in action ...
    if (errFlag == false && yyerror == false)
    return complex_finish(false);

    if (errFlag == true && yyerror == true) {
        std::cout << str_error  << std::endl;
        std::cout << "FAIL !!!" << std::endl;
        return 1;
    }   return 0;
}

int complex_finish(bool errFlag)
{
    if (errFlag == false && yyerror == false) {
        for (auto item : app_object.app_parameter) {
            std::cout
            << "var " << item
            << ";"    << std::endl;
        }

        std::cout
        << "class _Z" << scriptName
        << " {"       << std::endl
        << "\tconstructor(";

        int pos = 0;
        for (auto item : app_object.app_parameter) {
            if (app_object.app_parameter.size()-1 >= ++pos)
            std::cout << item << ", " ; else
            std::cout << item ;
        }

        std::cout << ") {" << std::endl;

        for (auto item : app_object.app_parameter) {
            std::cout
            << "\t\tthis." << item << ";"
            << std::endl;
        }
        std::cout << "\t}" << std::endl;
        std::cout << "}"   << std::endl;

        std::cout << "const _" << scriptName
        << " = new _Z"
        << scriptName
        << "(";

        pos = 0;
        for (auto item : app_object.app_parameter) {
            if (app_object.app_parameter.size()-1 >= ++pos)
            std::cout << item << ", " ; else
            std::cout << item ;
        }


        std::cout
        << ");" << std::endl;

        std::cout << "SUCCESS" << std::endl;
        return 0;
    }   return 1;
}

// ----------------------------------------------------------------------------
// main entry function, do init stuff ...
// ----------------------------------------------------------------------------
int main(int argc, char **argv)
{
    std::cout << "dbase2js (c) 2019 Jens Kallup" << std::endl;
    std::cout << "all rights reserved."          << std::endl;

    if (argc < 2) {
        std::cerr
        << "no source file given."
        << std::endl;
        return 1;
    }

    scriptName.clear();
    scriptName.append(argv[1]);

    try {
        // ---------------------------
        // set default/init stuff ...
        // ---------------------------
        line_row = 1;
        line_col = 1;

        comment_start_on = false;

        std::stringstream iss;  iss << argv[1];
        std::stringstream oss;  oss << argv[1] << ".pro";

        yyin .open(iss.str(), ios_base::in );
        yyout.open(oss.str(), ios_base::out);

        if (!yyin .good()) { throw dBaseError("read input file."  ); }
        if (!yyout.good()) { throw dBaseError("write output file."); }

        // -------------------------------------
        // check, if called from cli or wif ...
        // -------------------------------------
        if (scriptName.size() > 11) {
            scriptName = scriptName.erase(0,11);
        }   scriptName = scriptName.replace(scriptName.find("."), 1, "_");

        // ----------------------------
        // get file size:
        // ----------------------------
        yyin.seekg(0, ios_base::end); int source_length = yyin.tellg();
        yyin.seekg(0, ios_base::beg);

        // ----------------------------------------
        yyparse();
        // ----------------------------------------
        if (yyerror == true)
        throw  dBaseSyntaxError();
        return complex_finish(false);
    }

    catch (dBaseUnknownError &err) { return complex_finish(err); }
    catch (dBaseDefaultError &err) { return complex_finish(err); }
    catch (dBaseCommentError &err) { return complex_finish(err); }
    catch (dBaseSyntaxError  &err) { return complex_finish(err); }
    //
    catch (dBaseSyntaxEOF    &err) { return complex_finish(false); }

    catch (...) {
        dBaseSyntaxError      err ;
        return complex_finish(err);
    }   return 0 ;
}

