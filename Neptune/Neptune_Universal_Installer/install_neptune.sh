#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "=============================================="
echo "      Neptune C-- Universal Installer"
echo "=============================================="

mkdir -p "$HOME/.local/bin"

# Interpreter
if [ -f "$ROOT/neptune.cpp" ] && command -v g++ >/dev/null 2>&1; then
  echo "[Neptune] Compiling interpreter..."
  g++ -std=c++17 -Wall -Wextra -O2 "$ROOT/neptune.cpp" -o "$HOME/.local/bin/neptune" &&
  chmod +x "$HOME/.local/bin/neptune"
else
  echo "[Neptune] neptune.cpp/g++ not found; syntax installation continues."
fi

if [ -f "$HOME/.bashrc" ] && ! grep -qF 'export PATH="$HOME/.local/bin:$PATH"' "$HOME/.bashrc"; then
  printf '\nexport PATH="$HOME/.local/bin:$PATH"\n' >> "$HOME/.bashrc"
fi

# GtkSourceView: one v2 language definition is intentionally compatible with
# GtkSourceView 2 through 5. Install into every detected user search path.
echo "[GtkSourceView] Installing Neptune language..."
installed_gsv=0
for d in \
  "$HOME/.local/share/gtksourceview-2/language-specs" \
  "$HOME/.local/share/gtksourceview-2.0/language-specs" \
  "$HOME/.local/share/gtksourceview-3/language-specs" \
  "$HOME/.local/share/gtksourceview-3.0/language-specs" \
  "$HOME/.local/share/gtksourceview-4/language-specs" \
  "$HOME/.local/share/gtksourceview-5/language-specs" \
  "$HOME/.local/share/gtksourceview-6/language-specs"
do
  parent="$(dirname "$d")"
  if [ -d "$parent" ] || [ -d "$d" ]; then
    mkdir -p "$d"
    cp "$ROOT/gtksourceview/neptune.lang" "$d/neptune.lang"
    installed_gsv=$((installed_gsv+1))
  fi
done

# If an editor is present but its data directory wasn't created yet, detect its
# linked GtkSourceView library and create the corresponding path.
if command -v xed >/dev/null 2>&1; then
  so="$(ldd "$(command -v xed)" 2>/dev/null | sed -n 's/.*libgtksourceview-\([0-9][^.] *\)\.so.*/\1/p' | head -n1)"
  if [ -n "$so" ]; then
    d="$HOME/.local/share/gtksourceview-$so/language-specs"
    mkdir -p "$d"
    cp "$ROOT/gtksourceview/neptune.lang" "$d/neptune.lang"
    installed_gsv=$((installed_gsv+1))
  fi
fi

echo "[Geany] Installing custom filetype..."
GEANY="$HOME/.config/geany"
mkdir -p "$GEANY/filedefs"
install -m 644 "$ROOT/geany/filetypes.Neptune.conf" "$GEANY/filedefs/filetypes.Neptune.conf"

# Safely update/create filetype_extensions.conf using Python.
python3 - "$GEANY/filetype_extensions.conf" <<'PY'
import sys, os, re
p=sys.argv[1]
if os.path.exists(p):
    text=open(p, encoding="utf-8").read()
else:
    text="[Extensions]\n"
if "[Extensions]" not in text:
    text="[Extensions]\n"+text
lines=text.splitlines()
out=[]
found=False
for line in lines:
    if re.match(r"^Neptune\s*=", line):
        if not found:
            out.append("Neptune=*.nep;")
            found=True
    else:
        out.append(line)
if not found:
    try:
        idx=out.index("[Extensions]")+1
    except ValueError:
        out.insert(0,"[Extensions]")
        idx=1
    out.insert(idx,"Neptune=*.nep;")
open(p,"w",encoding="utf-8").write("\n".join(out)+"\n")
PY

# VS Code
if command -v code >/dev/null 2>&1; then
  echo "[VS Code] Installing extension..."
  TMP="$HOME/.local/share/neptune-vscode"
  mkdir -p "$TMP/syntaxes"
  cp "$ROOT/vscode/package.json" "$TMP/"
  cp "$ROOT/vscode/language-configuration.json" "$TMP/"
  cp "$ROOT/vscode/syntaxes/neptune.tmLanguage.json" "$TMP/syntaxes/"
  code --install-extension "$TMP" --force >/dev/null 2>&1 || \
    echo "[VS Code] CLI installation failed; files remain in $TMP."
else
  echo "[VS Code] not installed; package is included for manual installation."
fi

# Kate
if command -v kate >/dev/null 2>&1 || [ -d "$HOME/.local/share/katepart5" ]; then
  echo "[Kate] Installing syntax..."
  mkdir -p "$HOME/.local/share/katepart5/syntax"
  cp "$ROOT/kate/neptune.xml" "$HOME/.local/share/katepart5/syntax/"
fi
mkdir -p "$HOME/.local/share/org.kde.syntax-highlighting/syntax"
cp "$ROOT/kate/neptune.xml" "$HOME/.local/share/org.kde.syntax-highlighting/syntax/"

# Sublime
if command -v subl >/dev/null 2>&1 || [ -d "$HOME/.config/sublime-text" ]; then
  mkdir -p "$HOME/.config/sublime-text/Packages/User"
  cp "$ROOT/sublime/Neptune.sublime-syntax" "$HOME/.config/sublime-text/Packages/User/"
fi

# Vim
if command -v vim >/dev/null 2>&1; then
  mkdir -p "$HOME/.vim/syntax" "$HOME/.vim/ftdetect"
  cp "$ROOT/vim/neptune.vim" "$HOME/.vim/syntax/neptune.vim"
  printf '%s\n' 'au BufRead,BufNewFile *.nep set filetype=neptune' > "$HOME/.vim/ftdetect/neptune.vim"
fi

# Neovim
if command -v nvim >/dev/null 2>&1; then
  mkdir -p "$HOME/.config/nvim/syntax" "$HOME/.config/nvim/ftdetect"
  cp "$ROOT/neovim/neptune.vim" "$HOME/.config/nvim/syntax/neptune.vim"
  printf '%s\n' 'au BufRead,BufNewFile *.nep set filetype=neptune' > "$HOME/.config/nvim/ftdetect/neptune.vim"
fi

# Emacs
if command -v emacs >/dev/null 2>&1; then
  mkdir -p "$HOME/.emacs.d"
  cp "$ROOT/emacs/neptune-mode.el" "$HOME/.emacs.d/"
fi

# Micro
if command -v micro >/dev/null 2>&1; then
  mkdir -p "$HOME/.config/micro/syntax"
  cp "$ROOT/micro/neptune.yaml" "$HOME/.config/micro/syntax/"
fi

# Notepad++ is a Windows application. On Linux, do not pretend it can be
# installed globally; leave the valid UDL ready for Windows/Winetricks/manual import.
mkdir -p "$HOME/.local/share/neptune-editor-support"
cp "$ROOT/notepadpp/Neptune.xml" "$HOME/.local/share/neptune-editor-support/"
cp "$ROOT/codeblocks/README.txt" "$HOME/.local/share/neptune-editor-support/"

echo
echo "=============================================="
echo "Installation finished."
echo "GtkSourceView definitions installed: $installed_gsv"
echo "=============================================="
echo
echo "Close and reopen your editors."
echo "For Geany, also use: Tools -> Reload Configuration"
echo "For an already-open .nep file, close it and reopen it so Geany detects Neptune."
echo
echo "If you want to test the interpreter:"
echo "  source ~/.bashrc"
echo "  neptune --c"
