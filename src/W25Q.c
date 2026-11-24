#include "W25Q.h"

//-----Data-----
#define W25Q_COMMAND_WRITE_ENABLE       0x06u
#define W25Q_COMMAND_READ_STATUS_1      0x05u
#define W25Q_COMMAND_READ_STATUS_3      0x15u
#define W25Q_COMMAND_PAGE_PROGRAM       0x02u
#define W25Q_COMMAND_FAST_READ          0x0Bu
#define W25Q_COMMAND_SECTOR_ERASE       0x20u
#define W25Q_COMMAND_BLOCK_ERASE        0xD8u
#define W25Q_COMMAND_CHIP_ERASE         0xC7u
#define W25Q_COMMAND_JEDEC_ID           0x9Fu
#define W25Q_COMMAND_UNIQUE_ID          0x4Bu
#define W25Q_COMMAND_RELEASE_POWER_DOWN 0xABu
#define W25Q_COMMAND_ENTER_4_BYTE_MODE  0xB7u

#define W25Q_STATUS_1_BUSY              0x01u
#define W25Q_STATUS_3_ADS               0x01u

#define W25Q_JEDEC_MANUFACTURER_WINBOND 0xEFu
#define W25Q_JEDEC_TYPE_SPI             0x40u
#define W25Q_JEDEC_TYPE_SPI_1V8         0x60u
#define W25Q_JEDEC_TYPE_SPI_DTR         0x70u
#define W25Q_JEDEC_CAPACITY_W25Q10      0x11u
#define W25Q_JEDEC_CAPACITY_W25Q256     0x19u
#define W25Q_JEDEC_CAPACITY_W25Q512     0x20u

#define W25Q_W25Q512_CAPACITY           0x04000000u
#define W25Q_3_BYTE_ADDRESS_LIMIT       0x01000000u
#define W25Q_HEADER_MAX_SIZE            10u
//-----Data-----



static W25Q_Result_et detectDevice(W25Q_st* _w25q);
static W25Q_Result_et decodeCapacity(W25Q_st* _w25q, uint8_t _capacityId);
static W25Q_Result_et erase(W25Q_st* _w25q, uint8_t _command, uint32_t _address, uint8_t _hasAddress, uint32_t _timeout);
static W25Q_Result_et programPage(W25Q_st* _w25q, uint32_t _address, const uint8_t* _data, uint32_t _size);
static W25Q_Result_et checkReady(W25Q_st* _w25q);
static uint8_t        isRangeValid(W25Q_st* _w25q, uint32_t _address, uint32_t _size);
static W25Q_Result_et waitWhileBusy(W25Q_st* _w25q, uint32_t _timeout);
static void           delay(W25Q_st* _w25q, uint32_t _time);
static W25Q_Result_et readStatus(W25Q_st* _w25q, uint8_t _command, uint8_t* _status);
static W25Q_Result_et sendCommand(W25Q_st* _w25q, uint8_t _command);
static size_t         buildHeader(W25Q_st* _w25q, uint8_t* _header, uint8_t _command, uint32_t _address, uint8_t _dummySize);
static W25Q_Result_et transaction(W25Q_st* _w25q, const uint8_t* _header, size_t _headerSize, const uint8_t* _txData, uint8_t* _rxData, size_t _dataSize);

W25Q_Result_et W25Q_Init(W25Q_st* _w25q, W25Q_Descriptor_st* _descriptor){
	_w25q->descriptor = *_descriptor;

	_w25q->model       = W25Q_MODEL_UNKNOWN;
	_w25q->capacity    = 0;
	_w25q->addressSize = 3;

	for(uint8_t i = 0; i < W25Q_UNIQUE_ID_SIZE; i++){
		_w25q->uniqueId[i] = 0;
	}

	_w25q->descriptor.setChipSelect(_w25q, W25Q_CHIP_SELECT_INACTIVE);
	delay(_w25q, W25Q_POWER_UP_TIME_MS);

	W25Q_Result_et result = sendCommand(_w25q, W25Q_COMMAND_RELEASE_POWER_DOWN);

	if(result != W25Q_RESULT_OK){
		return result;
	}

	delay(_w25q, W25Q_WAKE_UP_TIME_MS);

	// An erase started before an MCU reset may still be running: the chip ignores ID commands while busy
	result = waitWhileBusy(_w25q, W25Q_BLOCK_ERASE_TIMEOUT_MS);

	if(result == W25Q_RESULT_ERROR_SPI){
		return result;
	}

	result = detectDevice(_w25q);

	if(result != W25Q_RESULT_OK){
		_w25q->model    = W25Q_MODEL_UNKNOWN;
		_w25q->capacity = 0;
	}

	return result;
}

