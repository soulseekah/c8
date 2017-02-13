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
typedef uint16_t c8_instruction_t;

/**
 * An 16-bit address.
 */
typedef uint16_t c8_address_t;

/**
 * A 16-bit register.
 */
typedef uint8_t c8_register_t;

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
 * The display.
 */
typedef struct {
	/**
	 * A 64x32 bitfield of pixels.
	 */
	uint64_t p[32];
} Display_t;

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

	/**
	 * The I 16-bit register.
	 */
	c8_address_t i;

	/**
	 * A pointer to the ROM to read data, etc.
	 */
	ROM_t *rom;

	/**
	 * A pointer to the display.
	 */
	Display_t *display;
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
 * Retrieve one byte of data from ROM.
 *
 * \param rom The ROM to use.
 * \param address The address to get the data from.
 *
 * \return uint8_t The byte retrieved.
 */
uint8_t rom_read(ROM_t *rom, c8_address_t address) {
	fseek(rom->file, address, SEEK_SET);
	return fgetc(rom->file);
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
	cpu->i = 0;
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
 * Dump the state of the display.
 *
 * \param display The display to dump.
 *
 * \return void
 */
void display_dump(Display_t *display) {
	/** Frames */
	printf(" ");
	for (int f = 0; f < 64; f++)
		printf("-");

	for (int y = 0; y < 32; y++) {
		printf("\n|");
		uint64_t p = display->p[y];
		for (int x = 0; x < 64; x++) {
			printf("%c", p & 0x1 ? '*' : ' ');
			p = p >> 1;
		}
		printf("|");
	}
	printf("\n");

	printf(" ");
	for (int f = 0; f < 64; f++)
		printf("-");
	printf("\n");
}

/**
 * Clear the display.
 *
 * \param display The display to clear.
 *
 * \return void
 */
void display_clear(Display_t *display) {
	for (int p = 0; p < 32; p++)
		display->p[p] = 0;
}

/**
 * Draw a row on this display.
 *
 * \param display The display to draw on.
 * \param row The 8-bit row.
 * \param x The x coordinate.
 * \param y The y coordinate.
 *
 * \return bool Whether pixels were turned from on to off.
 */
bool display_draw_row(Display_t *display, uint8_t row, uint8_t x, uint8_t y) {
	/** Mirror the row */
	row = (row & 0xF0) >> 4 | (row & 0x0F) << 4;
	row = (row & 0xCC) >> 2 | (row & 0x33) << 2;
	row = (row & 0xAA) >> 1 | (row & 0x55) << 1;

	uint64_t before = display->p[y];
	display->p[y] ^= ((uint64_t)row) << x;
	uint64_t after = display->p[y];

	return before > after;
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
	       if (/* 6XNN */ ((instruction >> 12) & 0xf) == 0x6 /* Set VX to NN */) {	
		cpu->v[instruction >> 8 & 0xf] = instruction & 0xff;

	} else if (/* ANNN */ ((instruction >> 12) & 0xf) == 0xa /* Set I to NNN */) {
		cpu->i = instruction & 0xfff;
	
	} else if (/* DXYN */ ((instruction >> 12) & 0xf) == 0xd /* Draw 8xN sprite at VX VY, set VF to screen set */) {

		bool unset = false;

		uint8_t x = cpu->v[(instruction >> 8) & 0xf];
		uint8_t y = cpu->v[(instruction & 0xf0) >> 4];
		for (uint8_t h = 0; h < (instruction & 0xf); h++) {
			if (display_draw_row(cpu->display, rom_read(cpu->rom, cpu->i - 0x200 + h), x, y - h)) {
				unset = true;
			}
		}
		cpu->v[0xf] = unset ? 1 : 0;
	} else {
		fprintf(stderr, "Unknown instruction %04x!\n", instruction);
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

	Display_t display;
	display_clear(&display);

	cpu.rom = &rom;
	cpu.display = &display;

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

	Display_t display;
	display_clear(&display);

	/** Display clear */
	for (uint64_t y = 0; y < 32; y++) {
		TEST_EQUALS((uint32_t)(display.p[y] & 0xffffffff), 0);
		TEST_EQUALS((uint32_t)((display.p[y] >> 32) & 0xffffffff), 0);
	}

	CPU_t cpu;
	cpu_reset(&cpu);

	/** Reset state */
	TEST_EQUALS(cpu.pc, 0)
	for (int i = 0; i < 16; i++)
		TEST_EQUALS(cpu.v[i], 0)
	TEST_EQUALS(cpu.i, 0)

	/** 6VNN Set V to NN */
	cpu_execute(&cpu, 0x6001); TEST_EQUALS(cpu.v[0], 0x1)
	cpu_execute(&cpu, 0x6a9f); TEST_EQUALS(cpu.v[0xa], 0x9f)
	cpu_execute(&cpu, 0x6fff); TEST_EQUALS(cpu.v[0xf], 0xff)
	TEST_EQUALS(cpu.pc, 6)

	/** ANNN Set I to NNN */
	cpu_execute(&cpu, 0xa423); TEST_EQUALS(cpu.i, 0x423)
	cpu_execute(&cpu, 0xa0ff); TEST_EQUALS(cpu.i, 0x0ff)

	/**
	 * Some display tests.
	 * \todo We should really be testing for DXYN, too.
	 */
	bool unset = display_draw_row(&display, 0x80, 0, 0);
	TEST_EQUALS((uint8_t)(display.p[0] & 0xff), 0x01)
	TEST_EQUALS((uint8_t)unset, 0)
	unset = display_draw_row(&display, 0x80, 0, 0);
	TEST_EQUALS((uint8_t)(display.p[0] & 0xff), 0x00)
	TEST_EQUALS((uint8_t)unset, 1)

	printf("\n%d tests: %d passed, %d failed\n", passed + failed, passed, failed);

	return 0;
}
