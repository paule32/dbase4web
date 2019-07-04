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
int         scriptSize;
int         scriptPosition = 0;

std::string str_token;      // current parsed token
std::string token_ctx;

std::string str_tmp;

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
    E_NEW        = 0x021,
    E_PARAMETER  = 0x100,
    E_LOCAL      = 0x101,
    E_CBREAK     = 0x102,
    E_COMMA      = 0x103,
    E_OBRACE     = 0x104,     // open  soft bracket: (
    E_CBRACE     = 0x105,     // close soft bracket: )
    E_IF         = 0x106,     // condition: 
    E_ELSE       = 0x107,     //
    E_ENDIF      = 0x108,     // IF ELSE ENDIF
    E_CLASS      = 0x109,
    E_OF         = 0x110,
    E_ENDCLASS   = 0x111,     // CLASS name OF parent <body> ENDCLASS
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

void        handle_if();
std::string handle_class();

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

    std::vector<std::string> app_local_class;
    std::vector<std::string> app_local_vector;

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
    line_col += 1;              // increase line column counter ..
    scriptPosition += 1;
    //
    if (yyin.eof()) {           // check, eof == true ?
        tok_ch = EOF;           // conversation
        token  = E_TOKEN::E_EOF;
        throw dBaseSyntaxEOF();
    }
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
        }   line_col += 1;
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
        if (isalnum(tok_ch)
        ||  isalpha(tok_ch)
        ||  tok_ch == '_'
        ||  tok_ch == '.'  ) {
            if (++len >= MAX_TOKEN_LEN)
            break;
            str_token.append(1,tok_ch);
        }   else {
            if (tok_ch == EOF) {
                token = E_TOKEN::E_EOF;
                return token;
            }   else {
                printf("===> %s\n",str_token.c_str());

                if (tok_ch == ')'
                ||  tok_ch == '('
                )   {
                    yyin.putback(tok_ch);
                    tok_ch = 200;
                }
                break;
            }
        }
    }

    // ----------------------------------------------
    // check, and return "token" when form match ...
    // ----------------------------------------------
    if (str_token == std::string("parameter")) token = E_TOKEN::E_PARAMETER; else
    if (str_token == std::string("local"    )) token = E_TOKEN::E_LOCAL;     else
    if (str_token == std::string("new"      )) token = E_TOKEN::E_NEW;       else
    if (str_token == std::string("if"       )) token = E_TOKEN::E_IF;        else
    if (str_token == std::string("else"     )) token = E_TOKEN::E_ELSE;      else
    if (str_token == std::string("endif"    )) token = E_TOKEN::E_ENDIF;     else
    if (str_token == std::string("class"    )) token = E_TOKEN::E_CLASS;     else
    if (str_token == std::string("of"       )) token = E_TOKEN::E_OF;        else
    if (str_token == std::string("endclass" )) token = E_TOKEN::E_ENDCLASS;  else
    if (str_token == std::string(","        )) token = E_TOKEN::E_COMMA;     else
    if (str_token == std::string("="        )) token = E_TOKEN::E_ASSIGN;    else
    if (str_token == std::string("("        )) token = E_TOKEN::E_OBRACE;    else
    if (str_token == std::string(")"        )) token = E_TOKEN::E_CBRACE;    else {
            token =  E_TOKEN::E_IDENT;
    }
    return  token ;
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
    for (;;) {
        tok_ch = yygetc();
        if (tok_ch == '*') {
            tok_ch = yygetc();
            if (tok_ch == '*') {
                get_oneline_comment();
            }   else {
                yyerror = true;
                throw dBaseSyntaxError();
            }
        }   else
        if (tok_ch == '&') {
            tok_ch = yygetc();
            if (tok_ch == '&') {
                get_oneline_comment();
            }   else {
                yyerror = true;
                throw dBaseSyntaxError();
            }
        }   else
        if (tok_ch == '/') {
            tok_ch = yygetc();
            if (tok_ch == '/') {
                get_oneline_comment();
            }   else
            if (tok_ch == '*') {
                skip_comment();
            }   else {
                yyerror = true;
                throw dBaseSyntaxError();
            }
        }   else

        if (tok_ch == ' ' ) {                } else
        if (tok_ch == '\t') { line_col += 8; } else
        if (tok_ch == '\n') {
            line_col  = 1;
            line_row += 1;
        }   else

        if (isalnum(tok_ch)
        ||  tok_ch == '_') {
            str_token.clear();
            yyin.putback(tok_ch);
            token = getToken();
            return 300;
        }

        if (tok_ch == ',') { token = E_TOKEN::E_COMMA ;  break; } else
        if (tok_ch == '=') { token = E_TOKEN::E_ASSIGN;  break; } else
        if (tok_ch == '(') { token = E_TOKEN::E_OBRACE;  break; } else
        if (tok_ch == ')') { token = E_TOKEN::E_CBRACE;  break; } else

        if (tok_ch == '.') {
            tok_ch = yygetc();
            if (tok_ch == 'f' || tok_ch == 'F') {       // false
                tok_ch = yygetc();
                if (tok_ch == '.') {
                    token = E_TOKEN::E_FALSE;
                    str_token = ".f.";
                    return 200;
                }
            }
            else
            if (tok_ch == 't' || tok_ch == 'T') {       // true
                tok_ch = yygetc();
                if (tok_ch == '.') {
                    token = E_TOKEN::E_TRUE;
                    str_token = ".t.";
                    return 200;
                }
            }
        }
    }

    return tok_ch;
}

