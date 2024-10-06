#!/bin/bash
codesign -s - --force --options=runtime --entitlements rvvm_debug.entitlements $@