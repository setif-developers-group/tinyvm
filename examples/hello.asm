; hello.asm - print a NUL-terminated string, one byte at a time
start:  LDI   r1, message        ; r1 = pointer to the string
loop:   LDB   r2, [r1]           ; r2 = *r1
        CMPI  r2, 0              ; end of string?
        JZ    done
        OUTC  r2                 ; print the character
        ADDI  r1, r1, 1          ; advance pointer
        JMP   loop
done:   HALT

message: .string "Hello, TinyVM!\n"
