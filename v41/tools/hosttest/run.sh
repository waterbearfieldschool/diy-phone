#!/bin/bash
# Compiles the pure-logic modules for the host and runs their tests.
# These need no board attached.
set -e
cd "$(dirname "$0")"
g++ -std=c++11 -Wall -O1 -I. -o /tmp/v41_hosttest test_main.cpp
/tmp/v41_hosttest
