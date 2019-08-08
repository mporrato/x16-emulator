#include "video.h"
#include "memory.h"
#include "ps2.h"
#include "glue.h"

#define ESC_IS_BREAK /* if enabled, Esc sends Break/Pause key instead of Esc */

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

#define SCREEN_RAM_OFFSET 0x00000

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *sdlTexture;

static uint8_t video_ram[0x20000];
static uint8_t chargen_rom[4096];
static uint8_t palette[256 * 2];

// I/O registers
static uint32_t io_addr[2];
static uint8_t io_inc[2];
bool io_addrsel;

static uint8_t layer_registers[2][16];

static uint8_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT * 4];

static const uint16_t default_palette[] = {
0x0000,0xfff,0x800,0xafe,0xc4c,0x0c5,0x00a,0xee7,0xd85,0x640,0xf77,0x333,0x777,0xaf6,0x08f,0xbbb,0x000,0x111,0x222,0x333,0x444,0x555,0x666,0x777,0x888,0x999,0xaaa,0xbbb,0xccc,0xddd,0xeee,0xfff,0x211,0x433,0x644,0x866,0xa88,0xc99,0xfbb,0x211,0x422,0x633,0x844,0xa55,0xc66,0xf77,0x200,0x411,0x611,0x822,0xa22,0xc33,0xf33,0x200,0x400,0x600,0x800,0xa00,0xc00,0xf00,0x221,0x443,0x664,0x886,0xaa8,0xcc9,0xfeb,0x211,0x432,0x653,0x874,0xa95,0xcb6,0xfd7,0x210,0x431,0x651,0x862,0xa82,0xca3,0xfc3,0x210,0x430,0x640,0x860,0xa80,0xc90,0xfb0,0x121,0x343,0x564,0x786,0x9a8,0xbc9,0xdfb,0x121,0x342,0x463,0x684,0x8a5,0x9c6,0xbf7,0x120,0x241,0x461,0x582,0x6a2,0x8c3,0x9f3,0x120,0x240,0x360,0x480,0x5a0,0x6c0,0x7f0,0x121,0x343,0x465,0x686,0x8a8,0x9ca,0xbfc,0x121,0x242,0x364,0x485,0x5a6,0x6c8,0x7f9,0x020,0x141,0x162,0x283,0x2a4,0x3c5,0x3f6,0x020,0x041,0x061,0x082,0x0a2,0x0c3,0x0f3,0x122,0x344,0x466,0x688,0x8aa,0x9cc,0xbff,0x122,0x244,0x366,0x488,0x5aa,0x6cc,0x7ff,0x022,0x144,0x166,0x288,0x2aa,0x3cc,0x3ff,0x022,0x044,0x066,0x088,0x0aa,0x0cc,0x0ff,0x112,0x334,0x456,0x668,0x88a,0x9ac,0xbcf,0x112,0x224,0x346,0x458,0x56a,0x68c,0x79f,0x002,0x114,0x126,0x238,0x24a,0x35c,0x36f,0x002,0x014,0x016,0x028,0x02a,0x03c,0x03f,0x112,0x334,0x546,0x768,0x98a,0xb9c,0xdbf,0x112,0x324,0x436,0x648,0x85a,0x96c,0xb7f,0x102,0x214,0x416,0x528,0x62a,0x83c,0x93f,0x102,0x204,0x306,0x408,0x50a,0x60c,0x70f,0x212,0x434,0x646,0x868,0xa8a,0xc9c,0xfbe,0x211,0x423,0x635,0x847,0xa59,0xc6b,0xf7d,0x201,0x413,0x615,0x826,0xa28,0xc3a,0xf3c,0x201,0x403,0x604,0x806,0xa08,0xc09,0xf0b
};

static uint8_t video_ram_read(uint32_t address);

static void
video_reset()
{
	// init I/O registers
	memset(io_addr, 0, sizeof(io_addr));
	memset(io_inc, 0, sizeof(io_inc));
	io_addrsel = 0;

	// init Layer registers
	memset(layer_registers, 0, sizeof(layer_registers));
	uint32_t tile_base = 0x20000; // uppercase PETSCII
	layer_registers[0][0] = 1; // enabled
	layer_registers[0][4] = tile_base >> 2;
	layer_registers[0][5] = tile_base >> 10;

	// copy palette
	memcpy(palette, default_palette, sizeof(palette));
	for (int i = 0; i < 256; i++) {
		palette[i * 2 + 0] = default_palette[i] & 0xff;
		palette[i * 2 + 1] = default_palette[i] >> 8;
	}
}

