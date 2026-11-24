// Host test for W25Q library with a software flash emulator.
// Build: gcc -std=c99 -Wall -Wextra -pedantic -I../src ../src/W25Q.c W25Q_Test.c -o W25Q_Test && ./W25Q_Test
#include "W25Q.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMU_MAX_SIZE          (64u * 1024u * 1024u)
#define EMU_PAGE_PROGRAM_TIME 1u
#define EMU_BLOCK_ERASE_TIME  200u
#define EMU_CHIP_ERASE_TIME   3000u

//-----Emulator-----
typedef struct{
	uint8_t*  memory;
	uint32_t  size;
	uint8_t   jedecId[3];
	uint8_t   uniqueId[8];
	uint8_t   addressSize;
	uint8_t   poweredDown;
	uint8_t   writeEnable;
	uint8_t   failSpi;
	uint32_t  busyUntil;
	uint8_t   csActive;
	uint32_t  position;
	uint8_t   command;
	uint32_t  address;
	uint32_t  programCount;
} Emulator_st;

static Emulator_st emu;
static uint32_t    tick;
static uint32_t    sectorEraseTime = 50u;
static int         failures;

#define CHECK(_condition) do{ if(!(_condition)){ printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #_condition); failures++; } }while(0)

static int emuBusy(void){
	return (int32_t)(emu.busyUntil - tick) > 0;
}

static void emuSetBusy(uint32_t _time){
	emu.busyUntil   = tick + _time;
	emu.writeEnable = 0;
}

static void emuCsRise(void){
	if(emu.poweredDown){
		return;
	}

	uint32_t sectorBase = emu.address & ~(W25Q_SECTOR_SIZE - 1u);
	uint32_t blockBase  = emu.address & ~(W25Q_BLOCK_SIZE - 1u);

	switch(emu.command){
	case 0x20:{
		if(emu.writeEnable && emu.position == 1u + emu.addressSize){
			memset(emu.memory + (sectorBase % emu.size), 0xFF, W25Q_SECTOR_SIZE);
			emuSetBusy(sectorEraseTime);
		}
		break;
	}

	case 0xD8:{
		if(emu.writeEnable && emu.position == 1u + emu.addressSize){
			memset(emu.memory + (blockBase % emu.size), 0xFF, W25Q_BLOCK_SIZE);
			emuSetBusy(EMU_BLOCK_ERASE_TIME);
		}
		break;
	}

	case 0xC7:{
		if(emu.writeEnable && emu.position == 1u){
			memset(emu.memory, 0xFF, emu.size);
			emuSetBusy(EMU_CHIP_ERASE_TIME);
		}
		break;
	}

	case 0x02:{
		if(emu.writeEnable && emu.position > 1u + emu.addressSize){
			emu.programCount++;
			emuSetBusy(EMU_PAGE_PROGRAM_TIME);
		}
		break;
	}

	default: break;
	}
}

static uint8_t emuByte(uint8_t _tx){
	uint32_t pos = emu.position++;
	uint8_t  rx  = 0xFF;

	if(emu.poweredDown){
		if(pos == 0 && _tx == 0xAB){
			emu.poweredDown = 0;
		}
		return 0xFF;
	}

	if(pos == 0){
		emu.command = _tx;
		emu.address = 0;

		if(emuBusy() && _tx != 0x05 && _tx != 0x15){
			emu.command = 0;   // ignored while busy
		}
		if(emu.command == 0x06){ emu.writeEnable = 1; }
		if(emu.command == 0xB7){ emu.addressSize = 4; }
		return rx;
	}

	switch(emu.command){
	case 0x9F:{ if(pos <= 3){ rx = emu.jedecId[pos - 1]; } break; }
	case 0x05:{ rx = (uint8_t)((emuBusy() ? 0x01 : 0x00) | (emu.writeEnable ? 0x02 : 0x00)); break; }
	case 0x15:{ rx = (emu.addressSize == 4) ? 0x01 : 0x00; break; }

	case 0x4B:{
		uint32_t dummy = emu.addressSize + 1u;
		if(pos > dummy && pos <= dummy + 8u){ rx = emu.uniqueId[pos - dummy - 1u]; }
		break;
	}

	case 0x02:
	case 0x20:
	case 0xD8:
	case 0x0B:{
		if(pos <= emu.addressSize){
			emu.address = (emu.address << 8) | _tx;
		}
		else if(emu.command == 0x02){
			uint32_t page  = emu.address & ~(W25Q_PAGE_SIZE - 1u);
			uint32_t index = (emu.address + (pos - emu.addressSize - 1u)) % W25Q_PAGE_SIZE;
			emu.memory[(page + index) % emu.size] &= _tx;
		}
		else if(emu.command == 0x0B && pos > emu.addressSize + 1u){
			rx = emu.memory[(emu.address + (pos - emu.addressSize - 2u)) % emu.size];
		}
		break;
	}

	default: break;
	}

	return rx;
}

