	.file "ioctl64.s"
	.text
	.globl ioctl
	.align 16, 0x90
	.type ioctl,@function
ioctl:
	# Be kind to debuggers, set up a stack frame.
	pushq %rbp
	movq %rsp, %rbp

	# Save the argument registers
	#pushq %rax
	pushq %rbx
	pushq %rcx
	pushq %rdx
	pushq %rsi
	pushq %rdi
	pushq %r8
	pushq %r9
	pushq %r10

	# Save the callee owned registers
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15

	# No need to setup the arguments for ioctl_resolver, they're already set up

	# Stack should be 16-byte aligned already

	# Call the resolver
	callq ioctl_resolver@PLT

	# rax now has the resolved pointer. Move it into r11
	movq %rax, %r11

	# Restore the callee owned registers
	popq %r15
	popq %r14
	popq %r13
	popq %r12

	# Restore the argument registers
	popq %r10
	popq %r9
	popq %r8
	popq %rdi
	popq %rsi
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax

	# Destroy stack frame
	movq %rbp, %rsp
	popq %rbp

	# Jump into the resolved
	jmpq *%r11

.ioctl_end:
	.size ioctl, .ioctl_end-ioctl
