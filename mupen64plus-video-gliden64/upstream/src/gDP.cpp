#include <assert.h>
#include <algorithm>
#include <cmath>
#include "GLideN64.h"
#include "N64.h"
#include "GBI.h"
#include "RSP.h"
#include "RDP.h"
#include "gDP.h"
#include "gSP.h"
#include "Types.h"
#include "DebugDump.h"
#include "convert.h"
#include "CRC.h"
#include "FrameBuffer.h"
#include "DepthBuffer.h"
#include "FrameBufferInfo.h"
#include "TextureFilterHandler.h"
#include "VI.h"
#include "Config.h"
#include "Combiner.h"
#include "Performance.h"
#include "DisplayWindow.h"
#include <Graphics/Context.h>

using namespace std;

gDPInfo gDP;

bool isCurrentColorImageDepthImage()
{
	return (gDP.colorImage.address == gDP.depthImageAddress) ||
		(gDP.fillColor.color == DepthClearColor && gDP.otherMode.cycleType == G_CYC_FILL);
}

bool isDepthCompareEnabled()
{
	return gDP.otherMode.cycleType <= G_CYC_2CYCLE &&
		gDP.otherMode.depthCompare != 0 &&
		((gSP.geometryMode & G_ZBUFFER) || gDP.otherMode.depthSource == G_ZS_PRIM);
}

void gDPSetOtherMode( u32 mode0, u32 mode1 )
{
	gDP.otherMode.h = mode0;
	gDP.otherMode.l = mode1;

	gDP.changed |= CHANGED_RENDERMODE | CHANGED_CYCLETYPE | CHANGED_ALPHACOMPARE;

#ifdef DEBUG_DUMP
	DebugMsg( DEBUG_NORMAL, "gDPSetOtherMode( %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s, %s | %s | %s%s%s%s%s | %s | %s%s%s );\n",
		AlphaDitherText[gDP.otherMode.alphaDither],
		ColorDitherText[gDP.otherMode.colorDither],
		CombineKeyText[gDP.otherMode.combineKey],
		TextureConvertText[gDP.otherMode.convert_one | (gDP.otherMode.bi_lerp1 << 1) | (gDP.otherMode.bi_lerp0 << 2)],
		TextureFilterText[gDP.otherMode.textureFilter],
		TextureLUTText[gDP.otherMode.textureLUT],
		TextureLODText[gDP.otherMode.textureLOD],
		TextureDetailText[gDP.otherMode.textureDetail],
		TexturePerspText[gDP.otherMode.texturePersp],
		CycleTypeText[gDP.otherMode.cycleType],
		PipelineModeText[gDP.otherMode.pipelineMode],
		AlphaCompareText[gDP.otherMode.alphaCompare],
		DepthSourceText[gDP.otherMode.depthSource],
		gDP.otherMode.AAEnable ? "AA_EN | " : "",
		gDP.otherMode.depthCompare ? "Z_CMP | " : "",
		gDP.otherMode.depthUpdate ? "Z_UPD | " : "",
		gDP.otherMode.imageRead ? "IM_RD | " : "",
		CvgDestText[gDP.otherMode.cvgDest],
		DepthModeText[gDP.otherMode.depthMode],
		gDP.otherMode.cvgXAlpha ? "CVG_X_ALPHA | " : "",
		gDP.otherMode.alphaCvgSel ? "ALPHA_CVG_SEL | " : "",
		gDP.otherMode.forceBlender ? "FORCE_BL" : "" );
#endif
}

void gDPSetPrimDepth( u16 z, u16 dz )
{
	gDP.primDepth.z = _FIXED2FLOAT(_SHIFTR(z, 0, 15), 15);
	gDP.primDepth.deltaZ = _FIXED2FLOAT(_SHIFTR(dz, 0, 15), 15);
	DebugMsg( DEBUG_NORMAL, "gDPSetPrimDepth( %f, %f );\n", gDP.primDepth.z, gDP.primDepth.deltaZ);
}

void gDPSetTexturePersp( u32 enable )
{
	gDP.otherMode.texturePersp = enable & 1;

#ifdef DEBUG_DUMP
	DebugMsg( DEBUG_NORMAL, "gDPSetTexturePersp( %s );\n", TexturePerspText[gDP.otherMode.texturePersp] );
#endif
}

void gDPSetTextureLUT( u32 mode )
{
	gDP.otherMode.textureLUT = mode & 3;

#ifdef DEBUG_DUMP
	DebugMsg( DEBUG_NORMAL, "gDPSetTextureLUT( %s );\n", TextureLUTText[gDP.otherMode.textureLUT] );
#endif
}

void gDPSetCombine( s32 muxs0, s32 muxs1 )
{
	gDP.combine.muxs0 = muxs0;
	gDP.combine.muxs1 = muxs1;

	gDP.changed |= CHANGED_COMBINE;

	DebugMsg(DEBUG_NORMAL, "gDPSetCombine\n");
#ifdef DEBUG_DUMP
	DebugMsg( DEBUG_NORMAL, "	%s, %s, %s, %s, %s, %s, %s, %s,\n",
		saRGBText[gDP.combine.saRGB0],
		sbRGBText[gDP.combine.sbRGB0],
		mRGBText[gDP.combine.mRGB0],
		aRGBText[gDP.combine.aRGB0],
		saAText[gDP.combine.saA0],
		sbAText[gDP.combine.sbA0],
		mAText[gDP.combine.mA0],
		aAText[gDP.combine.aA0] );

	DebugMsg( DEBUG_NORMAL, "	%s, %s, %s, %s, %s, %s, %s, %s );\n",
		saRGBText[gDP.combine.saRGB1],
		sbRGBText[gDP.combine.sbRGB1],
		mRGBText[gDP.combine.mRGB1],
		aRGBText[gDP.combine.aRGB1],
		saAText[gDP.combine.saA1],
		sbAText[gDP.combine.sbA1],
		mAText[gDP.combine.mA1],
		aAText[gDP.combine.aA1] );
#endif
}

void gDPSetColorImage( u32 format, u32 size, u32 width, u32 address )
{
	address = RSP_SegmentToPhysical( address );

	gDP.colorImage.format = format;
	gDP.colorImage.size = size;
	gDP.colorImage.width = width;
	gDP.colorImage.height = 0;
	gDP.colorImage.address = address;

	frameBufferList().saveBuffer(address, (u16)format, (u16)size, (u16)width, false);

#ifdef DEBUG_DUMP
	DebugMsg( DEBUG_NORMAL, "gDPSetColorImage( %s, %s, %i, 0x%08X );\n",
		ImageFormatText[gDP.colorImage.format],
		ImageSizeText[gDP.colorImage.size],
		gDP.colorImage.width,
		gDP.colorImage.address );
#endif
}

void gDPSetTextureImage(u32 format, u32 size, u32 width, u32 address)
{
	gDP.textureImage.format = format;
	gDP.textureImage.size = size;
	gDP.textureImage.width = width;
	gDP.textureImage.address = RSP_SegmentToPhysical(address);
	gDP.textureImage.bpl = gDP.textureImage.width << gDP.textureImage.size >> 1;
	if (gSP.DMAOffsets.tex_offset != 0) {
		if (format == G_IM_FMT_RGBA) {
			u16 * t = (u16*)(RDRAM + gSP.DMAOffsets.tex_offset);
			gSP.DMAOffsets.tex_shift = t[gSP.DMAOffsets.tex_count ^ 1];
			gDP.textureImage.address += gSP.DMAOffsets.tex_shift;
		} else {
			gSP.DMAOffsets.tex_offset = 0;
			gSP.DMAOffsets.tex_shift = 0;
			gSP.DMAOffsets.tex_count = 0;
		}
	}
#ifdef DEBUG_DUMP
	DebugMsg( DEBUG_NORMAL, "gDPSetTextureImage( %s, %s, %i, 0x%08X );\n",
		ImageFormatText[gDP.textureImage.format],
		ImageSizeText[gDP.textureImage.size],
		gDP.textureImage.width,
		gDP.textureImage.address );
#endif
}

void gDPSetDepthImage( u32 address )
{
	address = RSP_SegmentToPhysical( address );
	gDP.depthImageAddress = address;
	depthBufferList().saveBuffer(address);

	DebugMsg( DEBUG_NORMAL, "gDPSetDepthImage( 0x%08X );\n", gDP.depthImageAddress );
}

void gDPSetEnvColor( u32 r, u32 g, u32 b, u32 a )
{
	gDP.envColor.r = _FIXED2FLOATCOLOR( r, 8 );
	gDP.envColor.g = _FIXED2FLOATCOLOR( g, 8 );
	gDP.envColor.b = _FIXED2FLOATCOLOR( b, 8 );
	gDP.envColor.a = _FIXED2FLOATCOLOR( a, 8 );

	DebugMsg( DEBUG_NORMAL, "gDPSetEnvColor( %i, %i, %i, %i );\n", r, g, b, a );
}

