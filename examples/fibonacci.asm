; fibonacci.asm - print the first 10 Fibonacci numbers
        LDI   r1, 0              ; a
        LDI   r2, 1              ; b
        LDI   r3, 10             ; how many numbers to print
loop:   OUT   r1
        ADD   r4, r1, r2         ; next = a + b
        MOV   r1, r2
        MOV   r2, r4
        ADDI  r3, r3, -1
        CMPI  r3, 0
        JNZ   loop
        HALT
