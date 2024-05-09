// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/assert.h>
#include <inc/memlayout.h>
#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/monitor.h>

#define CMDBUF_SIZE 80  // enough for one VGA text line

struct Command {
  const char *name;
  const char *desc;
  // return -1 to force monitor to exit
  int (*func)(int argc, char **argv, struct Trapframe *tf);
};

static struct Command commands[] = {
    {"help", "Display this list of commands", mon_help},
    {"kerninfo", "Display information about the kernel", mon_kerninfo},
    {"backtrace", "Display a listing of function call frames", mon_backtrace},
};

/***** Implementations of basic kernel monitor commands *****/

int mon_help(int argc, char **argv, struct Trapframe *tf) {
  int i;

  for (i = 0; i < ARRAY_SIZE(commands); i++)
    cprintf("%s - %s\n", commands[i].name, commands[i].desc);
  return 0;
}

int mon_kerninfo(int argc, char **argv, struct Trapframe *tf) {
  extern char _start[], entry[], etext[], edata[], end[];

  cprintf("Special kernel symbols:\n");
  cprintf("  _start                  %08x (phys)\n", _start);
  cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
  cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
  cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
  cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
  cprintf("Kernel executable memory footprint: %dKB\n",
          ROUNDUP(end - entry, 1024) / 1024);
  return 0;
}
//      ...
// ------------
// |    arg5  | 6
// ____________
// |    arg4  | 5
// ____________
// |    arg3  | 4
// ____________
// |    arg2  | 3
// ____________
// |    arg1  | ebp[2]
// ____________
// |    eip   | ebp[1]
// ____________
// |    ebp   | 0
// ____________
//      ...
int mon_backtrace(int argc, char **argv, struct Trapframe *tf) {
  //dohvatiti ebp preko fje read_ebp()
  uint32_t *ebp = (uint32_t *)read_ebp();
  //zasto ovdje ne moze (uint32_t)(ebp+1)  
  //kao sto u labu pise da moze?
  //znamo da je eip jedna kocka iznad ebp u stacku pa se pomjerimo za jedno mjesto
  uint32_t *eip = (uint32_t*)ebp[1]; 
  // dohvatiti argumente
  //argumenti pocinju na trecoj kocki (drugoj ako brojimo od 0)
  struct Eipdebuginfo info;
  uint32_t arg[5];
  for (uint32_t i = 0; i <= 4; i++) {
    arg[i] = ebp[i + 2];
  }
  cprintf("Stack backtrace:\n");
  while (ebp != NULL) {
    cprintf("ebp %08x ", ebp);
    cprintf("eip %08x ", eip);
    //zbog grade skripte moramo zapisati broj sa 8 cifara
    //prazan prostor ispuniti 0
    cprintf("args %08x %08x %08x %08x %08x\n", arg[0], arg[1], arg[2],
            arg[3], arg[4]);
    //sada trebamo pozvati debuginfo_eip
    //on prima pointer na adresu i strukturu Eipdebuginfo
    //eipdebuginfo struktura se nalazi u kdebug.h ima clanove:
	  /* const char *eip_file;		// Source code filename for EIP */
	  /* int eip_line;			// Source code linenumber for EIP */
	  /* const char *eip_fn_name;	// Name of function containing EIP */
	  /* int eip_fn_namelen;		// Length of function name */
	  /* uintptr_t eip_fn_addr;		// Address of start of function */
  	/* int eip_fn_narg;		// Number of function arguments */
    //eip moramo castovati u uintptr_t
    debuginfo_eip((uintptr_t)eip, &info );
    //nakon poziva treba ispisati informacije u formatu:
    //  kern/monitor.c:143: monitor+106
    // %s - string, %u - unsigned broj, %.*s - string varijabilne duzine
    // njegovu duzinu odredjuje eip_fn_namelen 
    // offset racunamo kao eip - eip_fn_adr
    cprintf("\t %s:%u: %.*s+%d\n",info.eip_file,info.eip_line, info.eip_fn_namelen,
        info.eip_fn_name,  ebp[1] - info.eip_fn_addr);

    ebp = (uint32_t *)ebp[0];
    //ponovo dohvatiti eip
    eip = (uint32_t *)ebp[1];
    //ponovo dohvatiti argumente
    for (uint32_t i = 0; i <= 4; i++) {
      arg[i] = ebp[i + 2];
    }
  }
  return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int runcmd(char *buf, struct Trapframe *tf) {
  int argc;
  char *argv[MAXARGS];
  int i;

  // Parse the command buffer into whitespace-separated arguments
  argc = 0;
  argv[argc] = 0;
  while (1) {
    // gobble whitespace
    while (*buf && strchr(WHITESPACE, *buf)) *buf++ = 0;
    if (*buf == 0) break;

    // save and scan past next arg
    if (argc == MAXARGS - 1) {
      cprintf("Too many arguments (max %d)\n", MAXARGS);
      return 0;
    }
    argv[argc++] = buf;
    while (*buf && !strchr(WHITESPACE, *buf)) buf++;
  }
  argv[argc] = 0;

  // Lookup and invoke the command
  if (argc == 0) return 0;
  for (i = 0; i < ARRAY_SIZE(commands); i++) {
    if (strcmp(argv[0], commands[i].name) == 0)
      return commands[i].func(argc, argv, tf);
  }
  cprintf("Unknown command '%s'\n", argv[0]);
  return 0;
}

void monitor(struct Trapframe *tf) {
  char *buf;

	if (tf != NULL)
		print_trapframe(tf);
  cprintf("Welcome to the JOS kernel monitor!\n");
  cprintf("Type 'help' for a list of commands.\n");

  while (1) {
    buf = readline("K> ");
    if (buf != NULL)
      if (runcmd(buf, tf) < 0) break;
  }
}
