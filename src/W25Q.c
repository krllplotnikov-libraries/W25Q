#include "W25Qxx.h"

#define W25QXX_DUMMY_BYTE        0xA5

static void W25Q_Delay(W25Q_st* w25q, uint32_t ms){
	uint32_t startTime = w25q->descriptor.getTick();
	while(startTime + ms > w25q->descriptor.getTick()){}
}

static uint8_t W25Q_SendByte(W25Q_st* w25q, uint8_t byte)
{
	uint8_t rxByte = 0;
	w25q->descriptor.spiTransmitReceive(w25q, &byte, &rxByte, 1);
	return rxByte;
}

static uint32_t W25Q_ReadID(W25Q_st* w25q)
{
	uint32_t id = 0;
	uint8_t rxByte0 = 0, rxByte1 = 0, rxByte2 = 0;

	w25q->descriptor.spiChipSelect(w25q, 0);

	W25Q_SendByte(w25q, W25_GET_JEDEC_ID);

	rxByte0 = W25Q_SendByte(w25q, W25QXX_DUMMY_BYTE);
	rxByte1 = W25Q_SendByte(w25q, W25QXX_DUMMY_BYTE);
	rxByte2 = W25Q_SendByte(w25q, W25QXX_DUMMY_BYTE);

	w25q->descriptor.spiChipSelect(w25q, 1);

	id = (rxByte0 << 16) | (rxByte1 << 8) | rxByte2;

	return id;
}

static void W25Q_WriteEnable(W25Q_st* w25q)
{
	w25q->descriptor.spiChipSelect(w25q, 0);
	W25Q_SendByte(w25q, W25_WRITE_ENABLE);
	w25q->descriptor.spiChipSelect(w25q, 1);
	W25Q_Delay(w25q, 1);
}

//static void W25Q_WriteDisable(W25Q_st* w25q)
//{
//	w25q->descriptor.spiChipSelect(w25q, 0);
//	W25Q_SendByte(w25q, W25_WRITE_DISABLE);
//	w25q->descriptor.spiChipSelect(w25q, 1);
//	W25Q_Delay(w25q, 1);
//}

static void W25Q_WaitForWriteEnd(W25Q_st* w25q)
{
	W25Q_Delay(w25q, 1);
	w25q->descriptor.spiChipSelect(w25q, 0);
	W25Q_SendByte(w25q, W25_READ_STATUS_1);

	do{
		w25q->statusRegister1 = W25Q_SendByte(w25q, W25QXX_DUMMY_BYTE);
		W25Q_Delay(w25q, 1);
	}
	while((w25q->statusRegister1 & 0x01) == 0x01);

	w25q->descriptor.spiChipSelect(w25q, 1);
}

static uint32_t W25Q_SectorToPage(W25Q_st* w25q, uint32_t sectorAddress)
{
	return(sectorAddress * w25q->sectorSize) / w25q->pageSize;
}

static uint32_t W25Q_BlockToPage(W25Q_st* w25q, uint32_t blockAddress)
{
	return (blockAddress * w25q->blockSize) / w25q->pageSize;
}


