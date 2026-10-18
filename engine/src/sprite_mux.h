// ____________________________
// ██▀▀█▀▀██▀▀▀▀▀▀▀█▀▀█        │
// ██  ▀  █▄  ▀██▄ ▀ ▄█ ▄▀▀ █  │  Sprite Multiplexer
// █  █ █  ▀▀  ▄█  █  █ ▀▄█ █▄ │
// ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀────────┘
//  by Guillaume 'Aoineko' Blanchard under CC BY-SA license
//─────────────────────────────────────────────────────────────────────────────
// Sprite multiplexer module
//
// The TMS9918 (and Sprite Mode 1 of later VDPs) can only display 4 sprites on
// the same horizontal line: the 5th one (and all the following ones) are not
// drawn on that line. The sprite with the lowest index has the highest
// priority.
//
// This module keeps a shadow copy of the sprite attribute table in RAM. The
// game writes its sprites into the shadow table (using logical indexes) and,
// once per frame, the module uploads the table to VRAM with a different
// priority order. Sprites that would be hidden by the 4-per-line limit are then
// displayed on alternate frames (flickering) instead of disappearing.
//
// Features:
// - Several ordering strategies (see <SPRITEMUX_MODE>)
// - "Pinned" sprites that are never multiplexed (ex: the player character)
// - Hidden sprites are skipped so priority is never wasted on empty slots
// - The table is closed with the end-of-list marker (Y=208) to minimize the
//   number of bytes written to VRAM and the work done by the VDP
//
// Note: changing the priority order also changes which sprite is drawn on top
// when two sprites overlap. Sprites that are layered on purpose (ex: a
// multi-color character made of several sprites) should be pinned.
//
// Requirements:
// - The VDP must use Sprite Mode 1 (4 bytes per sprite attribute) with its
//   sprite pattern and attribute tables configured by the application.
// - VDP_USE_SPRITE must be enabled and the "sprite_mux" module must be
//   compiled and linked (add it to the project's LibModules list).
//─────────────────────────────────────────────────────────────────────────────
#pragma once

//=============================================================================
// INCLUDES
//=============================================================================

#include "msxgl.h"

//=============================================================================
// OPTIONS VALIDATION
//=============================================================================

// SPRITEMUX_MAX
#ifndef SPRITEMUX_MAX
	#warning SPRITEMUX_MAX is not defined in "msxgl_config.h"! Default value will be used: 32
	#define SPRITEMUX_MAX				32
#endif
#if (SPRITEMUX_MAX < 1)
	#error SPRITEMUX_MAX must be greater or equal to 1
#endif
#if (SPRITEMUX_MAX > 32)
	#error SPRITEMUX_MAX must be lower or equal to 32 (number of hardware sprites)
#endif

// SPRITEMUX_USE_SWAP
#ifndef SPRITEMUX_USE_SWAP
	#warning SPRITEMUX_USE_SWAP is not defined in "msxgl_config.h"! Default value will be used: TRUE
	#define SPRITEMUX_USE_SWAP			TRUE
#endif

// SPRITEMUX_USE_ROTATE
#ifndef SPRITEMUX_USE_ROTATE
	#warning SPRITEMUX_USE_ROTATE is not defined in "msxgl_config.h"! Default value will be used: TRUE
	#define SPRITEMUX_USE_ROTATE		TRUE
#endif

// SPRITEMUX_ROTATE_STEP
#ifndef SPRITEMUX_ROTATE_STEP
	#warning SPRITEMUX_ROTATE_STEP is not defined in "msxgl_config.h"! Default value will be used: 1
	#define SPRITEMUX_ROTATE_STEP		1
#endif

//=============================================================================
// DEFINES
//=============================================================================

// Ordering strategies
enum SPRITEMUX_MODE
{
	SPRITEMUX_MODE_NONE = 0, // Logical order is kept (no multiplexing)
	SPRITEMUX_MODE_SWAP,     // Normal and reversed order on alternate frames (up to 8 sprites per line visible on 2 frames). Needs SPRITEMUX_USE_SWAP
	SPRITEMUX_MODE_ROTATE,   // The first sprite in priority rotates every frame (fair visibility for any number of sprites per line). Needs SPRITEMUX_USE_ROTATE
	SPRITEMUX_MODE_MAX,
};