void gDPSetBlendColor( u32 r, u32 g, u32 b, u32 a )
{
	gDP.blendColor.r = _FIXED2FLOATCOLOR( r, 8 );
	gDP.blendColor.g = _FIXED2FLOATCOLOR( g, 8 );
	gDP.blendColor.b = _FIXED2FLOATCOLOR( b, 8 );
	gDP.blendColor.a = _FIXED2FLOATCOLOR( a, 8 );

	gDP.changed |= CHANGED_BLENDCOLOR;

	DebugMsg( DEBUG_NORMAL, "gDPSetBlendColor( %i, %i, %i, %i );\n", r, g, b, a );
}

void gDPSetFogColor( u32 r, u32 g, u32 b, u32 a )
{
	gDP.fogColor.r = _FIXED2FLOATCOLOR( r, 8 );
	gDP.fogColor.g = _FIXED2FLOATCOLOR( g, 8 );
	gDP.fogColor.b = _FIXED2FLOATCOLOR( b, 8 );
	gDP.fogColor.a = _FIXED2FLOATCOLOR( a, 8 );

	gDP.changed |= CHANGED_FOGCOLOR;

	DebugMsg( DEBUG_NORMAL, "gDPSetFogColor( %i, %i, %i, %i );\n", r, g, b, a );
}

void gDPSetFillColor( u32 c )
{
	gDP.fillColor.color = c;
	gDP.fillColor.z = (f32)_SHIFTR( c,  2, 14 );
	gDP.fillColor.dz = (f32)_SHIFTR( c, 0, 2 );

	DebugMsg( DEBUG_NORMAL, "gDPSetFillColor( 0x%08X );\n", c );
}

static void getFillColor(u32 c, f32 _fillColor[4])
{
	if (gDP.colorImage.size < 3) {
		_fillColor[0] = _FIXED2FLOATCOLOR(_SHIFTR(c, 11, 5), 5);
		_fillColor[1] = _FIXED2FLOATCOLOR(_SHIFTR(c, 6, 5), 5);
		_fillColor[2] = _FIXED2FLOATCOLOR(_SHIFTR(c, 1, 5), 5);
		_fillColor[3] = (f32)_SHIFTR(c, 0, 1);
	}
	else {
		_fillColor[0] = _FIXED2FLOATCOLOR(_SHIFTR(c, 24, 8), 8);
		_fillColor[1] = _FIXED2FLOATCOLOR(_SHIFTR(c, 16, 8), 8);
		_fillColor[2] = _FIXED2FLOATCOLOR(_SHIFTR(c, 8, 8), 8);
		_fillColor[3] = _FIXED2FLOATCOLOR(_SHIFTR(c, 0, 8), 8);
	}
}

void gDPGetFillColor(f32 _fillColor[4])
{
	const u32 c = gDP.fillColor.color;
	return getFillColor(c, _fillColor);
}

void gDPSetPrimColor( u32 m, u32 l, u32 r, u32 g, u32 b, u32 a )
{
	gDP.primColor.m = _FIXED2FLOAT( m, 5 );
	gDP.primColor.l = _FIXED2FLOATCOLOR( l, 8 );
	gDP.primColor.r = _FIXED2FLOATCOLOR( r, 8 );
	gDP.primColor.g = _FIXED2FLOATCOLOR( g, 8 );
	gDP.primColor.b = _FIXED2FLOATCOLOR( b, 8 );
	gDP.primColor.a = _FIXED2FLOATCOLOR( a, 8 );

	DebugMsg( DEBUG_NORMAL, "gDPSetPrimColor( %i, %i, %i, %i, %i, %i );\n", m, l, r, g, b, a );
}

void gDPSetTile( u32 format, u32 size, u32 line, u32 tmem, u32 tile, u32 palette, u32 cmt, u32 cms, u32 maskt, u32 masks, u32 shiftt, u32 shifts )
{
	gDP.tiles[tile].format = format;
	gDP.tiles[tile].size = size;
	gDP.tiles[tile].line = line;
	gDP.tiles[tile].tmem = tmem;
	gDP.tiles[tile].palette = palette;
	gDP.tiles[tile].cmt = cmt;
	gDP.tiles[tile].cms = cms;
	gDP.tiles[tile].maskt = gDP.tiles[tile].originalMaskT = maskt;
	gDP.tiles[tile].masks = gDP.tiles[tile].originalMaskS = masks;
	gDP.tiles[tile].shiftt = shiftt;
	gDP.tiles[tile].shifts = shifts;

	if (!gDP.tiles[tile].masks) gDP.tiles[tile].clamps = 1;
	if (!gDP.tiles[tile].maskt) gDP.tiles[tile].clampt = 1;

	if (tile == gSP.texture.tile || tile == gSP.texture.tile + 1) {
		u32 nTile = gDP.loadTileIdx;
		while(gDP.tiles[nTile].tmem != tmem && nTile > gSP.texture.tile + 1)
			--nTile;
		if (nTile > gSP.texture.tile + 1) {
			gDP.tiles[tile].textureMode = gDP.tiles[nTile].textureMode;
			gDP.tiles[tile].loadType = gDP.tiles[nTile].loadType;
			gDP.tiles[tile].frameBufferAddress = gDP.tiles[nTile].frameBufferAddress;
			gDP.tiles[tile].imageAddress = gDP.tiles[nTile].imageAddress;
		}
	}

	gDP.changed |= CHANGED_TILE;

#ifdef DEBUG_DUMP
	DebugMsg( DEBUG_NORMAL, "gDPSetTile( %s, %s, %i, %i, %i, %i, %s%s, %s%s, %i, %i, %i, %i );\n",
		ImageFormatText[format],
		ImageSizeText[size],
		line,
		tmem,
		tile,
		palette,
		cmt & G_TX_MIRROR ? "G_TX_MIRROR" : "G_TX_NOMIRROR",
		cmt & G_TX_CLAMP ? " | G_TX_CLAMP" : "",
		cms & G_TX_MIRROR ? "G_TX_MIRROR" : "G_TX_NOMIRROR",
		cms & G_TX_CLAMP ? " | G_TX_CLAMP" : "",
		maskt,
		masks,
		shiftt,
		shifts );
#endif
}

#ifdef __GNUC__
#define leading_zeroes(x) ((x) == 0 ? 0 : __builtin_clz(x))
#define trailing_zeroes(x) ((x) == 0 ? 0 : __builtin_ctz(x))
#define trailing_ones(x) __builtin_ctz(~uint32_t(x))
#elif defined(_MSC_VER)
static inline uint32_t clz(uint32_t x)
{
	unsigned long result;
	if (_BitScanReverse(&result, x))
		return 31 - result;
	else
		return 0;
}

static inline uint32_t ctz(uint32_t x)
{
	unsigned long result;
	if (_BitScanForward(&result, x))
		return result;
	else
		return 0;
}

#define leading_zeroes(x) clz(x)
#define trailing_zeroes(x) ctz(x)
#define trailing_ones(x) ctz(~uint32_t(x))
#else
#error "Implement me."
#endif

extern "C" uint32_t LegacySm64ToolsHacks;
void gDPSetTileSize( u32 tile, u32 uls, u32 ult, u32 lrs, u32 lrt )
{
	gDP.tiles[tile].uls = _SHIFTR( uls, 2, 10 );
	gDP.tiles[tile].ult = _SHIFTR( ult, 2, 10 );
	gDP.tiles[tile].lrs = _SHIFTR( lrs, 2, 10 );
	gDP.tiles[tile].lrt = _SHIFTR( lrt, 2, 10 );

	gDP.tiles[tile].fuls = _FIXED2FLOAT( uls, 2 );
	gDP.tiles[tile].fult = _FIXED2FLOAT( ult, 2 );
	gDP.tiles[tile].flrs = _FIXED2FLOAT( lrs, 2 );
	gDP.tiles[tile].flrt = _FIXED2FLOAT( lrt, 2 );

	if (LegacySm64ToolsHacks)
	{
		// Force maskT and maskS for given sizes if we go too far
		// Get normal sizes for lrs/lrt
		u32 lrsizes = gDP.tiles[tile].lrs + 1;
		u32 lrsizet = gDP.tiles[tile].lrt + 1;

		if (lrsizes == 16 && lrsizet == 2)
		{
			// Get normal sizes for masks/maskt
			u32 masksizes = 1 << gDP.tiles[tile].masks;
			u32 masksizet = 1 << gDP.tiles[tile].maskt;

			// Do validity check
			// TODO: Not sure if this actually works
			if (masksizes > lrsizes)
			{
				unsigned long index = trailing_zeroes(lrsizes);
				gDP.tiles[tile].masks = index;
				gDP.tiles[tile].originalMaskS = index;
			}

			if (masksizet > lrsizet)
			{
				unsigned long index = trailing_zeroes(lrsizet);
				gDP.tiles[tile].maskt = index;
				gDP.tiles[tile].originalMaskT = index;
			}
		}
	}

	gDP.changed |= CHANGED_TILE;

	DebugMsg( DEBUG_NORMAL, "gDPSetTileSize( %i, %.2f, %.2f, %.2f, %.2f );\n",
		tile,
		gDP.tiles[tile].fuls,
		gDP.tiles[tile].fult,
		gDP.tiles[tile].flrs,
		gDP.tiles[tile].flrt );
}