W25Q_Result_et W25Q_Read(W25Q_st* _w25q, uint32_t _address, uint8_t* _data, uint32_t _size){
	W25Q_Result_et result = checkReady(_w25q);

	if(result != W25Q_RESULT_OK){
		return result;
	}

	if((_data == NULL) || (isRangeValid(_w25q, _address, _size) == 0)){
		return W25Q_RESULT_ERROR_INVALID_ARGUMENT;
	}

	uint8_t header[W25Q_HEADER_MAX_SIZE];
	size_t  headerSize = buildHeader(_w25q, header, W25Q_COMMAND_FAST_READ, _address, 1);

	return transaction(_w25q, header, headerSize, NULL, _data, _size);
}

W25Q_Result_et W25Q_Write(W25Q_st* _w25q, uint32_t _address, const uint8_t* _data, uint32_t _size){
	W25Q_Result_et result = checkReady(_w25q);

	if(result != W25Q_RESULT_OK){
		return result;
	}

	if((_data == NULL) || (isRangeValid(_w25q, _address, _size) == 0)){
		return W25Q_RESULT_ERROR_INVALID_ARGUMENT;
	}

	while(_size > 0){
		uint32_t pageRemaining = W25Q_PAGE_SIZE - (_address % W25Q_PAGE_SIZE);
		uint32_t chunkSize     = (_size < pageRemaining) ? _size : pageRemaining;

		result = programPage(_w25q, _address, _data, chunkSize);

		if(result != W25Q_RESULT_OK){
			return result;
		}

		_address += chunkSize;
		_data    += chunkSize;
		_size    -= chunkSize;
	}

	return W25Q_RESULT_OK;
}

W25Q_Result_et W25Q_EraseSector(W25Q_st* _w25q, uint32_t _sector){
	W25Q_Result_et result = checkReady(_w25q);

	if(result != W25Q_RESULT_OK){
		return result;
	}

	if(_sector >= (_w25q->capacity / W25Q_SECTOR_SIZE)){
		return W25Q_RESULT_ERROR_INVALID_ARGUMENT;
	}

	return erase(_w25q, W25Q_COMMAND_SECTOR_ERASE, _sector * W25Q_SECTOR_SIZE, 1, W25Q_SECTOR_ERASE_TIMEOUT_MS);
}

W25Q_Result_et W25Q_EraseBlock(W25Q_st* _w25q, uint32_t _block){
	W25Q_Result_et result = checkReady(_w25q);

	if(result != W25Q_RESULT_OK){
		return result;
	}

	if(_block >= (_w25q->capacity / W25Q_BLOCK_SIZE)){
		return W25Q_RESULT_ERROR_INVALID_ARGUMENT;
	}

	return erase(_w25q, W25Q_COMMAND_BLOCK_ERASE, _block * W25Q_BLOCK_SIZE, 1, W25Q_BLOCK_ERASE_TIMEOUT_MS);
}

W25Q_Result_et W25Q_EraseChip(W25Q_st* _w25q){
	W25Q_Result_et result = checkReady(_w25q);

	if(result != W25Q_RESULT_OK){
		return result;
	}

	uint32_t timeout = (_w25q->capacity / W25Q_BLOCK_SIZE) * W25Q_CHIP_ERASE_TIMEOUT_PER_BLOCK_MS;

	return erase(_w25q, W25Q_COMMAND_CHIP_ERASE, 0, 0, timeout);
}

W25Q_Model_et W25Q_GetModel(W25Q_st* _w25q){
	return _w25q->model;
}

uint32_t W25Q_GetCapacity(W25Q_st* _w25q){
	return _w25q->capacity;
}

W25Q_Result_et W25Q_GetUniqueId(W25Q_st* _w25q, uint8_t* _uniqueId){
	if(_w25q->capacity == 0){
		return W25Q_RESULT_ERROR_DEVICE_NOT_FOUND;
	}

	if(_uniqueId == NULL){
		return W25Q_RESULT_ERROR_INVALID_ARGUMENT;
	}

	for(uint8_t i = 0; i < W25Q_UNIQUE_ID_SIZE; i++){
		_uniqueId[i] = _w25q->uniqueId[i];
	}

	return W25Q_RESULT_OK;
}

