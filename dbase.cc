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
# include <sstream>
# include <vector>
# include <exception>
# include <cctype>
using namespace std;

# include "dbase_macros.hh"     // placeholders macros

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

int tok_ch;         // current token char
int tok_ctx;        // token context

int ident_pos = 1;  // for string ident

std::string       str_para;
std::stringstream str_exec;

std::string str_token;      // current parsed token

std::vector<std::string> str_pvec(255);  // parameter vector (items)
std::vector<std::string>::const_iterator pvec_i;

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
    E_UNKNOWN    = 0x000
};
E_TOKEN token_state = E_TOKEN::E_UNKNOWN;    // state/ctx of parsed token

E_TOKEN token       = E_TOKEN::E_UNKNOWN;    // current token
E_TOKEN token_prev  = E_TOKEN::E_UNKNOWN;    // prev.   token
E_TOKEN token_next  = E_TOKEN::E_UNKNOWN;    // next    token

std::string token_number;     // parsed number

// function's forward declarations --------------------------
E_TOKEN yygetc    ();
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
    explicit dBaseError(const std::string & arg = "syntax error");
    //explicit dBaseError(const char* arg = "syntax error");
    std::string message;
};
dBaseError::dBaseError(const std::string & arg) {
    msg << putErrorMsg(arg);
    message.append(msg.str());
}
/*
dBaseError::dBaseError(const char* arg) {
    msg << putErrorMsg(arg);
    message.append(msg.str());
}*/
// -----------------------------------------
class dBaseSyntaxError: public exception {
    std::stringstream msg;
public:
    explicit dBaseSyntaxError() {
        msg << putErrorMsg("syntax error.");
        message.append(msg.str());
    }
    std::string message;
};
// -----------------------------------------
class dBaseUnknownError: public exception {
    std::stringstream msg;
public:
    explicit dBaseUnknownError();
    std::string message;
};
dBaseUnknownError::dBaseUnknownError() {
    msg << putErrorMsg("unknown error,");
    message.append(msg.str());
}
// ----------------------------------------
class dBaseCommentError: public exception {
    std::stringstream msg;
public:
    explicit dBaseCommentError();
    std::string message;
};
dBaseCommentError::dBaseCommentError() {
    msg << putErrorMsg("unterminated comment");
    msg << std::endl;

    msg << "possible start: "
        << dec << comment_start_row << ":"
        << dec << comment_start_col
        << std::endl;
    message.append(msg.str());
}
// ----------------------------------------
class dBaseDefaultError: public exception {
    std::stringstream msg;
public:
    explicit dBaseDefaultError();
    std::string message;
};
dBaseDefaultError::dBaseDefaultError() {
    msg << putErrorMsg("default error");
    message.append(msg.str());
}

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

// visitor pattern ------------------------------------
//
class dBaseAppClassCommand;         // program (name) class
class dBaseAppClassCTORCommand;     // constructor
class dBaseAppClassCTOREnd;         // end code for constructor
class dBaseAppClassDTORCommand;     // de-structor
class dBaseAppClassNewObject;       // this.f = new Object

class dBaseCodeClassCommand;        // application code
class dBaseCodeClassCTORCommand;    // constructor
class dBaseCodeClassDTORCommand;    // de-structor

class dBaseAppClassHeader;      // file class header
class dBaseAppClassFooter;      // file class footer

class dBaseAppParameter;        // parameter bmodal, ...

template <
    typename Ta,            // dBaseAppClassCommand
    typename Tb = void,     // dBaseAppClassCTORCommand
    typename Tc = void,     // dBaseAppClassCTOREnd
    typename Td = void,     // dBaseAppClassDTORCommand
    typename Te = void,     // dBaseAppClassNewObject
    // -------------------- //
    typename TA = void,     // dBaseCodeClassCommand
    typename TB = void,     // dBaseCodeClassCTORCommand
    typename TC = void,     // dBaseCodeClassDTORCommand

    typename T0 = void,     // dBaseAppClassHeader
    typename T1 = void,     // dBaseAppClassFooter
    //
    typename TP = void>     // dBaseAppParameter
