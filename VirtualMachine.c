#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

// Program's constants
#define REGCOUNT 10
#define PC_START 0x3000 // PC default address

// Memory definition
u_int16_t memory[__UINT16_MAX__]; // uint16t must be used sice LC-3 has 16 bit address memory

// Cpu registers definition
typedef enum
{      
    R0,
    R1,
    R2,
    R3,
    R4,
    R5,
    R6,
    R7,
    PC,
    COND
} Registers;

u_int16_t reg[REGCOUNT];

// Istruction Set definition
typedef enum{
    BR,
    ADD,
    LD,
    ST,
    JSR,
    AND,
    LDR,
    STR,
    RTI,
    NOT,
    LDI,
    STI,
    JMP,
    RES, // opcode reserved for future use
    LEA,
    TRAP
}IstructionSet;

// Conditions flags
typedef enum{
    POS = 1 << 0,
    ZRO = 1 << 1,
    NEG = 1 << 2
} Conditions;

// Trap Codes: trap will be implemented in C using OS primitives
typedef enum{
    GETC = 0x20,    // get character from keyboard
    OUT = 0x21,     // outputs a character
    PUTS = 0x22,    // outputs a word string
    IN = 0x23,      // get character from keyboard and echoes it onto terminal
    PUTSP = 0x24,   // output a byte string
    HALT = 0x25,    // stops the program
} TrapCodes;

// CPU status
typedef enum{
    STOP,
    RUN
}States;

int state = RUN;

// Memory mapped register
typedef enum{
    KBSR = 0xFE00,  // keyboard's status register
    KBDR = 0xFE02   // keyboard's data register
}MemoryMappedRegister;

// termninal input struct
struct termios originalTio;

// Return values
typedef enum
{
    COMPLETED = 0,    // ==> no errors
    WRONGSYNTAX = -1, // ==> wrong syntax
    LOADFAIL = -2,    // ==> failed to load source file(s)
    OPNOTDEFINED = -3 // ==> opcode not defined
} ReturnValues;

// Functions declaration
int readSourceFile(const char *sourcePath);
u_int16_t memRead(u_int16_t address);
void handleInterrupt(int signal);
void disableInputBuffering();
void restoreInputBuffering();
u_int16_t signExtension(u_int16_t n, int bitCount);
void add(u_int16_t instr);
void ldi(u_int16_t instr);
void and (u_int16_t instr);
void br(u_int16_t instr);
void jmp(u_int16_t instr);
void jsr(u_int16_t instr);
void ld(u_int16_t instr);
void ldr(u_int16_t instr);
void lea(u_int16_t instr);
void not(u_int16_t instr);
void st(u_int16_t instr);
void sti(u_int16_t instr);
void str(u_int16_t instr);
void trap(u_int16_t instr);
void put();
void get();
void out();
void in();
void halt();
void putsp();
void memWrite(u_int16_t address, u_int16_t value);
void readFile(FILE *fp);
u_int16_t convertEndianess(u_int16_t n);
u_int16_t memRead(u_int16_t address);
u_int16_t keyCheck();

int main(int argc, char const *argv[])
{
    u_int16_t instr = 0;
    u_int16_t opcode = 0;
    int res = COMPLETED; 
    reg[PC] = PC_START;

    if (argc < 2) // syntax check
    {
        printf("lc3 [source file 1] ... \n");
        res = WRONGSYNTAX;
    }
    else
    {

        // Loading object file(s)
        for (int j = 1; j < argc && res != LOADFAIL; j++)
        {
            if (readSourceFile(argv[j]) == -1)
            {
                res = LOADFAIL;
                state = STOP;
                printf("Failed to load source file: %s\n", argv[j]);
            }
        }


        signal(SIGINT, handleInterrupt);
        disableInputBuffering();

        // Fetch-Decode-Execute cycle
        while (state == RUN)
        {
            instr = memRead(reg[PC]);
            reg[PC]++;
            opcode = instr >> 12; // since istructions are 16-bit wide, and opcodes are located at bits [15, 12], we shift right to remove the other twelwe bits


            switch (opcode)
            {
                case ADD:
                    add(instr);
                    break;

                case AND:
                    and(instr);
                    break;

                case NOT:
                    not(instr);
                    break;

                case BR:
                    br(instr);
                    break;

                case JMP:
                    jmp(instr);
                    break;

                case JSR:
                    jsr(instr);
                    break;

                case LD:
                    ld(instr);
                    break;

                case LDI:
                    ldi(instr);
                    break;

                case LDR:
                    ldr(instr);
                    break;

                case LEA:
                    lea(instr);
                    break;

                case ST:
                    st(instr);
                    break;

                case STI:
                    sti(instr);
                    break;

                case STR:
                    str(instr);
                    break;

                case TRAP:
                    trap(instr);
                    break;

                case RES:
                    break;

                case RTI:
                    break;

                default:
                    printf("Invalid opcode. Execution aborted.");
                    state = STOP;
                    res = OPNOTDEFINED;
                    break;
            }
        }
    }

    restoreInputBuffering();

    return res;
}

    