static W25Q_Result_et detectDevice(W25Q_st* _w25q){
	uint8_t        header[W25Q_HEADER_MAX_SIZE];
	uint8_t        jedecId[3] = {0};
	uint8_t        command    = W25Q_COMMAND_JEDEC_ID;
	W25Q_Result_et result     = transaction(_w25q, &command, 1, NULL, jedecId, sizeof(jedecId));

	if(result != W25Q_RESULT_OK){
		return result;
	}

	if(jedecId[0] != W25Q_JEDEC_MANUFACTURER_WINBOND){
		return W25Q_RESULT_ERROR_DEVICE_NOT_FOUND;
	}

	if((jedecId[1] != W25Q_JEDEC_TYPE_SPI) && (jedecId[1] != W25Q_JEDEC_TYPE_SPI_1V8) && (jedecId[1] != W25Q_JEDEC_TYPE_SPI_DTR)){
		return W25Q_RESULT_ERROR_DEVICE_NOT_FOUND;
	}

	result = decodeCapacity(_w25q, jedecId[2]);

	if(result != W25Q_RESULT_OK){
		return result;
	}

	_w25q->addressSize = 3;

	if(_w25q->capacity > W25Q_3_BYTE_ADDRESS_LIMIT){
		uint8_t status = 0;

		result = sendCommand(_w25q, W25Q_COMMAND_ENTER_4_BYTE_MODE);

		if(result == W25Q_RESULT_OK){
			result = readStatus(_w25q, W25Q_COMMAND_READ_STATUS_3, &status);
		}

		if(result != W25Q_RESULT_OK){
			return result;
		}

		if((status & W25Q_STATUS_3_ADS) == 0){
			return W25Q_RESULT_ERROR_ADDRESS_MODE;
		}

		_w25q->addressSize = 4;
	}

	// Read Unique ID: dummy bytes = address size + 1 (4 in 3-byte mode, 5 in 4-byte mode)
	size_t headerSize = buildHeader(_w25q, header, W25Q_COMMAND_UNIQUE_ID, 0, 1);

	return transaction(_w25q, header, headerSize, NULL, _w25q->uniqueId, W25Q_UNIQUE_ID_SIZE);
}

static W25Q_Result_et decodeCapacity(W25Q_st* _w25q, uint8_t _capacityId){
	// 0x11 (W25Q10) .. 0x19 (W25Q256): capacity = 2^id bytes, models follow W25Q_Model_et order
	if((_capacityId >= W25Q_JEDEC_CAPACITY_W25Q10) && (_capacityId <= W25Q_JEDEC_CAPACITY_W25Q256)){
		_w25q->model    = (W25Q_Model_et)(W25Q_MODEL_W25Q10 + (_capacityId - W25Q_JEDEC_CAPACITY_W25Q10));
		_w25q->capacity = 1ul << _capacityId;
	}
	else if(_capacityId == W25Q_JEDEC_CAPACITY_W25Q512){
		_w25q->model    = W25Q_MODEL_W25Q512;
		_w25q->capacity = W25Q_W25Q512_CAPACITY;
	}
	else{
		return W25Q_RESULT_ERROR_DEVICE_NOT_FOUND;
	}

	return W25Q_RESULT_OK;
}

static W25Q_Result_et erase(W25Q_st* _w25q, uint8_t _command, uint32_t _address, uint8_t _hasAddress, uint32_t _timeout){
	uint8_t header[W25Q_HEADER_MAX_SIZE];
	size_t  headerSize = 1;

	if(_hasAddress != 0){
		headerSize = buildHeader(_w25q, header, _command, _address, 0);
	}
	else{
		header[0] = _command;
	}

	W25Q_Result_et result = sendCommand(_w25q, W25Q_COMMAND_WRITE_ENABLE);

	if(result == W25Q_RESULT_OK){
		result = transaction(_w25q, header, headerSize, NULL, NULL, 0);
	}

	if(result != W25Q_RESULT_OK){
		return result;
	}

	return waitWhileBusy(_w25q, _timeout);
}