class dBaseVisitable
{
public:
    virtual void accept(Ta&) = 0;
    virtual void accept(Tb&) = 0;
    virtual void accept(Tc&) = 0;
    virtual void accept(Td&) = 0;
    virtual void accept(Te&) = 0;
    //
    virtual void accept(TA&) = 0;
    virtual void accept(TB&) = 0;
    virtual void accept(TC&) = 0;
    //
    virtual void accept(T0&) = 0;
    virtual void accept(T1&) = 0;
    //
    virtual void accept(TP&) = 0;
};
template <typename T1>
class dBaseVisitable<T1>
{
public:
    virtual void accept(T1&) = 0;
};
// -----------------------------------------------------------
template <
    typename Ta       , typename Tb = void, typename Tc = void,
    typename Td = void, typename Te,
    typename TA = void, typename TB = void, typename TC = void,
    typename T0 = void, typename T1 = void,
    typename TP = void>
class dBaseVisitor
{
public:
    virtual void visit(Ta&) = 0;
    virtual void visit(Tb&) = 0;
    virtual void visit(Tc&) = 0;
    virtual void visit(Td&) = 0;
    virtual void visit(Te&) = 0;
    //
    virtual void visit(TA&) = 0;
    virtual void visit(TB&) = 0;
    virtual void visit(TC&) = 0;
    //
    virtual void visit(T0&) = 0;
    virtual void visit(T1&) = 0;
    //
    virtual void visit(TP&) = 0;
};

template <typename T1>
class dBaseVisitor<T1>
{
public:
    virtual void visit(T1&) = 0;
};

class dBaseCommandVisitor : public dBaseVisitor<
class dBaseAppClassCommand,
class dBaseAppClassCTORCommand,
class dBaseAppClassCTOREnd,
class dBaseAppClassDTORCommand,
class dBaseAppClassNewObject
// -------------------------------------
class dBaseCodeClassCommand,
class dBaseCodeClassCTORCommand,
class dBaseCodeClassDTORCommand,
// -------------------------------------
class dBaseAppClassHeader,
class dBaseAppClassFooter,
// -------------------------------------
dBaseAppParameter> { };

class dBaseAppObject {
public:
    std::stringstream js_code_str;      // javascript code string stream
    std::vector<std::string> parameters;
    std::map<std::string, std::vector<std::string>> object_parameter;
};

class  dBaseAST: public dBaseVisitable<dBaseCommandVisitor> { };
class  dBaseAppClassCommand:
public dBaseAppObject,
public dBaseAST
{
public:
    void accept(dBaseCommandVisitor& visitor) {
        visitor.visit(*this);
    }
};

// ---------------------------------------
// code for creation of an new object ...
// ---------------------------------------
class dBaseAppClassNewObject:
public dBaseAppObject,
public dBaseAST {
public:
    explicit dBaseAppClassNewObject() { }
    explicit dBaseAppClassNewObject(std::string oname) {
        bool found = false;
        for (auto item: object_parameter) {
            if (item[oname] != nullptr) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::cout << "falsch" << std::endl;
            std::vector<std::string> null_vec;
            null_vec.push_back(std::string("@null"));

            object_parameter[oname] = null_vec;
        }
    }
    explicit dBaseAppClassNewObject(std::string oname,
             dBaseAppClassNewObjectParameter  & pvec) {
    }

    void accept(dBaseCommandVisitor& visitor) {
        visitor.visit(*this);
    }
};

class dBaseAppClassCTOREnd:
public dBaseAppObject,
public dBaseAST {
public:
    void accept(dBaseCommandVisitor& visitor) {
        ident_pos -= 4;
        std::string tab(ident_pos, ' ');
        js_code_str <<   tab
        << "}" << std::endl;
        visitor.visit(*this);
    }
};

// ---------------------------------------------
// parameter - if source for a window form ...
// ---------------------------------------------
class dBaseAppParameter:
public dBaseAppObject,
public dBaseAST {
public:
    explicit dBaseAppParameter() { }
    explicit dBaseAppParameter(std::string pname) {
        parameters.push_back(pname);
    }
    void setParameter(std::string & pname) { parameters.push_back( pname); }
    void setParameter(std::string * pname) { parameters.push_back(*pname); }

    std::vector<std::string> getParameter() const {
        return parameters;
    }

    void accept(dBaseCommandVisitor& visitor) {
        for (auto item: parameters) {
            js_code_str << "var "
            << item
            << ";"  << std::endl;
        }
        visitor.visit(*this);
    }
};

