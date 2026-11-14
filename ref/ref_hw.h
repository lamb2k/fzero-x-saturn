/* Hardware reads in racer.c, rerouted by the Makefile's rewrite (card G1.2). */
#ifndef REF_HW_H
#define REF_HW_H
unsigned int ref_rom_read32(unsigned int offset);
unsigned int ref_io_read(unsigned int addr);
void ref_io_write(unsigned int addr, unsigned int value);
#define REF_ROM_READ(addr) ref_rom_read32((unsigned int) (addr))
#define REF_IO_READ(addr) ref_io_read((unsigned int) (addr))
#define REF_IO_WRITE(addr, data) ref_io_write((unsigned int) (addr), (unsigned int) (data))
#endif
