#!/bin/sh
build/mpv --lua=player/mruby/test.mrb --quiet --msg-level=all=error $1
