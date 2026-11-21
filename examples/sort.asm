; sort.asm - bubble sort an array of signed words in memory, then print it
        .equ  N, 8

start:  LDI   r10, N             ; r10 = element count
outer:  LDI   r1, 0              ; r1 = "swapped" flag
        LDI   r2, array          ; r2 = pointer into the array
        ADDI  r3, r10, -1        ; r3 = comparisons per pass
inner:  LD    r4, [r2]
        LD    r5, [r2+4]
        CMP   r4, r5             ; signed compare
        JLE   noswap
        ST    r5, [r2]           ; swap neighbours
        ST    r4, [r2+4]
        LDI   r1, 1
noswap: ADDI  r2, r2, 4
        ADDI  r3, r3, -1
        CMPI  r3, 0
        JNZ   inner
        CMPI  r1, 0              ; any swap this pass? go again
        JNZ   outer

        LDI   r2, array         ; print the sorted array
        LDI   r3, N
print:  LD    r4, [r2]
        OUT   r4
        ADDI  r2, r2, 4
        ADDI  r3, r3, -1
        CMPI  r3, 0
        JNZ   print
        HALT

array:  .word 42, -7, 19, 3, 88, 0, -50, 12
