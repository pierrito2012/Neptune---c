#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LANGFILE="$ROOT/gtksourceview/neptune.lang"

printf '%s\n' '=============================================='
printf '%s\n' '       Neptune Universal Installer'
printf '%s\n' '=============================================='
printf '%s\n' '[Neptune] Installing interpreter and editor syntax...'

# ------------------------------------------------------------
# Neptune interpreter
# ------------------------------------------------------------
mkdir -p "$HOME/.local/bin"
if [ -f "$ROOT/neptune.cpp" ] && command -v g++ >/dev/null 2>&1; then
  echo "[Neptune] Compiling interpreter..."
  if g++ -std=c++17 -Wall -Wextra -O2 "$ROOT/neptune.cpp" -o "$HOME/.local/bin/neptune"; then
    chmod +x "$HOME/.local/bin/neptune"
    echo "[Neptune] Interpreter installed at ~/.local/bin/neptune"
  else
    echo "[Neptune] Interpreter compilation failed. Syntax installation continues."
  fi
else
  echo "[Neptune] neptune.cpp or g++ not found. Syntax installation continues."
fi

if [ -f "$HOME/.bashrc" ] && ! grep -qF 'export PATH="$HOME/.local/bin:$PATH"' "$HOME/.bashrc"; then
  printf '\nexport PATH="$HOME/.local/bin:$PATH"\n' >> "$HOME/.bashrc"
fi

# ------------------------------------------------------------
# GtkSourceView: cover all common recent user paths.
# This is the syntax engine used by Xed/Gedit/Pluma/Mousepad and others.
# ------------------------------------------------------------
echo "[GtkSourceView] Installing Neptune syntax..."
for d in \
  "$HOME/.local/share/gtksourceview-2/language-specs" \
  "$HOME/.local/share/gtksourceview-2.0/language-specs" \
  "$HOME/.local/share/gtksourceview-3/language-specs" \
  "$HOME/.local/share/gtksourceview-3.0/language-specs" \
  "$HOME/.local/share/gtksourceview-4/language-specs" \
  "$HOME/.local/share/gtksourceview-5/language-specs" \
  "$HOME/.local/share/gtksourceview-6/language-specs"
do
  mkdir -p "$d"
  cp "$LANGFILE" "$d/neptune.lang"
done

# Also install into detected XDG data roots so editors using an alternate
# XDG_DATA_HOME still find the language.
XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
for d in \
  "$XDG_DATA_HOME/gtksourceview-2/language-specs" \
  "$XDG_DATA_HOME/gtksourceview-3/language-specs" \
  "$XDG_DATA_HOME/gtksourceview-3.0/language-specs" \
  "$XDG_DATA_HOME/gtksourceview-4/language-specs" \
  "$XDG_DATA_HOME/gtksourceview-5/language-specs" \
  "$XDG_DATA_HOME/gtksourceview-6/language-specs"
do
  mkdir -p "$d"
  cp "$LANGFILE" "$d/neptune.lang"
done

# ------------------------------------------------------------
# Geany 1.x / 2.x
# ------------------------------------------------------------
echo "[Geany] Installing Neptune filetype..."
GEANY="$HOME/.config/geany"
mkdir -p "$GEANY/filedefs"
cp "$ROOT/geany/filetypes.Neptune.conf" "$GEANY/filedefs/filetypes.Neptune.conf"

python3 - "$GEANY/filetype_extensions.conf" <<'PY'
import os, sys, re
p=sys.argv[1]
text=open(p,encoding='utf-8').read() if os.path.exists(p) else '[Extensions]\n'
lines=text.splitlines()
if '[Extensions]' not in lines:
    lines.insert(0,'[Extensions]')
out=[]
seen=False
for line in lines:
    if re.match(r'^Neptune\s*=', line):
        if not seen:
            out.append('Neptune=*.nep;')
            seen=True
    else:
        out.append(line)
if not seen:
    out.insert(out.index('[Extensions]')+1,'Neptune=*.nep;')
open(p,'w',encoding='utf-8').write('\n'.join(out)+'\n')
PY