bool yyexpect(char ch)
{
    if (skip_white_spaces() == ch)
    return true; else
    return false;
}

void push_parameter(std::string str) {
    app_object.app_parameter.push_back(str);
}
void push_local(std::string str, int mode)
{
    str_token.clear();

    std::string str__var;
    std::string str__str;

    str__var.clear(); str__var += str;
    str__str.clear(); str__str += str;

    for (;;)
    {
        tok_ch = skip_white_spaces();

        if (tok_ch == ',') { continue; } else
        if (tok_ch == '=') {
            tok_ch = skip_white_spaces();
            str__str += " = ";
            if (str_token == "new") {
                str__str  += "new " ;
                tok_ch = skip_white_spaces();
                str__str += str_token;

                tok_ch = skip_white_spaces();
                if (tok_ch == '(') {
                    str__str += "(";
                    bool the_end = false;
                    int cnt = 0;
                    for (;;++cnt) {
                        tok_ch = skip_white_spaces();
                        if (tok_ch == ',') {
                            if (cnt > 0)
                            str__str += ",";
                            str__str += str_token;
                            continue;
                        }
                        if (tok_ch == ')') {
                            str__str += ");";
                            app_object.app_local_vector.push_back(str__str);
                            the_end   = true;
                            break;
                        }
                        if (token == E_TOKEN::E_IDENT) {
                            str__str += str_token;
                            tok_ch = skip_white_spaces();
                        }
                    }
                    if (the_end)
                    break;
                }
            }
        }
    }
}

std::string
handle_commands()
{
    std::string str__str;

    str_token.clear();
    str__str .clear();

    for (;;) {
        tok_ch = skip_white_spaces();
        if (str_token == "class") {
printf("---->> %s\n",str_token.c_str());
            app_object.app_local_class.push_back("class ");
            handle_class();
        }
        else if (str_token == "if") {
            handle_if();
        }
        else if (str_token == "else") {
            str__str += "\n} else {\n";
            str__str += handle_commands();
            break;
        }
        else if (str_token == "endif") {
            str__str += "\n}";
            break;
        }
        if (token == E_TOKEN::E_IDENT) {
            str__str += str_token;
            tok_ch = skip_white_spaces();

            if (tok_ch == '=') {
                tok_ch = skip_white_spaces();

                if (str_token == ".f.") {
                    str_token = "false";
                }   else
                if (str_token == ".t.") {
                    str_token = "true" ;
                }

                str__str += " = ";
                str__str += str_token;
                str__str += ";\n";

                tok_ch = skip_white_spaces();
                if (str_token == "else") {
                    str__str += "\n} else {\n";
                }
                else if (str_token == "endif") {
                    str__str += "\n}";
                    break;
                }   else {
                    tok_ch = skip_white_spaces();
                    if (tok_ch == '(') {
                        tok_ch = skip_white_spaces();
                        str__str += str_token;
                        str__str += "(" ;   if (tok_ch == ')') {
                        str__str += ");";
                        continue;
                        }
                    }
                }
            }
        }
    }
    return str__str;
}

void handle_if()
{
    std::string str__str;

    str_token.clear();
    str__str .clear();

    str__str += "if ";

    tok_ch = skip_white_spaces();
    if (tok_ch == '(') {
        tok_ch = skip_white_spaces();
        str__str += "(";
        if (token == E_TOKEN::E_IDENT) {
            str__str += str_token;
            tok_ch = skip_white_spaces();
            if (tok_ch == ')') {
                str__str += ") {\n";
                str__str += handle_commands();
                    app_object.
                    app_local_vector.
                    push_back(str__str);
                tok_ch = skip_white_spaces();
                if (str_token == "class") {
                    handle_class();
                }
            }
        }
    }
}

