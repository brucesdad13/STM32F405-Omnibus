#include <math.h> // fabsf
#include <stdint.h>

#include "binary.h"
#include "config.h"
#include "drv_led.h"
#include "drv_mpu.h"
#include "drv_time.h"
#include "filter.h"
#include "sixaxis.h"
#include "util.h"

// this works only on newer boards (non mpu-6050)
// on older boards the hw gyro setting controls the acc as well
#define ACC_LOW_PASS_FILTER 5

// gyro ids for the gyro check
#define GYRO_ID_1 0x68
#define GYRO_ID_2 0x98
#define GYRO_ID_3 0x7D
#define GYRO_ID_4 0x72

static void process_gyronew_to_gyro( float gyronew[] ); // To avoid code duplication.

void sixaxis_init( void )
{
	// gyro soft reset
	mpu_writereg( 107, 128 );

	delay( 40000 ); // 30 ms gyro start up time, 10 ms PLL settling time

	// disable I2C
	mpu_writereg( 106, 0x10 );

	// set pll to 1, clear sleep bit old type gyro (mpu-6050)
	mpu_writereg( 107, 1 );

	const int id = mpu_readreg( 117 );
	const int newboard = id != 0x68;

#ifdef GYRO_CHECK
	extern void failloop( int );
	if ( id != GYRO_ID_1 && id != GYRO_ID_2 && id != GYRO_ID_3 && id != GYRO_ID_4 ) {
		failloop( 4 );
	}
#endif

	delay( 100 );

	mpu_writereg( 28, B00011000 ); // 16G scale
	mpu_writereg( 25, B00000000 ); // Sample Rate = Gyroscope Output Rate (default)

	// acc lpf for the new gyro type
	// 0-6 (same as gyro)
	if ( newboard ) {
		mpu_writereg( 29, ACC_LOW_PASS_FILTER );
	}

	// gyro scale 2000 deg (FS =3)
	mpu_writereg( 27, 24 );

	// Gyro DLPF low pass filter
	mpu_writereg( 26, GYRO_LOW_PASS_FILTER );
}

float accel[ 3 ];
float gyro[ 3 ];
float gyro_unfiltered[ 3 ];

float accelcal[ 3 ];
float gyrocal[ 3 ];

int calibration_done;

void sixaxis_read( void )
{
	uint32_t data[ 16 ];

	mpu_readdata( 59, data, 14 );

#ifdef SENSOR_ROTATE_90_CW
	accel[ 0 ] = (int16_t)( ( data[ 2 ] << 8 ) + data[ 3 ] );
	accel[ 1 ] = -(int16_t)( ( data[ 0 ] << 8 ) + data[ 1 ] );
	accel[ 2 ] = (int16_t)( ( data[ 4 ] << 8 ) + data[ 5 ] );
#else
	accel[ 0 ] = -(int16_t)( ( data[ 0 ] << 8 ) + data[ 1 ] );
	accel[ 1 ] = -(int16_t)( ( data[ 2 ] << 8 ) + data[ 3 ] );
	accel[ 2 ] = (int16_t)( ( data[ 4 ] << 8 ) + data[ 5 ] );
#endif

#ifdef SENSOR_ROTATE_90_CW_deleted
{
	float temp = accel[ 1 ];
	accel[ 1 ] = accel[ 0 ];
	accel[ 0 ] = -temp;
}
#endif

// this is the value of both cos 45 and sin 45 = 1/sqrt(2)
#define INVSQRT2 0.707106781f

#ifdef SENSOR_ROTATE_45_CCW
{
	float temp = accel[ 0 ];
	accel[ 0 ] = (accel[ 0 ] * INVSQRT2 + accel[ 1 ] * INVSQRT2 );
	accel[ 1 ] = -( temp * INVSQRT2 - accel[ 1 ] * INVSQRT2 );
}
#endif

#ifdef SENSOR_ROTATE_45_CW
{
	float temp = accel[ 1 ];
	accel[ 1 ] = (accel[ 1 ] * INVSQRT2 + accel[ 0 ] * INVSQRT2 );
	accel[ 0 ] = -( temp * INVSQRT2 - accel[ 0 ] * INVSQRT2 );
}
#endif

#ifdef SENSOR_ROTATE_90_CCW
{
	float temp = accel[ 1 ];
	accel[ 1 ] = -accel[ 0 ];
	accel[ 0 ] = temp;
}
#endif

#ifdef SENSOR_ROTATE_180
{
	accel[ 1 ] = -accel[ 1 ];
	accel[ 0 ] = -accel[ 0 ];
}
#endif

#ifdef SENSOR_FLIP_180
{
	accel[ 2 ] = -accel[ 2 ];
	accel[ 0 ] = -accel[ 0 ];
}
#endif

	float gyronew[ 3 ];
	//order
	gyronew[ 1 ] = (int16_t)( ( data[8] << 8 ) + data[9] );
	gyronew[ 0 ] = (int16_t)( ( data[10] << 8 ) + data[11] );
	gyronew[ 2 ] = (int16_t)( ( data[12] << 8 ) + data[13] );

	process_gyronew_to_gyro( gyronew );
}

void gyro_read( void )
{
	uint32_t data[ 6 ];

	mpu_readdata( 67, data, 6 );

	float gyronew[ 3 ];
	// order
	gyronew[ 1 ] = (int16_t)( ( data[ 0 ] << 8 ) + data[ 1 ] );
	gyronew[ 0 ] = (int16_t)( ( data[ 2 ] << 8 ) + data[ 3 ] );
	gyronew[ 2 ] = (int16_t)( ( data[ 4 ] << 8 ) + data[ 5 ] );

	process_gyronew_to_gyro( gyronew );
}

