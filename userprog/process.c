#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	char *save_ptr;
	strtok_r(file_name, " ", save_ptr);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);  //배열은 전역변수로 선언 안해도 전역변수로 쓰이는가??
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* count null 미포함. 확인 필요 */
int
parsing_str(char *file_name, char* argv[]){
	// 토큰 변수, 포인터 - 김채욱
	printf("=====================parsing_str진입============\n");
	char *token, *save_ptr;
	int count = 0; 
	// 0 grep 1 foo 2 bar 3 \0 
	for (token = strtok_r (file_name, " ", &save_ptr); 
		token != NULL;
		token = strtok_r (NULL, " ", &save_ptr))
		{
			argv[count] = token; // grep foo bar
			count++; // 1 2 3 
		}
	//argv[count] = "\0";
	// printf("Count: %d\n", count);
	return count; // \0을 포함한 개수를 셈
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_) {
	/* Clone current thread to new thread.*/
	printf("=====================process_fork진입=============\n");
	struct thread *curr = thread_current();
	memcpy(&curr->parent_if, if_, sizeof(struct intr_frame));

	tid_t tid = thread_create (name, PRI_DEFAULT, __do_fork, curr);
	if(tid == TID_ERROR)
	{
		return TID_ERROR;
	}
	struct thread *child_thread = get_child_process(tid);
	sema_down(&child_thread->sema_fork);
	// printf("-------------------------child id : %d----child_thread->exit_status : %d\n", tid, child_thread->exit_status);
	if(child_thread->exit_status == -1)
	{
		return TID_ERROR;
	}
	return tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va))
	{
		return true;
	}
	
	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page(parent->pml4, va);
	if(parent_page == NULL)
	{
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */

	newpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if(newpage == NULL)
	{
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = &parent->parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	if(parent->fd == FDT_COUNT_LIMIT)
	{
		goto error;
	}

	current->fdt[0] = parent->fdt[0];
	current->fdt[1] = parent->fdt[1];

	for(int i = 2; i < FDT_COUNT_LIMIT; i++)
	{
		struct file *f = parent->fdt[i];
		if(f == NULL)
		{
			continue;
		}
		current->fdt[i] = file_duplicate(f);
	}
	current->fd = parent->fd;

	sema_up(&current->sema_fork);
	if_.R.rax = 0;
	process_init();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	current->exit_status = TID_ERROR;
	sema_up(&current->sema_fork);
	exit(TID_ERROR);
	// thread_exit ();
}exec;

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;
	printf("===================process_exec진입============\n");

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;                   
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;


	/* We first kill the current context */
	process_cleanup ();

	// 안쓰는 변수 선언해서 버그 발생 
	// char fn_copy[128];
	// memcpy(fn_copy, file_name, strlen(file_name) + 1);

	/* And then load the binary */
	success = load (file_name, &_if); // 복사한 전체 파일 인자로 넣음.

	/* If load failed, quit. */
	if (!success){
		palloc_free_page (file_name);
		return -1;
	}

	// 디버깅
	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);
	
	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}

