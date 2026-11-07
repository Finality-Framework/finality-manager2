#!/bin/sh
# Bundles the Windows build of Finality Manager together with every GTK4/GLib
# runtime dependency it needs to run on a machine without MSYS2 installed.
#
# Usage: bundle-windows.sh <exe> <runtime-dir> <res-dir> <output-dir> [objdump]
#
#   exe         path to finality-manager.exe
#   runtime-dir prefix that holds bin/, lib/ and share/ (either an MSYS2 sysroot
#               or the vendored vendor/ucrt64 directory)
#   res-dir     directory with finality-loader-1.6.0.jar and the png/ico assets
#   output-dir  directory that receives the self-contained bundle
#   objdump     optional cross objdump used to read PE imports
#
# The layout mirrors the MSYS2 prefix (exe and DLLs at the top, share/ and
# lib/ below) so GLib finds its data relative to the executable.  When the
# runtime directory already ships a gdk-pixbuf loaders.cache and a compiled
# GSettings schema cache they are copied as-is, so bundling needs no Wine.
set -eu

EXE=$1
RUNTIME=$2
RESDIR=$3
OUT=$4
OBJDUMP=${5:-x86_64-w64-mingw32-objdump}
if ! command -v "$OBJDUMP" >/dev/null 2>&1; then
  OBJDUMP=objdump
fi

BIN="$RUNTIME/bin"
LIB="$RUNTIME/lib"
SHARE="$RUNTIME/share"

if [ ! -f "$EXE" ]; then
  echo "bundle-windows: executable not found: $EXE" >&2
  exit 1
fi
if [ ! -d "$BIN" ]; then
  echo "bundle-windows: runtime bin directory not found: $BIN" >&2
  echo "bundle-windows: run 'make vendor-windows' first, or set WINDOWS_SYSROOT" >&2
  exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT"

echo "bundle-windows: copying executable"
cp "$EXE" "$OUT/"

# Windows ships these with the OS; they must not be bundled.
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

# Resolves a DLL name to a file in the runtime bin directory.
resolve_dll() {
  dll=$1
  if [ -f "$BIN/$dll" ]; then
    printf '%s' "$BIN/$dll"
    return 0
  fi
  found=$(find "$BIN" -maxdepth 1 -iname "$dll" 2>/dev/null | head -n 1 || true)
  printf '%s' "$found"
}

# Recursively copies every non-system DLL imported by $1 into the bundle.
copy_deps() {
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
        base=$(basename "$src")
        if [ ! -f "$OUT/$base" ]; then
          cp "$src" "$OUT/"
          copied=$((copied + 1))
        fi
        case "$seen" in *" $src "*) ;; *) queue="$queue $src" ;; esac
      else
        echo "bundle-windows: warning: $dll not found in $BIN" >&2
      fi
    done
  done
}

echo "bundle-windows: resolving DLL dependencies"
seen=" "
copied=0
copy_deps "$EXE"
echo "bundle-windows: copied $copied DLL(s)"

# GLib locates its gspawn helper executables next to libglib-2.0-0.dll.  They
# are .exe files so the DLL walk above does not pick them up; without them
# g_subprocess spawning fails with "Failed to execute helper program".
echo "bundle-windows: copying gspawn helpers"
for helper in gspawn-win64-helper.exe gspawn-win64-helper-console.exe; do
  if [ -f "$BIN/$helper" ]; then
    cp "$BIN/$helper" "$OUT/"
  else
    echo "bundle-windows: warning: $helper not found in $BIN" >&2
  fi
done

