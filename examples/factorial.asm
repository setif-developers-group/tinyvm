; factorial.asm - recursive factorial using CALL / RET and the stack
; Calling convention: argument and result in r0.
        LDI   r0, 5
        CALL  fact
        OUT   r0                 ; 5!  = 120
        LDI   r0, 10
        CALL  fact
        OUT   r0                 ; 10! = 3628800
        HALT

fact:   CMPI  r0, 1
        JGT   recurse
        LDI   r0, 1              ; base case: fact(<=1) = 1
        RET
recurse:
        PUSH  r0                 ; save n
        ADDI  r0, r0, -1
        CALL  fact               ; r0 = fact(n-1)
        POP   r1                 ; r1 = n
        MUL   r0, r0, r1
        RET
