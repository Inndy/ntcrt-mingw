# The members of the libraries of the toolchain that the library for tcc takes (make TCC=1, see the Makefile).
# Inputs: "have", the names that libntcrt.a gives; "seeds", the names to give; then the output of nm -A on the
# members, in the order of preference. Prints the members that define the names wanted and those they use, except a
# member that defines a name of "have". A name no member gives is left to the link (ntdll.def), as a GCC build links
# libmingwex before ntdll.

FILENAME == have \
{
    Have[$1] = 1
    next
}

FILENAME == seeds \
{
    Need[++n] = $1
    next
}

{
    member = substr($1, 1, index($1, ":") - 1)
    type = $(NF - 1)
    name = $NF
    if (type == "U")
    {
        Uses[member] = Uses[member] " " name
    } else if (type ~ /^[A-Z]$/)
    {
        if (!(name in Definer))
        {
            Definer[name] = member
        }
        if (name in Have)
        {
            Clash[member] = 1
        }
    }
}

END \
{
    for (i = 1; i <= n; i++)
    {
        name = Need[i]
        if (name in Have || name in Done)
        {
            continue
        }
        Done[name] = 1
        member = Definer[name]
        if (member == "" || member in Clash || member in Taken)
        {
            continue
        }
        Taken[member] = 1
        print member
        count = split(Uses[member], used, " ")
        for (k = 1; k <= count; k++)
        {
            Need[++n] = used[k]
        }
    }
}
