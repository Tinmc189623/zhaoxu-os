#!/bin/sh
# Link the combined OS_5P image from the FreeBSD personality objects and the
# Yuanshuo core objects.  A global that both kernels define is renamed in a
# mirror copy of one side's objects, so the two stop sharing a symbol instead
# of relying on --allow-multiple-definition.  Renaming the core side touches
# ~80 objects and leaves every FreeBSD object exactly as its build produced it.
#
# Runs inside the FreeBSD 15.1 build VM, where the objects already exist:
#
#   sh freebsd-link.sh --script kernel.ld [--rename-side core|bsd]
#                      [--objects /tmp/fs_objs.txt]
#                      [--freebsd-prefix DIR] [--core-prefix DIR]
#                      [--out /tmp/os5p.elf] [--mirror /tmp/fs_renamed]
#                      [--keep-multiple]
set -eu

OBJECTS=/tmp/fs_objs.txt
BSD_PREFIX=/usr/obj/usr/src/amd64.amd64/sys/ZHAOXU
CORE_PREFIX=/tmp/kobjs
SCRIPT=
OUT=/tmp/os5p.elf
MIRROR=/tmp/fs_renamed
KEEP_MULTIPLE=0
SIDE=core
NM=${NM:-llvm-nm}
OBJCOPY=${OBJCOPY:-llvm-objcopy}
LD=${LD:-ld.lld}

while [ $# -gt 0 ]; do
    case "$1" in
        --objects) OBJECTS=$2; shift 2 ;;
        --freebsd-prefix) BSD_PREFIX=$2; shift 2 ;;
        --core-prefix) CORE_PREFIX=$2; shift 2 ;;
        --script) SCRIPT=$2; shift 2 ;;
        --out) OUT=$2; shift 2 ;;
        --mirror) MIRROR=$2; shift 2 ;;
        --rename-side) SIDE=$2; shift 2 ;;
        --keep-multiple) KEEP_MULTIPLE=1; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "freebsd-link: unknown option $1" >&2; exit 2 ;;
    esac
done

case $SIDE in
    core) SIDE_PREFIX=$CORE_PREFIX; SIDE_TAG=yuanshuo ;;
    bsd) SIDE_PREFIX=$BSD_PREFIX; SIDE_TAG=bsd ;;
    *) echo "freebsd-link: --rename-side must be core or bsd" >&2; exit 2 ;;
esac

[ -f "$OBJECTS" ] || { echo "freebsd-link: no object list $OBJECTS" >&2; exit 1; }
[ -n "$SCRIPT" ] || { echo "freebsd-link: --script is required" >&2; exit 1; }
[ -f "$SCRIPT" ] || { echo "freebsd-link: no linker script $SCRIPT" >&2; exit 1; }

rm -rf "$MIRROR"
mkdir -p "$MIRROR"

# One nm pass over the whole set.  With -A each line names its object; the
# type field sits in $3 for definitions and $2 for undefined references.
list=$(cat "$OBJECTS")
$NM -A -g $list > "$MIRROR/symbols.txt"

# Defined names per side.  U/w/v are undefined (or undefined weak) and do not
# count as a definition for collision purposes.
for side in bsd core; do
    case $side in
        bsd) prefix=$BSD_PREFIX ;;
        core) prefix=$CORE_PREFIX ;;
    esac
    awk -v p="$prefix" '
        index($1, p) == 1 {
            if ($2 == "U" || $2 == "w" || $2 == "v") next
            print $NF
        }' "$MIRROR/symbols.txt" | sort -u > "$MIRROR/$side.names"
done

comm -12 "$MIRROR/bsd.names" "$MIRROR/core.names" > "$MIRROR/collisions.txt"
count=$(wc -l < "$MIRROR/collisions.txt" | tr -d ' ')
echo "freebsd-link: $count collision(s)"

# A name the renamed side defines twice cannot be fixed by renaming; report it
# before the link so the failure is obvious.
if [ "$count" -gt 0 ]; then
    : > "$MIRROR/redefine.txt"
    while read -r name; do
        awk -v p="$SIDE_PREFIX" -v n="$name" '
            index($1, p) == 1 && $NF == n {
                if ($2 == "U" || $2 == "w" || $2 == "v") next
                print $1
            }' "$MIRROR/symbols.txt" | sort -u > "$MIRROR/defs.txt"
        defs=$(wc -l < "$MIRROR/defs.txt" | tr -d ' ')
        if [ "$defs" -ne 1 ]; then
            echo "freebsd-link: warning: $name is defined $defs times"
        fi
        echo "$name ${SIDE_TAG}_$name" >> "$MIRROR/redefine.txt"
    done < "$MIRROR/collisions.txt"

    # Mirror only the renamed side's objects; the other side stays untouched.
    : > "$MIRROR/objects.txt"
    renamed=0
    for object in $list; do
        case $object in
            "$SIDE_PREFIX"*)
                target="$MIRROR/$(echo "$object" | tr '/' '_')"
                $OBJCOPY --redefine-syms="$MIRROR/redefine.txt" "$object" "$target"
                echo "$target" >> "$MIRROR/objects.txt"
                renamed=$((renamed + 1))
                ;;
            *) echo "$object" >> "$MIRROR/objects.txt" ;;
        esac
    done
    echo "freebsd-link: renamed $renamed $SIDE_TAG object(s)"
else
    cp "$OBJECTS" "$MIRROR/objects.txt"
fi

link_list=$(cat "$MIRROR/objects.txt")
# separate-loadable-segments: Limine refuses an image where segments of
# different permissions share a page, and FreeBSD's IFUNC sections land right
# after .text.
set +e
if [ "$KEEP_MULTIPLE" -eq 1 ]; then
    $LD -T "$SCRIPT" -nostdlib -static -z max-page-size=0x1000 \
        -z separate-loadable-segments --allow-multiple-definition \
        -o "$OUT" $link_list > "$MIRROR/link.log" 2>&1
else
    $LD -T "$SCRIPT" -nostdlib -static -z max-page-size=0x1000 \
        -z separate-loadable-segments -o "$OUT" $link_list > "$MIRROR/link.log" 2>&1
fi
rc=$?
set -e
echo "LINK=$rc"
tail -5 "$MIRROR/link.log"
if [ "$rc" -eq 0 ]; then
    ls -l "$OUT"
fi
exit "$rc"