static void process_gyronew_to_gyro( float gyronew[] )
{
	gyronew[ 0 ] = gyronew[ 0 ] - gyrocal[ 0 ];
	gyronew[ 1 ] = gyronew[ 1 ] - gyrocal[ 1 ];
	gyronew[ 2 ] = gyronew[ 2 ] - gyrocal[ 2 ];

#ifdef SENSOR_ROTATE_45_CCW
{
	float temp = gyronew[ 1 ];
	gyronew[ 1 ] = gyronew[ 0 ] * INVSQRT2 + gyronew[ 1 ] * INVSQRT2;
	gyronew[ 0 ] = gyronew[ 0 ] * INVSQRT2 - temp * INVSQRT2;
}
#endif

#ifdef SENSOR_ROTATE_45_CW
{
	float temp = gyronew[ 0 ];
	gyronew[ 0 ] = gyronew[ 1 ] * INVSQRT2 + gyronew[ 0 ] * INVSQRT2;
	gyronew[ 1 ] = gyronew[ 1 ] * INVSQRT2 - temp * INVSQRT2;
}
#endif

#ifdef SENSOR_ROTATE_90_CW
{
	float temp = gyronew[ 1 ];
	gyronew[ 1 ] = -gyronew[ 0 ];
	gyronew[ 0 ] = temp;
}
#endif

#ifdef SENSOR_ROTATE_90_CCW
{
	float temp = gyronew[ 1 ];
	gyronew[ 1 ] = gyronew[ 0 ];
	gyronew[ 0 ] = -temp;
}
#endif

#ifdef SENSOR_ROTATE_180
{
	gyronew[ 1 ] = -gyronew[ 1 ];
	gyronew[ 0 ] = -gyronew[ 0 ];
}
#endif

#ifdef SENSOR_FLIP_180
{
	gyronew[ 1 ] = -gyronew[ 1 ];
	gyronew[ 2 ] = -gyronew[ 2 ];
}
#endif

	//gyronew[ 0 ] = -gyronew[ 0 ];
	gyronew[ 1 ] = -gyronew[ 1 ];
	gyronew[ 2 ] = -gyronew[ 2 ];

	// 16 bit, +-2000°/s -> 4000°/s / 2**16 * reading = °/s
	for ( int i = 0; i < 3; ++i ) {
		gyronew[ i ] = gyronew[ i ] * 0.061035156f * DEGTORAD;
#ifndef GYRO_FILTER_NONE
		gyro[ i ] = gyro_filter( gyronew[ i ], i );
#else
		gyro[ i ] = gyronew[ i ];
#endif
		gyro_unfiltered[ i ] = gyronew[ i ];
	}
}

#define CAL_TIME 2.5e6f

void gyro_cal( void )
{
	uint32_t data[ 6 ];
	float limit[ 3 ];
	uint32_t time = gettime();
	uint32_t timestart = time;
	uint32_t timemax = time;

	float gyro[ 3 ];

	for ( int i = 0; i < 3; i++) {
		limit[ i ] = gyrocal[ i ];
	}

	// 2.5 and 15 seconds
	while ( time - timestart < CAL_TIME && time - timemax < 15e6f ) {
		mpu_readdata( 67, data, 6 );

		gyro[ 1 ] = (int16_t)( ( data[ 0 ] << 8 ) + data[ 1 ] );
		gyro[ 0 ] = (int16_t)( ( data[ 2 ] << 8 ) + data[ 3 ] );
		gyro[ 2 ] = (int16_t)( ( data[ 4 ] << 8 ) + data[ 5 ] );

		#define GLOW_TIME 62500
		static int brightness = 0;
		led_pwm( brightness );
		if ( ( brightness & 1 ) ^ ( ( time - timestart ) % GLOW_TIME > ( GLOW_TIME >> 1 ) ) ) {
			++brightness;
		}

		brightness &= 0xF;

		for ( int i = 0; i < 3; ++i ) {
			if ( gyro[ i ] > limit[ i ] ) { limit[ i ] += 0.1f; } // 100 gyro bias / second change
			if ( gyro[ i ] < limit[ i ] ) { limit[ i ] -= 0.1f; }

			limitf( &limit[ i ], 800 );

			#define ALLOWED_MOTION_LIMIT 100
			if ( fabsf( gyro[ i ] ) > ALLOWED_MOTION_LIMIT + fabsf( limit[ i ] ) ) {
				timestart = gettime();
				brightness = 1;
			} else {
				lpf( &gyrocal[ i ], gyro[ i ], FILTERCALC( LOOPTIME, 6 * 0.5f * 1e6f ) ); // 6 * because FILTERCALC uses 6 instead of 2 * pi
			}
		}

		// receiver function
		void checkrx( void );
		checkrx();

		while ( gettime() - time < LOOPTIME );
		time = gettime();
	}

	if ( time - timestart < CAL_TIME ) {
		for ( int i = 0; i < 3; i++ ) {
			gyrocal[ i ] = 0;
		}
	}

	calibration_done = 1;
}

void acc_cal( void )
{
	accelcal[ 2 ] = 2048;
	for ( int i = 0; i < 100; ++i ) {
		sixaxis_read();
		#define DELAYTIME 1000 // The accelerometer is updated only at 1 kHz.
		for ( int x = 0; x < 3; ++x ) {
			lpf( &accelcal[ x ], accel[ x ], FILTERCALC( DELAYTIME, 0.072e6f ) );
		}
		delay( DELAYTIME );
		gettime(); // if it takes too long time will overflow so we call it here
	}
	accelcal[ 2 ] -= 2048;

	for ( int x = 0; x < 3; ++x ) {
		limitf( &accelcal[ x ], 500 );
	}
}
