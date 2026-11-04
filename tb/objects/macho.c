// Mach-O 64-bit relocatable object writer (x86_64 + arm64 hosts)
//
// NOTE(macos-fix): this was an unfinished upstream stub (header + empty
// sections, no symbols, no code, no relocations). Completed using the ELF
// writer (elf64.c) as the structural template:
//   - full LC_SEGMENT_64 with one section per TB module section
//   - LC_SYMTAB with nlist_64 entries (Darwin '_' name prefix)
//   - X86_64_RELOC_BRANCH / X86_64_RELOC_UNSIGNED relocations
#include "macho.h"
#include <log.h>

#define N_UNDF 0x00

#define S_THREAD_LOCAL_REGULAR 0x11

#define X86_64_RELOC_UNSIGNED 0
#define X86_64_RELOC_SIGNED   1
#define X86_64_RELOC_BRANCH   2

typedef struct {
    uint32_t n_strx;
    uint8_t  n_type;
    uint8_t  n_sect;
    uint16_t n_desc;
    uint64_t n_value;
} MO_NList64;

typedef struct {
    int32_t  r_address;
    uint32_t r_symbolnum:24, r_pcrel:1, r_length:2, r_extern:1, r_type:4;
} MO_RelocInfo;

static int macho_put_symbol(TB_Emitter* stab, TB_Emitter* strtbl, const char* name, uint8_t type, uint8_t sect, uint64_t value) {
    // Darwin C symbols are prefixed with an underscore
    size_t strx = strtbl->count;
    if (name != NULL) {
        tb_out1b(strtbl, '_');
        // PATCH(odin-tilde): was tb_outstr_nul_UNSAFE — the string table is
        // only pre-reserved to 1024 bytes, and long mangled names (Odin's
        // runtime has hundreds) overflowed it, silently corrupting the
        // object file (heap-buffer-overflow, nondeterministic traps and
        // "invalid r_symbolnum=0" linker errors). tb_outs reserves first.
        tb_outs(strtbl, strlen(name) + 1, name);
    } else {
        tb_out1b(strtbl, 0);
    }

    MO_NList64 sym = {
        .n_strx = (uint32_t) strx,
        .n_type = type,
        .n_sect = sect,
        .n_desc = 0,
        .n_value = value,
    };
    tb_outs(stab, sizeof(sym), (uint8_t*) &sym);
    return (stab->count / sizeof(MO_NList64)) - 1;
}

// emits all function/global symbols of the given linkage class; ids are
// offset by `base` since the final file concatenates the local and global
// symbol tables into one nlist array
static void macho_put_section_symbols(DynArray(TB_ModuleSection) sections, TB_Emitter* strtbl, TB_Emitter* stab, TB_Linkage linkage, int base) {
    dyn_array_for(i, sections) {
        int sec_num = sections[i].section_num;

        dyn_array_for(j, sections[i].funcs) {
            TB_FunctionOutput* out_f = sections[i].funcs[j];
            TB_Function* f = out_f->parent;
            if (f->super.linkage != linkage) continue;

            uint8_t type = N_SECT | (linkage == TB_LINKAGE_PUBLIC ? N_EXT : 0);
            f->super.symbol_id = base + macho_put_symbol(stab, strtbl, f->super.name, type, sec_num, out_f->code_pos);
        }

        dyn_array_for(j, sections[i].globals) {
            TB_Global* g = sections[i].globals[j];
            if (g->super.linkage != linkage) continue;

            const char* name = g->super.name;
            char buf[16];
            if (name == NULL) {
                snprintf(buf, sizeof(buf), "$anon_%d_%td", sec_num, j);
                name = buf;
            }

            uint8_t type = N_SECT | (linkage == TB_LINKAGE_PUBLIC ? N_EXT : 0);
            g->super.symbol_id = base + macho_put_symbol(stab, strtbl, name, type, sec_num, g->pos);
        }
    }
}

