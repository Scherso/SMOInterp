OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(__module_start)

PHDRS
{
  text PT_LOAD FLAGS(5);
  rodata PT_LOAD FLAGS(4);
  data PT_LOAD FLAGS(6);
  dynamic PT_DYNAMIC;
}

SECTIONS
{
  PROVIDE_HIDDEN(__start__ = 0x0);
  . = __start__;
  __code_start__ = .;

  .text : {
    HIDDEN(__text_start__ = .);
    KEEP (*(.text.crt0))
    *(.text .text.*)
    HIDDEN(__text_end__ = .);
  } :text

  .plt : { *(.plt .plt.*) } :text
  __code_end__ = .;

  . = ALIGN(0x1000);

  .module_name : { KEEP (*(.nx-module-name)) } :rodata

  . = ALIGN(8);

  .rodata : {
    *(.rodata .rodata.*)
  } :rodata

  .hash     : { *(.hash) } :rodata
  .gnu.hash : { *(.gnu.hash) } :rodata
  .dynsym   : { *(.dynsym .dynsym.*) } :rodata
  .dynstr   : { *(.dynstr .dynstr.*) } :rodata

  __rel_dyn_start__ = .;
  .rel.dyn : { *(.rel.dyn) } :rodata
  __rel_dyn_end__ = .;

  __rela_dyn_start__ = .;
  .rela.dyn : { *(.rela.dyn) } :rodata
  __rela_dyn_end__ = .;

  __rel_plt_start__ = .;
  .rel.plt        : { *(.rel.plt) } :rodata
  __rel_plt_end__ = .;

  __rela_plt_start__ = .;
  .rela.plt       : { *(.rela.plt) } :rodata
  __rela_plt_end__ = .;

  .gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) } :rodata
  .eh_frame_hdr : {
    HIDDEN(__eh_frame_hdr_start__ = .);
    *(.eh_frame_hdr)
    HIDDEN(__eh_frame_hdr_end__ = .);
  } :rodata
  .eh_frame : { KEEP (*(.eh_frame)) } :rodata

  .note.gnu.build-id : { *(.note.gnu.build-id) } :rodata

  . = ALIGN(0x1000);

  .data : {
    *(.data .data.*)
  } :data

  .data.rela.ro : {
    *(.data.rela.ro.local*)
    *(.data.rela.ro .data.rela.ro.*)
  } :data

  .data.rel.ro : {
    *(.data.rel.ro.local*)
    *(.data.rel.ro .data.rel.ro.*)
  } :data

  __got_start__ = .;
  .got : { *(.got.plt) *(.igot.plt) *(.got) *(.igot) } :data
  __got_end__ = .;

  .dynamic : {
    HIDDEN(__dynamic_start__ = .);
    *(.dynamic)
  } :data :dynamic

  . = ALIGN(8);

  .preinit_array ALIGN(8) :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } :data

  .init_array : {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP (*(.init_array))
    PROVIDE_HIDDEN (__init_array_end  = .);
  } :data

  .fini_array : {
    PROVIDE_HIDDEN (__fini_array_start__ = .);
    KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP (*(.fini_array))
    PROVIDE_HIDDEN (__fini_array_end__ = .);
  } :data


  .tdata : {
    __tdata_align_abs__ = ABSOLUTE(.);
    __tdata_start__ = .;
    *(.tdata .tdata.*)
    __tdata_end__ = .;
  } :data

  .tbss : {
    __tbss_align_abs__ = ABSOLUTE(.);
    __tbss_start__ = .;
    *(.tbss .tbss.*)
    *(.tcommon)
    __tbss_end__ = .;
  } :data

  . = ALIGN(0x1000);

  .bss : {
    HIDDEN(__bss_start__ = .);
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(8);
    HIDDEN(__bss_end__ = .);
    . = ALIGN(0x1000);
  } :data

  __end__ = ABSOLUTE(.);

  HIDDEN(__argdata__ = .);

  /DISCARD/ : {
    *(.interp)
  }
}