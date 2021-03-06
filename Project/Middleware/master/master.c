#include "radio.h"

#define RECEIVE_CHANNEL 0x01
#define TRANSMIT_CHANNEL 0x00
#define BAUDRATE 9600
#define NODES_NUMBER 5
#define TIMEOUT_COUNT 5
#define TIMEOUT_ENABLE
//#define DEBUG_MSG

// Radio settings
// Chipcon
// Product = CC2500
// Chip version = E   (VERSION = 0x03)
// Crystal accuracy = 10 ppm
// X-tal frequency = 26 MHz
// RF output power = 1 dBm
// RX filterbandwidth = 541.666667 kHz
// Phase = 1
// Datarate ~ 4 kBaud
// Modulation = (7) MSK
// Manchester enable = (0) Manchester disabled
// RF Frequency = 2432.999908 MHz
// Channel spacing = 199.951172 kHz
// Channel number = 6
// Optimization = Sensitivity
// Sync mode = (3) 30/32 sync word bits detected
// Format of RX/TX data = (0) Normal mode, use FIFOs for RX and TX
// CRC operation = (1) CRC calculation in TX and CRC check in RX enabled
// Forward Error Correction = (0) FEC disabled
// Length configuration = (1) Variable length packets, packet length configured by the first received byte after sync word.
// Packetlength = 255
// Preamble count = (2)  4 bytes
// Append status = 1
// Address check = (0) No address check
// FIFO autoflush = 0
// Device address = 0
// GDO0 signal selection = ( 6) Asserts when sync word has been sent / received, and de-asserts at the end of the packet
// GDO2 signal selection = (41) CHIP_RDY

RF_SETTINGS code rfSettings = {
	0x08,	// FSCTRL1		Frequency synthesizer control.
	0x00,	// FSCTRL0		Frequency synthesizer control.
	0x5D,	// FREQ2		Frequency control word, high byte.
	0x93,	// FREQ1		Frequency control word, middle byte.
	0xB1,	// FREQ0		Frequency control word, low byte.
	0x87,	// MDMCFG4		Modem configuration.
	0x43,	// MDMCFG3		Modem configuration.
	0x03,	// MDMCFG2		Modem configuration.
	0x22,	// MDMCFG1		Modem configuration.
	0xF8,	// MDMCFG0		Modem configuration.
	0x06,	// CHANNR		Channel number.
	0x44,	// DEVIATN		Modem deviation setting (when FSK modulation is enabled).
	0xB6,	// FREND1		Front end RX configuration.
	0x10,	// FREND0		Front end TX configuration.
	0x18,	// MCSM0		Main Radio Control State Machine configuration.
	0x16,	// FOCCFG		Frequency Offset Compensation Configuration.
	0x1C,	// BSCFG		Bit synchronization Configuration.
	0xC7,	// AGCCTRL2		AGC control.
	0x00,	// AGCCTRL1		AGC control.
	0xB0,	// AGCCTRL0		AGC control.
	0xEA,	// FSCAL3		Frequency synthesizer calibration.
	0x0A,	// FSCAL2		Frequency synthesizer calibration.
	0x00,	// FSCAL1		Frequency synthesizer calibration.
	0x11,	// FSCAL0		Frequency synthesizer calibration.
	0x59,	// FSTEST		Frequency synthesizer calibration.
	0x88,	// TEST2		Various test settings.
	0x31,	// TEST1		Various test settings.
	0x0B,	// TEST0		Various test settings.
	0x07,	// FIFOTHR		RXFIFO and TXFIFO thresholds.
	0x29,	// IOCFG2		GDO2 output pin configuration.
	0x06,	// IOCFG0D		GDO0 output pin configuration.
	0x04,	// PKTCTRL1		Packet automation control.
	0x05,	// PKTCTRL0		Packet automation control.
	0x00,	// ADDR			Device address.
	0xFF 	// PKTLEN		Packet length.
};


BYTE code paTable = 0xFF;	// PATABLE (1 dBm output power)


BYTE xdata txBuffer[] = {1, 0};
BYTE xdata rxBuffer[61];  // Length byte  + 2 status bytes are not stored in
						  // this buffer

UINT8 length;
UINT8 timeout = 0;
UINT16 timer2_interrupt_count = 0;
UINT16 measurements[5] = {0, 0, 0, 0, 0};
UINT8 received=0;