static void emuReset(uint32_t _jedecId, uint8_t _poweredDown){
	memset(&emu, 0, sizeof(emu));
	emu.memory      = malloc(EMU_MAX_SIZE);
	emu.jedecId[0]  = (uint8_t)(_jedecId >> 16);
	emu.jedecId[1]  = (uint8_t)(_jedecId >> 8);
	emu.jedecId[2]  = (uint8_t)(_jedecId);
	emu.size        = (emu.jedecId[2] == 0x20) ? 0x04000000u : (emu.jedecId[2] >= 0x11 && emu.jedecId[2] <= 0x19 ? (1u << emu.jedecId[2]) : 0x100000u);
	emu.addressSize = 3;
	emu.poweredDown = _poweredDown;
	for(int i = 0; i < 8; i++){ emu.uniqueId[i] = (uint8_t)(0x10 + i); }
	memset(emu.memory, 0xFF, emu.size);
}
//-----Emulator-----



//-----Hardware functions-----
static void setChipSelect(W25Q_st* _w25q, W25Q_ChipSelect_et _state){
	(void)_w25q;
	if(_state == W25Q_CHIP_SELECT_ACTIVE){
		emu.csActive = 1;
		emu.position = 0;
	}
	else if(emu.csActive){
		emu.csActive = 0;
		emuCsRise();
	}
}

static W25Q_Result_et transmit(W25Q_st* _w25q, const uint8_t* _data, size_t _size){
	(void)_w25q;
	if(emu.failSpi){ return W25Q_RESULT_ERROR_SPI; }
	for(size_t i = 0; i < _size; i++){ (void)emuByte(_data[i]); }
	return W25Q_RESULT_OK;
}

static W25Q_Result_et receive(W25Q_st* _w25q, uint8_t* _data, size_t _size){
	(void)_w25q;
	if(emu.failSpi){ return W25Q_RESULT_ERROR_SPI; }
	for(size_t i = 0; i < _size; i++){ _data[i] = emuByte(0xFF); }
	return W25Q_RESULT_OK;
}


static uint32_t getTick(void){
	return tick++;   // every call advances time: busy loops make progress
}
//-----Hardware functions-----



static W25Q_st flash;

static W25Q_Result_et initFlash(void){
	W25Q_Descriptor_st descriptor = {
		.setChipSelect = setChipSelect,
		.transmit      = transmit,
		.receive       = receive,
		.getTick       = getTick,
	};

	return W25Q_Init(&flash, &descriptor);
}

static W25Q_Result_et start(uint32_t _jedecId, uint8_t _poweredDown, uint32_t _startTick){
	free(emu.memory);
	emuReset(_jedecId, _poweredDown);
	tick          = _startTick;
	emu.busyUntil = tick;

	return initFlash();
}

