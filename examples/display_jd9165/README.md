# example/display_jd9165 — JC1060P470C_I_W_Y

Минимальный тест **дисплея JD9165** (7″ 1024×600 MIPI-DSI) + подсветка.

## Требования

- ESP-IDF **5.5.x**
- Плата GUITION **JC1060P470C_I_W_Y** (rev P4 1.0 / 1.3)
- Компонент: `espressif/esp_lcd_jd9165` **^1.0.3** (не 2.x — тот для IDF 6)

## Пины

| Сигнал | GPIO / ресурс |
|--------|----------------|
| LCD Reset | **GPIO27** (если чёрный экран — попробовать GPIO5) |
| Backlight PWM | **GPIO23** (LEDC) |
| MIPI-DSI PHY | LDO channel **3**, 2500 mV |
| Lanes | 2 |

## Сборка и прошивка

```bash
cd examples/display_jd9165
idf.py set-target esp32p4
idf.py build
idf.py -p PORT flash monitor
```

Перед первой сборкой:

```bash
idf.py add-dependency "espressif/esp_lcd_jd9165^1.0.3"
```

(или зависимости подтянутся из `main/idf_component.yml`)

## Обязательные настройки (menuconfig / sdkconfig.defaults)

1. Chip revision: **Minimum Supported = Rev v1.0**, не выбирать только ≥3.0
2. PSRAM включён, `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`
3. Flash 16 MB
4. Partition table offset **0x10000**

## Критерий успеха

- Экран загорается (подсветка)
- Виден цветной паттерн / заливка (красный → зелёный → синий → белый)
- В логе: `Display init OK`

## Известные проблемы

- Чёрный экран → проверить LDO ch3, RST (27 vs 5), lane bitrate, PSRAM
- Guru Meditation / Illegal Instruction → chip revision не v1.x
- Нет логов → подключать **USB High-Speed** порт

После успешного теста обновить `docs/JC1060P470C_I_W_Y/00-BOARD-INVENTORY.md`.