# ------------------------------------------------------------
# VS Code / compatible builds
# ------------------------------------------------------------
if command -v code >/dev/null 2>&1; then
  echo "[VS Code] Installing Neptune extension..."
  TMP="$HOME/.local/share/neptune-vscode"
  rm -rf "$TMP"
  mkdir -p "$TMP/syntaxes"
  cp "$ROOT/vscode/package.json" "$TMP/"
  cp "$ROOT/vscode/language-configuration.json" "$TMP/"
  cp "$ROOT/vscode/syntaxes/neptune.tmLanguage.json" "$TMP/syntaxes/"
  code --install-extension "$TMP" --force >/dev/null 2>&1 || true
else
  echo "[VS Code] CLI not found; extension remains in the installer package."
fi

# ------------------------------------------------------------
# Kate / KDE syntax highlighting
# ------------------------------------------------------------
echo "[Kate/KDE] Installing Neptune syntax..."
mkdir -p "$HOME/.local/share/org.kde.syntax-highlighting/syntax"
cp "$ROOT/kate/neptune.xml" "$HOME/.local/share/org.kde.syntax-highlighting/syntax/neptune.xml"
mkdir -p "$HOME/.local/share/katepart5/syntax"
cp "$ROOT/kate/neptune.xml" "$HOME/.local/share/katepart5/syntax/neptune.xml"

# ------------------------------------------------------------
# Other common editors
# ------------------------------------------------------------
if command -v subl >/dev/null 2>&1 || [ -d "$HOME/.config/sublime-text" ]; then
  echo "[Sublime Text] Installing Neptune syntax..."
  mkdir -p "$HOME/.config/sublime-text/Packages/User"
  cp "$ROOT/sublime/Neptune.sublime-syntax" "$HOME/.config/sublime-text/Packages/User/"
fi

if command -v vim >/dev/null 2>&1; then
  echo "[Vim] Installing Neptune syntax..."
  mkdir -p "$HOME/.vim/syntax" "$HOME/.vim/ftdetect"
  cp "$ROOT/vim/neptune.vim" "$HOME/.vim/syntax/neptune.vim"
  printf '%s\n' 'au BufRead,BufNewFile *.nep set filetype=neptune' > "$HOME/.vim/ftdetect/neptune.vim"
fi

if command -v nvim >/dev/null 2>&1; then
  echo "[Neovim] Installing Neptune syntax..."
  mkdir -p "$HOME/.config/nvim/syntax" "$HOME/.config/nvim/ftdetect"
  cp "$ROOT/neovim/neptune.vim" "$HOME/.config/nvim/syntax/neptune.vim"
  printf '%s\n' 'au BufRead,BufNewFile *.nep set filetype=neptune' > "$HOME/.config/nvim/ftdetect/neptune.vim"
fi

if command -v emacs >/dev/null 2>&1; then
  echo "[Emacs] Installing Neptune mode..."
  mkdir -p "$HOME/.emacs.d"
  cp "$ROOT/emacs/neptune-mode.el" "$HOME/.emacs.d/neptune-mode.el"
fi

if command -v micro >/dev/null 2>&1; then
  echo "[Micro] Installing Neptune syntax..."
  mkdir -p "$HOME/.config/micro/syntax"
  cp "$ROOT/micro/neptune.yaml" "$HOME/.config/micro/syntax/neptune.yaml"
fi

# ------------------------------------------------------------
# Portable support files for Windows/Code::Blocks/Notepad++
# ------------------------------------------------------------
echo "[Portable] Saving Code::Blocks and Notepad++ support files..."
mkdir -p "$HOME/.local/share/neptune-editor-support/codeblocks"
cp "$ROOT/codeblocks/README.txt" "$HOME/.local/share/neptune-editor-support/codeblocks/"
cp "$ROOT/notepadpp/Neptune.xml" "$HOME/.local/share/neptune-editor-support/Neptune.xml"

printf '%s\n' ''
printf '%s\n' '=============================================='
printf '%s\n' 'Neptune installation finished.'
printf '%s\n' '=============================================='
printf '%s\n' 'Close and reopen your editors.'
printf '%s\n' 'Geany: Tools -> Reload Configuration, then reopen the .nep file.'
printf '%s\n' 'Xed/Gedit/Pluma/Mousepad: close and reopen the editor.'
printf '%s\n' 'VS Code: reload/restart the window if it was already open.'
printf '%s\n' 'No computer reboot is required.'