static
bool CheckForFrameBufferTexture(u32 _address, u32 _width, u32 _bytes)
{
	gDP.loadTile->textureMode = TEXTUREMODE_NORMAL;
	gDP.loadTile->frameBufferAddress = 0U;
	gDP.changed |= CHANGED_TMEM;
	if (!config.frameBufferEmulation.enable)
		return false;

	FrameBufferList & fbList = frameBufferList();
	FrameBuffer *pBuffer = fbList.findBuffer(_address);
	bool bRes = pBuffer != nullptr && pBuffer->m_readable;
	while (bRes) {
		if ((config.generalEmulation.hacks & hack_blurPauseScreen) != 0) {
			if (gDP.colorImage.address == gDP.depthImageAddress && pBuffer->m_copiedToRdram) {
				memcpy(RDRAM + gDP.depthImageAddress,
					RDRAM + pBuffer->m_startAddress,
					(pBuffer->m_width*pBuffer->m_height) << pBuffer->m_size >> 1);
				pBuffer->m_copiedToRdram = false;
				fbList.getCurrent()->m_isPauseScreen = true;
			}
			if (pBuffer->m_isPauseScreen) {
				bRes = false;
				break;
			}
		}

		if (gDP.otherMode.textureLUT == G_TT_RGBA16 && (config.generalEmulation.hacks & hack_StarCraftBackgrounds) != 0) {
			bRes = false;
			break;
		}

		if (pBuffer->m_cfb) {
			fbList.removeBuffer(pBuffer->m_startAddress);
			bRes = false;
			break;
		}

		if ((config.generalEmulation.hacks & hack_noDepthFrameBuffers) != 0 && pBuffer->m_isDepthBuffer) {
			fbList.removeBuffer(pBuffer->m_startAddress);
			bRes = false;
			break;
		}

		const u32 texEndAddress = _address + _bytes - 1;
		if (_address > pBuffer->m_startAddress &&
			pBuffer->m_width != _width &&
			texEndAddress > (pBuffer->m_endAddress + (pBuffer->m_width << pBuffer->m_size >> 1))) {
			//fbList.removeBuffer(pBuffer->m_startAddress);
			bRes = false;
			break;
		}

		if (gDP.loadTile->loadType == LOADTYPE_TILE &&
			gDP.textureImage.width != pBuffer->m_width &&
			gDP.textureImage.size != pBuffer->m_size) {
			//fbList.removeBuffer(pBuffer->m_startAddress); // Does not work with Zelda MM
			bRes = false;
			break;
		}

		bRes = pBuffer->isValid(false);
		if (!bRes && pBuffer != fbList.getCurrent()) {
			fbList.removeBuffer(pBuffer->m_startAddress);
			break;
		}

		pBuffer->m_loadType = gDP.loadTile->loadType;
		pBuffer->m_loadTileOrigin.uls = gDP.loadTile->uls;
		pBuffer->m_loadTileOrigin.ult = gDP.loadTile->ult;
		gDP.loadTile->frameBufferAddress = pBuffer->m_startAddress;
		gDP.loadTile->textureMode = TEXTUREMODE_FRAMEBUFFER;
		break;
	}

	for (int nTile = gSP.texture.tile; nTile < 6; ++nTile) {
		if (gDP.tiles[nTile].tmem == gDP.loadTile->tmem) {
			gDPTile & curTile = gDP.tiles[nTile];
			curTile.textureMode = gDP.loadTile->textureMode;
			curTile.loadType = gDP.loadTile->loadType;
			curTile.frameBufferAddress = gDP.loadTile->frameBufferAddress;
			curTile.imageAddress = gDP.loadTile->imageAddress;
		}
	}
	return bRes;
}

//****************************************************************
// LoadTile for 32bit RGBA texture
// Based on sources of angrylion's software plugin.
//
void gDPLoadTile32b(u32 uls, u32 ult, u32 lrs, u32 lrt)
{
	// TODO: This can be cached as well
	tmemCacheHashInvalidate();
	const u32 width = lrs - uls + 1;
	const u32 height = lrt - ult + 1;
	const u32 line = gDP.loadTile->line << 2;
	const u32 tbase = gDP.loadTile->tmem << 2;
	const u32 addr = gDP.textureImage.address >> 2;
	const u32 * src = (const u32*)RDRAM;
	u16 * tmem16 = (u16*)TMEM;
	u32 c, ptr, tline, s, xorval;

	for (u32 j = 0; j < height; ++j) {
		tline = tbase + line * j;
		s = ((j + ult) * gDP.textureImage.width) + uls;
		xorval = (j & 1) ? 3 : 1;
		for (u32 i = 0; i < width; ++i) {
			c = src[addr + s + i];
			ptr = ((tline + i) ^ xorval) & 0x3ff;
			tmem16[ptr] = c >> 16;
			tmem16[ptr | 0x400] = c & 0xffff;
		}
	}
}

void gDPLoadTile(u32 tile, u32 uls, u32 ult, u32 lrs, u32 lrt)
{
	gDPSetTileSize( tile, uls, ult, lrs, lrt );
	gDP.loadTileIdx = tile;
	gDP.loadTile = &gDP.tiles[tile];
	gDP.loadTile->loadType = LOADTYPE_TILE;
	gDP.loadTile->imageAddress = gDP.textureImage.address;

	if (gDP.loadTile->lrs < gDP.loadTile->uls || gDP.loadTile->lrt < gDP.loadTile->ult)
		return;

	const u32 width = (gDP.loadTile->lrs - gDP.loadTile->uls + 1) & 0x03FF;
	const u32 height = (gDP.loadTile->lrt - gDP.loadTile->ult + 1) & 0x03FF;
	const u32 bpl = gDP.loadTile->line << 3;

	u32 alignedWidth = width;
	u32 wmask = 0;
	switch (gDP.textureImage.size) {
	case G_IM_SIZ_8b:
		wmask = 7;
		break;
	case G_IM_SIZ_16b:
		wmask = 3;
		break;
	case G_IM_SIZ_32b:
		wmask = 1;
		break;
	}
	if ((width & wmask) != 0)
		alignedWidth = (width & (~wmask)) + wmask + 1;
	const u32 bpr = alignedWidth << gDP.loadTile->size >> 1;

	gDPLoadTileInfo &info = gDP.loadInfo[gDP.loadTile->tmem];
	info.texAddress = gDP.loadTile->imageAddress;
	info.uls = gDP.loadTile->uls;
	info.ult = gDP.loadTile->ult;
	info.lrs = gDP.loadTile->lrs;
	info.lrt = gDP.loadTile->lrt;
	info.width = gDP.loadTile->masks != 0 ? (u16)min(width, 1U << gDP.loadTile->masks) : (u16)width;
	info.height = gDP.loadTile->maskt != 0 ? (u16)min(height, 1U << gDP.loadTile->maskt) : (u16)height;
	info.texWidth = gDP.textureImage.width;
	info.size = gDP.textureImage.size;
	info.loadType = LOADTYPE_TILE;
	info.bytes = bpl * height;
	if (gDP.loadTile->size == G_IM_SIZ_32b)
		// 32 bit texture loaded into lower and upper half of TMEM, thus actual bytes doubled.
		info.bytes *= 2;

	if (gDP.loadTile->line == 0)
		return;

	if (gDP.loadTile->masks == 0)
		gDP.loadTile->loadWidth = max(gDP.loadTile->loadWidth, info.width);
	if (gDP.loadTile->maskt == 0 && gDP.loadTile->tmem % gDP.loadTile->line == 0) {
		const u16 theight = info.height + gDP.loadTile->tmem / gDP.loadTile->line;
		gDP.loadTile->loadHeight = max(gDP.loadTile->loadHeight, theight);
	}

	u32 address = gDP.textureImage.address + gDP.loadTile->ult * gDP.textureImage.bpl + (gDP.loadTile->uls << gDP.textureImage.size >> 1);
	u32 bpl2 = bpl;
	if (gDP.loadTile->lrs > gDP.textureImage.width)
		bpl2 = (gDP.textureImage.width - gDP.loadTile->uls);
	u32 height2 = height;
	if (gDP.loadTile->lrt > gDP.scissor.lry)
		height2 = (u32)gDP.scissor.lry - gDP.loadTile->ult;

	if (CheckForFrameBufferTexture(address, info.width, bpl2*height2))
		return;

	tmemCacheHashInvalidate();
	if (gDP.loadTile->size == G_IM_SIZ_32b)
		gDPLoadTile32b(gDP.loadTile->uls, gDP.loadTile->ult, gDP.loadTile->lrs, gDP.loadTile->lrt);
	else {
		u32 tmemAddr = gDP.loadTile->tmem;
		const u32 line = gDP.loadTile->line;
		const u32 qwpr = bpr >> 3;
		for (u32 y = 0; y < height; ++y) {
			if (address + bpl > RDRAMSize)
				UnswapCopyWrap<0xfff>(RDRAM, address, (u8*)TMEM, tmemAddr << 3, RDRAMSize - address);
			else
				UnswapCopyWrap<0xfff>(RDRAM, address, (u8*)TMEM, tmemAddr << 3, bpr);
			if (y & 1)
				DWordInterleaveWrap((u32*)TMEM, tmemAddr << 1, 0x3FF, qwpr);

			address += gDP.textureImage.bpl;
			if (address >= RDRAMSize)
				break;
			tmemAddr += line;
		}
	}

	DebugMsg( DEBUG_NORMAL, "gDPLoadTile( %i, %i, %i, %i, %i );\n",
			tile, gDP.loadTile->uls, gDP.loadTile->ult, gDP.loadTile->lrs, gDP.loadTile->lrt );
}

