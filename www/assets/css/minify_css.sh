#!/bin/bash
#
# Minimize a CSS file.
#
# Usage: ./minify_css.sh <input.css> [output.css]
#   If output is omitted, <input>.min.css is used (style.css -> style.min.css).
#
# Example: ./minify_css.sh www/assets/css/style.css www/assets/css/style.min.css

set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: $0 <input.css> [output.css]" >&2
    exit 1
fi

in="$1"
out="${2:-${in%.css}.min.css}"

if [[ ! -f "$in" ]]; then
    echo "Error: input file '$in' not found" >&2
    exit 1
fi

if [[ "$in" -ef "$out" ]]; then
    echo "Error: input and output must be different files" >&2
    exit 1
fi

# -z treats the whole file as a single record so multi-line comments can be matched.
#  1. strip /* ... */ comments
#  2. turn newlines/tabs/CRs into spaces and collapse runs of whitespace
#  3. remove spaces around { } ; , > and after ':'
#  4. remove the last ';' before '}' and any empty rules
#  5. trim leading/trailing whitespace
sed -z -E '
    s#/\*[^*]*\*+([^/*][^*]*\*+)*/##g
    s/[\r\n\t]+/ /g
    s/ {2,}/ /g
    s/ *([{};,>]) */\1/g
    s/: +/:/g
    s/;+\}/}/g
    s/[^{}]+\{\}//g
    s/^ +//
    s/ +$//
' "$in" > "$out"

in_size=$(wc -c < "$in")
out_size=$(wc -c < "$out")
echo "Minified $in ($in_size bytes) -> $out ($out_size bytes)"
