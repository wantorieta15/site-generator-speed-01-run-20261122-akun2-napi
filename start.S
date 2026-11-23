	.text
	.globl _start
_start:
	xor %ebp, %ebp
	mov %rsp, %rdi
	and $-16, %rsp
	call main_entry
	mov %rax, %rdi
	mov $60, %rax
	syscall
	.section .note.GNU-stack,"",@progbits
