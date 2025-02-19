#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm/frame.h"
#include "vm/swap.h"
#include "vm/page.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"

static thread_func start_process NO_RETURN;
static bool load (char *cmdline, void (**eip) (void), void **esp);

/* Start of Project 2 */
bool load_child_exec (char *file_name);
struct file *fp = NULL;
/* End of Project 2 */

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy, *save_ptr;
  char command[16];
  tid_t tid;
  struct dir *dir;
  //struct file *file;
  //struct Elf32_Ehdr ehdr;

  if(strnlen(file_name, (PGSIZE + 1))*sizeof(char)>PGSIZE)
    user_exit(-1);
  
  
  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Start of Project 2 */

  /* Tokenizing the file_name of the file from the given command */
  strlcpy (command, file_name, 16);
  save_ptr = strtok_r (command, " ", &save_ptr);
 
  /* Opening the root directory */
  dir = dir_open_root ();

  /* Validating whether the file can be loaded in the memory. */
  //if (!lookup (dir, command, NULL, NULL) || !load_child_exec (command))
  if (!load_child_exec (command))
  {
    palloc_free_page (fn_copy);
    return TID_ERROR;
  }

  /* After validating that this file can be loaded in the memory, we add this
   * file to the list of all the open files and increment it's exec_cnt by 1
   * as this file will be now being executed */
  add_new_file (command, EXEC);

  /* End of Project 2 */

  tid = thread_create (command, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
  {
    //check_remove_file (command, EXEC);
    palloc_free_page (fn_copy);
  }
  
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Start of Project 3 */
#ifdef VM
  hash_init (&(thread_current ()->sup_page_table), sup_hash_func, sup_less_func, NULL);
#endif
  /* End of Project 3 */

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  /* Start of Project 2 */
  struct child_process *child = get_wait_child (thread_current (), child_tid);

  if (child == NULL)
    return -1;
  
  if (child->child_status != THREAD_DYING)
  {
    lock_acquire (&(thread_current ()->lock_tid));
    cond_wait (&(child->child_cond), &(thread_current ()->lock_tid));
    lock_release (&(thread_current ()->lock_tid));
  }

  int return_status = child->status;
  list_remove (&child->elem);
  free (child);
  return return_status;
  /* End of Project 2 */
}

/* Free the current process's resources. */
void
process_exit (void)
{
  /* Start of Project 3 */
  enum intr_level old_level;
  //old_level = intr_disable ();
  /* End of Project 3 */

  /* Start of Project 2 */

  struct thread *cur = thread_current ();
  struct thread *parent;
  uint32_t *pd;
  struct list_elem *e;
  struct child_process *cur_process, *child;
  struct fd_name *open_fd;
  struct mmap_file *mmap;

  /* Notify child process that parent process is dying.
   * This will help in handling orphan processes. */

  /* Start of Project 3 */
#ifdef VM
  //hash_destroy (&cur->sup_page_table, sup_page_destroy);
  //hash_apply (&cur->sup_page_table, sup_page_destroy);
  memory_unmap_files (cur);
  file_close (cur->exec_file);
#endif
  /* End of Project 3 */

  e = list_begin (&(cur->child_processes));
  while (e != list_end (&(cur->child_processes)))
  {
    child = list_entry (e, struct child_process, elem);
    e = list_next (e);
    if (child->pid != 2)
       child->process->orphan = true;

    list_remove (&child->elem);
    free (child);
  }

  if (!cur->orphan)
  {
    parent = cur->parent;
    cur_process = get_wait_child (parent, cur->tid);
    if (cur_process != NULL)
    {
      cur_process->child_status = THREAD_DYING;
      lock_acquire (&(parent->lock_tid));
      cond_signal (&(cur_process->child_cond), &(parent->lock_tid));
      lock_release (&(parent->lock_tid));
    }
  }


  /* Close all open files of current process. */
  e = list_begin (&(cur->open_files));
  while (e != list_end (&(cur->open_files)))
  {
    open_fd = list_entry (e, struct fd_name, elem);
    check_remove_file (open_fd->file_name, OPEN);
    file_close(open_fd->file);
    e = list_next (e);
    list_remove (&open_fd->elem);
    free (open_fd);
  }

  check_remove_file (cur->name, EXEC);

  /* End of Project 2 */

  /* Start of Project 3 */
//#ifdef VM
  //write_back_mmap ();
  //mmap_write_back ();
  //hash_destroy (&thread_current ()->sup_page_table, sup_page_destroy);

  /* Close all memory mapped files of current process. *
  e = list_begin (&(cur->mmap_files));
  while (e != list_end (&(cur->mmap_files)))
  {
    mmap = list_entry (e, struct mmap_file, elem);
    file_close(mmap->file);
    e = list_next (e);
    list_remove (&mmap->elem);
    free (mmap);
  } */
//#endif
  /* End of Project 3 */

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
  //intr_set_level (old_level);			/* Project 3 */
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, char *file_name);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  /* Start of Project 2 */
  char *fn_copy, *save_ptr;

  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  file_name = strtok_r (file_name, " ", &save_ptr);
  /* End of Project 2 */

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  t->exec_file = filesys_open (file_name);		/* Project 3 */
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
	      /* Start of Project 3 */
	      /* Create entries in Supplemental Page Table for executable file. */
	      load_sup_page_table (t->exec_file, file_page, mem_page,
	      			   read_bytes, zero_bytes, writable);

              /* if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done; */
	      /* End of Project 3 */
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, fn_copy))		/* Project 2 */
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  palloc_free_page (fn_copy);			/* Project 2 */
  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */


