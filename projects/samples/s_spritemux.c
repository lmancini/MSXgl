// ____________________________
// ██▀▀█▀▀██▀▀▀▀▀▀▀█▀▀█        │
// ██  ▀  █▄  ▀██▄ ▀ ▄█ ▄▀▀ █  │  Sprite Multiplexer sample
// █  █ █  ▀▀  ▄█  █  █ ▀▄█ █▄ │
// ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀────────┘
//  by Guillaume 'Aoineko' Blanchard under CC BY-SA license
//─────────────────────────────────────────────────────────────────────────────
// Sprite multiplexer module sample
//
// 12 balls and a "player" sprite moved with the arrow keys. Two scenes:
// - Row: the balls share the same horizontal lines (the VDP can only display
//   4 of them)
// - Bounce: the balls fly across the screen and bounce on the borders
//
// F1: Change ordering strategy (None, Swap, Rotate)
// F2: Pin/unpin the player sprite
// F3: Change scene (Row, Bounce)
// Arrows: Move the player sprite
// ESC: Exit
//─────────────────────────────────────────────────────────────────────────────

//=============================================================================
// INCLUDES
//=============================================================================
#include "msxgl.h"
#include "sprite_mux.h"

//=============================================================================
// DEFINES
//=============================================================================

// VRAM layout (default Graphic 2 layout)
#define VRAM_LAYOUT_TABLE		0x1800
#define VRAM_COLOR_TABLE		0x2000
#define VRAM_PATTERN_TABLE		0x0000
#define VRAM_SPRITE_PATTERN		0x3800
#define VRAM_SPRITE_ATTRIBUTE	0x1B00

// Logical sprites
#define PLAYER_ID				0
#define ROW_FIRST				1
#define ROW_NUM					12
#define ROW_Y					95

// Play area (sprite coordinates, 16x16 sprites)
#define AREA_MIN_X				0
#define AREA_MAX_X				240 // 256 - 16
#define AREA_MIN_Y				16
#define AREA_MAX_Y				128 // 144 - 16

// Scenes
enum SCENE
{
	SCENE_ROW = 0,
	SCENE_BOUNCE,
	SCENE_MAX,
};

// Sprite patterns (16x16 mode: 4 pattern indexes per shape)
#define PATTERN_BALL			0
#define PATTERN_PLAYER			4

//=============================================================================
// READ-ONLY DATA
//=============================================================================

// Fonts
#include "font/font_mgl_sample8.h"

// 16x16 ball (quadrants order: top-left, bottom-left, top-right, bottom-right)
const u8 g_SprtBall[32] =
{
	0x07, 0x1F, 0x3F, 0x7F, 0x7F, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0x7F, 0x7F, 0x3F, 0x1F, 0x07,
	0xE0, 0xF8, 0xFC, 0xFE, 0xFE, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFE, 0xFE, 0xFC, 0xF8, 0xE0,
};

// 16x16 hollow square for the player
const u8 g_SprtPlayer[32] =
{
	0xFF, 0xFF, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,
	0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFF, 0xFF,
	0xFF, 0xFF, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0xFF, 0xFF,
};

// Ordering strategy names
const c8* const g_ModeName[SPRITEMUX_MODE_MAX] = { "None  ", "Swap  ", "Rotate" };

// Scene names
const c8* const g_SceneName[SCENE_MAX] = { "Row   ", "Bounce" };

// Initial speed of the balls in the bounce scene
const i8 g_BallSpeed[ROW_NUM][2] =
{
	{  1,  1 }, { -2,  1 }, {  1, -2 }, { -1, -1 },
	{  2,  2 }, { -1,  2 }, {  2, -1 }, { -2, -2 },
	{  1,  2 }, { -2, -1 }, {  2,  1 }, { -1, -2 },
};

// Ball structure
typedef struct Ball
{
	u8 X;
	u8 Y;
	i8 DX;
	i8 DY;
} Ball;

//=============================================================================
// MEMORY DATA
//=============================================================================

u8 g_VBlank = 0;
u8 g_PlayerX = 120;
u8 g_PlayerY = 140;
bool g_Pinned = TRUE;
u8 g_Scene = SCENE_ROW;
u8 g_Frame = 0;
Ball g_Ball[ROW_NUM];

//=============================================================================
// FUNCTIONS
//=============================================================================

//-----------------------------------------------------------------------------
// H_TIMI hook
void VBlankHook()
{
	g_VBlank = 1;
}

//-----------------------------------------------------------------------------
// Wait for V-Blank period
void WaitVBlank()
{
	while (g_VBlank == 0) {}
	g_VBlank = 0;
}

//-----------------------------------------------------------------------------
// Display current settings
void DisplayStatus()
{
	Print_SetPosition(12, 20);
	Print_DrawText(g_ModeName[SpriteMux_GetMode()]);
	Print_SetPosition(12, 21);
	Print_DrawText(g_Pinned ? "Yes" : "No ");
	Print_SetPosition(12, 22);
	Print_DrawText(g_SceneName[g_Scene]);
}

//-----------------------------------------------------------------------------
// Initialize the balls of the current scene
void InitScene()
{
	loop(i, ROW_NUM)
	{
		Ball* ball = &g_Ball[i];
		ball->X = 32 + i * 16;
		ball->Y = ROW_Y;
		ball->DX = g_BallSpeed[i][0];
		ball->DY = g_BallSpeed[i][1];
	}
}