uint8_t	W25Q_Init(W25Q_st* w25q, W25Q_Descriptor_st* descriptor)
{
	w25q->descriptor.spiChipSelect = descriptor->spiChipSelect;
	w25q->descriptor.spiTransmitReceive = descriptor->spiTransmitReceive;
	w25q->descriptor.spiTransmit = descriptor->spiTransmit;
	w25q->descriptor.spiReceive = descriptor->spiReceive;
	w25q->descriptor.getTick = descriptor->getTick;

	w25q->lock = 1;

	W25Q_Delay(w25q, 100);
	w25q->descriptor.spiChipSelect(w25q, 1);
	W25Q_Delay(w25q, 100);

	uint32_t id = W25Q_ReadID(w25q);

	switch(id & 0x0000FFFF)
	{
		case 0x401A:	// 	w25q512
			w25q->id = W25Q512;
			w25q->blockCount = 1024;
		break;

		case 0x4019:	// 	w25q256
			w25q->id = W25Q256;
			w25q->blockCount = 512;
		break;

		case 0x4018:	// 	w25q128
			w25q->id = W25Q128;
			w25q->blockCount = 256;
		break;

		case 0x4017:	//	w25q64
			w25q->id = W25Q64;
			w25q->blockCount = 128;
		break;

		case 0x4016:	//	w25q32
			w25q->id = W25Q32;
			w25q->blockCount = 64;
		break;

		case 0x4015:	//	w25q16
			w25q->id = W25Q16;
			w25q->blockCount = 32;
		break;

		case 0x4014:	//	w25q80
			w25q->id = W25Q80;
			w25q->blockCount = 16;
		break;

		case 0x4013:	//	w25q40
			w25q->id = W25Q40;
			w25q->blockCount = 8;
		break;

		case 0x4012:	//	w25q20
			w25q->id = W25Q20;
			w25q->blockCount = 4;
		break;

		case 0x4011:	//	w25q10
			w25q->id = W25Q10;
			w25q->blockCount = 2;
		break;

		default:
			w25q->lock = 0;
			return 0;
	}


	w25q->pageSize = 256;
	w25q->sectorSize = 0x1000;
	w25q->sectorCount = w25q->blockCount * 16;
	w25q->pageCount = (w25q->sectorCount * w25q->sectorSize) / w25q->pageSize;
	w25q->blockSize = w25q->sectorSize * 16;
	w25q->capacityInKiloByte = (w25q->sectorCount * w25q->sectorSize) / 1024;

	w25q->lock = 0;
	return 1;
}

void W25Q_EraseChip(W25Q_st* w25q)
{
	while(w25q->lock == 1)
		W25Q_Delay(w25q, 1);

	w25q->lock = 1;

	W25Q_WriteEnable(w25q);

	w25q->descriptor.spiChipSelect(w25q, 0);

	W25Q_SendByte(w25q, W25_CHIP_ERASE);

	w25q->descriptor.spiChipSelect(w25q, 1);

	W25Q_WaitForWriteEnd(w25q);

	W25Q_Delay(w25q, 10);

	w25q->lock = 0;
}

void W25Q_EraseSector(W25Q_st* w25q, uint32_t sectorAddr)
{
	while(w25q->lock == 1)
		W25Q_Delay(w25q, 1);

	w25q->lock = 1;

	W25Q_WaitForWriteEnd(w25q);
	sectorAddr = sectorAddr * w25q->sectorSize;

	W25Q_WriteEnable(w25q);

	w25q->descriptor.spiChipSelect(w25q, 0);

	W25Q_SendByte(w25q, W25_SECTOR_ERASE);

	if(w25q->id >= W25Q256){
		W25Q_SendByte(w25q, (sectorAddr & 0xFF000000) >> 24);
	}

	W25Q_SendByte(w25q, (sectorAddr & 0xFF0000) >> 16);
	W25Q_SendByte(w25q, (sectorAddr & 0xFF00) >> 8);
	W25Q_SendByte(w25q, (sectorAddr & 0xFF));

	w25q->descriptor.spiChipSelect(w25q, 1);

	W25Q_WaitForWriteEnd(w25q);

	W25Q_Delay(w25q, 1);
	w25q->lock = 0;
}

void W25Q_EraseBlock(W25Q_st* w25q, int32_t blockAddr)
{
	while(w25q->lock == 1)
		W25Q_Delay(w25q, 1);

	w25q->lock = 1;

	W25Q_WaitForWriteEnd(w25q);

	blockAddr = blockAddr * w25q->sectorSize * 16;

	W25Q_WriteEnable(w25q);

	w25q->descriptor.spiChipSelect(w25q, 0);

	W25Q_SendByte(w25q, W25_BLOCK_ERASE);

	if(w25q->id >= W25Q256){
		W25Q_SendByte(w25q, (blockAddr & 0xFF000000) >> 24);
	}

	W25Q_SendByte(w25q, (blockAddr & 0xFF0000) >> 16);
	W25Q_SendByte(w25q, (blockAddr & 0xFF00) >> 8);
	W25Q_SendByte(w25q, (blockAddr & 0xFF));

	w25q->descriptor.spiChipSelect(w25q, 1);

	W25Q_WaitForWriteEnd(w25q);

	W25Q_Delay(w25q, 1);
	w25q->lock = 0;
}