/*
    n: number to be extended to 16 bits
    bitCount: number of bits used to represent n
*/
u_int16_t signExtension(u_int16_t n, int bitCount){
    if ((n >> (bitCount - 1)) & 1) // check if most significant bit is 1 (with a bitwise and)
        n = (65535 << bitCount) | n; // 65535 = 1111...111 shift it left of bitCount positions to "create space" for n that it's "inserted" with a bitwise or
}

// Updates given register according to the result of the last operation performed
// regID: register which value must be used to set conditions flag register
void updateFlags(u_int16_t regID){
    if (reg[regID] == 0)
        reg[COND] = ZRO;
    else if (reg[regID] >> 15 == 1) // shift right to isolate sign bit
        reg[COND] = NEG;
    else 
        reg[COND] = POS;
}

// Istruction Set implementation
void add(u_int16_t instr){
    u_int16_t dr = (instr >> 9) & 7;
    u_int16_t sr1 = (instr >> 6) & 7;
    u_int16_t mode = (instr >> 5) & 1;

    if (mode == 1){
        u_int16_t immediate = signExtension(instr & 31, 5);
        reg[dr] = reg[sr1] + immediate;
    } else {
        u_int16_t sr2 = instr & 7;
        reg[dr] = reg[sr1] + reg[sr2];
    }

    updateFlags(dr); // updates conditions flags according to destination register's value
}

void ldi(u_int16_t instr){
    // useful to load a value that's to far away from PC to be reached with ld
    // with this istruction that value can be stored in a reachable location
    u_int16_t dr = (instr >> 9) & 7;
    u_int16_t offset = signExtension((instr & 511), 9);

    reg[dr] = memRead(memRead(reg[PC] + offset));

    updateFlags(dr);
}

void and(u_int16_t instr){
    u_int16_t dr = (instr >> 9) & 7;
    u_int16_t sr1 = (instr >> 6) & 7;
    u_int16_t mode = (instr >> 5) & 1;

    if (mode == 1)
    {
        u_int16_t immediate = signExtension(instr & 31, 5);
        reg[dr] = reg[sr1] & immediate;
    }
    else
    {
        u_int16_t sr2 = instr & 7;
        reg[dr] = reg[sr1] & reg[sr2];
    }

    updateFlags(dr);
}

void br(u_int16_t instr){
    u_int16_t condFlags = (instr >> 9) & 7;
    u_int16_t offset = signExtension(instr & 511, 9);

    if (condFlags & reg[COND])
        reg[PC] += offset;
}

void jmp(u_int16_t instr){
    // Jump also implements RET, since the latter it's just a special case
    u_int16_t baseReg = (instr >> 6) & 7;

    reg[PC] = reg[baseReg]; 
}

void jsr(u_int16_t instr){
    u_int16_t mode = (instr >> 11) & 1;

    reg[R7] = reg[PC];
    if (mode == 0){
        reg[PC] = (instr >> 6) & 7;
        reg[PC] = reg[R1];
    }
    else
        reg[PC] += signExtension(instr & 2047, 11); 
}

void ld(u_int16_t instr){
    u_int16_t dr = (instr >> 9) & 7;
    u_int16_t offset = signExtension(instr & 511, 9);

    reg[dr] = memRead(reg[PC] + offset);
    updateFlags(dr);
}

void ldr(u_int16_t instr){
    u_int16_t dr = (instr >> 9) & 7;
    u_int16_t baseReg = (instr >> 6) & 7;
    u_int16_t offset = signExtension(instr & 63, 6);

    reg[dr] = memRead(reg[baseReg] + offset);
    updateFlags(dr);
}

void lea(u_int16_t instr){
    u_int16_t dr = (instr >> 9) & 7;
    u_int16_t offset = signExtension(instr & 511, 9);

    reg[dr] = reg[PC] + offset;
    updateFlags(dr);
}

