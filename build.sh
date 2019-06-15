#!/bin/bash

export PATH=/usr/local/bin::$PATH
#flex  -o dbase.lex.cc -l  dbase.lpp
#bison -o dbase.tab.cc -vd dbase.ypp

#g++ -D_GLIBCXX_USE_CXX11_ABI=0 -std=c++14 -Wno-write-strings -o dbase2js dbase.lex.cc dbase.tab.cc -lfl -ly

g++ -g -DDEBUG -D_GLIBCXX_USE_CXX11_ABI=0 -std=c++14 -Wno-write-strings -o dbase2js dbase.cc

echo "ready, press <enter> to return to console."
read enter

