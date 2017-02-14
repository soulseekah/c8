/**
 * \file c8.c
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "SDL.h"

#define DISPLAY_W 64
#define DISPLAY_H 32
#define WINDOW_SCALE 16
#define WINDOW_W WINDOW_SCALE * DISPLAY_W
#define WINDOW_H WINDOW_SCALE * DISPLAY_H

#define PIXEL_SET 255, 255, 255
#define PIXEL_UNSET 0, 0, 0 

#define ROM_OFFSET 0x200
#define RAM_SIZE 0x1000

#define NELEMS(x) (sizeof(x) / sizeof((x)[0]))

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
 * The RAM.
 */
typedef uint8_t * RAM_t;

/**
 * The display.
 */
typedef struct {
	/**
	 * A 64x32 bitfield of pixels.
	 */
	uint64_t p[DISPLAY_H];

	/**
	 * An SDL renderer.
	 */
	SDL_Renderer *renderer;
} Display_t;

/**
 * CPU Flags.
 */
typedef struct {
	uint8_t HALT : 1;
} CPU_Flags_t;

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
	 * The stack pointer.
	 */
	uint8_t sp;

	/**
	 * The stack.
	 */
	c8_address_t stack[UINT8_MAX];

	/**
	 * A pointer to the RAM.
	 */
	RAM_t ram;

	/**
	 * A pointer to the display.
	 */
	Display_t *display;

	/**
	 * Flags.
	 */
	CPU_Flags_t flags;
} CPU_t;

/**
 * Retrieve one byte of data from RAM.
 *
 * \param ram The RAM to use.
 * \param address The address to get the data from.
 *
 * \return uint8_t The byte retrieved.
 */
uint8_t ram_get_byte(RAM_t ram, c8_address_t address) {
	if (address >= RAM_SIZE) {
		fprintf(stderr, "Segmentation fault!");
		exit(-1);
	}
	return ram[address];
}

/**
 * Write a byte to RAM.
 *
 * \param ram The RAM.
 * \param address The address to write to.
 * \param byte The byte to write.
 *
 * \return void
 */
void ram_write_byte(RAM_t ram, c8_address_t address, uint8_t byte) {
	if (address >= RAM_SIZE) {
		fprintf(stderr, "Segmentation fault!");
		exit(-1);
	}
	ram[address] = byte;
}


/**
 * Retrieve an instruction from the RAM at a specific address.
 *
 * \param ram The RAM to use.
 * \param address The address to get the instruction from.
 *
 * \return instruction_t The instruction retrieved.
 */
c8_instruction_t ram_get_instruction(RAM_t ram, c8_address_t address) {
	return (ram_get_byte(ram, address) << 8) | ram_get_byte(ram, address + 1);
}

/**
 * Load a ROM file into RAM from current seek position.
 *
 * \param ram The RAM to load into.
 * \param offset The offset to load to.
 * \param rom The ROM file to lead.
 *
 * \return void
 */
void ram_load_rom(RAM_t ram, uint16_t offset, FILE *rom) {
	while ( ! feof(rom) ) {
		if (offset >= RAM_SIZE) {
			fprintf(stderr, "Buffer overflow!");
			exit(-1);
		}
		ram[offset++] = fgetc(rom);
	}
}

/**
 * Reset the state of this CPU.
 *
 * \param cpu This CPU.
 *
 * \return void
 */