void UART0_Init(void) {
	P0MDOUT |= 0x10;  // enable UTX as push-pull output
	XBR0 |= 0x01;     // Enable UART on P0.4(TX) and P0.5(RX)
	XBR1 |= 0x40;     // Enable crossbar and weak pull-ups

	SCON0 = 0x10;  // SCON0: 8-bit variable bit rate
					//        level of STOP bit is ignored
					//        RX enabled
					//        ninth bits are zeros
					//        clear RI0 and TI0 bits
	if (SYSCLK / BAUDRATE / 2 / 256 < 1) {
		TH1 = -(SYSCLK / BAUDRATE / 2);
		CKCON &= ~0x0B;  // T1M = 1; SCA1:0 = xx
		CKCON |= 0x08;
	} else if (SYSCLK / BAUDRATE / 2 / 256 < 4) {
		TH1 = -(SYSCLK / BAUDRATE / 2 / 4);
		CKCON &= ~0x0B;  // T1M = 0; SCA1:0 = 01
		CKCON |= 0x01;
	} else if (SYSCLK / BAUDRATE / 2 / 256 < 12) {
		TH1 = -(SYSCLK / BAUDRATE / 2 / 12);
		CKCON &= ~0x0B;  // T1M = 0; SCA1:0 = 00
	} else {
		TH1 = -(SYSCLK / BAUDRATE / 2 / 48);
		CKCON &= ~0x0B;  // T1M = 0; SCA1:0 = 10
		CKCON |= 0x02;
	}

	TL1 = TH1;      // init Timer1
	TMOD &= ~0xf0;  // TMOD: timer 1 in 8-bit autoreload
	TMOD |= 0x20;
	TR1 = 1;  // START Timer1
	TI0 = 1;  // Indicate TX0 ready
}

void Timer2_Init () {
   TMR2CN = 0x00;                      // Stop Timer2; Clear TF2;
                                       // use SYSCLK/12 as timebase

   CKCON &= ~0x30;                     // Timer2 clocked based on T2XCLK;

   TMR2RLH = 0x4e;
   TMR2RLL = 0x20;

   TMR2 = 0xffff;                      // Set to reload immediately
   timer2_interrupt_count = 0;
   timeout = 0;
   ET2 = 1;                            // Enable Timer2 interrupts
   TR2 = 1;                            // Start Timer2
}

void Timer2_Stop(void){
	TR2 = 0;
	timeout = 0;
}

void setup(void) {

	// SYSCLK_Init ();							//
	// Initialize system clock to 24.5MHz
	CLOCK_INIT();          // Initialize clock
	PORT_Init();           // Initialize crossbar and GPIO
	SPI_INIT(SCLK_6_MHZ);  // Initialize SPI

	UART0_Init();

	waitRadioForResponce();  // you need to wait ~41 usecs, before CC2500 responds
	resetRadio();

	halRfWriteRfSettings(&rfSettings);
	halSpiWriteReg(CCxxx0_PATABLE, paTable);

	// Timer2_Init (SYSCLK / 12 / 10);           // Init Timer2 to generate
	// interrupts at a 10Hz rate.
	// ALARM_Init();
	EA = 1;  // enable global interrupts

	#ifdef DEBUG_MSG
	printf("\033[2J");	// Clear Screen
	printf("\033[0;0H");	// Move cursor to 0,0
	#endif
}

void txMode(void) {
//	LED = 1;
    halSpiWriteReg(CCxxx0_CHANNR, TRANSMIT_CHANNEL);
	halRfSendPacket(txBuffer, sizeof(txBuffer));
//	LED = 0;
}

void rxMode(void) {
    halSpiWriteReg(CCxxx0_CHANNR, RECEIVE_CHANNEL);
	length = sizeof(rxBuffer);
	#ifdef TIMEOUT_ENABLE
	Timer2_Init();
	#endif
	received = halRfReceivePacket(rxBuffer, &length, &timeout);
//	printf("RECEIVED %s\n", received?"TRUE":"FALSE");
	#ifdef TIMEOUT_ENABLE
	Timer2_Stop();
	#endif
}

void request(void) {
	int i;

	#ifdef DEBUG_MSG
	printf("Broadcast... ");
	#endif

	LED = 1;
	txBuffer[1] = 0;
//	halWait(30000);
//	halWait(30000);
//	halWait(30000);
//	halWait(30000);
//	halWait(30000);
//	halWait(30000);
//	halWait(30000);
//	halWait(30000);
	txMode();

	halWait(30000);
	halWait(30000);
	halWait(30000);
	halWait(30000);
	halWait(30000);
	halWait(30000);
	halWait(30000);
	halWait(30000);
	LED = 0;

	#ifdef DEBUG_MSG
	printf("Success!\n");
	#endif

	for (i = 0; i < NODES_NUMBER; i++) {
		txBuffer[1] = (BYTE) i + 1;

		#ifdef DEBUG_MSG
		printf("Requesting from: %d... \n", (int) txBuffer[1]);
		#endif

		txMode();
//		printf("Success!\n");
		rxMode();
		if(!received) continue;

		#ifdef DEBUG_MSG
		printf("Received from: %d\n", (int) rxBuffer[2]);
		#endif

		measurements[i] = (rxBuffer[1] << 8) | rxBuffer[0];
	}
	printf("%d %d %d %d %d\n", measurements[0], measurements[1], measurements[2], measurements[3], measurements[4]);
}

INTERRUPT(Timer2_ISR, INTERRUPT_TIMER2) {
	TF2H = 0; // Clear Timer2 interrupt flag
	timer2_interrupt_count++;
	//   printf("Timer2 Interrupt Count: %u\n", timer2_interrupt_count);
	if(timer2_interrupt_count >= TIMEOUT_COUNT){
		timer2_interrupt_count = 0;
		timeout = 1;
		#ifdef DEBUG_MSG
		printf("-------- TIMEOUT --------\n");
		#endif
	}
}

void loop(void) {
	request();
}

void main(void) {
	setup();
	while (1) loop();
}
