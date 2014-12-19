// Komunikacja z kontrolerem serwomechanizmów SK18 firmy SOMMER TECHNOLOGIES
// www.sommertech.pl

#include<string.h>

int number;
ushort position;
int speed;
int posHigh;
int posLow;

/*
 #define uint8_t unsigned char
 uint8_t packet[4];
 */

unsigned char packet[4];
/* warning: return type defaults to ‘int’ */
int CalculatePacket( number, position, speed) {
	packet[0] = 255;
	packet[1] = number;
	posHigh = (position >> 2);
	/*przesunięcie bitów o 2 w prawo*/
	posLow = (position << 6);
	/*przesunięcie bitów o X w lewo*/
	packet[2] = posHigh;
	packet[3] = (posLow | speed);

	return packet[4];
}
