//===------------------------------------------------------------*- C++ -*-===//
//
//                     Created by F8LEFT on 2017/6/28.
//                   Copyright (c) 2017. All rights reserved.
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef FAOATDUMP_EXELF_H
#define FAOATDUMP_EXELF_H

#include "elf.h"

// ===== ELF 类型位宽抽象 =====
// 用 __SO64__ 宏在编译期切换 32/64 位 ELF 类型，使同一份源代码能处理
// 两种位宽的 SO。__SO64__ 由 CMakeLists.txt 通过 -DSO_64=ON 注入。
// 因此一个二进制只能处理对应位宽的 SO —— SoFixer32 处理 32 位，
// SoFixer64 处理 64 位。这也是 SoFixer-skills-CLI 需要双变体的根因。
#ifndef __SO64__
typedef Elf32_Ehdr Elf_Ehdr;
typedef Elf32_Phdr Elf_Phdr;
typedef Elf32_Shdr Elf_Shdr;
typedef Elf32_Sym Elf_Sym;
typedef Elf32_Dyn Elf_Dym;
typedef Elf32_Rel Elf_Rel;
typedef Elf32_Rela Elf_Rela;
typedef Elf32_Addr Elf_Addr;
typedef Elf32_Dyn Elf_Dyn;
typedef Elf32_Word Elf_Word;
#else
typedef Elf64_Ehdr Elf_Ehdr;
typedef Elf64_Phdr Elf_Phdr;
typedef Elf64_Shdr Elf_Shdr;
typedef Elf64_Sym Elf_Sym;
typedef Elf64_Dyn Elf_Dym;
typedef Elf64_Rel Elf_Rel;
typedef Elf64_Rela Elf_Rela;
typedef Elf64_Addr Elf_Addr;
typedef Elf64_Dyn Elf_Dyn;
typedef Elf64_Word Elf_Word;
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE 0x1000

#define PAGE_MASK (~(PAGE_SIZE-1))
// Returns the address of the page containing address 'x'.
#define PAGE_START(x)  ((x) & PAGE_MASK)

// Returns the offset of address 'x' in its page.
#define PAGE_OFFSET(x) ((x) & ~PAGE_MASK)

// Returns the address of the next page after address 'x', unless 'x' is
// itself at the start of a page.
#define PAGE_END(x)    PAGE_START((x) + (PAGE_SIZE-1))
#endif

// TEMP_FAILURE_RETRY：被 EINTR 中断的系统调用自动重试。
// GNU/clang 用语句表达式 __extension__({ ... }) 实现；MSVC 不支持该扩展。
// Windows 的 fread 不会因 EINTR 失败，故 MSVC 路径下直接退化为原表达式，
// 仅保持可编译性，行为等价（不做重试，因为无需重试）。
#ifndef TEMP_FAILURE_RETRY
#  ifdef _MSC_VER
#    define TEMP_FAILURE_RETRY(expression) (expression)
#  else
#    define TEMP_FAILURE_RETRY(expression) \
  (__extension__\
   ({ long int __result;\
       do __result = (long int)(expression);\
       while(__result == -1L&& errno == EINTR);\
       __result;}))
#  endif
#endif


#endif //FAOATDUMP_EXELF_H
