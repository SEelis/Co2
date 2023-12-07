/*
 ===============================================================================
 Name        : main.c
 Author      : $(author)
 Version     :
 Copyright   : $(copyright)
 Description : main definition
 ===============================================================================
 */

#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif

#include <cr_section_macros.h>

// TODO: insert other include files here
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "heap_lock_monitor.h"
#include "retarget_uart.h"

#include "ModbusRegister.h"
#include "DigitalIoPin.h"
#include "LiquidCrystal.h"

#include "utils/Globals.h"
#include "input/rotaryinput.h"
#include "ui/Menu.h"

// TODO: insert other definitions and declarations here

/* The following is required if runtime statistics are to be collected
 * Copy the code to the source file where other you initialize hardware */
extern "C" {

void vConfigureTimerForRunTimeStats(void) {
	Chip_SCT_Init(LPC_SCTSMALL1);
	LPC_SCTSMALL1->CONFIG = SCT_CONFIG_32BIT_COUNTER;
	LPC_SCTSMALL1->CTRL_U = SCT_CTRL_PRE_L(255) | SCT_CTRL_CLRCTR_L; // set prescaler to 256 (255 + 1), and start timer
}

}
/* end runtime statictics collection */

static void idle_delay() {
	vTaskDelay(1);
}

void task1(void *params) {
	(void) params;

	retarget_init();

	/*ModbusMaster node4(240);
	node4.begin(9600); // all nodes must operate at the same speed!
	node4.idle(idle_delay); // idle function is called while waiting for reply from slave
	ModbusMaster node3(241); // Create modbus object that connects to slave id 241 (HMP60)
	node3.begin(9600); // all nodes must operate at the same speed!
	node3.idle(idle_delay); // idle function is called while waiting for reply from slave
	ModbusRegister RH(&node3, 256, true);
	ModbusRegister temp(&node3, 257, true);
	ModbusRegister carb(&node4, 256, true);*/

	DigitalIoPin relay(0, 27, DigitalIoPin::output); // CO2 relay
	relay.write(0);

	DigitalIoPin sw_a2(1, 8, DigitalIoPin::pullup, true);
	DigitalIoPin sw_a3(0, 5, DigitalIoPin::pullup, true);
	DigitalIoPin sw_a4(0, 6, DigitalIoPin::pullup, true);
	DigitalIoPin sw_a5(0, 7, DigitalIoPin::pullup, true);

	DigitalIoPin *rs = new DigitalIoPin(0, 29, DigitalIoPin::output);
	DigitalIoPin *en = new DigitalIoPin(0, 9, DigitalIoPin::output);
	DigitalIoPin *d4 = new DigitalIoPin(0, 10, DigitalIoPin::output);
	DigitalIoPin *d5 = new DigitalIoPin(0, 16, DigitalIoPin::output);
	DigitalIoPin *d6 = new DigitalIoPin(1, 3, DigitalIoPin::output);
	DigitalIoPin *d7 = new DigitalIoPin(0, 0, DigitalIoPin::output);
	LiquidCrystal *lcd = new LiquidCrystal(rs, en, d4, d5, d6, d7);
	// configure display geometry
	lcd->begin(16, 2);
	// set the cursor to column 0, line 1
	// (note: line 1 is the second row, since counting begins with 0):
	lcd->setCursor(0, 0);
	// Print a message to the LCD.
	lcd->print("MQTT_FreeRTOS");

	Menu menu = Menu();
	InputEvent inputEvent;

	while (true) {
		float relativeHumidity;
		int co2Level;
		int temperature;

		char buffer[32];

//		vTaskDelay(2000);
		if (xQueueReceive(globalStruct.rotaryEncoderQueue, &inputEvent,
				5000) == pdTRUE) {
			menu.handle_input(inputEvent);
		} else {
			menu.idle();
		}

		switch (menu.get_state()) {
			case ViewCo2Level: {
				co2Level = globalStruct.co2level;
				snprintf(buffer, 32, "Co2=%dppm", co2Level);
				printf("res: %s\n", buffer);
				break;
			}
			case ViewHumidity: {
				relativeHumidity = globalStruct.humidity;
				snprintf(buffer, 32, "RH=%5.1f%%", relativeHumidity);
				printf("res: %s\n", buffer);
				break;
			}
			case ViewTemperature: {
				temperature = globalStruct.temperature;
				snprintf(buffer, 32, "Temp=%dC", temperature);
				printf("res: %s\n", buffer);
			}
		}

		lcd->setCursor(0, 1);
		// Print a message to the LCD.
		lcd->print(buffer);
		vTaskDelay(1);
	}
}

void modbusTask(void *params) {
	ModbusMaster gmp252(240);
	gmp252.begin(9600); // all nodes must operate at the same speed!
	gmp252.idle(idle_delay); // idle function is called while waiting for reply from slave
	ModbusMaster hmp60(241); // Create modbus object that connects to slave id 241 (HMP60)
	hmp60.begin(9600); // all nodes must operate at the same speed!
	hmp60.idle(idle_delay); // idle function is called while waiting for reply from slave

	ModbusRegister co2(&gmp252, 256, true);
	ModbusRegister rh(&hmp60, 256, true);
	ModbusRegister tempc(&hmp60, 257, true);

	while(true) {
		globalStruct.co2level = co2.read();
		globalStruct.humidity = rh.read() / 10.0;
		globalStruct.temperature = tempc.read() / 10.0;
		vTaskDelay(100);
	}

}

extern "C" {
void vStartSimpleMQTTDemo(void); // ugly - should be in a header
}

int main(void) {

#if defined (__USE_LPCOPEN)
	// Read clock settings and update SystemCoreClock variable
	SystemCoreClockUpdate();
#if !defined(NO_BOARD_LIB)
	// Set up and initialize all required blocks and
	// functions related to the board hardware
	Board_Init();
	// Set the LED to the state of "On"
	Board_LED_Set(0, true);
#endif
#endif

	heap_monitor_setup();

	// Setup global state
	initializeGlobalStruct();

	// Setup input (rotary encoder)
	setup_input_gpios();

	// initialize RIT (= enable clocking etc.)
	//Chip_RIT_Init(LPC_RITIMER);
	// set the priority level of the interrupt
	// The level must be equal or lower than the maximum priority specified in FreeRTOS config
	// Note that in a Cortex-M3 a higher number indicates lower interrupt priority
	//NVIC_SetPriority( RITIMER_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1 );

	xTaskCreate(task1, "test",
		configMINIMAL_STACK_SIZE * 4, NULL, (tskIDLE_PRIORITY + 1UL),
		(TaskHandle_t*) NULL);

	xTaskCreate(modbusTask, "modbusTask",
		configMINIMAL_STACK_SIZE * 4, NULL, (tskIDLE_PRIORITY + 1UL),
		(TaskHandle_t*) NULL);

	vStartSimpleMQTTDemo();
	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