void argument_stack(char **argv, int count, struct intr_frame* if_)
{
	printf("==================argument_stack진입============\n");
	/* 프로그램 이름 및 인자(문자열) push */
	/* 프로그램 이름 및 인자 주소들 push */
	/* argv (문자열을 가리키는 주소들의 배열을 가리킴) push*/ 
	/* argc (문자열의 개수 저장) push */
	/* fake address(0) 저장 */
	char* rsp_adr[128];
	int i, j;
	/* 프로그램 이름 및 인자(문자열) push */
	for(i = count - 1; i > -1; i--) 
	{
		if_->rsp = if_->rsp - strlen(argv[i]) - 1;
		rsp_adr[i] = if_->rsp;
		// strlcat(**(char **)rsp, parse[i][j],strlen(parse[i]));
		// *(char *)if_->rsp = parse[i][j];
		memcpy(if_->rsp, argv[i], strlen(argv[i]) + 1);
	}


	// rsp(16진수)를 8의 배수로 체크하고 맞춤
	while (if_->rsp % 8 != 0)
	{
		if_->rsp--;
		// *(uint8_t*) if_->rsp = 0; // 이거 써도 됨
		memset(if_->rsp, 0, sizeof(char)); //char*가 아니라 char로 해줘야 함
	}
	// 0 추가
	if_->rsp = if_->rsp - 8;
	memset(if_->rsp, 0, sizeof(char*));

	/* 프로그램 이름 및 인자 주소들 push */
	for (i = count - 1; i >= 0; i--)
	{
		if_->rsp = if_->rsp - 8;
		memcpy(if_->rsp, &rsp_adr[i], sizeof(char*));
		
		// *(char *)if_->rsp = rsp_adr[i];
	}

	// /* argv (문자열을 가리키는 주소들의 배열을 가리킴) push*/ 
	// if_->rsp = if_->rsp - 8;
	// memcpy(if_->rsp, &rsp_adr[-1], sizeof(char*));

	// if_->rsp = if_->rsp - 8;
	// memcpy(if_->rsp, count, count);

	// 마지막에 fake address를 저장한다
	if_->rsp = if_->rsp - 8 ;
	memset(if_->rsp, 0, sizeof(void*));

	if_->R.rsi = if_->rsp + 8;
	if_->R.rdi = count;
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
/* 자식 스레드(child_tid)가 종료될 때 까지 대기하다가 종료상태를 반환
	커널에 의해 종료된 경우 -1을 반환 child_tid가 잘못되었거나, 호출 프로세스의
	하위 항목이 아니거나, 지정된 child_tid에 대해 process_wait가 이미 호출된
	경우 -1을 즉시 반환.
	이 기능은 2-2에서 구현 */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	/* 자식 프로세스가 모두 종료될 때 까지 대기(sleep state) */
	/* 자식 프로세스가 올바르게 종료 됐는지 확인 */

	/* 자식 프로세스의 프로세스 디스크립터 검색 */
	/* 예외 처리 발생 시 -1 리턴 */
	/* 자식 프로세스가 종료될 때 까지 부모 프로세스 대기 (세마포어 이용) */
	/* 자식 프로세스 디스크립터 삭제 */
	/* 자식 프로세스의 exit status 리턴 */
	printf("=============================process_wait=======\n");
	struct thread *child = get_child_process(child_tid);

	if(child == NULL)
	{
		return -1;
	}

	sema_down(&child->sema_wait);
	int exit_status = child->exit_status;
	list_remove(&child->child_elem);
	sema_up(&child->sema_free);

	// thread_set_priority(thread_get_priority()-1);
	return exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	/* 프로세스에 열린 모든 파일을 닫음 */
	/* 파일 디스크립터 테이블의 최대값을 이용해 파일 디스크립터의
		최소값인 2가 될 때 까지 파일을 닫음 */
	// for(int i = 0; i < FDT_COUNT_LIMIT; i++)
	// {
	// 	close(i);
	// }
	// palloc_free_multiple(curr->fdt, 3);

	// file_close(curr->running_file);

	// sema_up(&curr->sema_wait);
	// sema_down(&curr->sema_free);
	printf("====================process_exit진입\n");
	palloc_free_multiple(curr->fdt, 3);
	file_close(curr->running_file);
	sema_up(&curr->sema_wait);
	sema_down(&curr->sema_free);
	
	/* 파일 디스크립터 테이블 메모리 해제 */
	process_cleanup ();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	printf("===================load 진입==================\n");
	char* argv[128];
	int count = parsing_str(file_name, argv);

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create (); // 페이지 디렉토리 생성
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ()); // 페이지 테이블 활성화

	/* Open executable file. */
	file = filesys_open (file_name); // 프로그램 파일 open
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	/* ELF파일의 헤더 정보를 읽어와 저장 */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	t->running_file = file; // thread 구조체의 running_file을 현재 실행할 파일로 초기화
	file_deny_write(file);	// file_deny_write()를 이룔하여 파일에 대한 write를 거부

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		/* 배치 정보를 읽어와 저장 */
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
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
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					/* 배치 정보를 통해 파일을 메모리에 적재 */
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	/* 스택 초기화 */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	argument_stack(argv, count, if_);

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
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

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */

int process_add_file(struct file *f)
{
	struct thread* curr = thread_current();
	while(curr->fdt[curr->fd] && curr->fd < FDT_COUNT_LIMIT)
	{
		/* 파일 디스크립터의 최대값 1 증가 */
		curr->fd++;
	}
	if(curr->fd >= FDT_COUNT_LIMIT)
	{
		return -1;
	}
	/* 파일 객체를 파일 디스크립터 테이블에 추가 */
	curr->fdt[curr->fd] = f;

	/* 파일 디스크립터 리턴 */
	return curr->fd;
}

struct file *process_get_file(int fd)
{
	struct thread* curr = thread_current();

	// if (curr->fdt[fd] == NULL)
	if(fd < 0 || fd >= FDT_COUNT_LIMIT)
	{
		/* 없을 시 NULL을 리턴 */
		return NULL;
	}
	/* 파일 디스크립터에 해당하는 파일 객체를 리턴 */
	return curr->fdt[fd];
}

void process_close_file(int fd)
{
	struct thread* curr = thread_current();
	/* 파일 디스크립터에 해당하는 파일을 닫음 */
	file_close(curr->fdt[fd]);
	/* 파일 디스크립터 테이블 해당 엔트리 초기화 */
	curr->fdt[fd] = NULL;
}

struct thread* get_child_process(int pid)
{
	struct thread *curr = thread_current();
	struct list *childs = &curr->child_list;
	struct list_elem *elem = list_begin(childs);
	/* 자식 리스트에 접근하여 프로세스 디스크립터 검색 */
	for(;elem != list_end(childs); elem = list_next(elem))
	{
		struct thread *child_thread = list_entry(elem, struct thread, child_elem);
		if(child_thread->tid == pid)
		{
			/* 해당 pid가 존재하면 프로세스 디스크립터 반환 */
			return child_thread;
		}
	}
	/* 리스트에 존재하지 않으면 NULL 리턴 */
	return NULL;
}

void remove_child_process(struct thread *cp)
{
	struct thread *curr = thread_current();
	struct list_elem *child_elem = &cp->child_elem;
	/* 자식 리스트에서 제거 */
	for(struct list_elem *current_child_elem = list_begin(&curr->child_list);
		current_child_elem != list_end(&curr->child_list); list_next(current_child_elem))
		{
			/* 확인 요망 */
			if(current_child_elem == child_elem) list_remove(current_child_elem);
		}
	/* 프로세스 디스크립터 메모리 해제 */
	palloc_free_page(cp);
}