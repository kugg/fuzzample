#!/bin/bash
rm server.c.g*
gcc -g -ftest-coverage -fprofile-arcs server.c -o server
echo "Use 'gcov server' to generate coverage file'
