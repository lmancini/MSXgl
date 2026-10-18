// ____________________________
// ██▀▀█▀▀██▀▀▀▀▀▀▀█▀▀█        │
// ██  ▀  █▄  ▀██▄ ▀ ▄█ ▄▀▀ █  │  Sprite Multiplexer
// █  █ █  ▀▀  ▄█  █  █ ▀▄█ █▄ │
// ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀────────┘
//  by Guillaume 'Aoineko' Blanchard under CC BY-SA license
//─────────────────────────────────────────────────────────────────────────────
// Sprite multiplexer module
//─────────────────────────────────────────────────────────────────────────────
#include "sprite_mux.h"

//=============================================================================
// MEMORY DATA
//=============================================================================

// Shadow sprite attribute table (logical order)
VDP_Sprite g_SpriteMux_Table[SPRITEMUX_MAX];

// Buffer with the attribute table in the order of the current frame
u8 g_SpriteMux_Buffer[SPRITEMUX_BUFFER_SIZE];

// Number of bytes to upload from g_SpriteMux_Buffer
u8 g_SpriteMux_BufferSize;

// Current ordering strategy
u8 g_SpriteMux_Mode;

// Number of pinned sprites
u8 g_SpriteMux_Pinned;

// Frame counter
u8 g_SpriteMux_Frame;

#if (SPRITEMUX_USE_ROTATE)
// Rotation offset (ROTATE mode)
static u8 g_SpriteMux_Offset;
#endif

// Logical indexes of the visible non-pinned sprites (in logical order)
static u8 g_SpriteMux_Active[SPRITEMUX_MAX];

// Is the multiplexer the owner of the hardware sprite attribute table?
bool g_SpriteMux_Enabled;

//=============================================================================
// FUNCTIONS
//=============================================================================

//-----------------------------------------------------------------------------
// Initialize the sprite multiplexer
void SpriteMux_Initialize()
{
	SpriteMux_HideAll();
	g_SpriteMux_Enabled = TRUE;
#if (SPRITEMUX_USE_SWAP)
	g_SpriteMux_Mode = SPRITEMUX_MODE_SWAP;
#elif (SPRITEMUX_USE_ROTATE)
	g_SpriteMux_Mode = SPRITEMUX_MODE_ROTATE;
#else
	g_SpriteMux_Mode = SPRITEMUX_MODE_NONE;
#endif
	g_SpriteMux_Pinned = 0;
	g_SpriteMux_Frame = 0;
#if (SPRITEMUX_USE_ROTATE)
	g_SpriteMux_Offset = 0;
#endif
	g_SpriteMux_BufferSize = 0;
}

//-----------------------------------------------------------------------------
// Hide all the logical sprites
void SpriteMux_HideAll()
{
	loop(i, SPRITEMUX_MAX)
		g_SpriteMux_Table[i].Y = SPRITEMUX_HIDDEN;
}

//-----------------------------------------------------------------------------
// Give or take back the ownership of the hardware sprite attribute table
void SpriteMux_SetEnable(bool enable)
{
	g_SpriteMux_Enabled = enable;
	if (!enable)
		VDP_HideAllSprites(); // Leave a clean table (no end-of-list marker) for code writing sprites directly to VRAM
}

//-----------------------------------------------------------------------------
// Set all the attributes of a logical sprite
void SpriteMux_Set(u8 id, u8 x, u8 y, u8 pattern, u8 color)
{
	VDP_Sprite* sprt = &g_SpriteMux_Table[id];
	sprt->Y = y;
	sprt->X = x;
	sprt->Pattern = pattern;
	sprt->Color = color;
}

//-----------------------------------------------------------------------------
// Copy a logical sprite to the frame buffer
static u8* SpriteMux_Emit(u8* dst, u8 id)
{
	const u8* src = (const u8*)&g_SpriteMux_Table[id];
	*dst++ = *src++;
	*dst++ = *src++;
	*dst++ = *src++;
	*dst++ = *src;
	return dst;
}

//-----------------------------------------------------------------------------
// Build the attribute table for the current frame
void SpriteMux_Prepare()
{
	u8* dst = g_SpriteMux_Buffer;

	// Pinned sprites always get the highest priorities
	u8 pinned = g_SpriteMux_Pinned;
	if (pinned > SPRITEMUX_MAX)
		pinned = SPRITEMUX_MAX;
	for (u8 i = 0; i < pinned; ++i)
		if (g_SpriteMux_Table[i].Y != SPRITEMUX_HIDDEN)
			dst = SpriteMux_Emit(dst, i);

	// Gather the visible non-pinned sprites
	u8 num = 0;
	for (u8 i = pinned; i < SPRITEMUX_MAX; ++i)
		if (g_SpriteMux_Table[i].Y != SPRITEMUX_HIDDEN)
			g_SpriteMux_Active[num++] = i;

	// Emit them in the order of the current strategy
	if (num)
	{
		switch (g_SpriteMux_Mode)
		{
	#if (SPRITEMUX_USE_SWAP)
		case SPRITEMUX_MODE_SWAP:
			if (g_SpriteMux_Frame & 1)
			{
				for (u8 i = num; i > 0; --i)
					dst = SpriteMux_Emit(dst, g_SpriteMux_Active[i - 1]);
				break;
			}
			// fallthrough: even frames use the logical order
	#endif

		case SPRITEMUX_MODE_NONE:
		default:
			for (u8 i = 0; i < num; ++i)
				dst = SpriteMux_Emit(dst, g_SpriteMux_Active[i]);
			break;

	#if (SPRITEMUX_USE_ROTATE)
		case SPRITEMUX_MODE_ROTATE:
		{
			// The number of active sprites can change from a frame to another
			while (g_SpriteMux_Offset >= num)
				g_SpriteMux_Offset -= num;
			u8 j = g_SpriteMux_Offset;
			for (u8 i = 0; i < num; ++i)
			{
				dst = SpriteMux_Emit(dst, g_SpriteMux_Active[j]);
				if (++j == num)
					j = 0;
			}
			g_SpriteMux_Offset += SPRITEMUX_ROTATE_STEP;
			break;
		}
	#endif
		}
	}

	// Close the list so the VDP stops scanning the attribute table
	g_SpriteMux_BufferSize = (u8)(dst - g_SpriteMux_Buffer);
	if (g_SpriteMux_BufferSize < 32 * 4)
	{
		*dst = SPRITEMUX_HIDDEN;
		g_SpriteMux_BufferSize++;
	}

	g_SpriteMux_Frame++;
}

//-----------------------------------------------------------------------------
// Upload the attribute table to VRAM
void SpriteMux_Flush()
{
	if (!g_SpriteMux_Enabled)
		return;
	VDP_WriteVRAM(g_SpriteMux_Buffer, g_SpriteAttributeLow, g_SpriteAttributeHigh, g_SpriteMux_BufferSize);
}

//-----------------------------------------------------------------------------
// Build the attribute table for the current frame and upload it to VRAM
void SpriteMux_Update()
{
	if (!g_SpriteMux_Enabled)
		return;
	SpriteMux_Prepare();
	SpriteMux_Flush();
}
