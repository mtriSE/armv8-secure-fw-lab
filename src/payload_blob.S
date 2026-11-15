/* Embed the EL1 payload image (built separately, linked at NS RAM) into the
 * firmware. It sits in flash with .rodata; EL3 copies it to RAM at boot. */
    .section .rodata.payload, "a"
    .balign 16
    .global payload_blob_start, payload_blob_end
payload_blob_start:
    .incbin "build/payload.bin"
payload_blob_end:
