#!/bin/sh
# gen.sh <game.HC ...>  -- regenerate gen/ from the TempleOS sources (paths relative to tos_src/tos2)
H="$(cd "$(dirname "$0")" && pwd)"
SRC="${TOS_SRC:-$H/tos_src}"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) PY=python ;; *) PY=python3 ;; esac   # Windows' python3 may be the Store stub
cd "$SRC" && "${PYTHON:-$PY}" -I "$H/tools/hc_emit.py" tos2 m_tinkeros_TinkerOS/src "$H/gen" "$@"
rm -f "$H/obj/tos_lib.obj"   # build.cmd recompiles the library only when its object is missing