static W25Q_Result_et programPage(W25Q_st* _w25q, uint32_t _address, const uint8_t* _data, uint32_t _size){
	uint8_t header[W25Q_HEADER_MAX_SIZE];
	size_t  headerSize = buildHeader(_w25q, header, W25Q_COMMAND_PAGE_PROGRAM, _address, 0);

	W25Q_Result_et result = sendCommand(_w25q, W25Q_COMMAND_WRITE_ENABLE);

	if(result == W25Q_RESULT_OK){
		result = transaction(_w25q, header, headerSize, _data, NULL, _size);
	}

	if(result != W25Q_RESULT_OK){
		return result;
	}

	return waitWhileBusy(_w25q, W25Q_PAGE_PROGRAM_TIMEOUT_MS);
}

static W25Q_Result_et checkReady(W25Q_st* _w25q){
	if(_w25q->capacity == 0){
		return W25Q_RESULT_ERROR_DEVICE_NOT_FOUND;
	}

	// The chip may still be busy after a previous timeout
	uint8_t        status = 0;
	W25Q_Result_et result = readStatus(_w25q, W25Q_COMMAND_READ_STATUS_1, &status);

	if(result != W25Q_RESULT_OK){
		return result;
	}

	if((status & W25Q_STATUS_1_BUSY) != 0){
		return W25Q_RESULT_BUSY;
	}

	return W25Q_RESULT_OK;
}

static uint8_t isRangeValid(W25Q_st* _w25q, uint32_t _address, uint32_t _size){
	if((_size == 0) || (_address >= _w25q->capacity) || (_size > (_w25q->capacity - _address))){
		return 0;
	}

	return 1;
}

static W25Q_Result_et waitWhileBusy(W25Q_st* _w25q, uint32_t _timeout){
	uint32_t startTime = _w25q->descriptor.getTick();

	while(1){
		uint8_t        status      = 0;
		uint32_t       currentTime = _w25q->descriptor.getTick();
		W25Q_Result_et result      = readStatus(_w25q, W25Q_COMMAND_READ_STATUS_1, &status);

		if(result != W25Q_RESULT_OK){
			return result;
		}

		if((status & W25Q_STATUS_1_BUSY) == 0){
			return W25Q_RESULT_OK;
		}

		// '>' instead of '>=': with a 1 ms tick it guarantees at least _timeout ms
		if((currentTime - startTime) > _timeout){
			return W25Q_RESULT_ERROR_TIMEOUT;
		}
	}
}

static void delay(W25Q_st* _w25q, uint32_t _time){
	uint32_t startTime = _w25q->descriptor.getTick();

	// '<=' instead of '<': with a 1 ms tick it guarantees at least _time ms
	while((_w25q->descriptor.getTick() - startTime) <= _time){
	}
}

static W25Q_Result_et readStatus(W25Q_st* _w25q, uint8_t _command, uint8_t* _status){
	return transaction(_w25q, &_command, 1, NULL, _status, 1);
}

static W25Q_Result_et sendCommand(W25Q_st* _w25q, uint8_t _command){
	return transaction(_w25q, &_command, 1, NULL, NULL, 0);
}

static size_t buildHeader(W25Q_st* _w25q, uint8_t* _header, uint8_t _command, uint32_t _address, uint8_t _dummySize){
	size_t size = 0;

	_header[size++] = _command;

	if(_w25q->addressSize == 4){
		_header[size++] = (uint8_t)(_address >> 24);
	}

	_header[size++] = (uint8_t)(_address >> 16);
	_header[size++] = (uint8_t)(_address >> 8);
	_header[size++] = (uint8_t)(_address);

	for(uint8_t i = 0; i < _dummySize; i++){
		_header[size++] = 0;
	}

	return size;
}

static W25Q_Result_et transaction(W25Q_st* _w25q, const uint8_t* _header, size_t _headerSize, const uint8_t* _txData, uint8_t* _rxData, size_t _dataSize){
	_w25q->descriptor.setChipSelect(_w25q, W25Q_CHIP_SELECT_ACTIVE);

	W25Q_Result_et result = _w25q->descriptor.transmit(_w25q, _header, _headerSize);

	if((result == W25Q_RESULT_OK) && (_txData != NULL)){
		result = _w25q->descriptor.transmit(_w25q, _txData, _dataSize);
	}

	if((result == W25Q_RESULT_OK) && (_rxData != NULL)){
		result = _w25q->descriptor.receive(_w25q, _rxData, _dataSize);
	}

	_w25q->descriptor.setChipSelect(_w25q, W25Q_CHIP_SELECT_INACTIVE);

	return result;
}