void W25Q_WriteByte(W25Q_st* w25q, uint8_t byte, uint32_t addr)
{
	while(w25q->lock == 1)
		W25Q_Delay(w25q, 1);

	w25q->lock = 1;

	W25Q_WaitForWriteEnd(w25q);
	W25Q_WriteEnable(w25q);

	w25q->descriptor.spiChipSelect(w25q, 0);

	W25Q_SendByte(w25q, W25_PAGE_PROGRAMM);

	if(w25q->id >= W25Q256){
		W25Q_SendByte(w25q, (addr & 0xFF000000) >> 24);
	}

	W25Q_SendByte(w25q, (addr & 0xFF0000) >> 16);
	W25Q_SendByte(w25q, (addr & 0xFF00) >> 8);
	W25Q_SendByte(w25q, (addr & 0xFF));

	W25Q_SendByte(w25q, byte);

	w25q->descriptor.spiChipSelect(w25q, 1);

	W25Q_WaitForWriteEnd(w25q);

	w25q->lock = 0;
}

void W25Q_WritePage(W25Q_st* w25q, uint8_t *buf, uint32_t pageAddr, uint32_t offset, uint32_t size)
{
	while(w25q->lock == 1)
		W25Q_Delay(w25q, 1);

	w25q->lock = 1;

	if(((size + offset) > w25q->pageSize) || (size == 0))
		size = w25q->pageSize - offset;

	W25Q_WaitForWriteEnd(w25q);
	W25Q_WriteEnable(w25q);

	w25q->descriptor.spiChipSelect(w25q, 0);

	W25Q_SendByte(w25q, W25_PAGE_PROGRAMM);

	pageAddr = (pageAddr * w25q->pageSize) + offset;

	if(w25q->id >= W25Q256){
		W25Q_SendByte(w25q, (pageAddr & 0xFF000000) >> 24);
	}

	W25Q_SendByte(w25q, (pageAddr & 0xFF0000) >> 16);
	W25Q_SendByte(w25q, (pageAddr & 0xFF00) >> 8);
	W25Q_SendByte(w25q, (pageAddr & 0xFF));

	w25q->descriptor.spiTransmit(w25q, buf, size);

	w25q->descriptor.spiChipSelect(w25q, 1);

	W25Q_WaitForWriteEnd(w25q);

	W25Q_Delay(w25q, 1);
	w25q->lock = 0;
}

void W25Q_WriteSector(W25Q_st* w25q, uint8_t *buf, uint32_t sectorAddr, uint32_t offset, uint32_t size)
{
	if((size > w25q->sectorSize) || (size == 0))
		size = w25q->sectorSize;

	uint32_t startPage;
	int32_t	bytesToWrite;
	uint32_t localOffset;

	if((offset + size) > w25q->sectorSize)
		bytesToWrite = w25q->sectorSize - offset;
	else
		bytesToWrite = size;

	startPage = W25Q_SectorToPage(w25q, sectorAddr) + (offset / w25q->pageSize);
	localOffset = offset % w25q->pageSize;

	do
	{
		W25Q_WritePage(w25q, buf, startPage, localOffset, bytesToWrite);
		startPage++;

		bytesToWrite -= w25q->pageSize - localOffset;
		buf += w25q->pageSize - localOffset;
		localOffset = 0;
	}
	while(bytesToWrite > 0);
}

void W25Q_WriteBlock(W25Q_st* w25q, uint8_t* buf, uint32_t blockAddr, uint32_t offset, uint32_t size)
{
	if((size>w25q->blockSize)||(size == 0))
		size=w25q->blockSize;

	uint32_t	startPage;
	int32_t		bytesToWrite;
	uint32_t	localOffset;

	if((offset+size) > w25q->blockSize)
		bytesToWrite = w25q->blockSize - offset;
	else
		bytesToWrite = size;

	startPage = W25Q_BlockToPage(w25q, blockAddr) + (offset/w25q->pageSize);

	localOffset = offset % w25q->pageSize;

	do
	{
		W25Q_WritePage(w25q, buf, startPage, localOffset, bytesToWrite);
		startPage++;
		bytesToWrite -= w25q->pageSize - localOffset;
		buf += w25q->pageSize - localOffset;
		localOffset = 0;
	}
	while(bytesToWrite > 0);
}

