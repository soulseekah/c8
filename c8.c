/**
 * \file c8.c
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

/**
 * An 16-bit instruction.
 */
typedef int16_t c8_instruction_t;

/**
 * An 16-bit address.
 */
typedef int16_t c8_address_t;

/**
 * A 16-bit register.
 */
typedef int16_t c8_register_t;

/**
 * The ROM.
 */
typedef struct {
	/**
	 * The backing file pointer.
	 */
	FILE *file;
} ROM_t;

/**
 * The CPU.
 */
typedef struct {
	/**
	 * The program counter.
	 */
	c8_address_t pc;
	/**
	 * The 16 registers.
	 */
	c8_register_t v[16];
} CPU_t;

/**
 * Retrieve an instruction from the ROM at a specific address.
 *
 * \param rom The ROM to use.
 * \param address The address to get the instruction from.
 *
 * \return instruction_t The instruction retrieved.
 */
c8_instruction_t rom_get(ROM_t *rom, c8_address_t address) {
	fseek(rom->file, address, SEEK_SET);
	return (fgetc(rom->file) << 8) | fgetc(rom->file);
}

/**
 * Reset the state of this CPU.
 *
 * \param cpu This CPU.
 *
 * \return void
 */
void cpu_reset(CPU_t *cpu) {
	cpu->pc = 0;
	for (int i = 0; i < 16; i++) {
		cpu->v[i] = 0;
	}
}

/**
 * Dump the state of the CPU.
 *
 * \param cpu The CPU to dump the state of.
 *
 * \return void
 */
void cpu_dump(CPU_t *cpu) {
	for (int i = 0; i < 16; i++) {
		printf("V%X = %04x\n", i, cpu->v[i]);
	}
}

/**
 * Execute an instruction.
 *
 * \param cpu The CPU to execute the instruction on.
 * \param instruction The instruction to execute.
 *
 * \return void
 */
void cpu_execute(CPU_t *cpu, c8_instruction_t instruction) {
	if (/* 6XNN */ instruction >> 12 == 0x6 /* Set VX to NN */) {	
		cpu->v[instruction >> 8 & 0xf] = instruction & 0xff;
	} else {
		fprintf(stderr, "Unknown instruction %04x!\n", instruction & 0xffff);
		exit(-1);
	}

	/** Move forward */
	cpu->pc += 2;
}

/**
 * Test our code.
 *
 * \param argc The number of arguments.
 * \param argv The arguments.
 *
 * \return int Program exit code.
 */
int test(int, char *[]);

/**
 * The main function.
 *
 * \param argc The number of arguments.
 * \param argv The arguments.
 *
 * \return int Program exit code.
 */
int main(int argc, char *argv[]) {
	printf("The Chip-8 Emulator Project\n");

	if (getenv("TEST")) {
		return test(argc, argv);
	}

	if (argc < 2) {
		fprintf(stderr, "Please supply a ROM file.\n");
		return -1;
	}

	CPU_t cpu;
	cpu_reset(&cpu);

	ROM_t rom;
	rom.file = fopen(argv[1], "rb");

	while (true) {
		cpu_execute(&cpu, rom_get(&rom, cpu.pc));
	}

	return 0;
}

#define TEST_EQUALS(a,b) if (a == b) { printf("."); passed++; } else { printf("F("#a"[%04x] != "#b"[%04x])\n", a, b); failed++; }

int test(int argc, char *argv[]) {
	printf("** Running tests...\n\n");

	int passed = 0;
	int failed = 0;

	CPU_t cpu;
	cpu_reset(&cpu);

	TEST_EQUALS(cpu.pc, 0);
	cpu_execute(&cpu, 0x6001); TEST_EQUALS(cpu.v[0], 0x1)
	cpu_execute(&cpu, 0x6a9f); TEST_EQUALS(cpu.v[0xa], 0x9f)
	cpu_execute(&cpu, 0x6fff); TEST_EQUALS(cpu.v[0xf], 0xff)
	TEST_EQUALS(cpu.pc, 6)

	printf("\n%d tests: %d passed, %d failed\n", passed + failed, passed, failed);

	return 0;
}