bool
video_init(uint8_t *in_chargen)
{
	// copy chargen
	memcpy(chargen_rom, in_chargen, sizeof(chargen_rom));

	video_reset();

	SDL_Init(SDL_INIT_VIDEO);
	SDL_CreateWindowAndRenderer(SCREEN_WIDTH, SCREEN_HEIGHT, 0, &window, &renderer);

	sdlTexture = SDL_CreateTexture(renderer,
				       SDL_PIXELFORMAT_RGB888,
				       SDL_TEXTUREACCESS_STREAMING,
				       SCREEN_WIDTH, SCREEN_HEIGHT);

	return true;
}

int
ps2_scancode_from_SDLKey(SDL_Keycode k)
{
	switch (k) {
		case SDLK_BACKSPACE:
			return 0x66;
		case SDLK_TAB:
			return 0xd;
		case SDLK_CLEAR:
			return 0;
		case SDLK_RETURN:
			return 0x5a;
		case SDLK_PAUSE:
			return 0;
		case SDLK_ESCAPE:
#ifdef ESC_IS_BREAK
			return 0xff;
#else
			return 0x76;
#endif
		case SDLK_SPACE:
			return 0x29;
		case SDLK_EXCLAIM:
			return 0x16;
		case SDLK_QUOTEDBL:
			return 0x52;
		case SDLK_HASH:
			return 0x26;
		case SDLK_DOLLAR:
			return 0x25;
		case SDLK_AMPERSAND:
			return 0x3d;
		case SDLK_QUOTE:
			return 0x52;
		case SDLK_LEFTPAREN:
			return 0x46;
		case SDLK_RIGHTPAREN:
			return 0x45;
		case SDLK_ASTERISK:
			return 0x3e;
		case SDLK_PLUS:
			return 0x55;
		case SDLK_COMMA:
			return 0x41;
		case SDLK_MINUS:
			return 0x4e;
		case SDLK_PERIOD:
			return 0x49;
		case SDLK_SLASH:
			return 0x4a;
		case SDLK_0:
			return 0x45;
		case SDLK_1:
			return 0x16;
		case SDLK_2:
			return 0x1e;
		case SDLK_3:
			return 0x26;
		case SDLK_4:
			return 0x25;
		case SDLK_5:
			return 0x2e;
		case SDLK_6:
			return 0x36;
		case SDLK_7:
			return 0x3d;
		case SDLK_8:
			return 0x3e;
		case SDLK_9:
			return 0x46;
		case SDLK_COLON:
			return 0x4c;
		case SDLK_SEMICOLON:
			return 0x4c;
		case SDLK_LESS:
			return 0x41;
		case SDLK_EQUALS:
			return 0x55;
		case SDLK_GREATER:
			return 0x49;
		case SDLK_QUESTION:
			return 0x4a;
		case SDLK_AT:
			return 0x1e;
		case SDLK_LEFTBRACKET:
			return 0x54;
		case SDLK_BACKSLASH:
			return 0x5d;
		case SDLK_RIGHTBRACKET:
			return 0x5b;
		case SDLK_CARET:
			return 0x36;
		case SDLK_UNDERSCORE:
			return 0x4e;
		case SDLK_BACKQUOTE:
			return 0xe;
		case SDLK_a:
			return 0x1c;
		case SDLK_b:
			return 0x32;
		case SDLK_c:
			return 0x21;
		case SDLK_d:
			return 0x23;
		case SDLK_e:
			return 0x24;
		case SDLK_f:
			return 0x2b;
		case SDLK_g:
			return 0x34;
		case SDLK_h:
			return 0x33;
		case SDLK_i:
			return 0x43;
		case SDLK_j:
			return 0x3B;
		case SDLK_k:
			return 0x42;
		case SDLK_l:
			return 0x4B;
		case SDLK_m:
			return 0x3A;
		case SDLK_n:
			return 0x31;
		case SDLK_o:
			return 0x44;
		case SDLK_p:
			return 0x4D;
		case SDLK_q:
			return 0x15;
		case SDLK_r:
			return 0x2D;
		case SDLK_s:
			return 0x1B;
		case SDLK_t:
			return 0x2C;
		case SDLK_u:
			return 0x3C;
		case SDLK_v:
			return 0x2A;
		case SDLK_w:
			return 0x1D;
		case SDLK_x:
			return 0x22;
		case SDLK_y:
			return 0x35;
		case SDLK_z:
			return 0x1A;
		case SDLK_DELETE:
			return 0;
		case SDLK_UP:
			return 0x75 | 0x80;
		case SDLK_DOWN:
			return 0x72 | 0x80;
		case SDLK_RIGHT:
			return 0x74 | 0x80;
		case SDLK_LEFT:
			return 0x6b | 0x80;
		case SDLK_INSERT:
			return 0;
		case SDLK_HOME:
			return 0x6c | 0x80;
		case SDLK_END:
			return 0;
		case SDLK_PAGEUP:
			return 0;
		case SDLK_PAGEDOWN:
			return 0;
		case SDLK_F1:
			return 0x05;
		case SDLK_F2:
			return 0x06;
		case SDLK_F3:
			return 0x04;
		case SDLK_F4:
			return 0x0c;
		case SDLK_F5:
			return 0x03;
		case SDLK_F6:
			return 0x0b;
		case SDLK_F7:
			return 0x83; // XXX the MSB clashes with the "extended" flag!
		case SDLK_F8:
			return 0x0a;
		case SDLK_F9:
			return 0;
		case SDLK_F10:
			return 0;
		case SDLK_F11:
			return 0;
		case SDLK_F12:
			return 0;
		case SDLK_F13:
			return 0;
		case SDLK_F14:
			return 0;
		case SDLK_F15:
			return 0;
		case SDLK_RSHIFT:
			return 0x59;
		case SDLK_LSHIFT:
			return 0x12;
		case SDLK_LCTRL:
			return 0x14;
		case SDLK_RCTRL:
			return 0x14 | 0x80;
		case SDLK_LALT:
			return 0x11;
//		case SDLK_LGUI: // Windows/Command
//			return 0x5b | 0x80;
		default:
			return 0;
	}
}