//****************************************************************
// LoadBlock for 32bit RGBA texture
// Based on sources of angrylion's software plugin.
//
void gDPLoadBlock32(u32 uls,u32 lrs, u32 dxt)
{
	const u32 * src = (const u32*)RDRAM;
	const u32 tb = gDP.loadTile->tmem << 2;
	const u32 line = gDP.loadTile->line << 2;

	u16 *tmem16 = (u16*)TMEM;
	u32 addr = gDP.loadTile->imageAddress >> 2;
	u32 width = (lrs - uls + 1) << 2;
	if (width == 4) // lr_s == 0, 1x1 texture
		width = 1;
	else if (width & 7)
		width = (width & (~7)) + 8;

	if (dxt != 0) {
		u32 j = 0;
		u32 t = 0;
		u32 oldt = 0;
		u32 ptr;

		u32 c = 0;
		for (u32 i = 0; i < width; i += 2) {
			oldt = t;
			t = ((j >> 11) & 1) ? 3 : 1;
			if (t != oldt)
				i += line;
			ptr = ((tb + i) ^ t) & 0x3ff;
			c = src[addr + i];
			tmem16[ptr] = c >> 16;
			tmem16[ptr | 0x400] = c & 0xffff;
			ptr = ((tb + i + 1) ^ t) & 0x3ff;
			c = src[addr + i + 1];
			tmem16[ptr] = c >> 16;
			tmem16[ptr | 0x400] = c & 0xffff;
			j += dxt;
		}
	} else {
		u32 c, ptr;
		for (u32 i = 0; i < width; i++) {
			ptr = ((tb + i) ^ 1) & 0x3ff;
			c = src[addr + i];
			tmem16[ptr] = c >> 16;
			tmem16[ptr | 0x400] = c & 0xffff;
		}
	}
}

#define TMEM_CACHE_BITS 11
#define TMEM_CACHE_SIZE (1 << TMEM_CACHE_BITS)

static uint32_t sTmemFrame = 0;

struct TmemCacheEntryDesc
{
	uint32_t address;
	uint16_t qwords;
	uint16_t dxt;
	uint32_t crc;

	bool matches(uint32_t _address, uint32_t _qwords, uint32_t _dxt) const
	{
		return address == _address && qwords == _qwords && dxt == _dxt;
	}
};

struct Tmem
{
	uint64_t data[512];
};

static TmemCacheEntryDesc sTmemCacheEntryDescriptors[TMEM_CACHE_SIZE] = {};
static Tmem sTmemCache[TMEM_CACHE_SIZE];

void tmemCacheDrop()
{
	memset(sTmemCacheEntryDescriptors, 0, sizeof(sTmemCacheEntryDescriptors));
	tmemCacheHashInvalidate();
}

static inline uint32_t hashInt32(uint32_t x)
{
	// given values normally passed, this will give a very good spread
	// return (x >> 10) % TMEM_CACHE_SIZE;
	return std::hash<uint32_t>()(x) & (TMEM_CACHE_SIZE - 1);
}

static void tmemAddCacheEntry(uint32_t address, uint32_t tmemIdx, uint16_t qwords, uint16_t dxt)
{
	uint32_t hash = hashInt32(address);
	TmemCacheEntryDesc& entry = sTmemCacheEntryDescriptors[hash];
	Tmem& tmem = sTmemCache[hash];
	entry.address = address;
	entry.qwords = qwords;
	entry.dxt = dxt;
	entry.crc = CRC_Calculate(0xffffffff, &TMEM[tmemIdx], qwords << 3);
#if 1
	__builtin_memcpy(tmem.data, &TMEM[tmemIdx], qwords << 3);
#else
	__movsd((unsigned long*)tmem.data, (unsigned long*)&TMEM[tmemIdx], qwords << 1);
#endif
	tmemCacheHashSet(tmemIdx, qwords << 3, entry.crc);
}

static Tmem* tmemFindCacheEntry(uint32_t address, uint16_t qwords, uint16_t dxt)
{
	uint32_t hash = hashInt32(address);
	const TmemCacheEntryDesc& entry = sTmemCacheEntryDescriptors[hash];
	if (entry.matches(address, qwords, dxt))
		return &sTmemCache[hash];
	else
		return nullptr;
}

static bool tmemTryLoadFromCache(uint32_t address, uint32_t tmemIdx, uint16_t qwords, uint16_t dxt)
{
	Tmem* cacheEntry = tmemFindCacheEntry(address, qwords, dxt);
	if (cacheEntry == nullptr)
		return false;

	uint32_t hash = hashInt32(address);
	const TmemCacheEntryDesc& entry = sTmemCacheEntryDescriptors[hash];
#if 1
	__builtin_memcpy(&TMEM[tmemIdx], cacheEntry->data, qwords << 3);
#else
	__movsd((unsigned long*)&TMEM[tmemIdx], (unsigned long*)cacheEntry->data, qwords << 1);
#endif
	tmemCacheHashSet(tmemIdx, qwords, entry.crc);
	return true;
}

void gDPLoadBlock(u32 tile, u32 uls, u32 ult, u32 lrs, u32 dxt)
{
	gDPSetTileSize( tile, uls, ult, lrs, dxt );
	gDP.loadTileIdx = tile;
	gDP.loadTile = &gDP.tiles[tile];
	gDP.loadTile->loadType = LOADTYPE_BLOCK;

	if (gSP.DMAOffsets.tex_offset != 0) {
		if (gSP.DMAOffsets.tex_shift % (((lrs>>2) + 1) << 3)) {
			gDP.textureImage.address -= gSP.DMAOffsets.tex_shift;
			gSP.DMAOffsets.tex_offset = 0;
			gSP.DMAOffsets.tex_shift = 0;
			gSP.DMAOffsets.tex_count = 0;
		} else
			++gSP.DMAOffsets.tex_count;
	}
	gDP.loadTile->imageAddress = gDP.textureImage.address;

	gDPLoadTileInfo &info = gDP.loadInfo[gDP.loadTile->tmem];
	info.texAddress = gDP.loadTile->imageAddress;
	info.uls = gDP.loadTile->uls;
	info.ult = gDP.loadTile->ult;
	info.lrs = gDP.loadTile->lrs;
	info.lrt = gDP.loadTile->lrt;
	info.width = gDP.loadTile->lrs;
	info.dxt = dxt;
	info.size = gDP.textureImage.size;
	info.loadType = LOADTYPE_BLOCK;

	const u32 width = (lrs - uls + 1) & 0x0FFF;
	u32 bytes = width << gDP.loadTile->size >> 1;
	if ((bytes & 7) != 0)
		bytes = (bytes & (~7)) + 8;

	info.bytes = bytes;
	u32 address = gDP.textureImage.address + ult * gDP.textureImage.bpl + (uls << gDP.textureImage.size >> 1);

	if (bytes == 0 || (address + bytes) > RDRAMSize) {
		DebugMsg(DEBUG_NORMAL | DEBUG_ERROR, "// Attempting to load texture block out of range\n");
		DebugMsg(DEBUG_NORMAL, "gDPLoadBlock( %i, %i, %i, %i, %i );\n", tile, uls, ult, lrs, dxt );
		return;
	}

	gDP.loadTile->frameBufferAddress = 0;
	CheckForFrameBufferTexture(address, info.width, bytes); // Load data to TMEM even if FB texture is found. See comment to texturedRectDepthBufferCopy

	tmemCacheHashInvalidate();

	if (gDP.loadTile->size == G_IM_SIZ_32b)
		gDPLoadBlock32(gDP.loadTile->uls, gDP.loadTile->lrs, dxt);
	else if (gDP.loadTile->format == G_IM_FMT_YUV)
		memcpy(TMEM, &RDRAM[address], bytes); // HACK!
	else {
		u32 tmemAddr = gDP.loadTile->tmem;
		const u32 tmemAddrForCache = tmemAddr;
		u32 destLim = bytes + (tmemAddr << 3);
		bool canCache = destLim <= 0xfff + 1;
		bool load = true;
		if (canCache)
		{
			load = !tmemTryLoadFromCache(address, tmemAddr, bytes >> 3, dxt);
		}

		if (load)
		{
			UnswapCopyWrap<0xFFF>(RDRAM, address, (u8*)TMEM, tmemAddr << 3, bytes);
			if (dxt != 0) {
				u32 dxtCounter = 0;
				u32 qwords = (bytes >> 3);
				u32 line = 0;
				while (true) {
					do {
						++tmemAddr;
						--qwords;
						if (qwords == 0)
							goto end_dxt_test;
						dxtCounter += dxt;
					} while ((dxtCounter & 0x800) == 0);
					do {
						++line;
						--qwords;
						if (qwords == 0)
							goto end_dxt_test;
						dxtCounter += dxt;
					} while ((dxtCounter & 0x800) != 0);
					DWordInterleaveWrap((u32*)TMEM, tmemAddr << 1, 0x3FF, line);
					tmemAddr += line;
					line = 0;
				}
			end_dxt_test:
				DWordInterleaveWrap((u32*)TMEM, tmemAddr << 1, 0x3FF, line);
			}

			if (canCache)
			{
				tmemAddCacheEntry(address, tmemAddrForCache, bytes >> 3, dxt);
			}
		}
	}

	DebugMsg( DEBUG_NORMAL, "gDPLoadBlock( %i, %i, %i, %i, %i );\n", tile, uls, ult, lrs, dxt );
}

