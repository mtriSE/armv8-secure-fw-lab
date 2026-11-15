# A1 check: reset entry, EL3, stack, BSS clear, jump to main.
set pagination off
set confirm off
target remote :1234
# "*main" = first instruction of main, before its prologue pushes a frame.
# Plain "main" stops after the prologue, where sp is already __stack_top - 16.
hbreak *main
printf "A1: at reset pc=0x%lx (_start=0x%lx) CPSR=0x%lx\n", $pc, (unsigned long)&_start, $cpsr
if ($pc != (unsigned long)&_start)
  printf "A1: FAIL pc != _start\n"
  kill
  quit 1
end
if (($cpsr & 0xc) != 0xc)
  printf "A1: FAIL not EL3\n"
  kill
  quit 1
end
# Dirty BSS so we can prove start.S clears it.
set {unsigned long}&bss_probe = 0xdeadbeef
if (bss_probe != 0xdeadbeef)
  printf "A1: FAIL cannot write secure RAM from GDB\n"
  kill
  quit 1
end
continue
printf "A1: reached main, sp=0x%lx\n", $sp
if (bss_probe != 0)
  printf "A1: FAIL bss not cleared\n"
  kill
  quit 1
end
if ($sp != (unsigned long)&__stack_top)
  printf "A1: FAIL sp mismatch\n"
  kill
  quit 1
end
if (data_probe != 0x1234)
  printf "A1: FAIL .data not copied\n"
  kill
  quit 1
end
printf "A1: PASS\n"
kill
quit 0