uint8_t
get_pixel(uint8_t layer, uint16_t x, uint16_t y)
{
	uint8_t enabled = layer_registers[layer][0] & 1;
	if (!enabled) {
		return 0; // transparent
	}

	uint8_t mode = layer_registers[layer][0] >> 5;
	uint32_t map_base = layer_registers[layer][2] << 2 | layer_registers[layer][3] << 10;
	uint32_t tile_base = layer_registers[layer][4] << 2 | layer_registers[layer][5] << 10;

	bool text_mode = mode == 0 || mode == 1;
	bool tile_mode = mode == 2 || mode == 3 || mode == 4;
	bool bitmap_mode = mode == 5 || mode == 6 || mode == 7;

	uint16_t mapw;
	uint16_t maph;
	uint16_t tilew = 0;
	uint16_t tileh = 0;

	if (tile_mode || text_mode) {
		mapw = 1 << ((layer_registers[layer][1] & 3) + 5);
		maph = 1 << (((layer_registers[layer][1] >> 2) & 3) + 5);
		if (tile_mode) {
			tilew = 1 << (((layer_registers[layer][1] >> 4) & 1) + 3);
			tileh = 1 << (((layer_registers[layer][1] >> 5) & 1) + 3);
		} else {
			tilew = 8;
			tileh = 8;
		}
	} else if (bitmap_mode) {
		// bitmap mode is basically tiled mode with a single huge tile
		tilew = SCREEN_WIDTH;
		tileh = SCREEN_HEIGHT;
	}

	uint8_t hscale = ((layer_registers[layer][0] >> 1) & 3) + 1;
	uint8_t vscale = ((layer_registers[layer][0] >> 3) & 3) + 1;

	int eff_x = x / hscale;
	int eff_y = y / vscale;

	// Scrolling
	if (!bitmap_mode) {
		uint16_t hscroll = layer_registers[layer][6] | (layer_registers[layer][7] & 0xf) << 8;
		uint16_t vscroll = layer_registers[layer][8] | (layer_registers[layer][9] & 0xf) << 8;

		eff_x = (eff_x + hscroll) % (mapw * tilew);
		eff_y = (eff_y + vscroll) % (maph * tileh);
	}

	int xx = eff_x % tilew;
	int yy = eff_y % tileh;

	uint16_t tile_index = 0;
	uint8_t fg_color = 0;
	uint8_t bg_color = 0;
	uint8_t palette_offset = 0;

	// extract all information from the map
	if (bitmap_mode) {
		tile_index = 0;
		palette_offset = layer_registers[layer][7] & 0xf;
	} else {
		uint32_t map_addr = map_base + (eff_y / tileh * mapw + eff_x / tilew) * 2;
		uint8_t byte0 = video_ram_read(map_addr);
		uint8_t byte1 = video_ram_read(map_addr + 1);
		if (text_mode) {
			tile_index = byte0;

			if (mode == 0) {
				fg_color = byte1 & 15;
				bg_color = byte1 >> 4;
			} else {
				fg_color = byte1;
				bg_color = 0;
			}
		} else if (tile_mode) {
			tile_index = byte0 | ((byte1 & 3) << 8);

			// Tile Flipping
			bool vflip = (byte1 >> 3) & 1;
			bool hflip = (byte1 >> 2) & 1;
			if (vflip) {
				yy = yy ^ (tileh - 1);
			}
			if (hflip) {
				xx = xx ^ (tilew - 1);
			}

			palette_offset = byte1 >> 4;
		}
	}

	uint8_t bits_per_pixel = 0;
	if (mode == 0 || mode == 1) {
		bits_per_pixel = 1;
	} else if (mode == 2 || mode == 5) {
		bits_per_pixel = 2;
	} else if (mode == 3 || mode == 6) {
		bits_per_pixel = 4;
	} else if (mode == 4 || mode == 7) {
		bits_per_pixel = 8;
	}

	uint32_t tile_size = (tilew * bits_per_pixel * tileh) >> 3;
	// offset within tilemap of the current tile
	uint32_t tile_start = tile_index * tile_size;
	// additional bytes to reach the correct line of the tile
	uint32_t y_add;
	if (bitmap_mode) {
		uint8_t bm_stride = layer_registers[layer][6];
		y_add = yy * bm_stride * 4;
	} else {
		y_add = (yy * tilew * bits_per_pixel) >> 3;
	}
	// additional bytes to reach the correct column of the tile
	uint16_t x_add = (xx * bits_per_pixel) >> 3;
	uint32_t tile_offset = tile_start + y_add + x_add;
	uint8_t s = video_ram_read(tile_base + tile_offset);

	// convert tile byte to indexed color
	uint8_t col_index = 0;
	if (bits_per_pixel == 1) {
		bool bit = (s >> (7 - xx)) & 1;
		col_index = bit ? fg_color : bg_color;
	} else if (bits_per_pixel == 2) {
		col_index = (s >> (6 - (xx << 1))) & 3;
	} else if (bits_per_pixel == 4) {
		col_index = (s >> (4 - (xx << 2))) & 0xf;
	} else if (bits_per_pixel == 8) {
		col_index = s;
	}

	// Apply Palette Offset
	if (palette_offset && col_index > 0 && col_index < 16) {
		col_index += palette_offset << 4;
	}

	return col_index;
}