// Compiler will play dumb if I don't do this (and it still plays a bit dumb)
static void copyFastPalette(const u8* __restrict src, u16* __restrict dst)
{
	for (u16 j = 0; j < 16; ++j) {
		dst[j * 4] = swapword(*(const u16*)(src + ((j * 2) ^ 2)));
	}
}

void gDPLoadTLUT( u32 tile, u32 uls, u32 ult, u32 lrs, u32 lrt )
{
	gDPSetTileSize( tile, uls, ult, lrs, lrt );
	if (gDP.tiles[tile].tmem < 256) {
		DebugMsg(DEBUG_NORMAL | DEBUG_ERROR, "gDPLoadTLUT wrong tile tmem addr: tile[%d].tmem=%04x;\n", tile, gDP.tiles[tile].tmem);
		return;
	}
	u16 count = (u16)((gDP.tiles[tile].lrs - gDP.tiles[tile].uls + 1) * (gDP.tiles[tile].lrt - gDP.tiles[tile].ult + 1));
	u32 address = gDP.textureImage.address + gDP.tiles[tile].ult * gDP.textureImage.bpl + (gDP.tiles[tile].uls << gDP.textureImage.size >> 1);
	u16 pal = (u16)((gDP.tiles[tile].tmem - 256) >> 4);
	u16 * dest = reinterpret_cast<u16*>(TMEM);
	u32 destIdx = 0x400 | (gDP.tiles[tile].tmem << 2);

	if (TMEMCacheHash.off > 0x400)
		tmemCacheHashInvalidate();

	if ((0 == (address & 0x3)) && (destIdx < 0x800 - count * 4))
	{
		int i = 0;
		while (i < count) {
			if (i + 16 < count)
			{
				copyFastPalette(RDRAM + address, dest + destIdx);
				i += 16;
				address += 32;
				destIdx += 64;
			}
			else
			{
				for (u16 j = 0; (j < 16) && (i < count); ++j, ++i) {
					dest[destIdx] = swapword(*(u16*)(RDRAM + (address ^ 2)));
					address += 2;
					destIdx += 4;
				}
			}

			gDP.paletteCRC16[pal] = CRC_CalculatePalette(0xFFFFFFFF, &TMEM[256 + (pal << 4)]);
			pal = (pal + 1) & 0x0F;
		}
	}
	else
	{
		int i = 0;
		while (i < count) {
			for (u16 j = 0; (j < 16) && (i < count); ++j, ++i) {
				dest[(destIdx | 0x0400) & 0x07FF] = swapword(*(u16*)(RDRAM + (address ^ 2)));
				address += 2;
				destIdx += 4;
			}

			gDP.paletteCRC16[pal] = CRC_CalculatePalette(0xFFFFFFFF, &TMEM[256 + (pal << 4)]);
			pal = (pal + 1) & 0x0F;
		}
	}

	gDP.paletteCRC256 = CRC_Calculate(0xFFFFFFFF, gDP.paletteCRC16, 64);

	if (TFH.isInited()) {
		const u16 start = gDP.tiles[tile].tmem - 256; // starting location in the palettes
		u16 *spal = (u16*)(RDRAM + gDP.textureImage.address);
		memcpy((u8*)(gDP.TexFilterPalette + start), spal, count<<1);
	}

	gDP.changed |= CHANGED_TMEM;

	DebugMsg( DEBUG_NORMAL, "gDPLoadTLUT( %i, %i, %i, %i, %i );\n",
		tile, gDP.tiles[tile].uls, gDP.tiles[tile].ult, gDP.tiles[tile].lrs, gDP.tiles[tile].lrt );
}

void gDPSetScissor(u32 mode, s16 xh, s16 yh, s16 xl, s16 yl)
{
	gDP.scissor.mode = mode;
	gDP.scissor.xh = xh;
	gDP.scissor.yh = yh;
	gDP.scissor.xl = xl;
	gDP.scissor.yl = yl;
	gDP.scissor.ulx = _FIXED2FLOAT(xh, 2);
	gDP.scissor.uly = _FIXED2FLOAT(yh, 2);
	gDP.scissor.lrx = _FIXED2FLOAT(xl, 2);
	gDP.scissor.lry = _FIXED2FLOAT(yl, 2);

	gDP.changed |= CHANGED_SCISSOR | CHANGED_REJECT_BOX;

#ifdef DEBUG_DUMP
	DebugMsg( DEBUG_NORMAL, "gDPSetScissor( %s, %.2f, %.2f, %.2f, %.2f );\n",
		ScissorModeText[gDP.scissor.mode],
		gDP.scissor.ulx,
		gDP.scissor.uly,
		gDP.scissor.lrx,
		gDP.scissor.lry );
#endif
}

// This performs the same thing action as gDPFillRectangle but explicitly specifying what to overwrite
void gDPMemset(u32 value, u32 addr, u32 length)
{
	u32 uly = 0U, lry = 0U;
	u32 fillColor = value;

	auto calculateParams = [&](u32 imageAddr, u32 imageSize, u32 imageWidth)
		{
			if (imageSize == G_IM_SIZ_16b)
				fillColor |= value << 16;
			else if (imageSize == G_IM_SIZ_8b)
				fillColor |= (value << 24) | (value << 16) | (value << 8);
			getFillColor(fillColor, &gDP.rectColor.r);

			// Deduce ulx, uly, lrx, lry from addr, size, and colorImageStart
			// Currently I am assuming a simple letterbox case - top and bottom are not cleared
			// ulx = 0;
			// lrx = screenWidth;
			const u32 lineSize = imageWidth << imageSize >> 1;
			uly = (addr - imageAddr) / lineSize;
			lry = uly + length / lineSize;
		};

	GraphicsDrawer& drawer = dwnd().getDrawer();

	u32 imageWidth = VI.width;
	u32 imageHeight = VI.height;
	const u32 depthImageStart = gDP.depthImageAddress;
	const u32 depthImageEnd = gDP.depthImageAddress + imageWidth * imageHeight * 2;
	if (depthImageStart <= addr && addr < depthImageEnd) {
		ValueKeeper<u32> backupColorImageSize(gDP.colorImage.size, G_IM_SIZ_16b);
		calculateParams(gDP.depthImageAddress, G_IM_SIZ_16b, imageWidth);

		// HACK: this usually replaces gDPSetColorImage for zb so save zb
		frameBufferList().saveBuffer(gDP.depthImageAddress, (u16)G_IM_FMT_RGBA, (u16)G_IM_SIZ_16b, (u16)imageWidth, false);

		if (config.generalEmulation.enableFragmentDepthWrite == 0)
			drawer.clearDepthBuffer();
		else
			depthBufferList().setCleared(true);

		if (config.generalEmulation.enableFragmentDepthWrite != 0) {
			// Pretend that we are drawing the rectangle over ZB with fill mode
			ValueKeeper<u32> backupColorImageAddress(gDP.colorImage.address, gDP.depthImageAddress);
			ValueKeeper<u32> backupWidth(gDP.colorImage.width, imageWidth);
			gDPInfo::OtherMode otherMode = gDP.otherMode;
			otherMode.cycleType = G_CYC_FILL;
			ValueKeeper<gDPInfo::OtherMode> backupOtherMode(gDP.otherMode, otherMode);
			drawer.drawRect(0, uly, imageWidth, lry);
			frameBufferList().setBufferChanged(f32(lry));
		}
	}
	else if (0 == config.frameBufferEmulation.enable) {
		// FB clear
		// This heavily assume "niceness" of developers to bind color buffer before memset...
		imageWidth = gDP.colorImage.width;
		const u32 colorImageStart = gDP.colorImage.address;
		const u32 colorImageEnd = gDP.colorImage.address + ((imageWidth * imageHeight) << gDP.colorImage.size >> 1);
		if (colorImageStart <= addr && addr < colorImageEnd) {
			calculateParams(gDP.colorImage.address, gDP.colorImage.size, imageWidth);
			ValueKeeper<u32> backupFillColor(gDP.fillColor.color, fillColor);
			gDPInfo::OtherMode otherMode = gDP.otherMode;
			otherMode.cycleType = G_CYC_FILL;
			ValueKeeper<gDPInfo::OtherMode> backupOtherMode(gDP.otherMode, otherMode);
			drawer.drawRect(0, uly, imageWidth, lry);
		}
	}
	else if (const auto pBuffer = frameBufferList().findBuffer(addr)) {
		imageWidth = pBuffer->m_width;
		calculateParams(pBuffer->m_startAddress, pBuffer->m_size, imageWidth);
		{
			ValueKeeper<u32> backupFillColor(gDP.fillColor.color, fillColor);
			gDPInfo::OtherMode otherMode = gDP.otherMode;
			otherMode.cycleType = G_CYC_FILL;
			ValueKeeper<gDPInfo::OtherMode> backupOtherMode(gDP.otherMode, otherMode);
			const auto backupCurr = frameBufferList().getCurrent();
			frameBufferList().setCurrent(pBuffer);

			drawer.drawRect(0, uly, imageWidth, lry);

			pBuffer->setBufferClearParams(gDP.fillColor.color, 0, uly, imageWidth, lry);
			frameBufferList().setBufferChanged(f32(lry));
			frameBufferList().setCurrent(backupCurr);
		}
	}

	// Memset
	u32* pDest = reinterpret_cast<u32*>(RDRAM + addr);
	u32 lengthInDwords = length >> 2;
	for (u32 i = 0; i < lengthInDwords; i++) {
		pDest[i] = fillColor;
	}
}