// ---------------------------------------------
// header class (each file sit in a class) ...
// ---------------------------------------------
class  dBaseAppClassHeader:
public dBaseAppObject,
public dBaseAST {
public:
    void accept(dBaseCommandVisitor& visitor) {
        js_code_str << "class __test {" << std::endl;
        visitor.visit(*this);
    }
};
class  dBaseAppClassFooter:
public dBaseAppObject,
public dBaseAST {
public:
    void accept(dBaseCommandVisitor& visitor) {
        js_code_str << "}"
        << std::endl
        << "_test = new __test();"
        << std::endl;
        visitor.visit(*this);
    }
};

// ----------------------------------------------
// constructor for application: ** END HEADER
// ----------------------------------------------
class dBaseAppClassCTORCommand:
public dBaseAppObject,
public dBaseAST
{
    dBaseAppClassCTORCommand *ctor_parent;
public:
    explicit dBaseAppClassCTORCommand(
             dBaseAppClassCTORCommand* parent = nullptr) {
        ctor_parent = parent;
    }
    explicit dBaseAppClassCTORCommand(
             dBaseAppParameter*  pvec) {
        parameters = pvec->parameters;
    } 
    ~dBaseAppClassCTORCommand() {
        delete(ctor_parent);
    }

    dBaseAppClassCTORCommand & operator *() {
        return *ctor_parent;
    }

    void accept(dBaseCommandVisitor& visitor) {
        int i      = 0;
        ident_pos += 4;
        std::string str1(ident_pos, ' ');
        js_code_str << str1 << "constructor(";

        for (auto item: parameters) {
            js_code_str  << item;
            if (++i < parameters.size())
            js_code_str  << ", ";
        }   js_code_str  << ") {" << std::endl;

        ident_pos += 4;
        for (auto item: parameters) {
            std::string tab(ident_pos, ' ');
            js_code_str << tab << "this."
            << item     << " = "
            << item     << ";"
            << std::endl;
        }

        visitor.visit(*this);
    }
};
class dBaseAppClassDTORCommand:
public dBaseAppObject,
public dBaseAST
{
public:
    void accept(dBaseCommandVisitor& visitor) {
        ident_pos += 4;
        std::string str(ident_pos, ' ');
        js_code_str << str << "}" << std::endl;
        //
        visitor.visit(*this);
    }
};
// ------------------------------------------------------------
class dBaseCodeClassCommand:
public dBaseAppObject,
public dBaseAST
{
public:
    void accept(dBaseCommandVisitor& visitor) {
        visitor.visit(*this);
    }
};
class dBaseCodeClassCTORCommand:
public dBaseAppObject,
public dBaseAST
{
public:
    void accept(dBaseCommandVisitor& visitor) {
        ident_pos += 4;
        js_code_str << "class Form1 {" << std::endl;
        visitor.visit(*this);
    }
};
class dBaseCodeClassDTORCommand:
public dBaseAppObject,
public dBaseAST
{
public:
    void accept(dBaseCommandVisitor& visitor) {
        std::cout << "accep:6" << std::endl;
        visitor.visit(*this);
    }
};

class dBaseExecVisitor:
public dBaseCommandVisitor {
public:
    template <typename T> void emit_code(T const &t) {
        std::cout << t.js_code_str.str() << std::endl;
    }

    void visit(dBaseAppClassHeader       & cmd) { emit_code(cmd); }
    void visit(dBaseAppClassFooter       & cmd) { emit_code(cmd); }

    void visit(dBaseAppParameter         & cmd) { emit_code(cmd); }
    void visit(dBaseAppClassCTOREnd      & cmd) { emit_code(cmd); }
    void visit(dBaseAppClassCommand      & cmd) { emit_code(cmd); }
    void visit(dBaseAppClassCTORCommand  & cmd) { emit_code(cmd); }
    void visit(dBaseAppClassDTORCommand  & cmd) { emit_code(cmd); }

    void visit(dBaseCodeClassCommand     & cmd) { emit_code(cmd); }
    void visit(dBaseCodeClassCTORCommand & cmd) { emit_code(cmd); }
    void visit(dBaseCodeClassDTORCommand & cmd) { emit_code(cmd); }
};

// --------------------------
// actual visitor's.
// --------------------------
class dBaseExecVisitor exec_visitor;

