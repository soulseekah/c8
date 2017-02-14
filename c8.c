/**
 * \file c8.c
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
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
#define BUILTIN_SPRITES_OFFSET 0x100
#define RAM_SIZE 0x1000
#define INSTRUCTION_LENGTH 2

#define NELEMS(x) (sizeof(x) / sizeof((x)[0]))
#define STRING_LEN_COUNT(s) #s, NELEMS(#s), NELEMS(#s)

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

	/**
	 * Input.
	 */
	uint16_t input;

	/**
	 * Delay timer.
	 */
	uint8_t delay;

	/**
	 * Sound timer.
	 */
	uint8_t sound;
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
void ram_load_rom(RAM_t ram, c8_address_t offset, FILE *rom) {
	while (!feof(rom) ) {
		if (offset >= RAM_SIZE) {
			fprintf(stderr, "Buffer overflow!");
			exit(-1);
		}
		ram[offset++] = fgetc(rom);
	}
}

/**
 * Preload sprites for characters 0-F in RAM.
 *
 * \param ram The RAM to load sprites into.
 * \param offset The offset to load them to.
 *
 * \return void
 */
void ram_load_digit_sprites(RAM_t ram, c8_address_t offset) {
	FILE *sprites = tmpfile();
	fwrite(STRING_LEN_COUNT(\xf0\x90\x90\x90\xf0), sprites); fflush(sprites);
	fwrite(STRING_LEN_COUNT(\x20\x60\x20\x20\x70), sprites); fflush(sprites);
	fwrite(STRING_LEN_COUNT(\xf0\x10\xf0\x80\xf0), sprites); fflush(sprites);
	fseek(sprites, 0, SEEK_SET);
	ram_load_rom(ram, offset, sprites);
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

	cpu->input = 0;

	cpu->ram = 0;

	cpu->delay = 0;
	cpu->sound = 0;
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

	SDL_SetRenderDrawColor(display->renderer, PIXEL_UNSET, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(display->renderer);
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

	SDL_SetRenderDrawColor(display->renderer, PIXEL_UNSET, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(display->renderer);
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

	} else if (/* 00EE */ instruction == 0x00ee /* Return */) {
		if (cpu->sp < 1) {
			fprintf(stderr, "Stack underrun! HALTING!\n");
			cpu->flags.HALT = 1;
			return;
		}
		cpu->pc = cpu->stack[--cpu->sp];

	} else if (/* 1NNN */ ((instruction >> 12) & 0xf) == 0x1 /* Jump to NNN */) {
		cpu->pc = instruction & 0xfff;
		return;

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
	
	} else if (/* 3XNN */ ((instruction >> 12) & 0xf) == 0x3 /* Skip instruction if VX is NN */) {
		if (cpu->v[instruction >> 8 & 0xf] == (instruction & 0xff))
			cpu->pc += INSTRUCTION_LENGTH;
	
	} else if (/* 4XNN */ ((instruction >> 12) & 0xf) == 0x4 /* Skip instruction if VX is not NN */) {
		if (cpu->v[instruction >> 8 & 0xf] != (instruction & 0xff))
			cpu->pc += INSTRUCTION_LENGTH;

	} else if (/* 6XNN */ ((instruction >> 12) & 0xf) == 0x6 /* Set VX to NN */) {	
		cpu->v[instruction >> 8 & 0xf] = instruction & 0xff;

	} else if (/* 7XNN */ ((instruction >> 12) & 0xf) == 0x7/* Add NN to VS */) {
		cpu->v[instruction >> 8 & 0xf] += instruction & 0xff;

	} else if (/* 8XY0 */ ((instruction >> 12) & 0xf) == 0x8 && (instruction & 0xf) == 0x0 /* VX = VY */) {
		cpu->v[instruction >> 8 & 0xf] = cpu->v[(instruction & 0xf0) >> 4];
	} else if (/* 8XY2 */ ((instruction >> 12) & 0xf) == 0x8 && (instruction & 0xf) == 0x2 /* VX = VX & VY */) {
		cpu->v[instruction >> 8 & 0xf] &= cpu->v[(instruction & 0xf0) >> 4];
	} else if (/* 8XY4 */ ((instruction >> 12) & 0xf) == 0x8 && (instruction & 0xf) == 0x4 /* VX = VX + VY, VF carry */) {
		uint8_t carry = cpu->v[instruction >> 8 & 0xf];
		cpu->v[instruction >> 8 & 0xf] += cpu->v[(instruction & 0xf0) >> 4];
		cpu->v[0xf] = carry > cpu->v[instruction >> 8 & 0xf]; /** Overflown */
	} else if (/* 8XY5 */ ((instruction >> 12) & 0xf) == 0x8 && (instruction & 0xf) == 0x5 /* VX = VX - VY, VF borrow */) {
		cpu->v[0xf] = (cpu->v[(instruction & 0xf0) >> 4] > cpu->v[instruction >> 8 & 0xf]); /** Borrow */
		cpu->v[instruction >> 8 & 0xf] -= cpu->v[(instruction & 0xf0) >> 4];
		
	} else if (/* ANNN */ ((instruction >> 12) & 0xf) == 0xa /* Set I to NNN */) {
		cpu->i = instruction & 0xfff;

	} else if (/* CXNN */ ((instruction >> 12) & 0xf) == 0xc /* Set VX to a random number masked with NN */) {
		cpu->v[instruction >> 8 & 0xf] = (rand() & (instruction & 0xff)) & 0xff;
	
	} else if (/* DXYN */ ((instruction >> 12) & 0xf) == 0xd /* Draw 8xN sprite at VX VY, set VF to screen set */) {
		bool unset = false;

		uint8_t x = cpu->v[(instruction >> 8) & 0xf];
		uint8_t y = cpu->v[(instruction & 0xf0) >> 4];
		for (uint8_t h = 0; h < (instruction & 0xf); h++) {
			if (display_draw_row(cpu->display, ram_get_byte(cpu->ram, cpu->i + h), x, y + h)) {
				unset = true;
			}
		}
		cpu->v[0xf] = unset ? 1 : 0;
		display_render(cpu->display);

	} else if (/* EXA1 */ (((instruction >> 12) & 0xf) == 0xe) && ((instruction & 0xff) == 0xa1) /* Skip instruction if key VX is not pressed */) {
		if (!(cpu->input & cpu->v[instruction >> 8 & 0xf]))
			cpu->pc += INSTRUCTION_LENGTH;
		printf("pressed: %x\n", cpu->input);
		
	} else if (/* FX07 */ (((instruction >> 12) & 0xf) == 0xf) && ((instruction & 0xff) == 0x07) /* Read delay timer to VX */) {
		cpu->v[instruction >> 8 & 0xf] = cpu->delay;

	} else if (/* FX15 */ (((instruction >> 12) & 0xf) == 0xf) && ((instruction & 0xff) == 0x15) /* Delay timer to VX */) {
		cpu->delay = cpu->v[instruction >> 8 & 0xf];

	} else if (/* FX18 */ (((instruction >> 12) & 0xf) == 0xf) && ((instruction & 0xff) == 0x18) /* Sound timer to VX */) {
		cpu->sound = cpu->v[instruction >> 8 & 0xf];

	} else if (/* FX29 */ (((instruction >> 12) & 0xf) == 0xf) && ((instruction & 0xff) == 0x29) /* Set I to sprite in digit VX */) {
		cpu->i = BUILTIN_SPRITES_OFFSET + (cpu->v[instruction >> 8 & 0xf] * 6);

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
	cpu->pc += INSTRUCTION_LENGTH;
}

/**
 * Decrement the CPU timers.
 *
 * \param cpu The CPU that ticked.
 *
 * \return void
 */
void cpu_timer_tick(CPU_t *cpu) {
	if (cpu->delay) cpu->delay--;
	if (cpu->sound) cpu->sound--;
}


/**
 * Poll input and write state to CPU.
 *
 * \param cpu The CPU to write state to.
 * \param keys The SDL keys array.
 *
 * \return void
 */
void cpu_poll_keystate(CPU_t *cpu, const uint8_t *keys) {
	/** Setup keypresses */
	cpu->input = ( 0
		/** Map your own keys https://wiki.libsdl.org/SDL_Scancode */
		| ((keys[SDL_SCANCODE_Z]) ? 1 << 0x0 : 0)
		| ((keys[SDL_SCANCODE_X]) ? 1 << 0x1 : 0)
		| ((keys[SDL_SCANCODE_C]) ? 1 << 0x2 : 0)
		| ((keys[SDL_SCANCODE_V]) ? 1 << 0x3 : 0)
		| ((keys[SDL_SCANCODE_A]) ? 1 << 0x4 : 0)
		| ((keys[SDL_SCANCODE_S]) ? 1 << 0x5 : 0)
		| ((keys[SDL_SCANCODE_D]) ? 1 << 0x6 : 0)
		| ((keys[SDL_SCANCODE_F]) ? 1 << 0x7 : 0)
		| ((keys[SDL_SCANCODE_Q]) ? 1 << 0x8 : 0)
		| ((keys[SDL_SCANCODE_W]) ? 1 << 0x9 : 0)
		| ((keys[SDL_SCANCODE_E]) ? 1 << 0xa : 0)
		| ((keys[SDL_SCANCODE_R]) ? 1 << 0xb : 0)
		| ((keys[SDL_SCANCODE_1]) ? 1 << 0xc : 0)
		| ((keys[SDL_SCANCODE_2]) ? 1 << 0xd : 0)
		| ((keys[SDL_SCANCODE_3]) ? 1 << 0xe : 0)
		| ((keys[SDL_SCANCODE_4]) ? 1 << 0xf : 0)
	);
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
	ram_load_digit_sprites(ram, BUILTIN_SPRITES_OFFSET);

	Display_t display;
	display.renderer = SDL_CreateRenderer(
		SDL_CreateWindow("Chip-8 Emulator Project", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, 0),
		-1, 0);
	SDL_RenderSetScale(display.renderer, WINDOW_SCALE, WINDOW_SCALE);
	display_clear(&display);

	const uint8_t *keys = SDL_GetKeyboardState(NULL);

	cpu.ram = ram;
	cpu.display = &display;

	srand(time(NULL));

	/** Tick-tock */
	uint64_t cycles = 0;
	while (true) {
		SDL_Event e;

		if (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT)
				break;
		}

		cpu_poll_keystate(&cpu, keys);
		cpu_execute(&cpu, ram_get_instruction(ram, cpu.pc));

		if (cpu.flags.HALT) break;

		SDL_Delay(2); /* ~ 520Hz */
		if ((++cycles % 8 /* ~60 Hz */) == 0) {
			cpu_timer_tick(&cpu);
		}
	}

	/** Cleanup */
	SDL_DestroyRenderer(display.renderer);
	SDL_Quit();

	return 0;
}

#define TEST_EQUALS(a,b) if (a == b) { printf("."); passed++; } else { printf("F("#a"[%04x] != "#b"[%04x])\n", a, b); failed++; }

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

	/** 6XNN Set VX to NN */
	cpu_execute(&cpu, 0x6001); TEST_EQUALS(cpu.v[0], 0x1)
	cpu_execute(&cpu, 0x6a9f); TEST_EQUALS(cpu.v[0xa], 0x9f)
	cpu_execute(&cpu, 0x6fff); TEST_EQUALS(cpu.v[0xf], 0xff)
	TEST_EQUALS(cpu.pc, 0x206)

	/** 7XNN Add NN to VX */
	cpu_execute(&cpu, 0x7001); TEST_EQUALS(cpu.v[0], 0x2)

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
	fwrite(STRING_LEN_COUNT(\x61\x00\x22\x04\x60\x06\x00\xee), tmp); fflush(tmp);
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
	cpu_execute(&cpu, ram_get_instruction(ram, cpu.pc));
	TEST_EQUALS(cpu.pc, 0x204);
	TEST_EQUALS(cpu.sp, 0x00);

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
	 * 8XY2 VX = VX & VY.
	 */
	cpu_reset(&cpu);
	cpu.v[0] = 7;
	cpu.v[1] = 3;
	cpu_execute(&cpu, 0x8012);
	TEST_EQUALS(cpu.v[0], 3);

	/**
	 * 8XY4 VX = VX + VY, VF carry.
	 */
	cpu_execute(&cpu, 0x8014);
	TEST_EQUALS(cpu.v[0], 6);
	TEST_EQUALS(cpu.v[0xf], 0);
	cpu.v[1] = 0xfe;
	cpu_execute(&cpu, 0x8014);
	TEST_EQUALS(cpu.v[0], 4);
	TEST_EQUALS(cpu.v[0xf], 1);

	/**
	 * 8XY0 VX = VY.
	 */
	cpu_execute(&cpu, 0x8100);
	TEST_EQUALS(cpu.v[1], 4);

	/**
	 * 8XY5 VX = VX - VY, VF borrow.
	 */
	cpu_execute(&cpu, 0x8015);
	TEST_EQUALS(cpu.v[0], 0);
	TEST_EQUALS(cpu.v[0xf], 0);
	cpu_execute(&cpu, 0x8015);
	TEST_EQUALS(cpu.v[0xf], 1);
	TEST_EQUALS(cpu.v[0], 0xfc);

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
