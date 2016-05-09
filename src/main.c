/**
 * \file main.c
 * CX-DP60 emulator for ATtiny85.
 */
#define F_CPU 8000000UL

#include <stdbool.h>
#include <avr/interrupt.h>
#include <util/delay.h>

/**
 * \def CDC_DAT
 * 8-pin DIN pin 1 (\e data from CDC).
 */
#define CDC_DAT PB0
/**
 * \def CDC_CLK
 * 8-pin DIN pin 2 (\e clock from CDC).
 */
#define CDC_CLK PB1
/**
 * \def CDC_STB
 * 8-pin DIN pin 4 (\e strobe from CDC).
 */
#define CDC_STB PB3
/**
 * \def REMOCON
 * 8-pin DIN pin 5 (\e remocon from radio).
 */
#define REMOCON PB2

//***************************************************************************/

/**
 * CDC player states.
 */
typedef enum {
	/**
	 * CDC has been powered on.
	 */
	STATE_INIT,
	/**
	 * The radio selected the CDC input.
	 */
	STATE_PLAY,
	/**
	 * The radio \e deselected the CDC (either by selecting the radio, tape or
	 * powering off).
	 */
	STATE_STOP
} state_t;

/**
 * Container for the eight byte message sent to the radio.
 */
struct payload {
	uint8_t b0; /**< byte 1 */
	uint8_t b1; /**< byte 2 */
	uint8_t b2; /**< byte 3 */
	uint8_t b3; /**< byte 4 */
	uint8_t b4; /**< byte 5 */
	uint8_t b5; /**< byte 6 */
	uint8_t b6; /**< byte 7 */
	uint8_t b7; /**< byte 8 */
};

/**
 * Message sent when the CDC is first powered up. This (and the other \c
 * payload entries, were recorded with Saleae Logic).
 */
struct payload dataInit = {
	0x73, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x7B
};

/**
 * Message sent during CD playback. The \c payload no doubt contains CD, track
 * and time data, but we don't need to even know.
 */
struct payload dataPlay = {
	0x33, 0xFF, 0xCF, 0x81, 0xCF, 0x7F, 0x7F, 0x3B
};

/**
 * Message sent when either powering off or switching sources.
 */
struct payload dataStop = {
	0x13, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x1B
};

//********************************** State **********************************/

/**
 * CDC player state.
 */
state_t state = STATE_INIT;

/**
 * Timer 1 overflow count (each is equal to 256us).
 */
uint16_t oflow1 = 0;

/**
 * Number of \e us the \e remote signal was \e high.
 */
uint16_t remHi = 0;

/**
 * Number of \e us the \e remote signal was \e low.
 */
uint16_t remLo = 0;

/**
 * \true if the \e remocon signal is being decoded (otherwise the decoder is
 * waiting on the start bit).
 */
bool decode = false;

/**
 * Current bit being processed from the \e remocon signal.
 */
uint8_t cmdBit = 0;

/**
 * Buffer into which the \e remocon signal is composed, one bit at a time.
 */
uint32_t cmdBuf = 0;

//********************************* Sending *********************************/

/**
 * Sends a single bit on the \e data line followed by a \e clock (high then
 * low, with the radio reading on the low). The operation takes approximately
 * 20us (when counting in the call overhead from \c #sendByte()).
 * 
 * \param[in] val bit value
 */
void sendBit(bool val) {
	if (val) {
		PORTB |=  _BV(CDC_DAT);
	} else {
		PORTB &= ~_BV(CDC_DAT);
	}
	PORTB |=  _BV(CDC_CLK);
	_delay_us(5);
	PORTB &= ~_BV(CDC_CLK);
	_delay_us(11);
}

/**
 * Sends a single byte on the \e data line, as eight bit/clock operations
 * followed by an optional \e strobe.
 * 
 * \param[in] val byte value
 * \param[in] stb \c true \e strobe should pulse low after sending the byte
 */
void sendByte(uint8_t val, bool stb) {
	PORTB |= _BV(CDC_STB);
	for (int8_t n = 7; n >= 0; n--) {
		sendBit((val >> n) & 1);
	}
	if (stb) {
		_delay_us(100);
		PORTB &= ~_BV(CDC_STB);
		_delay_us(5);
		PORTB |=  _BV(CDC_STB);
	} else {
		_delay_us(5);
	}
	_delay_us(15);
}

/**
 * Sends an eight byte message, with \e clock and \e strobe signals.
 * 
 * \param[in] data complete message to send
 */
void sendBytes(struct payload* data) {
	uint8_t* val = &data->b0;
	for (int8_t n = 7; n >= 0; n--) {
		sendByte(*val++, (n == 0) || (n == 7));
		if (n != 0) {
			/*
			 * This timing is nothing like a real CDC but it works just fine
			 * with the MG radio. A real CDC has a long 4ms delay between each
			 * pair of bytes sent.
			 */
			_delay_us(50);
		}
	}
}

//******************************** Receiving ********************************/

/**
 * \def APPROX
 * Compares approximately \a val with \a cmp, give or take 40%. Used to
 * compare timings where fluctuations are present.
 * 
 * \param val \c uint16_t value
 * \param cmp \c uint16_t to compare with \a val
 */
#define APPROX(val, cmp) ((val > (uint16_t) ((cmp * 60UL) / 100U)) && (val < (uint16_t) ((cmp * 140UL) / 100U)))

/**
 * Resets the \e remocon decoder state.
 */
void resetDecoder() {
	oflow1 = 0;
	decode = false;
	cmdBit = 0;
}