bool
video_update()
{
	for (int y = 0; y < SCREEN_HEIGHT; y++) {
		for (int x = 0; x < SCREEN_WIDTH; x++) {

			uint8_t col_index = get_pixel(1, x, y);
			if (col_index == 0) { // Layer 2 is transparent
				col_index = get_pixel(0, x, y);
			}

			uint16_t entry = palette[col_index * 2] | palette[col_index * 2 + 1] << 8;
			int fbi = (y * SCREEN_WIDTH + x) * 4;
			framebuffer[fbi + 0] = (entry & 0xf) << 4;
			framebuffer[fbi + 1] = ((entry >> 4) & 0xf) << 4;
			framebuffer[fbi + 2] = ((entry >> 8) & 0xf) << 4;
		}
	}

	SDL_UpdateTexture(sdlTexture, NULL, framebuffer, SCREEN_WIDTH * 4);
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, sdlTexture, NULL, NULL);
	SDL_RenderPresent(renderer);

	static bool cmd_down = false;
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) {
			return false;
		}
		if (event.type == SDL_KEYDOWN) {
			if (event.key.keysym.sym == SDLK_LGUI) { // Windows/Command
				cmd_down = true;
			} else if (cmd_down && event.key.keysym.sym == SDLK_s) {
				memory_save();
			} else {
	//			printf("DOWN 0x%02x\n", event.key.keysym.sym);
				int scancode = ps2_scancode_from_SDLKey(event.key.keysym.sym);
				if (scancode == 0xff) {
					// "Pause/Break" sequence
					kbd_buffer_add(0xe1);
					kbd_buffer_add(0x14);
					kbd_buffer_add(0x77);
					kbd_buffer_add(0xe1);
					kbd_buffer_add(0xf0);
					kbd_buffer_add(0x14);
					kbd_buffer_add(0xf0);
					kbd_buffer_add(0x77);
				} else {
					if (scancode & 0x80) {
						kbd_buffer_add(0xe0);
					}
					kbd_buffer_add(scancode & 0x7f);
				}
			}
			return true;
		}
		if (event.type == SDL_KEYUP) {
			if (event.key.keysym.sym == SDLK_LGUI) { // Windows/Command
				cmd_down = false;
			} else {
	//			printf("UP   0x%02x\n", event.key.keysym.sym);
				kbd_buffer_add(0xf0); // BREAK
				kbd_buffer_add(ps2_scancode_from_SDLKey(event.key.keysym.sym));
			}
			return true;
		}
	}
	return true;
}

