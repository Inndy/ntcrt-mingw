# Input lines "[_ONLY] Name Impl" from NTCRT_IMPORT / NTCRT_IMPORT_ONLY, see NtCRT.h.
# Writes one assembly file per name to "dir": a jump "Name" to "Impl" (unless _ONLY) and the pointer "__imp_Name".
# Files are numbered because names may differ in case only. The sections are COMDAT, lld drops those of the
# names a program does not use (--gc-sections).
{
    name = $(NF - 1)
    impl = $NF
    file = sprintf("%s/e%04d.s", dir, NR)
    if (NF == 2)
    {
        printf "\t.section\t.text$%s,\"xr\"\n\t.linkonce\tdiscard\n\t.globl\t%s\n\t.def\t%s;\t.scl\t2;\t.type\t32;\t.endef\n%s:\n\tjmp\t%s\n", name, name, name, name, impl > file
    }
    printf "\t.section\t.rdata$__imp_%s,\"dr\"\n\t.linkonce\tdiscard\n\t.globl\t__imp_%s\n\t.p2align\t3\n__imp_%s:\n\t.quad\t%s\n", name, name, name, impl > file
    close(file)
}
