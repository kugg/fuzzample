#!/bin/bash
rm -f server.c.g*
gcc -g -ftest-coverage -fprofile-arcs server.c -o server_cov
echo "Use 'gcov server_cov' to generate coverage file"
echo "Run to measure coverage of your queue:"
echo "for f in output/queue/*; do cat \$f | LD_PRELOAD=./slp.so ./server_cov; done"