void
video_end()
{
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}

uint32_t
get_and_inc_address(uint8_t sel)
{
	uint32_t address = io_addr[sel];
	io_addr[sel] += io_inc[sel];
//	printf("address = %06x, new = %06x\n", address, io_addr[sel]);
	return address;
}

//
// Vera: Layer Registers
//

static uint8_t
video_layer_reg_read(uint8_t layer, uint8_t reg)
{
	return layer_registers[layer][reg];
}

void
video_layer_reg_write(uint8_t layer, uint8_t reg, uint8_t value)
{
	layer_registers[layer][reg] = value;
}

//
// Vera: Internal Video Address Space
//

static uint8_t
video_ram_read(uint32_t address)
{
	if (address < 0x20000) {
		return video_ram[address];
	} else if (address < 0x21000) {
		return chargen_rom[address & 0xfff];
	} else if (address < 0x40000) {
		return 0xFF; // unassigned
	} else if (address < 0x40010) {
		return video_layer_reg_read(0, address & 0xf);
	} else if (address < 0x40020) {
		return video_layer_reg_read(1, address & 0xf);
	} else if (address < 0x40200) {
		return 0xFF; // unassigned
	} else if (address < 0x40400) {
		return palette[address & 0x1ff];
	} else {
		return 0xFF; // unassigned
	}
}

void
video_ram_write(uint32_t address, uint8_t value)
{
	if (address < 0x20000) {
		video_ram[address] = value;
	} else if (address < 0x21000) {
		// ROM, do nothing
	} else if (address < 0x40000) {
		// unassigned, do nothing
	} else if (address < 0x40010) {
		video_layer_reg_write(0, address & 0xf, value);
	} else if (address < 0x40020) {
		video_layer_reg_write(1, address & 0xf, value);
	} else if (address < 0x40200) {
		// unassigned, do nothing
	} else if (address < 0x40400) {
		palette[address & 0x1ff] = value;
	} else {
		// unassigned, do nothing
	}
}

//
// Vera: 6502 I/O Interface
//

uint8_t
video_read(uint8_t reg)
{
	switch (reg) {
		case 0:
			return (io_addr[io_addrsel] >> 16) | (io_inc[io_addrsel] << 4);
		case 1:
			return (io_addr[io_addrsel] >> 8) & 0xff;
		case 2:
			return io_addr[io_addrsel] & 0xff;
		case 3:
		case 4: {
			uint32_t address = get_and_inc_address(reg - 3);
//			printf("READ  video_ram[$%x] = $%02x\n", address, video_ram[address]);
			return video_ram_read(address);
		case 5:
			return io_addrsel;
		default:
			return 0;
		}
	}
}

void
video_write(uint8_t reg, uint8_t value)
{
//	printf("ioregisters[%d] = $%02x\n", reg, value);
	switch (reg) {
		case 0:
			io_addr[io_addrsel] = (io_addr[io_addrsel] & 0x0ffff) | ((value & 0xf) << 16);
			io_inc[io_addrsel] = value >> 4;
			break;
		case 1:
			io_addr[io_addrsel] = (io_addr[io_addrsel] & 0xf00ff) | (value << 8);
			break;
		case 2:
			io_addr[io_addrsel] = (io_addr[io_addrsel] & 0xfff00) | value;
			break;
		case 3:
		case 4: {
			uint32_t address = get_and_inc_address(reg - 3);
//			printf("WRITE video_ram[$%x] = $%02x\n", address, value);
			video_ram_write(address, value);
			break;
		case 5:
			if (value & 0x80) {
				video_reset();
			}
			io_addrsel = value  & 1;
			break;
		}
	}
}
