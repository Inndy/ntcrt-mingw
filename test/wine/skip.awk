# usage: awk -v test=dll/name -f skip.awk skip.txt source.c
#
# Leaves out the test functions of skip.txt (lines "dll/name function reason") from a Wine test: the definition
# goes (blank lines keep the line numbers), a call becomes a skip with the reason. The tests of UCRT-only APIs
# would not compile or link.
NR == FNR {
    if ($1 == test) {
        reason = $0
        sub(/^[^ ]+ +[^ ]+ */, "", reason)
        Skip[$2] = reason
    }
    next
}
Deleting {
    if ($0 ~ /^}/) Deleting = 0
    print ""
    next
}
{
    for (f in Skip) {
        if ($0 ~ ("^static .*[ *]" f " *\\(") && $0 !~ /; *$/) {
            Deleting = $0 !~ /^}/
            print "/* " f ": " Skip[f] " */"
            next
        }
        if ($0 ~ ("^[ \t]+" f "\\(.*\\); *$")) {
            print "    skip(\"" f ": " Skip[f] "\\n\");"
            next
        }
    }
    print
}