// Y coordinate used to flag a shadow sprite as not used (it is the VDP end-of-list marker in Sprite Mode 1, so it can't be a valid visible position anyway)
#define SPRITEMUX_HIDDEN				VDP_SPRITE_DISABLE_SM1

// Shadow sprite attribute table (logical order)
extern VDP_Sprite g_SpriteMux_Table[SPRITEMUX_MAX];

// Maximum size of the frame buffer: SPRITEMUX_MAX sprites plus the end-of-list marker
#define SPRITEMUX_BUFFER_SIZE			(SPRITEMUX_MAX * 4 + 1)

// Buffer with the attribute table in the order of the current frame (ready to be uploaded to VRAM)
extern u8 g_SpriteMux_Buffer[SPRITEMUX_BUFFER_SIZE];

// Number of bytes to upload from g_SpriteMux_Buffer
extern u8 g_SpriteMux_BufferSize;

// Current ordering strategy
extern u8 g_SpriteMux_Mode;

// Number of pinned sprites
extern u8 g_SpriteMux_Pinned;

// Frame counter (used by ordering strategies)
extern u8 g_SpriteMux_Frame;

// Is the multiplexer the owner of the hardware sprite attribute table?
extern bool g_SpriteMux_Enabled;

//=============================================================================
// FUNCTIONS
//=============================================================================

// Function: SpriteMux_Initialize
// Initialize the sprite multiplexer: all sprites are hidden, the multiplexer is enabled, mode is set to <SPRITEMUX_MODE_SWAP> (or <SPRITEMUX_MODE_ROTATE> if only this one is compiled) and no sprite is pinned.
void SpriteMux_Initialize();

// Function: SpriteMux_SetEnable
// Give or take back the ownership of the hardware sprite attribute table.
//
// While enabled, <SpriteMux_Update> overwrites the whole attribute table every frame:
// sprites written directly to VRAM (ex: with <VDP_SetSpriteSM1>) are lost.
// When disabled, <SpriteMux_Update> does nothing and all hardware sprites are hidden,
// so code writing sprites directly to VRAM (ex: cutscenes, menus) starts from a clean table
// (the end-of-list marker written by the multiplexer would otherwise disable some of its sprites).
// The logical sprites are kept and displayed again when the multiplexer is enabled back.
//
// Parameters:
//   enable - TRUE to let the multiplexer own the attribute table
void SpriteMux_SetEnable(bool enable);

// Function: SpriteMux_IsEnabled
// Check if the multiplexer owns the hardware sprite attribute table.
//
// Return:
//   TRUE if enabled
inline bool SpriteMux_IsEnabled() { return g_SpriteMux_Enabled; }

// Function: SpriteMux_HideAll
// Hide all the logical sprites.
void SpriteMux_HideAll();

// Function: SpriteMux_SetMode
// Set the ordering strategy.
// Modes disabled at compile time fall back to <SPRITEMUX_MODE_NONE>.
//
// Parameters:
//   mode - Ordering strategy (see <SPRITEMUX_MODE>)
inline void SpriteMux_SetMode(u8 mode)
{
#if (!SPRITEMUX_USE_SWAP)
	if (mode == SPRITEMUX_MODE_SWAP)
		mode = SPRITEMUX_MODE_NONE;
#endif
#if (!SPRITEMUX_USE_ROTATE)
	if (mode == SPRITEMUX_MODE_ROTATE)
		mode = SPRITEMUX_MODE_NONE;
#endif
	g_SpriteMux_Mode = mode;
}

// Function: SpriteMux_GetMode
// Get the current ordering strategy.
//
// Return:
//   Ordering strategy (see <SPRITEMUX_MODE>)
inline u8 SpriteMux_GetMode() { return g_SpriteMux_Mode; }

// Function: SpriteMux_SetPinned
// Set the number of pinned sprites.
// Pinned sprites are the first logical sprites [0:num-1]: they always get the highest priorities (in their logical order) and are never multiplexed.
//
// Parameters:
//   num - Number of pinned sprites (should be lower than 4 to let other sprites a chance to be displayed on the same lines)
inline void SpriteMux_SetPinned(u8 num) { g_SpriteMux_Pinned = num; }

