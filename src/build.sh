#!/bin/bash
gcc -g -c tinyexpr.c -o tinyexpr.o -std=c99 -lm
g++ -c SejScript.cpp -o SejScript.o -O3
g++ -o SejScript tinyexpr.o SejScript.o
rm tinyexpr.o SejScript.o
rm ../SejScript
mv ./SejScript ../SejScript