/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  //ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  //ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);

  /* Start of Project 3 */
  // while (read_bytes > 0 || zero_bytes > 0) 
  if (read_bytes >= 0 || zero_bytes > 0) 
  /* End of Project 3 */
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      /* Start of Project 3 */
      uint8_t *kpage = palloc_get_frame (PAL_USER);
      /* End of Project 3 */
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */

      /* Start of Project 3 */
      enum page_type pgtype = writable ? DATA : CODE;

      if (!install_frame (upage, kpage, writable, file, ofs, pgtype, page_zero_bytes, IN_MEMORY))
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* End of Project 3 */
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, char *file_name) 
{
  uint8_t *kpage;
  bool success = false;
  
  /* Start of Project 2 */
  
  char *argv_address[64];
  uint32_t count = 0;
  char *token, *command, *save_ptr;
  size_t len = 0;
  int i = 0;
  int *base_esp;
  struct sup_page *spte;
  
  /* End of Project 2 */
  
  /* Start of Project 3 */
  kpage = palloc_get_frame (PAL_USER | PAL_ZERO);
  /* End of Project 3 */

  if (kpage != NULL) 
    {
      /* Start of Project 3 */
      uint8_t * upage = ((uint8_t *) PHYS_BASE) - PGSIZE;
      success = install_frame (upage, kpage, true,
			       NULL, 0, DATA, PGSIZE, IN_MEMORY);
      struct thread *t = thread_current ();
      spte = sup_page_lookup (t, upage);
      spte->pinned = true;
      t->stack_seg.start_addr = upage;
      t->stack_seg.end_addr = upage;
      /* End of Project 3 */
      if (success)
      {
        *esp = PHYS_BASE;

	/* Start of Project 2 */
	command = file_name;
        for (token = strtok_r (command, " ", &save_ptr); token != NULL;
             token = strtok_r (NULL, " ", &save_ptr))
        {
	  len = strlen (token) + 1;
	  *esp = *esp - len;
	  memcpy (*esp, token, len);
	  argv_address[count++] = *esp;
        }

	// Insert Sentinel 0 on stack.
	int zero = 0;
	*esp = *esp - 0x4;
	memcpy (*esp, &zero, sizeof(zero));

	/* Insert addresses of argv parameters on the stack in reverse order
	 * of argv_address array. */
	 for (i = count; i >= 0; i--)
	 {
	   // Push onto stack argv_address[i].
	   *esp = *esp - 0x4;
	   memcpy (*esp, &(argv_address[i]), sizeof (argv_address[i]));
	 }

	 // Push argv on the stack.
	 base_esp = *esp;
	 *esp = *esp - 0x4;
	 memcpy (*esp, &base_esp, 4);

	 *esp = *esp - 0x4;
	 memcpy (*esp, &count, sizeof (count));

        // Insert fake return address.
        *esp = *esp - 0x4;
        memcpy (*esp, &zero, sizeof(zero));

	/* End of Project 2 */
      }
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* Start of Project 2 */

/* Used to exit the user program. Prints the return status of the 
 * process. */
void 
user_exit (int status)
{
  struct thread *cur = running_thread ();
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit();
}

/* Validates whether the given file is executable or no. 
 * Called from proccess_execute(). It checks the ELF Header to see 
 * whether the given file is executable. */
bool
load_child_exec (char *file_name)
{
  struct file *file;
  struct Elf32_Ehdr ehdr;
  bool success = false;
  struct open_file *open_fp;

  open_fp = get_file_open (file_name);

  if (open_fp != NULL && open_fp->exec_cnt > 0)
    success = true;
  else
  {
    /* Open executable file. */
    file = filesys_open (file_name);
    if (file == NULL)
      success = false;
    else
    {
      /* Read and verify executable header. */
       if( ehdr.e_ident[0] != ELFMAG0 || ehdr.e_ident[1] != ELFMAG1 ||
           ehdr.e_ident[2] != ELFMAG2 || ehdr.e_ident[3] != ELFMAG3 )
           success = true;
       
       if (ehdr.e_type != ET_EXEC && ehdr.e_type != ET_DYN) success = true;
       if (ehdr.e_machine != EM_386) success = true;
       if (ehdr.e_version != EV_CURRENT) success = true;

 
      file_close (file);
    }
  }
  return success;
}

/* End of Project 2 */


/* Start of Project 3 */
void
memory_unmap_files (struct thread *t)
{
  struct list_elem *e;
  struct mmap_file *mmap;

  /* Close all memory mapped files of current process. */
  while (!list_empty (&(t->mmap_files)))
  {
    e = list_begin (&(t->mmap_files));
    mmap = list_entry (e, struct mmap_file, elem);
    mmap_write_back (mmap);
    file_close(mmap->file);
    list_remove (&mmap->elem);
    free (mmap);
  }
}


void mmap_write_back (struct mmap_file *mmap)
{
  int i;
  uint32_t *upage;
  struct sup_page *spte;
  struct thread *t = running_thread();

  upage = mmap->start_addr;

  for (i = 0; i < mmap->pages; i++)
  {
    spte = sup_page_lookup (t, upage);
    if (spte != NULL && spte->status == IN_MEMORY)
    {
      file_write_at (spte->file, spte->paddr, spte->read_bytes, spte->ofs);
      spte->status = IN_FILE;
      pagedir_clear_page (t->pagedir, spte->vaddr);
    }
    upage += PGSIZE;
  }
}
/* End of Project 3 */
