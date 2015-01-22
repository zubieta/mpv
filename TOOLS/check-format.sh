#!/bin/sh

clang-format $1 | diff $1 - | colordiff
