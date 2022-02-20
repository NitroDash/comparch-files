	global _start

	section .text
_start:
	mov r10, 0
startLoop:
	push r10
	call main
	pop r10
	add r10, 1
	cmp r10, 200
	jle startLoop
	mov     rax, 60
        xor     rdi, rdi
        syscall


main:
	mov	rax, 201
	mov	rdi, 1
	mov	rsi, message
	mov	rdx, 13
	;jmp loop6
loop0:
	add	rax, 1
	add	rax, 3
	mov rdx, 200
	cmp	rax, 204
	jle loop0
	sub     rax, 204
loop1:
	add	rax, 1
	add	rax, 3
	mov rdx, 200
	cmp	rax, 204
	jle loop1
	sub     rax, 204
loop2:
	add	rax, 1
	add	rax, 3
	mov rdx, 200
	cmp	rax, 204
	jle loop2
	sub     rax, 204
loop3:
	add	rax, 1
	add	rax, 3
	mov rdx, 200
	cmp	rax, 204
	jle loop3
	sub     rax, 204
loop4:
	add	rax, 1
	add	rax, 3
	mov rdx, 200
	cmp	rax, 204
	jle loop4
	sub     rax, 204
loop5:
	add	rax, 1
	add	rax, 3
	mov rdx, 200
	cmp	rax, 204
	jle loop5
	sub     rax, 204
loop6:
	add	rax, 1
	add	rax, 3
	mov rdx, 200
	cmp	rax, 204
	jle loop6
	sub     rax, 204
	syscall
endLabel:
	ret

	section .data
message:
	db	"Hello, World", 10