// Function: SpriteMux_Set
// Set all the attributes of a logical sprite.
//
// Parameters:
//   id      - Logical sprite index [0:SPRITEMUX_MAX-1]
//   x       - X screen coordinate
//   y       - Y screen coordinate (must not be <SPRITEMUX_HIDDEN>)
//   pattern - Sprite pattern index (in 16x16 mode, only multiple of 4 can be used)
//   color   - Sprite color (LSB 4-bits) + optional EC flag (VDP_SPRITE_EC)
void SpriteMux_Set(u8 id, u8 x, u8 y, u8 pattern, u8 color);

// Function: SpriteMux_SetPosition
// Set the position of a logical sprite.
//
// Parameters:
//   id - Logical sprite index [0:SPRITEMUX_MAX-1]
//   x  - X screen coordinate
//   y  - Y screen coordinate (must not be <SPRITEMUX_HIDDEN>)
inline void SpriteMux_SetPosition(u8 id, u8 x, u8 y) { g_SpriteMux_Table[id].X = x; g_SpriteMux_Table[id].Y = y; }

// Function: SpriteMux_SetPattern
// Set the pattern of a logical sprite.
//
// Parameters:
//   id      - Logical sprite index [0:SPRITEMUX_MAX-1]
//   pattern - Sprite pattern index (in 16x16 mode, only multiple of 4 can be used)
inline void SpriteMux_SetPattern(u8 id, u8 pattern) { g_SpriteMux_Table[id].Pattern = pattern; }

// Function: SpriteMux_SetColor
// Set the color of a logical sprite.
//
// Parameters:
//   id    - Logical sprite index [0:SPRITEMUX_MAX-1]
//   color - Sprite color (LSB 4-bits) + optional EC flag (VDP_SPRITE_EC)
inline void SpriteMux_SetColor(u8 id, u8 color) { g_SpriteMux_Table[id].Color = color; }

// Function: SpriteMux_Hide
// Hide a logical sprite. Hidden sprites don't use any hardware slot.
//
// Parameters:
//   id - Logical sprite index [0:SPRITEMUX_MAX-1]
inline void SpriteMux_Hide(u8 id) { g_SpriteMux_Table[id].Y = SPRITEMUX_HIDDEN; }

// Function: SpriteMux_IsVisible
// Check if a logical sprite is used (not hidden).
//
// Parameters:
//   id - Logical sprite index [0:SPRITEMUX_MAX-1]
//
// Return:
//   FALSE if the sprite is hidden
inline bool SpriteMux_IsVisible(u8 id) { return g_SpriteMux_Table[id].Y != SPRITEMUX_HIDDEN; }

// Function: SpriteMux_GetSprite
// Get direct access to a logical sprite attributes.
//
// Parameters:
//   id - Logical sprite index [0:SPRITEMUX_MAX-1]
//
// Return:
//   Pointer to the sprite attributes in the shadow table
inline VDP_Sprite* SpriteMux_GetSprite(u8 id) { return &g_SpriteMux_Table[id]; }

// Function: SpriteMux_Prepare
// Build the attribute table for the current frame (in RAM).
// Can be called at any time during the frame (ex: at the end of the game logic) to keep the V-Blank work as short as possible.
void SpriteMux_Prepare();

// Function: SpriteMux_Flush
// Upload the attribute table built by <SpriteMux_Prepare> to VRAM.
// Should be called during V-Blank. Does nothing while the multiplexer is disabled (see <SpriteMux_SetEnable>).
void SpriteMux_Flush();

// Function: SpriteMux_Update
// Build the attribute table for the current frame and upload it to VRAM (<SpriteMux_Prepare> + <SpriteMux_Flush>).
// Should be called once per frame during V-Blank. Does nothing while the multiplexer is disabled (see <SpriteMux_SetEnable>).
// Can be called automatically by the game state module (see GAME_USE_SPRITEMUX option).
void SpriteMux_Update();