void gDPFillRectangle( s32 ulx, s32 uly, s32 lrx, s32 lry )
{
	GraphicsDrawer & drawer = dwnd().getDrawer();
	if (gDP.otherMode.cycleType == G_CYC_FILL) {
		++lrx;
		++lry;
	} else if (lry == uly)
		++lry;

	enum {
		dbNone,
		dbFound,
		dbCleared
	} depthBuffer = dbNone;
	if (gDP.depthImageAddress == gDP.colorImage.address) {
		// Game may use depth texture as auxilary color texture. Example: Mario Tennis
		// If color is not depth clear color, that is most likely the case
		if (gDP.fillColor.color == DepthClearColor) {
			depthBuffer = dbFound;
			if (config.generalEmulation.enableFragmentDepthWrite == 0) {
				drawer.clearDepthBuffer();
				depthBuffer = dbCleared;
			} else
				depthBufferList().setCleared(true);
		}
	} else if (gDP.fillColor.color == DepthClearColor && gDP.otherMode.cycleType == G_CYC_FILL) {
		depthBuffer = dbFound;
		depthBufferList().saveBuffer(gDP.colorImage.address);
		if (config.generalEmulation.enableFragmentDepthWrite == 0 ||
			(config.generalEmulation.hacks & hack_Snap) != 0) {
			drawer.clearDepthBuffer();
			depthBuffer = dbCleared;
		} else
			depthBufferList().setCleared(true);
	}

	if (depthBuffer != dbCleared) {
		if (gDP.otherMode.cycleType == G_CYC_FILL) {
			f32 fillColor[4];
			gDPGetFillColor(fillColor);
			gDP.rectColor.r = fillColor[0];
			gDP.rectColor.g = fillColor[1];
			gDP.rectColor.b = fillColor[2];
			gDP.rectColor.a = fillColor[3];
		} else {
			gDP.rectColor = gDPInfo::Color();
		}
		drawer.drawRect(ulx, uly, lrx, lry);
	}

	if (gDP.otherMode.cycleType == G_CYC_FILL)
		frameBufferList().fillRDRAM(ulx, uly, lrx, lry);

	frameBufferList().setBufferChanged(f32(lry));

	DebugMsg( DEBUG_NORMAL, "gDPFillRectangle #%i- #%i ( %i, %i, %i, %i );\n", gSP.tri_num, gSP.tri_num +1, ulx, uly, lrx, lry );
	gSP.tri_num += 2;
}

void gDPSetConvert( s32 k0, s32 k1, s32 k2, s32 k3, s32 k4, s32 k5 )
{
// angrylion's macro
#define SIGN(x, numb)	(((x) & ((1 << numb) - 1)) | -((x) & (1 << (numb - 1))))

	gDP.convert.k0 = (SIGN(k0, 9) << 1) + 1;
	gDP.convert.k1 = (SIGN(k1, 9) << 1) + 1;
	gDP.convert.k2 = (SIGN(k2, 9) << 1) + 1;
	gDP.convert.k3 = (SIGN(k3, 9) << 1) + 1;
	gDP.convert.k4 = k4;
	gDP.convert.k5 = k5;

	DebugMsg( DEBUG_NORMAL, "gDPSetConvert( %i, %i, %i, %i, %i, %i );\n", k0, k1, k2, k3, k4, k5);
}

void gDPSetKeyR( u32 cR, u32 sR, u32 wR )
{
	gDP.key.center.r = 	_FIXED2FLOATCOLOR( cR, 8 );
	gDP.key.scale.r = 	_FIXED2FLOATCOLOR( sR, 8 );
	gDP.key.width.r = 	_FIXED2FLOATCOLOR( wR, 8 );
	DebugMsg( DEBUG_NORMAL, "gDPSetKeyR( %u, %u, %u );\n", cR, sR, wR );
}

void gDPSetKeyGB(u32 cG, u32 sG, u32 wG, u32 cB, u32 sB, u32 wB )
{
	gDP.key.center.g = 	_FIXED2FLOATCOLOR( cG, 8 );
	gDP.key.scale.g = 	_FIXED2FLOATCOLOR( sG, 8 );
	gDP.key.width.g = 	_FIXED2FLOATCOLOR( wG, 8 );
	gDP.key.center.b = 	_FIXED2FLOATCOLOR( cB, 8 );
	gDP.key.scale.b = 	_FIXED2FLOATCOLOR( sB, 8 );
	gDP.key.width.b = 	_FIXED2FLOATCOLOR( wB, 8 );
	DebugMsg( DEBUG_NORMAL, "gDPSetKeyGB( %u, %u, %u, %u, %u, %u );\n",
			  cG, sG, wG, cB, sB, wB );
}

void gDPTextureRectangle(f32 ulx, f32 uly, f32 lrx, f32 lry, s32 tile, s16 s, s16 t, f32 dsdx, f32 dtdy , bool flip)
{
	if (gDP.otherMode.cycleType == G_CYC_COPY) {
		dsdx /= 4.0f;
		lrx += 1.0f;
		lry += 1.0f;
	}
	lry = max(lry, uly + 1.0f);

	gDPTile *textureTileOrg[2];
	textureTileOrg[0] = gSP.textureTile[0];
	textureTileOrg[1] = gSP.textureTile[1];
	gSP.textureTile[0] = &gDP.tiles[tile];
	gSP.textureTile[1] = &gDP.tiles[(tile + 1) & 7];

	// HACK ALERT!
	if (s == 0x4000 && (gDP.colorImage.width + gSP.textureTile[0]->uls < 512))
		s = 0;

	gDP.rectColor = gDPInfo::Color();
	if (gDP.otherMode.cycleType < G_CYC_COPY) {
		if ((config.generalEmulation.hacks & hack_texrect_shade_alpha) != 0 &&
			gDP.combine.mA0 == G_ACMUX_0 && gDP.combine.aA0 == G_ACMUX_SHADE)
			gDP.rectColor.a = 1.0f;
	}

	GraphicsDrawer & drawer = dwnd().getDrawer();
	GraphicsDrawer::TexturedRectParams params(ulx, uly, lrx, lry, dsdx, dtdy, s, t,
		flip, false, true, frameBufferList().getCurrent());
	if (config.graphics2D.enableNativeResTexrects == 0 && config.graphics2D.correctTexrectCoords != Config::tcDisable)
		drawer.correctTexturedRectParams(params);
	drawer.drawTexturedRect(params);

	gSP.textureTile[0] = textureTileOrg[0];
	gSP.textureTile[1] = textureTileOrg[1];

	frameBufferList().setBufferChanged(lry);

	if (flip)
		DebugMsg( DEBUG_NORMAL, "gDPTextureRectangleFlip( %f, %f, %f, %f, %i, %f, %f, %f, %f);\n",
				  ulx, uly, lrx, lry, tile, s/32.0f, t/32.0f, dsdx, dtdy );
	else
		DebugMsg( DEBUG_NORMAL, "gDPTextureRectangle( %f, %f, %f, %f, %i, %f, %f, %f, %f );\n",
				  ulx, uly, lrx, lry, tile, s/32.0f, t/32.0f, dsdx, dtdy);
	gSP.tri_num += 2;
}

