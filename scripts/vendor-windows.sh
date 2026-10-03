#!/bin/sh
# Populates vendor/ucrt64 with the GTK4/GLib runtime files needed by
# bundle-windows.sh, so the Windows bundle can be produced without an installed
# MSYS2.  Run this once when upgrading the GTK4 runtime; the result is meant to
# be committed to the repository.
#
# Usage: vendor-windows.sh <sysroot> <vendor-dir> [objdump]
#
#   sysroot    MSYS2 UCRT64 prefix that holds bin/, lib/ and share/
#   vendor-dir directory that receives bin/, lib/ and share/
#   objdump    optional cross objdump used to read PE imports
set -eu

SYSROOT=$1
VENDOR=$2
OBJDUMP=${3:-x86_64-w64-mingw32-objdump}
if ! command -v "$OBJDUMP" >/dev/null 2>&1; then
  OBJDUMP=objdump
fi

BIN="$SYSROOT/bin"
LIB="$SYSROOT/lib"
SHARE="$SYSROOT/share"

if [ ! -d "$BIN" ]; then
  echo "vendor-windows: sysroot bin directory not found: $BIN" >&2
  exit 1
fi

echo "vendor-windows: collecting runtime into $VENDOR"
mkdir -p "$VENDOR/bin" \
         "$VENDOR/lib/gdk-pixbuf-2.0/2.10.0/loaders" \
         "$VENDOR/share/glib-2.0/schemas" \
         "$VENDOR/share/gtk-4.0" \
         "$VENDOR/share/icons" \
         "$VENDOR/share/locale"

# --- Collect every non-system DLL used by the GTK stack ---------------------
# The seed below pulls in the GTK/GLib entry points; the recursive walk over the
# PE import table then gathers their transitive dependencies.
is_windows_system_dll() {
  case "$(printf '%s' "$1" | tr 'A-Z' 'a-z')" in
    api-ms-win-* | ext-ms-win-* | kernel32.dll | kernelbase.dll | ntdll.dll | \
    user32.dll | gdi32.dll | gdi32full.dll | advapi32.dll | shell32.dll | \
    shlwapi.dll | ole32.dll | oleaut32.dll | comdlg32.dll | comctl32.dll | \
    ws2_32.dll | wsock32.dll | winmm.dll | imm32.dll | dwmapi.dll | uxtheme.dll | \
    d3d11.dll | d3d12.dll | dxgi.dll | d2d1.dll | dwrite.dll | usp10.dll | \
    iphlpapi.dll | secur32.dll | bcrypt.dll | crypt32.dll | setupapi.dll | \
    cfgmgr32.dll | version.dll | rpcrt4.dll | msvcrt.dll | powrprof.dll | \
    winspool.drv | windowscodecs.dll | wintrust.dll | hid.dll | mfplat.dll | \
    avrt.dll | authz.dll | netapi32.dll | userenv.dll | psapi.dll | dbghelp.dll | \
    gdiplus.dll | dnsapi.dll | bcryptprimitives.dll | msimg32.dll | winhttp.dll | \
    dbgeng.dll | wlanapi.dll | wtsapi32.dll | d3dcompiler_47.dll | opengl32.dll | \
    glu32.dll | mswsock.dll | nlaapi.dll | dhcpcsvc.dll | propsys.dll | \
    schannel.dll | ncrypt.dll | cryptbase.dll | rsaenh.dll | msasn1.dll | \
    dcomp.dll | shcore.dll | dxva2.dll | d3d10warp.dll | rmclient.dll | \
    twinapi.appcore.dll | windows.ui.dll)
      return 0 ;;
  esac
  return 1
}

resolve_dll() {
  if [ -f "$BIN/$1" ]; then
    printf '%s' "$BIN/$1"
    return 0
  fi
  find "$BIN" -maxdepth 1 -iname "$1" 2>/dev/null | head -n 1 || true
}

collect_deps() {
  root=$1
  queue="$root"
  while [ -n "$queue" ]; do
    f=${queue%% *}
    if [ "$queue" = "$f" ]; then queue=""; else queue=${queue#* }; fi
    case "$seen" in *" $f "*) continue ;; esac
    seen="$seen$f "

    deps=$("$OBJDUMP" -p "$f" 2>/dev/null | sed -n 's/.*DLL Name: //p') || true
    for dll in $deps; do
      lower=$(printf '%s' "$dll" | tr 'A-Z' 'a-z')
      if is_windows_system_dll "$lower"; then
        continue
      fi
      src=$(resolve_dll "$dll")
      if [ -n "$src" ] && [ -f "$src" ]; then
        if [ ! -f "$VENDOR/bin/$(basename "$src")" ]; then
          cp -p "$src" "$VENDOR/bin/"
        fi
        case "$seen" in *" $src "*) ;; *) queue="$queue $src" ;; esac
      fi
    done
  done
}

seen=" "
for seed in libgtk-4-1.dll libgio-2.0-0.dll libgdk_pixbuf-2.0-0.dll; do
  if [ -f "$BIN/$seed" ]; then
    cp -p "$BIN/$seed" "$VENDOR/bin/"
    collect_deps "$BIN/$seed"
  fi