# --- Application resources ------------------------------------------------
if [ -d "$RESDIR" ]; then
  echo "bundle-windows: copying application resources"
  mkdir -p "$OUT/res"
  cp "$RESDIR/finality-loader-1.6.0.jar" "$OUT/res/" 2>/dev/null || true
  for asset in "$RESDIR"/*.png "$RESDIR"/*.ico; do
    [ -f "$asset" ] && cp "$asset" "$OUT/res/" 2>/dev/null || true
  done
fi

# --- GTK / GLib data files -------------------------------------------------
if [ -d "$SHARE/glib-2.0/schemas" ]; then
  echo "bundle-windows: copying GSettings schemas"
  mkdir -p "$OUT/share/glib-2.0/schemas"
  cp "$SHARE/glib-2.0/schemas/"*.xml "$OUT/share/glib-2.0/schemas/" 2>/dev/null || true
  cp "$SHARE/glib-2.0/schemas/gschemas.compiled" "$OUT/share/glib-2.0/schemas/" 2>/dev/null || true
fi

if [ -d "$SHARE/gtk-4.0" ]; then
  echo "bundle-windows: copying GTK data"
  mkdir -p "$OUT/share"
  cp -R "$SHARE/gtk-4.0" "$OUT/share/"
fi

# Only ship the icon parts GTK4 uses on Windows.  AdwaitaLegacy is for GTK3
# applications and the Adwaita cursor set is not requested (Win32 uses the
# native cursors), so they are intentionally left out.
icon_copy_theme() {
  theme=$1
  shift
  [ -d "$SHARE/icons/$theme" ] || return 0
  echo "bundle-windows: copying icon theme $theme"
  mkdir -p "$OUT/share/icons/$theme"
  for part in "$@"; do
    if [ -e "$SHARE/icons/$theme/$part" ]; then
      cp -R "$SHARE/icons/$theme/$part" "$OUT/share/icons/$theme/"
    fi
  done
}
icon_copy_theme Adwaita index.theme symbolic scalable 16x16
icon_copy_theme hicolor index.theme symbolic scalable

if [ -d "$SHARE/locale" ]; then
  echo "bundle-windows: copying translations"
  mkdir -p "$OUT/share/locale"
  for lang in en zh_CN; do
    if [ -d "$SHARE/locale/$lang" ]; then
      cp -R "$SHARE/locale/$lang" "$OUT/share/locale/" 2>/dev/null || true
    fi
  done
fi

# --- gdk-pixbuf loaders ----------------------------------------------------
LOADER_DIR=""
if [ -d "$LIB/gdk-pixbuf-2.0" ]; then
  LOADER_DIR=$(find "$LIB/gdk-pixbuf-2.0" -maxdepth 2 -type d -name loaders | head -n 1 || true)
fi
if [ -n "$LOADER_DIR" ] && [ -d "$LOADER_DIR" ]; then
  echo "bundle-windows: copying gdk-pixbuf loaders"
  version_dir=$(basename "$(dirname "$LOADER_DIR")")
  dest="$OUT/lib/gdk-pixbuf-2.0/$version_dir/loaders"
  mkdir -p "$dest"
  cp "$LOADER_DIR/"*.dll "$dest/" 2>/dev/null || true
  cp "$LOADER_DIR/loaders.cache" "$dest/" 2>/dev/null || true

  # Loader DLLs bring their own dependencies (e.g. libjpeg, libtiff).
  for loader in "$dest/"*.dll; do
    [ -f "$loader" ] || continue
    seen=" "
    copy_deps "$loader"
  done

  # Generate a relocatable loaders.cache when the runtime does not ship one and
  # a Windows gdk-pixbuf-query-loaders helper is available through Wine.
  gdkpixbuf_cache="$dest/loaders.cache"
  query_loaders="$BIN/gdk-pixbuf-query-loaders.exe"
  if [ ! -s "$gdkpixbuf_cache" ] && [ -f "$query_loaders" ] && command -v wine >/dev/null 2>&1; then
    echo "bundle-windows: generating gdk-pixbuf loaders.cache"
    set -- ""
    for loader in "$dest/"*.dll; do
      [ -f "$loader" ] || continue
      win_loader=$(winepath -w "$loader" 2>/dev/null || printf '%s' "$loader")
      set -- "$@" "$win_loader"
    done
    shift
    ( cd "$OUT" && WINEPATH="$BIN" WINEDEBUG=-all wine "$query_loaders" "$@" ) \
      > "$gdkpixbuf_cache" 2>/dev/null || true
    if [ -s "$gdkpixbuf_cache" ]; then
      sed -i "s#\"[^\"]*[/\\\\]lib/gdk-pixbuf-2.0/#\"lib/gdk-pixbuf-2.0/#g" "$gdkpixbuf_cache" 2>/dev/null || true
    fi
  fi
fi

# --- GSettings schema cache ------------------------------------------------
schemas="$OUT/share/glib-2.0/schemas"
compile_schemas="$BIN/glib-compile-schemas.exe"
if [ -f "$compile_schemas" ] && [ -d "$schemas" ] && [ ! -s "$schemas/gschemas.compiled" ] && command -v wine >/dev/null 2>&1; then
  echo "bundle-windows: compiling GSettings schemas"
  win_schemas=$(winepath -w "$schemas" 2>/dev/null || printf '%s' "$schemas")
  ( cd "$OUT" && WINEPATH="$BIN" WINEDEBUG=-all wine "$compile_schemas" --strict "$win_schemas" ) \
    >/dev/null 2>&1 || true
fi

echo "bundle-windows: bundle ready at $OUT"
du -sh "$OUT" 2>/dev/null || true
