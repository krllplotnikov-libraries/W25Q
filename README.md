# W25Q

Аппаратно-независимая библиотека для SPI NOR-флешек Winbond W25Q10…W25Q512 с синхронным API.

- Автоопределение модели по JEDEC ID (производитель `0xEF`, типы `0x40`/`0x60`/`0x70`).
- W25Q256/W25Q512: автоматический переход в 4-байтную адресацию (`0xB7`) с проверкой бита ADS.
- Все функции синхронные: возвращают управление после завершения операции (опрос бита BUSY с таймаутом).
- Чтение одной непрерывной командой на любую длину; запись любой длины с автоматической разбивкой по страницам.
- Коды результата `W25Q_Result_et`, проверка аргументов.
- Корректная работа при переполнении 32-битного тика.

## Подключение

Библиотека получает аппаратные функции через дескриптор:

| Поле | Назначение |
|---|---|
| `setChipSelect` | управление линией CS (`W25Q_CHIP_SELECT_ACTIVE` = низкий уровень) |
| `transmit` | передача `_size` байт, возвращает `W25Q_RESULT_OK` или `W25Q_RESULT_ERROR_SPI` |
| `receive` | приём `_size` байт, возвращает `W25Q_RESULT_OK` или `W25Q_RESULT_ERROR_SPI` |
| `getTick` | миллисекундный счётчик |

Пример `Board.c` (STM32 HAL). `HAL_SPI_Transmit/Receive` принимают `uint16_t` размер, поэтому длинные передачи режутся на части:

```c
#define BOARD_FLASH_SPI_TIMEOUT_MS 100u

static void           setFlashChipSelect(W25Q_st* _w25q, W25Q_ChipSelect_et _state);
static W25Q_Result_et flashTransmit(W25Q_st* _w25q, const uint8_t* _data, size_t _size);
static W25Q_Result_et flashReceive(W25Q_st* _w25q, uint8_t* _data, size_t _size);

void Board_Init(Board_st* _board){
	boardInstance = _board;

	W25Q_Descriptor_st flashDescriptor = {
		.setChipSelect = setFlashChipSelect,
		.transmit      = flashTransmit,
		.receive       = flashReceive,
		.getTick       = getTick,
	};
	(void)W25Q_Init(&_board->flashMain, &flashDescriptor);   // результат можно проверить и позже: W25Q_GetModel()
}

static void setFlashChipSelect(W25Q_st* _w25q, W25Q_ChipSelect_et _state){
	if(_w25q == &boardInstance->flashMain){
		HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, (_state == W25Q_CHIP_SELECT_ACTIVE) ? GPIO_PIN_RESET : GPIO_PIN_SET);
	}
}

static W25Q_Result_et flashTransmit(W25Q_st* _w25q, const uint8_t* _data, size_t _size){
	if(_w25q != &boardInstance->flashMain){
		return W25Q_RESULT_ERROR_SPI;
	}

	while(_size > 0){
		uint16_t chunkSize = (_size > UINT16_MAX) ? UINT16_MAX : (uint16_t)_size;

		if(HAL_SPI_Transmit(&hspi1, (uint8_t*)_data, chunkSize, BOARD_FLASH_SPI_TIMEOUT_MS) != HAL_OK){
			return W25Q_RESULT_ERROR_SPI;
		}

		_data += chunkSize;
		_size -= chunkSize;
	}

	return W25Q_RESULT_OK;
}

static W25Q_Result_et flashReceive(W25Q_st* _w25q, uint8_t* _data, size_t _size){
	if(_w25q != &boardInstance->flashMain){
		return W25Q_RESULT_ERROR_SPI;
	}

	while(_size > 0){
		uint16_t chunkSize = (_size > UINT16_MAX) ? UINT16_MAX : (uint16_t)_size;

		if(HAL_SPI_Receive(&hspi1, _data, chunkSize, BOARD_FLASH_SPI_TIMEOUT_MS) != HAL_OK){
			return W25Q_RESULT_ERROR_SPI;
		}

		_data += chunkSize;
		_size -= chunkSize;
	}

	return W25Q_RESULT_OK;
}
```

## Инициализация

`W25Q_Init` копирует дескриптор, поднимает CS, ждёт 10 мс после подачи питания, отправляет Release Power-down (`0xAB`), дожидается окончания операции, которая могла идти до сброса МК, читает JEDEC ID, для W25Q256/512 включает 4-байтный режим и читает Unique ID.

Возвращает `W25Q_RESULT_OK`, `ERROR_DEVICE_NOT_FOUND`, `ERROR_SPI` или `ERROR_ADDRESS_MODE`. При ошибке объект остаётся неинициализированным, остальные функции возвращают `ERROR_DEVICE_NOT_FOUND`; повторная попытка — снова `W25Q_Init`.

## API

Адресация линейная, в байтах. Номер сектора/блока → адрес: `sector * W25Q_SECTOR_SIZE`, `block * W25Q_BLOCK_SIZE`.

| Функция | Что делает | Типичное время (W25Q128JV) |
|---|---|---|
| `W25Q_Read(w25q, address, data, size)` | чтение любой длины | определяется SPI |
| `W25Q_Write(w25q, address, data, size)` | запись любой длины, по страницам | ~0,4 мс на страницу 256 Б |
| `W25Q_EraseSector(w25q, sector)` | стирание 4 КБ | 45 мс (макс. 400 мс) |
| `W25Q_EraseBlock(w25q, block)` | стирание 64 КБ | 150 мс (макс. 2 с) |
| `W25Q_EraseChip(w25q)` | стирание всей микросхемы | 40 с (макс. 200 с) |
| `W25Q_GetModel`, `W25Q_GetCapacity`, `W25Q_GetUniqueId` | информация о чипе | — |

Возвращаемые значения:

- `W25Q_RESULT_OK` — операция выполнена;
- `W25Q_RESULT_BUSY` — чип ещё занят (например, после таймаута предыдущей операции);
- `W25Q_RESULT_ERROR_INVALID_ARGUMENT` — `NULL`, нулевой размер, выход за ёмкость, номер сектора/блока вне диапазона;
- `W25Q_RESULT_ERROR_TIMEOUT` — чип не завершил запись/стирание за отведённое время;
- `W25Q_RESULT_ERROR_DEVICE_NOT_FOUND` — чип не инициализирован;
- `W25Q_RESULT_ERROR_SPI` — ошибка аппаратной функции.

Важно:

- Функции блокируют вызывающий код на время операции. Стирание чипа — десятки секунд; в суперцикле с другими задачами лучше стирать по секторам/блокам или вынести работу с флешкой в отдельную задачу RTOS.
- Запись только переводит биты 1 → 0: перед записью область нужно стереть.
- Объект не потокобезопасен: все вызовы — из одного контекста, не из прерываний.

Пример:

```c
uint8_t        buffer[100];
W25Q_Result_et result = W25Q_EraseSector(&board.flashMain, 0);

if(result == W25Q_RESULT_OK){
	result = W25Q_Write(&board.flashMain, 0, buffer, sizeof(buffer));
}

if(result == W25Q_RESULT_OK){
	result = W25Q_Read(&board.flashMain, 0, buffer, sizeof(buffer));
}
```

## Тесты

Хост-тесты с программным эмулятором флешки (определение всех моделей, 3/4-байтная адресация, запись через границы страниц, проверка аргументов, таймауты, занятый при старте чип, переполнение тика):

```sh
cd test
gcc -std=c99 -Wall -Wextra -pedantic -I../src ../src/W25Q.c W25Q_Test.c -o W25Q_Test && ./W25Q_Test
```
