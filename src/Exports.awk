# Input lines "[_ONLY] Name Impl" from NTCRT_IMPORT / NTCRT_IMPORT_ONLY, see NtCRT.h.
# Keeps one assembly file per name in "dir": a jump "Name" to "Impl" (unless _ONLY) and the pointer "__imp_Name".
# A file is written only when its text changes, the files to assemble (written or without an object) are printed,
# and the names of all files to keep are written to "keep". The sections are COMDAT, lld drops those of the names a
# program does not use (--gc-sections).

# The file of a name: "e-", then the name with "_" as "__" and an uppercase letter as "_" and the letter in lowercase.
# ar of Windows ignores case: names that differ in case only ("_exit", "_Exit") have different members, and no stub
# replaces an object of the runtime ("printf" and Printf.o).
function file_name(name,    i, c, r)
{
    r = "e-"
    for (i = 1; i <= length(name); i++)
    {
        c = substr(name, i, 1)
        if (c == "_")
        {
            r = r "__"
        } else if (c ~ /[A-Z]/)
        {
            r = r "_" tolower(c)
        } else
        {
            r = r c
        }
    }
    return r
}

{
    name = $(NF - 1)
    impl = $NF
    base = file_name(name)
    file = dir "/" base ".s"
    text = ""
    if (NF == 2)
    {
        text = sprintf("\t.section\t.text$%s,\"xr\"\n\t.linkonce\tdiscard\n\t.globl\t%s\n\t.def\t%s;\t.scl\t2;\t.type\t32;\t.endef\n%s:\n\tjmp\t%s\n", name, name, name, name, impl)
    }
    text = text sprintf("\t.section\t.rdata$__imp_%s,\"dr\"\n\t.linkonce\tdiscard\n\t.globl\t__imp_%s\n\t.p2align\t3\n__imp_%s:\n\t.quad\t%s\n", name, name, name, impl)

    old = ""
    while ((getline line < file) > 0)
    {
        old = old line "\n"
    }
    close(file)
    object = dir "/" base ".o"
    if (old != text)
    {
        # The object is emptied first: if make stops before it is assembled, the next make finds it missing
        printf "" > object
        close(object)
        printf "%s", text > file
        close(file)
        print base ".s"
    } else if ((getline line < object) <= 0)
    {
        print base ".s"
    }
    close(object)
    print base ".s" > keep
    print base ".o" > keep
}