void W25Q_ReadByte(W25Q_st* w25q, uint8_t* byte, uint32_t addr)
{
	while(w25q->lock==1)
		W25Q_Delay(w25q, 1);

	w25q->lock=1;

	w25q->descriptor.spiChipSelect(w25q, 0);

	W25Q_SendByte(w25q, W25_FAST_READ);

	if(w25q->id >= W25Q256){
		W25Q_SendByte(w25q, (addr & 0xFF000000) >> 24);
	}

	W25Q_SendByte(w25q, (addr & 0xFF0000) >> 16);
	W25Q_SendByte(w25q, (addr & 0xFF00) >> 8);
	W25Q_SendByte(w25q, (addr & 0xFF));

	W25Q_SendByte(w25q, 0);

	*byte = W25Q_SendByte(w25q, W25QXX_DUMMY_BYTE);

	w25q->descriptor.spiChipSelect(w25q, 1);

	w25q->lock = 0;
}

void W25Q_ReadPage(W25Q_st* w25q, uint8_t *buf, uint32_t pageAddr, uint32_t offset, uint32_t size)
{
	while(w25q->lock==1)
		W25Q_Delay(w25q, 1);

	w25q->lock = 1;

	if((size > w25q->pageSize) || (size == 0))
		size = w25q->pageSize;

	if((offset + size) > w25q->pageSize)
		size = w25q->pageSize - offset;

	pageAddr = pageAddr * w25q->pageSize + offset;
	w25q->descriptor.spiChipSelect(w25q, 0);

	W25Q_SendByte(w25q, W25_FAST_READ);

	if(w25q->id >= W25Q256){
		W25Q_SendByte(w25q, (pageAddr & 0xFF000000) >> 24);
	}

	W25Q_SendByte(w25q, (pageAddr & 0xFF0000) >> 16);
	W25Q_SendByte(w25q, (pageAddr & 0xFF00) >> 8);
	W25Q_SendByte(w25q, (pageAddr & 0xFF));

	W25Q_SendByte(w25q, 0);

	w25q->descriptor.spiReceive(w25q, buf, size);

	w25q->descriptor.spiChipSelect(w25q, 1);

	W25Q_Delay(w25q, 1);
	w25q->lock = 0;
}

void W25Q_ReadSector(W25Q_st* w25q, uint8_t *buf, uint32_t sectorAddr, uint32_t offset, uint32_t size)
{
	if((size > w25q->sectorSize) || (size==0))
		size = w25q->sectorSize;

	uint32_t startPage;
	int32_t	bytesToRead;
	uint32_t localOffset;

	if((offset + size) > w25q->sectorSize)
		bytesToRead = w25q->sectorSize - offset;
	else
		bytesToRead = size;

	startPage = W25Q_SectorToPage(w25q, sectorAddr) + (offset / w25q->pageSize);

	localOffset = offset % w25q->pageSize;

	do
	{
		W25Q_ReadPage(w25q, buf, startPage, localOffset, bytesToRead);
		startPage++;
		bytesToRead -= w25q->pageSize - localOffset;
		buf += w25q->pageSize - localOffset;
		localOffset = 0;
	}
	while(bytesToRead > 0);
}

void W25Q_ReadBlock(W25Q_st* w25q, uint8_t* buf, uint32_t blockAddr, uint32_t offset, uint32_t size)
{
	if((size > w25q->blockSize) || (size == 0))
		size = w25q->blockSize;

	uint32_t startPage;
	int32_t	bytesToRead;
	uint32_t localOffset;

	if((offset + size) > w25q->blockSize)
		bytesToRead = w25q->blockSize - offset;
	else
		bytesToRead = size;

	startPage = W25Q_BlockToPage(w25q, blockAddr) + (offset / w25q->pageSize);

	localOffset = offset%w25q->pageSize;

	do
	{
		W25Q_ReadPage(w25q, buf,startPage,localOffset,bytesToRead);
		startPage++;
		bytesToRead -= w25q->pageSize - localOffset;
		buf += w25q->pageSize - localOffset;
		localOffset = 0;
	}
	while(bytesToRead > 0);
}
