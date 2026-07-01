//===------------------------------------------------------------*- C++ -*-===//
//
//                     Created by F8LEFT on 2017/6/3.
//                   Copyright (c) 2017. All rights reserved.
//===----------------------------------------------------------------------===//
//  Parse and read elf file.
//===----------------------------------------------------------------------===//

#ifndef SOFIXER_ELFREADER_H
#define SOFIXER_ELFREADER_H

#include "macros.h"
#include "FileReader.h"

#include <cstdint>
#include <cstddef>
#include <memory.h>

class ElfRebuilder;
class ObElfReader;



class ElfReader {
public:
    ElfReader();
    virtual ~ElfReader();

    virtual bool Load();
    bool setSource(const char* source);

    size_t phdr_count() { return phdr_num_; }
    uint8_t * load_start() { return load_start_; }
    Elf_Addr load_size() { return load_size_; }
    uint8_t * load_bias() { return load_bias_; }
    const Elf_Phdr* loaded_phdr() { return loaded_phdr_; }

    const Elf_Ehdr* record_ehdr() { return &header_; }

protected:
    bool ReadElfHeader();
    bool VerifyElfHeader();
    bool ReadProgramHeader();
    bool ReserveAddressSpace(uint32_t padding_size = 0);
    bool LoadSegments();
    bool FindPhdr();
    bool CheckPhdr(uint8_t *);
    // If I have change anything in phtr_table_, just apply the chagnes into loaded_phdr.
    void ApplyPhdrTable();

    virtual void GetDynamicSection(Elf_Dyn** dynamic, size_t* dynamic_count, Elf_Word* dynamic_flags);

    const char* name_;
    FileReader* source_ = nullptr;

    Elf_Ehdr header_;
    size_t phdr_num_;

    void* phdr_mmap_;
    Elf_Phdr* phdr_table_;
    Elf_Addr phdr_size_;

    // First page of reserved address space.
    uint8_t * load_start_;
    // Size in bytes of reserved address space.
    Elf_Addr load_size_;
    Elf_Addr pad_size_;
    size_t file_size;
    // Load bias.
    uint8_t * load_bias_;

    // Loaded phdr.
    const Elf_Phdr* loaded_phdr_;


private:

    friend class ElfRebuilder;
    friend class ObElfReader;

};



// 计算所有 PT_LOAD 段覆盖的虚拟地址范围大小（页对齐）。
// min_vaddr/max_vaddr 若非空，分别回填最小/最大虚拟地址（已页对齐）。
// 无可加载段时返回 0。这是 ElfReader::ReserveAddressSpace 用来预留地址空间的依据。
size_t
phdr_table_get_load_size(const Elf_Phdr* phdr_table,
                         size_t phdr_count,
                         Elf_Addr* min_vaddr = NULL,
                         Elf_Addr* max_vaddr = NULL);

int
phdr_table_protect_segments(const Elf_Phdr* phdr_table,
                            int               phdr_count,
                            uint8_t * load_bias);

int
phdr_table_unprotect_segments(const Elf_Phdr* phdr_table,
                              int               phdr_count,
                              uint8_t * load_bias);

int
phdr_table_protect_gnu_relro(const Elf_Phdr* phdr_table,
                             int               phdr_count,
                             uint8_t *load_bias);


int phdr_table_get_arm_exidx(const Elf_Phdr* phdr_table,
                         int               phdr_count,
                         uint8_t * load_bias,
                         Elf_Addr**      arm_exidx,
                         unsigned*         arm_exidix_count);

// 在已加载的镜像中定位 PT_DYNAMIC 段，回填其内存地址、条目数与权限标志。
// dynamic 指向内存里 dynamic 段的起始（load_bias + p_vaddr）；
// dynamic_count = p_memsz / sizeof(Elf_Dyn)。无 PT_DYNAMIC 时 *dynamic=NULL。
// SoFixer-skills-CLI 的 info/verify 子命令直接复用此函数读取动态段。
void
phdr_table_get_dynamic_section(const Elf_Phdr* phdr_table,
                               int               phdr_count,
                               uint8_t * load_bias,
                               Elf_Dyn**       dynamic,
                               size_t*           dynamic_count,
                               Elf_Word*       dynamic_flags);


#endif //SOFIXER_ELFREADER_H