static void testDetection(void){
	struct{ uint32_t id; W25Q_Model_et model; uint32_t capacity; } table[] = {
		{0xEF4011, W25Q_MODEL_W25Q10,  128u * 1024u},
		{0xEF4015, W25Q_MODEL_W25Q16,  2u * 1024u * 1024u},
		{0xEF4018, W25Q_MODEL_W25Q128, 16u * 1024u * 1024u},
		{0xEF7018, W25Q_MODEL_W25Q128, 16u * 1024u * 1024u},
		{0xEF4019, W25Q_MODEL_W25Q256, 32u * 1024u * 1024u},
		{0xEF4020, W25Q_MODEL_W25Q512, 64u * 1024u * 1024u},
	};

	printf("detection\n");
	for(size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++){
		uint32_t startTick = 0xFFFFFFF8u;

		CHECK(start(table[i].id, 1, startTick) == W25Q_RESULT_OK);
		CHECK((tick - startTick) > W25Q_POWER_UP_TIME_MS);
		CHECK(W25Q_GetModel(&flash) == table[i].model);
		CHECK(W25Q_GetCapacity(&flash) == table[i].capacity);
		CHECK(emu.addressSize == (table[i].capacity > 0x01000000u ? 4 : 3));

		uint8_t uid[8];
		CHECK(W25Q_GetUniqueId(&flash, uid) == W25Q_RESULT_OK);
		CHECK(uid[0] == 0x10 && uid[7] == 0x17);
	}

	// Chip still erasing after an MCU reset: Init waits for it
	free(emu.memory);
	emuReset(0xEF4018, 0);
	tick          = 0;
	emu.busyUntil = 300;
	CHECK(initFlash() == W25Q_RESULT_OK);
	CHECK(W25Q_GetModel(&flash) == W25Q_MODEL_W25Q128);

	CHECK(start(0xC84018, 0, 0) == W25Q_RESULT_ERROR_DEVICE_NOT_FOUND);   // other manufacturer
	CHECK(W25Q_Read(&flash, 0, (uint8_t[1]){0}, 1) == W25Q_RESULT_ERROR_DEVICE_NOT_FOUND);
	CHECK(W25Q_Write(&flash, 0, (uint8_t[1]){0}, 1) == W25Q_RESULT_ERROR_DEVICE_NOT_FOUND);
	CHECK(W25Q_EraseChip(&flash) == W25Q_RESULT_ERROR_DEVICE_NOT_FOUND);
	CHECK(W25Q_GetUniqueId(&flash, (uint8_t[8]){0}) == W25Q_RESULT_ERROR_DEVICE_NOT_FOUND);

	CHECK(start(0xEF4000, 0, 0) == W25Q_RESULT_ERROR_DEVICE_NOT_FOUND);   // unknown capacity
	CHECK(W25Q_GetCapacity(&flash) == 0);

	free(emu.memory);
	emuReset(0xEF4018, 0);
	emu.failSpi = 1;
	CHECK(initFlash() == W25Q_RESULT_ERROR_SPI);
	CHECK(emu.csActive == 0);
}

static void testReadWrite(uint32_t _jedecId, uint32_t _startTick){
	static uint8_t data[70000];
	static uint8_t readBack[W25Q_BLOCK_SIZE];

	printf("read/write 0x%06lX, tick 0x%08lX\n", (unsigned long)_jedecId, (unsigned long)_startTick);
	CHECK(start(_jedecId, 0, _startTick) == W25Q_RESULT_OK);

	for(size_t i = 0; i < sizeof(data); i++){ data[i] = (uint8_t)rand(); }

	uint32_t base = W25Q_GetCapacity(&flash) - 2u * W25Q_BLOCK_SIZE;   // high addresses: exercises the 4th address byte

	memset(emu.memory + base, 0x00, 2u * W25Q_BLOCK_SIZE);
	CHECK(W25Q_EraseBlock(&flash, base / W25Q_BLOCK_SIZE) == W25Q_RESULT_OK);
	CHECK(!emuBusy());
	CHECK(emu.memory[base] == 0xFF && emu.memory[base + W25Q_BLOCK_SIZE - 1u] == 0xFF);
	CHECK(emu.memory[base + W25Q_BLOCK_SIZE] == 0x00);

	uint32_t address = base + 100u;
	uint32_t size    = 5000u;

	CHECK(W25Q_Write(&flash, address, data, size) == W25Q_RESULT_OK);
	CHECK(!emuBusy());
	CHECK(emu.programCount == 20);   // 156 + 18 * 256 + 236 bytes

	memset(readBack, 0, sizeof(readBack));
	CHECK(W25Q_Read(&flash, address, readBack, size) == W25Q_RESULT_OK);
	CHECK(memcmp(readBack, data, size) == 0);
	CHECK(emu.memory[address - 1] == 0xFF && emu.memory[address + size] == 0xFF);

	CHECK(W25Q_Read(&flash, base, readBack, W25Q_BLOCK_SIZE) == W25Q_RESULT_OK);   // one continuous read
	CHECK(readBack[0] == 0xFF && memcmp(readBack + 100, data, size) == 0);

	CHECK(W25Q_EraseSector(&flash, (address + W25Q_SECTOR_SIZE) / W25Q_SECTOR_SIZE) == W25Q_RESULT_OK);
	CHECK(W25Q_Read(&flash, address, readBack, size) == W25Q_RESULT_OK);
	CHECK(memcmp(readBack, data, W25Q_SECTOR_SIZE - 100u) == 0);
	CHECK(readBack[W25Q_SECTOR_SIZE - 100u] == 0xFF);

	uint32_t last = W25Q_GetCapacity(&flash) - 3u;   // last bytes of the chip
	CHECK(W25Q_EraseSector(&flash, last / W25Q_SECTOR_SIZE) == W25Q_RESULT_OK);
	CHECK(W25Q_Write(&flash, last, data, 3) == W25Q_RESULT_OK);
	CHECK(W25Q_Read(&flash, last, readBack, 3) == W25Q_RESULT_OK);
	CHECK(memcmp(readBack, data, 3) == 0);
}

