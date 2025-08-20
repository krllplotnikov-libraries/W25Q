#ifndef W25QXX_H_
#define W25QXX_H_

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define W25_WRITE_DISABLE     0x04
#define W25_WRITE_ENABLE      0x06
#define W25_CHIP_ERASE        0xC7 //0x60
#define W25_SECTOR_ERASE      0x20
#define W25_BLOCK_ERASE       0xD8
#define W25_FAST_READ         0x0B
#define W25_PAGE_PROGRAMM     0x02
#define W25_GET_JEDEC_ID      0x9F
#define W25_READ_STATUS_1     0x05
#define W25_READ_STATUS_2     0x35
#define W25_READ_STATUS_3     0x15
#define W25_WRITE_STATUS_1    0x01
#define W25_WRITE_STATUS_2    0x31
#define W25_WRITE_STATUS_3    0x11
#define W25_READ_UNIQUE_ID    0x4B

typedef enum W25Q_ID_e W25Q_ID_et;

typedef struct W25Q             W25Q_st;
typedef struct W25Q_Descriptor  W25Q_Descriptor_st;

typedef void (*W25Q_SPI_ChipSelect)      (W25Q_st* w25q, uint8_t level);
typedef void (*W25Q_SPI_TransmitReceive) (W25Q_st* w25q, uint8_t* txBuf, uint8_t* rxBuf, size_t size);
typedef void (*W25Q_SPI_Transmit)        (W25Q_st* w25q, uint8_t* txBuf, size_t size);
typedef void (*W25Q_SPI_Receive)         (W25Q_st* w25q, uint8_t* rxBuf, size_t size);
typedef uint32_t (*W25Q_GetTick)         ();

enum W25Q_ID_e
{
	W25Q10 = 1,
	W25Q20,
	W25Q40,
	W25Q80,
	W25Q16,
	W25Q32,
	W25Q64,
	W25Q128,
	W25Q256,
	W25Q512,
};

struct W25Q_Descriptor
{
	W25Q_SPI_ChipSelect spiChipSelect;
	W25Q_SPI_TransmitReceive spiTransmitReceive;
	W25Q_SPI_Transmit spiTransmit;
	W25Q_SPI_Receive spiReceive;
	W25Q_GetTick getTick;
};

struct W25Q
{
	W25Q_Descriptor_st descriptor;
	W25Q_ID_et id;
	uint8_t		uniqID[8];
	uint16_t	pageSize;
	uint32_t	pageCount;
	uint32_t	sectorSize;
	uint32_t	sectorCount;
	uint32_t	blockSize;
	uint32_t	blockCount;
	uint32_t	capacityInKiloByte;
	uint8_t		statusRegister1;
	uint8_t		statusRegister2;
	uint8_t		statusRegister3;
	uint8_t		lock;
};

#ifdef __cplusplus
extern "C" {
#endif

uint8_t	W25Q_Init(W25Q_st* w25q, W25Q_Descriptor_st* descriptor);

void W25Q_EraseChip(W25Q_st* w25q);
void W25Q_EraseSector(W25Q_st* w25q, uint32_t sectorAddr);
void W25Q_EraseBlock(W25Q_st* w25q, int32_t blockAddr);

void W25Q_WriteByte(W25Q_st* w25q, uint8_t byte, uint32_t addr);
void W25Q_WritePage(W25Q_st* w25q, uint8_t *buf, uint32_t pageAddr, uint32_t offset, uint32_t size);
void W25Q_WriteSector(W25Q_st* w25q, uint8_t *buf, uint32_t sectorAddr, uint32_t offset, uint32_t size);
void W25Q_WriteBlock(W25Q_st* w25q, uint8_t* buf, uint32_t blockAddr, uint32_t offset, uint32_t size);

void W25Q_ReadByte(W25Q_st* w25q, uint8_t* byte, uint32_t addr);
void W25Q_ReadPage(W25Q_st* w25q, uint8_t *buf, uint32_t pageAddr, uint32_t offset, uint32_t size);
void W25Q_ReadSector(W25Q_st* w25q, uint8_t *buf, uint32_t sectorAddr, uint32_t offset, uint32_t size);
void W25Q_ReadBlock(W25Q_st* w25q, uint8_t* buf, uint32_t blockAddr, uint32_t offset, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