void not(u_int16_t instr){
    u_int16_t dr = (instr >> 9) & 7;
    u_int16_t sr = (instr >> 6) & 7;

    reg[dr] = ~reg[sr];
    updateFlags(dr);
}

void st(u_int16_t instr){
    u_int16_t sr = (instr >> 9) & 7;
    u_int16_t offset = signExtension(instr & 511, 9);

    memWrite(reg[PC] + offset, reg[sr]);
}

void sti(u_int16_t instr){
    u_int16_t sr = (instr >> 9) & 7;
    u_int16_t offset = signExtension(instr & 511, 9);

    memWrite(memRead(reg[PC] + offset), reg[sr]);
}

void str(u_int16_t instr){
    u_int16_t sr = (instr >> 9) & 7;
    u_int16_t baseReg = (instr >> 6) & 7;
    u_int16_t offset = signExtension(instr & 63, 6);

    memWrite(reg[baseReg] + offset, reg[sr]);
}

// Trap software management
void trap(u_int16_t instr){
    u_int16_t trapIndex = instr & 255;

    switch (trapIndex)
    {
        case GETC:
            get();
            break;
        
        case OUT:
            out();
            break;

        case PUTS:
            put();
            break;
        
        case IN:
            in();
            break;
        
        case PUTSP:
            putsp();
            break;

        case HALT:
            halt();
            break;
    }
}

// Puts trap management
void put(){
    u_int16_t* c = memory + reg[R0];

    while (*c) // loops till c != 0x0000
    {
        putc((char)*c, stdout);
        c++;
    }

    fflush(stdout);
}

// Getc trap management
void get(){
    reg[R0] = (u_int16_t)getchar();
}

void out()
{
    putc((char)reg[R0], stdout);
    fflush(stdout);
}

void in(){
    char c = ' ';

    printf("Enter a character ==> ");
    c = getchar();
    putc(c, stdout);
    reg[R0] = (u_int16_t)c;
}

void halt(){
    printf("Execution completed");
    fflush(stdout);
    state = STOP;
}

void putsp(){
    u_int16_t* c = memory + reg[R0];
    char aus = ' ';

    while (*c){
        aus = (*c) & 255;
        putc(aus, stdout);

        aus = (*c) >> 8; 
        if(aus)
            putc(aus, stdout);
        c++;
    }

    fflush(stdout);
}

// Object file loading into memory
int readSourceFile(const char* sourcePath){
    FILE* fp = fopen(sourcePath, "rb"); // rb and wb are used if the opened file contains non-text content
    int res = -1;

    if (fp != NULL)
    {
        readFile(fp);
        fclose(fp);
        res = 0;
    }

    return res;
}

void readFile(FILE* fp){
    u_int16_t startAddress;
    u_int16_t maxFileSize;
    u_int16_t *absoluteAddress;
    size_t fileSize;

    // read and covert program's starting memory address
    fread(&startAddress, sizeof(startAddress), 1, fp);
    startAddress = convertEndianess(startAddress);

    // compute memory aboslute address
    maxFileSize = __UINT16_MAX__ - startAddress;
    absoluteAddress = memory + startAddress; // memory absolute address to load the source file at
    fileSize = fread(absoluteAddress, sizeof(u_int16_t), maxFileSize, fp);

    // big endian -> little endian source file conversion
    while (fileSize > 0)
    {
        *absoluteAddress = convertEndianess(*absoluteAddress);
        absoluteAddress++;
        fileSize--;
    }
    
}

u_int16_t convertEndianess(u_int16_t n){
    return (n << 8) | (n >> 8);
}

// memory write function
void memWrite(u_int16_t address, u_int16_t value){
    memory[address] = value;
}

// memory read function
u_int16_t memRead(u_int16_t address){
    if (address == KBSR){
        if (keyCheck()){
            memory[KBSR] = 1 << 15;
            memory[KBDR] = getchar();
        } else
            memory[KBSR] = 0;
    }

    return memory[address];
}

// Keyboard and interrumpts management
u_int16_t keyCheck(){
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

void disableInputBuffering(){
    tcgetattr(STDIN_FILENO, &originalTio);
    struct termios new_tio = originalTio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restoreInputBuffering(){
    tcsetattr(STDIN_FILENO, TCSANOW, &originalTio);
}

void handleInterrupt(int signal){
    restoreInputBuffering();
    exit(-2);
}