void cpu_reset(CPU_t *cpu) {
	cpu->pc = ROM_OFFSET;
	for (int i = 0; i < 16; i++) {
		cpu->v[i] = 0;
	}
	cpu->i = 0;
	cpu->flags.HALT = 0;

	for (int sp = 0; sp < NELEMS(cpu->stack); sp++) {
		cpu->stack[sp] = 0;
	}
	cpu->sp = 0;

	cpu->ram = 0;
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
	for (int f = 0; f < DISPLAY_W; f++)
		printf("-");

	for (int y = 0; y < DISPLAY_H; y++) {
		printf("\n|");
		uint64_t p = display->p[y];
		for (int x = 0; x < DISPLAY_W; x++) {
			printf("%c", p & 0x1 ? '*' : ' ');
			p = p >> 1;
		}
		printf("|");
	}
	printf("\n");

	printf(" ");
	for (int f = 0; f < DISPLAY_W; f++)
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
	for (int p = 0; p < DISPLAY_H; p++)
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
 * Render the display.
 *
 * \param display The display to render.
 *
 * \return void
 */
void display_render(Display_t *display) {
	/** Clear */
	SDL_SetRenderDrawColor(display->renderer, PIXEL_UNSET, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(display->renderer);

	/** Draw */
	for (int y = 0; y < DISPLAY_H; y++) {
		uint64_t p = display->p[y];
		for (int x = 0; x < DISPLAY_W; x++) {
			p & 0x1 ? SDL_SetRenderDrawColor(display->renderer, PIXEL_SET, SDL_ALPHA_OPAQUE)
				: SDL_SetRenderDrawColor(display->renderer, PIXEL_UNSET, SDL_ALPHA_OPAQUE);
			SDL_RenderDrawPoint(display->renderer, x, y);
			p = p >> 1;
		}
	}
	SDL_RenderPresent(display->renderer);
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
	if (cpu->flags.HALT) {
		return;
	}

	if (NULL) {

	} else if (/* 2NNN */ ((instruction >> 12) & 0xf) == 0x2 /* Call NNN */) {
		/** Stack overflow. */
		if (cpu->sp == NELEMS(cpu->stack)) {
			fprintf(stderr, "Stack overlow! HALTING!\n");
			cpu->flags.HALT = 1;
			return;
		}

		cpu->stack[cpu->sp++] = cpu->pc; /** Save return address. */
		cpu->pc = instruction & 0xfff; /** Hop to call */
		return;

	} else if (/* 6XNN */ ((instruction >> 12) & 0xf) == 0x6 /* Set VX to NN */) {	
		cpu->v[instruction >> 8 & 0xf] = instruction & 0xff;

	} else if (/* ANNN */ ((instruction >> 12) & 0xf) == 0xa /* Set I to NNN */) {
		cpu->i = instruction & 0xfff;
	
	} else if (/* DXYN */ ((instruction >> 12) & 0xf) == 0xd /* Draw 8xN sprite at VX VY, set VF to screen set */) {
		bool unset = false;

		uint8_t x = cpu->v[(instruction >> 8) & 0xf];
		uint8_t y = cpu->v[(instruction & 0xf0) >> 4];
		for (uint8_t h = 0; h < (instruction & 0xf); h++) {
			if (display_draw_row(cpu->display, ram_get_byte(cpu->ram, cpu->i + h), x, y - h)) {
				unset = true;
			}
		}
		cpu->v[0xf] = unset ? 1 : 0;
	} else if (/* FX33 */ (((instruction >> 12) & 0xf) == 0xf) && ((instruction & 0xff) == 0x33) /** BCD VX to I */) {
		uint8_t value = cpu->v[instruction >> 8 & 0xf];

		ram_write_byte(cpu->ram, cpu->i, (value / 100) % 10);
		ram_write_byte(cpu->ram, cpu->i + 1, (value / 10) % 10);
		ram_write_byte(cpu->ram, cpu->i + 2, (value / 1) % 10);

	} else if (/* FX65 */ (((instruction >> 12) & 0xf) == 0xf) && ((instruction & 0xff) == 0x65) /* Fill V0 to VX from I */) {
		for (int i = 0; i <= (instruction >> 8 & 0xf); i++) {
			cpu->v[i] = ram_get_byte(cpu->ram, cpu->i + i);
		}

	} else {
		fprintf(stderr, "Unknown instruction %04x! HALTING!\n", instruction);
		cpu->flags.HALT = 1;
		return;
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

	SDL_Init(SDL_INIT_VIDEO);

	CPU_t cpu;
	cpu_reset(&cpu);

	uint8_t _ram[RAM_SIZE] = { 0 };
	RAM_t ram = _ram;

	ram_load_rom(ram, ROM_OFFSET, fopen(argv[1], "rb"));

	Display_t display;
	display_clear(&display);
	display.renderer = SDL_CreateRenderer(
		SDL_CreateWindow("Chip-8 Emulator Project", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, 0),
		-1, 0);
	SDL_RenderSetScale(display.renderer, WINDOW_SCALE, WINDOW_SCALE);

	cpu.ram = ram;
	cpu.display = &display;

	while (true) {
		SDL_Event e;

		if (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT)
				break;
		}

		cpu_execute(&cpu, ram_get_instruction(ram, cpu.pc));
		display_render(&display);

		if (cpu.flags.HALT) break;
	}

	/** Cleanup */
	SDL_DestroyRenderer(display.renderer);
	SDL_Quit();

	return 0;
}

#define TEST_EQUALS(a,b) if (a == b) { printf("."); passed++; } else { printf("F("#a"[%04x] != "#b"[%04x])\n", a, b); failed++; }
#define STRING_LEN_COUNT(s) #s, NELEMS(#s), NELEMS(#s)

int test(int argc, char *argv[]) {
	printf("** Running tests...\n\n");

	int passed = 0;
	int failed = 0;

	uint8_t _ram[RAM_SIZE] = { 0 };
	RAM_t ram = _ram;

	Display_t display;
	display_clear(&display);

	/** Display clear */
	for (uint64_t y = 0; y < DISPLAY_H; y++) {
		TEST_EQUALS((uint32_t)(display.p[y] & 0xffffffff), 0);
		TEST_EQUALS((uint32_t)((display.p[y] >> DISPLAY_H) & 0xffffffff), 0);
	}

	CPU_t cpu;
	cpu_reset(&cpu);

	/** Reset state */
	TEST_EQUALS(cpu.pc, ROM_OFFSET)
	for (int i = 0; i < 16; i++)
		TEST_EQUALS(cpu.v[i], 0)
	TEST_EQUALS(cpu.i, 0)
	TEST_EQUALS(cpu.sp, 0)

	/** 6VNN Set V to NN */
	cpu_execute(&cpu, 0x6001); TEST_EQUALS(cpu.v[0], 0x1)
	cpu_execute(&cpu, 0x6a9f); TEST_EQUALS(cpu.v[0xa], 0x9f)
	cpu_execute(&cpu, 0x6fff); TEST_EQUALS(cpu.v[0xf], 0xff)
	TEST_EQUALS(cpu.pc, 0x206)

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

	/**
	 * 2NNN Call NNN
	 */
	FILE *tmp = tmpfile();
	/** Call function which sets V0 to 0x6 */
	fwrite(STRING_LEN_COUNT(\x61\x00\x22\x04\x60\x06), tmp); fflush(tmp);
	fseek(tmp, 0, SEEK_SET); ram_load_rom(ram, ROM_OFFSET, tmp);

	cpu_reset(&cpu);
	cpu.ram = ram;
	cpu_execute(&cpu, ram_get_instruction(ram, cpu.pc));
	cpu_execute(&cpu, ram_get_instruction(ram, cpu.pc));
	TEST_EQUALS(cpu.stack[0], 0x202);
	TEST_EQUALS(cpu.sp, 0x01);
	TEST_EQUALS(cpu.pc, 0x204);
	cpu_execute(&cpu, ram_get_instruction(ram, cpu.pc));
	TEST_EQUALS(cpu.v[0], 0x06);

	tmp = tmpfile();
	/** Call itself. Stack overflow. */
	fwrite(STRING_LEN_COUNT(\x20\x00), tmp); fflush(tmp);
	fseek(tmp, 0, SEEK_SET); ram_load_rom(ram, 0, tmp);
	cpu_reset(&cpu);
	cpu.pc = 0;
	cpu.ram = ram;
	for (int i = 0; i < NELEMS(cpu.stack); i++)
		cpu_execute(&cpu, ram_get_instruction(ram, cpu.pc));
	cpu_execute(&cpu, ram_get_instruction(ram, cpu.pc));
	TEST_EQUALS(cpu.flags.HALT, 1);

	/**
	 * FX33 BCD from X to I.
	 */
	cpu_reset(&cpu);
	cpu.ram = ram;
	cpu_execute(&cpu, 0x607b );
	cpu_execute(&cpu, 0xf033);
	TEST_EQUALS(cpu.ram[0], 1);
	TEST_EQUALS(cpu.ram[1], 2);
	TEST_EQUALS(cpu.ram[2], 3);

	/**
	 * FX65 Fill V0 to VX from I.
	 */
	cpu_reset(&cpu);
	cpu.ram = ram;
	for (int i = 0; i < 255; i++ )
		ram_write_byte(ram, i, i + 10);
	cpu_execute(&cpu, 0xf165);
	TEST_EQUALS(cpu.v[0], 10);
	TEST_EQUALS(cpu.v[1], 11);
	TEST_EQUALS(cpu.v[2], 0);

	printf("\n%d tests: %d passed, %d failed\n", passed + failed, passed, failed);

	return 0;
}
