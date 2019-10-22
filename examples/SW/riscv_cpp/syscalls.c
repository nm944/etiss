#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#define __riscv__
#include "utils.h"
#include "machine/syscall.h"

#define ETISS_LOGGER_ADDR (void*)0x80000000



// BRK
extern char _heap_start;
extern char _heap_end;
static char *brk = &_heap_start;

// This is not the correct implementation according to linux, because:
// - It returns the current brk value instead of 0 on success.
// - It returns the current brk value if the given addr is 0.
// It is expected like this from: https://github.com/riscv/riscv-newlib/blob/riscv-newlib-3.0.0/libgloss/riscv/sys_sbrk.c
static unsigned int _brk(void *addr)
{
  /* If __heap_size == 0, we can't allocate memory on the heap */
  if(&_heap_start == &_heap_end) {
    return -1;
  }
  if(addr == 0) {
    return (unsigned int)brk;
  }
  if((char *)addr < &_heap_start) {
    return -1;
  }
  /* Don't move the break past the end of the heap */
  if((char*)addr < &_heap_end) {
    brk = addr;
  } else {
    brk = &_heap_end;
  }
  return (unsigned int)brk;
}


// FSTAT
int _fstat(int file, struct stat *st)
{
  if(file == 1 || file == 2) {
    st->st_mode = S_IFCHR;
    st->st_blksize = 0;
    return 0;
  }

  errno = -ENOSYS;
  return -1;
}


// WRITE
ssize_t _write(int file, const void *ptr, size_t len)
{
  if (file != 1 && file != 2) {
    errno = ENOSYS;
    return -1;
  }

  const char *bptr = ptr;
  for (size_t i = 0; i < len; ++i) {
    *(volatile char *)(ETISS_LOGGER_ADDR) = bptr[i];
  }
  return 0;
}


// EXIT
void _exit_(int exit_status)
{
  asm("ebreak");
  while (1);
}


// CLOSE
int _close(int file)
{
  if (file == 1 || file == 2) {
    return 0;
  }
  errno = ENOSYS;
  return -1;
}


// GETTIMEOFDAY
int _gettimeofday(struct timeval *tp, void *tzp)
{
  // Not implemented by ETISS?
  unsigned int cycle = 0;
  csrr(cycle, cycle);
  unsigned long long timebase = 100000000;
  tp->tv_sec = cycle / timebase;
  tp->tv_usec = cycle % timebase * 1000000 / timebase;
  return 0;
}



// Overrides weak definition from pulpino sys_lib.
int default_exception_handler_c(unsigned int a0, unsigned int a1, unsigned int a2, unsigned int a3, unsigned int a4, unsigned int a5, unsigned int a6, unsigned int a7)
{
  unsigned int mcause = 0;
  csrr(mcause, mcause);
  unsigned int mepc = 0;
  csrr(mepc, mepc);

  int ecall_result;

  switch (mcause)
  {
  case 0xb: // Machine ECALL
    switch (a7)
    {
    case SYS_brk:
      ecall_result = (unsigned int)_brk( (void *)a0);
      break;
    case SYS_fstat:
      ecall_result = _fstat(a0, (struct stat *)a1);
      break;
    case SYS_write:
      ecall_result = _write(a0, (void *)a1, a2);
      break;
    case SYS_exit:
      _exit_(a0);
      break;
    case SYS_close:
      ecall_result = _close(a0);
      break;
    case SYS_gettimeofday:
      ecall_result = _gettimeofday((struct timeval *)a0, (void *)a1);
      break;
    default:
      // Unhandled syscall!
      while (1);
    }
    // Advance to instruction after ECALL.
    csrw(mepc, mepc + 4);
    break;
  case 0x2: // Illegal instruction
    // The instruction is located at mepc.
    while (1);
  default:
    // Unhandled cause code!
    while (1);
  }

  return ecall_result;
}

// Required by iostream
// https://lists.debian.org/debian-gcc/2003/07/msg00057.html
void *__dso_handle = (void*)&__dso_handle;