//-----------------------------------------------------------------------------
// Move a coordinate and bounce it between low and high limits
u8 MoveAndBounce(u8 pos, i8* speed, u8 low, u8 high)
{
	i16 next = (i16)pos + *speed;
	if (next < low)
	{
		next = low;
		*speed = -*speed;
	}
	else if (next > high)
	{
		next = high;
		*speed = -*speed;
	}
	return (u8)next;
}

//-----------------------------------------------------------------------------
// Update the balls of the current scene
void UpdateScene()
{
	loop(i, ROW_NUM)
	{
		Ball* ball = &g_Ball[i];
		if (g_Scene == SCENE_ROW)
		{
			// Make the row wave a little so balls overlap different lines
			ball->Y = ROW_Y;
			if (((g_Frame >> 3) + i) & 1)
				ball->Y += 2;
		}
		else // if (g_Scene == SCENE_BOUNCE)
		{
			ball->X = MoveAndBounce(ball->X, &ball->DX, AREA_MIN_X, AREA_MAX_X);
			ball->Y = MoveAndBounce(ball->Y, &ball->DY, AREA_MIN_Y, AREA_MAX_Y);
		}
		SpriteMux_SetPosition(ROW_FIRST + i, ball->X, ball->Y);
	}
}

//-----------------------------------------------------------------------------
// Program entry point
void main()
{
	// Setup screen
	VDP_SetMode(VDP_MODE_GRAPHIC2);
	VDP_SetColor(0x44);
	VDP_SetLayoutTable(VRAM_LAYOUT_TABLE);
	VDP_SetColorTable(VRAM_COLOR_TABLE);
	VDP_SetPatternTable(VRAM_PATTERN_TABLE);
	VDP_SetSpritePatternTable(VRAM_SPRITE_PATTERN);
	VDP_SetSpriteAttributeTable(VRAM_SPRITE_ATTRIBUTE);
	VDP_FillVRAM_16K(0, 0x0000, 0x4000); // Clear 16 KB of VRAM
	VDP_SetSpriteFlag(VDP_SPRITE_SIZE_16);

	// Setup print
	Print_SetTextFont(g_Font_MGL_Sample8, 0);
	Print_SetColor(0xF, 0x1);
	Print_SetPosition(0, 0);
	Print_DrawText("SPRITE MULTIPLEXER\n");
	Print_DrawLineH(0, 1, 32);
	Print_DrawLineH(0, 18, 32);
	Print_SetPosition(0, 20);
	Print_DrawText("F1:Mode\n");
	Print_DrawText("F2:Pinned\n");
	Print_DrawText("F3:Scene\n");
	Print_DrawText("Arrows:Move\n");

	// Load sprite patterns
	VDP_LoadSpritePattern(g_SprtBall, PATTERN_BALL, 4);
	VDP_LoadSpritePattern(g_SprtPlayer, PATTERN_PLAYER, 4);

	// Initialize sprites
	SpriteMux_Initialize();
	SpriteMux_SetMode(SPRITEMUX_MODE_ROTATE);
	SpriteMux_SetPinned(1);
	SpriteMux_Set(PLAYER_ID, g_PlayerX, g_PlayerY, PATTERN_PLAYER, COLOR_WHITE);
	InitScene();
	loop(i, ROW_NUM)
		SpriteMux_Set(ROW_FIRST + i, g_Ball[i].X, g_Ball[i].Y, PATTERN_BALL, i + 2);
	DisplayStatus();

	BIOS_SetHookCallback(H_TIMI, VBlankHook);
	VDP_EnableVBlank(TRUE);

	u8 prevRow6 = 0xFF;
	bool bContinue = TRUE;
	while (bContinue)
	{
		WaitVBlank();
		SpriteMux_Update();

		g_Frame++;
		UpdateScene();

		// Handle user input
		u8 row6 = Keyboard_Read(6);
		if (!IS_KEY_PRESSED(prevRow6, KEY_F1) && IS_KEY_PRESSED(row6, KEY_F1))
		{
			u8 mode = SpriteMux_GetMode() + 1;
			if (mode >= SPRITEMUX_MODE_MAX)
				mode = SPRITEMUX_MODE_NONE;
			SpriteMux_SetMode(mode);
			DisplayStatus();
		}
		if (!IS_KEY_PRESSED(prevRow6, KEY_F2) && IS_KEY_PRESSED(row6, KEY_F2))
		{
			g_Pinned = !g_Pinned;
			SpriteMux_SetPinned(g_Pinned ? 1 : 0);
			DisplayStatus();
		}
		if (!IS_KEY_PRESSED(prevRow6, KEY_F3) && IS_KEY_PRESSED(row6, KEY_F3))
		{
			g_Scene++;
			if (g_Scene >= SCENE_MAX)
				g_Scene = SCENE_ROW;
			InitScene();
			DisplayStatus();
		}
		prevRow6 = row6;

		u8 row7 = Keyboard_Read(7);
		if (IS_KEY_PRESSED(row7, KEY_ESC))
			bContinue = FALSE;

		u8 row8 = Keyboard_Read(8);
		if (IS_KEY_PRESSED(row8, KEY_RIGHT) && (g_PlayerX < 240))
			g_PlayerX++;
		if (IS_KEY_PRESSED(row8, KEY_LEFT) && (g_PlayerX > 0))
			g_PlayerX--;
		if (IS_KEY_PRESSED(row8, KEY_DOWN) && (g_PlayerY < 128))
			g_PlayerY++;
		if (IS_KEY_PRESSED(row8, KEY_UP) && (g_PlayerY > 16))
			g_PlayerY--;
		SpriteMux_SetPosition(PLAYER_ID, g_PlayerX, g_PlayerY);
	}

	BIOS_ClearHook(H_TIMI);
	BIOS_Exit(0);
}