done

# --- Application-level DLLs used by the manager -----------------------------
for extra in libintl-8.dll; do
  [ -f "$BIN/$extra" ] && cp -p "$BIN/$extra" "$VENDOR/bin/" || true
done

# --- gspawn helpers --------------------------------------------------------
# GLib launches child processes through one of these helper executables and
# looks for them next to libglib-2.0-0.dll.  Without them every g_subprocess
# spawn fails with "Failed to execute helper program".  The console variant is
# used when the parent already has a console, the plain one otherwise.
for helper in gspawn-win64-helper.exe gspawn-win64-helper-console.exe; do
  if [ -f "$BIN/$helper" ]; then
    cp -p "$BIN/$helper" "$VENDOR/bin/"
    seen=" "
    collect_deps "$BIN/$helper"
  fi
done

# --- gdk-pixbuf loaders ----------------------------------------------------
if [ -d "$LIB/gdk-pixbuf-2.0" ]; then
  loader_dir=$(find "$LIB/gdk-pixbuf-2.0" -maxdepth 2 -type d -name loaders | head -n 1 || true)
  if [ -n "$loader_dir" ]; then
    cp -p "$loader_dir/"*.dll "$VENDOR/lib/gdk-pixbuf-2.0/2.10.0/loaders/"
    # Loader dependencies also need to be bundled at the top level.
    for loader in "$VENDOR/lib/gdk-pixbuf-2.0/2.10.0/loaders/"*.dll; do
      [ -f "$loader" ] || continue
      seen=" "
      collect_deps "$loader"
    done
  fi
fi

# --- GTK data --------------------------------------------------------------
[ -d "$SHARE/gtk-4.0" ] && cp -R "$SHARE/gtk-4.0/." "$VENDOR/share/gtk-4.0/"
if [ -d "$SHARE/glib-2.0/schemas" ]; then
  cp -p "$SHARE/glib-2.0/schemas/"*.xml "$VENDOR/share/glib-2.0/schemas/"
fi

# Only ship the icon parts GTK4 actually uses on Windows.  AdwaitaLegacy is for
# GTK3 applications and the XCursor/Adwaita cursor set is not requested by this
# application (Win32 uses the native cursors), so both are skipped.
mkdir -p "$VENDOR/share/icons"
copy_theme() {
  theme=$1
  shift
  [ -d "$SHARE/icons/$theme" ] || return 0
  mkdir -p "$VENDOR/share/icons/$theme"
  for part in "$@"; do
    if [ -e "$SHARE/icons/$theme/$part" ]; then
      cp -R "$SHARE/icons/$theme/$part" "$VENDOR/share/icons/$theme/"
    fi
  done
}
copy_theme Adwaita index.theme symbolic scalable 16x16
copy_theme hicolor index.theme symbolic scalable
for lang in en zh_CN; do
  [ -d "$SHARE/locale/$lang" ] && cp -R "$SHARE/locale/$lang" "$VENDOR/share/locale/"
done

# --- Pre-generated caches (so bundling needs no Wine) ----------------------
if command -v wine >/dev/null 2>&1 && [ -f "$BIN/glib-compile-schemas.exe" ]; then
  echo "vendor-windows: compiling GSettings schemas"
  win_schemas=$(winepath -w "$VENDOR/share/glib-2.0/schemas" 2>/dev/null || printf '%s' "$VENDOR/share/glib-2.0/schemas")
  WINEPATH="$BIN" WINEDEBUG=-all wine "$BIN/glib-compile-schemas.exe" --strict "$win_schemas" \
    >/dev/null 2>&1 || true
fi

loaders="$VENDOR/lib/gdk-pixbuf-2.0/2.10.0/loaders"
if command -v wine >/dev/null 2>&1 && [ -f "$BIN/gdk-pixbuf-query-loaders.exe" ]; then
  echo "vendor-windows: generating gdk-pixbuf loaders.cache"
  set -- ""
  for loader in "$loaders/"*.dll; do
    [ -f "$loader" ] || continue
    win_loader=$(winepath -w "$loader" 2>/dev/null || printf '%s' "$loader")
    set -- "$@" "$win_loader"
  done
  shift
  WINEPATH="$BIN" WINEDEBUG=-all wine "$BIN/gdk-pixbuf-query-loaders.exe" "$@" \
    > "$loaders/loaders.cache" 2>/dev/null || true
  if [ -s "$loaders/loaders.cache" ]; then
    sed -i "s#\"[^\"]*[/\\\\]lib/gdk-pixbuf-2.0/#\"lib/gdk-pixbuf-2.0/#g" \
      "$loaders/loaders.cache" 2>/dev/null || true
  fi
fi

echo "vendor-windows: vendored $(ls "$VENDOR/bin"/*.dll 2>/dev/null | wc -l) DLL(s) into $VENDOR"
du -sh "$VENDOR" 2>/dev/null || true
