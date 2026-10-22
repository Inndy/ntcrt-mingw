# The names of the functions of src/MathX87.cpp for the Makefile, of the architecture ("x86" set for x86, the lines
# under "#ifdef __i386__" for x86, under its "#else" for x64): "wrap" unset, those of NTCRT_X87_ROUND and
# NTCRT_X87_ROUND53 (members of libmsvcrt); "wrap" set, those of NTCRT_X87_ROUND_WRAP, NTCRT_X87_ROUND53_WRAP and
# NTCRT_X87_DECLARE_WRAP (members of libmingwex).

/^#ifdef __i386__/ \
{
    Arch = "x86"
    next
}

/^#el/ && Arch == "x86" \
{
    Arch = "x64"
    next
}

/^#endif/ \
{
    Arch = ""
    next
}

Arch == (x86 ? "x86" : "x64") && match($0, /^NTCRT_X87_(ROUND(53)?(_WRAP)?|DECLARE_WRAP)\(/) \
{
    if ((substr($0, RSTART, RLENGTH) ~ /_WRAP/) == (wrap != ""))
    {
        name = substr($0, RLENGTH + 1)
        sub(/,.*/, "", name)
        print name
    }
}
