APP_DIR=app
LVGL_DIR=app/lvgl

APP_C_SOURCES = 				\
$(APP_DIR)/app.c 				\
$(APP_DIR)/led.c 				\
$(APP_DIR)/lcd_bl.c			\
$(APP_DIR)/dwt.c				\
$(APP_DIR)/gt911.c			\
$(APP_DIR)/lvgl_app.c 

LVGL_SOURCES := $(shell find $(LVGL_DIR)/src -name "*.c")
LVGL_DEMO_SOURCES := $(shell find $(LVGL_DIR)/demos -name "*.c")

C_SOURCES += $(APP_C_SOURCES) 
C_SOURCES += $(LVGL_SOURCES)
C_SOURCES += $(LVGL_DEMO_SOURCES)

CFLAGS += -DATA_IN_D2_SRAM -I$(APP_DIR) -I$(LVGL_DIR)
C_DEFS += 
LIBS += 

dfu:
	-sudo dfu-util -a 0 -s 0x08000000:leave -D $(BUILD_DIR)/$(TARGET).bin

stflash:
	-st-flash --flash=1024k erase
	-st-flash --reset --flash=1024k --format binary write $(BUILD_DIR)/$(TARGET).bin 0x08000000