static void testArguments(void){
	uint8_t buffer[16] = {0};

	printf("arguments\n");
	CHECK(start(0xEF4018, 0, 0) == W25Q_RESULT_OK);

	uint32_t capacity = W25Q_GetCapacity(&flash);

	CHECK(W25Q_Read(&flash, 0, NULL, 1) == W25Q_RESULT_ERROR_INVALID_ARGUMENT);
	CHECK(W25Q_Read(&flash, 0, buffer, 0) == W25Q_RESULT_ERROR_INVALID_ARGUMENT);
	CHECK(W25Q_Read(&flash, capacity, buffer, 1) == W25Q_RESULT_ERROR_INVALID_ARGUMENT);
	CHECK(W25Q_Read(&flash, capacity - 1u, buffer, 2) == W25Q_RESULT_ERROR_INVALID_ARGUMENT);
	CHECK(W25Q_Read(&flash, 1, buffer, 0xFFFFFFFFu) == W25Q_RESULT_ERROR_INVALID_ARGUMENT);
	CHECK(W25Q_Read(&flash, capacity - 1u, buffer, 1) == W25Q_RESULT_OK);
	CHECK(W25Q_Write(&flash, 0, NULL, 1) == W25Q_RESULT_ERROR_INVALID_ARGUMENT);
	CHECK(W25Q_Write(&flash, capacity - 1u, buffer, 2) == W25Q_RESULT_ERROR_INVALID_ARGUMENT);
	CHECK(W25Q_EraseSector(&flash, capacity / W25Q_SECTOR_SIZE) == W25Q_RESULT_ERROR_INVALID_ARGUMENT);
	CHECK(W25Q_EraseBlock(&flash, capacity / W25Q_BLOCK_SIZE) == W25Q_RESULT_ERROR_INVALID_ARGUMENT);
	CHECK(W25Q_GetUniqueId(&flash, NULL) == W25Q_RESULT_ERROR_INVALID_ARGUMENT);
	CHECK(emu.programCount == 0);
}

static void testTimeoutAndErrors(void){
	printf("timeout / errors\n");
	CHECK(start(0xEF4018, 0, 0xFFFFFF00u) == W25Q_RESULT_OK);

	sectorEraseTime = 100000u;   // the chip hangs in erase
	uint32_t startTick = tick;
	CHECK(W25Q_EraseSector(&flash, 1) == W25Q_RESULT_ERROR_TIMEOUT);
	CHECK((tick - startTick) > W25Q_SECTOR_ERASE_TIMEOUT_MS);
	sectorEraseTime = 50u;

	// Chip still busy: new operations are refused
	CHECK(W25Q_EraseSector(&flash, 2) == W25Q_RESULT_BUSY);
	CHECK(W25Q_Read(&flash, 0, (uint8_t[1]){0}, 1) == W25Q_RESULT_BUSY);
	CHECK(W25Q_Write(&flash, 0, (uint8_t[1]){0}, 1) == W25Q_RESULT_BUSY);
	emu.busyUntil = tick;
	CHECK(W25Q_EraseSector(&flash, 2) == W25Q_RESULT_OK);

	CHECK(W25Q_EraseChip(&flash) == W25Q_RESULT_OK);
	CHECK(!emuBusy());

	emu.failSpi = 1;
	CHECK(W25Q_Read(&flash, 0, (uint8_t[1]){0}, 1) == W25Q_RESULT_ERROR_SPI);
	CHECK(W25Q_Write(&flash, 0, (uint8_t[1]){0}, 1) == W25Q_RESULT_ERROR_SPI);
	CHECK(emu.csActive == 0);
}

int main(void){
	setvbuf(stdout, NULL, _IONBF, 0);

	testDetection();
	testArguments();
	testReadWrite(0xEF4018, 0);
	testReadWrite(0xEF4018, 0xFFFFFF00u);
	testReadWrite(0xEF4019, 0);
	testReadWrite(0xEF4020, 0xFFFFFF00u);
	testTimeoutAndErrors();

	free(emu.memory);
	printf(failures ? "FAILED: %d\n" : "ALL TESTS PASSED\n", failures);
	return failures ? 1 : 0;
}
