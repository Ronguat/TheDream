#!/usr/bin/env bash
# Runs ue_remote.py with the engine's bundled Python; see that file for usage.
"/c/Program Files (x86)/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" \
  "$(dirname "$0")/ue_remote.py" "$@"
