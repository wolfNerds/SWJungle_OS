#include "userprog/syscall.h"
#include "lib/user/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/vaddr.h"
#include "threads/init.h" // 확인 요망
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	uintptr_t rsp = f->rsp;
	/* 유저 스택에 저장되어 있는 시스템 콜 넘버를 이용해 시스템 콜 핸들러 구현 */
	/* 스택 포인터가 유저 영역인지 확인 */
	check_address(rsp);
	/* 저장된 인자 값이 포인터일 경우 유저 영역의 주소인지 확인 */
	/* 확인 요망 (rax 값) */
	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	
	case SYS_EXIT:
		exit(f->R.rdi);
		break;

	// case SYS_FORK:
	// 	fork();
	// 	break;

	// case SYS_EXEC:
	// 	exec();
	// 	break;

	// case SYS_WAIT:
	// 	wait();
	// 	break;

	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;

	// case SYS_OPEN:
	// 	f->R.rax = open(f->R.rdi);
	// 	break;

	// case SYS_FILESIZE:
	// 	filesize();
	// 	break;

	// case SYS_READ:
	// 	read();
	// 	break;
		
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	// case SYS_SEEK:
	// 	seek();
	// 	break;

	// case SYS_TELL:
	// 	tell();
	// 	break;

	// case SYS_CLOSE:
	// 	close();
	// 	break;
		
	default:
		break;
	}
	/* 0 : halt */
	/* 1 : exit */
	/* . . . */

	// printf ("system call!\n");
	// thread_exit ();
}

void check_address(void *addr)
{
	/* 포인터가 가리키는 주소가 유저영역의 주소인지 확인 */
	if (!is_user_vaddr(addr))
	{
		/* 잘못된 접근일 경우 프로세스 종료 */
		/* 확인 요망 */
		exit(-1);
	}
}

// void get_argument(uintptr_t rsp, int *arg, int count)
// {
// 	int i;
// 	/* 인자가 저장된 위치가 유저영역인지 확인 */
// 	check_address(rsp);

// 	/* 유저 스택에 저장된 인자값들을 커널로 저장 */
// 	/* 확인 요망 */
// 	for(i = 0; i >count; i++)
// 	{
// 		arg[i] = *(char *) rsp;
// 		rsp++;
// 	}
// }

/* 확인 요망 */
void halt(void)
{
	/* PintOS를 종료시키는 시스템 콜 */
	power_off();
}

/* 확인 요망 */
void exit(int status)
{
	/* 현재 프로세스를 종료시키는 시스템 콜 */
	/* status: 프로그램이 정상적으로 종료됐는지 확인 */

	/* 실행중인 스레드 구조체를 가져옴 */
	struct thread *cur = thread_current();
	cur->exit_status = status;
	/* 정상적으로 종료 시 status는 0 */

	/* 종료 시 "프로세스 이름: exit(status)" 출력(Process Termination Message) */
	/* status 확인 요망 */
	/* 스레드 종료 */
	thread_exit();
}

/* 확인 요망 (나중에 구현!!!) */
pid_t fork(const char *thread_name)
{
	struct thread* cur = thread_current();
	process_fork(thread_name, &cur->tf);
	/* 프로세스 생성 시 부모 thread 구조체 안 list에 자식 thread 추가 */
}

/* 확인 요망 (나중에 구현!!!) */
int exec(const char *cmd_line)
{
	/* 자식 프로세스를 생성하고 프로그램을 실행시키는 시스템 콜 */

}

int wait(pid_t pid)
{
	
	/* 자식 프로세스의 pid를 기다리고 종료 상태를 확인. */
	/* pid가 살아있다면 종료될 때 까지 기다리고 종료 상태를 반환 */
	/* pid가 exit()을 호출하지 않았지만 커널에 의해 종료된 경우 wait함수는 -1을 반환 */
	
	/* 다음 조건 중 하나라도 참이면 -1 반환
		1. pid는 fork()에서 성공적으로 수신됐을 경우에만 호출 프로세스의 자식임.
		2. 프로세스는 주어진 자식에 대해 wait을 한 번만 수행 가능 */

	/* 프로세스는 임의의 수의 자식을 만들고 일부 또는 모든 자식을 기다리지 않고 종료 가능 */
	/* 초기 프로세스가 종료될 때 까지 PintOS가 종료되지 않도록 해야함.
		제공된 PintOS 코드는 main()에서 process_wait()을 호출하여 이를 수행
		process_wait()을 사용해 wait 시스템 호출을 구현하는 것이 좋음 */
	return process_wait(pid);
}

bool create(const char* file, unsigned initial_size)
{

	check_address(file);
	/* 파일 이름과 크기에 해당하는 파일 생성 */
	/* 파일 생성 성공 시 true 반환, 실패 시 flase 반환 */
	if (file == NULL || initial_size <= 0)
	{
		exit(-1);
	}
	
	return (filesys_create(file, initial_size)) ? true : false;
}

bool remove(const char *file)
{
	check_address(file);
	/* 파일 이름에 해당하는 파일을 제거 */
	/* 파일 제거 성공 시 true 반환, 실패 시 false 반환 */
	return (filesys_remove(file)) ? true : false;
}

int open(const char *file)
{
	/* 파일을 open */
	/* 해당 파일 객체에 파일 디스크립터 부여 */
	/* 파일 디스크립터 리턴 */
	/* 해당 파일이 존재하지 않으면 -1 리턴 */
	check_address(file); // 먼저 주소 유효한지 늘 체크
	struct file *file_obj = filesys_open(file); // 열려고 하는 파일 객체 정보를 filesys_open()으로 받기
	
	// 제대로 파일 생성됐는지 체크
	if (file_obj == NULL) {
		return -1;
	}
	int fd = add_file_to_fd_table(file_obj); // 만들어진 파일을 스레드 내 fdt 테이블에 추가

	// 만약 파일을 열 수 없으면] -1을 받음
	if (fd == -1) {
		file_close(file_obj);
	}

	return fd;
}

 /* 파일을 현재 프로세스의 fdt에 추가 */
int add_file_to_fd_table(struct file *file) {
	struct thread *t = thread_current();
	struct file **fdt = t-> fdt;
	int fd = t->fd; //fd값은 2부터 출발
	
	while (t->fdt[fd] != NULL && fd < FDT_COUNT_LIMIT) {
		fd++;
	}

	if (fd >= FDT_COUNT_LIMIT) {
		return -1;
	}
	t->fd = fd;
	fdt[fd] = file;
	return fd;
}

int write(int fd, const void *buffer, unsigned size)
{
	/* 열린 파일의 데이터를 기록하는 시스템 콜 */
	/* 성공 시 기록한 데이터의 바이트 수를 반환, 실패 시 -1 반환 */
	/* buffer : 기록 할 데이터를 저장한 버퍼의 주소 값 */
	/* size : 기록 할 데이터 크기 */
	/* fd 값이 1일 때 버퍼에 저장된 데이터를 화면에 출력(putbuf() 이용) */
	/* 파일에 동시 접근이 일어날 수 있으므로 lock 사용 */
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	/* 파일 디스크립터가 1일 경우 버퍼에 저장된 값을 화면에 출력 후
		버퍼의 크기 리턴 (putbuf() 이용) */
	/* 파일 디스크립터가 1이 아닐 경우 버퍼에 저장된 데이터를 크기 만큼
		파일에 기록 후 기록한 바이트 수를 리턴 */
	if (fd == 1) {
		putbuf(buffer, size);
		return size;
	}
}