void gDPFullSync()
{
	if (config.frameBufferEmulation.copyAuxToRDRAM != 0) {
		frameBufferList().copyAux();
		frameBufferList().removeAux();
	}

	dwnd().getDrawer().flush();

	frameBufferList().updateCurrentBufferEndAddress();

	FrameBuffer * pCurrentBuffer = frameBufferList().getCurrent();
	if ((config.frameBufferEmulation.copyToRDRAM != Config::ctDisable || (config.generalEmulation.hacks & hack_subscreen) != 0) &&
		!FBInfo::fbInfo.isSupported() &&
		pCurrentBuffer != nullptr &&
		!pCurrentBuffer->isAuxiliary()
	)
		FrameBuffer_CopyToRDRAM(gDP.colorImage.address, config.frameBufferEmulation.copyToRDRAM == Config::ctSync);

	if (RSP.LLE) {
		if (config.frameBufferEmulation.copyDepthToRDRAM != Config::cdDisable && !FBInfo::fbInfo.isSupported())
			FrameBuffer_CopyDepthBuffer(gDP.colorImage.address);
	}

	*REG.MI_INTR |= MI_INTR_DP;

	CheckInterrupts();

	DebugMsg( DEBUG_NORMAL, "gDPFullSync();\n" );
}

void gDPTileSync()
{
	DebugMsg( DEBUG_NORMAL | DEBUG_IGNORED, "gDPTileSync();\n" );
}

void gDPPipeSync()
{
	DebugMsg( DEBUG_NORMAL | DEBUG_IGNORED, "gDPPipeSync();\n" );
}

void gDPLoadSync()
{
	DebugMsg( DEBUG_NORMAL | DEBUG_IGNORED, "gDPLoadSync();\n" );
}

void gDPNoOp()
{
	DebugMsg( DEBUG_NORMAL | DEBUG_IGNORED, "gDPNoOp();\n" );
}

/*******************************************
 *          Low level triangle             *
 *******************************************
 *    based on sources of ziggy's z64      *
 *******************************************/