#define WRITE(data, size) (memcpy(&output[write_pos], data, size), write_pos += (size))
TB_ExportBuffer tb_macho_write_output(TB_Module* m, TB_Arena* dst_arena, const IDebugFormat* dbg) {
    ExportList exports;
    CUIK_TIMED_BLOCK("layout section") {
        exports = tb_module_layout_sections(m);
    }

    const ICodeGen* restrict code_gen = tb_codegen_info(m);
    DynArray(TB_ModuleSection) sections = m->sections;
    size_t nsections = dyn_array_length(sections);

    TB_ASSERT(dbg == NULL); // debug info not supported yet

    ////////////////////////////////
    // layout file offsets
    ////////////////////////////////
    size_t load_cmds_size = sizeof(MO_SegmentCmd64) + nsections * sizeof(MO_Section64) + sizeof(MO_SymtabCmd);
    size_t output_size = align_up(sizeof(MO_Header64) + load_cmds_size, 16);

    dyn_array_for(i, sections) {
        sections[i].section_num = 1 + i;
        sections[i].raw_data_pos = output_size;
        output_size += sections[i].total_size;
        output_size = align_up(output_size, 16);
    }

    // relocation arrays (8 bytes per Mach-O relocation_info)
    output_size = tb__layout_relocations(m, sections, code_gen, output_size, sizeof(MO_RelocInfo));

    ////////////////////////////////
    // generate symbol table
    ////////////////////////////////
    TB_Emitter strtbl = { 0 }, local_symtab = { 0 }, global_symtab = { 0 };
    tb_out_reserve(&strtbl, 1024);
    tb_out1b(&strtbl, 0); // string table starts with null

    macho_put_section_symbols(sections, &strtbl, &local_symtab, TB_LINKAGE_PRIVATE, 0);
    uint32_t nlocals = local_symtab.count / sizeof(MO_NList64);
    macho_put_section_symbols(sections, &strtbl, &global_symtab, TB_LINKAGE_PUBLIC, nlocals);

    // NOTE: macho_put_symbol returns the index *within* global_symtab, which
    // already accounts for the public symbols emitted above, so the base for
    // externs is just nlocals (not nlocals+npubs, that double-counts and
    // shifts every extern relocation one entry out of place).
    FOR_N(i, 0, exports.count) {
        TB_External* ext = exports.data[i];
        ext->super.symbol_id = nlocals + macho_put_symbol(&global_symtab, &strtbl, ext->super.name, N_UNDF | N_EXT, NO_SECT, 0);
    }

    uint32_t symtab_offset = output_size;
    output_size += local_symtab.count + global_symtab.count;
    uint32_t strtab_offset = output_size;
    output_size += strtbl.count;

    ////////////////////////////////
    // headers
    ////////////////////////////////
    MO_Header64 header = {
        .magic      = MH_MAGIC_64,
        .filetype   = MH_OBJECT,
        .ncmds      = 2,
        .sizeofcmds = load_cmds_size,
        .flags      = 0x2000, // MH_SUBSECTIONS_VIA_SYMBOLS
    };

    switch (m->target_arch) {
        case TB_ARCH_X86_64:  header.cputype = CPU_TYPE_X86_64;  header.cpusubtype = 3; break;
        case TB_ARCH_AARCH64: header.cputype = CPU_TYPE_AARCH64; header.cpusubtype = 0; break;
        default: tb_todo();
    }

    MO_SegmentCmd64 segment_cmd = {
        .header = {
            .cmd     = LC_SEGMENT_64,
            .cmdsize = sizeof(MO_SegmentCmd64) + sizeof(MO_Section64) * nsections,
        },
        // empty segname: section-level segments decide placement
        .nsects   = nsections,
        .fileoff  = nsections ? sections[0].raw_data_pos : 0,
        .filesize = nsections ? (sections[nsections - 1].raw_data_pos + sections[nsections - 1].total_size - sections[0].raw_data_pos) : 0,
    };

    MO_SymtabCmd symtab_cmd = {
        .header = {
            .cmd     = LC_SYMTAB,
            .cmdsize = sizeof(MO_SymtabCmd),
        },
        .symoff  = symtab_offset,
        .nsyms   = (local_symtab.count + global_symtab.count) / sizeof(MO_NList64),
        .stroff  = strtab_offset,
        .strsize = strtbl.count,
    };

    ////////////////////////////////
    // write output
    ////////////////////////////////
    size_t write_pos = 0;
    TB_ExportChunk* chunk = tb_export_make_chunk(dst_arena, output_size);
    uint8_t* restrict output = chunk->data;

    WRITE(&header, sizeof(header));
    WRITE(&segment_cmd, sizeof(segment_cmd));

    // section headers
    dyn_array_for(i, sections) {
        TB_ModuleSection* sec = &sections[i];
        MO_Section64 s = {
            .size   = sec->total_size,
            .offset = sec->raw_data_pos,
            .align  = 4, // 16-byte
            .reloff = sec->reloc_count ? sec->reloc_pos : 0,
            .nreloc = sec->reloc_count,
        };

        if (sec->flags & TB_MODULE_SECTION_EXEC) {
            memcpy(s.sectname, "__text", 7);
            memcpy(s.segname, "__TEXT", 7);
            s.flags = S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS;
        } else if (sec->flags & TB_MODULE_SECTION_TLS) {
            memcpy(s.sectname, "__thread_data", 14);
            memcpy(s.segname, "__DATA", 7);
            s.flags = S_THREAD_LOCAL_REGULAR;
        } else if (sec->flags & TB_MODULE_SECTION_WRITE) {
            memcpy(s.sectname, "__data", 7);
            memcpy(s.segname, "__DATA", 7);
        } else if (sec->reloc_count) {
            // Read-only data containing pointer relocations must NOT live in
            // __TEXT: ld64 counts those as (illegal) text relocations. The
            // standard home for relocated const data is __DATA,__const.
            memcpy(s.sectname, "__const", 8);
            memcpy(s.segname, "__DATA", 7);
        } else {
            memcpy(s.sectname, "__const", 8);
            memcpy(s.segname, "__TEXT", 7);
        }

        WRITE(&s, sizeof(s));
    }

    WRITE(&symtab_cmd, sizeof(symtab_cmd));

    // pad to section data start
    if (nsections) {
        while (write_pos < sections[0].raw_data_pos) output[write_pos++] = 0;
    }

    // section contents
    dyn_array_for(i, sections) {
        write_pos = tb_helper_write_section(m, write_pos, &sections[i], output, sections[i].raw_data_pos);
        // pad to next section's start
        size_t next = align_up(write_pos, 16);
        while (write_pos < next) output[write_pos++] = 0;
    }

    // relocation arrays
    dyn_array_for(i, sections) if (sections[i].reloc_count > 0) {
        TB_ASSERT(sections[i].reloc_pos == write_pos);
        MO_RelocInfo* relocs = (MO_RelocInfo*) &output[write_pos];

        dyn_array_for(j, sections[i].funcs) {
            TB_FunctionOutput* func_out = sections[i].funcs[j];
            size_t source_offset = func_out->code_pos;

            for (TB_SymbolPatch* p = func_out->first_patch; p; p = p->next) {
                if (p->internal) continue;

                uint32_t type;
                bool pcrel;
                uint32_t length;
                if (m->target_arch == TB_ARCH_AARCH64) {
                    // odin-tilde: arm64 Mach-O relocation types
                    // (ARM64_RELOC_UNSIGNED=0, BRANCH26=2, PAGE21=3, PAGEOFF12=4)
                    switch (p->type) {
                        case TB_OBJECT_RELOC_BRANCH26: type = 2; pcrel = true;  length = 2; break;
                        case TB_OBJECT_RELOC_REL21:    type = 3; pcrel = true;  length = 2; break;
                        case TB_OBJECT_RELOC_PAGEOFF12:type = 4; pcrel = false; length = 2; break;
                        case TB_OBJECT_RELOC_GOT_PAGE21:    type = 5; pcrel = true;  length = 2; break;
                        case TB_OBJECT_RELOC_GOT_PAGEOFF12: type = 6; pcrel = false; length = 2; break;
                        case TB_OBJECT_RELOC_ADDR64:   type = 0; pcrel = false; length = 3; break;
                        default: tb_todo();
                    }
                } else {
                    switch (p->type) {
                        case TB_OBJECT_RELOC_REL32:
                        case TB_OBJECT_RELOC_PLT32: type = X86_64_RELOC_BRANCH;   pcrel = true;  length = 2; break;
                        case TB_OBJECT_RELOC_ADDR64: type = X86_64_RELOC_UNSIGNED; pcrel = false; length = 3; break;
                        case TB_OBJECT_RELOC_ADDR32: type = X86_64_RELOC_SIGNED;   pcrel = false; length = 2; break;
                        default: tb_todo();
                    }
                }

                *relocs++ = (MO_RelocInfo){
                    .r_address   = source_offset + p->pos,
                    .r_symbolnum = p->target->symbol_id,
                    .r_pcrel     = pcrel,
                    .r_length    = length,
                    .r_extern    = 1,
                    .r_type      = type,
                };
            }
        }

        dyn_array_for(j, sections[i].globals) {
            TB_Global* g = sections[i].globals[j];

            FOR_N(k, 0, g->obj_count) {
                if (g->objects[k].type == TB_INIT_OBJ_RELOC) {
                    const TB_Symbol* s = g->objects[k].reloc;
                    *relocs++ = (MO_RelocInfo){
                        .r_address   = g->pos + g->objects[k].offset,
                        .r_symbolnum = s->symbol_id,
                        .r_pcrel     = 0,
                        .r_length    = 3,
                        .r_extern    = 1,
                        .r_type      = X86_64_RELOC_UNSIGNED,
                    };
                }
            }
        }

        write_pos += sections[i].reloc_count * sizeof(MO_RelocInfo);
    }

    // symbol table + string table
    TB_ASSERT(write_pos == symtab_offset);
    WRITE(local_symtab.data, local_symtab.count);
    WRITE(global_symtab.data, global_symtab.count);

    TB_ASSERT(write_pos == strtab_offset);
    WRITE(strtbl.data, strtbl.count);

    TB_ASSERT(write_pos == output_size);

    cuik_free(strtbl.data);
    cuik_free(local_symtab.data);
    cuik_free(global_symtab.data);
    return (TB_ExportBuffer){ .total = output_size, .head = chunk, .tail = chunk };
}
