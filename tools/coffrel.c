/*
 * coffrel <object>...: the ELF objects that objcopy -O elf64-x86-64 made of COFF objects of x64, made right for tcc
 * (make TCC=1). objcopy keeps the addend of a COFF relocation in place and writes 0 in the ELF one: a 64-bit address
 * (IMAGE_REL_AMD64_ADDR64, R_X86_64_64) is right so, but REL32 is relative to the end of its 32 bits, which
 * R_X86_64_PC32 takes in its addend (-4). Any other type is an error, the objects are written only without one.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What of <elf.h> is needed, which MinGW-w64 does not have */
typedef uint64_t Elf64_Addr, Elf64_Off, Elf64_Xword;
typedef int64_t Elf64_Sxword;

typedef struct
{
    unsigned char e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    Elf64_Addr e_entry;
    Elf64_Off e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct
{
    uint32_t sh_name, sh_type;
    Elf64_Xword sh_flags;
    Elf64_Addr sh_addr;
    Elf64_Off sh_offset;
    Elf64_Xword sh_size;
    uint32_t sh_link, sh_info;
    Elf64_Xword sh_addralign, sh_entsize;
} Elf64_Shdr;

typedef struct
{
    Elf64_Addr r_offset;
    Elf64_Xword r_info;
    Elf64_Sxword r_addend;
} Elf64_Rela;

#define ELFMAG          "\177ELF"
#define SELFMAG         4
#define EI_CLASS        4
#define ELFCLASS64      2
#define ET_REL          1
#define EM_X86_64       62
#define SHT_RELA        4
#define SHT_REL         9
#define R_X86_64_64     1
#define R_X86_64_PC32   2
#define ELF64_R_TYPE(i) ((uint32_t)(i))

static int Convert(const char* Path)
{
    FILE* File = fopen(Path, "r+b");
    unsigned char* Data;
    long Size;
    Elf64_Ehdr Header;

    if (File == NULL || fseek(File, 0, SEEK_END) != 0 || (Size = ftell(File)) < (long)sizeof(Header) ||
        fseek(File, 0, SEEK_SET) != 0 || (Data = malloc(Size)) == NULL || fread(Data, 1, Size, File) != (size_t)Size)
    {
        fprintf(stderr, "coffrel: %s: cannot read\n", Path);
        return 0;
    }
    memcpy(&Header, Data, sizeof(Header));
    if (memcmp(Header.e_ident, ELFMAG, SELFMAG) != 0 || Header.e_ident[EI_CLASS] != ELFCLASS64 ||
        Header.e_type != ET_REL || Header.e_machine != EM_X86_64 || Header.e_shentsize != sizeof(Elf64_Shdr) ||
        Header.e_shoff > (Elf64_Off)Size || Header.e_shnum > (Size - Header.e_shoff) / sizeof(Elf64_Shdr))
    {
        fprintf(stderr, "coffrel: %s: not a relocatable ELF object of x64\n", Path);
        return 0;
    }
    for (unsigned i = 0; i < Header.e_shnum; i++)
    {
        Elf64_Shdr Section;

        memcpy(&Section, Data + Header.e_shoff + i * sizeof(Section), sizeof(Section));
        if (Section.sh_type == SHT_REL ||
            (Section.sh_type == SHT_RELA && (Section.sh_offset > (Elf64_Off)Size ||
                                             Section.sh_size > Size - Section.sh_offset)))
        {
            fprintf(stderr, "coffrel: %s: unexpected relocation section %u\n", Path, i);
            return 0;
        }
        if (Section.sh_type != SHT_RELA)
        {
            continue;
        }
        for (Elf64_Xword Offset = 0; Offset + sizeof(Elf64_Rela) <= Section.sh_size; Offset += sizeof(Elf64_Rela))
        {
            Elf64_Rela Relocation;

            memcpy(&Relocation, Data + Section.sh_offset + Offset, sizeof(Relocation));
            if (ELF64_R_TYPE(Relocation.r_info) == R_X86_64_PC32 && Relocation.r_addend == 0)
            {
                Relocation.r_addend = -4;
                memcpy(Data + Section.sh_offset + Offset, &Relocation, sizeof(Relocation));
            } else if (ELF64_R_TYPE(Relocation.r_info) != R_X86_64_64 || Relocation.r_addend != 0)
            {
                fprintf(stderr,
                        "coffrel: %s: relocation of type %u, addend %lld\n",
                        Path,
                        (unsigned)ELF64_R_TYPE(Relocation.r_info),
                        (long long)Relocation.r_addend);
                return 0;
            }
        }
    }
    if (fseek(File, 0, SEEK_SET) != 0 || fwrite(Data, 1, Size, File) != (size_t)Size || fclose(File) != 0)
    {
        fprintf(stderr, "coffrel: %s: cannot write\n", Path);
        return 0;
    }
    free(Data);
    return 1;
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (!Convert(argv[i]))
        {
            return 1;
        }
    }
    return 0;
}
