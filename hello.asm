	global _start

	section .text
_start:
	call main
	mov     rax, 60
        xor     rdi, rdi
        ;syscall


main:
	mov	rax, 1
	mov	rdi, 1
	mov	rsi, message
	mov	rdx, 13
loop:
	add	rax, 1
	add	rax, 3
	cmp	rax, 204
	jle loop
	sub     rax, 204
	syscall
	ret

	section .data
message:
	db	"Hello, World", 10
