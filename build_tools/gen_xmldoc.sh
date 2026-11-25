#!/bin/sh
# gen_xmldoc.sh <module-source.c> <module-name> <output-xml>
#
# Extract the /*** DOCUMENTATION ***/ block from an Asterisk module source
# and wrap it into a standalone XML documentation file that Asterisk loads
# from ${ASTVARLIB}/documentation/thirdparty/ at startup (or on
# "xmldoc reload"). This gives out-of-tree modules the same documentation
# support in "core show function" etc. that in-tree modules get.
set -e

src=$1
module=$2
out=$3

if [ -z "$src" ] || [ -z "$module" ] || [ -z "$out" ]; then
	echo "Usage: $0 <module-source.c> <module-name> <output-xml>" >&2
	exit 1
fi

self_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

awk -f "${self_dir}/get_documentation" "$src" > "$tmp"

if [ ! -s "$tmp" ]; then
	echo "No DOCUMENTATION block found in $src" >&2
	exit 1
fi

{
	cat <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE docs SYSTEM "appdocsxml.dtd">
<?xml-stylesheet type="text/xsl" href="appdocsxml.xslt"?>
<docs xmlns:xi="http://www.w3.org/2001/XInclude">
EOF
	# Tag every documented element with the module name, like the Asterisk
	# build system does for in-tree modules.
	sed -E "s/<(function|application|manager|managerEvent|managerEventInstance|info) /<\\1 module=\"${module}\" /g" "$tmp"
	echo "</docs>"
} > "$out"

echo "Generated $out"
