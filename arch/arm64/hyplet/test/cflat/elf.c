
#include <stdio.h>
#include <fcntl.h>    /* O_RDONLY */
#include <sys/stat.h> /* For the size of the file. , fstat */
#include <sys/mman.h> /* mmap, MAP_PRIVATE */
#include <string.h>
#include <elf.h>      // Elf64_Shdr
#include <fcntl.h>

void *m_mmap_program;
Elf64_Ehdr *ehdr;
Elf64_Shdr *shdr;
Elf64_Shdr *sh_strtab;

int Elf_parser_load_memory_map(char *prog) 
{
    int shnum = 0;
    int fd, i;
    struct stat st;
    const char *const sh_strtab_p;

    if ((fd = open(prog, O_RDONLY)) < 0) {
        printf("Err: open\n");
        return -1;
    }

    if (fstat(fd, &st) < 0) {
        printf("Err: fstat\n");
        return (-1);
    }

    m_mmap_program = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (m_mmap_program == MAP_FAILED) {
        printf("Err: mmap\n");
        return -1;
    }

    ehdr = (Elf64_Ehdr*)m_mmap_program;
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        printf("Only 64-bit files supported\n");
        return -1;
    }
    ehdr = (Elf64_Ehdr*)m_mmap_program;
    shdr = (Elf64_Shdr*)(m_mmap_program + ehdr->e_shoff);

    return 0;

}

int get_section_addr(char *secname,long *addr, int *size)
{
    int i;
    int shnum = ehdr->e_shnum;
    Elf64_Shdr *sh_strtab = &shdr[ehdr->e_shstrndx];
    char *sh_strtab_p = (char*)m_mmap_program + sh_strtab->sh_offset;
    char *n;

    for (i = 0; i < shnum; ++i) {
        n = sh_strtab_p + shdr[i].sh_name;
	if (!strcmp(n,secname)) {
		*size = shdr[i].sh_size;
		*addr =	shdr[i].sh_addr;
		return 1;
        }
    }
    return 0;
}
/*
int main(int argc, char *argv[])
{
	long addr;
	int size;

	Elf_parser_load_memory_map(argv[0]); 
	get_section_addr(".data", &addr, &size);
	printf("addr %x size %d\n",addr,size);
}

*/
