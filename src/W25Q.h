#ifndef W25Q_H_
#define W25Q_H_

#include <stddef.h>
#include <stdint.h>

#define W25Q_PAGE_SIZE                       256u
#define W25Q_SECTOR_SIZE                     4096u
#define W25Q_BLOCK_SIZE                      65536u
#define W25Q_UNIQUE_ID_SIZE                  8u

#define W25Q_POWER_UP_TIME_MS                10u
#define W25Q_WAKE_UP_TIME_MS                 1u
#define W25Q_PAGE_PROGRAM_TIMEOUT_MS         10u
#define W25Q_SECTOR_ERASE_TIMEOUT_MS         1000u
#define W25Q_BLOCK_ERASE_TIMEOUT_MS          4000u
#define W25Q_CHIP_ERASE_TIMEOUT_PER_BLOCK_MS 2000u

enum W25Q_Result{
	W25Q_RESULT_OK = 0,
	W25Q_RESULT_BUSY,
	W25Q_RESULT_ERROR_INVALID_ARGUMENT,
	W25Q_RESULT_ERROR_TIMEOUT,
	W25Q_RESULT_ERROR_SPI,
	W25Q_RESULT_ERROR_DEVICE_NOT_FOUND,
	W25Q_RESULT_ERROR_ADDRESS_MODE
};

enum W25Q_Model{
	W25Q_MODEL_UNKNOWN = 0,
	W25Q_MODEL_W25Q10,
	W25Q_MODEL_W25Q20,
	W25Q_MODEL_W25Q40,
	W25Q_MODEL_W25Q80,
	W25Q_MODEL_W25Q16,
	W25Q_MODEL_W25Q32,
	W25Q_MODEL_W25Q64,
	W25Q_MODEL_W25Q128,
	W25Q_MODEL_W25Q256,
	W25Q_MODEL_W25Q512
};

enum W25Q_ChipSelect{
	W25Q_CHIP_SELECT_INACTIVE,
	W25Q_CHIP_SELECT_ACTIVE
};

typedef enum W25Q_Result     W25Q_Result_et;
typedef enum W25Q_Model      W25Q_Model_et;
typedef enum W25Q_ChipSelect W25Q_ChipSelect_et;

typedef struct W25Q_Descriptor W25Q_Descriptor_st;
typedef struct W25Q            W25Q_st;

typedef void           (*W25Q_SetChipSelect)(W25Q_st* _w25q, W25Q_ChipSelect_et _state);
typedef W25Q_Result_et (*W25Q_Transmit)(W25Q_st* _w25q, const uint8_t* _data, size_t _size);
typedef W25Q_Result_et (*W25Q_Receive)(W25Q_st* _w25q, uint8_t* _data, size_t _size);
typedef uint32_t       (*W25Q_GetTick)(void);

struct W25Q_Descriptor{
	W25Q_SetChipSelect setChipSelect;
	W25Q_Transmit      transmit;
	W25Q_Receive       receive;
	W25Q_GetTick       getTick;
};

struct W25Q{
	W25Q_Descriptor_st descriptor;

	W25Q_Model_et      model;
	uint32_t           capacity;
	uint8_t            addressSize;
	uint8_t            uniqueId[W25Q_UNIQUE_ID_SIZE];
};

#ifdef __cplusplus
extern "C" {
#endif

W25Q_Result_et W25Q_Init(W25Q_st* _w25q, W25Q_Descriptor_st* _descriptor);

W25Q_Result_et W25Q_Read(W25Q_st* _w25q, uint32_t _address, uint8_t* _data, uint32_t _size);
W25Q_Result_et W25Q_Write(W25Q_st* _w25q, uint32_t _address, const uint8_t* _data, uint32_t _size);
W25Q_Result_et W25Q_EraseSector(W25Q_st* _w25q, uint32_t _sector);
W25Q_Result_et W25Q_EraseBlock(W25Q_st* _w25q, uint32_t _block);
W25Q_Result_et W25Q_EraseChip(W25Q_st* _w25q);

W25Q_Model_et  W25Q_GetModel(W25Q_st* _w25q);
uint32_t       W25Q_GetCapacity(W25Q_st* _w25q);
W25Q_Result_et W25Q_GetUniqueId(W25Q_st* _w25q, uint8_t* _uniqueId);

#ifdef __cplusplus
}
#endif

#endif
