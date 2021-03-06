#include <stdio.h>
#include "ezee.h"

unsigned long pad_p_to_n(long p, long n) {
    long i = 0;
    while(i < p) {
        i += n;
    }
    return i;
}

unsigned long to_RVA(_PE target, _PTR raw) {
    unsigned long file_offset = (unsigned long long)raw - (unsigned long long)target->file;
    
    // Get distance from beginning of section
    PIMAGE_SECTION_HEADER first_section = target->s_h;
    unsigned long section_offset  = file_offset - first_section->PointerToRawData;
    
    // Convert to RVA
    return section_offset + first_section->VirtualAddress;
}

unsigned long size_of_injected(_PE lib_ez) {
    long dll_size = lib_ez->raw_size;
    long stub_size = ep_stub(EP_GET_SIZE);
    long new_iat_size = iat_size();
    return dll_size + stub_size + new_iat_size;
}

boolean crush_sections(_PE target, _PE lib_ez) {
    long section_raw = 0;
    long section_virt = 0;
    int section_flags = 0xE40000E0;
    long ez_size = size_of_injected(lib_ez);
    PIMAGE_SECTION_HEADER first_section = target->s_h;
    PIMAGE_SECTION_HEADER final_section = target->s_h;
    final_section = (void*)final_section + ((target->nt_h->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER)) - sizeof(IMAGE_SECTION_HEADER));
    
    if(IsBadReadPtr(final_section, sizeof(IMAGE_SECTION_HEADER)) || ez_size == 0) {
        return FALSE;
    }
    
    target->inject_ptr_raw = final_section->SizeOfRawData + final_section->PointerToRawData;
    
    section_raw = pad_p_to_n(target->inject_ptr_raw + ez_size, 0x200);
    section_raw -= first_section->PointerToRawData;
    
    section_virt = final_section->VirtualAddress + final_section->Misc.VirtualSize;
    section_virt += ez_size;
    section_virt -= first_section->VirtualAddress;
    
    PIMAGE_SECTION_HEADER destroy_section = target->s_h;
    for(int i = 1; i < target->nt_h->FileHeader.NumberOfSections; i++) {
        destroy_section = (void*)first_section + (i * sizeof(IMAGE_SECTION_HEADER));
        memset(destroy_section, 'Z', sizeof(IMAGE_SECTION_HEADER));
    }
    
    // Build crushed section
    target->nt_h->FileHeader.NumberOfSections = 1;
    
    first_section->Characteristics = section_flags;
    first_section->Misc.VirtualSize = section_virt;
    memcpy(first_section->Name, ".ezee\0\0\0", 8);
    first_section->SizeOfRawData = section_raw;
}

// This inserts the extra space we need to inject our loader stub and new import directory
void create_stub_space(_PE target) {
    PIMAGE_SECTION_HEADER ez_section = target->s_h;
    
    // NOTE: File size discrepancy means excess data is stored after image.
    // This is not supported by the packer, we assume its junk debug data or something -- lop it off.
    target->raw_size = ez_section->PointerToRawData + ez_section->SizeOfRawData;
    target->file = realloc(target->file, target->raw_size);
    reload_in_mem(target);
}

void inject_stub(_PE target, _PE lib_ez) {
    _PTR write_head = target->inject_ptr_raw + target->file;
    
    // Fixup entrypoint
    target->nt_h->OptionalHeader.AddressOfEntryPoint = to_RVA(target, write_head);
    
    // Copy loader stub
    unsigned long ep_size = ep_stub(EP_GET_SIZE);
    memcpy(write_head, (_PTR)ep_stub(EP_GET_PTR), ep_size);
    write_head += ep_size;

    // Build and copy new IAT
    unsigned long import_directory_rva = to_RVA(target, write_head);
    memset(write_head, '\0', iat_size());
    build_import_directory(write_head, import_directory_rva);
    write_head += iat_size();
    
    // Copy lib-ez DLL
    memcpy(write_head, lib_ez->file , lib_ez->raw_size);
    write_head += lib_ez->raw_size;
    
    // Redirect import directory and IAT entries
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = (sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2);
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = import_directory_rva;
}

void pe_fixup(_PE target) {
    // Correct Size of Image
    target->nt_h->OptionalHeader.SizeOfImage = target->s_h->VirtualAddress + target->s_h->Misc.VirtualSize;
    
    // SUPER WALL OF UNIMPLEMENTED SHAME :D
    // No support yet
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress = 0;
    
    // Resources are destroyed until runtime - selective resource protection still needs to be implemented
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress = 0;
    
    // Don't know what these do yet
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].VirtualAddress = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].VirtualAddress = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_ARCHITECTURE].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_ARCHITECTURE].VirtualAddress = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_GLOBALPTR].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_GLOBALPTR].VirtualAddress = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].VirtualAddress = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress = 0;
    
    // Destroy pointer to debug info
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size = 0;
    target->nt_h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress = 0;
}