void gDPLLETriangle(u32 _w1, u32 _w2, int _shade, int _texture, int _zbuffer, u32 * _pRdpCmd)
{
	gSP.texture.level = _SHIFTR(_w1, 19, 3);
	const u32 tile = _SHIFTR(_w1, 16, 3);
	gDPTile *textureTileOrg[2];
	textureTileOrg[0] = gSP.textureTile[0];
	textureTileOrg[1] = gSP.textureTile[1];
	gSP.textureTile[0] = &gDP.tiles[tile];
	gSP.textureTile[1] = &gDP.tiles[(tile + 1) & 7];

	int j;
	int xleft, xright, xleft_inc, xright_inc;
	int r, g, b, a, z, s, t, w;
	int drdx = 0, dgdx = 0, dbdx = 0, dadx = 0, dzdx = 0, dsdx = 0, dtdx = 0, dwdx = 0;
	int drde = 0, dgde = 0, dbde = 0, dade = 0, dzde = 0, dsde = 0, dtde = 0, dwde = 0;
	int flip = (_w1 & 0x800000) ? 1 : 0;

	s32 yl, ym, yh;
	s32 xl, xm, xh;
	s32 dxldy, dxhdy, dxmdy;
	u32 w3, w4, w5, w6, w7, w8;

	u32 * shade_base = _pRdpCmd + 8;
	u32 * texture_base = _pRdpCmd + 8;
	u32 * zbuffer_base = _pRdpCmd + 8;

	if (_shade != 0) {
		texture_base += 16;
		zbuffer_base += 16;
	}
	if (_texture != 0) {
		zbuffer_base += 16;
	}

	w3 = _pRdpCmd[2];
	w4 = _pRdpCmd[3];
	w5 = _pRdpCmd[4];
	w6 = _pRdpCmd[5];
	w7 = _pRdpCmd[6];
	w8 = _pRdpCmd[7];

	yl = (_w1 & 0x3fff);
	ym = ((_w2 >> 16) & 0x3fff);
	yh = ((_w2 >>  0) & 0x3fff);
	xl = (s32)(w3);
	xh = (s32)(w5);
	xm = (s32)(w7);
	dxldy = (s32)(w4);
	dxhdy = (s32)(w6);
	dxmdy = (s32)(w8);

	if (yl & (0x800<<2)) yl |= 0xfffff000<<2;
	if (ym & (0x800<<2)) ym |= 0xfffff000<<2;
	if (yh & (0x800<<2)) yh |= 0xfffff000<<2;

	yh &= ~3;

	r = 0xff; g = 0xff; b = 0xff; a = 0xff; z = 0xffff0000; s = 0;  t = 0;  w = 0x30000;

	if (_shade != 0) {
		r    = (shade_base[0] & 0xffff0000) | ((shade_base[+4 ] >> 16) & 0x0000ffff);
		g    = ((shade_base[0 ] << 16) & 0xffff0000) | (shade_base[4 ] & 0x0000ffff);
		b    = (shade_base[1 ] & 0xffff0000) | ((shade_base[5 ] >> 16) & 0x0000ffff);
		a    = ((shade_base[1 ] << 16) & 0xffff0000) | (shade_base[5 ] & 0x0000ffff);
		drdx = (shade_base[2 ] & 0xffff0000) | ((shade_base[6 ] >> 16) & 0x0000ffff);
		dgdx = ((shade_base[2 ] << 16) & 0xffff0000) | (shade_base[6 ] & 0x0000ffff);
		dbdx = (shade_base[3 ] & 0xffff0000) | ((shade_base[7 ] >> 16) & 0x0000ffff);
		dadx = ((shade_base[3 ] << 16) & 0xffff0000) | (shade_base[7 ] & 0x0000ffff);
		drde = (shade_base[8 ] & 0xffff0000) | ((shade_base[12] >> 16) & 0x0000ffff);
		dgde = ((shade_base[8 ] << 16) & 0xffff0000) | (shade_base[12] & 0x0000ffff);
		dbde = (shade_base[9 ] & 0xffff0000) | ((shade_base[13] >> 16) & 0x0000ffff);
		dade = ((shade_base[9 ] << 16) & 0xffff0000) | (shade_base[13] & 0x0000ffff);
	}
	if (_texture != 0) {
		s    = (texture_base[0 ] & 0xffff0000) | ((texture_base[4 ] >> 16) & 0x0000ffff);
		t    = ((texture_base[0 ] << 16) & 0xffff0000)      | (texture_base[4 ] & 0x0000ffff);
		w    = (texture_base[1 ] & 0xffff0000) | ((texture_base[5 ] >> 16) & 0x0000ffff);
		//    w = abs(w);
		dsdx = (texture_base[2 ] & 0xffff0000) | ((texture_base[6 ] >> 16) & 0x0000ffff);
		dtdx = ((texture_base[2 ] << 16) & 0xffff0000)      | (texture_base[6 ] & 0x0000ffff);
		dwdx = (texture_base[3 ] & 0xffff0000) | ((texture_base[7 ] >> 16) & 0x0000ffff);
		dsde = (texture_base[8 ] & 0xffff0000) | ((texture_base[12] >> 16) & 0x0000ffff);
		dtde = ((texture_base[8 ] << 16) & 0xffff0000)      | (texture_base[12] & 0x0000ffff);
		dwde = (texture_base[9 ] & 0xffff0000) | ((texture_base[13] >> 16) & 0x0000ffff);
	}
	if (_zbuffer != 0) {
		z    = zbuffer_base[0];
		dzdx = zbuffer_base[1];
		dzde = zbuffer_base[2];
	}

	xh <<= 2;  xm <<= 2;  xl <<= 2;
	r <<= 2;  g <<= 2;  b <<= 2;  a <<= 2;
	dsde >>= 2;  dtde >>= 2;  dsdx >>= 2;  dtdx >>= 2;
	dzdx >>= 2;  dzde >>= 2;
	dwdx >>= 2;  dwde >>= 2;

#define XSCALE(x) (float(x)/(1<<18))
#define YSCALE(y) (float(y)/(1<<2))
#define ZSCALE(z) ((gDP.otherMode.depthSource == G_ZS_PRIM)? gDP.primDepth.z : float(u32(z))/0xffff0000)
#define PERSP_EN (gDP.otherMode.texturePersp != 0)
#define WSCALE(z) 1.0f/(PERSP_EN? (float(u32(z) + 0x10000)/0xffff0000) : 1.0f)
#define CSCALE(c) _FIXED2FLOATCOLOR((((c)>0x3ff0000? 0x3ff0000:((c)<0? 0 : (c)))>>18), 8)
#define _PERSP(w) ( w )
#define PERSP(s, w) ( ((s64)(s) << 20) / (_PERSP(w)? _PERSP(w):1) )
#define SSCALE(s, _w) (PERSP_EN? float(PERSP(s, _w))/(1 << 10) : float(s)/(1<<21))
#define TSCALE(s, w) (PERSP_EN? float(PERSP(s, w))/(1 << 10) : float(s)/(1<<21))

	GraphicsDrawer & drawer = dwnd().getDrawer();
	drawer.setDMAVerticesSize(16);
	SPVertex * vtx0 = drawer.getDMAVerticesData();
	SPVertex * vtx = vtx0;

	xleft = xm;
	xright = xh;
	xleft_inc = dxmdy;
	xright_inc = dxhdy;

	while (yh<ym &&
		!((!flip && xleft < xright+0x10000) ||
		 (flip && xleft > xright-0x10000))) {
		xleft += xleft_inc;
		xright += xright_inc;
		s += dsde;    t += dtde;    w += dwde;
		r += drde;    g += dgde;    b += dbde;    a += dade;
		z += dzde;
		yh++;
	}

	j = ym-yh;
	if (j > 0) {
		int dx = (xleft-xright)>>16;
		if ((!flip && xleft < xright) || (flip/* && xleft > xright*/))
		{
			if (_shade != 0) {
				vtx->r = CSCALE(r+drdx*dx);
				vtx->g = CSCALE(g+dgdx*dx);
				vtx->b = CSCALE(b+dbdx*dx);
				vtx->a = CSCALE(a+dadx*dx);
			}
			if (_texture != 0) {
				vtx->s = SSCALE(s+dsdx*dx, w+dwdx*dx);
				vtx->t = TSCALE(t+dtdx*dx, w+dwdx*dx);
			}
			vtx->x = XSCALE(xleft);
			vtx->y = YSCALE(yh);
			vtx->z = ZSCALE(z+dzdx*dx);
			vtx->w = WSCALE(w+dwdx*dx);
			++vtx;
		}
		if ((!flip/* && xleft < xright*/) || (/*flip &&*/ xleft > xright))
		{
			if (_shade != 0) {
				vtx->r = CSCALE(r);
				vtx->g = CSCALE(g);
				vtx->b = CSCALE(b);
				vtx->a = CSCALE(a);
			}
			if (_texture != 0) {
				vtx->s = SSCALE(s, w);
				vtx->t = TSCALE(t, w);
			}
			vtx->x = XSCALE(xright);
			vtx->y = YSCALE(yh);
			vtx->z = ZSCALE(z);
			vtx->w = WSCALE(w);
			++vtx;
		}
		xleft += xleft_inc*j;  xright += xright_inc*j;
		s += dsde*j;  t += dtde*j;
		if (w + dwde*j != 0) w += dwde*j;
		else w += dwde*(j-1);
		r += drde*j;  g += dgde*j;  b += dbde*j;  a += dade*j;
		z += dzde*j;
		// render ...
	}

	if (xl != xh)
		xleft = xl;

	//if (yl-ym > 0)
	{
		int dx = (xleft-xright)>>16;
		if ((!flip && xleft <= xright) ||
				(flip/* && xleft >= xright*/))
		{
			if (_shade != 0) {
				vtx->r = CSCALE(r+drdx*dx);
				vtx->g = CSCALE(g+dgdx*dx);
				vtx->b = CSCALE(b+dbdx*dx);
				vtx->a = CSCALE(a+dadx*dx);
			}
			if (_texture != 0) {
				vtx->s = SSCALE(s+dsdx*dx, w+dwdx*dx);
				vtx->t = TSCALE(t+dtdx*dx, w+dwdx*dx);
			}
			vtx->x = XSCALE(xleft);
			vtx->y = YSCALE(ym);
			vtx->z = ZSCALE(z+dzdx*dx);
			vtx->w = WSCALE(w+dwdx*dx);
			++vtx;
		}
		if ((!flip/* && xleft <= xright*/) ||
				(/*flip && */xleft >= xright))
		{
			if (_shade != 0) {
				vtx->r = CSCALE(r);
				vtx->g = CSCALE(g);
				vtx->b = CSCALE(b);
				vtx->a = CSCALE(a);
			}
			if (_texture != 0) {
				vtx->s = SSCALE(s, w);
				vtx->t = TSCALE(t, w);
			}
			vtx->x = XSCALE(xright);
			vtx->y = YSCALE(ym);
			vtx->z = ZSCALE(z);
			vtx->w = WSCALE(w);
			++vtx;
		}
	}
	xleft_inc = dxldy;
	xright_inc = dxhdy;

	j = yl-ym;
	//j--; // ?
	xleft += xleft_inc*j;  xright += xright_inc*j;
	s += dsde*j;  t += dtde*j;  w += dwde*j;
	r += drde*j;  g += dgde*j;  b += dbde*j;  a += dade*j;
	z += dzde*j;

	while (yl>ym &&
		   !((!flip && xleft < xright+0x10000) ||
			 (flip && xleft > xright-0x10000))) {
		xleft -= xleft_inc;    xright -= xright_inc;
		s -= dsde;    t -= dtde;    w -= dwde;
		r -= drde;    g -= dgde;    b -= dbde;    a -= dade;
		z -= dzde;
		--j;
		--yl;
	}

	// render ...
	if (j >= 0) {
		int dx = (xleft-xright)>>16;
		if ((!flip && xleft <= xright) ||
				(flip/* && xleft >= xright*/))
		{
			if (_shade != 0) {
				vtx->r = CSCALE(r+drdx*dx);
				vtx->g = CSCALE(g+dgdx*dx);
				vtx->b = CSCALE(b+dbdx*dx);
				vtx->a = CSCALE(a+dadx*dx);
			}
			if (_texture != 0) {
				vtx->s = SSCALE(s+dsdx*dx, w+dwdx*dx);
				vtx->t = TSCALE(t+dtdx*dx, w+dwdx*dx);
			}
			vtx->x = XSCALE(xleft);
			vtx->y = YSCALE(yl);
			vtx->z = ZSCALE(z+dzdx*dx);
			vtx->w = WSCALE(w+dwdx*dx);
			++vtx;
		}
		if ((!flip/* && xleft <= xright*/) ||
				(/*flip &&*/ xleft >= xright))
		{
			if (_shade != 0) {
				vtx->r = CSCALE(r);
				vtx->g = CSCALE(g);
				vtx->b = CSCALE(b);
				vtx->a = CSCALE(a);
			}
			if (_texture != 0) {
				vtx->s = SSCALE(s, w);
				vtx->t = TSCALE(t, w);
			}
			vtx->x = XSCALE(xright);
			vtx->y = YSCALE(yl);
			vtx->z = ZSCALE(z);
			vtx->w = WSCALE(w);
			++vtx;
		}
	}

	if (_texture != 0)
		gDP.changed |= CHANGED_TILE;
	if (_zbuffer != 0)
		gSP.geometryMode |= G_ZBUFFER;

	drawer.drawScreenSpaceTriangle(static_cast<u32>(vtx - vtx0));
	gSP.textureTile[0] = textureTileOrg[0];
	gSP.textureTile[1] = textureTileOrg[1];

	DebugMsg( DEBUG_NORMAL, "gDPLLETriangle(%08x, %08x) shade: %d, texture: %d, zbuffer: %d\n",
			  _w1, _w2, _shade, _texture, _zbuffer);
}

static void gDPTriangle(u32 _w1, u32 _w2, int shade, int texture, int zbuffer)
{
	gDPLLETriangle(_w1, _w2, shade, texture, zbuffer, RDP.cmd_data + RDP.cmd_cur);
}

void gDPTriFill(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 0, 0, 0);
	DebugMsg( DEBUG_NORMAL, "trifill\n");
}

void gDPTriShade(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 1, 0, 0);
	DebugMsg( DEBUG_NORMAL, "trishade\n");
}

void gDPTriTxtr(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 0, 1, 0);
	DebugMsg( DEBUG_NORMAL, "tritxtr\n");
}

void gDPTriShadeTxtr(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 1, 1, 0);
	DebugMsg( DEBUG_NORMAL, "trishadetxtr\n");
}

void gDPTriFillZ(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 0, 0, 1);
	DebugMsg( DEBUG_NORMAL, "trifillz\n");
}

void gDPTriShadeZ(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 1, 0, 1);
	DebugMsg( DEBUG_NORMAL, "trishadez\n");
}

void gDPTriTxtrZ(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 0, 1, 1);
	DebugMsg( DEBUG_NORMAL, "tritxtrz\n");
}

void gDPTriShadeTxtrZ(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 1, 1, 1);
	DebugMsg( DEBUG_NORMAL, "trishadetxtrz\n");
}