// --------------------------
// the list of commands ...
// --------------------------
std::vector<dBaseAST*> command_list;

void dBaseApply(dBaseCommandVisitor& visitor)
{
    for (std::vector<dBaseAST*>::size_type i = 0;
        i < command_list.size(); ++i)
    {
        // send the visitor to the command.
        command_list[i]->accept(visitor);
    }
}

void testcase()
{
    dBaseAppParameter        app_parameter;
    app_parameter.setParameter( new std::string("bmodal"));
    app_parameter.setParameter( new std::string("aaa"));
    app_parameter.setParameter( new std::string("zzz"));
    command_list.push_back(&app_parameter);

    dBaseAppClassHeader      app_header;
    command_list.push_back(& app_header);

    dBaseAppClassCTORCommand app_ctor(&app_parameter);
    command_list.push_back(& app_ctor);


    dBaseAppClassCommand     app;
    ident_pos = 9;
    std::string str1(ident_pos, ' ');
    app.js_code_str << str1
    << "this.f = new Form1( );";
    ident_pos = 1;
    command_list.push_back(& app);


    dBaseAppClassCTOREnd     app_ctor_end;
    command_list.push_back(& app_ctor_end);

    dBaseCodeClassCTORCommand code_ctor;
    command_list.push_back(&code_ctor);


    dBaseAppClassDTORCommand app_dtor;
    command_list.push_back(& app_dtor);
    //
    dBaseAppClassFooter      app_footer;
    command_list.push_back(& app_footer);

    dBaseApply(exec_visitor);
}

// -----------------------------
// locale throw exception's ...
// -----------------------------
void throw_locale()
{
    switch (tok_ctx) {
        case ECTX_UNKNOWN: throw dBaseUnknownError();  break;
        case ECTX_COMMENT: throw dBaseCommentError();  break;
        case ECTX_DEFAULT: throw dBaseDefaultError();  break;
    }
}

// ----------------------------------------
// inline funktion, to avoid duplicates,
// and disk space wasteing ...
// ----------------------------------
void setCommentTrue() {
    tok_ctx = ECTX_COMMENT;
    if (comment_start_on == false) {
        comment_start_on  = true ;

        comment_start_row = line_row;
        comment_start_col = line_col;
    }
}

// ---------------------------------------------
// skip comments:
// C++     like one liner: //
// C       like block:     /* */
// dBase 1 like one liner: **
// dBase 2 like one liner: &&
// ---------------------------------------------
E_TOKEN skip_comment()
{
    static int nestedComment = 1;
    for (;;) {
        tok_ch = yyin.get();
        if (tok_ch == ' ' ) { line_col += 1; } else
        if (tok_ch == '\t') { line_col += 8; } else
        if (tok_ch == '\n') {
            line_row += 1;
            line_col  = 1;
        }   else
        if (tok_ch == '/') {
            tok_ch = yyin.get();
            if (tok_ch == ' ' ) { line_col += 1; } else
            if (tok_ch == '\t') { line_col += 8; } else
            if (tok_ch == '\n') {
                line_row += 1;
                line_col  = 1;
            }   else
            if (tok_ch == '*') {
                ++nestedComment;
                continue;
            }
        }
        else if (tok_ch == '*') {
            tok_ch = yyin.get();
            if (tok_ch == ' ' ) { line_col += 1; } else
            if (tok_ch == '\t') { line_col += 8; } else
            if (tok_ch == '\n') {
                line_row += 1;
                line_col  = 1;
            }   else
            if (tok_ch == '/') {
                if (--nestedComment <= 0) {
                    break;
                }
            }
        }
        else if (tok_ch == EOF)
        throw dBaseCommentError();
    }
}

void get_oneline_comment()
{
    while ((tok_ch = yyin.get()) != '\n' && tok_ch != EOF);
    if (tok_ch == EOF) {
        throw dBaseSyntaxError();
    }   else
    if (tok_ch == '\n') {
        line_col  = 1;
        line_row += 1;
    }
}

