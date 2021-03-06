#include <string.h>
#include "nds_header.h"
#include "module_params.h"
#include "patch.h"
#include "find.h"
#include "common.h"
#include "cardengine_header_arm9.h"
#include "debug_file.h"

//#define memcpy __builtin_memcpy // memcpy

bool cardReadFound = false; // card_patcher_common.c

static u32* debug = (u32*)DEBUG_PATCH_LOCATION;

u32 patchCardNdsArm9(cardengineArm9* ce9, const tNDSHeader* ndsHeader, const module_params_t* moduleParams, u32 patchMpuRegion, u32 patchMpuSize) {
	const char* ROM_TID = getRomTid(ndsHeader);
	bool sdk5 = isSdk5(moduleParams);
	
	debug[4] = (u32)ndsHeader->arm9destination;
	debug[8] = moduleParams->sdk_version;

	bool usesThumb = false;
	int readType = 0;
	int sdk5ReadType = 0; // SDK 5

	// Card read
	// SDK 5
	//dbg_printf("Trying SDK 5 thumb...\n");
	u32* cardReadEndOffset = (u32*)findCardReadEndOffsetThumb5Type0(ndsHeader, moduleParams);
	if (!cardReadEndOffset) {
		// SDK 5
		cardReadEndOffset = (u32*)findCardReadEndOffsetThumb5Type1(ndsHeader, moduleParams);
		if (cardReadEndOffset) {
			sdk5ReadType = 1;
		}
	}
	if (!cardReadEndOffset) {
		//dbg_printf("Trying thumb...\n");
		cardReadEndOffset = (u32*)findCardReadEndOffsetThumb(ndsHeader);
	}
	if (cardReadEndOffset) {
		usesThumb = true;
	} else {
		cardReadEndOffset = findCardReadEndOffsetType0(ndsHeader, moduleParams);
		if (!cardReadEndOffset) {
			//dbg_printf("Trying alt...\n");
			cardReadEndOffset = findCardReadEndOffsetType1(ndsHeader);
			if (cardReadEndOffset) {
				readType = 1;
				if (*(cardReadEndOffset - 1) == 0xFFFFFE00) {
					dbg_printf("Found thumb\n\n");
					--cardReadEndOffset;
					usesThumb = true;
				}
			}
		}
	}
	if (!cardReadEndOffset) {
		return 0;
	}
	debug[1] = (u32)cardReadEndOffset;
	u32* cardReadStartOffset;
	// SDK 5
	//dbg_printf("Trying SDK 5 thumb...\n");
	if (sdk5ReadType == 0) {
		cardReadStartOffset = (u32*)findCardReadStartOffsetThumb5Type0(moduleParams, (u16*)cardReadEndOffset);
	} else {
		cardReadStartOffset = (u32*)findCardReadStartOffsetThumb5Type1(moduleParams, (u16*)cardReadEndOffset);
	}
	if (!cardReadStartOffset) {
		//dbg_printf("Trying thumb...\n");
		cardReadStartOffset = (u32*)findCardReadStartOffsetThumb((u16*)cardReadEndOffset);
	}
	if (!cardReadStartOffset) {
		//dbg_printf("Trying SDK 5...\n");
		cardReadStartOffset = (u32*)findCardReadStartOffset5(moduleParams, cardReadEndOffset);
	}
	if (!cardReadStartOffset) {
		if (readType == 0) {
			cardReadStartOffset = findCardReadStartOffsetType0(cardReadEndOffset);
		} else {
			cardReadStartOffset = findCardReadStartOffsetType1(cardReadEndOffset);
		}
	}
	if (!cardReadStartOffset) {
		return 0;
	}
	cardReadFound = true;

	// Card read cached
	u32* cardReadCachedEndOffset = findCardReadCachedEndOffset(ndsHeader, moduleParams);
	u32* cardReadCachedStartOffset = findCardReadCachedStartOffset(moduleParams, cardReadCachedEndOffset);
	u32 needFlushCache = (patchMpuRegion == 1) ? 1 : 0;

	// Card pull out
	u32* cardPullOutOffset;
	if (usesThumb) {
		//dbg_printf("Trying SDK 5 thumb...\n");
		if (sdk5ReadType == 0) {
			cardPullOutOffset = (u32*)findCardPullOutOffsetThumb5Type0(ndsHeader, moduleParams);
		} else {
			cardPullOutOffset = (u32*)findCardPullOutOffsetThumb5Type1(ndsHeader, moduleParams);
		}
		if (!cardPullOutOffset) {
			//dbg_printf("Trying thumb...\n");
			cardPullOutOffset = (u32*)findCardPullOutOffsetThumb(ndsHeader);
		}
	} else {
		cardPullOutOffset = findCardPullOutOffset(ndsHeader, moduleParams);
	}

	// Force to power off
	//u32* forceToPowerOffOffset = findForceToPowerOffOffset(ndsHeader);

	// Find the card id
	u32* cardIdEndOffset;
	u32* cardIdStartOffset;
	if (usesThumb) {
		cardIdEndOffset = (u32*)findCardIdEndOffsetThumb(ndsHeader, moduleParams, (u16*)cardReadEndOffset);
		cardIdStartOffset = (u32*)findCardIdStartOffsetThumb(moduleParams, (u16*)cardIdEndOffset);
	} else {
		cardIdEndOffset = findCardIdEndOffset(ndsHeader, moduleParams, cardReadEndOffset);
		cardIdStartOffset = findCardIdStartOffset(moduleParams, cardIdEndOffset);
	}
	if (cardIdEndOffset) {
		debug[1] = (u32)cardIdEndOffset;
	}

	// Card read dma
	u32* cardReadDmaEndOffset = NULL;
	if (usesThumb) {
		//dbg_printf("Trying thumb alt...\n");
		cardReadDmaEndOffset = (u32*)findCardReadDmaEndOffsetThumb(ndsHeader);
	}
	if (!cardReadDmaEndOffset) {
		cardReadDmaEndOffset = findCardReadDmaEndOffset(ndsHeader);
	}
	u32* cardReadDmaStartOffset;
	if (usesThumb) {
		cardReadDmaStartOffset = (u32*)findCardReadDmaStartOffsetThumb((u16*)cardReadDmaEndOffset);
	} else {
		cardReadDmaStartOffset = findCardReadDmaStartOffset(moduleParams, cardReadDmaEndOffset);
	}

	// Find the mpu init
	u32* mpuStartOffset = findMpuStartOffset(ndsHeader, patchMpuRegion);
	u32* mpuDataOffset = findMpuDataOffset(moduleParams, patchMpuRegion, mpuStartOffset);
	if (mpuDataOffset) {
		// Change the region 1 configuration

		u32 mpuInitRegionNewData = PAGE_32M | 0x02000000 | 1;
		u32 mpuNewDataAccess     = 0;
		u32 mpuNewInstrAccess    = 0;
		int mpuAccessOffset      = 0;
		switch (patchMpuRegion) {
			case 0:
				mpuInitRegionNewData = PAGE_128M | 0x00000000 | 1;
				break;
			case 2:
				mpuNewDataAccess  = 0x15111111;
				mpuNewInstrAccess = 0x5111111;
				mpuAccessOffset   = 6;
				break;
			case 3:
				mpuInitRegionNewData = PAGE_8M | 0x03000000 | 1;
				mpuNewInstrAccess    = 0x5111111;
				mpuAccessOffset      = 5;
				break;
		}

		*(vu32*)((u32)ndsHeader + 0x200) = (vu32)mpuDataOffset;
		*(vu32*)((u32)ndsHeader + 0x204) = (vu32)*mpuDataOffset;

		*mpuDataOffset = mpuInitRegionNewData;

		if (mpuAccessOffset) {
			if (mpuNewInstrAccess) {
				mpuDataOffset[mpuAccessOffset] = mpuNewInstrAccess;
			}
			if (mpuNewDataAccess) {
				mpuDataOffset[mpuAccessOffset + 1] = mpuNewDataAccess;
			}
		}
	}

	// Find the mpu cache init
	u32* mpuInitCacheOffset = findMpuInitCacheOffset(mpuStartOffset);
	if (mpuInitCacheOffset) {
		*mpuInitCacheOffset = 0xE3A00046;
	}

	// Patch out all further mpu reconfiguration
	dbg_printf("patchMpuSize: ");
	dbg_hexa(patchMpuSize);
	dbg_printf("\n\n");
	const u32* mpuInitRegionSignature = getMpuInitRegionSignature(patchMpuRegion);
	while (mpuStartOffset && patchMpuSize) {
		u32 patchSize = ndsHeader->arm9binarySize;
		if (patchMpuSize > 1) {
			patchSize = patchMpuSize;
		}
		mpuStartOffset = findOffset(
			//(u32*)((u32)mpuStartOffset + 4), patchSize,
			mpuStartOffset + 1, patchSize,
			mpuInitRegionSignature, 1
		);
		if (mpuStartOffset) {
			dbg_printf("Mpu init: ");
			dbg_hexa((u32)mpuStartOffset);
			dbg_printf("\n\n");

			*mpuStartOffset = 0xE1A00000; // nop

			// Try to find it
			/*for (int i = 0; i < 0x100; i++) {
				mpuDataOffset += i;
				if ((*mpuDataOffset & 0xFFFFFF00) == 0x02000000) {
					*mpuDataOffset = PAGE_32M | 0x02000000 | 1;
					break;
				}
				if (i == 100) {
					*mpuStartOffset = 0xE1A00000;
				}
			}*/
		}
	}

	// Arena low
	/*u32* arenaLowOffset = findArenaLowOffset(ndsHeader);
	if (arenaLowOffset) {
		debug[0] = (u32)arenaLowOffset;

		arenaLowOffset = (u32*)((u32)arenaLowOffset + 0x88);
		debug[10] = (u32)arenaLowOffset;
		debug[11] = *arenaLowOffset;

		u32* oldArenaLow = (u32*)*arenaLowOffset;

		// *arenaLowOffset += 0x800; // shrink heap by 8 kb
		// *(vu32*)(0x027FFDA0) = *arenaLowOffset;
		debug[12] = *arenaLowOffset;

		u32* arenaLow2Offset = findOffset(
			(u32*)ndsHeader->arm9destination, 0x00100000,//ndsHeader->arm9binarySize,
			oldArenaLow, 1
		);

		// *arenaLow2Offset += 0x800; // shrink heap by 8 kb

		debug[13] = (u32)arenaLow2Offset;
	}*/
	
	// Random patch
	if (moduleParams->sdk_version > 0x3000000
	&& strncmp(ROM_TID, "AKT", 3) != 0  // Doctor Tendo
	&& strncmp(ROM_TID, "ACZ", 3) != 0  // Cars
	&& strncmp(ROM_TID, "ABC", 3) != 0  // Harvest Moon DS
	&& strncmp(ROM_TID, "AWL", 3) != 0) // TWEWY
	{
		u32* randomPatchOffset = findRandomPatchOffset(ndsHeader);
		if (randomPatchOffset) {
			//*(u32*)((u32)randomPatchOffset + 0xC) = 0x0;
			*(randomPatchOffset + 3) = 0x0;
		}
	}

	// SDK 5
	u32* randomPatchOffset5First = findRandomPatchOffset5First(ndsHeader, moduleParams);
	if (randomPatchOffset5First) {
		*randomPatchOffset5First = 0xE3A00000;
		//*(u32*)((u32)randomPatchOffset5First + 4) = 0xE12FFF1E;
		*(randomPatchOffset5First + 1) = 0xE12FFF1E;
	}

	// SDK 5
	u32* randomPatchOffset5Second = findRandomPatchOffset5Second(ndsHeader, moduleParams);
	if (randomPatchOffset5Second) {
		*randomPatchOffset5Second = 0xE3A00000;
		//*(u32*)((u32)randomPatchOffset2 + 4) = 0xE12FFF1E;
		*(randomPatchOffset5Second + 1) = 0xE12FFF1E;
	}

	debug[2] = (u32)ce9;

	//ce9->sdk_version = moduleParams->sdk_version;

	u32* cardReadPatch    = (usesThumb ? ce9->thumbPatches->card_read_arm9 : ce9->patches->card_read_arm9);
	u32* cardPullOutPatch = (usesThumb ? ce9->thumbPatches->card_pull      : ce9->patches->card_pull);
	u32* cardIdPatch      = (usesThumb ? ce9->thumbPatches->card_id_arm9   : ce9->patches->card_id_arm9);
	u32* cardDmaPatch     = (usesThumb ? ce9->thumbPatches->card_dma_arm9  : ce9->patches->card_dma_arm9);

	debug[5] = (u32)ce9->patches;

	u32** cardStruct = (u32**)(cardReadEndOffset - 1);
	//u32* cacheStruct = (u32**)(cardIdStartOffset - 1);

	debug[6] = (u32)*cardStruct;
	//debug[7] = (u32)*cacheStruct;

	u32* cardStructPatch = (usesThumb ? ce9->thumbPatches->cardStructArm9 : ce9->patches->cardStructArm9);
	if (moduleParams->sdk_version > 0x3000000) {
		ce9->cardStruct0 = (u32)(*cardStruct + 7);
		*cardStructPatch = (u32)(*cardStruct + 7); // Cache management alternative
	} else {
		ce9->cardStruct0 = (u32)(*cardStruct + 6);
		*cardStructPatch = (u32)(*cardStruct + 6); // Cache management alternative
	}
	//ce9->cacheStruct = (u32)*cacheStruct;

	u32* cacheFlushPatch = (usesThumb ? ce9->thumbPatches->cacheFlushRef : ce9->patches->cacheFlushRef);
	*cacheFlushPatch = (u32)cardPullOutOffset + 4;

	u32* readCachedPatch = (usesThumb ? ce9->thumbPatches->readCachedRef : ce9->patches->readCachedRef);
	if (strncmp(ROM_TID, "A2D", 3) != 0	// New Super Mario Bros
	&& strncmp(ROM_TID, "ADM", 3) != 0)	// Animal Crossing: Wild World
	{
		*readCachedPatch = (u32)cardReadCachedStartOffset;
	}

	if (!usesThumb) { // Based on: cardengine/arm9/source/card_engine_header.s
		ce9->patches->needFlushDCCache = needFlushCache;
	}

	//memcpy(oldArenaLow, cardReadPatch, 0xF0); //copyLoop(oldArenaLow, cardReadPatch, 0xF0);

	memcpy(cardReadStartOffset, cardReadPatch, usesThumb ? (sdk5 ? 0xB0 : 0xA0) : 0xF0); //copyLoop(cardReadStartOffset, cardReadPatch, usesThumb ? 0xA0 : 0xF0);

	memcpy(cardPullOutOffset, cardPullOutPatch, 0x4); //copyLoop(cardPullOutOffset, cardPullOutPatch, 0x4);

	/*if (forceToPowerOffOffset) {
		memcpy(forceToPowerOffOffset, cardPullOutPatch, 0x4); //copyLoop(forceToPowerOffOffset, cardPullOutPatch, 0x4);
	}*/

	if (cardIdStartOffset) {
		memcpy(cardIdStartOffset, cardIdPatch, usesThumb ? 0x4 : 0x8); //copyLoop(cardIdStartOffset, cardIdPatch, usesThumb ? 0x4 : 0x8);
	}

	if (cardReadDmaStartOffset) {
		memcpy(cardReadDmaStartOffset, cardDmaPatch, usesThumb ? 0x4 : 0x8); //copyLoop(cardReadDmaStartOffset, cardDmaPatch, usesThumb ? 0x4 : 0x8);
	}

	dbg_printf("ERR_NONE\n\n");
	return ERR_NONE;
}