/**
 * The 8-bit timer for \e remocon signals has overflowed (which is the
 * expected behaviour after 256us).
 */
ISR(TIMER1_OVF_vect) {
	oflow1++;
	/*
	 * If the overflow reached 65ms (i.e. it overflowed 254 times) then the
	 * decoding state is reset. In use, signals over 20ms are either an error
	 * or the wait between pulses (which we're not interested in anyway).
	 */
	if (oflow1 >= 254) {
		resetDecoder();
	}
}

/**
 * \c INT0 handler (transition of \e remocon signal), decoding the RC5-style
 * stream into radio commands.
 */
ISR(INT0_vect) {
	/*
	 * Any processing is done on the rising edge (remocon high), but we're
	 * interested in the previous pulse's falling edge (and its mark/space
	 * ratio). Any unknown pulses and we reset the decoder.
	 */
	if (PINB & _BV(REMOCON)) {
		remLo = (oflow1 << 8) + TCNT1;
		if (APPROX(remHi, 9000) && APPROX(remLo, 4500)) {
			/*
			 * Found a 9ms/4.5ms pulse. No matter what is being processed,
			 * this always signifies a start bit (resetting the decoding
			 * state).
			 */
			decode = true;
			cmdBit = 0;
		} else {
			if (decode) {
				if (APPROX(remHi, 650)) {
					if (APPROX(remLo, 1750)) {
						cmdBuf <<= 1;
						cmdBuf  |= 1;
						cmdBit++;
					} else {
						if (APPROX(remLo, 650)) {
							cmdBuf <<= 1;
							cmdBit++;
						} else {
							resetDecoder();
						}
					}
					if (cmdBit == 32) {
						/*
						 * After 32 bits have been processed we have a full
						 * radio command. The first two bytes are always
						 * 0x532C, the last two bytes are the inverse of one
						 * another (and contain the actual command).
						 */
						if ((cmdBuf >> 16) == 0x532C) {
							if (((cmdBuf >> 8) & 0xFF) == ((~cmdBuf) & 0xFF)) {
								/*
								 * Known buttons/commands:
								 * 
								 *   08 Power on
								 *   A4 CDC select
								 *   10 CDC deselect (and power off)
								 *   68 >>
								 *   A8 <<
								 *   38 PRG
								 *   D0 TPS
								 *   98 SKIP
								 *   48 REP
								 *   F0 D
								 */
								switch ((cmdBuf >> 8) & 0xFF) {
								case 0x08:
								case 0xA4:
									state = STATE_PLAY;
									break;
								case 0x10:
									state = STATE_STOP;
									break;
								default:
									/*
									 * We leave the current state.
									 */
									break;
								}
							}
						}
						resetDecoder();
					}
				} else {
					resetDecoder();
				}
			} else {
				resetDecoder();
			}
		}
	} else {
		remHi = (oflow1 << 8) + TCNT1;
	}
	TCNT1  = 0;
	oflow1 = 0;
}

//***************************************************************************/

/**
 * Delays for the requested number of milliseconds. Calling \c _delay_ms()
 * alone seems to block interrupts for the requested duration, so multiple
 * shorter calls are made.
 * 
 * \todo test whether this is needed
 * 
 * \param[in] ms number of milliseconds
 */
void _delay(int8_t ms) {
	for (; ms > 0; ms--) {
		_delay_ms(1);
	}
}

/**
 * Main loop.
 * \n
 * \note \e select is being ignored (from the radio) since the CDC always
 * sends data anyway (\e select is high when the radio is powered on, and
 * since the radio can't power off without communicating with the CDC, its
 * use can be inferred).
 * 
 * \todo the send loops should be timer/state machine driven so the MCU can sleep (then everything is interrupt driven)
 */
int main(void) {
	/*
	 * Wait for things to settle after power-on before starting.
	 */
	_delay_ms(100);
	/*
	 * Data, clock and strobe as output, set high. Remote as input, tri-state
	 * (this was externally strongly pulled low but the radio couldn't drive
	 * it; weakly pulled low was noisy). Remaining ports pulled high (inc. PB4
	 * which is disconnected).
	 */
	DDRB  =  _BV(CDC_DAT)
		  |  _BV(CDC_CLK)
		  |  _BV(CDC_STB);
	PORTB = ~_BV(REMOCON);
	/*
	 * Timer1 with CK/8 @ 8MHz gives 1us per tick (with overflow). It's a good
	 * compromise, allowing for simpler calculations (e.g. a 600us period is
	 * 600 ticks).
	 */
	TCCR1  =  _BV(CS12);	// CK/8
	TIMSK  =  _BV(TOIE1);	// enable overflow
	MCUCR |=  _BV(ISC00);	// any logical change on INT0
	MCUCR &= ~_BV(ISC01);
	GIMSK |=  _BV(INT0);	// enable INT0
	sei();
	/*
	 * Sit waiting, switching between waiting for a connection, 'playing' a
	 * CD, and powering off (which goes back to waiting).
	 */
	while(1) {
		switch (state) {
		case STATE_INIT:
			sendBytes(&dataInit);
			break;
		case STATE_PLAY:
			sendBytes(&dataPlay);
			break;
		default:
			/*
			 * Note: the capture shows a few of these 'stop' payloads sent
			 * (followed by another packet, seemingly not needed, before going
			 * back to the 'init').
			 */
			sendBytes(&dataStop);
			_delay(40);
			sendBytes(&dataStop);
			state = STATE_INIT;
		}
		_delay(40);
	}
}