int white_skipper()
{
    for (;;) {
        tok_ch = yyin.get();
        if (tok_ch == EOF) {
            throw dBaseSyntaxError();
        }   else
        if (tok_ch == '*') {
            tok_ch = yyin.get();
            if (tok_ch == '*') {
                get_oneline_comment();
                continue;
            }   else {
                yyerror = true;
                throw dBaseSyntaxError();
            }
        }   else
        if (tok_ch == '&') {
            tok_ch = yyin.get();
            if (tok_ch == '&') {
                get_oneline_comment();
                continue;
            }   else {
                yyerror = true;
                throw dBaseSyntaxError();
            }
        }   else
        if (tok_ch == '/') {
            tok_ch = yyin.get();
            if (tok_ch == '/') {
                get_oneline_comment();
                continue;
            }   else
            if (tok_ch == '*') {
                skip_comment();
                continue;
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
            continue;
        }   else
        if (tok_ch == '=') {
            token = E_TOKEN::E_ASSIGN;
            return tok_ch;
        }
        else {
            break;
        }
    }
    return tok_ch;
}

// -------------------------------------------
// get char from token stream, and update
// the occurence (for error reporting) ...
// -------------------------------------------
E_TOKEN yygetc()
{
    tok_ch = white_skipper();
    if (isalpha(tok_ch)
    ||  tok_ch == '_') {
        str_token.clear();
        str_token.append(1,tok_ch);
        token = getToken();

        #ifdef DEBUG
        std::stringstream str;
        str << "yygetc: " << str_token << std::endl;
        dbDebug() << str.str();
        #endif

        if (str_token == "parameter") { token = E_TOKEN::E_PARAMETER; } else
        if (str_token == "local")     { token = E_TOKEN::E_LOCAL;     } else
        if (str_token == "=")         { token = E_TOKEN::E_ASSIGN;    } else

        token = E_TOKEN::E_IDENT;
    }
    return token;
}

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

E_TOKEN yyparse()
{
    #ifdef DEBUG
    std::stringstream str;
    #endif
    start:
    token = yygetc();
    if (str_token == "parameter") {
        for (;;) {

            #ifdef DEBUG
            str.clear();
            str << "token: " << str_token << std::endl;
            dbDebug() << str.str();
            #endif

            str_token.clear();
            tok_ch = white_skipper();
            if (tok_ch == EOF) {
                throw dBaseSyntaxError();
            }   mark1:
            if (isalpha(tok_ch)
            ||  tok_ch == '_') {
                str_token.clear();
                str_token.append(1,tok_ch);
                token = getToken();

                #ifdef DEBUG
                str.clear();
                str << "para---> " << str_token << std::endl;
                dbDebug() << str.str();
                #endif

                tok_ch = white_skipper();
                if (tok_ch == ',') {

                    #ifdef DEBUG
                    str.clear();
                    str << "comma" << std::endl;
                    dbDebug() << str.str();
                    #endif

                    goto mark1;
                }   else
                if (isalpha(tok_ch)
                ||  tok_ch == '_') {
                    str_token.clear();
                    str_token.append(1,tok_ch);
                    token = getToken();

                    #ifdef DEBUG
                    str.clear();
                    str << "idar---> " << str_token << std::endl;
                    dbDebug() << str.str();
                    #endif

                    if (token == E_TOKEN::E_IDENT) {
                        if (str_token == "local")
                        goto mark_local;
                    }
                    yyerror = true;
                    tok_ch = white_skipper();
                    if (tok_ch == EOF) {
                        yyerror = true;
                        throw dBaseSyntaxError();
                    }   else
                    if (tok_ch == '=') {
                        goto mark2;
                    }   else {
                        yyerror = true;
                        throw dBaseSyntaxError();
                    }
                }
            }   else
            if (tok_ch == ',') {

                #ifdef DEBUG
                str.clear();
                str << "comma 222 " << std::endl;
                dbDebug() << str.str();
                #endif

                tok_ch = white_skipper();
                goto mark1;
            }   else {

                #ifdef DEBUG
                str.clear();
                str << "---> "
                    << std::dec << static_cast<int>(tok_ch) << std::string(", ")
                    << std::dec << static_cast<int>(token)  << std::endl;
                dbDebug() << str.str();
                #endif

                throw dBaseSyntaxError();
            }
        }
    }   else
    if (token == E_TOKEN::E_LOCAL) {
        mark_local:
        for (;;) {

            #ifdef DEBUG
            str.clear();
            str << "token: " << str_token << std::endl;
            dbDebug() << str.str();
            #endif

            str_token.clear();
            tok_ch = white_skipper();
            if (tok_ch == EOF) {
                throw dBaseSyntaxError();
            }   mark_1:
            if (isalpha(tok_ch)
            ||  tok_ch == '_') {
                str_token.clear();
                str_token.append(1,tok_ch);
                token = getToken();

                #ifdef DEBUG
                str.clear();
                str << "loka---> " << str_token << std::endl;
                dbDebug() << str.str();
                #endif

                tok_ch = white_skipper();
                if (tok_ch == ',') {

                    #ifdef DEBUG
                    str.clear();
                    str << "comma" << std::endl;
                    dbDebug() << str.str();
                    #endif

                    goto mark_1;
                }   else
                if (isalpha(tok_ch)
                ||  tok_ch == '_') {
                    str_token.clear();
                    str_token.append(1,tok_ch);
                    token = getToken();

                    #ifdef DEBUG
                    str.clear();
                    str << "idar22---> " << str_token << std::endl;
                    dbDebug() << str.str();
                    #endif

                    yyerror = true;
                    tok_ch = white_skipper();
                    if (tok_ch == EOF) {
                        yyerror = true;
                        throw dBaseSyntaxError();
                    }   else
                    if (tok_ch == '=') {
                        goto mark2;
                    }   else {
                        yyerror = true;
                        throw dBaseSyntaxError();
                    }
                }
            }   else
            if (tok_ch == ',') {

                #ifdef DEBUG
                str.clear();
                str << "comma 444 " << std::endl;
                dbDebug() << str.str();
                #endif

                tok_ch = white_skipper();
                goto mark_1;
            }   else {

                #ifdef DEBUG
                str.clear();
                str << "---> "
                    << std::dec << static_cast<int>(tok_ch) << std::string(", ")
                    << std::dec << static_cast<int>(token)  << std::endl;
                dbDebug() << str.str();
                #endif

                throw dBaseSyntaxError();
            }
        }
    }   else
    if (token == E_TOKEN::E_IDENT) {
        for (;;) {
            yyerror = true;

            #ifdef DEBUG
            str.clear();
            str << "ident => " << str_token << std::endl;
            dbDebug() << str.str();
            #endif

            tok_ch = white_skipper();
            if (tok_ch == '=') {
                mark2:
                str_token.clear();
                tok_ch = white_skipper();
                if (isalpha(tok_ch)
                ||  tok_ch == '_') {
                    str_token.append(1,tok_ch);
                    token = getToken();
                    if (str_token == "new") {

                        #ifdef DEBUG
                        str.clear();
                        str << "newer---> " << str_token << std::endl;
                        dbDebug() << str.str();
                        #endif

                        str_token.clear();
                        tok_ch = white_skipper();
                        if (isalpha(tok_ch)
                        ||  tok_ch == '_') {
                            str_token.clear();
                            str_token.append(1,tok_ch);
                            token = getToken();

                            #ifdef DEBUG
                            str.clear();
                            str << "new classler: " << str_token << std::endl;
                            dbDebug() << str.str();
                            #endif

                            tok_ch = white_skipper();
                            if (tok_ch == '(') {
                                tok_ch = white_skipper();
                                if (tok_ch == ')') {
                                    yyerror = false;
                                    tok_ch = white_skipper();
                                    if (tok_ch == '/') { yyerror = true; throw dBaseSyntaxError(); }
                                    goto start;
                                }
                            }
                            else throw dBaseSyntaxError();
                        }   else throw dBaseSyntaxError();
                    }   else {
                        #ifdef DEBUG
                        str.clear();
                        str << "assinger=> " << str_token << std::endl;
                        dbDebug() << str.str();
                        #endif

                        goto start;
                    }
                }   else
                if (tok_ch == '.') {
                    printf("punkter\n");
                    token = handle_false_true();
                    if (token == E_TOKEN::E_FALSE) {

                        #ifdef DEBUG
                        str.clear();
                        str << "false" << std::endl;
                        dbDebug() << str.str();
                        #endif

                        yyerror = false;
                        goto start;
                    }   else
                    if (token == E_TOKEN::E_TRUE) {

                        #ifdef DEBUG
                        str.clear();
                        str << "true" << std::endl;
                        dbDebug() << str.str();
                        #endif

                        yyerror = false;
                        goto start;
                    }
                }   else
                if (tok_ch >= '0' && tok_ch <= '9') {
                    yyerror = false;
                    str_token.clear();
                    token = getNumber();

                    #ifdef DEBUG
                    str.clear();
                    str << "num:==>> " << str_token << std::endl;
                    dbDebug() << str.str();
                    #endif

                    goto start;
                }
            }
        }
    }
    return token;
}

// ------------------------
// parse a token ident ...
// ------------------------
E_TOKEN getToken()
{
    token = E_TOKEN::E_IDENT;
    int len = 0;

    while (1) {
        tok_ch = yyin.get();
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
    return token;
}

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

// ---------------------------------------
// main entry function, do init stuff ...
// ---------------------------------------
int main(int argc, char **argv)
{
    testcase();
return 0;

    std::cout << "dbase2js (c) 2019 Jens Kallup" << std::endl;
    std::cout << "all rights reserved."          << std::endl;

    if (argc < 2) {
        std::cerr
        << "no source file given."
        << std::endl;
        return 1;
    }

    std::string str_error;

    bool errFlag = false;
    bool b_exec  = false;

    std::string sc(argv[1]);
    std::string para;

    str_exec.clear();

    try {
        // ---------------------------
        // set default/init stuff ...
        // ---------------------------
        line_row = 1;
        line_col = 1;

        comment_start_on = false;

        std::stringstream iss;  iss << argv[1] << ".prg";
        std::stringstream oss;  oss << argv[1] << ".pro";

        yyin .open(iss.str(), ios_base::in );
        yyout.open(oss.str(), ios_base::out);

        if (!yyin ) { throw dBaseError("read input file."  ); }
        if (!yyout) { throw dBaseError("write output file."); }

        // -------------------------------------
        // check, if called from cli or wif ...
        // -------------------------------------
        if (sc.size() > 11) {
            sc = sc.erase(0,11);
            sc = sc.replace(sc.find("."), 1, "_");
        }

        str_exec
            << "class _Z"
            << sc
            << " {\n\tconstructor(@parameter@)"
            << " {@defaultparameter@";

        // ----------------------------
        // get file size:
        // ----------------------------
        yyin.seekg(0, ios_base::end); int source_length = yyin.tellg();
        yyin.seekg(0, ios_base::beg);

        // ----------------------------------------
        for (;;) {
        label_0:
            token = yyparse();
        }
        label_end:
        // ----------------------------------------

printf("EEEMNNNNDE\n");

        if (yyerror == true)
        throw dBaseSyntaxError();




	    for ( pvec_i = str_pvec.begin();
	          pvec_i!= str_pvec.end();
	        ++pvec_i) {
	        if ((*pvec_i).size() > 0) {
		        if (pvec_i+1 == str_pvec.end())
		        {   str_para.append((*pvec_i));
			        para.append("\n\t\tthis.");
			        para.append((*pvec_i));
			        para.append(" = ");
			        para.append((*pvec_i));
			        para.append(";");
		            break;
		          }   else {
		            str_para.append((*pvec_i));
		            str_para.append(", ");
			        para.append("\n\t\tthis.");
			        para.append((*pvec_i));
			        para.append(" = ");
			        para.append((*pvec_i));
			        para.append(";");
		        }
	        }
	    }
    }

    catch (dBaseUnknownError &err) { str_error.append(err.message); errFlag = true; }
    catch (dBaseDefaultError &err) { str_error.append(err.message); errFlag = true; }
    catch (dBaseCommentError &err) { str_error.append(err.message); errFlag = true; }
    catch (dBaseError        &err) { str_error.append(err.message); errFlag = true; }
    catch (dBaseSyntaxError  &err) { str_error.append(err.message); errFlag = true; }

    catch (...) {
        std::cout << "unknown error occur!\n" <<
        std::endl ;
        errFlag = true;
    }

    // ---------------------------------
    // check, and close file stream ...
    // ---------------------------------
    yyin .close();
    yyout.close();

    // catch was in action ...
    if (errFlag == true && yyerror == true) {
        std::cout << str_error  << std::endl;
        std::cout << "FAIL !!!" << std::endl;
        return 1;
    }

    // else, all done
    std::cout << "SUCCESS" << std::endl;
    return 0;
}