std::string handle_class()
{
    std::string str__str;

    str_token.clear();

    str__str .clear();
    str__str += "class ";

    tok_ch = skip_white_spaces();
    if (token == E_TOKEN::E_IDENT) {
        str__str += str_token;
        tok_ch = skip_white_spaces();
        if (str_token == "of") {
            tok_ch = skip_white_spaces();
            str__str += " extends ";
            str__str += str_token;
            str__str += " {\n";
            for (;;) {
                tok_ch = skip_white_spaces();
                if (str_token == "endclass") {
                    str__str += "}\n";
                    app_object.
                    app_local_class.
                    push_back(str__str);

                    str_token.clear();
                    str__str .clear();

                    tok_ch = skip_white_spaces();
                    if (str_token == "class") {
                        str__str += handle_class();
                        break;
                    }   else {
                        app_object.
                        app_local_class.
                        push_back(str__str);
                        break;
                    }
                }
            }
        }
    }
    app_object.
    app_local_class.
    push_back(str__str);

    return std::string("");
}

void handle_parameter_local(int mode)
{
    int checkA = 0;
    for (;;) {
        str_token.clear();
        tok_ch = skip_white_spaces();

        if (str_token == "class" && checkA < 3) {
            token = E_TOKEN::E_CLASS;
            handle_class();
            break;
        }
        else if (str_token == "if" && checkA < 3) {
            token = E_TOKEN::E_IF;
            handle_if();
            break;
        }
        else if (str_token == "local" && checkA < 3) {
            token = E_TOKEN::E_LOCAL;
            handle_parameter_local(1);
            break;
        }
        else if (str_token == "parameter" && checkA < 3) {
            token = E_TOKEN::E_PARAMETER;
            handle_parameter_local(0);
            break;
        }

        if (token == E_TOKEN::E_IDENT) {
            if (mode == 0) push_parameter(str_token  ); else
            if (mode == 1) push_local    (str_token,0);

            str_token.clear();
            tok_ch = skip_white_spaces();

            if (str_token == "class" && checkA < 3) {
                token = E_TOKEN::E_CLASS;
                handle_class();
                break;
            }
            if (str_token == "if" && checkA < 3) {
                token = E_TOKEN::E_IF;
                handle_if();
                break;
            }
            else if (str_token == "local" && checkA < 3) {
                token = E_TOKEN::E_LOCAL;
                handle_parameter_local(1);
                break;
            }
            else if (str_token == "parameter" && checkA < 3) {
                token = E_TOKEN::E_PARAMETER;
                handle_parameter_local(0);
                break;
            }

            if (mode == 0) push_parameter(str_token  ); else
            if (mode == 1) push_local    (str_token,0);

            str_token.clear();
            tok_ch = skip_white_spaces();

            if (str_token == "class" && checkA < 3) {
                token = E_TOKEN::E_CLASS;
                handle_class();
                break;
            }
            if (str_token == "if" && checkA < 3) {
                token = E_TOKEN::E_IF;
                handle_if();
                break;
            }
            else if (str_token == "local" && checkA < 3) {
                token = E_TOKEN::E_LOCAL;
                handle_parameter_local(1);
                break;
            }
            else if (str_token == "parameter" && checkA < 3) {
                token = E_TOKEN::E_PARAMETER;
                handle_parameter_local(0);
                break;
            }

            if (mode == 0) push_parameter(str_token  ); else
            if (mode == 1) push_local    (str_token,0);

            continue;
        }   break;
    }
}

void yyparse()
{
    str_token.clear();
    tok_ch = skip_white_spaces();

    if (token == E_TOKEN::E_PARAMETER) { handle_parameter_local(0); }
    if (token == E_TOKEN::E_LOCAL    ) { handle_parameter_local(1); }
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
    if (errFlag == true || yyerror == true) {
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

        // parameter
        for (auto item : app_object.app_parameter) {
            std::cout
            << "\t\tthis."
            << item << " = "
            << item << ";"
            << std::endl;
        }
        std::cout << std::endl;
        // locals
        int length;
        for (auto it: app_object.app_local_vector) {
            std::cout << it << std::endl;
        }
        std::cout << " }" << std::endl;

        for (auto it: app_object.app_local_class) {
            std::cout << it << std::endl;
        }
/*
        std::map<std::string, std::vector<std::string> >::iterator it;
        for (
        it  = app_object.app_local_map.begin();
        it != app_object.app_local_map.end();
        it++) {
            std::vector<std::string> items = it->second;
            std::string item = items[items.size()-1];
            int  length      = item.size();
            
            item.erase(length-2,1);

            std::cout
            << "\t\tthis." << item
            << ";"
            << std::endl;
        }
*/

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
     
        scriptSize = source_length;

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

